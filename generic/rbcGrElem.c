/*
 * rbcGrElem.c --
 *
 *      This module implements generic elements for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include <ctype.h>
#include <limits.h>
#include "rbcGraph.h"
#include "rbcChain.h"
#include <X11/Xutil.h>

static Tk_OptionParseProc StringToAlong;
static Tk_OptionPrintProc AlongToString;
static Tk_CustomOption alongOption = {StringToAlong, AlongToString, (ClientData)0};
extern Tk_CustomOption rbcDistanceOption;

#include "rbcGrElem.h"

static Rbc_VectorChangedProc VectorChangedProc;

static int GetPenStyleFromObj(Graph *graphPtr, Tcl_Obj *objPtr, Rbc_Uid type, PenStyle *stylePtr);
static void SyncElemVector(ElemVector *vPtr);
static void FindRange(ElemVector *vPtr);
static int EvalExprListObj(Tcl_Interp *interp, Tcl_Obj *listObjPtr, int *nElemPtr, double **arrayPtr);
static int GetIndex(Tcl_Interp *interp, Element *elemPtr, const char *string, int *indexPtr);
static int NameToElement(Graph *graphPtr, Tcl_Obj *nameObj, Element **elemPtrPtr);
static int CreateElement(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid);
static void DestroyElement(Graph *graphPtr, Element *elemPtr);
static int RebuildDisplayList(Graph *graphPtr, Tcl_Obj *newList);
static int InitElementOptions(Graph *graphPtr, Element *elemPtr);
static int ConfigureElementOptions(Graph *graphPtr, Element *elemPtr, Tcl_Size objc, Tcl_Obj *const objv[],
                                   int *maskPtr);
static void ReleaseElementResources(Graph *graphPtr, Element *elemPtr);

typedef int RbcGrElementOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcGrElementOp *proc;
} ElementOpSpec;
static RbcGrElementOp ActivateOp;
static RbcGrElementOp BindOp;
static RbcGrElementOp CreateOp;
static RbcGrElementOp ConfigureOp;
static RbcGrElementOp DeactivateOp;
static RbcGrElementOp DeleteOp;
static RbcGrElementOp ExistsOp;
static RbcGrElementOp GetOp;
static RbcGrElementOp NamesOp;
static RbcGrElementOp ShowOp;
static RbcGrElementOp TypeOp;

typedef enum {
    ELEM_DATA_OPTION_NONE,
    ELEM_DATA_OPTION_PAIRS,
    ELEM_DATA_OPTION_WEIGHTS,
    ELEM_DATA_OPTION_X,
    ELEM_DATA_OPTION_Y,
    ELEM_DATA_OPTION_X_ERROR,
    ELEM_DATA_OPTION_X_HIGH,
    ELEM_DATA_OPTION_X_LOW,
    ELEM_DATA_OPTION_Y_ERROR,
    ELEM_DATA_OPTION_Y_HIGH,
    ELEM_DATA_OPTION_Y_LOW
} ElemDataOption;

#define ELEM_DATA_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

typedef enum { ELEM_PEN_OPTION_NONE, ELEM_PEN_OPTION_ACTIVE, ELEM_PEN_OPTION_NORMAL } ElemPenOption;

#define ELEM_PEN_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

typedef enum { ELEM_AXIS_OPTION_NONE, ELEM_AXIS_OPTION_X, ELEM_AXIS_OPTION_Y } ElemAxisOption;

#define ELEM_AXIS_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

/*
 * ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */
/*
 *----------------------------------------------------------------------
 *
 * GetPenStyleFromObj --
 *
 *      Parses one palette-style specification.
 *
 *      A style is either:
 *
 *          penName
 *
 *      or:
 *
 *          penName min max
 *
 * Parameters:
 *      Graph *graphPtr     - Owning graph.
 *      Tcl_Obj *objPtr     - Style specification.
 *      Rbc_Uid type        - Required pen type.
 *      PenStyle *stylePtr  - Receives the parsed style.
 *
 * Results:
 *      TCL_OK if the style is valid.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Acquires a reference to the selected pen on success. The
 *      destination is not modified until every fallible numeric
 *      conversion has succeeded.
 *
 *----------------------------------------------------------------------
 */
static int GetPenStyleFromObj(Graph *graphPtr, Tcl_Obj *objPtr, Rbc_Uid type, PenStyle *stylePtr) {
    Tcl_Interp *interp;
    Tcl_Obj **objv;
    Tcl_Size objc;
    Pen *penPtr;
    double min;
    double max;

    interp = graphPtr->interp;

    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }

    if ((objc != 1) && (objc != 3)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad style \"%s\": should be "
                                               "\"penName\" or \"penName min max\"",
                                               Tcl_GetString(objPtr)));
        return TCL_ERROR;
    }

    /*
     * Validate the numeric range before acquiring a pen reference.
     */
    if (objc == 3) {
        if (Tcl_GetDoubleFromObj(interp, objv[1], &min) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Tcl_GetDoubleFromObj(interp, objv[2], &max) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    penPtr = NULL;

    if (Rbc_GetPen(graphPtr, Tcl_GetString(objv[0]), type, &penPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    if (objc == 3) {
        SetWeight(stylePtr->weight, min, max);
    }

    stylePtr->penPtr = penPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SyncElemVector --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ElemVector *vPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void SyncElemVector(ElemVector *vPtr) {
    vPtr->nValues = Rbc_VecLength(vPtr->vecPtr);
    vPtr->arraySize = Rbc_VecSize(vPtr->vecPtr);
    vPtr->valueArr = Rbc_VecData(vPtr->vecPtr);
    vPtr->min = Rbc_VecMin(vPtr->vecPtr);
    vPtr->max = Rbc_VecMax(vPtr->vecPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * FindRange --
 *
 *      Find the minimum, positive minimum, and maximum values in a
 *      given vector and store the results in the vector structure.
 *
 * Parameters:
 *      ElemVector *vPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Minimum, positive minimum, and maximum values are stored in
 *      the vector.
 *
 *----------------------------------------------------------------------
 */
static void FindRange(ElemVector *vPtr) {
    register int i;
    register double *x;
    register double min, max;

    if ((vPtr->nValues < 1) || (vPtr->valueArr == NULL)) {
        return; /* This shouldn't ever happen. */
    }
    x = vPtr->valueArr;

    min = DBL_MAX, max = -DBL_MAX;
    for (i = 0; i < vPtr->nValues; i++) {
        if (FINITE(x[i])) {
            min = max = x[i];
            break;
        }
    }
    /*  Initialize values to track the vector range */
    for (/* empty */; i < vPtr->nValues; i++) {
        if (FINITE(x[i])) {
            if (x[i] < min) {
                min = x[i];
            } else if (x[i] > max) {
                max = x[i];
            }
        }
    }
    vPtr->min = min, vPtr->max = max;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FindElemVectorMinimum --
 *
 *      Find the minimum, positive minimum, and maximum values in a
 *      given vector and store the results in the vector structure.
 *
 * Parameters:
 *      ElemVector *vPtr
 *      double minLimit
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Minimum, positive minimum, and maximum values are stored in
 *      the vector.
 *
 *----------------------------------------------------------------------
 */
double Rbc_FindElemVectorMinimum(ElemVector *vPtr, double minLimit) {
    register int i;
    register double *arr;
    register double min, x;

    min = DBL_MAX;
    arr = vPtr->valueArr;
    for (i = 0; i < vPtr->nValues; i++) {
        x = arr[i];
        if (x < 0.0) {
            /* What do you do about negative values when using log
             * scale values seems like a grey area.  Mirror. */
            x = -x;
        }
        if ((x > minLimit) && (min > x)) {
            min = x;
        }
    }
    if (min == DBL_MAX) {
        min = minLimit;
    }
    return min;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeElemVector --
 *
 *      Releases the storage or vector-client token owned by an
 *      element data vector.
 *
 * Parameters:
 *      ElemVector *vPtr - Vector to release and clear.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Releases an external vector client token or an internally
 *      allocated value array. The complete ElemVector structure is
 *      reset to zero.
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeElemVector(ElemVector *vPtr) {
    if (vPtr->clientId != NULL) {
        Rbc_FreeVectorId(vPtr->clientId);
    } else if (vPtr->valueArr != NULL) {
        ckfree((char *)vPtr->valueArr);
    }
    memset(vPtr, 0, sizeof(*vPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * VectorChangedProc --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      ClientData clientData
 *      Rbc_VectorNotify notify
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Graph is redrawn.
 *
 *----------------------------------------------------------------------
 */
static void VectorChangedProc(Tcl_Interp *interp, ClientData clientData, Rbc_VectorNotify notify) {
    ElemVector *vPtr = clientData;
    Element *elemPtr = vPtr->elemPtr;
    Graph *graphPtr = elemPtr->graphPtr;

    switch (notify) {
    case RBC_VECTOR_NOTIFY_DESTROY:
        vPtr->clientId = NULL;
        vPtr->valueArr = NULL;
        vPtr->nValues = 0;
        break;

    case RBC_VECTOR_NOTIFY_UPDATE:
    default:
        Rbc_GetVectorById(interp, vPtr->clientId, &vPtr->vecPtr);
        SyncElemVector(vPtr);
        break;
    }
    graphPtr->flags |= RESET_AXES;
    elemPtr->flags |= MAP_ITEM;
    if (!elemPtr->hidden) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * EvalExprListObj --
 *
 *      Converts a Tcl list of numeric expressions into a newly
 *      allocated array of doubles.
 *
 * Parameters:
 *      Tcl_Interp *interp    - Interpreter for list and expression
 *                              parsing.
 *      Tcl_Obj *listObjPtr   - Tcl list containing numeric expressions.
 *      int *nElemPtr         - Receives the number of parsed values.
 *      double **arrayPtr     - Receives the allocated value array.
 *
 * Results:
 *      TCL_OK if all list elements were evaluated successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Allocates an array owned by the caller on success. On failure,
 *      any partially allocated array is released and the interpreter
 *      result describes the error.
 *
 *----------------------------------------------------------------------
 */
static int EvalExprListObj(Tcl_Interp *interp, Tcl_Obj *listObjPtr, int *nElemPtr, double **arrayPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_Size i;
    double *array;

    *nElemPtr = 0;
    *arrayPtr = NULL;

    if (Tcl_ListObjGetElements(interp, listObjPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }

    if (objc > INT_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("too many data points", -1));
        return TCL_ERROR;
    }

    array = NULL;
    if (objc > 0) {
        array = (double *)ckalloc(sizeof(double) * (size_t)objc);

        for (i = 0; i < objc; i++) {
            /*
             * Preserve the legacy behaviour: every list member may
             * be a Tcl numeric expression, not only a plain double.
             */
            if (Tcl_ExprDouble(interp, Tcl_GetString(objv[i]), array + i) != TCL_OK) {
                ckfree((char *)array);
                return TCL_ERROR;
            }
        }
    }

    *nElemPtr = (int)objc;
    *arrayPtr = array;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ParseElemVectorObj --
 *
 *      Parses an element-vector option into temporary storage without
 *      modifying the live element vector.
 *
 *      The value may be either the name of an existing RBC vector or
 *      a Tcl list of numeric expressions.
 *
 * Parameters:
 *      Tcl_Interp *interp         - Interpreter for error reporting.
 *      Element *elemPtr           - Element that will own the vector.
 *      Tcl_Obj *objPtr            - New option value.
 *      ElemVector *candidatePtr   - Receives the staged vector.
 *
 * Results:
 *      TCL_OK if the value was parsed successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      May allocate a literal value array or an RBC vector client ID.
 *      No callback is installed and the live element is not modified.
 *
 *----------------------------------------------------------------------
 */
int Rbc_ParseElemVectorObj(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr, ElemVector *candidatePtr) {
    const char *string;

    memset(candidatePtr, 0, sizeof(*candidatePtr));
    candidatePtr->elemPtr = elemPtr;

    string = Tcl_GetString(objPtr);

    if (Rbc_VectorExists2(interp, string)) {
        Rbc_VectorId clientId;

        clientId = Rbc_AllocVectorId(interp, string);
        if (clientId == NULL) {
            return TCL_ERROR;
        }

        if (Rbc_GetVectorById(interp, clientId, &candidatePtr->vecPtr) != TCL_OK) {
            Rbc_FreeVectorId(clientId);
            memset(candidatePtr, 0, sizeof(*candidatePtr));
            return TCL_ERROR;
        }

        candidatePtr->clientId = clientId;
        SyncElemVector(candidatePtr);

        /*
         * Do not install VectorChangedProc yet. Its clientData must
         * point to the final, stable ElemVector member, not this
         * temporary candidate.
         */
        return TCL_OK;
    }

    if (EvalExprListObj(interp, objPtr, &candidatePtr->nValues, &candidatePtr->valueArr) != TCL_OK) {
        memset(candidatePtr, 0, sizeof(*candidatePtr));
        return TCL_ERROR;
    }

    candidatePtr->arraySize = candidatePtr->nValues;
    FindRange(candidatePtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ParseElemVectorPairsObj --
 *
 *      Parses a flat Tcl list of X/Y coordinate pairs into two
 *      temporary element vectors without modifying the live element.
 *
 * Parameters:
 *      Tcl_Interp *interp           - Interpreter for error reporting.
 *      Element *elemPtr             - Element that will own the data.
 *      Tcl_Obj *objPtr              - Flat list of X/Y values.
 *      ElemVector *xCandidatePtr    - Receives staged X values.
 *      ElemVector *yCandidatePtr    - Receives staged Y values.
 *
 * Results:
 *      TCL_OK if all coordinate pairs were parsed successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Allocates value arrays on success. The caller must either
 *      commit or free both candidates.
 *
 *----------------------------------------------------------------------
 */
int Rbc_ParseElemVectorPairsObj(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr, ElemVector *xCandidatePtr,
                                ElemVector *yCandidatePtr) {
    double *pairArr;
    int nElem;
    int nValues;
    int i;

    memset(xCandidatePtr, 0, sizeof(*xCandidatePtr));
    memset(yCandidatePtr, 0, sizeof(*yCandidatePtr));

    xCandidatePtr->elemPtr = elemPtr;
    yCandidatePtr->elemPtr = elemPtr;

    if (EvalExprListObj(interp, objPtr, &nElem, &pairArr) != TCL_OK) {
        return TCL_ERROR;
    }

    if ((nElem & 1) != 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("odd number of data points", -1));

        if (pairArr != NULL) {
            ckfree((char *)pairArr);
        }
        return TCL_ERROR;
    }

    nValues = nElem / 2;

    if (nValues > 0) {
        xCandidatePtr->valueArr = (double *)ckalloc(sizeof(double) * (size_t)nValues);
        yCandidatePtr->valueArr = (double *)ckalloc(sizeof(double) * (size_t)nValues);

        for (i = 0; i < nValues; i++) {
            xCandidatePtr->valueArr[i] = pairArr[i * 2];
            yCandidatePtr->valueArr[i] = pairArr[i * 2 + 1];
        }

        ckfree((char *)pairArr);
    }

    xCandidatePtr->nValues = nValues;
    xCandidatePtr->arraySize = nValues;
    yCandidatePtr->nValues = nValues;
    yCandidatePtr->arraySize = nValues;

    FindRange(xCandidatePtr);
    FindRange(yCandidatePtr);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CommitElemVector --
 *
 *      Replaces a live element vector with a previously parsed
 *      candidate.
 *
 * Parameters:
 *      Element *elemPtr         - Element owning the destination.
 *      ElemVector *destPtr      - Live vector to replace.
 *      ElemVector *candidatePtr - Successfully parsed candidate.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Releases the previous live vector, transfers ownership from the
 *      candidate, and installs an external-vector change callback when
 *      required.
 *
 *----------------------------------------------------------------------
 */
void Rbc_CommitElemVector(Element *elemPtr, ElemVector *destPtr, ElemVector *candidatePtr) {
    Rbc_FreeElemVector(destPtr);

    *destPtr = *candidatePtr;
    memset(candidatePtr, 0, sizeof(*candidatePtr));

    destPtr->elemPtr = elemPtr;

    if (destPtr->clientId != NULL) {
        /*
         * The callback must refer to the stable destination member,
         * never to the temporary candidate.
         */
        Rbc_SetVectorChangedProc(destPtr->clientId, VectorChangedProc, destPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetElemDataOption --
 *
 *      Determines which bar data option is represented by an option
 *      name accepted by Tk_SetOptions.
 *
 *      Exact names take precedence over prefix matching. This matters
 *      for "-x" and "-y", which are exact option names as well as
 *      prefixes of several longer options.
 *
 * Parameters:
 *      Tcl_Obj *objPtr - Option-name object.
 *
 * Results:
 *      The corresponding ElemDataOption value. ELEM_DATA_OPTION_NONE is
 *      returned when the option is unrelated to bar data.
 *
 * Side Effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static ElemDataOption GetElemDataOption(Tcl_Obj *objPtr) {
    static const struct {
        const char *name;
        ElemDataOption option;
    } optionMap[] = {{"-data", ELEM_DATA_OPTION_PAIRS},
                     {"-weights", ELEM_DATA_OPTION_WEIGHTS},

                     {"-x", ELEM_DATA_OPTION_X},
                     {"-xdata", ELEM_DATA_OPTION_X},
                     {"-y", ELEM_DATA_OPTION_Y},
                     {"-ydata", ELEM_DATA_OPTION_Y},

                     {"-xerror", ELEM_DATA_OPTION_X_ERROR},
                     {"-xhigh", ELEM_DATA_OPTION_X_HIGH},
                     {"-xlow", ELEM_DATA_OPTION_X_LOW},

                     {"-yerror", ELEM_DATA_OPTION_Y_ERROR},
                     {"-yhigh", ELEM_DATA_OPTION_Y_HIGH},
                     {"-ylow", ELEM_DATA_OPTION_Y_LOW}};

    const char *string;
    Tcl_Size length;
    ElemDataOption match;
    size_t i;

    string = Tcl_GetStringFromObj(objPtr, &length);

    /*
     * Prefer an exact match. For example, "-x" must mean "-x", not an
     * ambiguous prefix of "-xdata", "-xerror", "-xhigh", and "-xlow".
     */
    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length == fullLength) && (memcmp(string, optionMap[i].name, (size_t)length) == 0)) {
            return optionMap[i].option;
        }
    }

    /*
     * Tk_SetOptions has already checked that an abbreviation is valid
     * and unambiguous. Repeat enough of that matching here to recover
     * the canonical data-option identity.
     */
    match = ELEM_DATA_OPTION_NONE;

    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length > 0) && (length < fullLength) && (strncmp(string, optionMap[i].name, (size_t)length) == 0)) {
            if (match == ELEM_DATA_OPTION_NONE) {
                match = optionMap[i].option;
            } else if (match != optionMap[i].option) {
                /*
                 * This should already have been rejected by
                 * Tk_SetOptions.
                 */
                return ELEM_DATA_OPTION_NONE;
            }
        }
    }

    return match;
}

/*
 *----------------------------------------------------------------------
 *
 * GetElemPenOption --
 *
 *      Determines whether an option represents "-activepen" or
 *      "-pen".
 *
 *      Tk_SetOptions has already validated abbreviations. This helper
 *      recovers the canonical option identity from the original
 *      option/value vector.
 *
 *----------------------------------------------------------------------
 */
static ElemPenOption GetElemPenOption(Tcl_Obj *objPtr) {
    static const struct {
        const char *name;
        ElemPenOption option;
    } optionMap[] = {{"-activepen", ELEM_PEN_OPTION_ACTIVE}, {"-pen", ELEM_PEN_OPTION_NORMAL}};

    const char *string;
    Tcl_Size length;
    ElemPenOption match;
    size_t i;

    string = Tcl_GetStringFromObj(objPtr, &length);

    /*
     * Prefer exact matches.
     */
    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length == fullLength) && (memcmp(string, optionMap[i].name, (size_t)length) == 0)) {
            return optionMap[i].option;
        }
    }

    /*
     * Recover a canonical option from an accepted abbreviation.
     */
    match = ELEM_PEN_OPTION_NONE;

    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length > 0) && (length < fullLength) && (strncmp(string, optionMap[i].name, (size_t)length) == 0)) {
            if (match == ELEM_PEN_OPTION_NONE) {
                match = optionMap[i].option;
            } else if (match != optionMap[i].option) {
                return ELEM_PEN_OPTION_NONE;
            }
        }
    }

    return match;
}

/*
 *----------------------------------------------------------------------
 *
 * GetElemAxisOption --
 *
 *      Determines whether an option represents "-mapx" or "-mapy".
 *
 *      Tk_SetOptions has already validated option abbreviations. This
 *      helper recovers the canonical option identity from the original
 *      option/value vector.
 *
 * Parameters:
 *      Tcl_Obj *objPtr - Option-name object.
 *
 * Results:
 *      The corresponding ElemAxisOption value.
 *      ELEM_AXIS_OPTION_NONE is returned for unrelated options.
 *
 * Side Effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static ElemAxisOption GetElemAxisOption(Tcl_Obj *objPtr) {
    static const struct {
        const char *name;
        ElemAxisOption option;
    } optionMap[] = {{"-mapx", ELEM_AXIS_OPTION_X}, {"-mapy", ELEM_AXIS_OPTION_Y}};

    const char *string;
    Tcl_Size length;
    ElemAxisOption match;
    size_t i;

    string = Tcl_GetStringFromObj(objPtr, &length);

    /*
     * Prefer exact matches.
     */
    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length == fullLength) && (memcmp(string, optionMap[i].name, (size_t)length) == 0)) {
            return optionMap[i].option;
        }
    }

    /*
     * Recover a canonical option from an accepted abbreviation.
     */
    match = ELEM_AXIS_OPTION_NONE;

    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length > 0) && (length < fullLength) && (strncmp(string, optionMap[i].name, (size_t)length) == 0)) {
            if (match == ELEM_AXIS_OPTION_NONE) {
                match = optionMap[i].option;
            } else if (match != optionMap[i].option) {
                return ELEM_AXIS_OPTION_NONE;
            }
        }
    }

    return match;
}

