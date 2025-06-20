/*
 * rbcConfig.c --
 *
 *      This module implements custom configuration options for the rbc
 *      toolkit.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include <stdarg.h>

#include "rbcTile.h"

static Tk_OptionParseProc StringToFill;
static Tk_OptionPrintProc FillToString;
Tk_CustomOption rbcFillOption = {StringToFill, FillToString, (ClientData)0};

static Tk_OptionParseProc StringToPad;
static Tk_OptionPrintProc PadToString;
Tk_CustomOption rbcPadOption = {StringToPad, PadToString, (ClientData)0};

static Tk_OptionParseProc StringToDistance;
static Tk_OptionPrintProc DistanceToString;
Tk_CustomOption rbcDistanceOption = {StringToDistance, DistanceToString, (ClientData)PIXELS_NONNEGATIVE};
Tk_CustomOption rbcPositiveDistanceOption = {StringToDistance, DistanceToString, (ClientData)PIXELS_POSITIVE};
Tk_CustomOption rbcAnyDistanceOption = {StringToDistance, DistanceToString, (ClientData)PIXELS_ANY};

static Tk_OptionParseProc StringToCount;
static Tk_OptionPrintProc CountToString;
Tk_CustomOption rbcCountOption = {StringToCount, CountToString, (ClientData)COUNT_NONNEGATIVE};
Tk_CustomOption rbcPositiveCountOption = {StringToCount, CountToString, (ClientData)COUNT_POSITIVE};

static Tk_OptionParseProc StringToDashes;
static Tk_OptionPrintProc DashesToString;
Tk_CustomOption rbcDashesOption = {StringToDashes, DashesToString, (ClientData)0};

static Tk_OptionParseProc StringToShadow;
static Tk_OptionPrintProc ShadowToString;
Tk_CustomOption rbcShadowOption = {StringToShadow, ShadowToString, (ClientData)0};

static Tk_OptionParseProc StringToUid;
static Tk_OptionPrintProc UidToString;
Tk_CustomOption rbcUidOption = {StringToUid, UidToString, (ClientData)0};

static Tk_OptionParseProc StringToState;
static Tk_OptionPrintProc StateToString;
Tk_CustomOption rbcStateOption = {StringToState, StateToString, (ClientData)0};

static Tk_OptionParseProc StringToList;
static Tk_OptionPrintProc ListToString;
Tk_CustomOption rbcListOption = {StringToList, ListToString, (ClientData)0};

static Tk_OptionParseProc StringToTile;
static Tk_OptionPrintProc TileToString;
Tk_CustomOption rbcTileOption = {StringToTile, TileToString, (ClientData)0};

static int GetInt(Tcl_Interp *, const char *, int, int *);

#ifdef notdef
static Tk_ConfigSpec *Rbc_GetCachedSpecs(Tcl_Interp *, const Tk_ConfigSpec *);
#endif /* notdef */
/*
 *----------------------------------------------------------------------
 *
 * Rbc_NameOfFill --
 *
 *      Converts the integer representing the fill direction into a string.
 *
 * Parameters:
 *      int fill
 *      void *b
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
char *Rbc_NameOfFill(int fill) {
    switch (fill) {
    case FILL_X:
        return "x";
    case FILL_Y:
        return "y";
    case FILL_NONE:
        return "none";
    case FILL_BOTH:
        return "both";
    default:
        return "unknown value";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * StringToFill --
 *
 *      Converts the fill style string into its numeric representation.
 *
 *      Valid style strings are:
 *
 *         "none"   Use neither plane.
 *         "x"      X-coordinate plane.
 *         "y"        Y-coordinate plane.
 *         "both"   Use both coordinate planes.
 *
 * Parameters:
 *      void *clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - Fill style string
 *      char *widgRec - Cubicle structure record
 *      Tcl_Size offset - Offset of style in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToFill(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                        Tcl_Size offset) {
    int *fillPtr = (int *)(widgRec + offset);
    unsigned int length;
    char c;

    c = string[0];
    length = strlen(string);
    if ((c == 'n') && (strncmp(string, "none", length) == 0)) {
        *fillPtr = FILL_NONE;
    } else if ((c == 'x') && (strncmp(string, "x", length) == 0)) {
        *fillPtr = FILL_X;
    } else if ((c == 'y') && (strncmp(string, "y", length) == 0)) {
        *fillPtr = FILL_Y;
    } else if ((c == 'b') && (strncmp(string, "both", length) == 0)) {
        *fillPtr = FILL_BOTH;
    } else {
        Tcl_AppendResult(interp, "bad argument \"", string, "\": should be \"none\", \"x\", \"y\", or \"both\"",
                         (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FillToString --
 *
 *      Returns the fill style string based upon the fill flags.
 *
 * Parameters:
 *      void *clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset of fill in widget record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The fill style string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *FillToString(void *clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                Tcl_FreeProc **freeProcPtr) {
    int fill = *(int *)(widgRec + offset);

    return Rbc_NameOfFill(fill);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_StringToFlag --
 *
 *      Converts the fill style string into its numeric representation.
 *
 * Parameters:
 *      void *clientData - Bit mask to be tested in status word
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - Fill style string
 *      char *widgRec - Cubicle structure record
 *      Tcl_Size offset - Offset of style in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_StringToFlag(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                     Tcl_Size offset) {
    unsigned long mask = PTR2UINT(clientData); /* Bit to be tested */
    int *flagPtr = (int *)(widgRec + offset);
    int bool;

    if (Tcl_GetBoolean(interp, string, &bool) != TCL_OK) {
        return TCL_ERROR;
    }
    if (bool) {
        *flagPtr |= mask;
    } else {
        *flagPtr &= ~mask;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FlagToString --
 *
 *      Returns the fill style string based upon the fill flags.
 *
 * Parameters:
 *      void *clientData - Bit mask to be test in status word
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset of fill in widget record
 *      Tcl_FreeProc **freeProcPtr - Not Used.
 *
 * Results:
 *      The fill style string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
const char *Rbc_FlagToString(void *clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                             Tcl_FreeProc **freeProcPtr) {
    /* Bit to be tested */
    unsigned int mask = PTR2UINT(clientData);
    unsigned int bool = *(unsigned int *)(widgRec + offset);

    return (bool &mask) ? "1" : "0";
}

static int DoCheck(Tcl_Interp *interp, int length, int check) {
    if (length >= SHRT_MAX) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad distance \"%d\": "
                                               "too big to represent",
                                               length));
        return TCL_ERROR;
    }
    switch (check) {
    case PIXELS_NONNEGATIVE:
        if (length < 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad distance \"%d\": "
                                                   "can't be negative",
                                                   length));
            return TCL_ERROR;
        }
        break;
    case PIXELS_POSITIVE:
        if (length <= 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad distance \"%d\": "
                                                   "must be positive",
                                                   length));
            return TCL_ERROR;
        }
        break;
    case PIXELS_ANY:
        break;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetPixels --
 *
 *      Like Tk_GetPixels, but checks for negative, zero.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      const char *string
 *      int check - Can be PIXELS_POSITIVE, PIXELS_NONNEGATIVE, or PIXELS_ANY
 *      int *valuePtr
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetPixels(Tcl_Interp *interp, Tk_Window tkwin, const char *string, int check, int *valuePtr) {
    int length;

    if (Tk_GetPixels(interp, tkwin, string, &length) != TCL_OK) {
        return TCL_ERROR;
    }
    if (DoCheck(interp, length, check) != TCL_OK) {
        return TCL_ERROR;
    }
    *valuePtr = length;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetPixelsFromObj --
 *
 *      Like Tk_GetPixelsFromObj, but checks for negative, zero.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      Tcl_Obj *pixelsObj
 *      int check - Can be PIXELS_POSITIVE, PIXELS_NONNEGATIVE, or PIXELS_ANY
 *      int *valuePtr
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetPixelsFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *pixelsObj, int check, int *valuePtr) {
    int length;

    if (Tk_GetPixelsFromObj(interp, tkwin, pixelsObj, &length) != TCL_OK) {
        return TCL_ERROR;
    }
    if (DoCheck(interp, length, check) != TCL_OK) {
        return TCL_ERROR;
    }
    *valuePtr = length;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToDistance --
 *
 *      Like TK_CONFIG_PIXELS, but adds an extra check for negative
 *      values.
 *
 * Parameters:
 *      void *clientData - Indicated how to check distance
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Window
 *      const char *string - Pixel value string
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of pixel size in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToDistance(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                            Tcl_Size offset) {
    int *valuePtr = (int *)(widgRec + offset);
    Tcl_Size data = (Tcl_Size)clientData;

    return Rbc_GetPixels(interp, tkwin, string, data, valuePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DistanceToString --
 *
 *      Returns the string representing the positive pixel size.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset in widget record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The pixel size string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *DistanceToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                    Tcl_FreeProc **freeProcPtr) {
    int value = *(int *)(widgRec + offset);
    char *result;

    result = RbcStrdup(Rbc_Itoa(value));
    assert(result);
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * GetInt --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *string
 *      int check - Can be COUNT_POSITIVE, COUNT_NONNEGATIVE, or COUNT_ANY
 *      int *valuePtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int GetInt(Tcl_Interp *interp, const char *string, int check, int *valuePtr) {
    int count;

    if (Tcl_GetInt(interp, string, &count) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (check) {
    case COUNT_NONNEGATIVE:
        if (count < 0) {
            Tcl_AppendResult(interp, "bad value \"", string, "\": ", "can't be negative", (char *)NULL);
            return TCL_ERROR;
        }
        break;
    case COUNT_POSITIVE:
        if (count <= 0) {
            Tcl_AppendResult(interp, "bad value \"", string, "\": ", "must be positive", (char *)NULL);
            return TCL_ERROR;
        }
        break;
    case COUNT_ANY:
        break;
    }
    *valuePtr = count;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToCount --
 *
 *      Like TK_CONFIG_PIXELS, but adds an extra check for negative
 *      values.
 *
 * Parameters:
 *      void *clientData - Indicated how to check distance
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - Pixel value string
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of pixel size in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToCount(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                         Tcl_Size offset) {
    int *valuePtr = (int *)(widgRec + offset);
    Tcl_Size data = (Tcl_Size)clientData;

    return GetInt(interp, string, data, valuePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * CountToString --
 *
 *      Returns the string representing the positive pixel size.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset in widget record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The pixel size string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *CountToString(void *clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                 Tcl_FreeProc **freeProcPtr) {
    int value = *(int *)(widgRec + offset);
    char *result;

    result = RbcStrdup(Rbc_Itoa(value));
    assert(result);
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToPad --
 *
 *      Convert a string into two pad values.  The string may be in one of
 *      the following forms:
 *
 *          n      - n is a non-negative integer. This sets both
 *                   pad values to n.
 *          {n m}  - both n and m are non-negative integers. side1
 *                   is set to n, side2 is set to m.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Window
 *      const char *string - Pixel value string
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of pad in widget
 *
 * Results:
 *      If the string is successfully converted, TCL_OK is returned.
 *      Otherwise, TCL_ERROR is returned and an error message is left in
 *      the interprwter result.
 *
 * Side Effects:
 *      The padding structure passed is updated with the new values.
 *
 *----------------------------------------------------------------------
 */
static int StringToPad(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                       Tcl_Size offset) {
    Rbc_Pad *padPtr = (Rbc_Pad *)(widgRec + offset);
    int pad, result;
    Tcl_Size nElem;
    const char **padArr;

    if (Tcl_SplitList(interp, string, &nElem, &padArr) != TCL_OK) {
        return TCL_ERROR;
    }
    result = TCL_ERROR;
    if ((nElem < 1) || (nElem > 2)) {
        Tcl_AppendResult(interp, "wrong # elements in padding list", (char *)NULL);
        goto error;
    }
    if (Rbc_GetPixels(interp, tkwin, padArr[0], PIXELS_NONNEGATIVE, &pad) != TCL_OK) {
        goto error;
    }
    padPtr->side1 = pad;
    if ((nElem > 1) && (Rbc_GetPixels(interp, tkwin, padArr[1], PIXELS_NONNEGATIVE, &pad) != TCL_OK)) {
        goto error;
    }
    padPtr->side2 = pad;
    result = TCL_OK;

error:
    ckfree((char *)padArr);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * PadToString --
 *
 *     Converts the two pad values into a Tcl list.  Each pad has two
 *     pixel values.  For vertical pads, they represent the top and bottom
 *     margins.  For horizontal pads, they're the left and right margins.
 *     All pad values are non-negative integers.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Structure record
 *      Tcl_Size offset - Offset of pad in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *     The padding list is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *PadToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                               Tcl_FreeProc **freeProcPtr) {
    Rbc_Pad *padPtr = (Rbc_Pad *)(widgRec + offset);
    char *result;
    char string[200];

    sprintf(string, "%d %d", padPtr->side1, padPtr->side2);
    result = RbcStrdup(string);
    if (result == NULL) {
        return "out of memory";
    }
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToShadow --
 *
 *      Convert a string into two pad values.  The string may be in one of
 *      the following forms:
 *
 *          n      - n is a non-negative integer. This sets both
 *                   pad values to n.
 *          {n m}  - both n and m are non-negative integers. side1
 *                   is set to n, side2 is set to m.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Window
 *      const char *string - Pixel value string
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of pad in widget
 *
 * Results:
 *      If the string is successfully converted, TCL_OK is returned.
 *      Otherwise, TCL_ERROR is returned and an error message is left in
 *      the interpreter result.
 *
 * Side Effects:
 *      The padding structure passed is updated with the new values.
 *
 *----------------------------------------------------------------------
 */
static int StringToShadow(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                          Tcl_Size offset) {
    Shadow *shadowPtr = (Shadow *)(widgRec + offset);
    XColor *colorPtr;
    int dropOffset;

    colorPtr = NULL;
    dropOffset = 0;
    if ((string != NULL) && (string[0] != '\0')) {
        Tcl_Size nElem;
        const char **elemArr;

        if (Tcl_SplitList(interp, string, &nElem, &elemArr) != TCL_OK) {
            return TCL_ERROR;
        }
        if ((nElem < 1) || (nElem > 2)) {
            Tcl_AppendResult(interp, "wrong # elements in drop shadow value", (char *)NULL);
            ckfree((char *)elemArr);
            return TCL_ERROR;
        }
        colorPtr = Tk_GetColor(interp, tkwin, Tk_GetUid(elemArr[0]));
        if (colorPtr == NULL) {
            ckfree((char *)elemArr);
            return TCL_ERROR;
        }
        dropOffset = 1;
        if (nElem == 2) {
            if (Rbc_GetPixels(interp, tkwin, elemArr[1], PIXELS_NONNEGATIVE, &dropOffset) != TCL_OK) {
                Tk_FreeColor(colorPtr);
                ckfree((char *)elemArr);
                return TCL_ERROR;
            }
        }
        ckfree((char *)elemArr);
    }
    if (shadowPtr->color != NULL) {
        Tk_FreeColor(shadowPtr->color);
    }
    shadowPtr->color = colorPtr;
    shadowPtr->offset = dropOffset;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ShadowToString --
 *
 *      Converts the two pad values into a Tcl list.  Each pad has two
 *      pixel values.  For vertical pads, they represent the top and bottom
 *      margins.  For horizontal pads, they're the left and right margins.
 *      All pad values are non-negative integers.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Structure record
 *      Tcl_Size offset - Offset of pad in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The padding list is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *ShadowToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                  Tcl_FreeProc **freeProcPtr) {
    Shadow *shadowPtr = (Shadow *)(widgRec + offset);
    char *result;

    result = "";
    if (shadowPtr->color != NULL) {
        char string[200];

        sprintf(string, "%s %d", Tk_NameOfColor(shadowPtr->color), shadowPtr->offset);
        result = RbcStrdup(string);
        *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * GetDashes --
 *
 *      Converts a Tcl list of dash values into a dash list ready for
 *      use with XSetDashes.
 *
 *      A valid list dash values can have zero through 11 elements
 *      (PostScript limit).  Values must be between 1 and 255. Although
 *      a list of 0 (like the empty string) means no dashes.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *string
 *      Rbc_Dashes *dashesPtr
 *
 * Results:
 *      A standard Tcl result. If the list represented a valid dash
 *      list TCL_OK is returned and *dashesPtr* will contain the
 *      valid dash list. Otherwise, TCL_ERROR is returned and
 *      the interpreter result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int GetDashes(Tcl_Interp *interp, const char *string, Rbc_Dashes *dashesPtr) {
    if ((string == NULL) || (*string == '\0')) {
        dashesPtr->values[0] = 0;
    } else if (strcmp(string, "dash") == 0) {
        dashesPtr->values[0] = 5;
        dashesPtr->values[1] = 2;
        dashesPtr->values[2] = 0;
    } else if (strcmp(string, "dot") == 0) {
        dashesPtr->values[0] = 1;
        dashesPtr->values[1] = 0;
    } else if (strcmp(string, "dashdot") == 0) {
        dashesPtr->values[0] = 2;
        dashesPtr->values[1] = 4;
        dashesPtr->values[2] = 2;
        dashesPtr->values[3] = 0;
    } else if (strcmp(string, "dashdotdot") == 0) {
        dashesPtr->values[0] = 2;
        dashesPtr->values[1] = 4;
        dashesPtr->values[2] = 2;
        dashesPtr->values[3] = 2;
        dashesPtr->values[4] = 0;
    } else {
        Tcl_Size nValues;
        const char **strArr;
        long int value;
        register int i;

        if (Tcl_SplitList(interp, string, &nValues, &strArr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (nValues > 11) {
            /* This is the postscript limit */
            Tcl_AppendResult(interp, "too many values in dash list \"", string, "\"", (char *)NULL);
            ckfree((char *)strArr);
            return TCL_ERROR;
        }
        for (i = 0; i < nValues; i++) {
            if (Tcl_ExprLong(interp, strArr[i], &value) != TCL_OK) {
                ckfree((char *)strArr);
                return TCL_ERROR;
            }
            /*
             * Backward compatibility:
             * Allow list of 0 to turn off dashes
             */
            if ((value == 0) && (nValues == 1)) {
                break;
            }
            if ((value < 1) || (value > 255)) {
                Tcl_AppendResult(interp, "dash value \"", strArr[i], "\" is out of range", (char *)NULL);
                ckfree((char *)strArr);
                return TCL_ERROR;
            }
            dashesPtr->values[i] = (unsigned char)value;
        }
        /* Make sure the array ends with a NUL byte  */
        dashesPtr->values[i] = 0;
        ckfree((char *)strArr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToDashes --
 *
 *      Convert the list of dash values into a dashes array.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - New dash value list
 *      char *widgRec - Widget record
 *      Tcl_Size offset - offset to Dashes structure
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      The Dashes structure is updated with the new dash list.
 *
 *----------------------------------------------------------------------
 */
static int StringToDashes(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                          Tcl_Size offset) {
    Rbc_Dashes *dashesPtr = (Rbc_Dashes *)(widgRec + offset);

    return GetDashes(interp, string, dashesPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DashesToString --
 *
 *      Convert the dashes array into a list of values.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget record
 *      Tcl_Size offset - offset to Dashes structure
 *      Tcl_FreeProc **freeProcPtr - Memory deallocation scheme to use
 *
 * Results:
 *      The string representing the dashes returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *DashesToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                  Tcl_FreeProc **freeProcPtr) {
    Rbc_Dashes *dashesPtr = (Rbc_Dashes *)(widgRec + offset);
    Tcl_DString dString;
    unsigned char *p;
    char *result;

    if (dashesPtr->values[0] == 0) {
        return "";
    }
    Tcl_DStringInit(&dString);
    for (p = dashesPtr->values; *p != 0; p++) {
        Tcl_DStringAppendElement(&dString, Rbc_Itoa(*p));
    }
    result = Tcl_DStringValue(&dString);
    if (result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToUid --
 *
 *      Converts the string to a Rbc_Uid. Rbc_Uid's are hashed, reference
 *      counted strings.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - Fill style string
 *      char *widgRec - Cubicle structure record
 *      Tcl_Size offset - Offset of style in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToUid(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                       Tcl_Size offset) {
    Rbc_Uid *uidPtr = (Rbc_Uid *)(widgRec + offset);
    Rbc_Uid newId;

    newId = NULL;
    if ((string != NULL) && (*string != '\0')) {
        newId = Rbc_GetUid(string);
    }
    if (*uidPtr != NULL) {
        Rbc_FreeUid(*uidPtr);
    }
    *uidPtr = newId;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * UidToString --
 *
 *      Returns the fill style string based upon the fill flags.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of fill in widget record 
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The fill style string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *UidToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                               Tcl_FreeProc **freeProcPtr) {
    Rbc_Uid uid = *(Rbc_Uid *)(widgRec + offset);

    return (uid == NULL) ? "" : uid;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToState --
 *
 *      Converts the string to a state value. Valid states are
 *      disabled, normal.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representation of option value
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset of field in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToState(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                         Tcl_Size offset) {
    int *statePtr = (int *)(widgRec + offset);

    if (strcmp(string, "normal") == 0) {
        *statePtr = STATE_NORMAL;
    } else if (strcmp(string, "disabled") == 0) {
        *statePtr = STATE_DISABLED;
    } else if (strcmp(string, "active") == 0) {
        *statePtr = STATE_ACTIVE;
    } else {
        Tcl_AppendResult(interp, "bad state \"", string, "\": should be normal, active, or disabled", (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StateToString --
 *
 *      Returns the string representation of the state configuration field
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset of fill in widget record 
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *StateToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                 Tcl_FreeProc **freeProcPtr) {
    int state = *(int *)(widgRec + offset);

    switch (state) {
    case STATE_ACTIVE:
        return "active";
    case STATE_DISABLED:
        return "disabled";
    case STATE_NORMAL:
        return "normal";
    default:
        return "???";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * StringToList --
 *
 *      Converts the string to a list.
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      const char *string 
 *      char *widgRec
 *      Tcl_Size offset
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToList(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                        Tcl_Size offset) {
    const char ***listPtr = (const char ***)(widgRec + offset);
    const char **elemArr;
    Tcl_Size nElem;

    if (*listPtr != NULL) {
        ckfree((char *)*listPtr);
        *listPtr = NULL;
    }
    if ((string == NULL) || (*string == '\0')) {
        return TCL_OK;
    }
    if (Tcl_SplitList(interp, string, &nElem, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (nElem > 0) {
        *listPtr = elemArr;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ListToString --
 *
 *      Returns the string representation of the state configuration field
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset of fill in widget record 
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *ListToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                Tcl_FreeProc **freeProcPtr) {
    char **list = *(char ***)(widgRec + offset);
    register char **p;
    char *result;
    Tcl_DString dString;

    if (list == NULL) {
        return "";
    }
    Tcl_DStringInit(&dString);
    for (p = list; *p != NULL; p++) {
        Tcl_DStringAppendElement(&dString, *p);
    }
    result = Tcl_DStringValue(&dString);
    if (result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    Tcl_DStringFree(&dString);
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToTile --
 *
 *      Converts the name of an image into a tile.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Window on same display as tile
 *      const char *string - Name of image
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset of tile in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToTile(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                        Tcl_Size offset) {
    Rbc_Tile *tilePtr = (Rbc_Tile *)(widgRec + offset);
    Rbc_Tile tile, oldTile;

    oldTile = *tilePtr;
    tile = NULL;
    if ((string != NULL) && (*string != '\0')) {
        if (Rbc_GetTile(interp, tkwin, string, &tile) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /* Don't delete the information for the old tile, until we know
     * that we successfully allocated a new one. */
    if (oldTile != NULL) {
        Rbc_FreeTile(oldTile);
    }
    *tilePtr = tile;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TileToString --
 *
 *      Returns the name of the tile.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset of tile in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The name of the tile is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *TileToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                Tcl_FreeProc **freeProcPtr) {
    Rbc_Tile tile = *(Rbc_Tile *)(widgRec + offset);

    return Rbc_NameOfTile(tile);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ConfigModified --
 *
 *      Given the configuration specifications and one or more option
 *      patterns (terminated by a NULL), indicate if any of the matching
 *      configuration options has been reset.
 *
 * Results:
 *      Returns 1 if one of the options has changed, 0 otherwise.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_ConfigModified(Tcl_Interp *interp, Tk_ConfigSpec *specs, ...) {
#if 1

    /*
     * The way RBC checks whether options have been modified is
     * broken for Tk 8.6 (and maybe 8.5 as well). Part of the issue is
     * described in the comments for Rbc_GetCachedSpecs. Unfortunately,
     * that fix also does not suffice because Tk_ConfigureWidget has
     * another problem - it does not set the TK_CONFIG_OPTION_SPECIFIED
     * flag if the TK_CONFIG_ARGV_ONLY flag was passed to it
     * (which is needed when configuring an existing widget.) Thus
     * the check in the loop below would fail and consequently the
     * caller would think no options were re-configured and fail to
     * update the display. Fixing this is not simple, as it requires
     * either moving to the new Tk option API or cargo culting Tk internal
     * code. So we just pretend  configuration is always modified. This will
     * result in potentially unnecessary redraws but hopefully not too bad.
     *
     * BTW, the original RBC code did exactly this by always returning
     * 1 at the end. We are just cutting out the unnecessary loop if we
     * always return the constant value!
     */
    return 1;

#else

    va_list argList;
    register Tk_ConfigSpec *specPtr;
    register char *option;

    va_start(argList, specs);
    specs = Rbc_GetCachedSpecs(interp, specs);
    while ((option = va_arg(argList, char *)) != NULL) {
        for (specPtr = specs; specPtr->type != TK_CONFIG_END; specPtr++) {
            if ((Tcl_StringMatch(specPtr->argvName, option)) && (specPtr->specFlags & TK_CONFIG_OPTION_SPECIFIED)) {
                va_end(argList);
                return 1;
            }
        }
    }
    va_end(argList);
    return 0;

#endif
}

/*
 *----------------------------------------------------------------------
 *
 * FindChild --
 *
 *      Search for the named child window in a given parent window.
 *
 * Parameters:
 *      Tk_Window parent
 *      const char *name
 *
 * Results:
 *      The child Tk_Window. If the named child can't be found, NULL
 *      is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Tk_Window FindChild(Tk_Window parent, const char *name) {
    Tcl_Obj *pathNamePtr;
    Tk_Window result;

    if (!parent) {
        return NULL;
    }

    pathNamePtr = Tcl_ObjPrintf("%s.%s", Tk_PathName(parent), name);
    result = Tk_NameToWindow(NULL, Tcl_GetString(pathNamePtr), parent);
    Tcl_BounceRefCount(pathNamePtr);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ConfigureWidgetComponent --
 *
 *      Configures a component of a widget.  This is useful for
 *      widgets that have multiple components which aren't uniquely
 *      identified by a Tk_Window. It allows us, for example, set
 *      resources for axes of the graph widget. The graph really has
 *      only one window, but its convenient to specify components in a
 *      hierarchy of options.
 *
 *          *graph.x.logScale yes
 *          *graph.Axis.logScale yes
 *          *graph.temperature.scaleSymbols yes
 *          *graph.Element.scaleSymbols yes
 *
 *      This is really a hack to work around the limitations of the Tk
 *      resource database.  It creates a temporary window, needed to
 *      call Tk_ConfigureWidget, using the name of the component.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window parent - Window to associate with component
 *      char resName[] - Name of component
 *      char className[]
 *      Tk_ConfigSpec *specsPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *      char *widgRec
 *      int flags
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      A temporary window is created merely to pass to Tk_ConfigureWidget.
 *
 *----------------------------------------------------------------------
 */
int Rbc_ConfigureWidgetComponent(Tcl_Interp *interp, Tk_Window parent, char resName[], char className[],
                                 Tk_ConfigSpec *specsPtr, int objc, Tcl_Obj *const objv[], char *widgRec, int flags) {
    Tk_Window tkwin;
    int result;
    char *tempName;
    int isTemporary = FALSE;

    tempName = RbcStrdup(resName);

    /* Window name can't start with an upper case letter */
    tempName[0] = tolower(resName[0]);

    /*
     * Create component if a child window by the component's name
     * doesn't already exist.
     */
    tkwin = FindChild(parent, tempName);
    if (tkwin == NULL) {
        tkwin = Tk_CreateWindow(interp, parent, tempName, (char *)NULL);
        isTemporary = TRUE;
    }
    if (tkwin == NULL) {
        Tcl_AppendResult(interp, "can't find window in \"", Tk_PathName(parent), "\"", (char *)NULL);
        return TCL_ERROR;
    }
    assert(Tk_Depth(tkwin) == Tk_Depth(parent));
    ckfree((char *)tempName);

    Tk_SetClass(tkwin, className);
    result = Tk_ConfigureWidget(interp, tkwin, specsPtr, objc, objv, widgRec, flags);
    if (isTemporary) {
        Tk_DestroyWindow(tkwin);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_StringToEnum --
 *
 *      Converts the string into its enumerated type.
 *
 * Parameters:
 *      ClientData clientData - Vectors of valid strings.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String to match.
 *      char *widgRec - Widget record.
 *      Tcl_Size offset - Offset of field in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_StringToEnum(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                     Tcl_Size offset) {
    int *enumPtr = (int *)(widgRec + offset);
    char c;
    register char **p;
    register int i;
    int count;

    c = string[0];
    count = 0;
    for (p = (char **)clientData; *p != NULL; p++) {
        if ((c == p[0][0]) && (strcmp(string, *p) == 0)) {
            *enumPtr = count;
            return TCL_OK;
        }
        count++;
    }
    *enumPtr = -1;

    Tcl_AppendResult(interp, "bad value \"", string, "\": should be ", (char *)NULL);
    p = (char **)clientData;
    if (count > 0) {
        Tcl_AppendResult(interp, p[0], (char *)NULL);
    }
    for (i = 1; i < (count - 1); i++) {
        Tcl_AppendResult(interp, " ", p[i], ", ", (char *)NULL);
    }
    if (count > 1) {
        Tcl_AppendResult(interp, " or ", p[count - 1], ".", (char *)NULL);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_EnumToString --
 *
 *      Returns the string associated with the enumerated type.
 *
 * Parameters:
 *      ClientData clientData - List of strings.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of field in widget record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
const char *Rbc_EnumToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                             Tcl_FreeProc **freeProcPtr) {
    int value = *(int *)(widgRec + offset);
    char **p;
    int count;

    count = 0;
    for (p = (char **)clientData; *p != NULL; p++) {
        count++;
    }
    if ((value >= count) || (value < 0)) {
        return "unknown value";
    }
    p = (char **)clientData;
    return p[value];
}

/*
 * RBC routines makes use of Tk_ConfigureWidget to parse options for
 * widgets, passing in an array of Tk_ConfigSpec structures. They then
 * check the TK_CONFIG_OPTION_SPECIFIED flag in those structures to
 * figure out which options have been modified. This worked in older
 * (pre-8.6) Tk versions.
 *
 * In 8.6, Tk_ConfigureWidget does not modify the passed in
 * Tk_ConfigSpec structures. Rather it maintains an internal
 * per-interp cache that mirrors the passed in Tk_ConfigSpec array and
 * modifies the elements of the cached structure instead. Based on the
 * comments, this is intended to make the option processing
 * thread-safe (or perhaps interp-specific).
 *
 * Obviously, this breaks Tk extensions, including RBC, which expect
 * the flags field in the passed-in structure to have been
 * modified. They should be checking for changes in the *cached* array
 * and not the array they passed-in. However, I cannot figure out how
 * an extension can get a pointer to this cached array. Tk itself uses
 * GetCachedSpecs() for this purpose but this does not seem to be exported.
 *
 * We therefore have to resort to poking into Tk internals to emulate
 * the functionality of GetCachedSpecs in Rbc_GetCachedSpecs. The difference
 * from the original is that if the cached specs is not found, we do not
 * create it. Rather we assume we are running against an older Tk which
 * does not cache the specs. This works because this routine is always
 * called after Tk_ConfigureWidget which will create the cached specs
 * if it did not exist.
 *
 * Below comment from the Tk GetCachedSpecs sources
 *--------------------------------------------------------------
 *
 * GetCachedSpecs --
 *
 *    Returns a writable per-interpreter (and hence thread-local) copy of
 *    the given spec-table with (some of) the char* fields converted into
 *    Tk_Uid fields; this copy will be released when the interpreter
 *    terminates (during AssocData cleanup).
 *
 * Results:
 *    A pointer to the copied table.
 *
 * Notes:
 *    The conversion to Tk_Uid is only done the first time, when the table
 *    copy is taken. After that, the table is assumed to have Tk_Uids where
 *    they are needed. The time of deletion of the caches isn't very
 *    important unless you've got a lot of code that uses Tk_ConfigureWidget
 *    (or *Info or *Value} when the interpreter is being deleted.
 */
#ifdef notdef
static Tk_ConfigSpec *Rbc_GetCachedSpecs(Tcl_Interp *interp, /* Interpreter in which to store the cache. */
                                         const Tk_ConfigSpec *staticSpecs)
/* Value to cache a copy of; it is also used
 * as a key into the cache. */
{
    Tk_ConfigSpec *cachedSpecs = NULL;
    Tcl_HashTable *specCacheTablePtr;
    Tcl_HashEntry *entryPtr;

    specCacheTablePtr = Tcl_GetAssocData(interp, "tkConfigSpec.threadTable", NULL);
    if (specCacheTablePtr != NULL) {
        /*
         * Look up the hash entry that the constant specs are mapped to,
         * which will have the writable specs as its associated value.
         */
        entryPtr = Tcl_FindHashEntry(specCacheTablePtr, (char *)staticSpecs);
        if (entryPtr)
            cachedSpecs = Tcl_GetHashValue(entryPtr);
    }

    /* Return the cached specs if they exist else the original specs */
    return cachedSpecs ? cachedSpecs : staticSpecs;
}
#endif /* notdef */
