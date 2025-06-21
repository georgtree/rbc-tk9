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

#include <string.h>
#include <stdlib.h>
#include "rbcVector.h"

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

/* Pointer to the array of values currently being sorted. */
static VectorObject **sortVectorArr;
static int nSortVectors;
static int reverse;

static int AppendVector(VectorObject *destPtr, VectorObject *srcPtr);
static int AppendList(VectorObject *vPtr, int objc, Tcl_Obj *const objv[]);
static int CopyValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, int length, int swap,
                      int *indexPtr);
static int InRange(double value, double min, double max);
static int CopyList(VectorObject *vPtr, int objc, Tcl_Obj *const objv[]);
static int *SortVectors(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const *objv);
static int CompareVectors(void *a, void *b);

/* Instance Functions Definitions (rbcVecObjCmd.c) */
typedef int(RbcVectorCmdOp)(VectorObject *, Tcl_Interp *, int, Tcl_Obj *const[]);
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

double drand48(void) { return (double)rand() / (double)RAND_MAX; }

void srand48(long int seed) { srand(seed); }

static Rbc_OpSpec vectorInstOpCmd[] = {{"*", ArithOp, 3, 3, "list"},
                                       {"+", ArithOp, 3, 3, "list"},
                                       {"-", ArithOp, 3, 3, "list"},
                                       {"/", ArithOp, 3, 3, "list"},
                                       {"append", AppendOp, 3, 0, "item ?item...?"},
                                       {"binread", BinreadOp, 3, 0, "channel ?numValues? ?flags?"},
                                       {"clear", ClearOp, 2, 2, ""},
                                       {"delete", DeleteOp, 3, 0, "index ?index...?"},
                                       {"dup", DupOp, 3, 3, "vecname"},
                                       {"expr", ExprOp, 3, 3, "expression"},
                                       {"index", IndexOp, 3, 4, "index ?value?"},
                                       {"length", LengthOp, 2, 3, "?newSize?"},
                                       {"merge", MergeOp, 3, 0, "vecName ?vecName...?"},
                                       {"normalize", NormalizeOp, 2, 3, "?vecName?"},
                                       {"offset", OffsetOp, 2, 3, "?offset?"},
                                       {"populate", PopulateOp, 4, 4, "vecName density"},
                                       {"random", RandomOp, 2, 2, ""},
                                       {"range", RangeOp, 4, 4, "first last"},
                                       {"search", SearchOp, 3, 5, "?-value? value ?value?"},
                                       {"seq", SeqOp, 4, 5, "start end ?step?"},
                                       {"set", SetOp, 3, 3, "list"},
                                       {"sort", SortOp, 2, 0, "?-reverse? ?vecName...?"},
                                       {"split", SplitOp, 2, 0, "?vecName...?"},
                                       {"variable", VariableOp, 2, 3, "?varName?"},
                                       RBC_OPSPEC_END};

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
int Rbc_VectorInstanceObjCmd(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    VectorObject *vPtr = clientData;
    Rbc_Op proc;

    proc = Rbc_GetOpFromObj(interp, vectorInstOpCmd, RBC_OP_ARG1, objc, objv);

    if (NULL == proc) {
        return TCL_ERROR;
    }

    /* reset selected indices */
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;

    return (*proc)(vPtr, interp, objc, objv);
}

/*
 * -----------------------------------------------------------------------
 *
 * AppendOp --
 *
 *      Appends one of more Tcl lists of values, or vector objects
 *      onto the end of the current vector object.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If a current vector can't be created,
 *      resized, any of the named vectors can't be found, or one of
 *      lists of values is invalid, TCL_ERROR is returned.
 *
 * Side Effects:
 *      Clients of current vector will be notified of the change.
 *
 * -----------------------------------------------------------------------
 */
