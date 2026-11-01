/*
 * rbcVecObjCmd.c --
 *
 *      This file contains all commands to process the operations
 *      on instances of a vector.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/**
 * @file rbcVecCmd.c
 * @brief Vector instance operations, conversions and binary input.
 */
/**
 * @defgroup vector_command_paths Vector instance command paths
 * @brief Selected private mutation and input helpers; see the developer manual
 * for partial-update and callback lifetime limitations.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "rbcVectorInt.h"

static int AddVectorSizes(Tcl_Interp *interp, Tcl_Size a, Tcl_Size b, Tcl_Size *resultPtr) {
    if ((a < 0) || (b < 0) || (a > (TCL_SIZE_MAX - b))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector size is too large", -1));
        return TCL_ERROR;
    }
    *resultPtr = a + b;
    return TCL_OK;
}

static int MultiplyVectorSizes(Tcl_Interp *interp, Tcl_Size a, Tcl_Size b, Tcl_Size *resultPtr) {
    if ((a < 0) || (b < 0) || ((b != 0) && (a > (TCL_SIZE_MAX / b)))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector size is too large", -1));
        return TCL_ERROR;
    }  
    *resultPtr = a * b;
    return TCL_OK;
}

static int GetArrayByteCount(Tcl_Interp *interp, Tcl_Size count, size_t elementSize, size_t *byteCountPtr) {
    if ((count < 0) || ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / elementSize))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("allocation size is too large", -1));
        return TCL_ERROR;
    }
    *byteCountPtr = (size_t)count * elementSize;
    return TCL_OK;
}

/*
 * Command mutation helpers. Callers preserve the destination and interpreter.
 * Never dereference names or storage after a callback without checking lifetime.
 */
static int CheckCommandDestination(VectorObject *vPtr) {
    if (Tcl_InterpDeleted(vPtr->interp) || vPtr->destroying) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector was destroyed during command evaluation", -1));
        Tcl_SetErrorCode(vPtr->interp, "RBC", "VECTOR", "DESTROYED", NULL);
        return TCL_ERROR;
    }
    return Rbc_VectorCheckWritable(vPtr->interp, (Rbc_Vector *)vPtr);
}

/* Prepare storage without invoking a user free procedure. Existing capacity is
 * reused for appends and binary writes; growth copies the existing prefix. */
static int PrepareCommandWrite(VectorObject *vPtr, Tcl_Size length, VectorData *dataPtr, Tcl_Size *sizePtr) {
    size_t bytes, usedBytes;
    size_t elementSize = (vPtr->type == RBC_VECTOR_REAL) ? sizeof(double) : sizeof(Rbc_Complex);
    Tcl_Size size = vPtr->size;

    if (CheckCommandDestination(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (length <= size) {
        *dataPtr = vPtr->data;
        *sizePtr = size;
        return TCL_OK;
    }
    if (size == 0) size = DEF_ARRAY_SIZE;
    while (size < length) {
        if (size > TCL_SIZE_MAX / 2) {
            size = length;
            break;
        }
        size *= 2;
    }
    if (GetArrayByteCount(vPtr->interp, size, elementSize, &bytes) != TCL_OK ||
        GetArrayByteCount(vPtr->interp, vPtr->length, elementSize, &usedBytes) != TCL_OK) {
        return TCL_ERROR;
    }
    dataPtr->raw = Tcl_AttemptAlloc(bytes);
    if (dataPtr->raw == NULL) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector command storage", -1));
        return TCL_ERROR;
    }
    if (usedBytes > 0) memcpy(dataPtr->raw, vPtr->data.raw, usedBytes);
    *sizePtr = size;
    return TCL_OK;
}

/* Install completed data before releasing old storage: a custom free procedure
 * may delete, rename or modify the destination. No storage access follows it. */
static void FinishCommandWrite(VectorObject *vPtr, VectorData data, Tcl_Size length, Tcl_Size size) {
    void *oldData = vPtr->data.raw;
    Tcl_FreeProc *oldFree = vPtr->freeProc;
    Tcl_Interp *interp = vPtr->interp;
    Tcl_InterpState state;

    vPtr->data = data;
    vPtr->length = length;
    vPtr->size = size;
    vPtr->first = 0;
    vPtr->last = length - 1;
    vPtr->min = vPtr->max = rbcNaN;
    vPtr->minIndex = vPtr->maxIndex = -1;
    vPtr->notifyFlags |= UPDATE_RANGE;
    if (oldData != data.raw) {
        vPtr->freeProc = TCL_DYNAMIC;
        if (oldData != NULL && oldFree != TCL_STATIC) {
            state = Tcl_SaveInterpState(interp, TCL_OK);
            if (oldFree == TCL_DYNAMIC) ckfree(oldData);
            else oldFree(oldData);
            Tcl_RestoreInterpState(interp, state);
        }
    }
}

/* Preserve the command's result and error options across traces and clients.
 * A negative first reports a full change; append callers may supply a range. */
static void NotifyCommandWrite(VectorObject *vPtr, Tcl_Size first) {
    Tcl_Interp *interp = vPtr->interp;
    Tcl_InterpState state = Tcl_SaveInterpState(interp, TCL_OK);

    if (!Tcl_InterpDeleted(interp) && !vPtr->destroying) {
        if (vPtr->flush) Rbc_VectorFlushCache(vPtr);
        if (!Tcl_InterpDeleted(interp) && !vPtr->destroying) {
            if (first >= 0 && first < vPtr->length) {
                Rbc_VectorUpdateClientsRange(vPtr, first, vPtr->length - 1);
            } else {
                Rbc_VectorUpdateClients(vPtr);
            }
        }
    }
    Tcl_RestoreInterpState(interp, state);
}

/* Convert once into owned staging storage. Conversion callbacks have their own
 * side effects, but this helper never writes destination values or length. */
static int ConvertCommandList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[], VectorData *dataPtr) {
    Tcl_Size i;
    Tcl_Obj *listObj;
    Tcl_Obj **values;
    size_t bytes;
    size_t elementSize = (vPtr->type == RBC_VECTOR_REAL) ? sizeof(double) : sizeof(Rbc_Complex);

    dataPtr->raw = NULL;
    if (GetArrayByteCount(vPtr->interp, objc, elementSize, &bytes) != TCL_OK) return TCL_ERROR;
    if (bytes > 0) {
        dataPtr->raw = Tcl_AttemptAlloc(bytes);
        if (dataPtr->raw == NULL) {
            Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector conversion storage", -1));
            return TCL_ERROR;
        }
    }
    /* Keep a private list representation: a conversion callback may shimmer
     * the original argument into another Tcl internal representation. */
    listObj = Tcl_NewListObj(objc, objv);
    Tcl_IncrRefCount(listObj);
    Tcl_ListObjGetElements(NULL, listObj, &objc, &values);
    for (i = 0; i < objc; i++) {
        int result;
        if (vPtr->type == RBC_VECTOR_COMPLEX) {
            result = Rbc_GetComplex(vPtr->interp, values[i], &dataPtr->complex[i]);
        } else {
            result = Rbc_GetDouble(vPtr->interp, values[i], &dataPtr->real[i]);
        }
        if (result != TCL_OK || CheckCommandDestination(vPtr) != TCL_OK) {
            if (dataPtr->raw != NULL) ckfree(dataPtr->raw);
            dataPtr->raw = NULL;
            Tcl_DecrRefCount(listObj);
            return TCL_ERROR;
        }
    }
    Tcl_DecrRefCount(listObj);
    return TCL_OK;
}

static int GetArithmeticScalar(Tcl_Interp *interp, Tcl_Obj *objPtr, Rbc_Complex *valuePtr, int *isComplexPtr) {
    Tcl_Size objc;
    Tcl_Obj **objv;

    /*
     * A two-element Tcl list is explicitly a complex scalar.
     *
     * Do this test before Rbc_GetDouble(), because a value such
     * as {3 -4} could otherwise be interpreted as the Tcl
     * expression "3 - 4".
     */
    *isComplexPtr = FALSE;
    if (Tcl_ListObjGetElements(NULL, objPtr, &objc, &objv) == TCL_OK) {
        if (objc == 2) {
            *isComplexPtr = TRUE;
        }
    }
    return Rbc_GetComplex(interp, objPtr, valuePtr);
}

static int ComplexArithmetic(Tcl_Interp *interp, int operator, Rbc_Complex a, Rbc_Complex b, Rbc_Complex *resultPtr) {
    switch (operator) {
    case '+':
        *resultPtr = Rbc_ComplexAdd(a, b);
        break;
    case '-':
        *resultPtr = Rbc_ComplexSub(a, b);
        break;
    case '*':
        *resultPtr = Rbc_ComplexMul(a, b);
        break;
    case '/':
        if (Rbc_ComplexIsZero(b)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("divide by zero", -1));
            return TCL_ERROR;
        }
        *resultPtr = Rbc_ComplexDiv(a, b);
        break;
    default:
        Tcl_Panic("bad complex arithmetic operator %c", operator);
    }
    return TCL_OK;
}

enum NativeFormats {
    FMT_UNKNOWN = -1,
    FMT_UCHAR,
    FMT_CHAR,
    FMT_USHORT,
    FMT_SHORT,
    FMT_UINT,
    FMT_INT,
    FMT_ULONG,
    FMT_LONG,
    FMT_ULONGLONG,
    FMT_LONGLONG,
    FMT_FLOAT,
    FMT_DOUBLE
};

static int AppendVector(VectorObject *destPtr, VectorObject *srcPtr);
static int AppendList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]);
static int CopyValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, Tcl_Size length, int swap,
                      Tcl_Size *indexPtr);
static int CopyComplexValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, Tcl_Size length,
                             int swap, Tcl_Size *indexPtr);
static int InRange(double value, double min, double max);
static int CopyList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]);
static int CompareVectorIndices(VectorObject *const *vectorArr, Tcl_Size nVectors, Tcl_Size i1, Tcl_Size i2);
static void SiftVectorIndices(Tcl_Size *indexArr, Tcl_Size start, Tcl_Size end, VectorObject *const *vectorArr,
                              Tcl_Size nVectors);
static void SortVectorIndices(Tcl_Size *indexArr, Tcl_Size length, VectorObject *const *vectorArr, Tcl_Size nVectors);

/* Instance Functions Definitions (rbcVecObjCmd.c) */
typedef int RbcVectorCmdOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcVectorCmdOp *proc;
} VectorInstOpSpec;

static RbcVectorCmdOp TypeOp;
static RbcVectorCmdOp ReadOnlyOp;
static RbcVectorCmdOp AppendOp;
static RbcVectorCmdOp ArithOp;
static RbcVectorCmdOp BinreadOp;
static RbcVectorCmdOp ClearOp;
static RbcVectorCmdOp DeleteOp;
static RbcVectorCmdOp DupOp;
static RbcVectorCmdOp ExprOp;
static RbcVectorCmdOp IndexOp;
static RbcVectorCmdOp LengthOp;
static RbcVectorCmdOp MergeOp;
static RbcVectorCmdOp NormalizeOp;
static RbcVectorCmdOp OffsetOp;
static RbcVectorCmdOp RandomOp;
static RbcVectorCmdOp PopulateOp;
static RbcVectorCmdOp RangeOp;
static RbcVectorCmdOp SearchOp;
static RbcVectorCmdOp SeqOp;
static RbcVectorCmdOp SetOp;
static RbcVectorCmdOp SortOp;
static RbcVectorCmdOp SplitOp;
static RbcVectorCmdOp VariableOp;

