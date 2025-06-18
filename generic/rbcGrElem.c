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

#include "rbcGraph.h"
#include "rbcChain.h"
#include <X11/Xutil.h>

static Tk_OptionParseProc StringToData;
static Tk_OptionPrintProc DataToString;
static Tk_OptionParseProc StringToDataPairs;
static Tk_OptionPrintProc DataPairsToString;
static Tk_OptionParseProc StringToAlong;
static Tk_OptionPrintProc AlongToString;
static Tk_CustomOption alongOption = {StringToAlong, AlongToString, (ClientData)0};
Tk_CustomOption rbcDataOption = {StringToData, DataToString, (ClientData)0};
Tk_CustomOption rbcDataPairsOption = {StringToDataPairs, DataPairsToString, (ClientData)0};
extern Tk_CustomOption rbcDistanceOption;

static int counter;

#include "rbcGrElem.h"

static Rbc_VectorChangedProc VectorChangedProc;

static int GetPenStyle(Graph *graphPtr, const char *string, Rbc_Uid type, PenStyle *stylePtr);
static void SyncElemVector(ElemVector *vPtr);
static void FindRange(ElemVector *vPtr);
static void FreeDataVector(ElemVector *vPtr);
static int EvalExprList(Tcl_Interp *interp, const char *list, int *nElemPtr, double **arrayPtr);
static int GetIndex(Tcl_Interp *interp, Element *elemPtr, char *string, int *indexPtr);
static int NameToElement(Graph *graphPtr, Tcl_Obj *nameObj, Element **elemPtrPtr);
static int CreateElement(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], Rbc_Uid classUid);
static void DestroyElement(Graph *graphPtr, Element *elemPtr);
static int RebuildDisplayList(Graph *graphPtr, Tcl_Obj *newList);

typedef int(RbcGrElementOp)(Graph *, Tcl_Interp *, Rbc_Uid, int, Tcl_Obj *const[]);
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

/*
 * ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */
/*
 *----------------------------------------------------------------------
 *
 * GetPenStyle --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      const char *string
 *      Rbc_Uid type
 *      PenStyle *stylePtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int GetPenStyle(Graph *graphPtr, const char *string, Rbc_Uid type, PenStyle *stylePtr) {
    Pen *penPtr;
    Tcl_Interp *interp = graphPtr->interp;
    const char **elemArr;
    Tcl_Size nElem;

    elemArr = NULL;
    if (Tcl_SplitList(interp, string, &nElem, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((nElem != 1) && (nElem != 3)) {
        Tcl_AppendResult(interp, "bad style \"", string, "\": should be ", "\"penName\" or \"penName min max\"",
                         (char *)NULL);
        if (elemArr != NULL) {
            ckfree((char *)elemArr);
        }
        return TCL_ERROR;
    }
    if (Rbc_GetPen(graphPtr, elemArr[0], type, &penPtr) != TCL_OK) {
        ckfree((char *)elemArr);
        return TCL_ERROR;
    }
    if (nElem == 3) {
        double min, max;

        if ((Tcl_GetDouble(interp, elemArr[1], &min) != TCL_OK) ||
            (Tcl_GetDouble(interp, elemArr[2], &max) != TCL_OK)) {
            ckfree((char *)elemArr);
            return TCL_ERROR;
        }
        SetWeight(stylePtr->weight, min, max);
    }
    stylePtr->penPtr = penPtr;
    ckfree((char *)elemArr);
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
 * FreeDataVector --
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
static void FreeDataVector(ElemVector *vPtr) {
    if (vPtr->clientId != NULL) {
        Rbc_FreeVectorId(vPtr->clientId); /* Free the old vector */
        vPtr->clientId = NULL;
    } else if (vPtr->valueArr != NULL) {
        ckfree((char *)vPtr->valueArr);
    }
    vPtr->valueArr = NULL;
    vPtr->nValues = 0;
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
 * EvalExprList --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *list
 *      int *nElemPtr
 *      double **arrayPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int EvalExprList(Tcl_Interp *interp, const char *list, int *nElemPtr, double **arrayPtr) {
    Tcl_Size nElem;
    const char **elemArr;
    double *array;
    int result;

    result = TCL_ERROR;
    elemArr = NULL;
    if (Tcl_SplitList(interp, list, &nElem, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }
    array = NULL;
    if (nElem > 0) {
        register double *valuePtr;
        Tcl_Size i;

        counter++;
        array = (double *)ckalloc(sizeof(double) * nElem);
        if (array == NULL) {
            Tcl_AppendResult(interp, "can't allocate new vector", (char *)NULL);
            goto badList;
        }
        valuePtr = array;
        for (i = 0; i < nElem; i++) {
            if (Tcl_ExprDouble(interp, elemArr[i], valuePtr) != TCL_OK) {
                goto badList;
            }
            valuePtr++;
        }
    }
    result = TCL_OK;

badList:
    ckfree((char *)elemArr);
    *arrayPtr = array;
    *nElemPtr = nElem;
    if (result != TCL_OK) {
        ckfree((char *)array);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToData --
 *
 *      Given a Tcl list of numeric expression representing the element
 *      values, convert into an array of double precision values. In
 *      addition, the minimum and maximum values are saved.  Since
 *      elastic values are allow (values which translate to the
 *      min/max of the graph), we must try to get the non-elastic
 *      minimum and maximum.
 *
 * Parameters:
 *      ClientData clientData - Type of axis vector to fill
 *      Tcl_Interp *interp - Interpreter to send results back to.
 *      Tk_Window tkwin - Not used.
 *      const char *string - Tcl list of expressions
 *      char *widgRec - Element record
 *      Tcl_Size offset - Offset of vector in Element record
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
static int StringToData(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                        Tcl_Size offset) {
    Element *elemPtr = (Element *)(widgRec);
    ElemVector *vPtr = (ElemVector *)(widgRec + offset);

    FreeDataVector(vPtr);
    if (Rbc_VectorExists2(interp, string)) {
        Rbc_VectorId clientId;

        clientId = Rbc_AllocVectorId(interp, string);
        if (Rbc_GetVectorById(interp, clientId, &vPtr->vecPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        Rbc_SetVectorChangedProc(clientId, VectorChangedProc, vPtr);
        vPtr->elemPtr = elemPtr;
        vPtr->clientId = clientId;
        SyncElemVector(vPtr);
        elemPtr->flags |= MAP_ITEM;
    } else {
        double *newArr;
        int nValues;

        if (EvalExprList(interp, string, &nValues, &newArr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (nValues > 0) {
            vPtr->valueArr = newArr;
        }
        vPtr->nValues = nValues;
        FindRange(vPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DataToString --
 *
 *      Convert the vector of floating point values into a Tcl list.
 *
 * Parameters:
 *      ClientData clientData - Type of axis vector to print
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Element record
 *      Tcl_Size offset - Offset of vector in Element record
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
static const char *DataToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                Tcl_FreeProc **freeProcPtr) {
    ElemVector *vPtr = (ElemVector *)(widgRec + offset);
    Element *elemPtr = (Element *)(widgRec);
    Tcl_DString dString;
    char *result;
    char string[TCL_DOUBLE_SPACE + 1];
    double *p, *endPtr;

    if (vPtr->clientId != NULL) {
        return Rbc_NameOfVectorId(vPtr->clientId);
    }
    if (vPtr->nValues == 0) {
        return "";
    }
    Tcl_DStringInit(&dString);
    endPtr = vPtr->valueArr + vPtr->nValues;
    for (p = vPtr->valueArr; p < endPtr; p++) {
        Tcl_PrintDouble(elemPtr->graphPtr->interp, *p, string);
        Tcl_DStringAppendElement(&dString, string);
    }
    result = Tcl_DStringValue(&dString);

    /*
     * If memory wasn't allocated for the dynamic string, do it here (it's
     * currently on the stack), so that Tcl can free it normally.
     */
    if (result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToDataPairs --
 *
 *      This procedure is like StringToData except that it interprets
 *      the list of numeric expressions as X Y coordinate pairs.  The
 *      minimum and maximum for both the X and Y vectors are
 *      determined.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - Tcl list of numeric expressions
 *      char *widgRec - Element record
 *      Tcl_Size offset - Not used.
 *
 * Results:
 *      The return value is a standard Tcl result.  The vectors are
 *      passed back via the widget record (elemPtr).
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToDataPairs(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string,
                             char *widgRec, Tcl_Size offset) {
    Element *elemPtr = (Element *)widgRec;
    int nElem;
    unsigned int newSize;
    double *newArr;

    if (EvalExprList(interp, string, &nElem, &newArr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (nElem & 1) {
        Tcl_AppendResult(interp, "odd number of data points", (char *)NULL);
        ckfree((char *)newArr);
        return TCL_ERROR;
    }
    nElem /= 2;
    newSize = nElem * sizeof(double);

    FreeDataVector(&elemPtr->x);
    FreeDataVector(&elemPtr->y);

    elemPtr->x.valueArr = (double *)ckalloc(newSize);
    elemPtr->y.valueArr = (double *)ckalloc(newSize);
    assert(elemPtr->x.valueArr && elemPtr->y.valueArr);
    elemPtr->x.nValues = elemPtr->y.nValues = nElem;

    if (newSize > 0) {
        register double *dataPtr;
        register int i;

        for (dataPtr = newArr, i = 0; i < nElem; i++) {
            elemPtr->x.valueArr[i] = *dataPtr++;
            elemPtr->y.valueArr[i] = *dataPtr++;
        }
        ckfree((char *)newArr);
        FindRange(&elemPtr->x);
        FindRange(&elemPtr->y);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DataPairsToString --
 *
 *      Convert pairs of floating point values in the X and Y arrays
 *      into a Tcl list.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Not used.
 *      Tcl_FreeProc **freeProcPtr - Memory deallocation scheme to use
 *
 * Results:
 *      The return value is a string (Tcl list).
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *DataPairsToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                     Tcl_FreeProc **freeProcPtr) {
    Element *elemPtr = (Element *)widgRec;
    Tcl_Interp *interp = elemPtr->graphPtr->interp;
    int i;
    int length;
    char *result;
    char string[TCL_DOUBLE_SPACE + 1];
    Tcl_DString dString;

    length = NumberOfPoints(elemPtr);
    if (length < 1) {
        return "";
    }
    Tcl_DStringInit(&dString);
    for (i = 0; i < length; i++) {
        Tcl_PrintDouble(interp, elemPtr->x.valueArr[i], string);
        Tcl_DStringAppendElement(&dString, string);
        Tcl_PrintDouble(interp, elemPtr->y.valueArr[i], string);
        Tcl_DStringAppendElement(&dString, string);
    }
    result = Tcl_DStringValue(&dString);

    /*
     * If memory wasn't allocated for the dynamic string, do it here
     * (it's currently on the stack), so that Tcl can free it
     * normally.
     */
    if (result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
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
 * Rbc_StringToStyles --
 *
 *      Parse the list of style names.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to.
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing style list
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of symbol type field in record
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_StringToStyles(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                       Tcl_Size offset) {
    Rbc_Chain *palette = *(Rbc_Chain **)(widgRec + offset);
    Rbc_ChainLink *linkPtr;
    Element *elemPtr = (Element *)(widgRec);
    PenStyle *stylePtr;
    const char **elemArr;
    Tcl_Size nStyles;
    register int i;
    size_t size = (size_t)clientData;

    elemArr = NULL;
    Rbc_FreePalette(elemPtr->graphPtr, palette);
    if ((string == NULL) || (*string == '\0')) {
        nStyles = 0;
    } else if (Tcl_SplitList(interp, string, &nStyles, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }
    /* Reserve the first entry for the "normal" pen. We'll set the
     * style later */
    linkPtr = Rbc_ChainFirstLink(palette);
    if (linkPtr == NULL) {
        linkPtr = Rbc_ChainAllocLink(size);
        Rbc_ChainLinkBefore(palette, linkPtr, NULL);
    }
    stylePtr = Rbc_ChainGetValue(linkPtr);
    stylePtr->penPtr = elemPtr->normalPenPtr;

    for (i = 0; i < nStyles; i++) {
        linkPtr = Rbc_ChainAllocLink(size);
        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->weight.min = (double)i;
        stylePtr->weight.max = (double)i + 1.0;
        stylePtr->weight.range = 1.0;
        if (GetPenStyle(elemPtr->graphPtr, elemArr[i], elemPtr->classUid, (PenStyle *)stylePtr) != TCL_OK) {
            ckfree((char *)elemArr);
            Rbc_FreePalette(elemPtr->graphPtr, palette);
            return TCL_ERROR;
        }
        Rbc_ChainLinkBefore(palette, linkPtr, NULL);
    }
    if (elemArr != NULL) {
        ckfree((char *)elemArr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_StylesToString --
 *
 *      Convert the style information into a string.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Not used.
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string representing the style information is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
const char *Rbc_StylesToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                               Tcl_FreeProc **freeProcPtr) {
    Rbc_Chain *palette = *(Rbc_Chain **)(widgRec + offset);
    Tcl_DString dString;
    char *result;
    Rbc_ChainLink *linkPtr;

    Tcl_DStringInit(&dString);
    linkPtr = Rbc_ChainFirstLink(palette);
    if (linkPtr != NULL) {
        Element *elemPtr = (Element *)(widgRec);
        char string[TCL_DOUBLE_SPACE + 1];
        Tcl_Interp *interp;
        PenStyle *stylePtr;

        interp = elemPtr->graphPtr->interp;
        for (linkPtr = Rbc_ChainNextLink(linkPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            Tcl_DStringStartSublist(&dString);
            Tcl_DStringAppendElement(&dString, stylePtr->penPtr->name);
            Tcl_PrintDouble(interp, stylePtr->weight.min, string);
            Tcl_DStringAppendElement(&dString, string);
            Tcl_PrintDouble(interp, stylePtr->weight.max, string);
            Tcl_DStringAppendElement(&dString, string);
            Tcl_DStringEndSublist(&dString);
        }
    }
    result = RbcStrdup(Tcl_DStringValue(&dString));
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
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
static int GetIndex(Tcl_Interp *interp, Element *elemPtr, char *string, int *indexPtr) {
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
        Tcl_AppendResult(graphPtr->interp, "can't find element \"", name, "\" in \"", Tk_PathName(graphPtr->tkwin),
                         "\"", (char *)NULL);
        return TCL_ERROR;
    }
    *elemPtrPtr = (Element *)Tcl_GetHashValue(hPtr);
    return TCL_OK;
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

    Tk_FreeOptions(elemPtr->specsPtr, (char *)elemPtr, graphPtr->display, 0);
    /*
     * Call the element's own destructor to release the memory and
     * resources allocated for it.
     */
    (*elemPtr->procsPtr->destroyProc)(graphPtr, elemPtr);

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
static int CreateElement(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], Rbc_Uid classUid) {
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

    if (Rbc_ConfigureWidgetComponent(interp, graphPtr->tkwin, elemPtr->name, "Element", elemPtr->specsPtr, objc - 4,
                                     objv + 4, (char *)elemPtr, 0) != TCL_OK) {
        DestroyElement(graphPtr, elemPtr);
        return TCL_ERROR;
    }
    (*elemPtr->procsPtr->configProc)(graphPtr, elemPtr);
    Rbc_ChainPrepend(graphPtr->elements.displayList, elemPtr);

    if (!elemPtr->hidden) {
        /* If the new element isn't hidden then redraw the graph.  */
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    elemPtr->flags |= MAP_ITEM;
    graphPtr->flags |= RESET_AXES;
    Tcl_SetResult(interp, elemPtr->name, TCL_VOLATILE);
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
static int ActivateOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    register int i;
    int *activeArr;
    int nActiveIndices;

    if (objc == 3) {
        register Tcl_HashEntry *hPtr;
        Tcl_HashSearch cursor;
        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

        /* List all the currently active elements */
        for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &cursor); hPtr != NULL;
             hPtr = Tcl_NextHashEntry(&cursor)) {
            elemPtr = (Element *)Tcl_GetHashValue(hPtr);
            if (elemPtr->flags & ELEM_ACTIVE) {
                Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(elemPtr->name, -1));
            }
        }
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    if (NameToElement(graphPtr, objv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR; /* Can't find named element */
    }
    elemPtr->flags |= ELEM_ACTIVE | ACTIVE_PENDING;

    activeArr = NULL;
    nActiveIndices = -1;
    if (objc > 4) {
        register int *activePtr;

        nActiveIndices = objc - 4;
        activePtr = activeArr = (int *)ckalloc(sizeof(int) * nActiveIndices);
        assert(activeArr);
        for (i = 4; i < objc; i++) {
            if (GetIndex(interp, elemPtr, Tcl_GetString(objv[i]), activePtr) != TCL_OK) {
                return TCL_ERROR;
            }
            activePtr++;
        }
    }
    if (elemPtr->activeIndices != NULL) {
        ckfree((char *)elemPtr->activeIndices);
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
static int BindOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
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
static int CreateOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
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
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;

    if (NameToElement(graphPtr, objv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR; /* Can't find named element */
    }
    if (Tk_ConfigureValue(interp, graphPtr->tkwin, elemPtr->specsPtr, (char *)elemPtr, Tcl_GetString(objv[4]), 0) !=
        TCL_OK) {
        return TCL_ERROR;
    }
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
static int ClosestOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    ClosestSearch search;
    int i, x, y;
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
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    int flags;
    int numNames, numOpts;
    Tcl_Obj *const *options;
    register int i;

    /* Figure out where the option value pairs begin */
    objc -= 3;
    objv += 3;
    for (i = 0; i < objc; i++) {
        const char *str = Tcl_GetString(objv[i]);
        if (str[0] == '-') {
            break;
        }
        if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR; /* Can't find named element */
        }
    }
    numNames = i;              /* Number of element names specified */
    numOpts = objc - i;        /* Number of options specified */
    options = objv + numNames; /* Start of options in argv  */

    for (i = 0; i < numNames; i++) {
        //    const char *str = Tcl_GetString(objv[i]);
        NameToElement(graphPtr, objv[i], &elemPtr);
        flags = TK_CONFIG_ARGV_ONLY;
        if (numOpts == 0) {
            return Tk_ConfigureInfo(interp, graphPtr->tkwin, elemPtr->specsPtr, (char *)elemPtr, (char *)NULL, flags);
        } else if (numOpts == 1) {
            return Tk_ConfigureInfo(interp, graphPtr->tkwin, elemPtr->specsPtr, (char *)elemPtr,
                                    Tcl_GetString(options[0]), flags);
        }
        if (Tk_ConfigureWidget(interp, graphPtr->tkwin, elemPtr->specsPtr, numOpts, options, elemPtr, flags) !=
            TCL_OK) {
            return TCL_ERROR;
        }
        if ((*elemPtr->procsPtr->configProc)(graphPtr, elemPtr) != TCL_OK) {
            return TCL_ERROR; /* Failed to configure element */
        }
        if (Rbc_ConfigModified(interp, elemPtr->specsPtr, "-hide", (char *)NULL)) {
            Rbc_ChainLink *linkPtr;

            for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
                 linkPtr = Rbc_ChainNextLink(linkPtr)) {
                if (elemPtr == Rbc_ChainGetValue(linkPtr)) {
                    break;
                }
            }
            if ((elemPtr->hidden) != (linkPtr == NULL)) {

                /* The element's "hidden" variable is out of sync with
                 * the display list. [That's what you get for having
                 * two ways to do the same thing.]  This affects what
                 * elements are considered for axis ranges and
                 * displayed in the legend. Update the display list by
                 * either by adding or removing the element.  */

                if (linkPtr == NULL) {
                    Rbc_ChainPrepend(graphPtr->elements.displayList, elemPtr);
                } else {
                    Rbc_ChainDeleteLink(graphPtr->elements.displayList, linkPtr);
                }
            }
            graphPtr->flags |= RESET_AXES;
            elemPtr->flags |= MAP_ITEM;
        }
        /* If data points or axes have changed, reset the axes (may
         * affect autoscaling) and recalculate the screen points of
         * the element. */

        if (Rbc_ConfigModified(interp, elemPtr->specsPtr, "-*data", "-map*", "-x", "-y", (char *)NULL)) {
            graphPtr->flags |= RESET_WORLD;
            elemPtr->flags |= MAP_ITEM;
        }
        /* The new label may change the size of the legend */
        if (Rbc_ConfigModified(interp, elemPtr->specsPtr, "-label", (char *)NULL)) {
            graphPtr->flags |= (MAP_WORLD | REDRAW_WORLD);
        }
    }
    /* Update the pixmap if any configuration option changed */
    graphPtr->flags |= (REDRAW_BACKING_STORE | DRAW_MARGINS);
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
static int DeactivateOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    register int i;

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
static int DeleteOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    register int i;

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
static int ExistsOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
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
static int GetOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
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
static int NamesOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_HashSearch cursor;
    register Tcl_HashEntry *hPtr;
    register int i;
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
static int ShowOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
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
static int TypeOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, int objc, Tcl_Obj *const objv[]) {
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
static Rbc_OpSpec elemOps[] = {{"activate", (Rbc_Op)ActivateOp, 3, 0, "?elemName? ?index...?"},
                               {"bind", (Rbc_Op)BindOp, 3, 6, "elemName sequence command"},
                               {"cget", (Rbc_Op)CgetOp, 5, 5, "elemName option"},
                               {"closest", (Rbc_Op)ClosestOp, 6, 0, "x y varName ?option value?... ?elemName?..."},
                               {"configure", (Rbc_Op)ConfigureOp, 4, 0, "elemName ?elemName?... ?option value?..."},
                               {"create", (Rbc_Op)CreateOp, 4, 0, "elemName ?option value?..."},
                               {"deactivate", (Rbc_Op)DeactivateOp, 3, 0, "?elemName?..."},
                               {"delete", (Rbc_Op)DeleteOp, 3, 0, "?elemName?..."},
                               {"exists", (Rbc_Op)ExistsOp, 4, 4, "elemName"},
                               {"get", (Rbc_Op)GetOp, 4, 4, "name"},
                               {"names", (Rbc_Op)NamesOp, 3, 0, "?pattern?..."},
                               {"show", (Rbc_Op)ShowOp, 3, 4, "?elemList?"},
                               {"type", (Rbc_Op)TypeOp, 4, 4, "elemName"},
                               RBC_OPSPEC_END};

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
int Rbc_ElementOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], Rbc_Uid type) {
    Rbc_Op proc;
    int result;

    proc = Rbc_GetOpFromObj(interp, elemOps, RBC_OP_ARG2, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    result = (*proc)(graphPtr, interp, type, objc, objv);
    return result;
}