/*
 *----------------------------------------------------------------------
 *
 * IsElemStateOption --
 *
 *      Determines whether an option name represents "-state".
 *
 *      Tk_SetOptions has already validated abbreviations. This helper
 *      recovers the option identity from the original option/value
 *      vector.
 *
 * Parameters:
 *      Tcl_Obj *objPtr - Option-name object.
 *
 * Results:
 *      Non-zero if the option represents "-state"; zero otherwise.
 *
 * Side Effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int IsElemStateOption(Tcl_Obj *objPtr) {
    static const char optionName[] = "-state";
    const char *string;
    Tcl_Size length;
    Tcl_Size fullLength;

    string = Tcl_GetStringFromObj(objPtr, &length);
    fullLength = (Tcl_Size)(sizeof(optionName) - 1);

    return ((length > 0) && (length <= fullLength) && (strncmp(string, optionName, (size_t)length) == 0));
}

/*
 *----------------------------------------------------------------------
 *
 * IsElemBindTagsOption --
 *
 *      Determines whether an option name represents "-bindtags".
 *
 *      Tk_SetOptions has already validated abbreviations. This helper
 *      recovers the canonical option identity from the original
 *      option/value vector.
 *
 *----------------------------------------------------------------------
 */
static int IsElemBindTagsOption(Tcl_Obj *objPtr) {
    static const char optionName[] = "-bindtags";
    const char *string;
    Tcl_Size length;
    Tcl_Size fullLength;

    string = Tcl_GetStringFromObj(objPtr, &length);
    fullLength = (Tcl_Size)(sizeof(optionName) - 1);
    return ((length > 0) && (length <= fullLength) && (strncmp(string, optionName, (size_t)length) == 0));
}

/*
 *----------------------------------------------------------------------
 *
 * StageElemTags --
 *
 *      Parses a bind-tags value into temporary transaction storage
 *      without modifying the live element.
 *
 *      A previously staged candidate is released only after its
 *      replacement has parsed successfully.
 *
 *----------------------------------------------------------------------
 */