static const VectorInstOpSpec vectorInstOpCmd[] = {{{"*", 3, 3, "list"}, ArithOp},
                                                   {{"+", 3, 3, "list"}, ArithOp},
                                                   {{"-", 3, 3, "list"}, ArithOp},
                                                   {{"/", 3, 3, "list"}, ArithOp},
                                                   {{"append", 3, 0, "item ?item...?"}, AppendOp},
                                                   {{"binread", 3, 0, "channel ?numValues? ?flags?"}, BinreadOp},
                                                   {{"clear", 2, 2, ""}, ClearOp},
                                                   {{"delete", 3, 0, "index ?index?..."}, DeleteOp},
                                                   {{"dup", 3, 3, "vecname"}, DupOp},
                                                   {{"expr", 3, 3, "expression"}, ExprOp},
                                                   {{"fft", 3, 0, "?destComplex? ?-real destReal -imag destImag? ?-imaginput vecName? ?-length auto|size?"}, Rbc_VectorFftOp},
                                                   {{"fftfreq", 3, 0, "length ?-delta interval? ?-onesided boolean?"}, Rbc_VectorFftfreqOp},
                                                   {{"ifft", 3, 0, "?destComplex? ?-real destReal -imag destImag? ?-imaginput vecName? ?-length auto|size?"}, Rbc_VectorFftOp},
                                                   {{"index", 3, 4, "index ?value?"}, IndexOp},
                                                   {{"length", 2, 3, "?newSize?"}, LengthOp},
                                                   {{"merge", 3, 0, "vecName ?vecName?..."}, MergeOp},
                                                   {{"normalize", 2, 3, "?vecName?"}, NormalizeOp},
                                                   {{"offset", 2, 3, "?offset?"}, OffsetOp},
                                                   {{"populate", 4, 4, "vecName density"}, PopulateOp},
                                                   {{"random", 2, 2, ""}, RandomOp},
                                                   {{"range", 4, 4, "first last"}, RangeOp},
                                                   {{"readonly", 2, 3, "?boolean?"}, ReadOnlyOp},
                                                   {{"search", 3, 5, "?-value? value ?value?"}, SearchOp},
                                                   {{"seq", 4, 5, "start end ?step?"}, SeqOp},
                                                   {{"set", 3, 3, "list"}, SetOp},
                                                   {{"sort", 2, 0, "?-reverse? ?vecName?..."}, SortOp},
                                                   {{"split", 2, 0, "?vecName?..."}, SplitOp},
                                                   {{"type", 2, 2, ""}, TypeOp},
                                                   {{"variable", 2, 3, "?varName?"}, VariableOp},
                                                   {{"window", 4, 0, "type length ?-periodic boolean?"}, Rbc_VectorWindowOp},
                                                   {{NULL, 0, 0, NULL}, NULL}};

static int ComplexOpSupported(RbcVectorCmdOp *proc) {
    return ((proc == ReadOnlyOp) || (proc == Rbc_VectorFftOp) || (proc == ExprOp) || (proc == AppendOp) || (proc == ArithOp) || (proc == BinreadOp) || (proc == ClearOp) ||
            (proc == DeleteOp) || (proc == DupOp) || (proc == IndexOp) || (proc == LengthOp) || (proc == MergeOp) ||
            (proc == OffsetOp) || (proc == PopulateOp) || (proc == RandomOp) || (proc == RangeOp) ||
            (proc == SearchOp) || (proc == SeqOp) || (proc == SetOp) || (proc == SplitOp) || (proc == TypeOp) ||
            (proc == VariableOp));
}

/*
 * ----------------------------------------------------------------------
 *
 * VectorInstanceCmd --
 *
 *      Instance command for the vector. This command
 *      is registered via Tcl_CreatObjCommand for each new vector
 *      and is called when the Tcl vector instance command is called
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      Returns the result from the operation called on the command or
 *      TCL_ERROR if the operation was unknown or a wrong number of
 *      arguments was specified
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int Rbc_VectorInstanceObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject *vPtr;
    int index;

    vPtr = clientData;
    if (Rbc_GetOpIndexFromObj(interp, vectorInstOpCmd, (Tcl_Size)sizeof(vectorInstOpCmd[0]), RBC_OP_ARG1, objc, objv,
                              &index) != TCL_OK) {
        return TCL_ERROR;
    }
    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        RbcVectorCmdOp *proc;

        proc = vectorInstOpCmd[index].proc;
        if (!ComplexOpSupported(proc)) {
            Tcl_SetObjResult(
                interp, Tcl_ObjPrintf("operation \"%s\" is not supported for complex vectors", Tcl_GetString(objv[1])));
            return TCL_ERROR;
        }
    }
    /* Check only operations that write this vector, not read-only sources. */
    {
        RbcVectorCmdOp *proc = vectorInstOpCmd[index].proc;
        if ((proc == AppendOp) || (proc == BinreadOp) || (proc == DeleteOp) ||
            (proc == ExprOp) || (proc == MergeOp) || (proc == RandomOp) ||
            (proc == SeqOp) || (proc == SetOp) || (proc == SortOp) ||
            (proc == Rbc_VectorFftfreqOp) || (proc == Rbc_VectorWindowOp) ||
            ((proc == IndexOp) && (objc == 4)) ||
            (((proc == LengthOp) || (proc == OffsetOp)) && (objc == 3))) {
            if (Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    /*
     * Reset the selected region before each instance operation.
     */
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;
    {
        int result;
        Tcl_Preserve(interp);
        Tcl_Preserve(vPtr);
        result = vectorInstOpCmd[index].proc(vPtr, interp, objc, objv);
        Tcl_Release(vPtr);
        Tcl_Release(interp);
        return result;
    }
}

/*
 * ReadOnlyOp -- Query or change the vector's reversible data protection.
 * Setting the state does not alter values or notify graph clients.
 */
static int ReadOnlyOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int readOnly;

    if (objc == 3) {
        if (Tcl_GetBooleanFromObj(interp, objv[2], &readOnly) != TCL_OK) {
            return TCL_ERROR;
        }
        Rbc_VectorSetReadOnly(interp, (Rbc_Vector *)vPtr, readOnly);
    }
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(vPtr->readOnly));
    return TCL_OK;
}

static int TypeOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    const char *typeName;

    (void)objc;
    (void)objv;
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        typeName = "real";
        break;
    case RBC_VECTOR_COMPLEX:
        typeName = "complex";
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)vPtr->type);
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(typeName, -1));
    return TCL_OK;
}

/**
 * @brief Append vector ranges or Tcl lists in argument order.
 * @ingroup vector_command_paths
 *
 * Vector lookup precedes list conversion. Earlier arguments remain appended
 * if a later argument fails. Reports the appended range when length grew,
 * including the error path; otherwise successful calls report a full update.
 * Flushes the mapped cache when configured. Notification may execute callbacks.
 */
static int AppendOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size oldLength = vPtr->length;
    Tcl_Size i;
    int result = TCL_OK;
    int changed = FALSE;

    for (i = 2; i < objc; i++) {
        VectorObject *srcPtr = Rbc_VectorParseElement(NULL, vPtr->dataPtr, Tcl_GetString(objv[i]), NULL, NS_SEARCH_BOTH);
        if (CheckCommandDestination(vPtr) != TCL_OK) {
            result = TCL_ERROR;
            break;
        }
        if (srcPtr != NULL) {
            result = AppendVector(vPtr, srcPtr);
        } else {
            Tcl_Size count;
            Tcl_Obj **values;
            result = Tcl_ListObjGetElements(interp, objv[i], &count, &values);
            if (result == TCL_OK) result = AppendList(vPtr, count, values);
        }
        if (result != TCL_OK) break;
        changed = TRUE;
        if (i + 1 < objc && CheckCommandDestination(vPtr) != TCL_OK) {
            result = TCL_ERROR;
            break;
        }
    }
    if (changed) NotifyCommandWrite(vPtr, oldLength);
    return result;
}

/*
 * -----------------------------------------------------------------------
 *
 * ArithOp --
 *
 *     TODO: Description
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If the source vector doesn't exist
 *      or the source list is not a valid list of numbers, TCL_ERROR
 *      returned.  Otherwise TCL_OK is returned.
 *
 * Side Effects:
 *      The vector data is reset.  Clients of the vector are notified.
 *      Any cached array indices are flushed.
 *
 * -----------------------------------------------------------------------
 */
static int ArithOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv) {
    VectorObject *v2Ptr;
    Tcl_Obj *listObjPtr;
    const char *operatorString;
    int operator;
    Tcl_Size i;

    operatorString = Tcl_GetString(objv[1]);
    operator= operatorString[0];
    v2Ptr = Rbc_VectorParseElement(NULL, vPtr->dataPtr, Tcl_GetString(objv[2]), NULL, NS_SEARCH_BOTH);
    listObjPtr = Tcl_NewListObj(0, NULL);
    if (v2Ptr != NULL) {
        Tcl_Size j;
        Tcl_Size length;
        int complexResult;

        length = v2Ptr->last - v2Ptr->first + 1;
        if (length != vPtr->length) {
            Rbc_AppendResultStrings(interp, "vectors \"", Tcl_GetString(objv[0]), "\" and \"", Tcl_GetString(objv[2]),
                                    "\" are not the same length", (char *)NULL);
            return TCL_ERROR;
        }
        complexResult = ((vPtr->type == RBC_VECTOR_COMPLEX) || (v2Ptr->type == RBC_VECTOR_COMPLEX));
        if (!complexResult) {
            double value;

            /*
             * Keep the original real arithmetic path unchanged.
             */
            switch (operator) {
            case '*':
                for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                    value = vPtr->data.real[i] * v2Ptr->data.real[j];
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '/':
                for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                    value = vPtr->data.real[i] / v2Ptr->data.real[j];
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '-':
                for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                    value = vPtr->data.real[i] - v2Ptr->data.real[j];
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '+':
                for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                    value = vPtr->data.real[i] + v2Ptr->data.real[j];
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            }
        } else {
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                Rbc_Complex a;
                Rbc_Complex b;
                Rbc_Complex result;

                a = Rbc_VectorValueAsComplex(vPtr, i);
                b = Rbc_VectorValueAsComplex(v2Ptr, j);
                if (ComplexArithmetic(interp, operator, a, b, &result) != TCL_OK) {
                    return TCL_ERROR;
                }
                Tcl_ListObjAppendElement(interp, listObjPtr, Rbc_NewComplexObj(result));
            }
        }
    } else {
        Rbc_Complex scalar;
        int scalarIsComplex;
        int complexResult;

        if (GetArithmeticScalar(interp, objv[2], &scalar, &scalarIsComplex) != TCL_OK) {
            return TCL_ERROR;
        }
        complexResult = ((vPtr->type == RBC_VECTOR_COMPLEX) || scalarIsComplex);
        if (!complexResult) {
            double value;
            double realScalar;

            realScalar = scalar.real;
            /*
             * Again preserve the existing real implementation.
             */
            switch (operator) {
            case '*':
                for (i = 0; i < vPtr->length; i++) {
                    value = vPtr->data.real[i] * realScalar;
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '/':
                for (i = 0; i < vPtr->length; i++) {
                    value = vPtr->data.real[i] / realScalar;
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '-':
                for (i = 0; i < vPtr->length; i++) {
                    value = vPtr->data.real[i] - realScalar;
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '+':
                for (i = 0; i < vPtr->length; i++) {
                    value = vPtr->data.real[i] + realScalar;
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            }
        } else {
            for (i = 0; i < vPtr->length; i++) {
                Rbc_Complex a;
                Rbc_Complex result;

                a = Rbc_VectorValueAsComplex(vPtr, i);
                if (ComplexArithmetic(interp, operator, a, scalar, &result) != TCL_OK) {
                    return TCL_ERROR;
                }
                Tcl_ListObjAppendElement(interp, listObjPtr, Rbc_NewComplexObj(result));
            }
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    return TCL_OK;
}

/*
 * Types and helper functions (for Tcl_ParseArgsObjv) for "binread" vector
 * instance command
 */
struct NativeFmtValue {
    int size;
    enum NativeFormats format;
};

/**
 * @brief Select a supported native scalar type from a format and byte width.
 * @ingroup vector_command_paths
 *
 * Floating, signed and unsigned formats resolve against native C sizeof
 * values. Returns the consumed argument count, or -1 on error. This is host representation, not a portable
 * serialized type description; conversion to double may lose integer precision.
 */
static Tcl_Size ParseFormat(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv, void *dstPtr) {
    const struct NativeFmtValue *table, *cursor;
    int size;
    Tcl_Size len;
    const char *string;

    (void)clientData;
    static const struct NativeFmtValue rtable[] = {
        {sizeof(float), FMT_FLOAT}, {sizeof(double), FMT_DOUBLE}, {-1, FMT_UNKNOWN}};
    static const struct NativeFmtValue itable[] = {
        {sizeof(char), FMT_CHAR}, {sizeof(short), FMT_SHORT},        {sizeof(int), FMT_INT},
        {sizeof(long), FMT_LONG}, {sizeof(long long), FMT_LONGLONG}, {-1, FMT_UNKNOWN}};
    static const struct NativeFmtValue utable[] = {{sizeof(unsigned char), FMT_UCHAR},
                                                   {sizeof(unsigned short), FMT_USHORT},
                                                   {sizeof(unsigned int), FMT_UINT},
                                                   {sizeof(unsigned long), FMT_ULONG},
                                                   {sizeof(unsigned long long), FMT_ULONGLONG},
                                                   {-1, FMT_UNKNOWN}};
    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("option \"-format\" requires an additional argument", -1));
        return -1;
    }
    string = Tcl_GetStringFromObj(objv[0], &len);
    if (len < 2) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown binary format \"%s\"", string));
        return -1;
    }

    Tcl_Obj *sizeObjPtr;
    int result;
    
    sizeObjPtr = Tcl_NewStringObj(string + 1, len - 1);
    Tcl_IncrRefCount(sizeObjPtr);
    result = Tcl_GetIntFromObj(NULL, sizeObjPtr, &size);
    Tcl_DecrRefCount(sizeObjPtr);
    if (result != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown binary format \"%s\": incorrect byte size", string));
        return -1;
    }
    switch (tolower(string[0])) {
    case 'r':
    case 'f':
        table = rtable;
        break;
    case 'i':
        table = itable;
        break;
    case 'u':
        table = utable;
        break;
    default:
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown binary format \"%s\": "
                                               "should be i#, r#, or u#, "
                                               "where # is the size in bytes",
                                               string));
        return -1;
    }
    for (cursor = table; cursor->size != -1; cursor++) {
        if (cursor->size == size) {
            *(struct NativeFmtValue *)dstPtr = *cursor;
            return 1;
        }
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't handle format \"%s\"", string));
    return -1;
}