static int AppendOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    register int i;
    int result;
    VectorObject *v2Ptr;

    for (i = 2; i < objc; i++) {
        v2Ptr = Rbc_VectorParseElement((Tcl_Interp *)NULL, vPtr->dataPtr, Tcl_GetString(objv[i]), (char **)NULL,
                                       NS_SEARCH_BOTH);
        if (v2Ptr != NULL) {
            result = AppendVector(vPtr, v2Ptr);
        } else {
            Tcl_Size nElem;
            Tcl_Obj **elemObjArr;

            if (Tcl_ListObjGetElements(interp, objv[i], &nElem, &elemObjArr) != TCL_OK) {
                return TCL_ERROR;
            }
            result = AppendList(vPtr, (int)nElem, elemObjArr);
        }
        if (result != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (objc > 2) {
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
static int ArithOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const *objv) {
    register double value;
    register int i;
    VectorObject *v2Ptr;
    double scalar;
    Tcl_Obj *listObjPtr;
    char *string;

    v2Ptr = Rbc_VectorParseElement((Tcl_Interp *)NULL, vPtr->dataPtr, Tcl_GetString(objv[2]), (char **)NULL,
                                   NS_SEARCH_BOTH);
    if (v2Ptr != NULL) {
        register int j;
        int length;

        length = v2Ptr->last - v2Ptr->first + 1;
        if (length != vPtr->length) {
            Tcl_AppendResult(interp, "vectors \"", Tcl_GetString(objv[0]), "\" and \"", Tcl_GetString(objv[2]),
                             "\" are not the same length", (char *)NULL);
            return TCL_ERROR;
        }
        string = Tcl_GetString(objv[1]);
        listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **)NULL);
        switch (string[0]) {
        case '*':
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                value = vPtr->valueArr[i] * v2Ptr->valueArr[j];
                Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
            }
            break;
        case '/':
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                value = vPtr->valueArr[i] / v2Ptr->valueArr[j];
                Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
            }
            break;
        case '-':
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                value = vPtr->valueArr[i] - v2Ptr->valueArr[j];
                Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
            }
            break;
        case '+':
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                value = vPtr->valueArr[i] + v2Ptr->valueArr[j];
                Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
            }
            break;
        }
        Tcl_SetObjResult(interp, listObjPtr);
    } else if (Rbc_GetDouble(interp, objv[2], &scalar) == TCL_OK) {
        listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **)NULL);
        string = Tcl_GetString(objv[1]);
        switch (string[0]) {
        case '*':
            for (i = 0; i < vPtr->length; i++) {
                value = vPtr->valueArr[i] * scalar;
                Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
            }
            break;
        case '/':
            for (i = 0; i < vPtr->length; i++) {
                value = vPtr->valueArr[i] / scalar;
                Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
            }
            break;
        case '-':
            for (i = 0; i < vPtr->length; i++) {
                value = vPtr->valueArr[i] - scalar;
                Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
            }
            break;
        case '+':
            for (i = 0; i < vPtr->length; i++) {
                value = vPtr->valueArr[i] + scalar;
                Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
            }
            break;
        }
        Tcl_SetObjResult(interp, listObjPtr);
    } else {
        return TCL_ERROR;
    }
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

/* Parses "-format" option */
static Tcl_Size ParseFormat(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv, void *dstPtr) {
    const struct NativeFmtValue *table, *cursor;
    int size;
    Tcl_Size len;
    const char *string;

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

    if (objc == 0) {
        Tcl_SetObjResult(interp,
                         Tcl_ObjPrintf("option \"%s\" requires an additional argument", Tcl_GetString(objv[-1])));
        return -1;
    }

    string = Tcl_GetStringFromObj(objv[0], &len);
    if (len < 2) {
        Tcl_AppendResult(interp, "unknown binary format \"", string, "\"", NULL);
        return -1;
    }
    if (Tcl_GetInt(NULL, string + 1, &size) != TCL_OK) {
        Tcl_AppendResult(interp, "unknown binary format \"", string, "\": incorrect byte size", NULL);
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
        Tcl_AppendResult(interp, "unknown binary format \"", string,
                         "\": should be either i#, r#, u# (where # is size in bytes)", NULL);
        return -1;
    }

    for (cursor = table; cursor->size != -1; cursor++) {
        if (cursor->size == size) {
            *(struct NativeFmtValue *)dstPtr = *cursor;
            return 1;
        }
    }

    Tcl_AppendResult(interp, "can't handle format \"", string, "\"", NULL);
    return -1;
}

/* Parses "-at" option */
static Tcl_Size ParseAt(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv, void *dstPtr) {
    VectorObject *vPtr = (VectorObject *)clientData;
    const char *string;
    int first;

    if (objc == 0) {
        Tcl_SetObjResult(interp,
                         Tcl_ObjPrintf("option \"%s\" requires an additional argument", Tcl_GetString(objv[-1])));
        return -1;
    }
    string = Tcl_GetString(objv[0]);
    if (Rbc_VectorGetIndex(interp, vPtr, string, &first, 0, (Rbc_VectorIndexProc **)NULL) != TCL_OK) {
        return -1;
    }
    if (first > vPtr->length || first < 0) {
        Tcl_AppendResult(interp, "index \"", string, "\" is out of range", NULL);
        return -1;
    }
    *(int *)dstPtr = first;
    return 1;
}
/*
 * -----------------------------------------------------------------------
 *
 * BinreadOp --
 *
 *      Reads binary values from a Tcl channel. Values are either appended
 *      to the end of the vector or placed at a given index (using the
 *      "-at" option), overwriting existing values.  Data is read until EOF
 *      is found on the channel or a specified number of values are read.
 *      (note that this is not necessarily the same as the number of bytes).
 *
 *      The following flags are supported:
 *        -swap          Swap bytes
 *        -at index      Start writing data at the index.
 *        -format fmt    Specifies the format of the data.
 *
 *      This binary reader was created by Harald Kirsch (kir@iitb.fhg.de).
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      Returns a standard Tcl result. The interpreter result will contain
 *      the number of values (not the number of bytes) read.
 *
 * Caveats:
 *      Channel reads must end on an element boundary.
 *
 * -----------------------------------------------------------------------
 */