static int StageElemTags(Tcl_Interp *interp, Tcl_Obj *objPtr, ElemTagsTransaction *transactionPtr) {
    char **newTags;

    newTags = NULL;
    if (Rbc_GetStringListFromObj(interp, objPtr, &newTags) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Do not discard the previous candidate until the replacement has
     * parsed successfully.
     */
    if (transactionPtr->tags != NULL) {
        ckfree((char *)transactionPtr->tags);
    }
    transactionPtr->tags = newTags;
    transactionPtr->staged = TRUE;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeElemTagsTransaction --
 *
 *      Releases a staged bind-tags list and clears the transaction.
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeElemTagsTransaction(ElemTagsTransaction *transactionPtr) {
    if (transactionPtr->tags != NULL) {
        ckfree((char *)transactionPtr->tags);
    }

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PrepareElemTagsTransaction --
 *
 *      Parses all bind-tags values involved in the current modern
 *      configuration without modifying the live element.
 *
 *      Explicit occurrences are processed in their original order.
 *      An invalid earlier repeated occurrence therefore causes the
 *      complete configuration to fail.
 *
 *----------------------------------------------------------------------
 */
int Rbc_PrepareElemTagsTransaction(Graph *graphPtr, Element *elemPtr, ElemTagsTransaction *transactionPtr) {
    int explicitlySpecified;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));

    explicitlySpecified = FALSE;

    assert((elemPtr->optionObjc & 1) == 0);

    /*
     * Determine whether -bindtags was supplied explicitly.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemBindTagsOption(elemPtr->optionObjv[i])) {
            explicitlySpecified = TRUE;
        }
    }

    /*
     * On the first modern configuration, process the effective default
     * or option-database value unless it was explicitly overridden.
     */
    if (!elemPtr->optionsConfigured && !explicitlySpecified && (elemPtr->bindTagsObjPtr != NULL)) {
        if (StageElemTags(graphPtr->interp, elemPtr->bindTagsObjPtr, transactionPtr) != TCL_OK) {
            goto error;
        }
    }

    /*
     * Process every explicit occurrence in caller order.
     *
     * Do not read only bindTagsObjPtr here. That field contains the
     * final value and would conceal an invalid earlier occurrence.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemBindTagsOption(elemPtr->optionObjv[i])) {
            if (StageElemTags(graphPtr->interp, elemPtr->optionObjv[i + 1], transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }

    return TCL_OK;

error:
    Rbc_FreeElemTagsTransaction(transactionPtr);

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CommitElemTagsTransaction --
 *
 *      Replaces the live bind-tags list with a successfully staged
 *      candidate.
 *
 *----------------------------------------------------------------------
 */
void Rbc_CommitElemTagsTransaction(Element *elemPtr, ElemTagsTransaction *transactionPtr) {
    char **oldTags;

    if (!transactionPtr->staged) {
        return;
    }

    oldTags = elemPtr->tags;

    elemPtr->tags = transactionPtr->tags;
    transactionPtr->tags = NULL;
    transactionPtr->staged = FALSE;

    if (oldTags != NULL) {
        ckfree((char *)oldTags);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PrepareElemStateTransaction --
 *
 *      Parses all element state values involved in the current modern
 *      configuration without modifying the live element.
 *
 *      Explicit occurrences are processed in their original order.
 *      An invalid earlier repeated value therefore causes the complete
 *      configuration to fail, even if a later occurrence is valid.
 *
 * Parameters:
 *      Graph *graphPtr
 *          Graph containing the element.
 *
 *      Element *elemPtr
 *          Element being configured.
 *
 *      ElemStateTransaction *transactionPtr
 *          Receives the staged state.
 *
 * Results:
 *      TCL_OK if every relevant state value is valid.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Sets the interpreter result on invalid input. The live element
 *      state is not modified.
 *
 *----------------------------------------------------------------------
 */
int Rbc_PrepareElemStateTransaction(Graph *graphPtr, Element *elemPtr, ElemStateTransaction *transactionPtr) {
    int explicitlySpecified;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));

    explicitlySpecified = FALSE;

    assert((elemPtr->optionObjc & 1) == 0);

    /*
     * Determine whether -state was supplied explicitly.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemStateOption(elemPtr->optionObjv[i])) {
            explicitlySpecified = TRUE;
        }
    }

    /*
     * During the first modern configuration, process the effective
     * default or option-database value unless the caller explicitly
     * overrides it.
     *
     * Elements whose option tables do not contain -state leave
     * stateObjPtr as NULL, so nothing is staged for them.
     */
    if (!elemPtr->optionsConfigured && !explicitlySpecified && (elemPtr->stateObjPtr != NULL)) {
        if (Rbc_GetStateFromObj(graphPtr->interp, elemPtr->stateObjPtr, &transactionPtr->state) != TCL_OK) {
            return TCL_ERROR;
        }

        transactionPtr->staged = TRUE;
    }

    /*
     * Process every explicit occurrence in caller order.
     *
     * Reading only stateObjPtr would incorrectly hide an invalid
     * earlier repeated occurrence because stateObjPtr contains only
     * the final retained value.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemStateOption(elemPtr->optionObjv[i])) {
            if (Rbc_GetStateFromObj(graphPtr->interp, elemPtr->optionObjv[i + 1], &transactionPtr->state) != TCL_OK) {
                return TCL_ERROR;
            }

            transactionPtr->staged = TRUE;
        }
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CommitElemStateTransaction --
 *
 *      Commits a successfully parsed state to the live element.
 *
 * Parameters:
 *      Element *elemPtr
 *          Destination element.
 *
 *      ElemStateTransaction *transactionPtr
 *          Prepared state transaction.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Replaces elemPtr->state if a value was staged.
 *
 *----------------------------------------------------------------------
 */
void Rbc_CommitElemStateTransaction(Element *elemPtr, ElemStateTransaction *transactionPtr) {
    if (transactionPtr->staged) {
        elemPtr->state = transactionPtr->state;
        transactionPtr->staged = FALSE;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * StageElemAxis --
 *
 *      Resolves one element axis option into temporary transaction
 *      storage without modifying the live element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Obj *objPtr
 *      ElemAxisTransaction *transactionPtr
 *      Axis **candidatePtrPtr
 *      ElemAxisOption option
 *
 * Results:
 *      TCL_OK if the axis exists and has the required orientation.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Acquires an axis reference. A previously staged reference for
 *      the same option is released only after the replacement has been
 *      resolved successfully.
 *
 *----------------------------------------------------------------------
 */
static int StageElemAxis(Graph *graphPtr, Tcl_Obj *objPtr, ElemAxisTransaction *transactionPtr, Axis **candidatePtrPtr,
                         ElemAxisOption option) {
    Axis *newAxisPtr;
    Rbc_Uid classUid;
    unsigned int mask;

    newAxisPtr = NULL;

    switch (option) {
    case ELEM_AXIS_OPTION_X:
        classUid = rbcXAxisUid;
        break;

    case ELEM_AXIS_OPTION_Y:
        classUid = rbcYAxisUid;
        break;

    case ELEM_AXIS_OPTION_NONE:
    default:
        Tcl_Panic("StageElemAxis called with invalid option");
        return TCL_ERROR;
    }

    /*
     * Element -mapx and -mapy values must identify an existing axis;
     * an empty axis name is not accepted.
     */
    if (Rbc_GetAxisFromObj(graphPtr, objPtr, classUid, FALSE, &newAxisPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    mask = ELEM_AXIS_OPTION_MASK(option);

    /*
     * Resolve the replacement before releasing a previous candidate.
     */
    if (transactionPtr->stagedMask & mask) {
        Rbc_FreeAxisReference(graphPtr, *candidatePtrPtr);
    }

    *candidatePtrPtr = newAxisPtr;
    transactionPtr->stagedMask |= mask;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeElemAxisTransaction --
 *
 *      Releases all axis references owned by a staged element-axis
 *      transaction.
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeElemAxisTransaction(Graph *graphPtr, ElemAxisTransaction *transactionPtr) {
    if (transactionPtr->xAxisPtr != NULL) {
        Rbc_FreeAxisReference(graphPtr, transactionPtr->xAxisPtr);
    }

    if (transactionPtr->yAxisPtr != NULL) {
        Rbc_FreeAxisReference(graphPtr, transactionPtr->yAxisPtr);
    }

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PrepareElemAxisTransaction --
 *
 *      Resolves all X- and Y-axis mappings involved in the current
 *      modern element configuration without modifying the live
 *      element.
 *
 *      Explicit option occurrences are processed in caller order, so
 *      an invalid earlier repeated value still causes the complete
 *      configuration to fail.
 *
 *----------------------------------------------------------------------
 */
int Rbc_PrepareElemAxisTransaction(Graph *graphPtr, Element *elemPtr, ElemAxisTransaction *transactionPtr) {
    unsigned int explicitMask;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));

    explicitMask = 0;

    assert((elemPtr->optionObjc & 1) == 0);

    /*
     * Determine which axis options were supplied explicitly.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemAxisOption option;

        option = GetElemAxisOption(elemPtr->optionObjv[i]);

        if (option != ELEM_AXIS_OPTION_NONE) {
            explicitMask |= ELEM_AXIS_OPTION_MASK(option);
        }
    }

    /*
     * During the first modern configuration, process effective
     * default or option-database values that were not overridden by
     * the caller.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & ELEM_AXIS_OPTION_MASK(ELEM_AXIS_OPTION_X)) && (elemPtr->mapXObjPtr != NULL)) {
            if (StageElemAxis(graphPtr, elemPtr->mapXObjPtr, transactionPtr, &transactionPtr->xAxisPtr,
                              ELEM_AXIS_OPTION_X) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_AXIS_OPTION_MASK(ELEM_AXIS_OPTION_Y)) && (elemPtr->mapYObjPtr != NULL)) {
            if (StageElemAxis(graphPtr, elemPtr->mapYObjPtr, transactionPtr, &transactionPtr->yAxisPtr,
                              ELEM_AXIS_OPTION_Y) != TCL_OK) {
                goto error;
            }
        }
    }

    /*
     * Process explicit options in their original order.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemAxisOption option;
        Tcl_Obj *valueObjPtr;

        option = GetElemAxisOption(elemPtr->optionObjv[i]);

        valueObjPtr = elemPtr->optionObjv[i + 1];

        switch (option) {
        case ELEM_AXIS_OPTION_X:
            if (StageElemAxis(graphPtr, valueObjPtr, transactionPtr, &transactionPtr->xAxisPtr, option) != TCL_OK) {
                goto error;
            }
            break;

        case ELEM_AXIS_OPTION_Y:
            if (StageElemAxis(graphPtr, valueObjPtr, transactionPtr, &transactionPtr->yAxisPtr, option) != TCL_OK) {
                goto error;
            }
            break;

        case ELEM_AXIS_OPTION_NONE:
            break;
        }
    }

    return TCL_OK;

error:
    Rbc_FreeElemAxisTransaction(graphPtr, transactionPtr);

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CommitElemAxisTransaction --
 *
 *      Replaces the live element axes with successfully staged axis
 *      references.
 *
 *----------------------------------------------------------------------
 */
void Rbc_CommitElemAxisTransaction(Graph *graphPtr, Element *elemPtr, ElemAxisTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & ELEM_AXIS_OPTION_MASK(ELEM_AXIS_OPTION_X)) {
        Axis *oldAxisPtr;

        oldAxisPtr = elemPtr->axes.x;

        elemPtr->axes.x = transactionPtr->xAxisPtr;

        transactionPtr->xAxisPtr = NULL;

        if (oldAxisPtr != NULL) {
            Rbc_FreeAxisReference(graphPtr, oldAxisPtr);
        }
    }

    if (transactionPtr->stagedMask & ELEM_AXIS_OPTION_MASK(ELEM_AXIS_OPTION_Y)) {
        Axis *oldAxisPtr;

        oldAxisPtr = elemPtr->axes.y;

        elemPtr->axes.y = transactionPtr->yAxisPtr;

        transactionPtr->yAxisPtr = NULL;

        if (oldAxisPtr != NULL) {
            Rbc_FreeAxisReference(graphPtr, oldAxisPtr);
        }
    }

    transactionPtr->stagedMask = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * StageElemPen --
 *
 *      Resolves an element pen option into temporary transaction
 *      storage without modifying the live element.
 *
 *      An empty string represents no named pen. For the normal pen,
 *      the commit function later substitutes the embedded built-in
 *      pen. For the active pen, it disables the active pen.
 *
 *----------------------------------------------------------------------
 */
static int StageElemPen(Graph *graphPtr, Tcl_Obj *objPtr, Rbc_Uid penType, ElemPenTransaction *transactionPtr,
                        Pen **candidatePtrPtr, ElemPenOption option) {
    const char *name;
    Pen *newPenPtr;
    unsigned int mask;

    newPenPtr = NULL;

    if (objPtr != NULL) {
        name = Tcl_GetString(objPtr);

        if (name[0] != '\0') {
            if (Rbc_GetPen(graphPtr, name, penType, &newPenPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }

    mask = ELEM_PEN_OPTION_MASK(option);

    /*
     * Resolve the replacement before releasing an earlier staged
     * candidate.
     */
    if ((transactionPtr->stagedMask & mask) && (*candidatePtrPtr != NULL)) {
        Rbc_FreePen(graphPtr, *candidatePtrPtr);
    }

    *candidatePtrPtr = newPenPtr;
    transactionPtr->stagedMask |= mask;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeElemPenTransaction --
 *
 *      Releases all named-pen references owned by a staged element
 *      pen transaction.
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeElemPenTransaction(Graph *graphPtr, ElemPenTransaction *transactionPtr) {
    if (transactionPtr->activePenPtr != NULL) {
        Rbc_FreePen(graphPtr, transactionPtr->activePenPtr);
    }

    if (transactionPtr->normalPenPtr != NULL) {
        Rbc_FreePen(graphPtr, transactionPtr->normalPenPtr);
    }

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PrepareElemPenTransaction --
 *
 *      Resolves all active and normal pen options involved in the
 *      current modern configuration without modifying the live
 *      element.
 *
 *      Explicit occurrences are processed in caller order, so an
 *      invalid earlier repeated value still causes the complete
 *      configuration to fail.
 *
 *----------------------------------------------------------------------
 */
int Rbc_PrepareElemPenTransaction(Graph *graphPtr, Element *elemPtr, Rbc_Uid penType,
                                  ElemPenTransaction *transactionPtr) {
    unsigned int explicitMask;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));

    explicitMask = 0;

    assert((elemPtr->optionObjc & 1) == 0);

    /*
     * Determine which pen options were supplied explicitly.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemPenOption option;

        option = GetElemPenOption(elemPtr->optionObjv[i]);

        if (option != ELEM_PEN_OPTION_NONE) {
            explicitMask |= ELEM_PEN_OPTION_MASK(option);
        }
    }

    /*
     * On the first modern configuration, process effective option
     * database/default values not explicitly overridden.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & ELEM_PEN_OPTION_MASK(ELEM_PEN_OPTION_ACTIVE)) && (elemPtr->activePenObjPtr != NULL)) {
            if (StageElemPen(graphPtr, elemPtr->activePenObjPtr, penType, transactionPtr, &transactionPtr->activePenPtr,
                             ELEM_PEN_OPTION_ACTIVE) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_PEN_OPTION_MASK(ELEM_PEN_OPTION_NORMAL)) && (elemPtr->normalPenObjPtr != NULL)) {
            if (StageElemPen(graphPtr, elemPtr->normalPenObjPtr, penType, transactionPtr, &transactionPtr->normalPenPtr,
                             ELEM_PEN_OPTION_NORMAL) != TCL_OK) {
                goto error;
            }
        }
    }

    /*
     * Process explicit options in their original order.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemPenOption option;
        Tcl_Obj *valueObjPtr;

        option = GetElemPenOption(elemPtr->optionObjv[i]);

        valueObjPtr = elemPtr->optionObjv[i + 1];

        switch (option) {
        case ELEM_PEN_OPTION_ACTIVE:
            if (StageElemPen(graphPtr, valueObjPtr, penType, transactionPtr, &transactionPtr->activePenPtr, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_PEN_OPTION_NORMAL:
            if (StageElemPen(graphPtr, valueObjPtr, penType, transactionPtr, &transactionPtr->normalPenPtr, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_PEN_OPTION_NONE:
            break;
        }
    }

    return TCL_OK;

error:
    Rbc_FreeElemPenTransaction(graphPtr, transactionPtr);

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CommitElemPenTransaction --
 *
 *      Replaces the live element pens with successfully staged
 *      candidates.
 *
 *      An empty normal-pen value selects the element's embedded
 *      built-in pen.
 *
 *----------------------------------------------------------------------
 */
void Rbc_CommitElemPenTransaction(Graph *graphPtr, Element *elemPtr, Pen *builtinPenPtr,
                                  ElemPenTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & ELEM_PEN_OPTION_MASK(ELEM_PEN_OPTION_ACTIVE)) {
        Pen *oldPenPtr;

        oldPenPtr = elemPtr->activePenPtr;

        elemPtr->activePenPtr = transactionPtr->activePenPtr;

        transactionPtr->activePenPtr = NULL;

        if (oldPenPtr != NULL) {
            Rbc_FreePen(graphPtr, oldPenPtr);
        }
    }

    if (transactionPtr->stagedMask & ELEM_PEN_OPTION_MASK(ELEM_PEN_OPTION_NORMAL)) {
        Pen *oldPenPtr;
        Pen *newPenPtr;

        oldPenPtr = elemPtr->normalPenPtr;
        newPenPtr = transactionPtr->normalPenPtr;

        if (newPenPtr == NULL) {
            newPenPtr = builtinPenPtr;
        }

        elemPtr->normalPenPtr = newPenPtr;
        transactionPtr->normalPenPtr = NULL;

        /*
         * The embedded pen is owned by the concrete element and has no
         * named-pen reference to release.
         */
        if ((oldPenPtr != NULL) && (oldPenPtr != builtinPenPtr)) {
            Rbc_FreePen(graphPtr, oldPenPtr);
        }
    }

    transactionPtr->stagedMask = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeElemDataTransaction --
 *
 *      Releases all staged vectors owned by a element-data transaction.
 *
 * Parameters:
 *      ElemDataTransaction *transactionPtr - Transaction to clear.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Releases staged arrays and RBC vector client IDs. The transaction
 *      is reset to zero.
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeElemDataTransaction(ElemDataTransaction *transactionPtr) {
    Rbc_FreeElemVector(&transactionPtr->x);
    Rbc_FreeElemVector(&transactionPtr->y);
    Rbc_FreeElemVector(&transactionPtr->w);

    Rbc_FreeElemVector(&transactionPtr->xError);
    Rbc_FreeElemVector(&transactionPtr->xHigh);
    Rbc_FreeElemVector(&transactionPtr->xLow);

    Rbc_FreeElemVector(&transactionPtr->yError);
    Rbc_FreeElemVector(&transactionPtr->yHigh);
    Rbc_FreeElemVector(&transactionPtr->yLow);

    transactionPtr->stagedMask = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * StageElemDataVector --
 *
 *      Parses one element data-vector option into temporary transaction
 *      storage.
 *
 *      If the same destination was staged previously, the previous
 *      candidate is released only after the replacement has parsed
 *      successfully.
 *
 * Parameters:
 *      Tcl_Interp *interp                    - Interpreter.
 *      Element *elemPtr                      - Owning element.
 *      Tcl_Obj *objPtr                       - Original option value.
 *      ElemDataTransaction *transactionPtr    - Current transaction.
 *      ElemVector *candidatePtr              - Destination candidate.
 *      ElemDataOption option                  - Candidate identity.
 *
 * Results:
 *      TCL_OK on success; TCL_ERROR on parsing failure.
 *
 * Side Effects:
 *      May allocate temporary vector storage or a vector client ID.
 *      The live element vector is not modified.
 *
 *----------------------------------------------------------------------
 */
static int StageElemDataVector(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr, ElemDataTransaction *transactionPtr,
                              ElemVector *candidatePtr, ElemDataOption option) {
    ElemVector newCandidate;
    unsigned int mask;

    memset(&newCandidate, 0, sizeof(newCandidate));
    if (Rbc_ParseElemVectorObj(interp, elemPtr, objPtr, &newCandidate) != TCL_OK) {
        Rbc_FreeElemVector(&newCandidate);
        return TCL_ERROR;
    }
    mask = ELEM_DATA_OPTION_MASK(option);
    if (transactionPtr->stagedMask & mask) {
        Rbc_FreeElemVector(candidatePtr);
    }
    *candidatePtr = newCandidate;
    transactionPtr->stagedMask |= mask;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StageElemDataPairs --
 *
 *      Parses the "-data" value into temporary X and Y candidates.
 *
 * Parameters:
 *      Tcl_Interp *interp                    - Interpreter.
 *      Element *elemPtr                      - Owning element.
 *      Tcl_Obj *objPtr                       - Original "-data" value.
 *      ElemDataTransaction *transactionPtr    - Current transaction.
 *
 * Results:
 *      TCL_OK on success; TCL_ERROR on parsing failure.
 *
 * Side Effects:
 *      Replaces the transaction's staged X and Y candidates only after
 *      both new candidates have parsed successfully.
 *
 *----------------------------------------------------------------------
 */
static int StageElemDataPairs(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr,
                             ElemDataTransaction *transactionPtr) {
    ElemVector newX;
    ElemVector newY;
    unsigned int xMask;
    unsigned int yMask;

    memset(&newX, 0, sizeof(newX));
    memset(&newY, 0, sizeof(newY));

    if (Rbc_ParseElemVectorPairsObj(interp, elemPtr, objPtr, &newX, &newY) != TCL_OK) {
        Rbc_FreeElemVector(&newX);
        Rbc_FreeElemVector(&newY);
        return TCL_ERROR;
    }

    xMask = ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X);
    yMask = ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y);

    if (transactionPtr->stagedMask & xMask) {
        Rbc_FreeElemVector(&transactionPtr->x);
    }
    if (transactionPtr->stagedMask & yMask) {
        Rbc_FreeElemVector(&transactionPtr->y);
    }

    transactionPtr->x = newX;
    transactionPtr->y = newY;
    transactionPtr->stagedMask |= xMask | yMask;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PrepareElemDataTransaction --
 *
 *      Builds a complete staged transaction for all bar data options
 *      involved in the current modern configuration.
 *
 *      Explicit option/value pairs are processed in caller order, so
 *      interactions between "-data", "-x", and "-y" retain their legacy
 *      last-option-wins behaviour.
 *
 * Parameters:
 *      Graph *graphPtr                      - Owning graph.
 *      Element *elemPtr                     - Bar element.
 *      ElemDataTransaction *transactionPtr   - Receives staged data.
 *
 * Results:
 *      TCL_OK when all relevant values parse successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Allocates temporary data arrays or vector client IDs. The live
 *      element vectors remain unchanged.
 *
 *----------------------------------------------------------------------
 */
int Rbc_PrepareElemDataTransaction(Graph *graphPtr, Element *elemPtr, ElemDataTransaction *transactionPtr) {
    Tcl_Interp *interp;
    unsigned int explicitMask;
    Tcl_Size i;

    interp = graphPtr->interp;
    explicitMask = 0;

    memset(transactionPtr, 0, sizeof(*transactionPtr));

    assert((elemPtr->optionObjc & 1) == 0);

    /*
     * First identify all explicitly supplied data options. This lets
     * the first configuration retain option-database values for options
     * that the caller did not override.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemDataOption option;

        option = GetElemDataOption(elemPtr->optionObjv[i]);
        if (option != ELEM_DATA_OPTION_NONE) {
            explicitMask |= ELEM_DATA_OPTION_MASK(option);
        }
    }

    /*
     * Tk_InitOptions may have installed values from the option database
     * before the first Tk_SetOptions call. Process those effective values
     * only when that option was not explicitly supplied.
     *
     * The order follows the legacy bar-element table: "-data" first,
     * followed by weights and individual vectors.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_PAIRS)) && (elemPtr->dataObjPtr != NULL)) {
            if (StageElemDataPairs(interp, elemPtr, elemPtr->dataObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_WEIGHTS)) && (elemPtr->weightsObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->weightsObjPtr, transactionPtr, &transactionPtr->w,
                                   ELEM_DATA_OPTION_WEIGHTS) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X)) && (elemPtr->xObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->xObjPtr, transactionPtr, &transactionPtr->x,
                                   ELEM_DATA_OPTION_X) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y)) && (elemPtr->yObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->yObjPtr, transactionPtr, &transactionPtr->y,
                                   ELEM_DATA_OPTION_Y) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_ERROR)) && (elemPtr->xErrorObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->xErrorObjPtr, transactionPtr, &transactionPtr->xError,
                                   ELEM_DATA_OPTION_X_ERROR) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_HIGH)) && (elemPtr->xHighObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->xHighObjPtr, transactionPtr, &transactionPtr->xHigh,
                                   ELEM_DATA_OPTION_X_HIGH) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_LOW)) && (elemPtr->xLowObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->xLowObjPtr, transactionPtr, &transactionPtr->xLow,
                                   ELEM_DATA_OPTION_X_LOW) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_ERROR)) && (elemPtr->yErrorObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->yErrorObjPtr, transactionPtr, &transactionPtr->yError,
                                   ELEM_DATA_OPTION_Y_ERROR) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_HIGH)) && (elemPtr->yHighObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->yHighObjPtr, transactionPtr, &transactionPtr->yHigh,
                                   ELEM_DATA_OPTION_Y_HIGH) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_LOW)) && (elemPtr->yLowObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->yLowObjPtr, transactionPtr, &transactionPtr->yLow,
                                   ELEM_DATA_OPTION_Y_LOW) != TCL_OK) {
                goto error;
            }
        }
    }

    /*
     * Process the original option/value vector rather than the retained
     * object fields. The retained fields contain only each option's final
     * value and would incorrectly hide an invalid earlier occurrence.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemDataOption option;
        Tcl_Obj *valueObjPtr;

        option = GetElemDataOption(elemPtr->optionObjv[i]);
        valueObjPtr = elemPtr->optionObjv[i + 1];

        switch (option) {
        case ELEM_DATA_OPTION_PAIRS:
            if (StageElemDataPairs(interp, elemPtr, valueObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_WEIGHTS:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->w, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_X:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->x, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_Y:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->y, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_X_ERROR:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->xError, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_X_HIGH:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->xHigh, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_X_LOW:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->xLow, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_Y_ERROR:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->yError, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_Y_HIGH:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->yHigh, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_Y_LOW:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->yLow, option) !=
                TCL_OK) {
                goto error;
            }
            break;

        case ELEM_DATA_OPTION_NONE:
            break;
        }
    }

    return TCL_OK;

error:
    Rbc_FreeElemDataTransaction(transactionPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CommitElemDataTransaction --
 *
 *      Commits all successfully staged bar vectors to the live element.
 *
 * Parameters:
 *      Element *elemPtr                     - Destination element.
 *      ElemDataTransaction *transactionPtr   - Completed transaction.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Releases replaced live vectors, transfers staged ownership, and
 *      installs named-vector change callbacks.
 *
 *----------------------------------------------------------------------
 */
void Rbc_CommitElemDataTransaction(Element *elemPtr, ElemDataTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X)) {
        Rbc_CommitElemVector(elemPtr, &elemPtr->x, &transactionPtr->x);
    }

    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y)) {
        Rbc_CommitElemVector(elemPtr, &elemPtr->y, &transactionPtr->y);
    }

    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_WEIGHTS)) {
        Rbc_CommitElemVector(elemPtr, &elemPtr->w, &transactionPtr->w);
    }

    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_ERROR)) {
        Rbc_CommitElemVector(elemPtr, &elemPtr->xError, &transactionPtr->xError);
    }

    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_HIGH)) {
        Rbc_CommitElemVector(elemPtr, &elemPtr->xHigh, &transactionPtr->xHigh);
    }

    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_LOW)) {
        Rbc_CommitElemVector(elemPtr, &elemPtr->xLow, &transactionPtr->xLow);
    }

    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_ERROR)) {
        Rbc_CommitElemVector(elemPtr, &elemPtr->yError, &transactionPtr->yError);
    }

    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_HIGH)) {
        Rbc_CommitElemVector(elemPtr, &elemPtr->yHigh, &transactionPtr->yHigh);
    }

    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_LOW)) {
        Rbc_CommitElemVector(elemPtr, &elemPtr->yLow, &transactionPtr->yLow);
    }

    transactionPtr->stagedMask = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * ReplaceElemOptionObject --
 *
 *      Replaces one Tcl object retained by the bar element option
 *      table.
 *
 * Parameters:
 *      Tcl_Obj **objPtrPtr - Address of the retained option object.
 *      Tcl_Obj *newObjPtr  - Replacement object, or NULL.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Updates Tcl object reference counts and replaces the retained
 *      object.
 *
 *----------------------------------------------------------------------
 */