/**
 * @brief Parse a binary-input starting storage position.
 * @ingroup vector_command_paths
 *
 * Uses vector index conversion, allowing positions from zero through length
 * inclusive. Returns the consumed argument count, or -1 on error.
 * Index expressions may evaluate Tcl.
 */
static Tcl_Size ParseAt(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv, void *dstPtr) {
    VectorObject *vPtr = (VectorObject *)clientData;
    const char *string;
    Tcl_Size first;

    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("option \"-at\" requires an additional argument", -1));
        return -1;
    }
    string = Tcl_GetString(objv[0]);
    if (Rbc_VectorGetIndex(interp, vPtr, string, &first, 0, (Rbc_VectorIndexProc **)NULL) != TCL_OK) {
        return -1;
    }
    if (first > vPtr->length || first < 0) {
        Rbc_AppendResultStrings(interp, "index \"", string, "\" is out of range", NULL);
        return -1;
    }
    *(Tcl_Size *)dstPtr = first;
    return 1;
}
/**
 * @brief Read native-format channel scalars into vector storage.
 * @ingroup vector_command_paths
 *
 * Retains a readable channel, temporarily sets binary translation, and restores
 * the saved translation if the caller has not closed the channel. Real values use one scalar, complex values
 * two interleaved scalars. A positive count performs one read; zero reads
 * chunks until EOF or a zero-byte read. Rejects incomplete values rather than
 * buffering partial records. Completed chunks remain on error and trigger cache
 * flushing/notification while preserving the original error state. Success
 * reports logical value count. Checks destination lifetime/read-only state and
 * channel registration after reflected reads. The dispatcher retains the vector.
 */
static int BinreadOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Channel channel = NULL;
    int retained = FALSE;
    const char *channelName;
    const char *string;
    char *byteArr;
    enum NativeFormats fmt;
    struct NativeFmtValue nfmt;
    Tcl_DString translation;
    Tcl_InterpState savedState;
    Tcl_Size count;
    Tcl_Size first;
    Tcl_Size total;
    Tcl_Size optionStart;
    Tcl_Size argc;
    Tcl_Size bufferValues;
    Tcl_Size bufferBytes;
    Tcl_Size bytesRead;
    Tcl_Size length;
    Tcl_Size newTotal;
    size_t bufferByteCount;
    Tcl_Size componentsPerValue;
    Tcl_Size bytesPerValue;
    int mode;
    int size;
    int swap;
    int translationChanged;
    int result;

    byteArr = NULL;
    savedState = NULL;
    count = 0;
    first = vPtr->length;
    total = 0;
    optionStart = 3;
    swap = FALSE;
    translationChanged = FALSE;
    result = TCL_ERROR;
    nfmt.size = sizeof(double);
    nfmt.format = FMT_DOUBLE;
    Tcl_DStringInit(&translation);
    channelName = Tcl_GetString(objv[2]);
    channel = Tcl_GetChannel(interp, channelName, &mode);
    if (channel == NULL) {
        goto cleanup;
    }
    if ((mode & TCL_READABLE) == 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("channel \"%s\" wasn't opened for reading", channelName));
        goto cleanup;
    }
    Tcl_RegisterChannel(NULL, channel);
    retained = TRUE;
    /*
     * An optional count precedes the options. A zero count retains the
     * historical meaning of reading until EOF.
     */
    if (optionStart < objc) {
        string = Tcl_GetString(objv[optionStart]);
        if (string[0] != '-') {
            if (Tcl_GetSizeIntFromObj(interp, objv[optionStart], &count) != TCL_OK) {
                goto cleanup;
            }
            if (count < 0) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("count can't be negative", -1));
                goto cleanup;
            }
            optionStart++;
        }
    }
    /*
     * Tcl_ParseArgsObjv() treats its first argument as the command name.
     * Use either the channel name or the optional count as that ignored
     * argument, then parse the remaining options.
     */
    argc = objc - optionStart + 1;
    if (argc > 1) {
        const Tcl_ArgvInfo binreadOpts[] = {{TCL_ARGV_CONSTANT, "-swap", (void *)TRUE, &swap, NULL, NULL},
                                            {TCL_ARGV_GENFUNC, "-format", ParseFormat, &nfmt, NULL, NULL},
                                            {TCL_ARGV_GENFUNC, "-at", ParseAt, &first, NULL, vPtr},
                                            TCL_ARGV_TABLE_END};
        if (Tcl_ParseArgsObjv(interp, binreadOpts, &argc, objv + optionStart - 1, NULL) != TCL_OK) {
            goto cleanup;
        }
    }
    if (CheckCommandDestination(vPtr) != TCL_OK) goto cleanup;
    if (!Tcl_IsChannelRegistered(interp, channel)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("channel was closed during command evaluation", -1));
        goto cleanup;
    }
    fmt = nfmt.format;
    size = nfmt.size;
    if ((fmt == FMT_UNKNOWN) || (size <= 0)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid binary format", -1));
        goto cleanup;
    }
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        componentsPerValue = 1;
        break;
    case RBC_VECTOR_COMPLEX:
        componentsPerValue = 2;
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)vPtr->type);
        goto cleanup;
    }
    if (MultiplyVectorSizes(interp, (Tcl_Size)size, componentsPerValue, &bytesPerValue) != TCL_OK) {
        goto cleanup;
    }
    /*
     * With no explicit count, read BUFFER_SIZE values per iteration.
     * With a count, allocate enough space for that one requested read.
     */
    bufferValues = (count == 0) ? (Tcl_Size)BUFFER_SIZE : count;
    if (GetArrayByteCount(interp, bufferValues, (size_t)bytesPerValue, &bufferByteCount) != TCL_OK) {
        goto cleanup;
    }
    /*
     * Tcl_Read() accepts its byte count as Tcl_Size.
     */
    if (bufferByteCount > (size_t)TCL_SIZE_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("binary read size is too large", -1));
        goto cleanup;
    }
    bufferBytes = (Tcl_Size)bufferByteCount;
    byteArr = Tcl_AttemptAlloc(bufferByteCount);
    if ((byteArr == NULL) && (bufferByteCount > 0)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate binary input buffer", -1));
        goto cleanup;
    }
    /*
     * Preserve the channel's original translation setting.
     */
    if (Tcl_GetChannelOption(interp, channel, "-translation", &translation) != TCL_OK) {
        goto cleanup;
    }
    if (Tcl_SetChannelOption(interp, channel, "-translation", "binary") != TCL_OK) {
        goto cleanup;
    }
    translationChanged = TRUE;
    for (;;) {
        if (CheckCommandDestination(vPtr) != TCL_OK) goto cleanup;
        if (!Tcl_IsChannelRegistered(interp, channel)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("channel was closed during command evaluation", -1));
            goto cleanup;
        }
        bytesRead = Tcl_Read(channel, byteArr, bufferBytes);
        if (CheckCommandDestination(vPtr) != TCL_OK) goto cleanup;
        if (!Tcl_IsChannelRegistered(interp, channel)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("channel was closed during command evaluation", -1));
            goto cleanup;
        }
        if (bytesRead < 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("error reading channel: %s", Tcl_PosixError(interp)));
            goto cleanup;
        }
        /*
         * This also prevents an infinite loop on a nonblocking channel
         * that currently has no input available.
         */
        if (bytesRead == 0) {
            break;
        }
        if ((bytesRead % bytesPerValue) != 0) {
            if (vPtr->type == RBC_VECTOR_COMPLEX) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("error reading channel: "
                                                          "input ended inside a complex binary value",
                                                          -1));
            } else {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("error reading channel: "
                                                          "input ended inside a binary value",
                                                          -1));
            }
            goto cleanup;
        }
        length = bytesRead / bytesPerValue;
        if (AddVectorSizes(interp, total, length, &newTotal) != TCL_OK) goto cleanup;
        switch (vPtr->type) {
        case RBC_VECTOR_REAL:
            if (CopyValues(vPtr, byteArr, fmt, size, length, swap, &first) != TCL_OK) {
                goto cleanup;
            }
            break;
        case RBC_VECTOR_COMPLEX:
            if (CopyComplexValues(vPtr, byteArr, fmt, size, length, swap, &first) != TCL_OK) {
                goto cleanup;
            }
            break;
        default:
            Tcl_Panic("bad vector type %d", (int)vPtr->type);
            goto cleanup;
        }
        total = newTotal;
        /*
         * An explicit count requests at most one Tcl_Read(). This
         * preserves the existing behavior for blocking and nonblocking
         * channels.
         */
        if (count > 0) {
            break;
        }
        if (Tcl_Eof(channel)) {
            break;
        }
    }
    result = TCL_OK;

cleanup:
    /* Keep the complete original error across restoration, channel finalizers,
     * cache traces and notifications. Partial successful chunks remain valid. */
    if (result != TCL_OK) savedState = Tcl_SaveInterpState(interp, result);
    if (translationChanged && !Tcl_InterpDeleted(interp) && Tcl_IsChannelRegistered(interp, channel)) {
        if (Tcl_SetChannelOption(interp, channel, "-translation", Tcl_DStringValue(&translation)) != TCL_OK &&
            result == TCL_OK) {
            result = TCL_ERROR;
            savedState = Tcl_SaveInterpState(interp, result);
        }
    }
    if (retained) {
        Tcl_InterpState state = Tcl_SaveInterpState(interp, result);
        Tcl_UnregisterChannel(NULL, channel);
        Tcl_RestoreInterpState(interp, state);
    }
    Tcl_DStringFree(&translation);
    if (byteArr != NULL) ckfree(byteArr);
    if (savedState != NULL) Tcl_RestoreInterpState(interp, savedState);
    if (total > 0 || result == TCL_OK) NotifyCommandWrite(vPtr, -1);
    if (result == TCL_OK) Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)total));
    return result;
}
/*
 * -----------------------------------------------------------------------
 *
 * ClearOp --
 *
 *      Deletes all the accumulated array indices for the Tcl array
 *      associated will the vector.  This routine can be used to
 *      free excess memory from a large vector.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      Always returns TCL_OK.
 *
 * Side Effects:
 *      Memory used for the entries of the Tcl array variable is freed.
 *
 * -----------------------------------------------------------------------
 */
