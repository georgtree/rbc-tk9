/*
 * rbcVector.c --
 *
 *      TODO: Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "tcl.h"
#include "rbcVectorInt.h"

typedef int RbcVectorOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcVectorOp *proc;
} VectorOpSpec;

static Tcl_ObjCmdProc2 VectorObjCmd;
static Tcl_ObjCmdProc2 VectorCreateObjCmd;
static Tcl_ObjCmdProc2 VectorDestroyObjCmd;
static Tcl_ObjCmdProc2 VectorExprObjCmd;
static Tcl_ObjCmdProc2 VectorNamesObjCmd;

static Tcl_CmdDeleteProc VectorInstDeleteProc;

static Tcl_InterpDeleteProc VectorInterpDeleteProc;

static void VectorNotifyClients(ClientData clientData);
static void VectorFlushCache(VectorObject *vPtr);
static char *VectorVarTrace(ClientData clientData, Tcl_Interp *interp, char *part1, char *part2, int flags);
static char *BuildQualifiedName(Tcl_Interp *interp, const char *name, Tcl_DString *fullName);
static int ParseQualifiedName(Tcl_Interp *interp, const char *qualName, Tcl_Namespace **nsPtrPtr,
                              const char **namePtrPtr);
static char *GetQualifiedName(Tcl_Namespace *nsPtr, const char *name, Tcl_DString *resultPtr);
static VectorObject *GetVectorObject(VectorInterpData *dataPtr, const char *name, int flags);
static VectorObject *FindVectorInNamespace(VectorInterpData *dataPtr, Tcl_Namespace *nsPtr, const char *vecName);
static void DeleteCommand(VectorObject *vPtr);
static void UnmapVariable(VectorObject *vPtr);

double rbcNaN;

#ifdef __BORLANDC__
static double MakeNaN(void) {
    union Real {
        struct DoubleWord {
            int lo, hi;
        } doubleWord;
        double number;
    } real;

    real.doubleWord.lo = real.doubleWord.hi = 0x7FFFFFFF;
    return real.number;
}
#endif /* __BORLANDC__ */

#ifdef _MSC_VER
static double MakeNaN(void) { return sqrt(-1.0); /* Generate IEEE 754 Quiet Not-A-Number. */ }
#endif /* _MSC_VER */

#if !defined(__BORLANDC__) && !defined(_MSC_VER)
static double MakeNaN(void) {
    /* copied over from generic/tclStrToD.c */
#define NAN_START 0x7FF8
#define NAN_MASK (((Tcl_WideUInt)1) << 51)
    int signum = 0; /* Sign bit (1=negative, 0=nonnegative. */
    union {
        Tcl_WideUInt iv;
        double dv;
    } theNaN;

    theNaN.iv = 0;
    theNaN.iv &= NAN_MASK - 1;
    if (signum) {
        theNaN.iv |= ((Tcl_WideUInt)(0x8000 | NAN_START)) << 48;
    } else {
        theNaN.iv |= ((Tcl_WideUInt)NAN_START) << 48;
    }
    return theNaN.dv;

    /* Generate IEEE 754 Not-A-Number. */
    //    return 0.0 / 0.0;
}
#endif /* !__BORLANDC__  && !_MSC_VER */

static size_t VectorElementSize(Rbc_VectorType type) {
    switch (type) {
    case RBC_VECTOR_REAL:
        return sizeof(double);
    case RBC_VECTOR_COMPLEX:
        return sizeof(Rbc_Complex);
    }
    Tcl_Panic("bad vector type %d", (int)type);
    return 0;
}

static int GetVectorByteCount(Tcl_Interp *interp, Rbc_VectorType type, Tcl_Size count, size_t *byteCountPtr) {
    size_t elemSize;

    if (count < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector size cannot be negative", -1));
        return TCL_ERROR;
    }
    elemSize = VectorElementSize(type);
    if (elemSize == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown vector type", -1));
        return TCL_ERROR;
    }
    if ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / elemSize)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector size is too large", -1));
        return TCL_ERROR;
    }
    *byteCountPtr = (size_t)count * elemSize;
    return TCL_OK;
}

static char *VectorTraceError(Tcl_Obj *objPtr) {
    Tcl_IncrRefCount(objPtr);
    return (char *)objPtr;
}

int Rbc_VectorGetRange(Rbc_Vector *vecPtr, double *minPtr, double *maxPtr) {
    VectorObject *vPtr;

    vPtr = (VectorObject *)vecPtr;
    if (vPtr->type != RBC_VECTOR_REAL) {
        return TCL_ERROR;
    }
    /*
     * min/max are cached in the vector.  Recompute both in one pass
     * only when source data have invalidated that cache.
     */
    if (vPtr->notifyFlags & UPDATE_RANGE) {
        Rbc_VectorUpdateRange(vPtr);
    }
    *minPtr = vPtr->min;
    *maxPtr = vPtr->max;
    return TCL_OK;
}

static void ZeroVectorElements(Rbc_VectorType type, VectorData data, Tcl_Size first, Tcl_Size last) {
    Tcl_Size i;

    switch (type) {
    case RBC_VECTOR_REAL:
        for (i = first; i < last; i++) {
            data.real[i] = 0.0;
        }
        break;
    case RBC_VECTOR_COMPLEX:
        for (i = first; i < last; i++) {
            data.complex[i].real = 0.0;
            data.complex[i].imag = 0.0;
        }
        break;
    }
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_VectorInit --
 *
 *      This procedure is invoked to initialize the "vector" command.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates the new command and adds a new entry into a global Tcl
 *      associative array.
 *
 * ------------------------------------------------------------------------
 */
int Rbc_VectorInit(Tcl_Interp *interp) {
    VectorInterpData *dataPtr; /* Interpreter-specific data. */
    rbcNaN = MakeNaN();

    dataPtr = Rbc_VectorGetInterpData(interp);
    Tcl_CreateObjCommand2(interp, "rbc::vector", VectorObjCmd, dataPtr, NULL);

    return TCL_OK;
}

static const VectorOpSpec vectorOpCmd[] = {{{"create", 2, 0, "?vecName? ?switches...?"}, VectorCreateObjCmd},
                                           {{"destroy", 2, 0, "?vecName?..."}, VectorDestroyObjCmd},
                                           {{"expr", 3, 3, "expression"}, VectorExprObjCmd},
                                           {{"names", 2, 3, "?pattern?..."}, VectorNamesObjCmd},
                                           {{NULL, 0, 0, NULL}, NULL}};

/*
 * ------------------------------------------------------------------------
 *
 * VectorObjCmd --
 *
 *      This implements the Tcl vector command from the rbc package.
 *      See the user documentation on what is does.
 *
 * Parameters:
 *      ClientData dataPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      Do some user action.
 *
 * ------------------------------------------------------------------------
 */
static int VectorObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, vectorOpCmd, (Tcl_Size)sizeof(vectorOpCmd[0]), RBC_OP_ARG1, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }

    return vectorOpCmd[index].proc(clientData, interp, objc, objv);
}

/*
 * Helper function for Tcl_ParseArgsObjv in VectorCreateObjCmd.
 */
static Tcl_Size ParseBool(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], void *dstPtr) {
    int value;
    int *dst;
    const char *optionName;

    dst = (int *)dstPtr;
    optionName = (const char *)clientData;
    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("option \"%s\" requires a boolean value", optionName));
        return -1;
    }
    if (Tcl_GetBooleanFromObj(interp, objv[0], &value) != TCL_OK) {
        return -1;
    }
    *dst = value;
    return 1;
}

static Tcl_Size ParseVectorLength(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[],
                                  void *dstPtr) {
    Tcl_Size value;
    const char *optionName;

    optionName = (const char *)clientData;
    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("option \"%s\" requires an additional argument", optionName));
        return -1;
    }
    if (Tcl_GetSizeIntFromObj(interp, objv[0], &value) != TCL_OK) {
        return -1;
    }
    *(Tcl_Size *)dstPtr = value;
    return 1;
}

static int GetSizeFromString(Tcl_Interp *interp, const char *string, Tcl_Size *valuePtr) {
    Tcl_Obj *objPtr;
    int result;

    objPtr = Tcl_NewStringObj(string, -1);
    Tcl_IncrRefCount(objPtr);
    result = Tcl_GetSizeIntFromObj(interp, objPtr, valuePtr);
    Tcl_DecrRefCount(objPtr);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * VectorCreateObjCmd --
 *
 *      processes the Tcl 'vector create' command, and calls
 *      vectorCreate to actually create the vector
 *
 *        vector create a
 *        vector create b(20)
 *        vector create c(-5:14)
 *
 * Parameters:
 *      ClientData clientData - Vector interp data
 *      Tcl_Interp *interp - Interp to return results to
 *      int objc - argument count
 *      Tcl_Obj * const objv[] - arguments to the command
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *----------------------------------------------------------------------
 */
typedef struct {
    Rbc_VectorType type;
    int specified;
} VectorTypeOption;

static Tcl_Size ParseVectorType(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[],
                                void *dstPtr) {
    VectorTypeOption *optionPtr;
    const char *string;
    const char *optionName;

    optionPtr = (VectorTypeOption *)dstPtr;
    optionName = (const char *)clientData;

    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("option \"%s\" requires an additional argument", optionName));
        return -1;
    }

    string = Tcl_GetString(objv[0]);

    if (strcmp(string, "real") == 0) {
        optionPtr->type = RBC_VECTOR_REAL;
    } else if (strcmp(string, "complex") == 0) {
        optionPtr->type = RBC_VECTOR_COMPLEX;
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad value \"%s\" for -type: must be real or complex", string));
        return -1;
    }

    optionPtr->specified = TRUE;
    return 1;
}