static int BinreadOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Channel channel;
    char *byteArr;
    char *string;
    enum NativeFormats fmt;
    int arraySize, bytesRead;
    int count = 0, total;
    int first = vPtr->length;
    int size, length, mode;
    int swap = FALSE;
    struct NativeFmtValue nfmt = {sizeof(double), FMT_DOUBLE};
    Tcl_DString ds;
    Tcl_Size argc;

    string = Tcl_GetString(objv[2]);
    channel = Tcl_GetChannel(interp, string, &mode);
    if (channel == NULL) {
        return TCL_ERROR;
    }
    if ((mode & TCL_READABLE) == 0) {
        Tcl_AppendResult(interp, "channel \"", string, "\" wasn't opened for reading", (char *)NULL);
        return TCL_ERROR;
    }

    if (objc > 3) {
        string = Tcl_GetString(objv[3]);
        if (string[0] != '-') {
            long int value;
            /* Get the number of values to read */
            if (Tcl_GetLongFromObj(interp, objv[3], &value) != TCL_OK) {
                return TCL_ERROR;
            }
            if (value < 0) {
                Tcl_AppendResult(interp, "count can't be negative", (char *)NULL);
                return TCL_ERROR;
            }
            count = (int)value;
            objc--;
            objv++;
        }
    }

    /* Process any options that remain */
    argc = objc - 2;
    objv += 2;
    if (argc > 1) {
        const Tcl_ArgvInfo binreadOpts[] = {{TCL_ARGV_CONSTANT, "-swap", (void *)TRUE, &swap, NULL, NULL},
                                            {TCL_ARGV_GENFUNC, "-format", ParseFormat, &nfmt, NULL, NULL},
                                            {TCL_ARGV_GENFUNC, "-at", ParseAt, &first, NULL, vPtr},
                                            TCL_ARGV_TABLE_END};
        if (Tcl_ParseArgsObjv(interp, binreadOpts, &argc, objv, NULL) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    fmt = nfmt.format;
    size = nfmt.size;
    if (count == 0) {
        arraySize = BUFFER_SIZE * size;
    } else {
        arraySize = count * size;
    }

    byteArr = (char *)ckalloc(arraySize);

    /* save current channel translation to restore later */
    Tcl_DStringInit(&ds);
    Tcl_GetChannelOption(interp, channel, "-translation", &ds);
    /* switch channel to binary */
    if (Tcl_SetChannelOption(interp, channel, "-translation", "binary") != TCL_OK) {
        return TCL_ERROR;
    }
    total = 0;
    while (!Tcl_Eof(channel)) {
        bytesRead = Tcl_Read(channel, byteArr, arraySize);
        if (bytesRead < 0) {
            Tcl_AppendResult(interp, "error reading channel: ", Tcl_PosixError(interp), NULL);
            goto error;
        }
        if ((bytesRead % size) != 0) {
            Tcl_AppendResult(interp, "error reading channel: short read", NULL);
            goto error;
        }
        length = bytesRead / size;
        if (CopyValues(vPtr, byteArr, fmt, size, length, swap, &first) != TCL_OK) {
            goto error;
        }
        total += length;
        if (count > 0) {
            break;
        }
    }
    ckfree(byteArr);

    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }
    Rbc_VectorUpdateClients(vPtr);

    /* Set the result as the number of values read */
    Tcl_SetObjResult(interp, Tcl_NewIntObj(total));
    Tcl_SetChannelOption(interp, channel, "-translation", Tcl_DStringValue(&ds));
    Tcl_DStringFree(&ds);
    return TCL_OK;

error:
    Tcl_SetChannelOption(interp, channel, "-translation", Tcl_DStringValue(&ds));
    Tcl_DStringFree(&ds);
    return TCL_ERROR;
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
static int ClearOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
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
static int DeleteOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    unsigned char *unsetArr;
    register int i, j;
    register int count;
    char *string;

    /* FIXME: Don't delete vector with no indices.  */
    if (objc == 2) {
        Rbc_VectorFree(vPtr);
        return TCL_OK;
    }
    /*
     * Allocate an "unset" bitmap the size of the vector.
     */
    unsetArr = (unsigned char *)RbcCalloc(sizeof(unsigned char), (vPtr->length + 7) / 8);
    /***    assert(unsetArr); */

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
    for (i = 0; i < vPtr->length; i++) {
        if (GetBit(i)) {
            continue; /* Skip elements marked for deletion. */
        }
        if (count < i) {
            vPtr->valueArr[count] = vPtr->valueArr[i];
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
static int DupOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    VectorObject *v2Ptr;
    int isNew;
    register int i;
    char *string;

    for (i = 2; i < objc; i++) {
        string = Tcl_GetString(objv[i]);
        v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, string, string, string, &isNew);
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
static int ExprOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
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
static int IndexOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    int first, last;
    char *string;

    string = Tcl_GetString(objv[2]);
    if (Rbc_VectorGetIndexRange(interp, vPtr, string, INDEX_ALL_FLAGS, (Rbc_VectorIndexProc **)NULL) != TCL_OK) {
        return TCL_ERROR;
    }
    first = vPtr->first, last = vPtr->last;
    if (objc == 3) {
        Tcl_Obj *listObjPtr;

        if (first == vPtr->length || last == vPtr->length) {
            Tcl_AppendResult(interp, "can't get index \"", string, "\"", (char *)NULL);
            return TCL_ERROR; /* Can't read from index "++end" */
        }
        listObjPtr = Rbc_GetValues(vPtr, first, last);
        Tcl_SetObjResult(interp, listObjPtr);
    } else {
        double value;

        /* FIXME: huh? Why set values here?.  */
        if (first == SPECIAL_INDEX) {
            Tcl_AppendResult(interp, "can't set index \"", string, "\"", (char *)NULL);
            return TCL_ERROR; /* Tried to set "min" or "max" */
        }
        if (Rbc_GetDouble(vPtr->interp, objv[3], &value) != TCL_OK) {
            return TCL_ERROR;
        }
        if (first == vPtr->length) {
            if (Rbc_VectorChangeLength(vPtr, vPtr->length + 1) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        Rbc_ReplicateValue(vPtr, first, last, value);
        Tcl_SetObjResult(interp, objv[3]);
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
static int LengthOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        int size;

        if (Tcl_GetIntFromObj(interp, objv[2], &size) != TCL_OK) {
            return TCL_ERROR;
        }
        if (size < 0) {
            Tcl_AppendResult(interp, "bad vector size \"", Tcl_GetString(objv[2]), "\"", (char *)NULL);
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
    Tcl_SetObjResult(interp, Tcl_NewIntObj(vPtr->length));
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
static int MergeOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    VectorObject *v2Ptr;
    VectorObject **vecArr;
    register VectorObject **vPtrPtr;
    int refSize, length, nElem;
    register int i;
    double *valuePtr, *valueArr;

    /* Allocate an array of vector pointers of each vector to be
     * merged in the current vector.  */
    vecArr = (VectorObject **)ckalloc(sizeof(VectorObject *) * objc);
    /***    assert(vecArr); */
    vPtrPtr = vecArr;

    refSize = -1;
    nElem = 0;
    for (i = 2; i < objc; i++) {
        if (Rbc_VectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i]), &v2Ptr) != TCL_OK) {
            ckfree((char *)vecArr);
            return TCL_ERROR;
        }
        /* Check that all the vectors are the same length */
        length = v2Ptr->last - v2Ptr->first + 1;
        if (refSize < 0) {
            refSize = length;
        } else if (length != refSize) {
            Tcl_AppendResult(vPtr->interp, "vectors \"", vPtr->name, "\" and \"", v2Ptr->name, "\" differ in length",
                             (char *)NULL);
            ckfree((char *)vecArr);
            return TCL_ERROR;
        }
        *vPtrPtr++ = v2Ptr;
        nElem += refSize;
    }
    *vPtrPtr = NULL;

    valueArr = (double *)ckalloc(sizeof(double) * nElem);
    if (valueArr == NULL) {
        Tcl_AppendResult(vPtr->interp, "not enough memory to allocate ", Rbc_Itoa(nElem), " vector elements",
                         (char *)NULL);
        return TCL_ERROR;
    }
    /* Merge the values from each of the vectors into the current vector */
    valuePtr = valueArr;
    for (i = 0; i < refSize; i++) {
        for (vPtrPtr = vecArr; *vPtrPtr != NULL; vPtrPtr++) {
            *valuePtr++ = (*vPtrPtr)->valueArr[i + (*vPtrPtr)->first];
        }
    }
    ckfree((char *)vecArr);
    Rbc_VectorReset(vPtr, valueArr, nElem, nElem, TCL_DYNAMIC);
    return TCL_OK;
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
static int NormalizeOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    register int i;
    double range;

    Rbc_VectorUpdateRange(vPtr);
    range = vPtr->max - vPtr->min;
    if (objc > 2) {
        VectorObject *v2Ptr;
        int isNew;
        char *string;

        string = Tcl_GetString(objv[2]);
        v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, string, string, string, &isNew);
        if (v2Ptr == NULL) {
            return TCL_ERROR;
        }
        if (Rbc_VectorChangeLength(v2Ptr, vPtr->length) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < vPtr->length; i++) {
            v2Ptr->valueArr[i] = (vPtr->valueArr[i] - vPtr->min) / range;
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
            norm = (vPtr->valueArr[i] - vPtr->min) / range;
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
static int OffsetOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        int newOffset;

        if (Tcl_GetIntFromObj(interp, objv[2], &newOffset) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->offset = newOffset;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(vPtr->offset));
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
static int PopulateOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    VectorObject *v2Ptr;
    int size, density;
    int isNew;
    register int i, j;
    double slice, range;
    register double *valuePtr;
    int count;
    char *string;

    string = Tcl_GetString(objv[2]);
    v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, string, string, string, &isNew);
    if (v2Ptr == NULL) {
        return TCL_ERROR;
    }
    if (vPtr->length == 0) {
        return TCL_OK; /* Source vector is empty. */
    }
    if (Tcl_GetIntFromObj(interp, objv[3], &density) != TCL_OK) {
        return TCL_ERROR;
    }
    if (density < 1) {
        Tcl_AppendResult(interp, "bad density \"", Tcl_GetString(objv[3]), "\"", (char *)NULL);
        return TCL_ERROR;
    }
    size = (vPtr->length - 1) * (density + 1) + 1;
    if (Rbc_VectorChangeLength(v2Ptr, size) != TCL_OK) {
        return TCL_ERROR;
    }
    count = 0;
    valuePtr = v2Ptr->valueArr;
    for (i = 0; i < (vPtr->length - 1); i++) {
        range = vPtr->valueArr[i + 1] - vPtr->valueArr[i];
        slice = range / (double)(density + 1);
        for (j = 0; j <= density; j++) {
            *valuePtr = vPtr->valueArr[i] + (slice * (double)j);
            valuePtr++;
            count++;
        }
    }
    count++;
    *valuePtr = vPtr->valueArr[i];
    /*** assert(count == v2Ptr->length); */
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
static int RandomOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
#ifdef HAVE_DRAND48
    register int i;

    for (i = 0; i < vPtr->length; i++) {
        vPtr->valueArr[i] = drand48();
    }
#endif
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
static int RangeOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *listObjPtr;
    int first, last;
    register int i;

    if ((Rbc_VectorGetIndex(interp, vPtr, Tcl_GetString(objv[2]), &first, INDEX_CHECK, (Rbc_VectorIndexProc **)NULL) !=
         TCL_OK) ||
        (Rbc_VectorGetIndex(interp, vPtr, Tcl_GetString(objv[3]), &last, INDEX_CHECK, (Rbc_VectorIndexProc **)NULL) !=
         TCL_OK)) {
        return TCL_ERROR;
    }
    listObjPtr = Tcl_NewListObj(0, NULL);
    if (first > last) {
        /* Return the list reversed */
        for (i = last; i <= first; i++) {
            Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(vPtr->valueArr[i]));
        }
    } else {
        for (i = first; i <= last; i++) {
            Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(vPtr->valueArr[i]));
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
static int SearchOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    double min, max;
    register int i;
    int wantValue;
    char *string;
    Tcl_Obj *listObjPtr;

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
    listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **)NULL);

    for (i = 0; i < vPtr->length; i++) {
        if (InRange(vPtr->valueArr[i], min, max)) {
            Tcl_ListObjAppendElement(interp, listObjPtr,
                                     wantValue ? Tcl_NewDoubleObj(vPtr->valueArr[i]) : Tcl_NewIntObj(i + vPtr->offset));
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
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
static int SeqOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    register int i;
    double start, finish, step;
    int fillVector;
    int nSteps;
    char *string;

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
        nSteps = vPtr->length;
    } else {
        nSteps = (int)((finish - start) / step) + 1;
    }
    if (nSteps > 0) {
        if (Rbc_VectorChangeLength(vPtr, nSteps) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < nSteps; i++) {
            vPtr->valueArr[i] = start + (step * (double)i);
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
 * SetOp --
 *
 *      Sets the data of the vector object from a list of values.
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
static int SetOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    int result;
    VectorObject *v2Ptr;
    Tcl_Size nElem;
    Tcl_Obj **elemObjArr;

    /* The source can be either a list of numbers or another vector.  */

    v2Ptr = Rbc_VectorParseElement((Tcl_Interp *)NULL, vPtr->dataPtr, Tcl_GetString(objv[2]), NULL, NS_SEARCH_BOTH);
    if (v2Ptr != NULL) {
        if (vPtr == v2Ptr) {
            VectorObject *tmpPtr;
            /*
             * Source and destination vectors are the same.  Copy the
             * source first into a temporary vector to avoid memory
             * overlaps.
             */
            tmpPtr = Rbc_VectorNew(vPtr->dataPtr);
            result = Rbc_VectorDuplicate(tmpPtr, v2Ptr);
            if (result == TCL_OK) {
                result = Rbc_VectorDuplicate(vPtr, tmpPtr);
            }
            Rbc_VectorFree(tmpPtr);
        } else {
            result = Rbc_VectorDuplicate(vPtr, v2Ptr);
        }
    } else if (Tcl_ListObjGetElements(interp, objv[2], &nElem, &elemObjArr) == TCL_OK) {
        result = CopyList(vPtr, (int)nElem, elemObjArr);
    } else {
        return TCL_ERROR;
    }

    if (result == TCL_OK) {
        /*
         * The vector has changed; so flush the array indices (they're
         * wrong now), find the new range of the data, and notify
         * the vector's clients that it's been modified.
         */
        if (vPtr->flush) {
            Rbc_VectorFlushCache(vPtr);
        }
        Rbc_VectorUpdateClients(vPtr);
    }
    return result;
}

/*
 * -----------------------------------------------------------------------
 *
 * SortOp --
 *
 *      Sorts the vector object and any other vectors according to
 *      sorting order of the vector object.
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      A standard Tcl result.  If any of the auxiliary vectors are
 *      a different size than the sorted vector object, TCL_ERROR is
 *      returned.  Otherwise TCL_OK is returned.
 *
 * Side Effects:
 *      The vectors are sorted.
 *
 * -----------------------------------------------------------------------
 */
static int SortOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    VectorObject *v2Ptr;
    char *string;
    double *mergeArr;
    int *iArr;
    int refSize, nBytes;
    int result;
    register int i, n;

    reverse = FALSE;
    if (objc > 2) {
        Tcl_Size length;
        string = Tcl_GetStringFromObj(objv[2], &length);
        if (string[0] == '-') {
            if ((length > 1) && (strncmp(string, "-reverse", length) == 0)) {
                reverse = TRUE;
            } else {
                Tcl_AppendResult(interp, "unknown flag \"", string, "\": should be \"-reverse\"", (char *)NULL);
                return TCL_ERROR;
            }
            objc--;
            objv++;
        }
    }
    if (objc > 2) {
        iArr = SortVectors(vPtr, interp, objc - 2, objv + 2);
    } else {
        iArr = Rbc_VectorSortIndex(&vPtr, 1);
    }
    if (iArr == NULL) {
        return TCL_ERROR;
    }
    refSize = vPtr->length;

    /*
     * Create an array to store a copy of the current values of the
     * vector. We'll merge the values back into the vector based upon
     * the indices found in the index array.
     */
    nBytes = sizeof(double) * refSize;
    mergeArr = (double *)ckalloc(nBytes);
    memcpy((char *)mergeArr, (char *)vPtr->valueArr, nBytes);
    for (n = 0; n < refSize; n++) {
        vPtr->valueArr[n] = mergeArr[iArr[n]];
    }
    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }
    Rbc_VectorUpdateClients(vPtr);

    /* Now sort any other vectors in the same fashion.  The vectors
     * must be the same size as the iArr though.  */
    result = TCL_ERROR;
    for (i = 2; i < objc; i++) {
        if (Rbc_VectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i]), &v2Ptr) != TCL_OK) {
            goto error;
        }
        if (v2Ptr->length != refSize) {
            Tcl_AppendResult(interp, "vector \"", v2Ptr->name, "\" is not the same size as \"", vPtr->name, "\"",
                             (char *)NULL);
            goto error;
        }
        memcpy((char *)mergeArr, (char *)v2Ptr->valueArr, nBytes);
        for (n = 0; n < refSize; n++) {
            v2Ptr->valueArr[n] = mergeArr[iArr[n]];
        }
        Rbc_VectorUpdateClients(v2Ptr);
        if (v2Ptr->flush) {
            Rbc_VectorFlushCache(v2Ptr);
        }
    }
    result = TCL_OK;