static int ClearOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_VectorFlushCache(vPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * DeleteOp --
 *
 *      Deletes the given indices from the vector.  If no indices are
 *      provided the entire vector is deleted.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If any of the given indices is invalid,
 *      the interpreter result will an error message and TCL_ERROR
 *      is returned.
 *
 * Side Effects:
 *      The clients of the vector will be notified of the vector
 *      deletions.
 *
 * -----------------------------------------------------------------------
 */
static int DeleteOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    unsigned char *unsetArr;
    Tcl_Size i;
    Tcl_Size valueIndex, j;
    Tcl_Size count;
    char *string;
    Tcl_Size bitmapSize;
    size_t bitmapBytes;

    /* FIXME: Don't delete vector with no indices.  */
    if (objc == 2) {
        Rbc_VectorFree(vPtr);
        return TCL_OK;
    }
    /*
     * Allocate an "unset" bitmap the size of the vector.
     */
    /*
     * One bit per vector element.  Avoid "(length + 7) / 8", since
     * the addition can overflow Tcl_Size before the division.
     */
    bitmapSize = vPtr->length / 8;
    if ((vPtr->length & 7) != 0) {
        bitmapSize++;
    }
    if (GetArrayByteCount(interp, bitmapSize, sizeof(*unsetArr), &bitmapBytes) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Keep one byte for the empty-vector case so the existing cleanup
     * path can remain unconditional.
     */
    if (bitmapBytes == 0) {
        bitmapBytes = 1;
    }
    unsetArr = ckalloc(bitmapBytes);
    memset(unsetArr, 0, bitmapBytes);

#define SetBit(i) unsetArr[(i) >> 3] |= (1 << ((i) & 0x07))
#define GetBit(i) (unsetArr[(i) >> 3] & (1 << ((i) & 0x07)))

    for (i = 2; i < objc; i++) {
        string = Tcl_GetString(objv[i]);
        if (Rbc_VectorGetIndexRange(interp, vPtr, string, (INDEX_COLON | INDEX_CHECK), (Rbc_VectorIndexProc **)NULL) !=
            TCL_OK) {
            ckfree((char *)unsetArr);
            return TCL_ERROR;
        }
        for (j = vPtr->first; j <= vPtr->last; j++) {
            SetBit(j); /* Mark the range of elements for deletion. */
        }
    }
    count = 0;
    for (valueIndex = 0; valueIndex < vPtr->length; valueIndex++) {
        if (GetBit(valueIndex)) {
            continue;
        }
        if (count < valueIndex) {
            switch (vPtr->type) {
            case RBC_VECTOR_REAL:
                vPtr->data.real[count] = vPtr->data.real[valueIndex];
                break;
            case RBC_VECTOR_COMPLEX:
                vPtr->data.complex[count] = vPtr->data.complex[valueIndex];
                break;
            default:
                Tcl_Panic("bad vector type %d", (int)vPtr->type);
            }
        }
        count++;
    }
    ckfree((char *)unsetArr);
    vPtr->length = count;
    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }
    Rbc_VectorUpdateClients(vPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * DupOp --
 *
 *      Creates one or more duplicates of the vector object.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If a new vector can't be created,
 *      or and existing vector resized, TCL_ERROR is returned.
 *
 * Side Effects:
 *      Clients of existing vectors will be notified of the change.
 *
 * -----------------------------------------------------------------------
 */
static int DupOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject *v2Ptr;
    int isNew;
    Tcl_Size i;
    char *string;

    for (i = 2; i < objc; i++) {
        string = Tcl_GetString(objv[i]);
        v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, string, string,
                                 (vPtr->type == RBC_VECTOR_REAL && strchr(string, '(') == NULL) ? string : NULL,
                                 vPtr->type, &isNew);
        if (v2Ptr == NULL) {
            return TCL_ERROR;
        }
        if (v2Ptr == vPtr) {
            continue;
        }
        if (Rbc_VectorDuplicate(v2Ptr, vPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (!isNew) {
            if (v2Ptr->flush) {
                Rbc_VectorFlushCache(v2Ptr);
            }
            Rbc_VectorUpdateClients(v2Ptr);
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ExprOp --
 *
 *      Computes the result of the expression which may be
 *      either a scalar (single value) or vector (list of values).
 *
 * Parameters:
 *      VectorObject *vPtr
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
static int ExprOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (Rbc_ExprVector(interp, Tcl_GetString(objv[2]), (Rbc_Vector *)vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }
    Rbc_VectorUpdateClients(vPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * IndexOp --
 *
 *      Sets or reads the value of the index.  This simulates what the
 *      vector's variable does.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If the index is invalid, the interpreter
 *      result will an error message and TCL_ERROR is returned.
 *      Otherwise, the interpreter result will contain the values.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int IndexOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size first, last;
    Tcl_Size oldLength;
    int rangedRealWrite;
    int appendWrite;
    char *string;

    oldLength = vPtr->length;
    rangedRealWrite = FALSE;
    appendWrite = FALSE;
    string = Tcl_GetString(objv[2]);
    if (Rbc_VectorGetIndexRange(interp, vPtr, string, INDEX_ALL_FLAGS, (Rbc_VectorIndexProc **)NULL) != TCL_OK) {
        return TCL_ERROR;
    }
    first = vPtr->first, last = vPtr->last;
    if ((first == oldLength) && (last == oldLength)) {
        appendWrite = TRUE;
    }
    if (objc == 3) {
        Tcl_Obj *listObjPtr;

        if (first == vPtr->length || last == vPtr->length) {
            Rbc_AppendResultStrings(interp, "can't get index \"", string, "\"", (char *)NULL);
            return TCL_ERROR; /* Can't read from index "++end" */
        }
        listObjPtr = Rbc_GetValues(vPtr, first, last);
        Tcl_SetObjResult(interp, listObjPtr);
    } else {
        if (first == SPECIAL_INDEX) {
            Rbc_AppendResultStrings(interp, "can't set index \"", string, "\"", (char *)NULL);
            return TCL_ERROR;
        }
        if (vPtr->type == RBC_VECTOR_COMPLEX) {
            Rbc_Complex value;
            Tcl_Size i;

            if (Rbc_GetComplex(interp, objv[3], &value) != TCL_OK) {
                return TCL_ERROR;
            }
            if (Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr) != TCL_OK) {
                return TCL_ERROR;
            }
            if (first == vPtr->length) {
                Tcl_Size newSize;

                if (AddVectorSizes(interp, vPtr->length, 1, &newSize) != TCL_OK) {
                    return TCL_ERROR;
                }
                if (Rbc_VectorChangeLength(vPtr, newSize) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
            for (i = first; i <= last; i++) {
                vPtr->data.complex[i] = value;
            }
        } else {
            double value;

            if (Rbc_GetDouble(interp, objv[3], &value) != TCL_OK) {
                return TCL_ERROR;
            }
            if (Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr) != TCL_OK) {
                return TCL_ERROR;
            }
            /*
             * An ordinary write to existing real-vector storage can carry an
             * exact changed source range and can usually preserve cached
             * min/max incrementally.
             */
            if ((first >= 0) && (last >= first) && (last < oldLength)) {
                rangedRealWrite = TRUE;
            }
            if (first == vPtr->length) {
                Tcl_Size newSize;

                if (AddVectorSizes(interp, vPtr->length, 1, &newSize) != TCL_OK) {
                    return TCL_ERROR;
                }
                if (Rbc_VectorChangeLength(vPtr, newSize) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
            Rbc_ReplicateValue(vPtr, first, last, value);
        }
        Tcl_SetObjResult(interp, objv[3]);
        if (vPtr->flush) {
            Rbc_VectorFlushCache(vPtr);
        }
        if (rangedRealWrite || appendWrite) {
            Rbc_VectorUpdateClientsRange(vPtr, first, last);
        } else {
            Rbc_VectorUpdateClients(vPtr);
        }
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * LengthOp --
 *
 *      Returns the length of the vector.  If a new size is given, the
 *      vector is resized to the new vector.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If the new length is invalid, the
 *      interpreter result will contain an error message and TCL_ERROR
 *      is returned.
 *      Otherwise, the interpreter result will contain the length
 *      of the vector.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int LengthOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        Tcl_Size size;
        if (Tcl_GetSizeIntFromObj(interp, objv[2], &size) != TCL_OK) {
            return TCL_ERROR;
        }
        if (size < 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad vector size \"%s\"", Tcl_GetString(objv[2])));
            return TCL_ERROR;
        }
        if (Rbc_VectorChangeLength(vPtr, size) != TCL_OK) {
            return TCL_ERROR;
        }
        if (vPtr->flush) {
            Rbc_VectorFlushCache(vPtr);
        }
        Rbc_VectorUpdateClients(vPtr);
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)vPtr->length));
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * MergeOp --
 *
 *      Merges the values from the given vectors to the current vector.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If any of the given vectors differ in size,
 *      TCL_ERROR is returned.  Otherwise TCL_OK is returned and the
 *      vector data will contain merged values of the given vectors.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int MergeOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject **vecArr;
    Tcl_Size *firstArr;
    VectorData valueData;
    Tcl_Size nVectors;
    Tcl_Size refSize;
    Tcl_Size nElem;
    Tcl_Size i;
    Tcl_Size j;
    Tcl_Size valueIndex;
    Tcl_Size outIndex;
    Tcl_Size length;
    size_t vectorBytes;
    size_t firstBytes;
    size_t valueBytes;
    size_t elementSize;
    int result;

    vecArr = NULL;
    firstArr = NULL;
    valueData.raw = NULL;
    result = TCL_ERROR;
    nVectors = objc - 2;
    /*
     * "merge" requires at least one source vector according to the
     * operation table, so nVectors should always be positive here.
     */
    if (GetArrayByteCount(interp, nVectors, sizeof(*vecArr), &vectorBytes) != TCL_OK) {
        goto cleanup;
    }
    if (GetArrayByteCount(interp, nVectors, sizeof(*firstArr), &firstBytes) != TCL_OK) {
        goto cleanup;
    }
    vecArr = ckalloc(vectorBytes);
    firstArr = ckalloc(firstBytes);
    refSize = -1;
    /*
     * Resolve and validate every source before modifying the
     * destination.
     */
    for (i = 0; i < nVectors; i++) {
        VectorObject *srcPtr;

        if (Rbc_VectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i + 2]), &srcPtr) != TCL_OK) {
            goto cleanup;
        }
        if (srcPtr->type != vPtr->type) {
            Tcl_SetObjResult(interp,
                             Tcl_ObjPrintf("vectors \"%s\" and \"%s\" have different types", vPtr->name, srcPtr->name));
            goto cleanup;
        }
        length = srcPtr->last - srcPtr->first + 1;
        if (refSize < 0) {
            refSize = length;
        } else if (length != refSize) {
            Rbc_AppendResultStrings(interp, "vectors \"", vPtr->name, "\" and \"", srcPtr->name, "\" differ in length",
                                    (char *)NULL);
            goto cleanup;
        }
        vecArr[i] = srcPtr;
        /*
         * Preserve the selected source range. This matters if the
         * destination is itself one of the source vectors.
         */
        firstArr[i] = srcPtr->first;
    }
    if (MultiplyVectorSizes(interp, refSize, nVectors, &nElem) != TCL_OK) {
        goto cleanup;
    }
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        elementSize = sizeof(double);
        break;
    case RBC_VECTOR_COMPLEX:
        elementSize = sizeof(Rbc_Complex);
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)vPtr->type);
        goto cleanup;
    }
    if (GetArrayByteCount(interp, nElem, elementSize, &valueBytes) != TCL_OK) {
        goto cleanup;
    }
    if (valueBytes > 0) {
        valueData.raw = ckalloc(valueBytes);
    }
    /*
     * Build the complete result before changing vPtr. This makes
     * "v merge v other" safe.
     */
    outIndex = 0;
    for (valueIndex = 0; valueIndex < refSize; valueIndex++) {
        for (j = 0; j < nVectors; j++) {
            Tcl_Size sourceIndex;

            sourceIndex = firstArr[j] + valueIndex;
            switch (vPtr->type) {
            case RBC_VECTOR_REAL:
                valueData.real[outIndex] = vecArr[j]->data.real[sourceIndex];
                break;
            case RBC_VECTOR_COMPLEX:
                valueData.complex[outIndex] = vecArr[j]->data.complex[sourceIndex];
                break;
            default:
                Tcl_Panic("bad vector type %d", (int)vPtr->type);
            }
            outIndex++;
        }
    }
    /*
     * Only now modify the destination.
     */
    if (Rbc_VectorChangeLength(vPtr, nElem) != TCL_OK) {
        goto cleanup;
    }
    if (valueBytes > 0) {
        memcpy(vPtr->data.raw, valueData.raw, valueBytes);
    }
    vPtr->notifyFlags |= UPDATE_RANGE;
    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }
    Rbc_VectorUpdateClients(vPtr);
    result = TCL_OK;