static void ReplaceElemOptionObject(Tcl_Obj **objPtrPtr, Tcl_Obj *newObjPtr) {
    Tcl_Obj *oldObjPtr;

    oldObjPtr = *objPtrPtr;

    if (oldObjPtr == newObjPtr) {
        return;
    }

    if (newObjPtr != NULL) {
        Tcl_IncrRefCount(newObjPtr);
    }

    *objPtrPtr = newObjPtr;

    if (oldObjPtr != NULL) {
        Tcl_DecrRefCount(oldObjPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SetElemDataPairOptionObjects --
 *
 *      Stores a coherent -data representation and derives the retained
 *      -x and -y representations from its alternating coordinate
 *      elements.
 *
 *      The original Tcl representations of the coordinate expressions
 *      are preserved. They are not reconstructed from the internal
 *      double arrays.
 *
 * Parameters:
 *      Element *elemPtr    - Bar element.
 *      Tcl_Obj *dataObjPtr - Valid, even-length -data list.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Replaces dataObjPtr, xObjPtr, and yObjPtr in the element.
 *
 *----------------------------------------------------------------------
 */
static void SetElemDataPairOptionObjects(Element *elemPtr, Tcl_Obj *dataObjPtr) {
    Tcl_Obj **objv;
    Tcl_Obj *xObjPtr;
    Tcl_Obj *yObjPtr;
    Tcl_Size objc;
    Tcl_Size i;

    if (Tcl_ListObjGetElements(elemPtr->graphPtr->interp, dataObjPtr, &objc, &objv) != TCL_OK) {
        Tcl_Panic("validated bar -data value is no longer a Tcl list");
    }

    if ((objc & 1) != 0) {
        Tcl_Panic("validated bar -data value has an odd length");
    }

    xObjPtr = Tcl_NewListObj(0, NULL);
    yObjPtr = Tcl_NewListObj(0, NULL);

    for (i = 0; i < objc; i += 2) {
        if (Tcl_ListObjAppendElement(NULL, xObjPtr, objv[i]) != TCL_OK) {
            Tcl_Panic("can't construct bar -x option value");
        }

        if (Tcl_ListObjAppendElement(NULL, yObjPtr, objv[i + 1]) != TCL_OK) {
            Tcl_Panic("can't construct bar -y option value");
        }
    }

    ReplaceElemOptionObject(&elemPtr->dataObjPtr, dataObjPtr);
    ReplaceElemOptionObject(&elemPtr->xObjPtr, xObjPtr);
    ReplaceElemOptionObject(&elemPtr->yObjPtr, yObjPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_SyncElemDataOptionObjects --
 *
 *      Synchronizes the retained -data, -x, and -y option objects after
 *      a successfully committed element-data transaction.
 *
 *      A retained -data value is valid only while both live coordinate
 *      vectors still originate from that -data value. Configuring -x or
 *      -y independently invalidates -data. A later -data occurrence
 *      restores a coherent representation.
 *
 *      Initial option-database values are processed before explicit
 *      option/value pairs. Explicit pairs are then processed in their
 *      original left-to-right order.
 *
 * Parameters:
 *      Element *elemPtr - Configured bar element.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Replaces retained Tcl option objects and updates their reference
 *      counts.
 *
 *----------------------------------------------------------------------
 */
void Rbc_SyncElemDataOptionObjects(Element *elemPtr) {
    Tcl_Obj *initialDataObjPtr;
    Tcl_Obj *initialXObjPtr;
    Tcl_Obj *initialYObjPtr;
    unsigned int explicitMask;
    Tcl_Size i;

    assert((elemPtr->optionObjc & 1) == 0);

    explicitMask = 0;

    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemDataOption option;

        option = GetElemDataOption(elemPtr->optionObjv[i]);

        if (option != ELEM_DATA_OPTION_NONE) {
            explicitMask |= ELEM_DATA_OPTION_MASK(option);
        }
    }

    initialDataObjPtr = NULL;
    initialXObjPtr = NULL;
    initialYObjPtr = NULL;

    /*
     * Preserve references to initial option-database values while the
     * retained fields are being replaced below.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_PAIRS))) {
            initialDataObjPtr = elemPtr->dataObjPtr;

            if (initialDataObjPtr != NULL) {
                Tcl_IncrRefCount(initialDataObjPtr);
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X))) {
            initialXObjPtr = elemPtr->xObjPtr;

            if (initialXObjPtr != NULL) {
                Tcl_IncrRefCount(initialXObjPtr);
            }
        }

        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y))) {
            initialYObjPtr = elemPtr->yObjPtr;

            if (initialYObjPtr != NULL) {
                Tcl_IncrRefCount(initialYObjPtr);
            }
        }

        /*
         * Apply initial values in the same order used by the data
         * transaction: -data first, followed by -x and -y.
         */
        if (initialDataObjPtr != NULL) {
            SetElemDataPairOptionObjects(elemPtr, initialDataObjPtr);
        }

        if (initialXObjPtr != NULL) {
            ReplaceElemOptionObject(&elemPtr->dataObjPtr, NULL);
            ReplaceElemOptionObject(&elemPtr->xObjPtr, initialXObjPtr);
        }

        if (initialYObjPtr != NULL) {
            ReplaceElemOptionObject(&elemPtr->dataObjPtr, NULL);
            ReplaceElemOptionObject(&elemPtr->yObjPtr, initialYObjPtr);
        }

        if (initialDataObjPtr != NULL) {
            Tcl_DecrRefCount(initialDataObjPtr);
        }
        if (initialXObjPtr != NULL) {
            Tcl_DecrRefCount(initialXObjPtr);
        }
        if (initialYObjPtr != NULL) {
            Tcl_DecrRefCount(initialYObjPtr);
        }
    }

    /*
     * Apply explicit options in their original order. This preserves
     * the same last-option-wins rule used when staging the vectors.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemDataOption option;
        Tcl_Obj *valueObjPtr;

        option = GetElemDataOption(elemPtr->optionObjv[i]);
        valueObjPtr = elemPtr->optionObjv[i + 1];

        switch (option) {
        case ELEM_DATA_OPTION_PAIRS:
            SetElemDataPairOptionObjects(elemPtr, valueObjPtr);
            break;

        case ELEM_DATA_OPTION_X:
            ReplaceElemOptionObject(&elemPtr->dataObjPtr, NULL);
            ReplaceElemOptionObject(&elemPtr->xObjPtr, valueObjPtr);
            break;

        case ELEM_DATA_OPTION_Y:
            ReplaceElemOptionObject(&elemPtr->dataObjPtr, NULL);
            ReplaceElemOptionObject(&elemPtr->yObjPtr, valueObjPtr);
            break;

        case ELEM_DATA_OPTION_NONE:
        case ELEM_DATA_OPTION_WEIGHTS:
        case ELEM_DATA_OPTION_X_ERROR:
        case ELEM_DATA_OPTION_X_HIGH:
        case ELEM_DATA_OPTION_X_LOW:
        case ELEM_DATA_OPTION_Y_ERROR:
        case ELEM_DATA_OPTION_Y_HIGH:
        case ELEM_DATA_OPTION_Y_LOW:
            break;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * IsElemStylesOption --
 *
 *      Determines whether an option name represents "-styles".
 *
 *      Tk_SetOptions has already validated abbreviations. This helper
 *      recovers the canonical option identity from the original
 *      option/value vector.
 *
 *----------------------------------------------------------------------
 */
static int IsElemStylesOption(Tcl_Obj *objPtr) {
    static const char optionName[] = "-styles";
    const char *string;
    Tcl_Size length;
    Tcl_Size fullLength;

    string = Tcl_GetStringFromObj(objPtr, &length);
    fullLength = (Tcl_Size)(sizeof(optionName) - 1);

    return ((length > 0) && (length <= fullLength) && (strncmp(string, optionName, (size_t)length) == 0));
}

/*
 *----------------------------------------------------------------------
 *
 * StageElemStyles --
 *
 *      Parses one -styles value into temporary palette storage.
 *
 *      A previously staged candidate is retained until its replacement
 *      has parsed successfully.
 *
 *----------------------------------------------------------------------
 */
static int StageElemStyles(Graph *graphPtr, Element *elemPtr, Tcl_Obj *objPtr, Rbc_Uid penType, size_t styleSize,
                           ElemStylesTransaction *transactionPtr) {
    Rbc_Chain *newPalette;

    newPalette = NULL;

    if (Rbc_ParseStylesObj(graphPtr, elemPtr, objPtr, styleSize, &newPalette) != TCL_OK) {
        return TCL_ERROR;
    }

    if (transactionPtr->palette != NULL) {
        Rbc_DestroyPalette(graphPtr, transactionPtr->palette);
    }

    transactionPtr->palette = newPalette;
    transactionPtr->staged = TRUE;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeElemStylesTransaction --
 *
 *      Releases a staged candidate palette.
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeElemStylesTransaction(Graph *graphPtr, ElemStylesTransaction *transactionPtr) {
    Rbc_DestroyPalette(graphPtr, transactionPtr->palette);

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PrepareElemStylesTransaction --
 *
 *      Parses all -styles values involved in the current modern
 *      configuration without modifying the live palette.
 *
 *      Explicit occurrences are processed in caller order. Therefore,
 *      an invalid earlier repeated value still causes the complete
 *      configuration to fail.
 *
 *----------------------------------------------------------------------
 */
int Rbc_PrepareElemStylesTransaction(Graph *graphPtr, Element *elemPtr, Rbc_Uid penType, size_t styleSize,
                                     ElemStylesTransaction *transactionPtr) {
    int explicitlySpecified;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));

    explicitlySpecified = FALSE;

    assert((elemPtr->optionObjc & 1) == 0);

    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemStylesOption(elemPtr->optionObjv[i])) {
            explicitlySpecified = TRUE;
        }
    }

    /*
     * Always construct the initial palette, even when stylesObjPtr is
     * NULL. NULL represents an empty style list, but the palette still
     * requires its reserved normal-pen entry.
     */
    if (!elemPtr->optionsConfigured && !explicitlySpecified) {
        if (StageElemStyles(graphPtr, elemPtr, elemPtr->stylesObjPtr, penType, styleSize, transactionPtr) != TCL_OK) {
            goto error;
        }
    }

    /*
     * Process every explicit occurrence in original caller order.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemStylesOption(elemPtr->optionObjv[i])) {
            if (StageElemStyles(graphPtr, elemPtr, elemPtr->optionObjv[i + 1], penType, styleSize, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
    }

    return TCL_OK;

error:
    Rbc_FreeElemStylesTransaction(graphPtr, transactionPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CommitElemStylesTransaction --
 *
 *      Replaces the live palette with a successfully staged candidate.
 *
 *----------------------------------------------------------------------
 */
void Rbc_CommitElemStylesTransaction(Graph *graphPtr, Element *elemPtr, ElemStylesTransaction *transactionPtr) {
    Rbc_Chain *oldPalette;

    if (!transactionPtr->staged) {
        return;
    }

    oldPalette = elemPtr->palette;

    elemPtr->palette = transactionPtr->palette;

    transactionPtr->palette = NULL;
    transactionPtr->staged = FALSE;

    Rbc_DestroyPalette(graphPtr, oldPalette);
}

/*
 *----------------------------------------------------------------------
 *
 * StringToAlong --
 *
 *      Given a Tcl list of numeric expression representing the element
 *      values, convert into an array of double precision values. In
 *      addition, the minimum and maximum values are saved.  Since
 *      elastic values are allow (values which translate to the
 *      min/max of the graph), we must try to get the non-elastic
 *      minimum and maximum.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to.
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representation of value.
 *      char *widgRec - Widget record.
 *      Tcl_Size offset - Offset of field in widget record.
 *
 * Results:
 *      The return value is a standard Tcl result.  The vector is passed
 *      back via the vPtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToAlong(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                         Tcl_Size offset) {
    int *intPtr = (int *)(widgRec + offset);

    if ((string[0] == 'x') && (string[1] == '\0')) {
        *intPtr = SEARCH_X;
    } else if ((string[0] == 'y') && (string[1] == '\0')) {
        *intPtr = SEARCH_Y;
    } else if ((string[0] == 'b') && (strcmp(string, "both") == 0)) {
        *intPtr = SEARCH_BOTH;
    } else {
        Tcl_AppendResult(interp, "bad along value \"", string, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * AlongToString --
 *
 *      Convert the vector of floating point values into a Tcl list.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of field in widget record
 *      Tcl_FreeProc **freeProcPtr - Memory deallocation scheme to use
 *
 * Results:
 *      The string representation of the vector is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *AlongToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                 Tcl_FreeProc **freeProcPtr) {
    int along = *(int *)(widgRec + offset);

    switch (along) {
    case SEARCH_X:
        return "x";
    case SEARCH_Y:
        return "y";
    case SEARCH_BOTH:
        return "both";
    default:
        return "unknown along value";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreePalette --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Rbc_Chain *palette
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreePalette(Graph *graphPtr, Rbc_Chain *palette) {
    Rbc_ChainLink *linkPtr;

    /* Skip the first slot. It contains the built-in "normal" pen of
     * the element.  */
    linkPtr = Rbc_ChainFirstLink(palette);
    if (linkPtr != NULL) {
        register PenStyle *stylePtr;
        Rbc_ChainLink *nextPtr;

        for (linkPtr = Rbc_ChainNextLink(linkPtr); linkPtr != NULL; linkPtr = nextPtr) {
            nextPtr = Rbc_ChainNextLink(linkPtr);
            stylePtr = Rbc_ChainGetValue(linkPtr);
            Rbc_FreePen(graphPtr, stylePtr->penPtr);
            Rbc_ChainDeleteLink(palette, linkPtr);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DestroyPalette --
 *
 *      Releases all pen references owned by a palette and destroys
 *      the palette chain itself.
 *
 * Parameters:
 *      Graph *graphPtr    - Owning graph.
 *      Rbc_Chain *palette - Palette to destroy.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Releases all named style pens except the non-owning first
 *      normal-pen entry, then destroys the chain.
 *
 *----------------------------------------------------------------------
 */
void Rbc_DestroyPalette(Graph *graphPtr, Rbc_Chain *palette) {
    if (palette == NULL) {
        return;
    }

    Rbc_FreePalette(graphPtr, palette);
    Rbc_ChainDestroy(palette);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ParseStylesObj --
 *
 *      Parses an element palette into a newly allocated candidate
 *      chain without modifying the live element palette.
 *
 * Parameters:
 *      Graph *graphPtr          - Owning graph.
 *      Element *elemPtr         - Element receiving the palette.
 *      Tcl_Obj *objPtr          - Tcl list of style specifications.
 *                                 NULL represents an empty style list.
 *      size_t styleSize         - Size of the concrete style record.
 *      Rbc_Chain **palettePtrPtr
 *                               - Receives the candidate palette.
 *
 * Results:
 *      TCL_OK if every style was parsed successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Allocates a new chain and acquires pen references on success.
 *      On failure, all partially acquired references and storage are
 *      released. The live element is never modified.
 *
 *----------------------------------------------------------------------
 */
int Rbc_ParseStylesObj(Graph *graphPtr, Element *elemPtr, Tcl_Obj *objPtr, size_t styleSize,
                       Rbc_Chain **palettePtrPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_Size i;
    Rbc_Chain *palette;
    Rbc_ChainLink *linkPtr;
    PenStyle *stylePtr;

    *palettePtrPtr = NULL;

    objv = NULL;
    objc = 0;

    if (objPtr != NULL) {
        if (Tcl_ListObjGetElements(graphPtr->interp, objPtr, &objc, &objv) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    palette = Rbc_ChainCreate();
    if (palette == NULL) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("can't allocate element palette", -1));
        return TCL_ERROR;
    }

    /*
     * The first entry is always reserved for the element's normal pen.
     * This entry does not own a pen reference.
     */
    linkPtr = Rbc_ChainAllocLink((unsigned int)styleSize);
    Rbc_ChainLinkBefore(palette, linkPtr, NULL);

    stylePtr = Rbc_ChainGetValue(linkPtr);
    stylePtr->penPtr = elemPtr->normalPenPtr;

    for (i = 0; i < objc; i++) {
        linkPtr = Rbc_ChainAllocLink((unsigned int)styleSize);

        stylePtr = Rbc_ChainGetValue(linkPtr);

        /*
         * Preserve the legacy default ranges. A three-item style
         * specification may replace these values.
         */
        stylePtr->weight.min = (double)i;
        stylePtr->weight.max = (double)i + 1.0;
        stylePtr->weight.range = 1.0;

        if (GetPenStyleFromObj(graphPtr, objv[i], elemPtr->classUid, stylePtr) != TCL_OK) {
            /*
             * This link has not yet been attached to the chain.
             */
            ckfree((char *)linkPtr);

            Rbc_DestroyPalette(graphPtr, palette);
            return TCL_ERROR;
        }

        Rbc_ChainLinkBefore(palette, linkPtr, NULL);
    }

    *palettePtrPtr = palette;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_StyleMap --
 *
 *      Creates an array of style indices and fills it based on the weight
 *      of each data point.
 *
 * Parameters:
 *      Element *elemPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed and allocated for the index array.
 *
 *----------------------------------------------------------------------
 */
PenStyle **Rbc_StyleMap(Element *elemPtr) {
    register int i;
    int nWeights; /* Number of weights to be examined.
                   * If there are more data points than
                   * weights, they will default to the
                   * normal pen. */

    PenStyle **dataToStyle; /* Directory of styles.  Each array
                             * element represents the style for
                             * the data point at that index */
    Rbc_ChainLink *linkPtr;
    PenStyle *stylePtr;
    double *w; /* Weight vector */
    int nPoints;

    nPoints = NumberOfPoints(elemPtr);
    nWeights = MIN(elemPtr->w.nValues, nPoints);
    w = elemPtr->w.valueArr;
    linkPtr = Rbc_ChainFirstLink(elemPtr->palette);
    stylePtr = Rbc_ChainGetValue(linkPtr);

    /*
     * Create a style mapping array (data point index to style),
     * initialized to the default style.
     */
    dataToStyle = (PenStyle **)ckalloc(nPoints * sizeof(PenStyle *));
    assert(dataToStyle);
    for (i = 0; i < nPoints; i++) {
        dataToStyle[i] = stylePtr;
    }

    for (i = 0; i < nWeights; i++) {
        for (linkPtr = Rbc_ChainLastLink(elemPtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainPrevLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);

            if (stylePtr->weight.range > 0.0) {
                double norm;

                norm = (w[i] - stylePtr->weight.min) / stylePtr->weight.range;
                if (((norm - 1.0) <= DBL_EPSILON) && (((1.0 - norm) - 1.0) <= DBL_EPSILON)) {
                    dataToStyle[i] = stylePtr;
                    break; /* Done: found range that matches. */
                }
            }
        }
    }
    return dataToStyle;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MapErrorBars --
 *
 *      Creates two arrays of points and pen indices, filled with
 *      the screen coordinates of the visible
 *
 * Parameters:
 *      Graph *graphPtr
 *      Element *elemPtr
 *      PenStyle **dataToStyle
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed and allocated for the index array.
 *
 *----------------------------------------------------------------------
 */
void Rbc_MapErrorBars(Graph *graphPtr, Element *elemPtr, PenStyle **dataToStyle) {
    int n, nPoints;
    Extents2D exts;
    PenStyle *stylePtr;

    Rbc_GraphExtents(graphPtr, &exts);
    nPoints = NumberOfPoints(elemPtr);
    if (elemPtr->xError.nValues > 0) {
        n = MIN(elemPtr->xError.nValues, nPoints);
    } else {
        n = MIN3(elemPtr->xHigh.nValues, elemPtr->xLow.nValues, nPoints);
    }
    if (n > 0) {
        Segment2D *errorBars;
        Segment2D *segPtr;
        double high, low;
        double x, y;
        int *errorToData;
        int *indexPtr;
        register int i;

        segPtr = errorBars = (Segment2D *)ckalloc(n * 3 * sizeof(Segment2D));
        indexPtr = errorToData = (int *)ckalloc(n * 3 * sizeof(int));
        for (i = 0; i < n; i++) {
            x = elemPtr->x.valueArr[i];
            y = elemPtr->y.valueArr[i];
            stylePtr = dataToStyle[i];
            if ((FINITE(x)) && (FINITE(y))) {
                if (elemPtr->xError.nValues > 0) {
                    high = x + elemPtr->xError.valueArr[i];
                    low = x - elemPtr->xError.valueArr[i];
                } else {
                    high = elemPtr->xHigh.valueArr[i];
                    low = elemPtr->xLow.valueArr[i];
                }
                if ((FINITE(high)) && (FINITE(low))) {
                    Point2D p, q;

                    p = Rbc_Map2D(graphPtr, high, y, &elemPtr->axes);
                    q = Rbc_Map2D(graphPtr, low, y, &elemPtr->axes);
                    segPtr->p = p;
                    segPtr->q = q;
                    if (Rbc_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                    /* Left cap */
                    segPtr->p.x = segPtr->q.x = p.x;
                    segPtr->p.y = p.y - stylePtr->errorBarCapWidth;
                    segPtr->q.y = p.y + stylePtr->errorBarCapWidth;
                    if (Rbc_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                    /* Right cap */
                    segPtr->p.x = segPtr->q.x = q.x;
                    segPtr->p.y = q.y - stylePtr->errorBarCapWidth;
                    segPtr->q.y = q.y + stylePtr->errorBarCapWidth;
                    if (Rbc_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                }
            }
        }
        elemPtr->xErrorBars = errorBars;
        elemPtr->xErrorBarCnt = segPtr - errorBars;
        elemPtr->xErrorToData = errorToData;
    }
    if (elemPtr->yError.nValues > 0) {
        n = MIN(elemPtr->yError.nValues, nPoints);
    } else {
        n = MIN3(elemPtr->yHigh.nValues, elemPtr->yLow.nValues, nPoints);
    }
    if (n > 0) {
        Segment2D *errorBars;
        Segment2D *segPtr;
        double high, low;
        double x, y;
        int *errorToData;
        int *indexPtr;
        register int i;

        segPtr = errorBars = (Segment2D *)ckalloc(n * 3 * sizeof(Segment2D));
        indexPtr = errorToData = (int *)ckalloc(n * 3 * sizeof(int));
        for (i = 0; i < n; i++) {
            x = elemPtr->x.valueArr[i];
            y = elemPtr->y.valueArr[i];
            stylePtr = dataToStyle[i];
            if ((FINITE(x)) && (FINITE(y))) {
                if (elemPtr->yError.nValues > 0) {
                    high = y + elemPtr->yError.valueArr[i];
                    low = y - elemPtr->yError.valueArr[i];
                } else {
                    high = elemPtr->yHigh.valueArr[i];
                    low = elemPtr->yLow.valueArr[i];
                }
                if ((FINITE(high)) && (FINITE(low))) {
                    Point2D p, q;

                    p = Rbc_Map2D(graphPtr, x, high, &elemPtr->axes);
                    q = Rbc_Map2D(graphPtr, x, low, &elemPtr->axes);
                    segPtr->p = p;
                    segPtr->q = q;
                    if (Rbc_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                    /* Top cap. */
                    segPtr->p.y = segPtr->q.y = p.y;
                    segPtr->p.x = p.x - stylePtr->errorBarCapWidth;
                    segPtr->q.x = p.x + stylePtr->errorBarCapWidth;
                    if (Rbc_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                    /* Bottom cap. */
                    segPtr->p.y = segPtr->q.y = q.y;
                    segPtr->p.x = q.x - stylePtr->errorBarCapWidth;
                    segPtr->q.x = q.x + stylePtr->errorBarCapWidth;
                    if (Rbc_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                        segPtr++;
                        *indexPtr++ = i;
                    }
                }
            }
        }
        elemPtr->yErrorBars = errorBars;
        elemPtr->yErrorBarCnt = segPtr - errorBars;
        elemPtr->yErrorToData = errorToData;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetIndex --
 *
 *      Given a string representing the index of a pair of x,y
 *      coordinates, return the numeric index.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Element *elemPtr
 *      char *string
 *      int *indexPtr
 *
 * Results:
 *      A standard TCL result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int GetIndex(Tcl_Interp *interp, Element *elemPtr, const char *string, int *indexPtr) {
    long ielem;
    int last;

    last = NumberOfPoints(elemPtr) - 1;
    if ((*string == 'e') && (strcmp("end", string) == 0)) {
        ielem = last;
    } else if (Tcl_ExprLong(interp, string, &ielem) != TCL_OK) {
        return TCL_ERROR;
    }
    *indexPtr = (int)ielem;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NameToElement --
 *
 *      Find the element represented the given name,  returning
 *      a pointer to its data structure via elemPtrPtr.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Obj *nameObj
 *      Element **elemPtrPtr
 *
 * Results:
 *      A standard TCL result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int NameToElement(Graph *graphPtr, Tcl_Obj *nameObj, Element **elemPtrPtr) {
    Tcl_HashEntry *hPtr;
    const char *name = Tcl_GetString(nameObj);

    hPtr = Tcl_FindHashEntry(&graphPtr->elements.table, name);
    if (hPtr == NULL) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_ObjPrintf("can't find element \"%s\" in \"%s\"", name, Tk_PathName(graphPtr->tkwin)));
        return TCL_ERROR;
    }
    *elemPtrPtr = (Element *)Tcl_GetHashValue(hPtr);
    return TCL_OK;
}

static int InitElementOptions(Graph *graphPtr, Element *elemPtr) {
    char *componentName;
    int result;

    if (elemPtr->optionsInitialized) {
        return TCL_OK;
    }
    assert(elemPtr->optionSpecs != NULL);
    elemPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, elemPtr->optionSpecs);
    componentName = RbcStrdup(elemPtr->name);
    if (componentName[0] != '\0') {
        componentName[0] = (char)tolower((unsigned char)componentName[0]);
    }
    result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, componentName, "Element", (char *)elemPtr,
                                      elemPtr->optionTable);
    ckfree(componentName);
    if (result != TCL_OK) {
        elemPtr->optionTable = NULL;
        return TCL_ERROR;
    }
    elemPtr->optionsInitialized = TRUE;
    return TCL_OK;
}

static int ConfigureElementOptions(Graph *graphPtr, Element *elemPtr, Tcl_Size objc, Tcl_Obj *const objv[], int *maskPtr) {
    Tk_SavedOptions savedOptions;
    Tcl_Obj *errorObjPtr;
    int mask;

    assert(elemPtr->optionsInitialized);
    assert(elemPtr->optionTable != NULL);

    /*
     * Clear any stale transaction context before invoking Tk.
     */
    elemPtr->optionMask = 0;
    elemPtr->optionObjc = 0;
    elemPtr->optionObjv = NULL;

    if (Tk_SetOptions(graphPtr->interp, (char *)elemPtr, elemPtr->optionTable, objc, objv, graphPtr->tkwin,
                      &savedOptions, &mask) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Make the changed-option mask and the original argument order
     * available to the concrete configuration procedure.
     */
    elemPtr->optionMask = mask;
    elemPtr->optionObjc = objc;
    elemPtr->optionObjv = objv;

    /*
     * Concrete configuration procedures must be transactional:
     * they must not replace active derived resources until every
     * fallible conversion has succeeded.
     */
    if ((*elemPtr->procsPtr->configProc)(graphPtr, elemPtr) != TCL_OK) {
        errorObjPtr = Tcl_GetObjResult(graphPtr->interp);
        Tcl_IncrRefCount(errorObjPtr);

        /*
         * The argument vector belongs to the caller and must not
         * remain stored while restoring or destroying the element.
         */
        elemPtr->optionMask = 0;
        elemPtr->optionObjc = 0;
        elemPtr->optionObjv = NULL;

        Tk_RestoreSavedOptions(&savedOptions);

        Tcl_SetObjResult(graphPtr->interp, errorObjPtr);
        Tcl_DecrRefCount(errorObjPtr);
        return TCL_ERROR;
    }

    elemPtr->optionMask = 0;
    elemPtr->optionObjc = 0;
    elemPtr->optionObjv = NULL;
    elemPtr->optionsConfigured = TRUE;

    Tk_FreeSavedOptions(&savedOptions);

    if (maskPtr != NULL) {
        *maskPtr = mask;
    }
    return TCL_OK;
}

static void ReleaseElementResources(Graph *graphPtr, Element *elemPtr) {
    if (elemPtr->tkResourcesReleased) {
        return;
    }
    /*
     * Never retain references to a caller-owned configuration argument
     * vector during destruction.
     */
    elemPtr->optionMask = 0;
    elemPtr->optionObjc = 0;
    elemPtr->optionObjv = NULL;

    /*
     * Release derived GCs and manually managed resources before Tk
     * releases the colours, fonts, borders, and bitmaps referenced by
     * those resources.
     *
     * Call the concrete destructor even if option initialization failed:
     * the element constructor may already have allocated chains, arrays,
     * or an embedded pen.
     */
    (*elemPtr->procsPtr->destroyProc)(graphPtr, elemPtr);
    if (elemPtr->optionsInitialized) {
        Tk_FreeConfigOptions((char *)elemPtr, elemPtr->optionTable, graphPtr->tkwin);
        elemPtr->optionsInitialized = FALSE;
    }
    elemPtr->tkResourcesReleased = TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyElement --
 *
 *      Add a new element to the graph.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Element *elemPtr
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DestroyElement(Graph *graphPtr, Element *elemPtr) {
    Rbc_ChainLink *linkPtr;

    Rbc_DeleteBindings(graphPtr->bindTable, elemPtr);
    Rbc_LegendRemoveElement(graphPtr->legend, elemPtr);

    ReleaseElementResources(graphPtr, elemPtr);

    /* Remove it also from the element display list */
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        if (elemPtr == Rbc_ChainGetValue(linkPtr)) {
            Rbc_ChainDeleteLink(graphPtr->elements.displayList, linkPtr);
            if (!elemPtr->hidden) {
                graphPtr->flags |= RESET_WORLD;
                Rbc_EventuallyRedrawGraph(graphPtr);
            }
            break;
        }
    }
    /* Remove the element for the graph's hash table of elements */
    if (elemPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(elemPtr->hashPtr);
    }
    if (elemPtr->name != NULL) {
        ckfree((char *)elemPtr->name);
    }
    ckfree((char *)elemPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateElement --
 *
 *      Add a new element to the graph.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *      Rbc_Uid classUid
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int CreateElement(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid) {
    Element *elemPtr;
    Tcl_HashEntry *hPtr;
    int isNew;
    const char *elemName = Tcl_GetString(objv[3]);

    if (elemName[0] == '-') {
        Tcl_AppendResult(graphPtr->interp, "name of element \"", elemName, "\" can't start with a '-'", (char *)NULL);
        return TCL_ERROR;
    }
    hPtr = Tcl_CreateHashEntry(&graphPtr->elements.table, elemName, &isNew);
    if (!isNew) {
        Tcl_AppendResult(interp, "element \"", elemName, "\" already exists in \"", Tcl_GetString(objv[0]), "\"",
                         (char *)NULL);
        return TCL_ERROR;
    }
    if (classUid == rbcBarElementUid) {
        elemPtr = Rbc_BarElement(graphPtr, elemName, classUid);
    } else {
        /* Stripcharts are line graphs with some options enabled. */
        elemPtr = Rbc_LineElement(graphPtr, elemName, classUid);
    }
    elemPtr->hashPtr = hPtr;
    Tcl_SetHashValue(hPtr, elemPtr);

    assert(elemPtr->optionSpecs != NULL);

    if (InitElementOptions(graphPtr, elemPtr) != TCL_OK) {
        DestroyElement(graphPtr, elemPtr);
        return TCL_ERROR;
    }

    /*
     * Always use the transactional path, including when no explicit
     * option/value pairs were supplied. Tk_InitOptions installed the
     * defaults, and configProc must construct the derived state.
     */
    if (ConfigureElementOptions(graphPtr, elemPtr, objc - 4, objv + 4, NULL) != TCL_OK) {
        DestroyElement(graphPtr, elemPtr);
        return TCL_ERROR;
    }
    Rbc_ChainPrepend(graphPtr->elements.displayList, elemPtr);

    if (!elemPtr->hidden) {
        /* If the new element isn't hidden then redraw the graph.  */
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    elemPtr->flags |= MAP_ITEM;
    graphPtr->flags |= RESET_AXES;
    Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->name, -1));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RebuildDisplayList --
 *
 *      Given a Tcl list of element names, this procedure rebuilds the
 *      display list, ignoring invalid element names. This list describes
 *      not only only which elements to draw, but in what order.  This is
 *      only important for bar and pie charts.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Tcl_Obj *newListObj - Tcl list of element names
 *
 * Results:
 *      The return value is a standard Tcl result.  Only if the Tcl list
 *      can not be split, a TCL_ERROR is returned and the interpreter
 *      result contains an error message.
 *
 * Side effects:
 *      The graph is eventually redrawn using the new display list.
 *
 *----------------------------------------------------------------------
 */
static int RebuildDisplayList(Graph *graphPtr, Tcl_Obj *newListObj) {
    Tcl_Size nNames;      /* Number of names found in Tcl name list */
                          //    const char **nameArr;        /* Broken out array of element names */
    Tcl_Obj **nameObjArr; /* Broken out array of element names */
    Tcl_HashSearch cursor;
    Tcl_Size i;
    register Tcl_HashEntry *hPtr;
    Element *elemPtr; /* Element information record */

    //    if (Tcl_ListObjGetElementsSplitList(graphPtr->interp, newList, &nNames, &nameArr) != TCL_OK) {
    if (Tcl_ListObjGetElements(graphPtr->interp, newListObj, &nNames, &nameObjArr) != TCL_OK) {
        //    Tcl_AppendResult(graphPtr->interp, "can't split name list \"", newList,
        //         "\"", (char *)NULL);
        return TCL_ERROR;
    }
    /* Clear the display list and mark all elements as hidden.  */
    Rbc_ChainReset(graphPtr->elements.displayList);
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = (Element *)Tcl_GetHashValue(hPtr);
        elemPtr->hidden = TRUE;
    }

    /* Rebuild the display list, checking that each name it exists
     * (currently ignoring invalid element names).  */
    for (i = 0; i < nNames; i++) {
        if (NameToElement(graphPtr, nameObjArr[i], &elemPtr) == TCL_OK) {
            elemPtr->hidden = FALSE;
            Rbc_ChainAppend(graphPtr->elements.displayList, elemPtr);
        }
    }
    //    ckfree((char *)nameArr);
    graphPtr->flags |= RESET_WORLD;
    Rbc_EventuallyRedrawGraph(graphPtr);
    Tcl_ResetResult(graphPtr->interp);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DestroyElements --
 *
 *      Removes all the graph's elements. This routine is called when
 *      the graph is destroyed.
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory allocated for the graph's elements is freed.
 *
 *----------------------------------------------------------------------
 */
void Rbc_DestroyElements(Graph *graphPtr) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Element *elemPtr;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = (Element *)Tcl_GetHashValue(hPtr);
        elemPtr->hashPtr = NULL;
        DestroyElement(graphPtr, elemPtr);
    }
    Tcl_DeleteHashTable(&graphPtr->elements.table);
    Tcl_DeleteHashTable(&graphPtr->elements.tagTable);
    Rbc_ChainDestroy(graphPtr->elements.displayList);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MapElements --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_MapElements(Graph *graphPtr) {
    Element *elemPtr;
    Rbc_ChainLink *linkPtr;

    if (graphPtr->mode != MODE_INFRONT) {
        Rbc_ResetStacks(graphPtr);
    }
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->hidden) {
            continue;
        }
        if ((graphPtr->flags & MAP_ALL) || (elemPtr->flags & MAP_ITEM)) {
            (*elemPtr->procsPtr->mapProc)(graphPtr, elemPtr);
            elemPtr->flags &= ~MAP_ITEM;
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_DrawElements --
 *
 *      Calls the individual element drawing routines for each
 *      element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Elements are drawn into the drawable (pixmap) which will
 *      eventually be displayed in the graph window.
 *
 * -----------------------------------------------------------------
 */
void Rbc_DrawElements(Graph *graphPtr, Drawable drawable) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (!elemPtr->hidden) {
            (*elemPtr->procsPtr->drawNormalProc)(graphPtr, drawable, elemPtr);
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_DrawActiveElements --
 *
 *      Calls the individual element drawing routines to display
 *      the active colors for each element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Elements are drawn into the drawable (pixmap) which will
 *      eventually be displayed in the graph window.
 *
 * -----------------------------------------------------------------
 */
void Rbc_DrawActiveElements(Graph *graphPtr, Drawable drawable) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if ((!elemPtr->hidden) && (elemPtr->flags & ELEM_ACTIVE)) {
            (*elemPtr->procsPtr->drawActiveProc)(graphPtr, drawable, elemPtr);
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_ElementsToPostScript --
 *
 *      Generates PostScript output for each graph element in the
 *      element display list.
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void Rbc_ElementsToPostScript(Graph *graphPtr, PsToken psToken) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (!elemPtr->hidden) {
            /* Comment the PostScript to indicate the start of the element */
            Rbc_FormatToPostScript(psToken, "\n%% Element \"%s\"\n\n", elemPtr->name);
            (*elemPtr->procsPtr->printNormalProc)(graphPtr, psToken, elemPtr);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ActiveElementsToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_ActiveElementsToPostScript(Graph *graphPtr, PsToken psToken) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if ((!elemPtr->hidden) && (elemPtr->flags & ELEM_ACTIVE)) {
            Rbc_FormatToPostScript(psToken, "\n%% Active Element \"%s\"\n\n", elemPtr->name);
            (*elemPtr->procsPtr->printActiveProc)(graphPtr, psToken, elemPtr);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GraphUpdateNeeded --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GraphUpdateNeeded(Graph *graphPtr) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->hidden) {
            continue;
        }
        /* Check if the x or y vectors have notifications pending */
        if ((Rbc_VectorNotifyPending(elemPtr->x.clientId)) || (Rbc_VectorNotifyPending(elemPtr->y.clientId))) {
            return 1;
        }
    }
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * ActivateOp --
 *
 *      Marks data points of elements (given by their index) as active.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns TCL_OK if no errors occurred.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ActivateOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Size i;
    int *activeArr;
    int nActiveIndices;

    /* Existing query branch remains unchanged. */

    if (NameToElement(graphPtr, objv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    elemPtr->flags |= ELEM_ACTIVE | ACTIVE_PENDING;
    activeArr = NULL;
    nActiveIndices = -1;

    if (objc > 4) {
        int *activePtr;
        Tcl_Size count;

        count = objc - 4;

        if (count > INT_MAX) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("too many active element indices", -1));
            return TCL_ERROR;
        }

        nActiveIndices = (int)count;

        activeArr = ckalloc(sizeof(int) * (size_t)nActiveIndices);
        activePtr = activeArr;

        for (i = 4; i < objc; i++) {
            if (GetIndex(interp, elemPtr, Tcl_GetString(objv[i]), activePtr) != TCL_OK) {
                ckfree(activeArr);
                return TCL_ERROR;
            }
            activePtr++;
        }
    }

    if (elemPtr->activeIndices != NULL) {
        ckfree(elemPtr->activeIndices);
    }

    elemPtr->nActiveIndices = nActiveIndices;
    elemPtr->activeIndices = activeArr;

    Rbc_EventuallyRedrawGraph(graphPtr);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MakeElementTag --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *tagName
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
ClientData Rbc_MakeElementTag(Graph *graphPtr, char *tagName) {
    Tcl_HashEntry *hPtr;
    int isNew;

    hPtr = Tcl_CreateHashEntry(&graphPtr->elements.tagTable, tagName, &isNew);
    assert(hPtr);
    return Tcl_GetHashKey(&graphPtr->elements.tagTable, hPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * BindOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int BindOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        Tcl_HashEntry *hPtr;
        Tcl_HashSearch cursor;
        char *tagName;
        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

        for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.tagTable, &cursor); hPtr != NULL;
             hPtr = Tcl_NextHashEntry(&cursor)) {
            tagName = Tcl_GetHashKey(&graphPtr->elements.tagTable, hPtr);
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(tagName, -1));
        }
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    return Rbc_ConfigureBindingsFromObj(interp, graphPtr->bindTable,
                                        Rbc_MakeElementTag(graphPtr, Tcl_GetString(objv[3])), objc - 4, objv + 4);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateOp --
 *
 *      Add a new element to the graph (using the default type of the
 *      graph).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int CreateOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return CreateElement(graphPtr, interp, objc, objv, type);
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Obj *resultObjPtr;

    if (NameToElement(graphPtr, objv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    resultObjPtr = Tk_GetOptionValue(interp, (char *)elemPtr, elemPtr->optionTable, objv[4], graphPtr->tkwin);

    if (resultObjPtr == NULL) {
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, resultObjPtr);

    return TCL_OK;
}

static Tk_ConfigSpec closestSpecs[] = {
    {TK_CONFIG_CUSTOM, "-halo", (char *)NULL, (char *)NULL, (char *)NULL, offsetof(ClosestSearch, halo), 0,
     &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-interpolate", (char *)NULL, (char *)NULL, (char *)NULL, offsetof(ClosestSearch, mode), 0},
    {TK_CONFIG_CUSTOM, "-along", (char *)NULL, (char *)NULL, (char *)NULL, offsetof(ClosestSearch, along), 0,
     &alongOption},
    {TK_CONFIG_END, (char *)NULL, (char *)NULL, (char *)NULL, (char *)NULL, 0, 0}};

/*
 *----------------------------------------------------------------------
 *
 * ClosestOp --
 *
 *      Find the element closest to the specified screen coordinates.
 *      Options:
 *      -halo        Consider points only with this maximum distance
 *              from the picked coordinate.
 *      -interpolate    Find closest point along element traces, not just
 *              data points.
 *      -along
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result. If an element could be found within
 *      the halo distance, the interpreter result is "1", otherwise
 *      "0".  If a closest element exists, the designated Tcl array
 *      variable will be set with the following information:
 *
 *      1) the element name,
 *      2) the index of the closest point,
 *      3) the distance (in screen coordinates) from the picked X-Y
 *         coordinate and the closest point,
 *      4) the X coordinate (graph coordinate) of the closest point,
 *      5) and the Y-coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ClosestOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    ClosestSearch search;
    Tcl_Size i;
    int x, y;
    int flags = TCL_LEAVE_ERR_MSG;
    const char *str;

    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[3], &x) != TCL_OK) {
        Tcl_AppendResult(interp, ": bad window x-coordinate", (char *)NULL);
        return TCL_ERROR;
    }
    if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[4], &y) != TCL_OK) {
        Tcl_AppendResult(interp, ": bad window y-coordinate", (char *)NULL);
        return TCL_ERROR;
    }
    if (graphPtr->inverted) {
        int temp;

        temp = x, x = y, y = temp;
    }
    for (i = 6; i < objc; i += 2) { /* Count switches-value pairs */
        str = Tcl_GetString(objv[i]);
        if ((str[0] != '-') || ((str[1] == '-') && (str[2] == '\0'))) {
            break;
        }
    }
    if (i > objc) {
        i = objc;
    }

    search.mode = SEARCH_POINTS;
    search.halo = graphPtr->halo;
    search.index = -1;
    search.along = SEARCH_BOTH;
    search.x = x;
    search.y = y;

    if (Tk_ConfigureWidget(interp, graphPtr->tkwin, closestSpecs, i - 6, objv + 6, &search, TK_CONFIG_ARGV_ONLY) !=
        TCL_OK) {
        return TCL_ERROR; /* Error occurred processing an option. */
    }
    if (i < objc) {
        str = Tcl_GetString(objv[i]);
        if (str[0] == '-') {
            i++; /* Skip "--" */
        }
    }
    search.dist = (double)(search.halo + 1);

    if (i < objc) {
        for (/* empty */; i < objc; i++) {
            if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
                return TCL_ERROR; /* Can't find named element */
            }
            if (elemPtr->hidden) {
                Tcl_AppendResult(interp, "element \"", Tcl_GetString(objv[i]), "\" is hidden", (char *)NULL);
                return TCL_ERROR; /* Element isn't visible */
            }
            /* Check if the X or Y vectors have notifications pending */
            if ((elemPtr->flags & MAP_ITEM) || (Rbc_VectorNotifyPending(elemPtr->x.clientId)) ||
                (Rbc_VectorNotifyPending(elemPtr->y.clientId))) {
                continue;
            }
            (*elemPtr->procsPtr->closestProc)(graphPtr, elemPtr, &search);
        }
    } else {
        Rbc_ChainLink *linkPtr;

        /*
         * Find the closest point from the set of displayed elements,
         * searching the display list from back to front.  That way if
         * the points from two different elements overlay each other
         * exactly, the last one picked will be the topmost.
         */
        for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
             linkPtr = Rbc_ChainPrevLink(linkPtr)) {
            elemPtr = Rbc_ChainGetValue(linkPtr);

            /* Check if the X or Y vectors have notifications pending */
            if ((elemPtr->flags & MAP_ITEM) || (Rbc_VectorNotifyPending(elemPtr->x.clientId)) ||
                (Rbc_VectorNotifyPending(elemPtr->y.clientId))) {
                continue;
            }
            if (!elemPtr->hidden) {
                (*elemPtr->procsPtr->closestProc)(graphPtr, elemPtr, &search);
            }
        }
    }

    /*  varname = objv[5] */
    if (search.dist < (double)search.halo) {
        /*
         *  Return an array of 5 elements
         */
        if (Tcl_ObjSetVar2(interp, objv[5], Tcl_NewStringObj("name", -1), Tcl_NewStringObj(search.elemPtr->name, -1),
                           flags) == NULL) {
            return TCL_ERROR;
        }
        if (Tcl_ObjSetVar2(interp, objv[5], Tcl_NewStringObj("index", -1), Tcl_NewIntObj(search.index), flags) ==
            NULL) {
            return TCL_ERROR;
        }
        if (Tcl_ObjSetVar2(interp, objv[5], Tcl_NewStringObj("x", -1), Tcl_NewDoubleObj(search.point.x), flags) ==
            NULL) {
            return TCL_ERROR;
        }
        if (Tcl_ObjSetVar2(interp, objv[5], Tcl_NewStringObj("y", -1), Tcl_NewDoubleObj(search.point.y), flags) ==
            NULL) {
            return TCL_ERROR;
        }
        if (Tcl_ObjSetVar2(interp, objv[5], Tcl_NewStringObj("dist", -1), Tcl_NewDoubleObj(search.dist), flags) ==
            NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Tcl_NewIntObj(1));
    } else {
        /* an array with values "name" -> "" */
        if (Tcl_ObjSetVar2(interp, objv[5], Tcl_NewStringObj("name", -1), Tcl_NewObj(), flags) == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Sets the element specifications by the given the command line
 *      arguments and calls the element specification configuration
 *      routine. If zero or one command line options are given, only
 *      information about the option(s) is returned in the interpreter
 *      result. If the element configuration has changed and the element
 *      is currently displayed, the axis limits are updated and
 *      recomputed.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Graph will be redrawn to reflect the new display list.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Size numNames;
    Tcl_Size numOpts;
    Tcl_Obj *const *options;
    Tcl_Size i;

    /*
     * Figure out where the option/value pairs begin.
     */
    objc -= 3;
    objv += 3;

    for (i = 0; i < objc; i++) {
        const char *string;

        string = Tcl_GetString(objv[i]);

        if (string[0] == '-') {
            break;
        }

        if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    numNames = i;
    numOpts = objc - i;
    options = objv + numNames;

    for (i = 0; i < numNames; i++) {
        if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR;
        }

        if (numOpts == 0) {
            Tcl_Obj *resultObjPtr;

            resultObjPtr = Tk_GetOptionInfo(interp, (char *)elemPtr, elemPtr->optionTable, NULL, graphPtr->tkwin);

            if (resultObjPtr == NULL) {
                return TCL_ERROR;
            }

            Tcl_SetObjResult(interp, resultObjPtr);

            return TCL_OK;
        }

        if (numOpts == 1) {
            Tcl_Obj *resultObjPtr;

            resultObjPtr = Tk_GetOptionInfo(interp, (char *)elemPtr, elemPtr->optionTable, options[0], graphPtr->tkwin);

            if (resultObjPtr == NULL) {
                return TCL_ERROR;
            }

            Tcl_SetObjResult(interp, resultObjPtr);

            return TCL_OK;
        }

        if (ConfigureElementOptions(graphPtr, elemPtr, numOpts, options, NULL) != TCL_OK) {
            return TCL_ERROR;
        }

        /*
         * Keep the element's hidden state and display-list membership
         * synchronized.
         */
        {
            Rbc_ChainLink *linkPtr;

            for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
                 linkPtr = Rbc_ChainNextLink(linkPtr)) {
                if (elemPtr == Rbc_ChainGetValue(linkPtr)) {
                    break;
                }
            }

            if (elemPtr->hidden != (linkPtr == NULL)) {
                if (linkPtr == NULL) {
                    Rbc_ChainPrepend(graphPtr->elements.displayList, elemPtr);
                } else {
                    Rbc_ChainDeleteLink(graphPtr->elements.displayList, linkPtr);
                }
            }
        }

        graphPtr->flags |= RESET_AXES;
        graphPtr->flags |= RESET_WORLD;
        graphPtr->flags |= MAP_WORLD | REDRAW_WORLD;

        elemPtr->flags |= MAP_ITEM;
    }

    graphPtr->flags |= REDRAW_BACKING_STORE | DRAW_MARGINS;

    Rbc_EventuallyRedrawGraph(graphPtr);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DeactivateOp --
 *
 *      Clears the active bit for the named elements.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns TCL_OK if no errors occurred.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int DeactivateOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Size i;

    for (i = 3; i < objc; i++) {
        if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR; /* Can't find named element */
        }
        elemPtr->flags &= ~ELEM_ACTIVE;
        if (elemPtr->activeIndices != NULL) {
            ckfree((char *)elemPtr->activeIndices);
            elemPtr->activeIndices = NULL;
        }
        elemPtr->nActiveIndices = 0;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteOp --
 *
 *      Delete the named elements from the graph.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TCL_ERROR is returned if any of the named elements can not be
 *      found.  Otherwise TCL_OK is returned;
 *
 * Side Effects:
 *      If the element is currently displayed, the plotting area of
 *      the graph is redrawn. Memory and resources allocated by the
 *      elements are released.
 *
 *----------------------------------------------------------------------
 */
static int DeleteOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Size i;

    for (i = 3; i < objc; i++) {
        if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR; /* Can't find named element */
        }
        DestroyElement(graphPtr, elemPtr);
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ExistsOp --
 *
 *      Indicates if the named element exists in the graph.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      The return value is a standard Tcl result.  The interpreter
 *      result will contain "1" or "0".
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ExistsOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->elements.table, Tcl_GetString(objv[3]));
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(hPtr != NULL));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetOp --
 *
 *      Returns the name of the picked element (using the element
 *      bind operation).  Right now, the only name accepted is
 *      "current".
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.  The interpreter result will contain
 *      the name of the element.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int GetOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    register Element *elemPtr;
    const char *str = Tcl_GetString(objv[3]);

    if ((str[0] == 'c') && (strcmp(str, "current") == 0)) {
        elemPtr = (Element *)Rbc_GetCurrentItem(graphPtr->bindTable);
        /* Report only on elements. */
        if ((elemPtr != NULL) && ((elemPtr->classUid == rbcBarElementUid) || (elemPtr->classUid == rbcLineElementUid) ||
                                  (elemPtr->classUid == rbcStripElementUid))) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->name, -1));
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NamesOp --
 *
 *      Returns the names of the elements is the graph matching
 *      one of more patterns provided.  If no pattern arguments
 *      are given, then all element names will be returned.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      The return value is a standard Tcl result. The interpreter
 *      result will contain a Tcl list of the element names.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int NamesOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_HashSearch cursor;
    register Tcl_HashEntry *hPtr;
    Tcl_Size i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = (Element *)Tcl_GetHashValue(hPtr);
        if (objc == 3) {
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(elemPtr->name, -1));
            continue;
        }
        for (i = 3; i < objc; i++) {
            if (Tcl_StringMatch(elemPtr->name, Tcl_GetString(objv[i]))) {
                Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(elemPtr->name, -1));
                break;
            }
        }
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ShowOp --
 *
 *      Queries or resets the element display list.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      The return value is a standard Tcl result. The interpreter
 *      result will contain the new display list of element names.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ShowOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Rbc_ChainLink *linkPtr;
    Tcl_Obj *resultObj;

    if (objc == 4) {
        if (RebuildDisplayList(graphPtr, objv[3]) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    resultObj = Tcl_NewListObj(0, NULL);
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(elemPtr->name, -1));
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TypeOp --
 *
 *      Returns the name of the type of the element given by some
 *      element name.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Rbc_Uid type
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result. Returns the type of the element in
 *      the interpreter result.
 *      If the identifier given doesn't represent an element, then
 *      an error message is left in the interpreter result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int TypeOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;

    if (NameToElement(graphPtr, objv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR; /* Can't find named element */
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->classUid, -1));
    return TCL_OK;
}

/*
 * Global routines:
 */
static const ElementOpSpec elemOps[] = {{{"activate", 3, 0, "?elemName? ?index...?"}, ActivateOp},
                                        {{"bind", 3, 6, "elemName sequence command"}, BindOp},
                                        {{"cget", 5, 5, "elemName option"}, CgetOp},
                                        {{"closest", 6, 0, "x y varName ?option value?... ?elemName?..."}, ClosestOp},
                                        {{"configure", 4, 0, "elemName ?elemName?... ?option value?..."}, ConfigureOp},
                                        {{"create", 4, 0, "elemName ?option value?..."}, CreateOp},
                                        {{"deactivate", 3, 0, "?elemName?..."}, DeactivateOp},
                                        {{"delete", 3, 0, "?elemName?..."}, DeleteOp},
                                        {{"exists", 4, 4, "elemName"}, ExistsOp},
                                        {{"get", 4, 4, "name"}, GetOp},
                                        {{"names", 3, 0, "?pattern?..."}, NamesOp},
                                        {{"show", 3, 4, "?elemList?"}, ShowOp},
                                        {{"type", 4, 4, "elemName"}, TypeOp},
                                        {{NULL, 0, 0, NULL}, NULL}};

/*
 * ----------------------------------------------------------------
 *
 * Rbc_ElementOp --
 *
 *      This procedure is invoked to process the Tcl command that
 *      corresponds to a widget managed by this module.  See the user
 *      documentation for details on what it does.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record 
 *      Tcl_Interp *interp
 *      int objc - # arguments 
 *      Tcl_Obj *const objv[] - Argument list 
 *      Rbc_Uid type
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 * ----------------------------------------------------------------
 */
int Rbc_ElementOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid type) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, elemOps, (Tcl_Size)sizeof(elemOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }

    return elemOps[index].proc(graphPtr, interp, type, objc, objv);
}