static int VectorCreateObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorInterpData *dataPtr = clientData;
    VectorObject *vPtr;
    Tcl_Obj *resultPtr; /* for the result of this function */
    char *cmdName, *varName;
    VectorTypeOption typeOption;
    int freeOnUnset;
    int flush;
    Tcl_Size defLen;
    Tcl_Obj **objNameArray; /* holds all vector names specified */
    Tcl_Size count;
    Tcl_DString ds;
    Tcl_Size i;
    const Tcl_ArgvInfo argsTable[] = {{TCL_ARGV_STRING, "-command", NULL, &cmdName, NULL, NULL},
                                      {TCL_ARGV_GENFUNC, "-flush", ParseBool, &flush, NULL, "-flush"},
                                      {TCL_ARGV_GENFUNC, "-length", ParseVectorLength, &defLen, NULL, "-length"},
                                      {TCL_ARGV_GENFUNC, "-type", ParseVectorType, &typeOption, NULL, "-type"},
                                      {TCL_ARGV_STRING, "-variable", NULL, &varName, NULL, NULL},
                                      {TCL_ARGV_GENFUNC, "-watchunset", ParseBool, &freeOnUnset, NULL, "-watchunset"},
                                      TCL_ARGV_TABLE_END};

    /*
     * Handle switches to the vector command and collect the vector
     * name arguments into an array.
     */
    varName = NULL;  /* name of Tcl variable to link to the vector */
    cmdName = NULL;  /* name of Tcl command to link to vector */
    freeOnUnset = 0; /* value of the user level '-watchunset' switch */
    defLen = 0;      /* default vector length */
    flush = FALSE;
    typeOption.type = RBC_VECTOR_REAL;
    typeOption.specified = FALSE;

    count = objc - 1; /* start at "create" */
    if (Tcl_ParseArgsObjv(interp, argsTable, &count, objv + 1, &objNameArray)) {
        return TCL_ERROR;
    }
    /* finished parsing arguments -> do some sanity checks: */
    Tcl_DStringInit(&ds);
    resultPtr = Tcl_NewObj();

    if (defLen < 0) {
        Tcl_AppendStringsToObj(resultPtr,
                               "value for \"-length\" option "
                               "must be zero or greater",
                               NULL);
        Tcl_SetObjResult(interp, resultPtr);
        goto error;
    }
    if (count == 1) {
        /* T_PAO always returns at least 1, "create" */
        Tcl_AppendStringsToObj(resultPtr, "no vector names supplied", NULL);
        Tcl_SetObjResult(interp, resultPtr);
        goto error;
    } else if (count > 2) {
        /* more than one vector was specified */
        if ((cmdName != NULL) && (cmdName[0] != '\0')) {
            Tcl_AppendStringsToObj(resultPtr, "can't specify more than one vector with \"-command\" switch", NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
        if ((varName != NULL) && (varName[0] != '\0')) {
            Tcl_AppendStringsToObj(resultPtr, "can't specify more than one vector with \"-variable\" switch", NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
    }

    /* Now process the vector names and check their validity
     * Use a Tcl_DString as workhorse since this code writes to the
     * char buffer.
     * Also, we start at 1 since objNameArray[0] holds "create"
     */
    vPtr = NULL;
    for (i = 1; i < count; i++) {
        char *leftParen, *rightParen; /* positions of left and right parens in vector specification */
        int isNew;
        Tcl_Size size;
        Tcl_Size first;
        Tcl_Size last;
        char *vecName; /* name of a vector */
        const char *createVarName;
        VectorObject *existingPtr;
        Rbc_VectorType effectiveType;

        Tcl_DStringFree(&ds);
        Tcl_DStringAppend(&ds, Tcl_GetString(objNameArray[i]), -1);
        vecName = Tcl_DStringValue(&ds);
        size = defLen; /* set to default value */
        first = last = 0;

        leftParen = strchr(vecName, '(');
        rightParen = strchr(vecName, ')');
        if (((leftParen != NULL) && (rightParen == NULL)) || ((leftParen == NULL) && (rightParen != NULL)) ||
            (leftParen > rightParen)) {
            Tcl_AppendStringsToObj(resultPtr, "bad vector specification \"", vecName, "\"", NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
        if (leftParen != NULL) {
            int result;
            char *colon;

            *rightParen = '\0';
            colon = strchr(leftParen + 1, ':');
            if (colon != NULL) {
                /* Specification is in the form vecName(first:last) */
                *colon = '\0';
                result = GetSizeFromString(interp, leftParen + 1, &first);
                if ((*(colon + 1) != '\0') && (result == TCL_OK)) {
                    result = GetSizeFromString(interp, colon + 1, &last);
                    if (first > last) {
                        Tcl_AppendStringsToObj(resultPtr, "bad vector range \"", vecName, "\"", NULL);
                        Tcl_SetObjResult(interp, resultPtr);
                        result = TCL_ERROR;
                    } else if ((first < 0) && (last >= (TCL_SIZE_MAX + first))) {
                        Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector range \"%s\" is too large", vecName));
                        result = TCL_ERROR;
                    } else {
                        Tcl_Size difference;
                        difference = last - first;
                        if (difference == TCL_SIZE_MAX) {
                            Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector range \"%s\" is too large", vecName));
                            result = TCL_ERROR;
                        } else {
                            size = difference + 1;
                        }
                    }
                }
                *colon = ':';
            } else {
                /* Specification is in the form vecName(size) */
                result = GetSizeFromString(interp, leftParen + 1, &size);
            }
            *rightParen = ')';
            if (result != TCL_OK) {
                goto error;
            }
            if (size < 0) {
                Tcl_AppendStringsToObj(resultPtr, "bad vector size \"", vecName, "\"", NULL);
                Tcl_SetObjResult(interp, resultPtr);
                goto error;
            }
        }
        if (leftParen != NULL) {
            *leftParen = '\0';
        }
        existingPtr = GetVectorObject(dataPtr, vecName, NS_SEARCH_BOTH);
        if (existingPtr != NULL) {
            if (typeOption.specified && (existingPtr->type != typeOption.type)) {

                Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't change vector \"%s\" from type \"%s\" to \"%s\"",
                                                       existingPtr->name,
                                                       (existingPtr->type == RBC_VECTOR_REAL) ? "real" : "complex",
                                                       (typeOption.type == RBC_VECTOR_REAL) ? "real" : "complex"));
                goto error;
            }
            effectiveType = existingPtr->type;
        } else {
            effectiveType = typeOption.type;
        }
        /*
         * actually create the vector:
         */
        if (effectiveType == RBC_VECTOR_COMPLEX) {
            /*
             * Complex vectors don't receive an automatic Tcl array mapping,
             * but an explicitly requested mapping is allowed.
             */
            createVarName = (varName == NULL) ? NULL : varName;
        } else {
            createVarName = (varName == NULL) ? vecName : varName;
        }
        vPtr = Rbc_VectorCreate(dataPtr, vecName, (cmdName == NULL) ? vecName : cmdName, createVarName, effectiveType,
                                &isNew);
        if (leftParen != NULL) {
            *leftParen = '(';
        }
        if (vPtr == NULL) {
            goto error;
        }
        vPtr->freeOnUnset = freeOnUnset;
        vPtr->flush = flush;
        vPtr->offset = first;
        if (size > 0) {
            if (Rbc_VectorChangeLength(vPtr, size) != TCL_OK) {
                goto error;
            }
        }
        if (!isNew) {
            if (vPtr->flush) {
                VectorFlushCache(vPtr);
            }
            Rbc_VectorUpdateClients(vPtr);
        }
    }

    ckfree(objNameArray);
    Tcl_DStringFree(&ds);
    if (vPtr != NULL) {
        /* Return the name of the last vector created  */
        Tcl_AppendStringsToObj(resultPtr, vPtr->name, NULL);
    }
    Tcl_SetObjResult(interp, resultPtr);
    return TCL_OK;

error:
    ckfree(objNameArray);
    Tcl_DStringFree(&ds);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * VectorDestroyObjCmd --
 *
 *      processes the Tcl 'vector destroy' command
 *
 * Parameters:
 *      ClientData clientData - Interpreter-specific data.
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *----------------------------------------------------------------------
 */