cleanup:
    if (valueData.raw != NULL) {
        ckfree(valueData.raw);
    }
    if (firstArr != NULL) {
        ckfree(firstArr);
    }
    if (vecArr != NULL) {
        ckfree(vecArr);
    }
    return result;
}

/*
 * -----------------------------------------------------------------------
 *
 * NormalizeOp --
 *
 *      Normalizes the vector.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If the density is invalid, TCL_ERROR
 *      is returned.  Otherwise TCL_OK is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int NormalizeOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    double range;

    Rbc_VectorUpdateRange(vPtr);
    range = vPtr->max - vPtr->min;
    if (objc > 2) {
        VectorObject *v2Ptr;
        int isNew;
        char *string;

        string = Tcl_GetString(objv[2]);
        v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, string, string, (strchr(string, '(') == NULL) ? string : NULL,
                                 RBC_VECTOR_REAL, &isNew);
        if (v2Ptr == NULL) {
            return TCL_ERROR;
        }
        if (Rbc_VectorChangeLength(v2Ptr, vPtr->length) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < vPtr->length; i++) {
            v2Ptr->data.real[i] = (vPtr->data.real[i] - vPtr->min) / range;
        }
        Rbc_VectorUpdateRange(v2Ptr);
        if (!isNew) {
            if (v2Ptr->flush) {
                Rbc_VectorFlushCache(v2Ptr);
            }
            Rbc_VectorUpdateClients(v2Ptr);
        }
    } else {
        double norm;
        Tcl_Obj *listObjPtr;

        listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **)NULL);
        for (i = 0; i < vPtr->length; i++) {
            norm = (vPtr->data.real[i] - vPtr->min) / range;
            Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(norm));
        }
        Tcl_SetObjResult(interp, listObjPtr);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * OffsetOp --
 *
 *      Queries or sets the offset of the array index from the base
 *      address of the data array of values.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If the source vector doesn't exist
 *      or the source list is not a valid list of numbers, TCL_ERROR
 *      returned.  Otherwise TCL_OK is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int OffsetOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        Tcl_Size value;
        if (Tcl_GetSizeIntFromObj(interp, objv[2], &value) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->offset = value;
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)vPtr->offset));
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * PopulateOp --
 *
 *      Creates or resizes a new vector based upon the density specified.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If the density is invalid, TCL_ERROR
 *      is returned.  Otherwise TCL_OK is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int PopulateOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject *v2Ptr;
    VectorObject *sourcePtr;
    VectorObject *tmpPtr;
    Tcl_Size density;
    Tcl_Size intervals;
    Tcl_Size valuesPerInterval;
    Tcl_Size populatedValues;
    Tcl_Size newSize;
    Tcl_Size i;
    Tcl_Size j;
    const char *name;
    int isNew;

    (void)objc;
    /*
     * Parse and validate the number of values inserted between each
     * pair of source values.
     */
    if (Tcl_GetSizeIntFromObj(interp, objv[3], &density) != TCL_OK) {
        return TCL_ERROR;
    }
    if (density < 1) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad density \"%s\"", Tcl_GetString(objv[3])));
        return TCL_ERROR;
    }
    /*
     * Calculate:
     *
     *     newSize =
     *         (sourceLength - 1) * (density + 1) + 1
     *
     * Check each operation before performing it, because Tcl_Size is
     * signed and overflowing it would be undefined behaviour.
     */
    if (vPtr->length == 0) {
        newSize = 0;
    } else {
        intervals = vPtr->length - 1;
        if (density == TCL_SIZE_MAX) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("density is too large", -1));
            return TCL_ERROR;
        }
        valuesPerInterval = density + 1;
        if (MultiplyVectorSizes(interp, intervals, valuesPerInterval, &populatedValues) != TCL_OK) {
            return TCL_ERROR;
        }
        if (AddVectorSizes(interp, populatedValues, 1, &newSize) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    name = Tcl_GetString(objv[2]);
    v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, name, name,
                             (vPtr->type == RBC_VECTOR_REAL && strchr(name, '(') == NULL) ? name : NULL, vPtr->type,
                             &isNew);
    if (v2Ptr == NULL) {
        return TCL_ERROR;
    }
    /*
     * The source and destination may be the same vector. Preserve the
     * source values before resizing the destination.
     *
     * This is the same temporary-vector technique already used by SetOp().
     */
    sourcePtr = vPtr;
    tmpPtr = NULL;
    if (v2Ptr == vPtr) {
        tmpPtr = Rbc_VectorNew(vPtr->dataPtr);
        if (tmpPtr == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate temporary vector", -1));
            return TCL_ERROR;
        }
        tmpPtr->type = vPtr->type;
        if (Rbc_VectorDuplicate(tmpPtr, vPtr) != TCL_OK) {
            Rbc_VectorFree(tmpPtr);
            return TCL_ERROR;
        }
        sourcePtr = tmpPtr;
    }
    if (Rbc_VectorChangeLength(v2Ptr, newSize) != TCL_OK) {
        if (tmpPtr != NULL) {
            Rbc_VectorFree(tmpPtr);
        }
        return TCL_ERROR;
    }
    if (sourcePtr->length > 0) {
        valuesPerInterval = density + 1;
        switch (sourcePtr->type) {
        case RBC_VECTOR_REAL: {
            double *valuePtr;
            double range;
            double slice;

            valuePtr = v2Ptr->data.real;
            for (i = 0; i < (sourcePtr->length - 1); i++) {
                range = sourcePtr->data.real[i + 1] - sourcePtr->data.real[i];
                slice = range / (double)valuesPerInterval;
                /*
                 * Write the interval's starting value followed by the
                 * requested intermediate values. The ending value is
                 * written as the start of the next interval.
                 */
                for (j = 0; j < valuesPerInterval; j++) {
                    *valuePtr++ = sourcePtr->data.real[i] + (slice * (double)j);
                }
            }
            /*
             * The final source value is not written by the interval loop.
             */
            *valuePtr++ = sourcePtr->data.real[sourcePtr->length - 1];
            assert(valuePtr == (v2Ptr->data.real + v2Ptr->length));
            break;
        }
        case RBC_VECTOR_COMPLEX: {
            Rbc_Complex *valuePtr;
            double realRange, imagRange;
            double realSlice, imagSlice;

            valuePtr = v2Ptr->data.complex;
            for (i = 0; i < (sourcePtr->length - 1); i++) {
                realRange = sourcePtr->data.complex[i + 1].real - sourcePtr->data.complex[i].real;
                imagRange = sourcePtr->data.complex[i + 1].imag - sourcePtr->data.complex[i].imag;
                realSlice = realRange / (double)valuesPerInterval;
                imagSlice = imagRange / (double)valuesPerInterval;
                for (j = 0; j < valuesPerInterval; j++) {
                    valuePtr->real = sourcePtr->data.complex[i].real + (realSlice * (double)j);
                    valuePtr->imag = sourcePtr->data.complex[i].imag + (imagSlice * (double)j);
                    valuePtr++;
                }
            }
            *valuePtr++ = sourcePtr->data.complex[sourcePtr->length - 1];
            assert(valuePtr == (v2Ptr->data.complex + v2Ptr->length));
            break;
        }
        default:
            Tcl_Panic("bad vector type %d", (int)sourcePtr->type);
            break;
        }
    }
    if (tmpPtr != NULL) {
        Rbc_VectorFree(tmpPtr);
    }
    /*
     * New vectors have no existing clients to notify. Existing vectors,
     * including an in-place population, must have their caches and clients
     * updated.
     */
    if (!isNew) {
        if (v2Ptr->flush) {
            Rbc_VectorFlushCache(v2Ptr);
        }
        Rbc_VectorUpdateClients(v2Ptr);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RandomOp --
 *
 *      Generates random values for the length of the vector.
 *
 * Parameters:
 *      VectorObject *vPtr
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
 * -----------------------------------------------------------------------
 */
static int RandomOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;

    (void)interp;
    (void)objc;
    (void)objv;
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        for (i = 0; i < vPtr->length; i++) {
            vPtr->data.real[i] = Rbc_RandomDouble();
        }
        break;
    case RBC_VECTOR_COMPLEX:
        for (i = 0; i < vPtr->length; i++) {
            vPtr->data.complex[i].real = Rbc_RandomDouble();
            vPtr->data.complex[i].imag = Rbc_RandomDouble();
        }
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)vPtr->type);
        return TCL_ERROR;
    }
    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }
    Rbc_VectorUpdateClients(vPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * RangeOp --
 *
 *      Returns a Tcl list of the range of vector values specified.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If the given range is invalid, TCL_ERROR
 *      is returned.  Otherwise TCL_OK is returned and the interpreter result
 *      will contain the list of values.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int RangeOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *listObjPtr;
    Tcl_Size first, last;
    Tcl_Size i;

    if ((Rbc_VectorGetIndex(interp, vPtr, Tcl_GetString(objv[2]), &first, INDEX_CHECK, (Rbc_VectorIndexProc **)NULL) !=
         TCL_OK) ||
        (Rbc_VectorGetIndex(interp, vPtr, Tcl_GetString(objv[3]), &last, INDEX_CHECK, (Rbc_VectorIndexProc **)NULL) !=
         TCL_OK)) {
        return TCL_ERROR;
    }
    listObjPtr = Tcl_NewListObj(0, NULL);
    if (first > last) {
        for (i = first; i >= last; i--) {
            Tcl_ListObjAppendElement(interp, listObjPtr, Rbc_NewVectorValueObj(vPtr, i));
        }
    } else {
        for (i = first; i <= last; i++) {
            Tcl_ListObjAppendElement(interp, listObjPtr, Rbc_NewVectorValueObj(vPtr, i));
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * SearchOp --
 *
 *      Searchs for a value in the vector. Returns the indices of all
 *      vector elements matching a particular value.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      Always returns TCL_OK. The interpreter result will contain a list of
 *      the indices of array elements matching value. If no elements
 *      match, the interpreter result will contain the empty string.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int ComplexSearchOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Complex target;
    Tcl_Obj *listObjPtr;
    Tcl_Size i;
    int wantValue;
    const char *string;

    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    wantValue = FALSE;
    string = Tcl_GetString(objv[2]);
    if ((string[0] == '-') && (strcmp(string, "-value") == 0)) {
        wantValue = TRUE;
        objv++;
        objc--;
    }
    /*
     * Complex numbers have no natural interval ordering.  Support
     * searching for one complex value, but keep the two-bound range
     * form real-only.
     */
    if (objc > 3) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("range search is not supported for complex vectors", -1));
        return TCL_ERROR;
    }
    if (Rbc_GetComplex(interp, objv[2], &target) != TCL_OK) {
        return TCL_ERROR;
    }
    listObjPtr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(listObjPtr);
    for (i = 0; i < vPtr->length; i++) {
        Rbc_Complex value;

        value = vPtr->data.complex[i];
        /*
         * Match the tolerance used by the existing real search for a
         * single value.  InRange(value, target, target) compares within
         * DBL_EPSILON.
         */
        if (InRange(value.real, target.real, target.real) && InRange(value.imag, target.imag, target.imag)) {
            Tcl_Obj *objPtr;

            if (wantValue) {
                objPtr = Rbc_NewVectorValueObj(vPtr, i);
            } else {
                Tcl_Size index;
                if ((vPtr->offset > 0) && (i > (TCL_SIZE_MAX - vPtr->offset))) {
                    Tcl_DecrRefCount(listObjPtr);
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("vector index is too large", -1));
                    return TCL_ERROR;
                }
                index = i + vPtr->offset;
                objPtr = Tcl_NewWideIntObj((Tcl_WideInt)index);
            }
            Tcl_ListObjAppendElement(interp, listObjPtr, objPtr);
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    Tcl_DecrRefCount(listObjPtr);
    return TCL_OK;
}

static int SearchOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    double min, max;
    Tcl_Size i;
    int wantValue;
    char *string;
    Tcl_Obj *listObjPtr;

    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        return ComplexSearchOp(vPtr, interp, objc, objv);
    }
    assert(vPtr->type == RBC_VECTOR_REAL);
    wantValue = FALSE;
    string = Tcl_GetString(objv[2]);
    if ((string[0] == '-') && (strcmp(string, "-value") == 0)) {
        wantValue = TRUE;
        objv++, objc--;
    }
    if (Rbc_GetDouble(interp, objv[2], &min) != TCL_OK) {
        return TCL_ERROR;
    }
    max = min;
    if ((objc > 3) && (Rbc_GetDouble(interp, objv[3], &max) != TCL_OK)) {
        return TCL_ERROR;
    }
    if ((min - max) >= DBL_EPSILON) {
        return TCL_OK; /* Bogus range. Don't bother looking. */
    }
    listObjPtr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(listObjPtr);
    for (i = 0; i < vPtr->length; i++) {
        if (InRange(vPtr->data.real[i], min, max)) {
            Tcl_Obj *objPtr;

            if (wantValue) {
                objPtr = Tcl_NewDoubleObj(vPtr->data.real[i]);
            } else {
                Tcl_Size index;
                if ((vPtr->offset > 0) && (i > (TCL_SIZE_MAX - vPtr->offset))) {
                    Tcl_DecrRefCount(listObjPtr);
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("vector index is too large", -1));
                    return TCL_ERROR;
                }
                index = i + vPtr->offset;
                objPtr = Tcl_NewWideIntObj((Tcl_WideInt)index);
            }
            Tcl_ListObjAppendElement(interp, listObjPtr, objPtr);
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    Tcl_DecrRefCount(listObjPtr);
    return TCL_OK;
}

