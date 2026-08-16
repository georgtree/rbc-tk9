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

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "rbcVector.h"

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
static int InRange(double value, double min, double max);
static int CopyList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]);
static Tcl_Size *SortVectors(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv);
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
                                                   {{"index", 3, 4, "index ?value?"}, IndexOp},
                                                   {{"length", 2, 3, "?newSize?"}, LengthOp},
                                                   {{"merge", 3, 0, "vecName ?vecName?..."}, MergeOp},
                                                   {{"normalize", 2, 3, "?vecName?"}, NormalizeOp},
                                                   {{"offset", 2, 3, "?offset?"}, OffsetOp},
                                                   {{"populate", 4, 4, "vecName density"}, PopulateOp},
                                                   {{"random", 2, 2, ""}, RandomOp},
                                                   {{"range", 4, 4, "first last"}, RangeOp},
                                                   {{"search", 3, 5, "?-value? value ?value?"}, SearchOp},
                                                   {{"seq", 4, 5, "start end ?step?"}, SeqOp},
                                                   {{"set", 3, 3, "list"}, SetOp},
                                                   {{"sort", 2, 0, "?-reverse? ?vecName?..."}, SortOp},
                                                   {{"split", 2, 0, "?vecName?..."}, SplitOp},
                                                   {{"variable", 2, 3, "?varName?"}, VariableOp},
                                                   {{NULL, 0, 0, NULL}, NULL}};

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

    /*
     * Reset the selected region before each instance operation.
     */
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;

    return vectorInstOpCmd[index].proc(vPtr, interp, objc, objv);
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
static int AppendOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    int result;
    VectorObject *v2Ptr;

    for (i = 2; i < objc; i++) {
        v2Ptr = Rbc_VectorParseElement(NULL, vPtr->dataPtr, Tcl_GetString(objv[i]), NULL, NS_SEARCH_BOTH);
        if (v2Ptr != NULL) {
            result = AppendVector(vPtr, v2Ptr);
        } else {
            Tcl_Size nElem;
            Tcl_Obj **elemObjArr;

            if (Tcl_ListObjGetElements(interp, objv[i], &nElem, &elemObjArr) != TCL_OK) {
                return TCL_ERROR;
            }
            result = AppendList(vPtr, nElem, elemObjArr);
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
static int ArithOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv) {
    register double value;
    Tcl_Size i;
    VectorObject *v2Ptr;
    double scalar;
    Tcl_Obj *listObjPtr;
    char *string;

    v2Ptr = Rbc_VectorParseElement(NULL, vPtr->dataPtr, Tcl_GetString(objv[2]), NULL, NS_SEARCH_BOTH);
    if (v2Ptr != NULL) {
        Tcl_Size j;
        Tcl_Size length;

        length = v2Ptr->last - v2Ptr->first + 1;
        if (length != vPtr->length) {
            Rbc_AppendResultStrings(interp, "vectors \"", Tcl_GetString(objv[0]), "\" and \"", Tcl_GetString(objv[2]),
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

/* Parses "-at" option */
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
static int BinreadOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Channel channel;
    const char *channelName;
    const char *string;
    char *byteArr;
    enum NativeFormats fmt;
    struct NativeFmtValue nfmt;
    Tcl_DString translation;
    Tcl_Obj *savedErrorObj;
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
    int mode;
    int size;
    int swap;
    int translationChanged;
    int result;

    byteArr = NULL;
    savedErrorObj = NULL;
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

    fmt = nfmt.format;
    size = nfmt.size;

    if ((fmt == FMT_UNKNOWN) || (size <= 0)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid binary format", -1));
        goto cleanup;
    }

    /*
     * With no explicit count, read BUFFER_SIZE values per iteration.
     * With a count, allocate enough space for that one requested read.
     */
    bufferValues = (count == 0) ? (Tcl_Size)BUFFER_SIZE : count;

    if (GetArrayByteCount(interp, bufferValues, (size_t)size, &bufferByteCount) != TCL_OK) {
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
        bytesRead = Tcl_Read(channel, byteArr, bufferBytes);

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

        if ((bytesRead % size) != 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("error reading channel: "
                                                      "input ended inside a binary value",
                                                      -1));
            goto cleanup;
        }

        length = bytesRead / size;

        if (CopyValues(vPtr, byteArr, fmt, size, length, swap, &first) != TCL_OK) {
            goto cleanup;
        }

        if (AddVectorSizes(interp, total, length, &newTotal) != TCL_OK) {
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
    /*
     * Preserve the original error if restoring the channel itself also
     * fails.
     */
    if (result != TCL_OK) {
        savedErrorObj = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(savedErrorObj);
    }

    if (translationChanged) {
        if (Tcl_SetChannelOption(interp, channel, "-translation", Tcl_DStringValue(&translation)) != TCL_OK) {
            if (result == TCL_OK) {
                result = TCL_ERROR;
            }
        }
    }

    if (savedErrorObj != NULL) {
        Tcl_SetObjResult(interp, savedErrorObj);
        Tcl_DecrRefCount(savedErrorObj);
    }

    Tcl_DStringFree(&translation);

    if (byteArr != NULL) {
        ckfree(byteArr);
    }

    if (result != TCL_OK) {
        return TCL_ERROR;
    }

    if (vPtr->flush) {
        Rbc_VectorFlushCache(vPtr);
    }

    Rbc_VectorUpdateClients(vPtr);

    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)total));

    return TCL_OK;
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
            continue; /* Skip elements marked for deletion. */
        }
        if (count < valueIndex) {
            vPtr->valueArr[count] = vPtr->valueArr[valueIndex];
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
    char *string;

    string = Tcl_GetString(objv[2]);
    if (Rbc_VectorGetIndexRange(interp, vPtr, string, INDEX_ALL_FLAGS, (Rbc_VectorIndexProc **)NULL) != TCL_OK) {
        return TCL_ERROR;
    }
    first = vPtr->first, last = vPtr->last;
    if (objc == 3) {
        Tcl_Obj *listObjPtr;

        if (first == vPtr->length || last == vPtr->length) {
            Rbc_AppendResultStrings(interp, "can't get index \"", string, "\"", (char *)NULL);
            return TCL_ERROR; /* Can't read from index "++end" */
        }
        listObjPtr = Rbc_GetValues(vPtr, first, last);
        Tcl_SetObjResult(interp, listObjPtr);
    } else {
        double value;

        /* FIXME: huh? Why set values here?.  */
        if (first == SPECIAL_INDEX) {
            Rbc_AppendResultStrings(interp, "can't set index \"", string, "\"", (char *)NULL);
            return TCL_ERROR; /* Tried to set "min" or "max" */
        }
        if (Rbc_GetDouble(vPtr->interp, objv[3], &value) != TCL_OK) {
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
    VectorObject *v2Ptr;
    VectorObject **vecArr;
    VectorObject **vPtrPtr;
    Tcl_Size refSize;
    Tcl_Size length;
    Tcl_Size nElem;
    Tcl_Size i;
    Tcl_Size valueIndex;
    double *valuePtr;
    double *valueArr;
    size_t vectorBytes;
    size_t valueBytes;

    if (GetArrayByteCount(interp, objc, sizeof(*vecArr), &vectorBytes) != TCL_OK) {
        return TCL_ERROR;
    }
    vecArr = ckalloc(vectorBytes);
    vPtrPtr = vecArr;
    refSize = -1;
    nElem = 0;
    for (i = 2; i < objc; i++) {
        if (Rbc_VectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i]), &v2Ptr) != TCL_OK) {
            ckfree(vecArr);
            return TCL_ERROR;
        }
        length = v2Ptr->last - v2Ptr->first + 1;
        if (refSize < 0) {
            refSize = length;

        } else if (length != refSize) {
            Rbc_AppendResultStrings(interp, "vectors \"", vPtr->name, "\" and \"", v2Ptr->name, "\" differ in length",
                             (char *)NULL);
            ckfree(vecArr);
            return TCL_ERROR;
        }
        *vPtrPtr++ = v2Ptr;
        if (AddVectorSizes(interp, nElem, refSize, &nElem) != TCL_OK) {
            ckfree(vecArr);
            return TCL_ERROR;
        }
    }
    *vPtrPtr = NULL;
    if (GetArrayByteCount(interp, nElem, sizeof(*valueArr), &valueBytes) != TCL_OK) {
        ckfree(vecArr);
        return TCL_ERROR;
    }
    if (nElem > 0) {
        valueArr = ckalloc(valueBytes);
    } else {
        valueArr = NULL;
    }
    valuePtr = valueArr;
    for (valueIndex = 0; valueIndex < refSize; valueIndex++) {
        for (vPtrPtr = vecArr; *vPtrPtr != NULL; vPtrPtr++) {
            *valuePtr++ = (*vPtrPtr)->valueArr[valueIndex + (*vPtrPtr)->first];
        }
    }
    ckfree(vecArr);
    if (Rbc_VectorReset(vPtr, valueArr, nElem, nElem, TCL_DYNAMIC) != TCL_OK) {
        if (valueArr != NULL) {
            ckfree(valueArr);
        }
        return TCL_ERROR;
    }
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
    double *valuePtr;
    const char *name;
    double range;
    double slice;
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
    v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, name, name, name, &isNew);
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
        valuePtr = v2Ptr->valueArr;
        for (i = 0; i < (sourcePtr->length - 1); i++) {
            range = sourcePtr->valueArr[i + 1] - sourcePtr->valueArr[i];
            slice = range / (double)valuesPerInterval;
            /*
             * Write the interval's starting value followed by the
             * requested intermediate values. The ending value is
             * written as the start of the next interval.
             */
            for (j = 0; j < valuesPerInterval; j++) {
                *valuePtr++ = sourcePtr->valueArr[i] + (slice * (double)j);
            }
        }
        /*
         * The final source value is not written by the interval loop.
         */
        *valuePtr++ = sourcePtr->valueArr[sourcePtr->length - 1];
        assert(valuePtr == (v2Ptr->valueArr + v2Ptr->length));
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
    for (i = 0; i < vPtr->length; i++) {
        vPtr->valueArr[i] = Rbc_RandomDouble();
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
static int SearchOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    double min, max;
    Tcl_Size i;
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
    listObjPtr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(listObjPtr);
    for (i = 0; i < vPtr->length; i++) {
        if (InRange(vPtr->valueArr[i], min, max)) {
            Tcl_Obj *objPtr;

            if (wantValue) {
                objPtr = Tcl_NewDoubleObj(vPtr->valueArr[i]);
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
            vPtr->valueArr[i] = start + step * (double)i;
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
static int SetOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
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
        result = CopyList(vPtr, nElem, elemObjArr);
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
static int SortOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject *v2Ptr;
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
        return TCL_ERROR;
    }
    mergeArr = ckalloc(nBytes);
    if (nBytes > 0) {
        memcpy(mergeArr, vPtr->valueArr, nBytes);
    }
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
            Rbc_AppendResultStrings(interp, "vector \"", v2Ptr->name, "\" is not the same size as \"", vPtr->name, "\"",
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
    extra = vPtr->length / nVectors;
    sourcePtr = vPtr;
    tmpPtr = NULL;
    for (argIndex = 0; argIndex < nVectorArgs; argIndex++) {
        string = Tcl_GetString(objv[argIndex + 2]);
        v2Ptr = Rbc_VectorCreate(vPtr->dataPtr, string, string, string, &isNew);
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
            v2Ptr->valueArr[destIndex] = sourcePtr->valueArr[sourceIndex];
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
    Tcl_Size sourceFirst;
    Tcl_Size sourceLength;
    Tcl_Size oldSize;
    Tcl_Size newSize;
    size_t byteCount;

    /*
     * Preserve the selected source range before resizing the
     * destination.  destPtr and srcPtr may refer to the same vector,
     * and resizing resets its first/last selection.
     */
    sourceFirst = srcPtr->first;
    sourceLength = srcPtr->last - sourceFirst + 1;
    oldSize = destPtr->length;
    if (AddVectorSizes(destPtr->interp, oldSize, sourceLength, &newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    if (GetArrayByteCount(destPtr->interp, sourceLength, sizeof(double), &byteCount) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Rbc_VectorChangeLength(destPtr, newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    if (byteCount > 0) {
        memmove(destPtr->valueArr + oldSize, srcPtr->valueArr + sourceFirst, byteCount);
    }
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
static int AppendList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size oldSize;
    Tcl_Size newSize;
    Tcl_Size i;
    double value;

    oldSize = vPtr->length;
    if (AddVectorSizes(vPtr->interp, oldSize, objc, &newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Rbc_VectorChangeLength(vPtr, newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i++) {
        if (Rbc_GetDouble(vPtr->interp, objv[i], &value) != TCL_OK) {
            Rbc_VectorChangeLength(vPtr, oldSize);
            return TCL_ERROR;
        }
        vPtr->valueArr[oldSize + i] = value;
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
static int CopyValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, Tcl_Size length, int swap,
                      Tcl_Size *indexPtr) {
    Tcl_Size i;
    Tcl_Size n;
    Tcl_Size newSize;

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

    if (newSize > vPtr->length) {
        if (Rbc_VectorChangeLength(vPtr, newSize) != TCL_OK) {
            return TCL_ERROR;
        }
    }

#define CopyArrayToVector(array)                                                                                       \
    do {                                                                                                               \
        for (i = 0, n = *indexPtr; i < length; i++, n++) {                                                             \
            vPtr->valueArr[n] = (double)(array)[i];                                                                    \
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
    vPtr->notifyFlags |= UPDATE_RANGE;

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
static int CopyList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
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
Tcl_Size *Rbc_VectorSortIndex(VectorObject **vPtrPtr, Tcl_Size nVectors) {
    VectorObject *vPtr;
    Tcl_Size *indexArr;
    Tcl_Size length;
    Tcl_Size i;
    size_t byteCount;

    vPtr = *vPtrPtr;
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
static Tcl_Size *SortVectors(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv) {
    VectorObject **vPtrArray;
    VectorObject *v2Ptr;
    Tcl_Size *iArr;
    Tcl_Size vectorCount;
    Tcl_Size i;
    Tcl_Size j;
    size_t vectorBytes;

    if (AddVectorSizes(interp, objc, 1, &vectorCount) != TCL_OK) {
        return NULL;
    }
    if (GetArrayByteCount(interp, vectorCount, sizeof(*vPtrArray), &vectorBytes) != TCL_OK) {
        return NULL;
    }
    vPtrArray = ckalloc(vectorBytes);
    vPtrArray[0] = vPtr;
    iArr = NULL;
    for (i = 0; i < objc; i++) {
        if (Rbc_VectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i]), &v2Ptr) != TCL_OK) {
            goto error;
        }
        if (v2Ptr->length != vPtr->length) {
            Rbc_AppendResultStrings(interp, "vector \"", v2Ptr->name, "\" is not the same size as \"", vPtr->name, "\"",
                             (char *)NULL);
            goto error;
        }
        /*
         * A vector may participate in the sort only once.  Applying the
         * resulting permutation more than once would reorder its already
         * sorted contents a second time.
         */
        for (j = 0; j <= i; j++) {
            if (vPtrArray[j] == v2Ptr) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" specified more than once", v2Ptr->name));
                goto error;
            }
        }
        vPtrArray[i + 1] = v2Ptr;
    }
    iArr = Rbc_VectorSortIndex(vPtrArray, vectorCount);
    
error:
    ckfree(vPtrArray);
    return iArr;
}

static int CompareVectorIndices(VectorObject *const *vectorArr, Tcl_Size nVectors, Tcl_Size i1, Tcl_Size i2) {
    Tcl_Size vectorIndex;

    for (vectorIndex = 0; vectorIndex < nVectors; vectorIndex++) {
        double value1;
        double value2;

        value1 = vectorArr[vectorIndex]->valueArr[i1];
        value2 = vectorArr[vectorIndex]->valueArr[i2];
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