static int VectorDestroyObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    /* Not Implemented Correctly */

    VectorInterpData *dataPtr = clientData;
    VectorObject *vPtr;
    Tcl_Size i;
    for (i = 2; i < objc; i++) {
        if (Rbc_VectorLookupName(dataPtr, Tcl_GetString(objv[i]), &vPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        Rbc_VectorFree(vPtr);
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * VectorExprObjCmd --
 *
 *      Computes the result of the expression which may be
 *      either a scalar (single value) or vector (list of values).
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int VectorExprObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_ExprVector(interp, Tcl_GetString(objv[2]), (Rbc_Vector *)NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * VectorNamesObjCmd --
 *
 *      Reports the names of all the current vectors in the
 *      interpreter.
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result. The interpreter result will contain a
 *      list of all the names of the vector instances.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int VectorNamesObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorInterpData *dataPtr = clientData;
    Tcl_HashEntry *hPtr;
    const char *name;
    Tcl_HashSearch cursor;
    Tcl_Obj *resultPtr;

    resultPtr = Tcl_NewListObj(0, NULL);
    for (hPtr = Tcl_FirstHashEntry(&(dataPtr->vectorTable), &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        name = Tcl_GetHashKey(&(dataPtr->vectorTable), hPtr);
        if ((objc == 2) || (Tcl_StringMatch(name, Tcl_GetString(objv[2])))) {
            Tcl_ListObjAppendElement(interp, resultPtr, Tcl_NewStringObj(name, -1));
        }
    }
    Tcl_SetObjResult(interp, resultPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 *Rbc_VectorGetInterpData --
 *
 *      Gathers the data need for the vector interpreter to function.
 *      It then stores it into the vector interpreter.
 *
 * Parameters:
 *      Tcl_Interp *interp - Base interpreter to wrap.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates the new command and adds a new entry into a global Tcl
 *      associative array.
 *
 * ------------------------------------------------------------------------
 */

VectorInterpData *Rbc_VectorGetInterpData(Tcl_Interp *interp) {
    VectorInterpData *dataPtr;
    Tcl_InterpDeleteProc *proc;

    dataPtr = (VectorInterpData *)Tcl_GetAssocData(interp, VECTOR_THREAD_KEY, &proc);
    if (dataPtr == NULL) {
        dataPtr = (VectorInterpData *)ckalloc(sizeof(VectorInterpData));
        /***    assert(dataPtr); */
        dataPtr->interp = interp;
        dataPtr->nextId = 0;
        Tcl_SetAssocData(interp, VECTOR_THREAD_KEY, VectorInterpDeleteProc, dataPtr);
        Tcl_InitHashTable(&(dataPtr->vectorTable), TCL_STRING_KEYS);
        Tcl_InitHashTable(&(dataPtr->mathProcTable), TCL_STRING_KEYS);
        Tcl_InitHashTable(&(dataPtr->indexProcTable), TCL_STRING_KEYS);
        Rbc_VectorInstallMathFunctions(&(dataPtr->mathProcTable));
        Rbc_VectorInstallSpecialIndices(&(dataPtr->indexProcTable));
#ifdef HAVE_SRAND48
//        what does this do??
//        srand48(time((time_t *) NULL));
#endif
    }
    return dataPtr;
}

/*
 * -----------------------------------------------------------------------
 *
 * VectorInterpDeleteProc --
 *
 *      This is called when the interpreter hosting the "vector"
 *      command is deleted.
 *
 * Parameters:
 *      ClientData clientData - Interpreter Specific
 *      Tcl_Interp *interp
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys the math and index hash tables.  In addition removes
 *      the hash table managing all vector names.
 *
 * ------------------------------------------------------------------------
 */
static void VectorInterpDeleteProc(ClientData clientData, Tcl_Interp *interp) {
    VectorInterpData *dataPtr = clientData;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    VectorObject *vPtr;

    for (hPtr = Tcl_FirstHashEntry(&(dataPtr->vectorTable), &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        vPtr = (VectorObject *)Tcl_GetHashValue(hPtr);
        vPtr->hashPtr = NULL;
        Rbc_VectorFree(vPtr);
    }
    Tcl_DeleteHashTable(&(dataPtr->vectorTable));

    /* If any user-defined math functions were installed, remove them.  */
    Tcl_DeleteHashTable(&(dataPtr->mathProcTable));

    Tcl_DeleteHashTable(&(dataPtr->indexProcTable));
    Tcl_DeleteAssocData(interp, VECTOR_THREAD_KEY);
    ckfree((char *)dataPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorNew --
 *
 *      Creates a new vector object and populates with the needed data.
 *
 * Parameters:
 *      VectorInterpData *dataPtr - Interpreter-specific data.
 *
 * Results:
 *      A pointer to the new vector object
 *
 * Side effects:
 *      None
 *
 * ---------------------------------------------------------------------- */
VectorObject *Rbc_VectorNew(VectorInterpData *dataPtr) {
    VectorObject *vPtr;

    vPtr = RbcCalloc(1, sizeof(VectorObject));

    vPtr->type = RBC_VECTOR_REAL;

    vPtr->notifyFlags = NOTIFY_WHENIDLE | UPDATE_RANGE;
    vPtr->freeProc = TCL_STATIC;
    vPtr->dataPtr = dataPtr;
    vPtr->data.raw = NULL;
    vPtr->length = vPtr->size = 0;
    vPtr->interp = dataPtr->interp;
    vPtr->hashPtr = NULL;
    vPtr->chainPtr = Rbc_ChainCreate();
    vPtr->flush = FALSE;
    vPtr->min = vPtr->max = rbcNaN;

    return vPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorCreate --
 *
 *      Actually creates a vector structure and the following items:
 *
 *        o Tcl command
 *        o Tcl array variable and establishes traces on the variable
 *        o Adds a  new entry in the vector hash table
 *
 * Parameters:
 *      VectorInterpData *dataPtr - Interpreter-specific data (clientData).
 *      const char *vecName - Name of the vector
 *      const char *cmdName - Name of the Tcl command mapped to the vector; if NULL (actually '\0') then do not create 
 *                            a command
 *      const char *varName - Name of the Tcl array mapped to the vector; if NULL (actually '\0') then do not create a 
 *                            variable 
 *      int *newPtr - pointer to the vector created
 *
 * Results:
 *      A pointer to the new vector structure.  If an error occurred
 *      NULL is returned and an error message is left in
 *      the interpreter result.
 *
 * Side effects:
 *      A new Tcl command and array variable is added to the
 *      interpreter.
 *
 * ---------------------------------------------------------------------- */
VectorObject *Rbc_VectorCreate(VectorInterpData *dataPtr, const char *vecName, const char *cmdName, const char *varName,
                               Rbc_VectorType type, int *newPtr) {
    Tcl_Obj *resultPtr = Tcl_NewStringObj("", -1);
    VectorObject *vPtr;
    int isNew;
    int isAutoName = 0;       /* is the name autmatically generated? */
    char *qualVecName = NULL; /* qualified name of the vector */
    const char *vecNameTail;  /* the name of the vector without namespace */
    Tcl_Namespace *nsPtr;     /* namespace of the vector name */
    Tcl_HashEntry *hPtr;
    Tcl_Interp *interp = dataPtr->interp;
    Tcl_DString qualVecNamePtr;

    isNew = 0;
    nsPtr = NULL;
    vPtr = NULL;

    /* process the vector name: */
    vecName = BuildQualifiedName(interp, vecName, &qualVecNamePtr);
    if (ParseQualifiedName(interp, vecName, &nsPtr, &vecNameTail) != TCL_OK) {
        Tcl_AppendStringsToObj(resultPtr, "unknown namespace in \"", vecName, "\"", NULL);
        Tcl_SetObjResult(interp, resultPtr);
        return NULL;
    }

    if ((vecNameTail[0] == '#') && (strcmp(vecNameTail, "#auto") == 0)) {
        /* generate a unique automatic name for the vector: */
        char string[200];

        do {
            snprintf(string, sizeof(string), "vector%d", dataPtr->nextId++);
            qualVecName = GetQualifiedName(nsPtr, string, &qualVecNamePtr);
            hPtr = Tcl_FindHashEntry(&(dataPtr->vectorTable), qualVecName);
        } while (hPtr != NULL);
        isAutoName = 1;
    } else {
        /* check correct vector name syntax: */
        register const char *p;

        for (p = vecNameTail; *p != '\0'; p++) {
            if (!VECTOR_CHAR(*p)) {
                Tcl_AppendStringsToObj(resultPtr, "bad vector name \"", vecName,
                                       "\": must contain digits, letters, underscore, or period", NULL);
                Tcl_SetObjResult(interp, resultPtr);
                goto error;
            }
        }
        qualVecName = (char *)vecName;
        vPtr = Rbc_VectorParseElement(NULL, dataPtr, qualVecName, NULL, NS_SEARCH_CURRENT);
    }

    /*
     * A vector's numeric type is immutable.  Only reject a mismatch
     * when -type was explicitly supplied; an omitted -type preserves
     * the type of an existing vector.
     */
    if (vPtr == NULL) {
        hPtr = Tcl_CreateHashEntry(&(dataPtr->vectorTable), qualVecName, &isNew);

        vPtr = Rbc_VectorNew(dataPtr);
        vPtr->type = type;

        vPtr->hashPtr = hPtr;
        vPtr->name = Tcl_GetHashKey(&(dataPtr->vectorTable), hPtr);

        Tcl_SetHashValue(hPtr, vPtr);
    } else if (vPtr->type != type) {
        /*
         * Defensive internal invariant.  The Tcl create command should
         * already have resolved the effective type.
         */
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector type mismatch", -1));
        goto error;
    }

    /* process the command name: */
    if (cmdName != NULL) {
        Tcl_CmdInfo cmdInfo;

        if (isAutoName) {
            cmdName = qualVecName;
        } else {
            cmdName = BuildQualifiedName(interp, cmdName, &qualVecNamePtr);
        }
        nsPtr = NULL;
        vecNameTail = NULL;
        if (ParseQualifiedName(interp, cmdName, &nsPtr, &vecNameTail) != TCL_OK) {
            Tcl_AppendStringsToObj(resultPtr, "unknown namespace in \"", cmdName, "\"", NULL);
            Tcl_SetObjResult(interp, resultPtr);
            return NULL;
        }

        if (Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
            /*
             * Vector instance commands are registered through
             * Tcl_CreateObjCommand2(), so their original callback and client
             * data are stored in objProc2 and objClientData2.
             */
            if ((cmdInfo.isNativeObjectProc != 2) || (cmdInfo.objProc2 != Rbc_VectorInstanceObjCmd) ||
                (cmdInfo.objClientData2 != vPtr)) {
                Tcl_AppendStringsToObj(resultPtr, "command \"", cmdName, "\" already exists", NULL);

                Tcl_SetObjResult(interp, resultPtr);
                goto error;
            }
        }
    }

    if (vPtr->cmdToken != 0) {
        DeleteCommand(vPtr); /* Command already exists, delete old first */
    }

    if (cmdName != NULL) {
        vPtr->cmdToken = Tcl_CreateObjCommand2(interp, cmdName, Rbc_VectorInstanceObjCmd, vPtr, VectorInstDeleteProc);
    }

    /* process array variable: */
    if (varName != NULL && varName[0] != '\0') {
        if ((varName[0] == '#') && (strcmp(varName, "#auto") == 0)) {
            varName = vPtr->name;
        } else {
            varName = BuildQualifiedName(interp, varName, &qualVecNamePtr);
        }
        if (Rbc_VectorMapVariable(interp, vPtr, varName) != TCL_OK) {
            goto error;
        }
    }

    *newPtr = isNew;
    Tcl_DStringFree(&qualVecNamePtr);
    return vPtr;

error:
    if ((vPtr != NULL) && isNew) {
        Rbc_VectorFree(vPtr);
    }

    Tcl_DStringFree(&qualVecNamePtr);
    return NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorInstDeleteProc --
 *
 *     Deletes the command associated with the vector.  This is
 *     called only when the command associated with the vector is
 *     destroyed.
 *
 * Parameters:
 *      ClientData clientData - Vector object to delete
 *
 * Results:
 *     None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void VectorInstDeleteProc(ClientData clientData) {
    VectorObject *vPtr = clientData;

    vPtr->cmdToken = 0;
    Rbc_VectorFree(vPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorFree --
 *
 *     Removes the memory and frees resources associated with the
 *     vector.
 *
 *        o Removes the trace and the Tcl array variable and unsets
 *          the variable.
 *        o Notifies clients of the vector that the vector is being
 *          destroyed.
 *        o Removes any clients that are left after notification.
 *        o Frees the memory (if necessary) allocated for the array.
 *        o Removes the entry from the hash table of vectors.
 *        o Frees the memory allocated for the name.
 *
 * Parameters:
 *      VectorObject *vPtr - The vector to free
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
void Rbc_VectorFree(VectorObject *vPtr) {
    Rbc_ChainLink *linkPtr;
    VectorClient *clientPtr;

    if (vPtr->cmdToken != 0) {
        DeleteCommand(vPtr);
    }

    if (vPtr->arrayName != NULL) {
        UnmapVariable(vPtr);
    }
    vPtr->length = 0;

    /* Immediately notify clients that vector is going away */
    if (vPtr->notifyFlags & NOTIFY_PENDING) {
        vPtr->notifyFlags &= ~NOTIFY_PENDING;
        Tcl_CancelIdleCall(VectorNotifyClients, vPtr);
    }
    vPtr->notifyFlags |= NOTIFY_DESTROYED;
    VectorNotifyClients(vPtr);

    for (linkPtr = Rbc_ChainFirstLink(vPtr->chainPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        clientPtr = Rbc_ChainGetValue(linkPtr);
        ckfree((char *)clientPtr);
    }
    Rbc_ChainDestroy(vPtr->chainPtr);
    if ((vPtr->data.raw != NULL) && (vPtr->freeProc != TCL_STATIC)) {
        if (vPtr->freeProc == TCL_DYNAMIC) {
            ckfree(vPtr->data.raw);
        } else {
            (*vPtr->freeProc)(vPtr->data.raw);
        }
    }
    if (vPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(vPtr->hashPtr);
    }
#ifdef NAMESPACE_DELETE_NOTIFY
    if (vPtr->nsPtr != NULL) {
        /* Not Implemented Yet */
        /*** Rbc_DestroyNsDeleteNotify(vPtr->interp, vPtr->nsPtr, vPtr); */
    }
#endif /* NAMESPACE_DELETE_NOTIFY */
    ckfree((char *)vPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorDuplicate --
 *
 *      Duplicates all elements of a vector.
 *
 * Parameters:
 *      VectorObject *destPtr
 *      VectorObject *srcPtr
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      New vector is created.
 *
 * ----------------------------------------------------------------------
 */
int Rbc_VectorDuplicate(VectorObject *destPtr, VectorObject *srcPtr) {
    Tcl_Size length;
    size_t byteCount;

    if (destPtr->type != srcPtr->type) {
        Tcl_SetObjResult(destPtr->interp, Tcl_NewStringObj("can't duplicate vectors of different types", -1));
        return TCL_ERROR;
    }
    length = srcPtr->last - srcPtr->first + 1;
    if (GetVectorByteCount(destPtr->interp, srcPtr->type, length, &byteCount) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Rbc_VectorChangeLength(destPtr, length) != TCL_OK) {
        return TCL_ERROR;
    }
    if (byteCount > 0) {
        switch (srcPtr->type) {
        case RBC_VECTOR_REAL:
            memmove(destPtr->data.real, srcPtr->data.real + srcPtr->first, byteCount);
            break;
        case RBC_VECTOR_COMPLEX:
            memmove(destPtr->data.complex, srcPtr->data.complex + srcPtr->first, byteCount);
            break;
        }
    }
    destPtr->offset = srcPtr->offset;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorFlushCache --
 *
 *      Unsets all the elements of the Tcl array variable associated
 *      with the vector, freeing memory associated with the variable.
 *      This includes both the hash table and the hash keys.  The down
 *      side is that this effectively flushes the caching of vector
 *      elements in the array.  This means that the subsequent reads
 *      of the array will require a decimal to string conversion.
 *
 *      This is needed when the vector changes its values, making
 *      the array variable out-of-sync.
 *
 * Parameters:
 *      VectorObject *vPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      All elements of array variable (except one) are unset, freeing
 *      the memory associated with the variable.
 *
 * ----------------------------------------------------------------------
 */
void Rbc_VectorFlushCache(VectorObject *vPtr) {
    Tcl_Interp *interp = vPtr->interp;

    if (vPtr->arrayName == NULL) {
        return; /* Doesn't use the variable API */
    }

    /* Turn off the trace temporarily so that we can unset all the
     * elements in the array.  */

    Tcl_UntraceVar2(interp, vPtr->arrayName, (char *)NULL, TRACE_ALL | vPtr->varFlags,
                    (Tcl_VarTraceProc *)VectorVarTrace, vPtr);

    /* Clear all the element entries from the entire array */
    Tcl_UnsetVar2(interp, vPtr->arrayName, (char *)NULL, vPtr->varFlags);

    /* Restore the "end" index by default and the trace on the entire array */
    Tcl_SetVar2(interp, vPtr->arrayName, "end", "", vPtr->varFlags);
    Tcl_TraceVar2(interp, vPtr->arrayName, (char *)NULL, TRACE_ALL | vPtr->varFlags, (Tcl_VarTraceProc *)VectorVarTrace,
                  vPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorMapVariable --
 *
 *      Sets up traces on a Tcl variable to access the vector.
 *
 *      If another variable is already mapped, it's first untraced and
 *      removed.  Don't do anything else for variables named "" (even
 *      though Tcl allows this pathology). Saves the name of the new
 *      array variable.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      VectorObject *vPtr
 *      const char *name - name of array variable to map to vector
 *
 * Results:
 *      A standard Tcl result. If an error occurs setting the variable
 *      TCL_ERROR is returned and an error message is left in the
 *      interpreter.
 *
 * Side effects:
 *      Traces are set for the new variable. The new variable name is
 *        saved in a malloc'ed string in vPtr->arrayName.  If this
 *        variable is non-NULL, it indicates that a Tcl variable has
 *      been mapped to this vector.
 *
 * ----------------------------------------------------------------------
 */
int Rbc_VectorMapVariable(Tcl_Interp *interp, VectorObject *vPtr, const char *name) {
    const char *result;
    Tcl_Namespace *varNsPtr;

    if (vPtr->arrayName != NULL) {
        UnmapVariable(vPtr);
    }
    if ((name == NULL) || (name[0] == '\0')) {
        /* If the variable name is the empty string, simply return after removing any existing variable. */
        return TCL_OK;
    }

    /*
     * To play it safe, delete the variable first.  This has
     * side-effect of unmapping the variable from any vector that may
     * be currently using it.
     */
    Tcl_UnsetVar2(interp, name, NULL, 0);

    /* Set the index "end" in the array.  This will create the
     * variable immediately so that we can check its namespace
     * context.
     */
    result = Tcl_SetVar2(interp, name, "end", "", TCL_LEAVE_ERR_MSG);

    /* Determine if the variable is global or not.  If there wasn't a
     * namespace qualifier, it still may be global.  We need to look
     * inside the Var structure to see what it's namespace field says.
     * NULL indicates that it's local.
     */
    varNsPtr = Tcl_FindNamespace(interp, name, NULL, 0);
    vPtr->varFlags = (varNsPtr != NULL) ? (TCL_NAMESPACE_ONLY | TCL_GLOBAL_ONLY) : 0;

    if (result != NULL) {
        /* Trace the array on reads, writes, and unsets */
        /*printf("trace on %s with variable %s\n",vPtr->name,name);*/
        Tcl_TraceVar2(interp, name, NULL, (TRACE_ALL | vPtr->varFlags), (Tcl_VarTraceProc *)VectorVarTrace, vPtr);
    }

    vPtr->arrayName = RbcStrdup(name);
    return (result == NULL) ? TCL_ERROR : TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_VectorReset --
 *
 *      Resets the vector data.  This is called by a client to
 *      indicate that the vector data has changed.  The vector does
 *      not need to point to different memory.  Any clients of the
 *      vector will be notified of the change.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      double *valueArr - Array containing the elements of the vector. If NULL, indicates to reset the vector.
 *      Tcl_Size length - Number of elements currently in the vector.
 *      Tcl_Size size   - Maximum number of elements the supplied
 *                        array can hold.
 *      Tcl_FreeProc *freeProc - Address of memory deallocation routine for the array of values.  Can also be
 *                               TCL_STATIC, TCL_DYNAMIC, or TCL_VOLATILE.
 *
 * Results:
 *      A standard Tcl result.  If the new array size is invalid,
 *      TCL_ERROR is returned.  Otherwise TCL_OK is returned and the
 *      new vector data is recorded.
 *
 * Side Effects:
 *      Any client designated callbacks will be posted.  Memory may
 *      be changed for the vector array.
 *
 * -----------------------------------------------------------------------
 */
static int ResetVectorStorage(VectorObject *vPtr, void *valueArr, Tcl_Size length, Tcl_Size size,
                              Tcl_FreeProc *freeProc) {
    void *newArr;
    size_t sizeBytes;
    size_t lengthBytes;

    if (length < 0) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector length cannot be negative", -1));
        return TCL_ERROR;
    }
    if (size < 0) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector array size cannot be negative", -1));
        return TCL_ERROR;
    }
    if (length > size) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector length exceeds array size", -1));
        return TCL_ERROR;
    }

    if ((GetVectorByteCount(vPtr->interp, vPtr->type, size, &sizeBytes) != TCL_OK) ||
        (GetVectorByteCount(vPtr->interp, vPtr->type, length, &lengthBytes) != TCL_OK)) {
        return TCL_ERROR;
    }
    /*
     * A NULL array or zero capacity represents an empty vector.
     */
    if ((valueArr == NULL) || (size == 0)) {
        valueArr = NULL;
        length = 0;
        size = 0;
        freeProc = TCL_STATIC;

    } else if (freeProc == TCL_VOLATILE) {
        /*
         * Volatile storage must be copied even when valueArr happens
         * to be the same address as the vector's current array.
         */
        newArr = ckalloc(sizeBytes);
        if (lengthBytes > 0) {
            memcpy(newArr, valueArr, lengthBytes);
        }
        valueArr = newArr;
        freeProc = TCL_DYNAMIC;
    }
    /*
     * Release the old array only when it is actually being replaced.
     */
    if ((vPtr->data.raw != valueArr) && (vPtr->data.raw != NULL) && (vPtr->freeProc != TCL_STATIC)) {
        if (vPtr->freeProc == TCL_DYNAMIC) {
            ckfree(vPtr->data.raw);
        } else {
            vPtr->freeProc(vPtr->data.raw);
        }
    }
    /*
     * Record the complete new array contract even if the address did
     * not change. The caller may have resized storage in place.
     */
    vPtr->freeProc = freeProc;
    vPtr->data.raw = valueArr;
    vPtr->size = size;
    vPtr->length = length;
    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }
    Rbc_VectorUpdateClients(vPtr);
    return TCL_OK;
}

int Rbc_VectorReset(VectorObject *vPtr, double *valueArr, Tcl_Size length, Tcl_Size size, Tcl_FreeProc *freeProc) {
    if (vPtr->type != RBC_VECTOR_REAL) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("Rbc_ResetVector cannot reset a complex vector", -1));
        return TCL_ERROR;
    }
    return ResetVectorStorage(vPtr, valueArr, length, size, freeProc);
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorNotifyClients --
 *
 *      Notifies each client of the vector that the vector has changed
 *      (updated or destroyed) by calling the provided function back.
 *      The function pointer may be NULL, in that case the client is
 *      not notified.
 *
 * Parameters:
 *      ClientData clientData
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The results depend upon what actions the client callbacks
 *      take.
 *
 * ----------------------------------------------------------------------
 */
static void VectorNotifyClients(ClientData clientData) {
    VectorObject *vPtr = clientData;
    Rbc_ChainLink *linkPtr;
    VectorClient *clientPtr;
    Rbc_VectorNotify notify;
    int updateAll;
    int updateRangeValid;
    Tcl_Size updateFirst;
    Tcl_Size updateLast;

    notify = (vPtr->notifyFlags & NOTIFY_DESTROYED) ? RBC_VECTOR_NOTIFY_DESTROY : RBC_VECTOR_NOTIFY_UPDATE;
    /*
     * Snapshot the coalesced update before invoking clients.
     *
     * Clear the server-side pending range now so that an update caused
     * from inside a callback starts a new notification range rather
     * than modifying the range currently being delivered.
     */
    if (notify == RBC_VECTOR_NOTIFY_DESTROY) {
        updateAll = TRUE;
        updateRangeValid = FALSE;
        updateFirst = 0;
        updateLast = 0;
    } else {
        updateAll = vPtr->updateAll;
        updateRangeValid = vPtr->updateRangeValid && !updateAll;
        updateFirst = vPtr->updateFirst;
        updateLast = vPtr->updateLast;
    }
    vPtr->updateAll = FALSE;
    vPtr->updateRangeValid = FALSE;
    vPtr->updateFirst = 0;
    vPtr->updateLast = 0;
    vPtr->notifyFlags &= ~(NOTIFY_UPDATED | NOTIFY_DESTROYED | NOTIFY_PENDING);
    for (linkPtr = Rbc_ChainFirstLink(vPtr->chainPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        clientPtr = Rbc_ChainGetValue(linkPtr);
        /*
         * Give each client its own snapshot.  This avoids making the
         * callback inspect mutable server-side notification state.
         */
        clientPtr->updateAll = updateAll;
        clientPtr->updateRangeValid = updateRangeValid;
        clientPtr->updateFirst = updateFirst;
        clientPtr->updateLast = updateLast;
        if (clientPtr->proc != NULL) {
            (*clientPtr->proc)(vPtr->interp, clientPtr->clientData, notify);
        }
        clientPtr->updateAll = FALSE;
        clientPtr->updateRangeValid = FALSE;
        clientPtr->updateFirst = 0;
        clientPtr->updateLast = 0;
    }
    /*
     * Some clients may not handle the "destroy" callback properly
     * (they should call Rbc_FreeVectorId to release the client
     * identifier), so mark any remaining clients to indicate that
     * vector's server has gone away.
     */
    if (notify == RBC_VECTOR_NOTIFY_DESTROY) {
        for (linkPtr = Rbc_ChainFirstLink(vPtr->chainPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            clientPtr = Rbc_ChainGetValue(linkPtr);
            clientPtr->serverPtr = NULL;
        }
    }
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_VectorNotifyPending --
 *
 *      Returns the name of the vector (and array variable).
 *
 * Parameters:
 *      Rbc_VectorId clientId - Client token identifying the vector
 *
 * Results:
 *      The name of the array variable is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_VectorNotifyPending(Rbc_VectorId clientId) {
    VectorClient *clientPtr = (VectorClient *)clientId;

    if ((clientPtr == NULL) || (clientPtr->magic != VECTOR_MAGIC) || (clientPtr->serverPtr == NULL)) {
        return 0;
    }
    return (clientPtr->serverPtr->notifyFlags & NOTIFY_PENDING);
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorFlushCache --
 *
 *      Unsets all the elements of the Tcl array variable associated
 *      with the vector, freeing memory associated with the variable.
 *      This includes both the hash table and the hash keys.  The down
 *      side is that this effectively flushes the caching of vector
 *      elements in the array.  This means that the subsequent reads
 *      of the array will require a decimal to string conversion.
 *
 *      This is needed when the vector changes its values, making
 *      the array variable out-of-sync.
 *
 * Parameters:
 *      VectorObject *vPtr - The vector to flush
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      All elements of array variable (except one) are unset, freeing
 *      the memory associated with the variable.
 *
 * ----------------------------------------------------------------------
 */
static void VectorFlushCache(VectorObject *vPtr) {
    Tcl_Interp *interp = vPtr->interp;

    if (vPtr->arrayName == NULL) {
        return; /* Doesn't use the variable API */
    }

    /* Turn off the trace temporarily so that we can unset all the
     * elements in the array.  */
    /* TODO I added a cast to Tcl_VarTraceProc * which might cause issues. */
    Tcl_UntraceVar2(interp, vPtr->arrayName, (char *)NULL, TRACE_ALL | vPtr->varFlags,
                    (Tcl_VarTraceProc *)VectorVarTrace, vPtr);

    /* Clear all the element entries from the entire array */
    Tcl_UnsetVar2(interp, vPtr->arrayName, (char *)NULL, vPtr->varFlags);

    /* Restore the "end" index by default and the trace on the entire array */
    Tcl_SetVar2(interp, vPtr->arrayName, "end", "", vPtr->varFlags);
    /* TODO I added a cast to Tcl_VarTraceProc * which might cause issues. */
    Tcl_TraceVar2(interp, vPtr->arrayName, (char *)NULL, TRACE_ALL | vPtr->varFlags, (Tcl_VarTraceProc *)VectorVarTrace,
                  vPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorChangeLength --
 *
 *      Resizes the vector to the new size.
 *
 *      The new size of the vector is computed by doubling the
 *      size of the vector until it fits the number of slots needed
 *      (designated by *length*).
 *
 *      If the new size is the same as the old, simply adjust the
 *      length of the vector.  Otherwise we're copying the data from
 *      one memory location to another. The trailing elements of the
 *      vector need to be reset to zero.
 *
 *      If the storage changed memory locations, free up the old
 *      location if it was dynamically allocated.
 *
 * Parameters:
 *      VectorObject *vPtr - The vector to change lengths
 *      int length - The new size of the vector
 *
 * Results:
 *      A standard Tcl result.  If the reallocation is successful,
 *      TCL_OK is returned, otherwise TCL_ERROR.
 *
 * Side effects:
 *      Memory for the array is reallocated.
 *
 * ----------------------------------------------------------------------
 */
int Rbc_VectorChangeLength(VectorObject *vPtr, Tcl_Size length) {
    Tcl_Size newSize;
    Tcl_Size used;
    VectorData newData;
    Tcl_FreeProc *freeProc;

    if (length < 0) {
        Tcl_SetObjResult(vPtr->interp, Tcl_ObjPrintf("bad vector size \"%" TCL_SIZE_MODIFIER "d\"", length));
        return TCL_ERROR;
    }
    newData.raw = NULL;
    newSize = 0;
    freeProc = TCL_STATIC;
    if (length > 0) {
        size_t byteCount;
        size_t usedByteCount;

        newSize = DEF_ARRAY_SIZE;
        while (newSize < length) {
            if (newSize > (TCL_SIZE_MAX / 2)) {
                newSize = length;
                break;
            }
            newSize *= 2;
        }
        if (GetVectorByteCount(vPtr->interp, vPtr->type, newSize, &byteCount) != TCL_OK) {
            return TCL_ERROR;
        }
        used = vPtr->length;
        if (used > length) {
            used = length;
        }
        freeProc = vPtr->freeProc;
        if (newSize == vPtr->size) {
            newData.raw = vPtr->data.raw;
        } else {
            newData.raw = Tcl_AttemptAlloc(byteCount);
            if (newData.raw == NULL) {
                Tcl_SetObjResult(vPtr->interp,
                                 Tcl_ObjPrintf("can't allocate %" TCL_SIZE_MODIFIER "d elements for vector \"%s\"",
                                               newSize, vPtr->name));
                return TCL_ERROR;
            }
            if (used > 0) {
                if (GetVectorByteCount(vPtr->interp, vPtr->type, used, &usedByteCount) != TCL_OK) {
                    ckfree(newData.raw);
                    return TCL_ERROR;
                }
                memcpy(newData.raw, vPtr->data.raw, usedByteCount);
            }
            freeProc = TCL_DYNAMIC;
        }
        if (length > used) {
            ZeroVectorElements(vPtr->type, newData, used, length);
        }
    }
    if ((newData.raw != vPtr->data.raw) && (vPtr->data.raw != NULL) && (vPtr->freeProc != TCL_STATIC)) {
        if (vPtr->freeProc == TCL_DYNAMIC) {
            ckfree(vPtr->data.raw);
        } else {
            vPtr->freeProc(vPtr->data.raw);
        }
    }
    vPtr->data = newData;
    vPtr->size = newSize;
    vPtr->length = length;
    vPtr->first = 0;
    vPtr->last = length - 1;
    vPtr->freeProc = freeProc;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorLookupName --
 *
 *      Searches for the vector associated with the name given.  Allow
 *      for a range specification.
 *
 * Parameters:
 *      VectorInterpData *dataPtr - Interpreter-specific data.
 *      char *vecName
 *      VectorObject **vPtrPtr
 *
 * Results:
 *      Returns a pointer to the vector if found, otherwise NULL.
 *      If the name is not associated with a vector and the
 *      TCL_LEAVE_ERR_MSG flag is set, and the interpreter result
 *      will contain an error message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int Rbc_VectorLookupName(VectorInterpData *dataPtr, const char *vecName, VectorObject **vPtrPtr) {
    VectorObject *vPtr;
    const char *endPtr;

    vPtr = Rbc_VectorParseElement(dataPtr->interp, dataPtr, vecName, &endPtr, NS_SEARCH_BOTH);
    if (vPtr == NULL) {
        return TCL_ERROR;
    }
    if (*endPtr != '\0') {
        Tcl_SetObjResult(dataPtr->interp, Tcl_NewStringObj("extra characters after vector name", -1));
        return TCL_ERROR;
    }
    *vPtrPtr = vPtr;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorUpdateRange --
 *
 *      TODO: Description
 *
 * Parameters:
 *      VectorObject *vPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
void Rbc_VectorUpdateRange(VectorObject *vPtr) {
    double min;
    double max;
    Tcl_Size i;

    if (vPtr->type != RBC_VECTOR_REAL) {
        vPtr->min = vPtr->max = rbcNaN;
        vPtr->notifyFlags &= ~UPDATE_RANGE;
        return;
    }
    min = max = rbcNaN;
    /*
     * Find the first finite value.  If there isn't one, the cached
     * range remains NaN/NaN, matching Rbc_VecMin/Rbc_VecMax.
     */
    for (i = 0; i < vPtr->length; i++) {
        double value;

        value = vPtr->data.real[i];
        if (FINITE(value)) {
            min = max = value;
            i++;
            break;
        }
    }
    for (; i < vPtr->length; i++) {
        double value;

        value = vPtr->data.real[i];
        if (!FINITE(value)) {
            continue;
        }
        if (value < min) {
            min = value;
        } else if (value > max) {
            max = value;
        }
    }
    vPtr->min = min;
    vPtr->max = max;
    vPtr->notifyFlags &= ~UPDATE_RANGE;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorGetIndex --
 *
 *      Converts the string representing an index in the vector to
 *      its numeric value.  A valid index may be an numeric string or
 *      the string "end" (indicating the last element in the string).
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      VectorObject *vPtr
 *      const char *string
 *      int *indexPtr - index to convert
 *      int flags
 *      Rbc_VectorIndexProc **procPtrPtr
 *
 * Results:
 *      A standard Tcl result.  If the string is a valid index, TCL_OK
 *      is returned.  Otherwise TCL_ERROR is returned and
 *      the interpreter result will contain an error message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int Rbc_VectorGetIndex(Tcl_Interp *interp, VectorObject *vPtr, const char *string, Tcl_Size *indexPtr, int flags,
                       Rbc_VectorIndexProc **procPtrPtr) {
    Tcl_Obj *indexObjPtr;
    Tcl_Size value;
    int result;

    assert(vPtr != NULL);
    assert(string != NULL);
    assert(indexPtr != NULL);

    if (procPtrPtr != NULL) {
        *procPtrPtr = NULL;
    }

    if (string[0] == '\0') {
        /*
         * Do not pass the empty string to the expression parser, where
         * it could otherwise be interpreted as zero.
         */
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can not use the empty string as index", -1));
        }

        return TCL_ERROR;
    }

    if (string[0] == 'e') {
        /*
         * Parse "end" and index expressions based on "end", such as
         * "end-1".
         */
        if (vPtr->length < 1) {
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad index \"%s\": vector is empty", string));
            }

            return TCL_ERROR;
        }

        indexObjPtr = Tcl_NewStringObj(string, -1);
        Tcl_IncrRefCount(indexObjPtr);

        result = Tcl_GetIntForIndex(interp, indexObjPtr, vPtr->length - 1, &value);

        Tcl_DecrRefCount(indexObjPtr);

        if (result != TCL_OK) {
            return TCL_ERROR;
        }

        if ((value < 0) || (value >= vPtr->length)) {
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("index \"%s\" is out of range", string));
            }

            return TCL_ERROR;
        }

        *indexPtr = value;

        return TCL_OK;
    }

    if ((string[0] == '+') && (strcmp(string, "++end") == 0)) {
        /*
         * The special append index denotes the position immediately
         * after the final vector value.
         */
        *indexPtr = vPtr->length;

        return TCL_OK;
    }

    if (procPtrPtr != NULL) {
        Tcl_HashEntry *hPtr;

        hPtr = Tcl_FindHashEntry(&vPtr->dataPtr->indexProcTable, string);

        if (hPtr != NULL) {
            *indexPtr = SPECIAL_INDEX;
            *procPtrPtr = (Rbc_VectorIndexProc *)Tcl_GetHashValue(hPtr);

            return TCL_OK;
        }
    }

    indexObjPtr = Tcl_NewStringObj(string, -1);
    Tcl_IncrRefCount(indexObjPtr);

    /*
     * First try a plain Tcl-sized integer without modifying either
     * interpreter result.
     */
    result = Tcl_GetSizeIntFromObj(NULL, indexObjPtr, &value);

    if (result != TCL_OK) {
        Tcl_Obj *valueObjPtr;

        valueObjPtr = NULL;

        /*
         * Preserve the historical support for index expressions. Use the
         * vector's interpreter because the public interp argument may be
         * NULL.
         */
        result = Tcl_ExprObj(vPtr->interp, indexObjPtr, &valueObjPtr);

        if (result == TCL_OK) {
            result = Tcl_GetSizeIntFromObj(vPtr->interp, valueObjPtr, &value);

            Tcl_DecrRefCount(valueObjPtr);
        }

        if (result != TCL_OK) {
            Tcl_ResetResult(vPtr->interp);

            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad index \"%s\"", string));
            }

            Tcl_DecrRefCount(indexObjPtr);

            return TCL_ERROR;
        }
    }

    Tcl_DecrRefCount(indexObjPtr);

    /*
     * Convert the externally visible index to the vector's zero-based
     * storage index.
     *
     * Check before subtraction to avoid signed Tcl_Size overflow. If the
     * external value is smaller than the offset, the resulting internal
     * index would be negative. A negative offset can instead cause an
     * upward overflow.
     */
    if ((value < vPtr->offset) || ((vPtr->offset < 0) && (value > (TCL_SIZE_MAX + vPtr->offset)))) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("index \"%s\" is out of range", string));
        }

        return TCL_ERROR;
    }

    value -= vPtr->offset;

    if ((flags & INDEX_CHECK) && (value >= vPtr->length)) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("index \"%s\" is out of range", string));
        }

        return TCL_ERROR;
    }

    *indexPtr = value;

    return TCL_OK;
}

static int GetVectorIndexFromSpan(Tcl_Interp *interp, VectorObject *vPtr, const char *start, const char *end,
                                  Tcl_Size *indexPtr, int flags) {
    Tcl_Obj *objPtr;
    int result;

    assert(start != NULL);
    assert(end >= start);
    objPtr = Tcl_NewStringObj(start, (Tcl_Size)(end - start));
    Tcl_IncrRefCount(objPtr);
    result = Rbc_VectorGetIndex(interp, vPtr, Tcl_GetString(objPtr), indexPtr, flags, NULL);
    Tcl_DecrRefCount(objPtr);
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorGetIndexRange --
 *
 *      Converts the string representing of an index in the vector to
 *      its numeric value.  A valid index may be an numeric string or
 *      the string "end" (indicating the last element in the string).
 *
 * Parameters:
 *      Tcl_Interp *interp - The interpreter to return results to
 *      VectorObject *vPtr - The vector object to get the range from
 *      const char *string - The index in the vector to convert
 *      int flags - The flags for special cases
 *      Rbc_VectorIndexProc **procPtrPtr - The index procedure
 *
 * Results:
 *      A standard Tcl result.  If the string is a valid index, TCL_OK
 *      is returned.  Otherwise TCL_ERROR is returned and
 *      the interpreter result will contain an error message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int Rbc_VectorGetIndexRange(Tcl_Interp *interp, VectorObject *vPtr, const char *string, int flags,
                            Rbc_VectorIndexProc **procPtrPtr) {
    const char *colon;
    Tcl_Size first;
    Tcl_Size last;
    Tcl_Size index;

    assert(vPtr != NULL);
    assert(string != NULL);
    if (procPtrPtr != NULL) {
        *procPtrPtr = NULL;
    }
    colon = NULL;
    if (flags & INDEX_COLON) {
        colon = strchr(string, ':');
    }
    if (colon == NULL) {
        if (Rbc_VectorGetIndex(interp, vPtr, string, &index, flags, procPtrPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->first = index;
        vPtr->last = index;
        return TCL_OK;
    }
    /*
     * Parse the first part without writing a temporary NUL into string.
     */
    if (colon == string) {
        first = 0;
    } else {
        if (GetVectorIndexFromSpan(interp, vPtr, string, colon, &first, flags) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /*
     * Parse the second part. It is already NUL-terminated.
     */
    if (colon[1] == '\0') {
        last = (vPtr->length > 0) ? vPtr->length - 1 : 0;
    } else {
        if (Rbc_VectorGetIndex(interp, vPtr, colon + 1, &last, flags, NULL) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (first > last) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad range \"%s\" (first > last)", string));
        }
        return TCL_ERROR;
    }
    /*
     * Commit only after the complete range has been validated.
     */
    vPtr->first = first;
    vPtr->last = last;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorParseElement --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      VectorInterpData *dataPtr - Interpreter-specific data.
 *      const char *string - name of the vector
 *      char **endPtr - ?
 *      int flags - NS_SEARCH_CURRENT nd such ...
 *
 * Results:
 *      A vector object
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
VectorObject *Rbc_VectorParseElement(Tcl_Interp *interp, VectorInterpData *dataPtr, const char *start,
                                     const char **endPtr, int flags) {
    const char *p;
    VectorObject *vPtr;
    Tcl_Obj *nameObjPtr;
    Tcl_Size oldFirst;
    Tcl_Size oldLast;

    assert(dataPtr != NULL);
    assert(start != NULL);
    /*
     * Find the end of the vector name.
     */
    p = start;
    while (VECTOR_CHAR(*p)) {
        p++;
    }
    /*
     * Copy only the vector-name portion. Do not write into start.
     */
    nameObjPtr = Tcl_NewStringObj(start, (Tcl_Size)(p - start));
    Tcl_IncrRefCount(nameObjPtr);
    vPtr = GetVectorObject(dataPtr, Tcl_GetString(nameObjPtr), flags);
    if (vPtr == NULL) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't find vector \"%s\"", Tcl_GetString(nameObjPtr)));
        }
        Tcl_DecrRefCount(nameObjPtr);
        return NULL;
    }
    Tcl_DecrRefCount(nameObjPtr);
    oldFirst = vPtr->first;
    oldLast = vPtr->last;
    /*
     * With no explicit range, select the complete vector.
     */
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;
    if (*p == '(') {
        const char *rangeStart;
        Tcl_Size depth;
        Tcl_Obj *rangeObjPtr;
        int result;
        rangeStart = p + 1;
        p = rangeStart;
        depth = 1;
        /*
         * Find the matching closing parenthesis. Nested parentheses are
         * allowed in index expressions.
         */
        while (*p != '\0') {
            if (*p == '(') {
                depth++;
            } else if (*p == ')') {
                depth--;
                if (depth == 0) {
                    break;
                }
            }
            p++;
        }
        if (depth != 0) {
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unbalanced parentheses \"%s\"", rangeStart));
            }
            vPtr->first = oldFirst;
            vPtr->last = oldLast;
            return NULL;
        }
        rangeObjPtr = Tcl_NewStringObj(rangeStart, (Tcl_Size)(p - rangeStart));
        Tcl_IncrRefCount(rangeObjPtr);
        result = Rbc_VectorGetIndexRange(interp, vPtr, Tcl_GetString(rangeObjPtr), INDEX_COLON | INDEX_CHECK, NULL);
        Tcl_DecrRefCount(rangeObjPtr);
        if (result != TCL_OK) {
            vPtr->first = oldFirst;
            vPtr->last = oldLast;
            return NULL;
        }
        /*
         * Advance beyond the closing parenthesis.
         */
        p++;
    }
    if (endPtr != NULL) {
        *endPtr = p;
    }
    return vPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * ScheduleVectorUpdate --
 *
 *      Records an update and arranges for vector clients to be
 *      notified according to the vector's notification policy.
 *
 *      A ranged update may be coalesced with another ranged update
 *      while an idle notification is pending.  An unknown/full update
 *      supersedes all ranged information.
 *
 * ----------------------------------------------------------------------
 */
static void ScheduleVectorUpdate(VectorObject *vPtr, int all, Tcl_Size first, Tcl_Size last) {
    vPtr->dirty++;
    if (vPtr->notifyFlags & NOTIFY_NEVER) {
        return;
    }
    if (all) {
        vPtr->updateAll = TRUE;
        vPtr->updateRangeValid = FALSE;
    } else if (!vPtr->updateAll) {
        if (!vPtr->updateRangeValid) {
            vPtr->updateFirst = first;
            vPtr->updateLast = last;
            vPtr->updateRangeValid = TRUE;
        } else {
            if (first < vPtr->updateFirst) {
                vPtr->updateFirst = first;
            }
            if (last > vPtr->updateLast) {
                vPtr->updateLast = last;
            }
        }
    }
    vPtr->notifyFlags |= NOTIFY_UPDATED;
    if (vPtr->notifyFlags & NOTIFY_ALWAYS) {
        VectorNotifyClients(vPtr);
        return;
    }
    if (!(vPtr->notifyFlags & NOTIFY_PENDING)) {
        vPtr->notifyFlags |= NOTIFY_PENDING;
        Tcl_DoWhenIdle(VectorNotifyClients, vPtr);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorUpdateClients --
 *
 *      Notifies each client of the vector that the vector has changed
 *      (updated or destroyed) by calling the provided function back.
 *
 * Parameters:
 *      VectorObject *vPtr - The vector to update clients for
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The individual client callbacks are eventually invoked.
 *
 * ----------------------------------------------------------------------
 */
void Rbc_VectorUpdateClients(VectorObject *vPtr) {
    /*
     * An unqualified update gives us no information from which the
     * cached global range can safely be maintained.
     */
    vPtr->min = vPtr->max = rbcNaN;
    vPtr->notifyFlags |= UPDATE_RANGE;

    ScheduleVectorUpdate(vPtr, TRUE, 0, 0);
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorUpdateClientsRange --
 *
 *      Notifies vector clients that a known inclusive source range was
 *      modified.
 *
 *      Multiple ranged updates occurring before notification are
 *      coalesced into one inclusive range.
 *
 *      Invalid ranges conservatively become full/unknown updates.
 *
 * ----------------------------------------------------------------------
 */
void Rbc_VectorUpdateClientsRange(VectorObject *vPtr, Tcl_Size first, Tcl_Size last) {
    if ((first < 0) || (last < first) || (last >= vPtr->length)) {
        Rbc_VectorUpdateClients(vPtr);
        return;
    }
    ScheduleVectorUpdate(vPtr, FALSE, first, last);
}

/*
 * ----------------------------------------------------------------------
 *
 * GetReplicatedWriteRange --
 *
 *      Determines whether the cached global min/max of a real vector
 *      can remain exact after replacing the inclusive source range
 *      [first,last] with one value.
 *
 *      The cached range can be maintained incrementally provided:
 *
 *        - it was valid before the write, and
 *        - the overwritten source range did not contain the old global
 *          minimum or maximum.
 *
 *      If either old extremum is overwritten, another occurrence may
 *      or may not exist elsewhere in the vector, so a complete range
 *      scan remains the conservative fallback.
 *
 * Results:
 *      TRUE if newMinPtr/newMaxPtr contain the exact post-write range.
 *      FALSE if the normal UPDATE_RANGE/full-scan path is required.
 *
 * ----------------------------------------------------------------------
 */
static int GetReplicatedWriteRange(VectorObject *vPtr, Tcl_Size first, Tcl_Size last, double value, double *newMinPtr,
                                   double *newMaxPtr) {
    double min;
    double max;
    int haveFiniteRange;
    Tcl_Size i;
    
    assert(vPtr->type == RBC_VECTOR_REAL);
    /*
     * The old global range must already be current.
     */
    if (vPtr->notifyFlags & UPDATE_RANGE) {
        return FALSE;
    }
    min = vPtr->min;
    max = vPtr->max;
    haveFiniteRange = FINITE(min) && FINITE(max);
    /*
     * If the old vector contained finite values, determine whether the
     * write destroys information needed to identify either extremum.
     */
    if (haveFiniteRange) {
        for (i = first; i <= last; i++) {
            double oldValue;

            oldValue = vPtr->data.real[i];
            if (!FINITE(oldValue)) {
                continue;
            }
            if ((oldValue == min) || (oldValue == max)) {
                return FALSE;
            }
        }
    }
    /*
     * A non-finite replacement cannot introduce a new finite extremum.
     * Since neither old extremum was removed above, the old range
     * remains exact.
     */
    if (!FINITE(value)) {
        *newMinPtr = min;
        *newMaxPtr = max;
        return TRUE;
    }
    if (!haveFiniteRange) {
        min = max = value;
    } else {
        if (value < min) {
            min = value;
        }
        if (value > max) {
            max = value;
        }
    }
    *newMinPtr = min;
    *newMaxPtr = max;
    return TRUE;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_ReplicateValuePreserveRange --
 *
 *      Replaces an existing inclusive range of a real vector while
 *      preserving the cached global minimum/maximum whenever that can
 *      be proven from the overwritten values.
 *
 *      The caller must supply an existing in-range source interval.
 *      Appends and resizes must use the ordinary Rbc_ReplicateValue()
 *      path instead.
 *
 * ----------------------------------------------------------------------
 */
void Rbc_ReplicateValuePreserveRange(VectorObject *vPtr, Tcl_Size first, Tcl_Size last, double value) {
    double newMin;
    double newMax;
    int preserveRange;

    assert(vPtr->type == RBC_VECTOR_REAL);
    assert(first >= 0);
    assert(last >= first);
    assert(last < vPtr->length);
    newMin = 0.0;
    newMax = 0.0;
    preserveRange = GetReplicatedWriteRange(vPtr, first, last, value, &newMin, &newMax);
    Rbc_ReplicateValue(vPtr, first, last, value);
    /*
     * Rbc_ReplicateValue() conservatively sets UPDATE_RANGE.
     * Cancel it when the new range has already been determined exactly.
     */
    if (preserveRange) {
        vPtr->min = newMin;
        vPtr->max = newMax;
        vPtr->notifyFlags &= ~UPDATE_RANGE;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorVarTrace --
 *
 *      Procedure invoked when a vector variable is read, written or unset
*
 * Parameters:
 *      ClientData clientData - Vector object.
 *      Tcl_Interp *interp - Interpreter of the vector
 *      char *part1 - name of array variable accessed
 *      char *part2 - name of array element accessed
 *      int flags
 *
 * Results:
 *      Returns NULL on success.  Returns an error message on failure.
 *      Only called from a variable trace.
 *
 * Side effects:
 *       may be several, like deleting a vector, etc.
 *
 * ----------------------------------------------------------------------
 */
static char *VectorVarTrace(ClientData clientData, Tcl_Interp *interp, char *part1, char *part2, int flags) {
    Rbc_VectorIndexProc *indexProc;
    VectorObject *vPtr = clientData;
    Tcl_Size first, last;
    int varFlags;

    if (part2 == NULL) {
        if (flags & TCL_TRACE_UNSETS) {
            /* vector is deleted via an unset on the whole array variable */
            ckfree((char *)vPtr->arrayName);
            vPtr->arrayName = NULL;
            if (vPtr->freeOnUnset) {
                Rbc_VectorFree(vPtr);
            }
        }
        return NULL;
    }
    if (Rbc_VectorGetIndexRange(interp, vPtr, part2, INDEX_ALL_FLAGS, &indexProc) != TCL_OK) {
        goto error;
    }
    first = vPtr->first;
    last = vPtr->last;
    varFlags = TCL_LEAVE_ERR_MSG | (flags & (TCL_GLOBAL_ONLY | TCL_NAMESPACE_ONLY));
    if (flags & TCL_TRACE_WRITES) {
        Tcl_Obj *objPtr;
        double realValue;
        int inPlaceRealWrite;
        Rbc_Complex complexValue;
        Tcl_Size oldLength;
        int result;

        oldLength = vPtr->length;
        inPlaceRealWrite = FALSE;
        if ((first == SPECIAL_INDEX) || (last == SPECIAL_INDEX)) {
            return VectorTraceError(Tcl_NewStringObj("read-only index", -1));
        }
        objPtr = Tcl_GetVar2Ex(interp, part1, part2, varFlags);
        if (objPtr == NULL) {
            goto error;
        }
        switch (vPtr->type) {
        case RBC_VECTOR_REAL:
            result = Rbc_GetDouble(interp, objPtr, &realValue);
            break;
        case RBC_VECTOR_COMPLEX:
            result = Rbc_GetComplex(interp, objPtr, &complexValue);
            break;
        default:
            Tcl_Panic("bad vector type %d", (int)vPtr->type);
            result = TCL_ERROR;
            break;
        }
        if (result != TCL_OK) {
            goto error;
        }
        /*
         * For an ordinary real-vector replacement, determine whether the
         * cached global min/max can be maintained before overwriting the old
         * source values.
         *
         * Appends are handled conservatively below because Rbc_VectorChangeLength()
         * changes the temporary vector contents before the final value is stored.
         */
        if ((vPtr->type == RBC_VECTOR_REAL) && (first >= 0) && (last >= first) && (last < oldLength)) {
            inPlaceRealWrite = TRUE;
        }
        if ((first == vPtr->length) || (last == vPtr->length)) {
            if (vPtr->length == TCL_SIZE_MAX) {
                return VectorTraceError(Tcl_NewStringObj("vector is too large to append", -1));
            }
            if (Rbc_VectorChangeLength(vPtr, vPtr->length + 1) != TCL_OK) {
                return VectorTraceError(Tcl_NewStringObj("error resizing vector", -1));
            }
        }
        switch (vPtr->type) {
        case RBC_VECTOR_REAL:
            if (inPlaceRealWrite) {
                Rbc_ReplicateValuePreserveRange(vPtr, first, last, realValue);
            } else {
                Rbc_ReplicateValue(vPtr, first, last, realValue);
            }
            break;
        case RBC_VECTOR_COMPLEX: {
            Tcl_Size i;

            for (i = first; i <= last; i++) {
                vPtr->data.complex[i] = complexValue;
            }
            vPtr->notifyFlags |= UPDATE_RANGE;
            break;
        }
        default:
            Tcl_Panic("bad vector type %d", (int)vPtr->type);
        }
    } else if (flags & TCL_TRACE_READS) {
        Tcl_Obj *objPtr;

        if ((first == vPtr->length) || (last == vPtr->length)) {
            return VectorTraceError(Tcl_NewStringObj("write-only index", -1));
        }
        if (vPtr->length == 0) {
            if (Tcl_SetVar2(interp, part1, part2, "", varFlags) == NULL) {
                goto error;
            }
            return NULL;
        }
        if (first == last) {
            if (first >= 0) {
                objPtr = Rbc_NewVectorValueObj(vPtr, first);
            } else {
                double value;

                /*
                 * min/max/mean/sum/prod still use real-only
                 * Rbc_VectorIndexProc callbacks.
                 */
                if (vPtr->type != RBC_VECTOR_REAL) {
                    return VectorTraceError(
                        Tcl_NewStringObj("special vector indices are not supported for complex vectors", -1));
                }
                vPtr->first = 0;
                vPtr->last = vPtr->length - 1;
                value = (*indexProc)((Rbc_Vector *)vPtr);
                objPtr = Tcl_NewDoubleObj(value);
            }
            if (Tcl_SetVar2Ex(interp, part1, part2, objPtr, varFlags) == NULL) {
                Tcl_DecrRefCount(objPtr);
                goto error;
            }
        } else {
            objPtr = Rbc_GetValues(vPtr, first, last);
            if (Tcl_SetVar2Ex(interp, part1, part2, objPtr, varFlags) == NULL) {
                Tcl_DecrRefCount(objPtr);
                goto error;
            }
        }
    } else if (flags & TCL_TRACE_UNSETS) {
        Tcl_Size i, j;

        if ((first == vPtr->length) || (first == SPECIAL_INDEX)) {
            return VectorTraceError(Tcl_NewStringObj("special vector index", -1));
        }
        /*
         * Collapse the vector from the point of the first unset element.
         * Also flush any array variable entries so that the shift is
         * reflected when the array variable is read.
         */
        for (i = first, j = last + 1; j < vPtr->length; i++, j++) {
            switch (vPtr->type) {
            case RBC_VECTOR_REAL:
                vPtr->data.real[i] = vPtr->data.real[j];
                break;
            case RBC_VECTOR_COMPLEX:
                vPtr->data.complex[i] = vPtr->data.complex[j];
                break;
            default:
                Tcl_Panic("bad vector type %d", (int)vPtr->type);
            }
        }
        vPtr->length -= ((last - first) + 1);
        if (vPtr->flush) {
            VectorFlushCache(vPtr);
        }
    } else {
        return VectorTraceError(Tcl_NewStringObj("unknown variable trace flag", -1));
    }
    if (flags & TCL_TRACE_WRITES) {
        /*
         * The write path knows the exact inclusive source range that
         * was modified.  Preserve it for clients such as graph display
         * decimation caches.
         *
         * Appends are harmless here: if the vector length changed,
         * clients will detect the length/storage mismatch and fall back
         * to a complete rebuild.
         */
        Rbc_VectorUpdateClientsRange(vPtr, first, last);

    } else if (flags & TCL_TRACE_UNSETS) {
        /*
         * Removing a range shifts every following source index, so the
         * resulting modification is not restricted to the unset range.
         */
        Rbc_VectorUpdateClients(vPtr);
    }
    Tcl_ResetResult(interp);
    return NULL;

error:
    return VectorTraceError(Tcl_GetObjResult(interp));
}

/*
 * ----------------------------------------------------------------------
 *
 * BuildQualifiedName --
 *
 *      Builds a fully qualified name from a given name depending on the current namespace
 *
 *        - lookup current namespace
 *        - if name starts with :: -> do nothing
 *        - if name does not start with :: -> set name relative to current namespace
 *
 *      (used in VectorCreate)
 *
 * Parameters:
 *      Tcl_Interp *interp - the interpreter in which to lookup the variable or command
 *      const char *name - the name of a variable, or command to build the qualified name for 
 *      Tcl_DString *fullName - string pointer to save the qualified name into (free or uninitialized DString)
 *
 * Results:
 *      Returns the qualified name
 *
 * Side effects:
 *      fullName is filled with the qualified name
 *
 * ----------------------------------------------------------------------
 */
static char *BuildQualifiedName(Tcl_Interp *interp, const char *name, Tcl_DString *fullName) {
    Tcl_Namespace *nsPtr;

    if (name == NULL) {
        return NULL;
    }

    Tcl_DStringInit(fullName);
    /* FIXME: Doesn't work in Tcl 8.4 */
    nsPtr = Tcl_GetCurrentNamespace(interp);

    if ((name[0] == ':') && (name[1] == ':')) {
        /* we have a fully qualified name already -> just return the given name */
        Tcl_DStringAppend(fullName, name, -1);
        return Tcl_DStringValue(fullName);
    }

    /* build a qualified name */
    Tcl_DStringAppend(fullName, nsPtr->fullName, -1);
    if (Tcl_DStringLength(fullName) > 2) {
        /* namespace is not the root namespace, so we need a separator */
        Tcl_DStringAppend(fullName, "::", -1);
    }
    Tcl_DStringAppend(fullName, name, -1);
    return Tcl_DStringValue(fullName);
}

/*
 * ----------------------------------------------------------------------
 *
 * ParseQualifiedName --
 *
 *      Parses a possibly namespaced (variable) name
 *      and checkes whether the corresponding namespace
 *      exists or not. Splits the name into its components
 *      as the namespace part and the name itself
 *
 *      This function is the counterpart of GetQualifiedName
 *
 * Parameters:
 *      Tcl_Interp *interp - the interpreter, where the name is found in
 *      const char *qualName - the qualified name to parse 
 *      Tcl_Namespace **nsPtrPtr - pointer to store the namespace part into
 *      const char **namePtrPtr - pointer to store the name itself into
 *
 * Results:
 *      A standard Tcl result. Returns TCL_ERROR if the namespace does
 *      not exist yet, else returns TCL_OK
 *
 * Side effects:
 *      If TCL_OK is returned, the nsPtr contains the namespace
 *      and namePtr contains the name of the vector in that namespace
 *
 * ----------------------------------------------------------------------
 */
static int ParseQualifiedName(Tcl_Interp *interp, const char *qualName, Tcl_Namespace **nsPtrPtr,
                              const char **namePtrPtr) {
    const char *p;
    const char *separator;
    const char *name;
    Tcl_Namespace *nsPtr;

    separator = NULL;
    name = qualName;
    /*
     * Find the final namespace separator without walking before the
     * beginning of an empty or one-character string.
     */
    p = qualName + strlen(qualName);
    while (p > (qualName + 1)) {
        p--;
        if ((*p == ':') && (p[-1] == ':')) {
            separator = p - 1;
            name = p + 1;
            break;
        }
    }
    if (separator == NULL) {
        *nsPtrPtr = NULL;
        *namePtrPtr = qualName;
        return TCL_OK;
    }
    if (separator == qualName) {
        /*
         * A name beginning with "::" belongs to the global namespace.
         */
        nsPtr = Tcl_GetGlobalNamespace(interp);
    } else {
        Tcl_Obj *namespaceObjPtr;
        namespaceObjPtr = Tcl_NewStringObj(qualName, (Tcl_Size)(separator - qualName));
        Tcl_IncrRefCount(namespaceObjPtr);
        nsPtr = Tcl_FindNamespace(interp, Tcl_GetString(namespaceObjPtr), NULL, 0);
        Tcl_DecrRefCount(namespaceObjPtr);
    }
    if (nsPtr == NULL) {
        return TCL_ERROR;
    }
    *nsPtrPtr = nsPtr;
    *namePtrPtr = name;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * GetQualifiedName --
 *
 *      Builds a namespaced variable name
 *      from a namespace and a variable name specification
 *
 *      This function is the counterpart of ParseQualifiedName
 *
 * Parameters:
 *      Tcl_Namespace *nsPtr
 *      const char *name
 *      Tcl_DString *resultPtr
 *
 * Results:
 *      A namespaced Tcl name
 *
 * Side effects:
 *      fills the supplied DString with the qualified name
 *
 * ----------------------------------------------------------------------
 */
static char *GetQualifiedName(Tcl_Namespace *nsPtr, const char *name, Tcl_DString *resultPtr) {
    Tcl_DStringInit(resultPtr);
    if ((nsPtr->fullName[0] != ':') || (nsPtr->fullName[1] != ':') || (nsPtr->fullName[2] != '\0')) {
        Tcl_DStringAppend(resultPtr, nsPtr->fullName, -1);
    }
    Tcl_DStringAppend(resultPtr, "::", -1);
    Tcl_DStringAppend(resultPtr, (char *)name, -1);
    return Tcl_DStringValue(resultPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * GetVectorObject --
 *
 *      Searches for the vector associated with the name given.
 *      Allow for a range specification.
 *
 * Parameters:
 *      VectorInterpData *dataPtr - Interpreter-specific data.
 *      const char *name
 *      int flags
 *
 * Results:
 *      Returns a pointer to the vector if found, otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static VectorObject *GetVectorObject(VectorInterpData *dataPtr, const char *name, int flags) {
    const char *vecName;
    Tcl_Namespace *nsPtr;
    VectorObject *vPtr;

    nsPtr = NULL;
    vecName = name;
    if (ParseQualifiedName(dataPtr->interp, name, &nsPtr, &vecName) != TCL_OK) {
        return NULL; /* Can't find namespace. */
    }
    vPtr = NULL;
    if (nsPtr != NULL) {
        vPtr = FindVectorInNamespace(dataPtr, nsPtr, vecName);
    } else {
        if (flags & NS_SEARCH_CURRENT) {
            nsPtr = Tcl_GetCurrentNamespace(dataPtr->interp);
            vPtr = FindVectorInNamespace(dataPtr, nsPtr, vecName);
        }
        if ((vPtr == NULL) && (flags & NS_SEARCH_GLOBAL)) {
            nsPtr = Tcl_GetGlobalNamespace(dataPtr->interp);
            vPtr = FindVectorInNamespace(dataPtr, nsPtr, vecName);
        }
    }
    return vPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * FindVectorInNamespace --
 *
 *      Retrieves the vector indicated when it is located in
 *      a certain namespace.
 *
 * Parameters:
 *      VectorInterpData *dataPtr - Interpreter-specific data.
 *      Tcl_Namespace *nsPtr - Namespace pointer
 *      const char *vecName - Name of the vector to find
 *
 * Results:
 *      Returns a pointer to the vector if found, otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static VectorObject *FindVectorInNamespace(VectorInterpData *dataPtr, Tcl_Namespace *nsPtr, const char *vecName) {
    Tcl_DString dString;
    const char *name;
    Tcl_HashEntry *hPtr;

    name = GetQualifiedName(nsPtr, vecName, &dString);
    hPtr = Tcl_FindHashEntry(&(dataPtr->vectorTable), name);
    Tcl_DStringFree(&dString);
    if (hPtr != NULL) {
        return (VectorObject *)Tcl_GetHashValue(hPtr);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetValues --
 *
 *      Return a list containing the values of the vector
 *
 * Parameters:
 *      VectorObject *vPtr
 *      int first
 *      int last
 *
 * Results:
 *      Returns a Tcl_Obj pointer to a list of doubles
 *      representing the values of the vector.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
Tcl_Obj *Rbc_GetValues(VectorObject *vPtr, Tcl_Size first, Tcl_Size last) {
    Tcl_Size i;
    Tcl_Obj *listObjPtr;

    listObjPtr = Tcl_NewListObj(0, NULL);
    for (i = first; i <= last; i++) {
        Tcl_ListObjAppendElement(vPtr->interp, listObjPtr, Rbc_NewVectorValueObj(vPtr, i));
    }
    return listObjPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_ReplicateValue --
 *
 *      Sets the value into the array from the first to last index.
 *
 * Parameters:
 *      VectorObject *vPtr - The vector to replicate values on
 *      int first - The start index to replicate into
 *      int last - The end index to replicate into
 *      double value - The value to replicate
 *
 * Results:
 *      None.
 *
 * Side effects:
 *       Sets new value for vector from first to last.
 *
 * ----------------------------------------------------------------------
 */
void Rbc_ReplicateValue(VectorObject *vPtr, Tcl_Size first, Tcl_Size last, double value) {
    Tcl_Size i;

    for (i = first; i <= last; i++) {
        vPtr->data.real[i] = value;
    }
    vPtr->notifyFlags |= UPDATE_RANGE;
}

/*
 * ----------------------------------------------------------------------
 *
 * DeleteCommand --
 *
 *      Deletes the Tcl command associated with the vector, without
 *      triggering a callback to "VectorInstDeleteProc".
 *
 * Parameters:
 *      VectorObject *vPtr - Vector associated with the Tcl command.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 * ----------------------------------------------------------------------
 */
static void DeleteCommand(VectorObject *vPtr) {
    Tcl_Interp *interp = vPtr->interp;
    Tcl_CmdInfo cmdInfo;
    const char *cmdName;

    cmdName = Tcl_GetCommandName(interp, vPtr->cmdToken);

    if (Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
        /* Disable the callback before deleting the Tcl command.*/
        cmdInfo.deleteProc = NULL;
        Tcl_SetCommandInfo(interp, cmdName, &cmdInfo);
        Tcl_DeleteCommand(interp, cmdName);
    }
    vPtr->cmdToken = 0;
}

/*
 * ----------------------------------------------------------------------
 *
 * UnmapVariable --
 *
 *      Destroys the trace on the current Tcl variable designated
 *      to access the vector.
 *
 * Parameters:
 *      VectorObject *vPtr - Vector to unmap
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void UnmapVariable(VectorObject *vPtr) {
    Tcl_Interp *interp = vPtr->interp;

    if (vPtr->arrayName == NULL) {
        return;
    }

    /* Unset the entire array */
    Tcl_UntraceVar2(interp, vPtr->arrayName, NULL, (TRACE_ALL | vPtr->varFlags), (Tcl_VarTraceProc *)VectorVarTrace,
                    vPtr);
    Tcl_UnsetVar2(interp, vPtr->arrayName, (char *)NULL, vPtr->varFlags);

    /* free the space */
    ckfree((char *)vPtr->arrayName);
    vPtr->arrayName = NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_GetDouble --
 *
 *      Returns a double from the Tcl_Obj provided.
 *
 * Parameters:
 *      Tcl_Interp *interp - Tcl Interp to use for extracting.
 *      Tcl_Obj *objPtr - The object holding the double value
 *      double *valuePtr - Return value for the double
 *
 * Results:
 *      Success of failure and the value object.
 *
 * Side effects:
 *       None.
 *
 * ----------------------------------------------------------------------
 */

int Rbc_GetDouble(Tcl_Interp *interp, Tcl_Obj *objPtr, double *valuePtr) {
    /* First try to extract the value as a double precision number. */
    if (Tcl_GetDoubleFromObj(interp, objPtr, valuePtr) == TCL_OK) {
        return TCL_OK;
    }
    Tcl_ResetResult(interp);

    /* Then try to parse it as an expression. */
    if (Tcl_ExprDoubleObj(interp, objPtr, valuePtr) == TCL_OK) {
        return TCL_OK;
    }
    return TCL_ERROR;
}

Tcl_Obj *Rbc_NewComplexObj(Rbc_Complex value) {
    Tcl_Obj *objv[2];

    objv[0] = Tcl_NewDoubleObj(value.real);
    objv[1] = Tcl_NewDoubleObj(value.imag);
    return Tcl_NewListObj(2, objv);
}

Tcl_Obj *Rbc_NewVectorValueObj(VectorObject *vPtr, Tcl_Size index) {
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        return Tcl_NewDoubleObj(vPtr->data.real[index]);
    case RBC_VECTOR_COMPLEX:
        return Rbc_NewComplexObj(vPtr->data.complex[index]);
    }
    Tcl_Panic("bad vector type %d", (int)vPtr->type);
    return NULL;
}

int Rbc_GetComplex(Tcl_Interp *interp, Tcl_Obj *objPtr, Rbc_Complex *valuePtr) {
    Tcl_Size objc;
    Tcl_Obj **objv;
    double real;

    /*
     * A two-element Tcl list is the canonical complex representation:
     *
     *     {real imag}
     *
     * Check this before Rbc_GetDouble(), because Rbc_GetDouble()
     * also accepts Tcl expressions.  For example, {3 -4} would
     * otherwise be evaluated as the expression "3 - 4".
     */
    if (Tcl_ListObjGetElements(NULL, objPtr, &objc, &objv) == TCL_OK) {
        if (objc == 2) {
            if (Rbc_GetDouble(interp, objv[0], &valuePtr->real) != TCL_OK) {
                return TCL_ERROR;
            }
            if (Rbc_GetDouble(interp, objv[1], &valuePtr->imag) != TCL_OK) {
                return TCL_ERROR;
            }
            return TCL_OK;
        }
    }
    /*
     * Otherwise accept a scalar real value (including an expression)
     * and promote it to complex with zero imaginary part.
     */
    if (Rbc_GetDouble(interp, objPtr, &real) == TCL_OK) {
        valuePtr->real = real;
        valuePtr->imag = 0.0;
        return TCL_OK;
    }
    Tcl_ResetResult(interp);
    Tcl_SetObjResult(interp, Tcl_NewStringObj("expected complex value as a real number or {real imag}", -1));
    return TCL_ERROR;
}

Rbc_Complex Rbc_ComplexFromReal(double value) {
    Rbc_Complex result;

    result.real = value;
    result.imag = 0.0;
    return result;
}

int Rbc_ComplexIsZero(Rbc_Complex value) { return ((value.real == 0.0) && (value.imag == 0.0)); }

Rbc_Complex Rbc_ComplexAdd(Rbc_Complex a, Rbc_Complex b) {
    Rbc_Complex result;

    result.real = a.real + b.real;
    result.imag = a.imag + b.imag;
    return result;
}

Rbc_Complex Rbc_ComplexSub(Rbc_Complex a, Rbc_Complex b) {
    Rbc_Complex result;

    result.real = a.real - b.real;
    result.imag = a.imag - b.imag;
    return result;
}

Rbc_Complex Rbc_ComplexMul(Rbc_Complex a, Rbc_Complex b) {
    Rbc_Complex result;

    result.real = (a.real * b.real) - (a.imag * b.imag);
    result.imag = (a.real * b.imag) + (a.imag * b.real);
    return result;
}

Rbc_Complex Rbc_ComplexDiv(Rbc_Complex a, Rbc_Complex b) {
    Rbc_Complex result;
    /*
     * Use the ratio form rather than directly computing
     *
     *     b.real*b.real + b.imag*b.imag
     *
     * to reduce unnecessary overflow/underflow.
     *
     * The caller must reject b == 0+0i.
     */
    if (fabs(b.real) >= fabs(b.imag)) {
        double ratio;
        double denominator;

        ratio = b.imag / b.real;
        denominator = b.real + b.imag * ratio;
        result.real = (a.real + a.imag * ratio) / denominator;
        result.imag = (a.imag - a.real * ratio) / denominator;

    } else {
        double ratio;
        double denominator;

        ratio = b.real / b.imag;
        denominator = b.imag + b.real * ratio;
        result.real = (a.real * ratio + a.imag) / denominator;
        result.imag = (a.imag * ratio - a.real) / denominator;
    }
    return result;
}

Rbc_Complex Rbc_VectorValueAsComplex(VectorObject *vPtr, Tcl_Size index) {
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        return Rbc_ComplexFromReal(vPtr->data.real[index]);
    case RBC_VECTOR_COMPLEX:
        return vPtr->data.complex[index];
    }
    Tcl_Panic("bad vector type %d", (int)vPtr->type);
    return Rbc_ComplexFromReal(0.0);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_FreeVectorId --
 *
 *      Releases the token for an existing vector.  This
 *      indicates that the client is no longer interested
 *      the vector.  Any previously specified callback
 *      routine will no longer be invoked when (and if) the
 *      vector changes.
 *
 * Parameters:
 *      Rbc_VectorId clientId - Client token identifying the vector
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Any previously specified callback routine will no
 *      longer be invoked when (and if) the vector changes.
 *
 *--------------------------------------------------------------
 */
void Rbc_FreeVectorId(Rbc_VectorId clientId) {
    VectorClient *clientPtr = (VectorClient *)clientId;

    if (clientPtr->magic != VECTOR_MAGIC) {
        return; /* Not a valid token */
    }
    if (clientPtr->serverPtr != NULL) {
        /* Remove the client from the server's list */
        Rbc_ChainDeleteLink(clientPtr->serverPtr->chainPtr, clientPtr->linkPtr);
    }
    ckfree((char *)clientPtr);
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_GetVectorById --
 *
 *      Returns a pointer to the vector associated with the client
 *      token.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Rbc_VectorId clientId - Client token identifying the vector
 *      Rbc_Vector **vecPtrPtr
 *
 * Results:
 *      A standard Tcl result.  If the client token is not associated
 *      with a vector any longer, TCL_ERROR is returned. Otherwise,
 *      TCL_OK is returned and vecPtrPtr will point to vector.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int Rbc_GetVectorById(Tcl_Interp *interp, Rbc_VectorId clientId, Rbc_Vector **vecPtrPtr) {
    VectorClient *clientPtr = (VectorClient *)clientId;

    if (clientPtr->magic != VECTOR_MAGIC) {
        Rbc_AppendResultStrings(interp, "bad vector token", (char *)NULL);
        return TCL_ERROR;
    }
    if (clientPtr->serverPtr == NULL) {
        Rbc_AppendResultStrings(interp, "vector no longer exists", (char *)NULL);
        return TCL_ERROR;
    }
    /*
     * Preserve the existing behaviour that obtaining a vector by ID
     * leaves its cached range current, but do not rescan an already
     * current vector.
     */    
    if (clientPtr->serverPtr->notifyFlags & UPDATE_RANGE) {
        Rbc_VectorUpdateRange(clientPtr->serverPtr);
    }
    *vecPtrPtr = (Rbc_Vector *)clientPtr->serverPtr;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VectorExists2 --
 *
 *      Returns whether the vector associated with the client token
 *      still exists.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *vecName
 *
 * Results:
 *      Returns 1 is the vector still exists, 0 otherwise.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int Rbc_VectorExists2(Tcl_Interp *interp, const char *vecName) {
    VectorInterpData *dataPtr; /* Interpreter-specific data. */

    dataPtr = Rbc_VectorGetInterpData(interp);
    if (GetVectorObject(dataPtr, vecName, NS_SEARCH_BOTH) != NULL) {
        return TRUE;
    }
    return FALSE;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_AllocVectorId --
 *
 *      Creates an identifier token for an existing vector.
 *      The identifier is used by the client routines to get
 *      call backs when (and if) the vector changes.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *name
 *
 * Results:
 *      A standard Tcl result.  If "vecName" is not associated
 *      with a vector, TCL_ERROR is returned and the interpreter
 *   c  result is filled with an error message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Rbc_VectorId Rbc_AllocVectorId(Tcl_Interp *interp, const char *name) {
    VectorInterpData *dataPtr; /* Interpreter-specific data. */
    VectorObject *vPtr;
    VectorClient *clientPtr;
    Rbc_VectorId clientId;
    int result;
    char *nameCopy;

    dataPtr = Rbc_VectorGetInterpData(interp);
    /*
     * If the vector name was passed via a read-only string (e.g. "x"),
     * the VectorParseName routine will segfault when it tries to write
     * into the string.  Therefore make a writable copy and free it
     * when we're done.
     */
    nameCopy = RbcStrdup(name);
    result = Rbc_VectorLookupName(dataPtr, nameCopy, &vPtr);
    ckfree((char *)nameCopy);

    if (result != TCL_OK) {
        return (Rbc_VectorId)0;
    }
    /* Allocate a new client structure */
    clientPtr = RbcCalloc(1, sizeof(VectorClient));
    assert(clientPtr);
    clientPtr->magic = VECTOR_MAGIC;

    /* Add the new client to the server's list of clients */
    clientPtr->linkPtr = Rbc_ChainAppend(vPtr->chainPtr, clientPtr);
    clientPtr->serverPtr = vPtr;
    clientId = (Rbc_VectorId)clientPtr;
    return clientId;
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_SetVectorChangedProc --
 *
 *      Sets the routine to be called back when the vector is changed
 *      or deleted.  *clientData* will be provided as an argument. If
 *      *proc* is NULL, no callback will be made.
 *
 * Parameters:
 *      Rbc_VectorId clientId - Client token identifying the vector
 *      Rbc_VectorChangedProc *proc - Address of routine to call when the contents of the vector change. If NULL, no 
 *                                    routine will be called
 *      ClientData clientData - One word of information to pass along when the above routine is called
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The designated routine will be called when the vector is changed
 *      or deleted.
 *
 * -----------------------------------------------------------------------
 */
void Rbc_SetVectorChangedProc(Rbc_VectorId clientId, Rbc_VectorChangedProc *proc, ClientData clientData) {
    VectorClient *clientPtr = (VectorClient *)clientId;

    if (clientPtr->magic != VECTOR_MAGIC) {
        return; /* Not a valid token */
    }
    clientPtr->clientData = clientData;
    clientPtr->proc = proc;
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_VectorGetChangedRange --
 *
 *      Returns the inclusive source range associated with the vector
 *      update currently being delivered to a client.
 *
 * Results:
 *      TRUE when an exact changed range is available.
 *
 *      FALSE when the update must be treated as affecting the complete
 *      vector, or when the client token is invalid.
 *
 * -----------------------------------------------------------------------
 */
int Rbc_VectorGetChangedRange(Rbc_VectorId clientId, Tcl_Size *firstPtr, Tcl_Size *lastPtr) {
    VectorClient *clientPtr;

    clientPtr = (VectorClient *)clientId;
    if ((clientPtr == NULL) || (clientPtr->magic != VECTOR_MAGIC) || (clientPtr->serverPtr == NULL) ||
        clientPtr->updateAll || !clientPtr->updateRangeValid) {
        return FALSE;
    }
    if (firstPtr != NULL) {
        *firstPtr = clientPtr->updateFirst;
    }
    if (lastPtr != NULL) {
        *lastPtr = clientPtr->updateLast;
    }
    return TRUE;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_NameOfVectorId --
 *
 *      Returns the name of the vector (and array variable).
 *
 * Parameters:
 *      Rbc_VectorId clientId - Client token identifying the vector
 *
 * Results:
 *      The name of the array variable is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char *Rbc_NameOfVectorId(Rbc_VectorId clientId) {
    VectorClient *clientPtr = (VectorClient *)clientId;

    if ((clientPtr->magic != VECTOR_MAGIC) || (clientPtr->serverPtr == NULL)) {
        return NULL;
    }
    return clientPtr->serverPtr->name;
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_GetVector --
 *
 *      Returns a pointer to the vector associated with the given name.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *name
 *      Rbc_Vector **vecPtrPtr
 *
 * Results:
 *      A standard Tcl result.  If there is no vector "name", TCL_ERROR
 *      is returned.  Otherwise TCL_OK is returned and vecPtrPtr will
 *      point to the vector.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
int Rbc_GetVector(Tcl_Interp *interp, const char *name, Rbc_Vector **vecPtrPtr) {
    VectorInterpData *dataPtr;
    VectorObject *vPtr;

    dataPtr = Rbc_VectorGetInterpData(interp);
    if (Rbc_VectorLookupName(dataPtr, name, &vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (vPtr->notifyFlags & UPDATE_RANGE) {
        Rbc_VectorUpdateRange(vPtr);
    }
    *vecPtrPtr = (Rbc_Vector *)vPtr;
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_CreateVector --
 *
 *      Creates a new vector by the name and size.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *vecName
 *      const char *cmdName
 *      const char *varName
 *      int initialSize
 *      Rbc_Vector **vecPtrPtr
 *
 * Results:
 *      A standard Tcl result.  If the new array size is invalid or a
 *      vector already exists by that name, TCL_ERROR is returned.
 *      Otherwise TCL_OK is returned and the new vector is created.
 *
 * Side Effects:
 *      Memory will be allocated for the new vector.  A new Tcl command
 *      and Tcl array variable will be created.
 *
 * -----------------------------------------------------------------------
 */
int Rbc_CreateVector2(Tcl_Interp *interp, const char *vecName, const char *cmdName, const char *varName,
                      Tcl_Size initialSize, Rbc_VectorType type, Rbc_Vector **vecPtrPtr) {
    VectorInterpData *dataPtr;
    VectorObject *vPtr;
    int isNew;

    if (initialSize < 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad vector size \"%" TCL_SIZE_MODIFIER "d\"", initialSize));
        return TCL_ERROR;
    }
    if ((type != RBC_VECTOR_REAL) && (type != RBC_VECTOR_COMPLEX)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad vector type %d", (int)type));
        return TCL_ERROR;
    }
    dataPtr = Rbc_VectorGetInterpData(interp);
    vPtr = Rbc_VectorCreate(dataPtr, vecName, cmdName, varName, type, &isNew);
    if (vPtr == NULL) {
        return TCL_ERROR;
    }
    if (initialSize > 0) {
        if (Rbc_VectorChangeLength(vPtr, initialSize) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (vecPtrPtr != NULL) {
        *vecPtrPtr = (Rbc_Vector *)vPtr;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_CreateVector --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *name
 *      int size
 *      Rbc_Vector **vecPtrPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_CreateVector(Tcl_Interp *interp, const char *name, Tcl_Size size, Rbc_Vector **vecPtrPtr) {
    return Rbc_CreateVector2(interp, name, name, name, size, RBC_VECTOR_REAL, vecPtrPtr);
}

int Rbc_CreateVectorWithType(Tcl_Interp *interp, const char *name, Tcl_Size size, Rbc_VectorType type,
                             Rbc_Vector **vecPtrPtr) {
    const char *varName;

    /*
     * Match the Tcl-level creation semantics: real vectors receive
     * their traditional automatic array mapping, complex vectors do
     * not.
     */
    varName = (type == RBC_VECTOR_REAL) ? name : NULL;
    return Rbc_CreateVector2(interp, name, name, varName, size, type, vecPtrPtr);
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_ResizeVector --
 *
 *      Changes the size of the vector.  All clients with designated
 *      callback routines will be notified of the size change.
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *      int length
 *
 * Results:
 *      A standard Tcl result.  If no vector exists by that name,
 *      TCL_ERROR is returned.  Otherwise TCL_OK is returned and
 *      vector is resized.
 *
 * Side Effects:
 *      Memory may be reallocated for the new vector size.  All clients
 *      which set call back procedures will be notified.
 *
 * -----------------------------------------------------------------------
 */
int Rbc_ResizeVector(Rbc_Vector *vecPtr, Tcl_Size length) {
    VectorObject *vPtr = (VectorObject *)vecPtr;

    if (Rbc_VectorChangeLength(vPtr, length) != TCL_OK) {
        Tcl_SetObjResult(vPtr->interp, Tcl_ObjPrintf("can't resize vector \"%s\"", vPtr->name));
        return TCL_ERROR;
    }
    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }
    Rbc_VectorUpdateClients(vPtr);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_NameOfVector --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr - Vector to query.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
char *Rbc_NameOfVector(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    return vPtr->name;
}

int Rbc_ResetComplexVector(Rbc_Vector *vecPtr, Rbc_Complex *valueArr, Tcl_Size length, Tcl_Size size,
                           Tcl_FreeProc *freeProc) {
    VectorObject *vPtr;

    vPtr = (VectorObject *)vecPtr;
    if (vPtr->type != RBC_VECTOR_COMPLEX) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("Rbc_ResetComplexVector cannot reset a real vector", -1));
        return TCL_ERROR;
    }
    return ResetVectorStorage(vPtr, valueArr, length, size, freeProc);
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_ResetVector --
*
 * Replaces the vector's storage contract. The supplied array,
 * length, capacity, and ownership policy become the vector's
 * current storage description. Clients are notified after the
 * reset succeeds.
 *
 * Use Rbc_VectorChanged() instead when modifying the existing
 * vector storage in place without changing its ownership contract.
 *
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *      double *valueArr - Array containing the elements of the vector. If NULL, indicates to reset the vector.
 *      int length - The number of elements that the vector currently holds.
 *      int size - The maximum number of elements that the array can hold.
 *      Tcl_FreeProc *freeProc - Address of memory deallocation routine for the array of values. Can also be 
 *                               TCL_STATIC, TCL_DYNAMIC, or TCL_VOLATILE.
 *
 * Results:
 *      A standard Tcl result.  If the new array size is invalid,
 *      TCL_ERROR is returned.  Otherwise TCL_OK is returned and the
 *      new vector data is recorded.
 *
 * Side Effects:
 *      Any client designated callbacks will be posted.  Memory may
 *      be changed for the vector array.
 *
 * -----------------------------------------------------------------------
 */
int Rbc_ResetVector(Rbc_Vector *vecPtr, double *valueArr, Tcl_Size length, Tcl_Size size, Tcl_FreeProc *freeProc) {
    return Rbc_VectorReset((VectorObject *)vecPtr, valueArr, length, size, freeProc);
}

void Rbc_VectorChanged(Rbc_Vector *vecPtr) {
    VectorObject *vPtr;

    vPtr = (VectorObject *)vecPtr;
    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }
    Rbc_VectorUpdateClients(vPtr);
}

void Rbc_FreeVector(Rbc_Vector *v) { Rbc_VectorFree((VectorObject *)v); }

Rbc_Complex *Rbc_VectorComplexData(Rbc_Vector *vPtr) {
    if (vPtr->type != RBC_VECTOR_COMPLEX) {
        return NULL;
    }
    return vPtr->data.complex;
}

double *Rbc_VectorData(Rbc_Vector *vPtr) {
    if (vPtr->type != RBC_VECTOR_REAL) {
        return NULL;
    }
    return vPtr->data.real;
}

Tcl_Size Rbc_VectorLength(Rbc_Vector *vPtr) { return vPtr->length; }
Tcl_Size Rbc_VectorSize(Rbc_Vector *vPtr) { return vPtr->size; }
int Rbc_VectorDirty(Rbc_Vector *vPtr) { return vPtr->dirty; }
Rbc_VectorType Rbc_VectorGetType(Rbc_Vector *vPtr) { return vPtr->type; }