static int GetComplexSequenceComponentLength(Tcl_Interp *interp, const char *componentName, double start, double finish,
                                             double step, int *hasLengthPtr, Tcl_Size *lengthPtr) {
    double span;
    double steps;

    *hasLengthPtr = FALSE;
    *lengthPtr = 0;
    if ((!FINITE(start)) || (!FINITE(finish)) || (!FINITE(step))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence bounds and step must be finite", -1));
        return TCL_ERROR;
    }
    /*
     * A zero step is valid for one component when that component is
     * constant.  It then places no constraint on the sequence length;
     * the other component determines it.
     */
    if (step == 0.0) {
        if (start != finish) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s component of complex sequence has zero step "
                                                   "but different bounds",
                                                   componentName));
            return TCL_ERROR;
        }
        return TCL_OK;
    }
    /*
     * Use the same length calculation as the existing real SeqOp:
     *
     *     trunc((finish - start) / step) + 1
     */
    span = (finish - start) / step;
    if (!FINITE(span)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence size is too large", -1));
        return TCL_ERROR;
    }
    steps = trunc(span) + 1.0;
    *hasLengthPtr = TRUE;
    if (steps <= 0.0) {
        *lengthPtr = 0;
        return TCL_OK;
    }
    if (steps >= (double)TCL_SIZE_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence size is too large", -1));
        return TCL_ERROR;
    }
    *lengthPtr = (Tcl_Size)steps;
    return TCL_OK;
}

static int ComplexSeqOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Complex start;
    Rbc_Complex finish;
    Rbc_Complex step;
    Tcl_Size realSteps;
    Tcl_Size imagSteps;
    Tcl_Size nSteps;
    Tcl_Size i;
    int realHasLength;
    int imagHasLength;
    int fillVector;
    const char *string;

    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    if (Rbc_GetComplex(interp, objv[2], &start) != TCL_OK) {
        return TCL_ERROR;
    }
    fillVector = FALSE;
    string = Tcl_GetString(objv[3]);
    if ((string[0] == 'e') && (strcmp(string, "end") == 0)) {
        fillVector = TRUE;
    } else {
        if (Rbc_GetComplex(interp, objv[3], &finish) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /*
     * Preserve the legacy default step of 1.  For a complex vector
     * this means 1+0i.
     */
    step.real = 1.0;
    step.imag = 0.0;
    if (objc == 5) {
        if (Rbc_GetComplex(interp, objv[4], &step) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (fillVector) {
        /*
         * "end" means that the vector's existing length determines
         * the number of values.  Either component of step may therefore
         * be zero, including both components.
         */
        nSteps = vPtr->length;
    } else {
        realSteps = 0;
        imagSteps = 0;
        if (GetComplexSequenceComponentLength(interp, "real", start.real, finish.real, step.real, &realHasLength,
                                              &realSteps) != TCL_OK) {
            return TCL_ERROR;
        }
        if (GetComplexSequenceComponentLength(interp, "imaginary", start.imag, finish.imag, step.imag, &imagHasLength,
                                              &imagSteps) != TCL_OK) {
            return TCL_ERROR;
        }
        /*
         * If neither component changes, a zero complex step gives no
         * way to determine the desired sequence length.
         */
        if ((!realHasLength) && (!imagHasLength)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence step cannot be zero", -1));
            return TCL_ERROR;
        }
        if (realHasLength && imagHasLength) {
            if (realSteps != imagSteps) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("real and imaginary sequences have different lengths", -1));
                return TCL_ERROR;
            }
            nSteps = realSteps;
        } else if (realHasLength) {
            nSteps = realSteps;
        } else {
            nSteps = imagSteps;
        }
    }
    /*
     * Match the existing real behavior: a sequence whose calculated
     * length is non-positive leaves the vector unchanged.
     */
    if (nSteps > 0) {
        if (Rbc_VectorChangeLength(vPtr, nSteps) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < nSteps; i++) {
            vPtr->data.complex[i].real = start.real + step.real * (double)i;
            vPtr->data.complex[i].imag = start.imag + step.imag * (double)i;
        }
        if (vPtr->flush) {
            Rbc_VectorFlushCache(vPtr);
        }
        Rbc_VectorUpdateClients(vPtr);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * SeqOp --
 *
 *      Generates a sequence of values in the vector.
 *
 * Parameters:
 *      VectorObject *vPtr
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
 * -----------------------------------------------------------------------
 */
static int SeqOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    Tcl_Size nSteps;
    double start;
    double finish;
    double step;
    double span;
    double steps;
    int fillVector;
    const char *string;

    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        return ComplexSeqOp(vPtr, interp, objc, objv);
    }
    assert(vPtr->type == RBC_VECTOR_REAL);
    if (Rbc_GetDouble(interp, objv[2], &start) != TCL_OK) {
        return TCL_ERROR;
    }
    fillVector = FALSE;
    string = Tcl_GetString(objv[3]);
    if ((string[0] == 'e') && (strcmp(string, "end") == 0)) {
        fillVector = TRUE;
    } else if (Rbc_GetDouble(interp, objv[3], &finish) != TCL_OK) {
        return TCL_ERROR;
    }
    step = 1.0;
    if ((objc == 5) && (Rbc_GetDouble(interp, objv[4], &step) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (fillVector) {
        /*
         * "end" means fill the vector's existing length.  There is
         * no floating-point-to-size conversion in this case.
         */
        nSteps = vPtr->length;
    } else {
        /*
         * These values determine the resulting allocation size, so
         * they must yield a finite and representable count.
         */
        if ((!FINITE(start)) || (!FINITE(finish)) || (!FINITE(step))) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence bounds and step must be finite", -1));
            return TCL_ERROR;
        }
        if (step == 0.0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence step cannot be zero", -1));
            return TCL_ERROR;
        }
        span = (finish - start) / step;
        if (!FINITE(span)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence size is too large", -1));
            return TCL_ERROR;
        }
        /*
         * Preserve the old C cast semantics: the old `(int)span`
         * truncated toward zero.
         */
        steps = trunc(span) + 1.0;
        if (steps <= 0.0) {
            return TCL_OK;
        }
        /*
         * (double)TCL_SIZE_MAX rounds upward on a 64-bit Tcl_Size,
         * which makes this a conservative boundary before the cast.
         */
        if (steps >= (double)TCL_SIZE_MAX) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence size is too large", -1));
            return TCL_ERROR;
        }
        nSteps = (Tcl_Size)steps;
    }
    if (nSteps > 0) {
        if (Rbc_VectorChangeLength(vPtr, nSteps) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < nSteps; i++) {
            vPtr->data.real[i] = start + step * (double)i;
        }
        if (vPtr->flush) {
            Rbc_VectorFlushCache(vPtr);
        }
        Rbc_VectorUpdateClients(vPtr);
    }
    return TCL_OK;
}

/**
 * @brief Replace values from a vector range or Tcl list.
 * @ingroup vector_command_paths
 *
 * Stages vector ranges (including self-copy) or converted list values before
 * replacing storage. Failed conversion does not write destination data. Tcl
 * callbacks retain their own side effects. Flushes/notifies only a still-live
 * committed object; callbacks cannot redirect the operation by recreating a name.
 */
static int SetOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject *srcPtr;
    int result;

    (void)objc;
    srcPtr = Rbc_VectorParseElement(NULL, vPtr->dataPtr, Tcl_GetString(objv[2]), NULL, NS_SEARCH_BOTH);
    if (CheckCommandDestination(vPtr) != TCL_OK) return TCL_ERROR;
    if (srcPtr != NULL) {
        VectorData data;
        Tcl_Size length = srcPtr->last - srcPtr->first + 1;
        size_t bytes;
        size_t elementSize = (vPtr->type == RBC_VECTOR_REAL) ? sizeof(double) : sizeof(Rbc_Complex);
        if (srcPtr->type != vPtr->type) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't duplicate vectors of different types", -1));
            return TCL_ERROR;
        }
        if (GetArrayByteCount(interp, length, elementSize, &bytes) != TCL_OK) return TCL_ERROR;
        data.raw = (bytes > 0) ? Tcl_AttemptAlloc(bytes) : NULL;
        if (bytes > 0 && data.raw == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate vector command storage", -1));
            return TCL_ERROR;
        }
        if (bytes > 0) memcpy(data.raw, (char *)srcPtr->data.raw + srcPtr->first * elementSize, bytes);
        vPtr->offset = srcPtr->offset;
        FinishCommandWrite(vPtr, data, length, length);
        result = TCL_OK;
    } else {
        Tcl_Size count;
        Tcl_Obj **values;
        result = Tcl_ListObjGetElements(interp, objv[2], &count, &values);
        if (result == TCL_OK) result = CopyList(vPtr, count, values);
    }
    if (result == TCL_OK) NotifyCommandWrite(vPtr, -1);
    return result;
}

/**
 * @brief Sort real values and apply their permutation to companion vectors.
 * @ingroup vector_command_paths
 *
 * Retains resolved destination identities and validates all keys before any
 * write. Reuses one scratch array to apply the permutation without callbacks;
 * only then flushes and notifies surviving objects. Notifications may alter or
 * destroy committed vectors, but never expose a partially applied permutation.
 */