error:
    ckfree((char *)mergeArr);
    ckfree((char *)iArr);
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
static int SplitOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    int nVectors;

    nVectors = objc - 2;
    /* make it a no-op if no vectors are supplied */
    if (nVectors == 0)
        return TCL_OK;

    if ((vPtr->length % nVectors) != 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't split vector \"%s\" into %d even parts.", vPtr->name, nVectors));
        return TCL_ERROR;
    }

    VectorObject *v2Ptr;
    char *string; /* Name of vector. */
    int i, j, k;
    int oldSize, newSize, extra, isNew;

    extra = vPtr->length / nVectors;
    for (i = 0; i < nVectors; i++) {
        string = Tcl_GetString(objv[i + 2]);
        v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, string, string, string, &isNew);
        oldSize = v2Ptr->length;
        newSize = oldSize + extra;
        if (Rbc_VectorChangeLength(v2Ptr, newSize) != TCL_OK) {
            return TCL_ERROR;
        }
        for (j = i, k = oldSize; j < vPtr->length; j += nVectors, k++) {
            v2Ptr->valueArr[k] = vPtr->valueArr[j];
        }
        Rbc_VectorUpdateClients(v2Ptr);
        if (v2Ptr->flush) {
            Rbc_VectorFlushCache(v2Ptr);
        }
    }
    return TCL_OK;
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
static int VariableOp(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc > 2) {
        if (Rbc_VectorMapVariable(interp, vPtr, Tcl_GetString(objv[2])) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (vPtr->arrayName != NULL) {
        Tcl_SetResult(interp, vPtr->arrayName, TCL_VOLATILE);
    }
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * AppendVector --
 *
 *      Appends a vector to the end of another vector
 *
 * Parameters:
 *      VectorObject *destPtr
 *      VectorObject *srcPtr
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int AppendVector(VectorObject *destPtr, VectorObject *srcPtr) {
    int nBytes;
    int oldSize, newSize;

    oldSize = destPtr->length;
    newSize = oldSize + srcPtr->last - srcPtr->first + 1;
    if (Rbc_VectorChangeLength(destPtr, newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    nBytes = (newSize - oldSize) * sizeof(double);
    memcpy((char *)(destPtr->valueArr + oldSize), (srcPtr->valueArr + srcPtr->first), nBytes);
    destPtr->notifyFlags |= UPDATE_RANGE;
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------------
 *
 * AppendList --
 *
 *      Appends a list to the end of another list
 *
 * Parameters:
 *      VectorObject *vPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static int AppendList(VectorObject *vPtr, int objc, Tcl_Obj *const objv[]) {
    int count;
    register int i;
    double value;
    int oldSize;

    oldSize = vPtr->length;
    if (Rbc_VectorChangeLength(vPtr, vPtr->length + objc) != TCL_OK) {
        return TCL_ERROR;
    }
    count = oldSize;
    for (i = 0; i < objc; i++) {
        if (Rbc_GetDouble(vPtr->interp, objv[i], &value) != TCL_OK) {
            Rbc_VectorChangeLength(vPtr, count);
            return TCL_ERROR;
        }
        vPtr->valueArr[count++] = value;
    }
    vPtr->notifyFlags |= UPDATE_RANGE;
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * CopyValues --
 *
 *      TODO: Description
 *
 * Parameters:
 *      VectorObject *vPtr
 *      char *byteArr
 *      enum NativeFormats fmt
 *      int size
 *      int length
 *      int swap
 *      int *indexPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int CopyValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, int length, int swap,
                      int *indexPtr) {
    register int i, n;
    int newSize;

    if ((swap) && (size > 1)) {
        int nBytes = size * length;
        register unsigned char *p;
        register int left, right;

        for (i = 0; i < nBytes; i += size) {
            p = (unsigned char *)(byteArr + i);
            for (left = 0, right = size - 1; left < right; left++, right--) {
                p[left] ^= p[right];
                p[right] ^= p[left];
                p[left] ^= p[right];
            }
        }
    }
    newSize = *indexPtr + length;
    if (newSize > vPtr->length) {
        if (Rbc_VectorChangeLength(vPtr, newSize) != TCL_OK) {
            return TCL_ERROR;
        }
    }
#define CopyArrayToVector(vPtr, arr)                                                                                   \
    for (i = 0, n = *indexPtr; i < length; i++, n++) {                                                                 \
        (vPtr)->valueArr[n] = (double)(arr)[i];                                                                        \
    }

    switch (fmt) {
    case FMT_CHAR:
        CopyArrayToVector(vPtr, (char *)byteArr);
        break;

    case FMT_UCHAR:
        CopyArrayToVector(vPtr, (unsigned char *)byteArr);
        break;

    case FMT_INT:
        CopyArrayToVector(vPtr, (int *)byteArr);
        break;

    case FMT_UINT:
        CopyArrayToVector(vPtr, (unsigned int *)byteArr);
        break;

    case FMT_LONG:
        CopyArrayToVector(vPtr, (long *)byteArr);
        break;

    case FMT_ULONG:
        CopyArrayToVector(vPtr, (unsigned long *)byteArr);
        break;

    case FMT_LONGLONG:
        CopyArrayToVector(vPtr, (long long *)byteArr);
        break;

    case FMT_ULONGLONG:
        CopyArrayToVector(vPtr, (unsigned long long *)byteArr);
        break;

    case FMT_SHORT:
        CopyArrayToVector(vPtr, (short int *)byteArr);
        break;

    case FMT_USHORT:
        CopyArrayToVector(vPtr, (unsigned short int *)byteArr);
        break;

    case FMT_FLOAT:
        CopyArrayToVector(vPtr, (float *)byteArr);
        break;

    case FMT_DOUBLE:
        CopyArrayToVector(vPtr, (double *)byteArr);
        break;

    case FMT_UNKNOWN:
        break;
    }
    *indexPtr += length;
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

/*
 *--------------------------------------------------------------
 *
 * CopyList --
 *
 *      TODO: Description
 *
 * Parameters:
 *      VectorObject *vPtr
 *      int objc
 *      Tcl_Obj * const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int CopyList(VectorObject *vPtr, int objc, Tcl_Obj *const objv[]) {
    register int i;
    double value;

    if (Rbc_VectorChangeLength(vPtr, objc) != TCL_OK) {
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i++) {
        if (Rbc_GetDouble(vPtr->interp, objv[i], &value) != TCL_OK) {
            Rbc_VectorChangeLength(vPtr, i);
            return TCL_ERROR;
        }
        vPtr->valueArr[i] = value;
    }
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
int *Rbc_VectorSortIndex(VectorObject **vPtrPtr, int nVectors) {
    int *indexArr;
    register int i;
    VectorObject *vPtr = *vPtrPtr;
    int length;

    length = vPtr->last - vPtr->first + 1;
    indexArr = (int *)ckalloc(sizeof(int) * length);
    for (i = vPtr->first; i <= vPtr->last; i++) {
        indexArr[i] = i;
    }
    sortVectorArr = vPtrPtr;
    nSortVectors = nVectors;
    qsort((char *)indexArr, length, sizeof(int), (QSortCompareProc *)CompareVectors);
    return indexArr;
}

/*
 *--------------------------------------------------------------
 *
 * SortVectors --
 *
 *      TODO: Description
 *
 * Parameters:
 *      VectorObject *vPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const *objv
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int *SortVectors(VectorObject *vPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const *objv) {
    VectorObject **vPtrArray, *v2Ptr;
    int *iArr;
    register int i;

    vPtrArray = (VectorObject **)ckalloc(sizeof(VectorObject *) * (objc + 1));
    vPtrArray[0] = vPtr;
    iArr = NULL;
    for (i = 0; i < objc; i++) {
        if (Rbc_VectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i]), &v2Ptr) != TCL_OK) {
            goto error;
        }
        if (v2Ptr->length != vPtr->length) {
            Tcl_AppendResult(interp, "vector \"", v2Ptr->name, "\" is not the same size as \"", vPtr->name, "\"",
                             (char *)NULL);
            goto error;
        }
        vPtrArray[i + 1] = v2Ptr;
    }
    iArr = Rbc_VectorSortIndex(vPtrArray, objc + 1);
error:
    ckfree((char *)vPtrArray);
    return iArr;
}

/*
 *--------------------------------------------------------------
 *
 * CompareVectors --
 *
 *      TODO: Description
 *
 * Parameters:
 *      void *a
 *      void *b
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int CompareVectors(void *a, void *b) {
    double delta;
    int i;
    int sign;
    register VectorObject *vPtr;

    sign = (reverse) ? -1 : 1;
    for (i = 0; i < nSortVectors; i++) {
        vPtr = sortVectorArr[i];
        delta = vPtr->valueArr[*(int *)a] - vPtr->valueArr[*(int *)b];
        if (delta < 0.0) {
            return (-1 * sign);
        } else if (delta > 0.0) {
            return (1 * sign);
        }
    }
    return 0;
}