static int SortOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject **vectors;
    Tcl_Size vectorCount, j, retained = 0;
    size_t vectorBytes;
    char *string;
    double *mergeArr;
    Tcl_Size *iArr;
    Tcl_Size refSize;
    size_t nBytes;
    int result;
    int reverse;
    Tcl_Size i;
    Tcl_Size n;

    reverse = FALSE;
    if (objc > 2) {
        Tcl_Size length;
        string = Tcl_GetStringFromObj(objv[2], &length);
        if (string[0] == '-') {
            if ((length > 1) && (strncmp(string, "-reverse", length) == 0)) {
                reverse = TRUE;
            } else {
                Rbc_AppendResultStrings(interp, "unknown flag \"", string, "\": should be \"-reverse\"", (char *)NULL);
                return TCL_ERROR;
            }
            objc--;
            objv++;
        }
    }
    vectorCount = objc - 1;
    if (GetArrayByteCount(interp, vectorCount, sizeof(*vectors), &vectorBytes) != TCL_OK) return TCL_ERROR;
    vectors = ckalloc(vectorBytes);
    vectors[0] = vPtr;
    Tcl_Preserve(vPtr);
    retained = 1;
    result = TCL_ERROR;
    for (i = 1; i < vectorCount; i++) {
        if (Rbc_VectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i + 1]), &vectors[i]) != TCL_OK) goto lookupError;
        Tcl_Preserve(vectors[i]);
        retained++;
        if (CheckCommandDestination(vPtr) != TCL_OK ||
            CheckCommandDestination(vectors[i]) != TCL_OK) goto lookupError;
        if (vectors[i]->length != vPtr->length) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" is not the same size as \"%s\"",
                                                  vectors[i]->name, vPtr->name));
            goto lookupError;
        }
        for (j = 0; j < i; j++) {
            if (vectors[j] == vectors[i]) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" specified more than once", vectors[i]->name));
                goto lookupError;
            }
        }
    }
    for (i = 0; i < vectorCount; i++) {
        if (CheckCommandDestination(vectors[i]) != TCL_OK) goto lookupError;
        if (vectors[i]->length != vPtr->length) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector lengths changed during sort evaluation", -1));
            goto lookupError;
        }
    }
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;
    iArr = Rbc_VectorSortIndex(vectors, vectorCount);
    if (iArr == NULL) goto lookupError;
    refSize = vPtr->length;
    /*
     * Rbc_VectorSortIndex always returns ascending order.  Reverse the
     * permutation locally for the instance command's -reverse option.
     * This keeps reverse ordering out of the shared sort implementation.
     */
    if ((reverse) && (refSize > 1)) {
        Tcl_Size left;
        Tcl_Size right;

        left = 0;
        right = refSize - 1;
        while (left < right) {
            Tcl_Size tmp;

            tmp = iArr[left];
            iArr[left] = iArr[right];
            iArr[right] = tmp;
            left++;
            right--;
        }
    }
    /*
     * Create an array to store a copy of the current values of the
     * vector. We'll merge the values back into the vector based upon
     * the indices found in the index array.
     */
    if (GetArrayByteCount(interp, refSize, sizeof(*mergeArr), &nBytes) != TCL_OK) {
        ckfree(iArr);
        goto lookupError;
    }
    mergeArr = ckalloc(nBytes);
    /* No callback-capable operations between validation and the last write.
     * Reuse one scratch array rather than staging a full copy per destination. */
    for (i = 0; i < vectorCount; i++) {
        if (nBytes > 0) memcpy(mergeArr, vectors[i]->data.raw, nBytes);
        for (n = 0; n < refSize; n++) vectors[i]->data.real[n] = mergeArr[iArr[n]];
        vectors[i]->notifyFlags |= UPDATE_RANGE;
        vectors[i]->minIndex = vectors[i]->maxIndex = -1;
    }
    ckfree(mergeArr);
    ckfree(iArr);
    /* All data is final before any cache trace or observer can inspect it.
     * Retained identities survive renames; deleted/recreated names are skipped. */
    for (i = 0; i < vectorCount; i++) NotifyCommandWrite(vectors[i], -1);
    result = TCL_OK;
lookupError:
    for (i = 0; i < retained; i++) Tcl_Release(vectors[i]);
    ckfree(vectors);
    return result;
}
/*
 * -----------------------------------------------------------------------
 *
 * SplitOp --
 *
 *      Copies the values from the vector evens into one of more
 *      vectors.
 *
 * Parameters:
 *      VectorObject *vPtr
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
 * -----------------------------------------------------------------------
 */
static int SplitOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size nVectorArgs;
    Tcl_Size argIndex;
    VectorObject *v2Ptr;
    VectorObject *sourcePtr;
    VectorObject *tmpPtr;
    const char *string;
    Tcl_Size nVectors;
    Tcl_Size sourceIndex;
    Tcl_Size destIndex;
    Tcl_Size oldSize;
    Tcl_Size newSize;
    Tcl_Size extra;
    int isNew;

    nVectorArgs = objc - 2;
    if (nVectorArgs == 0) {
        return TCL_OK;
    }
    nVectors = nVectorArgs;
    if ((vPtr->length % nVectors) != 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't split vector \"%s\" into "
                                               "%" TCL_SIZE_MODIFIER "d even parts.",
                                               vPtr->name, nVectors));
        return TCL_ERROR;
    }
    for (argIndex = 0; argIndex < nVectorArgs; argIndex++) {
        v2Ptr = Rbc_VectorParseElement(NULL, vPtr->dataPtr, Tcl_GetString(objv[argIndex + 2]),
                                      NULL, NS_SEARCH_BOTH);
        if ((v2Ptr != NULL) && (Rbc_VectorCheckWritable(interp, (Rbc_Vector *)v2Ptr) != TCL_OK)) {
            return TCL_ERROR;
        }
    }
    extra = vPtr->length / nVectors;
    sourcePtr = vPtr;
    tmpPtr = NULL;
    for (argIndex = 0; argIndex < nVectorArgs; argIndex++) {
        string = Tcl_GetString(objv[argIndex + 2]);
        v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, string, string,
                                 (vPtr->type == RBC_VECTOR_REAL && strchr(string, '(') == NULL) ? string : NULL,
                                 vPtr->type, &isNew);
        if (v2Ptr == NULL) {
            goto error;
        }
        /*
         * The source may also be one of the destinations.  Preserve
         * the original source before resizing it.
         */
        if ((v2Ptr == vPtr) && (tmpPtr == NULL)) {
            tmpPtr = Rbc_VectorNew(vPtr->dataPtr);
            if (tmpPtr == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate temporary vector", -1));
                goto error;
            }
            tmpPtr->type = vPtr->type;
            if (Rbc_VectorDuplicate(tmpPtr, vPtr) != TCL_OK) {
                goto error;
            }
            sourcePtr = tmpPtr;
        }
        oldSize = v2Ptr->length;
        if (AddVectorSizes(interp, oldSize, extra, &newSize) != TCL_OK) {
            goto error;
        }
        if (Rbc_VectorChangeLength(v2Ptr, newSize) != TCL_OK) {
            goto error;
        }
        sourceIndex = argIndex;
        destIndex = oldSize;
        while (sourceIndex < sourcePtr->length) {
            switch (vPtr->type) {
            case RBC_VECTOR_REAL:
                v2Ptr->data.real[destIndex] = sourcePtr->data.real[sourceIndex];
                break;
            case RBC_VECTOR_COMPLEX:
                v2Ptr->data.complex[destIndex] = sourcePtr->data.complex[sourceIndex];
                break;
            default:
                Tcl_Panic("bad vector type %d", (int)vPtr->type);
            }
            sourceIndex += nVectors;
            destIndex++;
        }
        Rbc_VectorUpdateClients(v2Ptr);
        if (v2Ptr->flush) {
            Rbc_VectorFlushCache(v2Ptr);
        }
    }
    if (tmpPtr != NULL) {
        Rbc_VectorFree(tmpPtr);
    }
    return TCL_OK;

error:
    if (tmpPtr != NULL) {
        Rbc_VectorFree(tmpPtr);
    }
    return TCL_ERROR;
}

/*
 * -----------------------------------------------------------------------
 *
 * VariableOp --
 *
 *      Renames the variable associated with the vector
 *
 * Parameters:
 *      VectorObject *vPtr
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
 * -----------------------------------------------------------------------
 */
static int VariableOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc > 2) {
        if (Rbc_VectorMapVariable(interp, vPtr, Tcl_GetString(objv[2])) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (vPtr->arrayName != NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(vPtr->arrayName, -1));
    }
    return TCL_OK;
}

/**
 * @brief Append a selected vector range without Tcl-list conversion.
 * @ingroup vector_command_paths
 *
 * Requires matching real/complex types. Saves source selection before resize
 * and uses memmove against current storage, permitting self-append. Checks
 * length/byte arithmetic. Returns a Tcl status; does not flush or notify.
 */
static int AppendVector(VectorObject *destPtr, VectorObject *srcPtr) {
    VectorData data;
    Tcl_Size size;
    Tcl_Size sourceFirst;
    Tcl_Size sourceLength;
    Tcl_Size oldSize;
    Tcl_Size newSize;
    size_t byteCount;
    size_t elementSize;

    /*
     * Do not reinterpret storage when appending one vector to another.
     */
    if (destPtr->type != srcPtr->type) {
        Tcl_SetObjResult(destPtr->interp, Tcl_NewStringObj("can't append vectors of different types", -1));
        return TCL_ERROR;
    }
    /*
     * Preserve the selected source range before resizing the
     * destination. destPtr and srcPtr may refer to the same vector,
     * and resizing resets its first/last selection.
     */
    sourceFirst = srcPtr->first;
    sourceLength = srcPtr->last - sourceFirst + 1;
    oldSize = destPtr->length;
    if (AddVectorSizes(destPtr->interp, oldSize, sourceLength, &newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (destPtr->type) {
    case RBC_VECTOR_REAL:
        elementSize = sizeof(double);
        break;
    case RBC_VECTOR_COMPLEX:
        elementSize = sizeof(Rbc_Complex);
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)destPtr->type);
        return TCL_ERROR;
    }
    if (GetArrayByteCount(destPtr->interp, sourceLength, elementSize, &byteCount) != TCL_OK) {
        return TCL_ERROR;
    }
    if (PrepareCommandWrite(destPtr, newSize, &data, &size) != TCL_OK) {
        return TCL_ERROR;
    }
    if (byteCount > 0) {
        switch (destPtr->type) {
        case RBC_VECTOR_REAL:
            memmove(data.real + oldSize, srcPtr->data.real + sourceFirst, byteCount);
            break;
        case RBC_VECTOR_COMPLEX:
            memmove(data.complex + oldSize, srcPtr->data.complex + sourceFirst, byteCount);
            break;
        default:
            Tcl_Panic("bad vector type %d", (int)destPtr->type);
        }
    }
    FinishCommandWrite(destPtr, data, newSize, size);
    return TCL_OK;
}

/**
 * @brief Stage list values before appending the completed list.
 * @ingroup vector_command_paths
 *
 * Converts a complete list into temporary storage and rechecks lifetime and
 * writable state after conversion. Appends to the current length, accounting
 * for callback edits. Conversion failure does not append a partial list.
 * Does not flush or notify; the instance dispatcher retains the destination.
 */
static int AppendList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorData values, data;
    Tcl_Size length, size;
    size_t bytes;
    size_t elementSize = (vPtr->type == RBC_VECTOR_REAL) ? sizeof(double) : sizeof(Rbc_Complex);

    if (ConvertCommandList(vPtr, objc, objv, &values) != TCL_OK) return TCL_ERROR;
    /* A conversion may have changed length: append to the current destination. */
    if (CheckCommandDestination(vPtr) != TCL_OK ||
        AddVectorSizes(vPtr->interp, vPtr->length, objc, &length) != TCL_OK ||
        GetArrayByteCount(vPtr->interp, objc, elementSize, &bytes) != TCL_OK ||
        PrepareCommandWrite(vPtr, length, &data, &size) != TCL_OK) {
        if (values.raw != NULL) ckfree(values.raw);
        return TCL_ERROR;
    }
    if (bytes > 0) memcpy((char *)data.raw + vPtr->length * elementSize, values.raw, bytes);
    if (values.raw != NULL) ckfree(values.raw);
    FinishCommandWrite(vPtr, data, length, size);
    return TCL_OK;
}

/**
 * @brief Convert a binary scalar buffer into real vector storage.
 * @ingroup vector_command_paths
 *
 * Checks writable state, range and size arithmetic, extends storage as needed,
 * and advances indexPtr. Optional byte swapping modifies the input buffer
 * in place per scalar. Returns a Tcl status; does not flush or notify.
 */
static int CopyValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, Tcl_Size length, int swap,
                      Tcl_Size *indexPtr) {
    VectorData data;
    Tcl_Size capacity;
    Tcl_Size finalLength;
    Tcl_Size i;
    Tcl_Size n;
    Tcl_Size newSize;

    if (Rbc_VectorCheckWritable(vPtr->interp, (Rbc_Vector *)vPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    if ((length < 0) || (indexPtr == NULL) || (*indexPtr < 0) || (*indexPtr > vPtr->length)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("invalid binary vector range", -1));
        return TCL_ERROR;
    }
    if ((fmt == FMT_UNKNOWN) || (size <= 0)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("invalid binary format", -1));
        return TCL_ERROR;
    }
    if (AddVectorSizes(vPtr->interp, *indexPtr, length, &newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    if (swap && (size > 1)) {
        size_t nBytes;
        size_t byteOffset;
        unsigned char *p;
        unsigned char temp;
        int left;
        int right;

        if (GetArrayByteCount(vPtr->interp, length, (size_t)size, &nBytes) != TCL_OK) {
            return TCL_ERROR;
        }
        for (byteOffset = 0; byteOffset < nBytes; byteOffset += (size_t)size) {
            p = (unsigned char *)byteArr + byteOffset;
            for (left = 0, right = size - 1; left < right; left++, right--) {
                temp = p[left];
                p[left] = p[right];
                p[right] = temp;
            }
        }
    }
    finalLength = MAX(newSize, vPtr->length);
    if (PrepareCommandWrite(vPtr, finalLength, &data, &capacity) != TCL_OK) return TCL_ERROR;
#define CopyArrayToVector(array)                                                                                       \
    do {                                                                                                               \
        for (i = 0, n = *indexPtr; i < length; i++, n++) {                                                             \
            data.real[n] = (double)(array)[i];                                                                    \
        }                                                                                                              \
    } while (0)
    switch (fmt) {
    case FMT_CHAR:
        CopyArrayToVector((char *)byteArr);
        break;
    case FMT_UCHAR:
        CopyArrayToVector((unsigned char *)byteArr);
        break;
    case FMT_SHORT:
        CopyArrayToVector((short *)byteArr);
        break;
    case FMT_USHORT:
        CopyArrayToVector((unsigned short *)byteArr);
        break;
    case FMT_INT:
        CopyArrayToVector((int *)byteArr);
        break;
    case FMT_UINT:
        CopyArrayToVector((unsigned int *)byteArr);
        break;
    case FMT_LONG:
        CopyArrayToVector((long *)byteArr);
        break;
    case FMT_ULONG:
        CopyArrayToVector((unsigned long *)byteArr);
        break;
    case FMT_LONGLONG:
        CopyArrayToVector((long long *)byteArr);
        break;
    case FMT_ULONGLONG:
        CopyArrayToVector((unsigned long long *)byteArr);
        break;
    case FMT_FLOAT:
        CopyArrayToVector((float *)byteArr);
        break;
    case FMT_DOUBLE:
        CopyArrayToVector((double *)byteArr);
        break;
    case FMT_UNKNOWN:
        /*
         * Rejected above. Keep the case to satisfy exhaustive-switch
         * diagnostics.
         */
        assert(0);
        return TCL_ERROR;
    }
#undef CopyArrayToVector
    *indexPtr = newSize;
    FinishCommandWrite(vPtr, data, finalLength, capacity);
    return TCL_OK;
}

/**
 * @brief Convert interleaved binary components into complex storage.
 * @ingroup vector_command_paths
 *
 * Requires a complex vector. Checks writable state and size arithmetic;
 * extends storage and advances indexPtr by logical values. Swaps bytes within
 * each scalar component, never exchanging real and imaginary components.
 * Modifies the input buffer when swapping; does not flush or notify.
 */
static int CopyComplexValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, Tcl_Size length,
                             int swap, Tcl_Size *indexPtr) {
    VectorData data;
    Tcl_Size capacity;
    Tcl_Size finalLength;
    Tcl_Size i;
    Tcl_Size n;
    Tcl_Size newSize;
    Tcl_Size componentCount;

    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    if (Rbc_VectorCheckWritable(vPtr->interp, (Rbc_Vector *)vPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    if ((length < 0) || (indexPtr == NULL) || (*indexPtr < 0) || (*indexPtr > vPtr->length)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("invalid binary vector range", -1));
        return TCL_ERROR;
    }
    if ((fmt == FMT_UNKNOWN) || (size <= 0)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("invalid binary format", -1));
        return TCL_ERROR;
    }
    if (AddVectorSizes(vPtr->interp, *indexPtr, length, &newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Each complex value contains two formatted scalar components.
     */
    if (MultiplyVectorSizes(vPtr->interp, length, 2, &componentCount) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Swap each scalar component independently.  Do not swap the
     * real/imaginary components themselves.
     */
    if (swap && (size > 1)) {
        size_t nBytes;
        size_t byteOffset;
        unsigned char *p;
        unsigned char temp;
        int left;
        int right;

        if (GetArrayByteCount(vPtr->interp, componentCount, (size_t)size, &nBytes) != TCL_OK) {
            return TCL_ERROR;
        }
        for (byteOffset = 0; byteOffset < nBytes; byteOffset += (size_t)size) {
            p = (unsigned char *)byteArr + byteOffset;
            for (left = 0, right = size - 1; left < right; left++, right--) {
                temp = p[left];
                p[left] = p[right];
                p[right] = temp;
            }
        }
    }
    finalLength = MAX(newSize, vPtr->length);
    if (PrepareCommandWrite(vPtr, finalLength, &data, &capacity) != TCL_OK) return TCL_ERROR;
#define CopyArrayToComplexVector(array)                                                                                \
    do {                                                                                                               \
        for (i = 0, n = *indexPtr; i < length; i++, n++) {                                                             \
            data.complex[n].real = (double)(array)[2 * i];                                                       \
            data.complex[n].imag = (double)(array)[2 * i + 1];                                                   \
        }                                                                                                              \
    } while (0)
    switch (fmt) {
    case FMT_CHAR:
        CopyArrayToComplexVector((char *)byteArr);
        break;
    case FMT_UCHAR:
        CopyArrayToComplexVector((unsigned char *)byteArr);
        break;
    case FMT_SHORT:
        CopyArrayToComplexVector((short *)byteArr);
        break;
    case FMT_USHORT:
        CopyArrayToComplexVector((unsigned short *)byteArr);
        break;
    case FMT_INT:
        CopyArrayToComplexVector((int *)byteArr);
        break;
    case FMT_UINT:
        CopyArrayToComplexVector((unsigned int *)byteArr);
        break;
    case FMT_LONG:
        CopyArrayToComplexVector((long *)byteArr);
        break;
    case FMT_ULONG:
        CopyArrayToComplexVector((unsigned long *)byteArr);
        break;
    case FMT_LONGLONG:
        CopyArrayToComplexVector((long long *)byteArr);
        break;
    case FMT_ULONGLONG:
        CopyArrayToComplexVector((unsigned long long *)byteArr);
        break;
    case FMT_FLOAT:
        CopyArrayToComplexVector((float *)byteArr);
        break;
    case FMT_DOUBLE:
        CopyArrayToComplexVector((double *)byteArr);
        break;
    case FMT_UNKNOWN:
        assert(0);
        return TCL_ERROR;
    }
#undef CopyArrayToComplexVector
    *indexPtr = newSize;
    FinishCommandWrite(vPtr, data, finalLength, capacity);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * InRange --
 *
 *      Determines if a value lies within a given range.
 *
 *      The value is normalized and compared against the interval
 *      [0..1], where 0.0 is the minimum and 1.0 is the maximum.
 *      DBL_EPSILON is the smallest number that can be represented
 *      on the host machine, such that (1.0 + epsilon) != 1.0.
 *
 *      Please note, min cannot be greater than max.
 *
 * Parameters:
 *      double value
 *      double min
 *      double max
 *
 * Results:
 *      If the value is within of the interval [min..max], 1 is
 *      returned; 0 otherwise.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int InRange(double value, double min, double max) {
    double range;

    range = max - min;
    if (range < DBL_EPSILON) {
        return (FABS(max - value) < DBL_EPSILON);
    } else {
        double norm;
        norm = (value - min) / range;
        return ((norm >= -DBL_EPSILON) && ((norm - 1.0) < DBL_EPSILON));
    }
}

/**
 * @brief Convert a complete Tcl list before replacing vector storage.
 * @ingroup vector_command_paths
 *
 * Returns a Tcl status. Conversion failure leaves destination values/length
 * unchanged except for the callbacks' own changes. Transfers staged storage
 * on success, with lifetime/read-only checks before commit. Does not notify;
 * callers retain the destination and interpreter through callbacks.
 */
static int CopyList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorData data;
    if (ConvertCommandList(vPtr, objc, objv, &data) != TCL_OK) return TCL_ERROR;
    if (CheckCommandDestination(vPtr) != TCL_OK) {
        if (data.raw != NULL) ckfree(data.raw);
        return TCL_ERROR;
    }
    FinishCommandWrite(vPtr, data, objc, objc);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_VectorSortIndex --
 *
 *      TODO: Description
 *
 * Parameters:
 *      VectorObject **vPtrPtr
 *      int nVectors
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Tcl_Size *Rbc_VectorSortIndex(VectorObject **vPtrPtr, Tcl_Size nVectors) {
    VectorObject *vPtr;
    Tcl_Size *indexArr;
    Tcl_Size length;
    Tcl_Size i;
    size_t byteCount;

    vPtr = *vPtrPtr;
    /*
     * Sorting requires an ordering relation.  Complex values have no
     * natural ordering, and the sorting implementation operates on
     * real vector storage.
     */
    for (i = 0; i < nVectors; i++) {
        if (vPtrPtr[i]->type != RBC_VECTOR_REAL) {
            Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't sort complex vectors", -1));
            return NULL;
        }
    }
    length = vPtr->last - vPtr->first + 1;
    if (length == 0) {
        /*
         * Callers use NULL to indicate failure.  Return a harmless
         * allocation for an empty successful sort instead of relying on
         * platform-specific zero-size allocation behavior.
         */
        indexArr = Tcl_AttemptAlloc(sizeof(*indexArr));
        if (indexArr == NULL) {
            Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector sort indices", -1));
            return NULL;
        }
        return indexArr;
    }
    if (GetArrayByteCount(vPtr->interp, length, sizeof(*indexArr), &byteCount) != TCL_OK) {
        return NULL;
    }
    indexArr = Tcl_AttemptAlloc(byteCount);
    if ((indexArr == NULL) && (byteCount > 0)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector sort indices", -1));
        return NULL;
    }
    for (i = 0; i < length; i++) {
        indexArr[i] = vPtr->first + i;
    }
    SortVectorIndices(indexArr, length, vPtrPtr, nVectors);
    return indexArr;
}

static int CompareVectorIndices(VectorObject *const *vectorArr, Tcl_Size nVectors, Tcl_Size i1, Tcl_Size i2) {
    Tcl_Size vectorIndex;

    for (vectorIndex = 0; vectorIndex < nVectors; vectorIndex++) {
        double value1;
        double value2;

        value1 = vectorArr[vectorIndex]->data.real[i1];
        value2 = vectorArr[vectorIndex]->data.real[i2];
        if (value1 < value2) {
            return -1;
        }
        if (value1 > value2) {
            return 1;
        }
    }
    return 0;
}

static void SiftVectorIndices(Tcl_Size *indexArr, Tcl_Size start, Tcl_Size end, VectorObject *const *vectorArr,
                              Tcl_Size nVectors) {
    Tcl_Size root;

    if (end <= start) {
        return;
    }
    root = start;
    /*
     * Build/restore a max heap.  The bound on root also guarantees
     * that root * 2 + 1 cannot overflow Tcl_Size.
     */
    while (root <= ((end - 1) / 2)) {
        Tcl_Size child;
        Tcl_Size swapIndex;
        Tcl_Size tmp;

        child = root * 2 + 1;
        swapIndex = root;
        if (CompareVectorIndices(vectorArr, nVectors, indexArr[swapIndex], indexArr[child]) < 0) {
            swapIndex = child;
        }
        if ((child < end) &&
            (CompareVectorIndices(vectorArr, nVectors, indexArr[swapIndex], indexArr[child + 1]) < 0)) {
            swapIndex = child + 1;
        }
        if (swapIndex == root) {
            return;
        }
        tmp = indexArr[root];
        indexArr[root] = indexArr[swapIndex];
        indexArr[swapIndex] = tmp;
        root = swapIndex;
    }
}

static void SortVectorIndices(Tcl_Size *indexArr, Tcl_Size length, VectorObject *const *vectorArr, Tcl_Size nVectors) {
    Tcl_Size start;
    Tcl_Size end;

    if (length < 2) {
        return;
    }
    /*
     * Heapify.
     */
    start = (length - 2) / 2;
    for (;;) {
        SiftVectorIndices(indexArr, start, length - 1, vectorArr, nVectors);
        if (start == 0) {
            break;
        }
        start--;
    }
    /*
     * Repeatedly move the largest remaining index to the end.
     */
    end = length - 1;
    while (end > 0) {
        Tcl_Size tmp;

        tmp = indexArr[end];
        indexArr[end] = indexArr[0];
        indexArr[0] = tmp;
        end--;
        SiftVectorIndices(indexArr, 0, end, vectorArr, nVectors);
    }
}
