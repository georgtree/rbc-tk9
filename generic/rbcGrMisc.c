/*
 * rbcGrMisc.c --
 *
 *      Graph coordinate parsing, color-pair options and plot extents.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"
#include "rbcGrTrack.h"

#include <math.h>

static Tk_OptionParseProc StringToColorPair;
static Tk_OptionPrintProc ColorPairToString;
Tk_CustomOption rbcColorPairOption = {StringToColorPair, ColorPairToString, (ClientData)0};

static int GetColorPair(Tcl_Interp *interp, Tk_Window tkwin, const char *fgStr, const char *bgStr, ColorPair *pairPtr,
                        int allowDefault);
static const char *NameOfColor(XColor *colorPtr);

/* ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetXY --
 *
 *      Parses a position in the form "@x,y".
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      Tk_Window tkwin    - Window used to convert Tk distance units.
 *      const char *string - Position string.
 *      int *xPtr          - Receives the X coordinate.
 *      int *yPtr          - Receives the Y coordinate.
 *
 * Results:
 *      Returns TCL_OK and stores the converted coordinates in xPtr
 *      and yPtr on success.  Returns TCL_ERROR and sets the interpreter
 *      result if the position is malformed or cannot be converted.
 *
 * Side Effects:
 *      May set the interpreter result.
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetXY(Tcl_Interp *interp, Tk_Window tkwin, const char *string, int *xPtr, int *yPtr) {
    const char *comma;
    Tcl_DString xString;
    int result;
    int x, y;

    if ((string == NULL) || (*string == '\0')) {
        *xPtr = *yPtr = -SHRT_MAX;
        return TCL_OK;
    }
    if (*string != '@') {
        goto badFormat;
    }
    comma = strchr(string + 1, ',');
    if (comma == NULL) {
        goto badFormat;
    }
    /*
     * Copy the X component because string is read-only.  The previous
     * implementation temporarily replaced the comma with a NUL byte.
     */
    Tcl_DStringInit(&xString);
    Tcl_DStringAppend(&xString, string + 1, (Tcl_Size)(comma - (string + 1)));
    result = Tk_GetPixels(interp, tkwin, Tcl_DStringValue(&xString), &x);
    Tcl_DStringFree(&xString);
    if (result == TCL_OK) {
        result = Tk_GetPixels(interp, tkwin, comma + 1, &y);
    }
    if (result != TCL_OK) {
        Rbc_AppendResultStrings(interp, ": can't parse position \"", string, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    *xPtr = x;
    *yPtr = y;
    return TCL_OK;

badFormat:
    Rbc_AppendResultStrings(interp, "bad position \"", string, "\": should be \"@x,y\"", (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * GetColorPair --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      const char *fgStr
 *      const char *bgStr
 *      ColorPair *pairPtr
 *      int allowDefault
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int GetColorPair(Tcl_Interp *interp, Tk_Window tkwin, const char *fgStr, const char *bgStr, ColorPair *pairPtr,
                        int allowDefault) {
    size_t length;
    XColor *fgColor, *bgColor;

    length = strlen(fgStr);
    if (fgStr[0] == '\0') {
        fgColor = NULL;
    } else if ((allowDefault) && (fgStr[0] == 'd') && (strncmp(fgStr, "defcolor", length) == 0)) {
        fgColor = COLOR_DEFAULT;
    } else {
        fgColor = Tk_GetColor(interp, tkwin, Tk_GetUid(fgStr));
        if (fgColor == NULL) {
            return TCL_ERROR;
        }
    }
    length = strlen(bgStr);
    if (bgStr[0] == '\0') {
        bgColor = NULL;
    } else if ((allowDefault) && (bgStr[0] == 'd') && (strncmp(bgStr, "defcolor", length) == 0)) {
        bgColor = COLOR_DEFAULT;
    } else {
        bgColor = Tk_GetColor(interp, tkwin, Tk_GetUid(bgStr));
        if (bgColor == NULL) {
            return TCL_ERROR;
        }
    }
    pairPtr->fgColor = fgColor;
    pairPtr->bgColor = bgColor;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeColorPair --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ColorPair *pairPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeColorPair(ColorPair *pairPtr) {
    if ((pairPtr->bgColor != NULL) && (pairPtr->bgColor != COLOR_DEFAULT)) {
        Tk_FreeColor(pairPtr->bgColor);
    }
    if ((pairPtr->fgColor != NULL) && (pairPtr->fgColor != COLOR_DEFAULT)) {
        Tk_FreeColor(pairPtr->fgColor);
    }
    pairPtr->bgColor = pairPtr->fgColor = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToColorPair --
 *
 *      Convert the color names into pair of XColor pointers.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing color
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of color field in record
 *
 * Results:
 *      A standard Tcl result.  The color pointer is written into the
 *      widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToColorPair(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string,
                             char *widgRec, Tcl_Size offset) {
    ColorPair *pairPtr = (ColorPair *)(widgRec + offset);
    ColorPair sample;
    Tcl_Size allowDefault = (Tcl_Size)clientData;

    sample.fgColor = sample.bgColor = NULL;
    if ((string != NULL) && (*string != '\0')) {
        Tcl_Size nColors;
        const char **colors;
        int result;

        if (Tcl_SplitList(interp, string, &nColors, &colors) != TCL_OK) {
            return TCL_ERROR;
        }
        switch (nColors) {
        case 0:
            result = TCL_OK;
            break;
        case 1:
            result = GetColorPair(interp, tkwin, colors[0], "", &sample, (int)allowDefault);
            break;
        case 2:
            result = GetColorPair(interp, tkwin, colors[0], colors[1], &sample, (int)allowDefault);
            break;
        default:
            result = TCL_ERROR;
            Rbc_AppendResultStrings(interp, "too many names in colors list", (char *)NULL);
        }
        ckfree((char *)colors);
        if (result != TCL_OK) {
            return TCL_ERROR;
        }
    }
    Rbc_FreeColorPair(pairPtr);
    *pairPtr = sample;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NameOfColor --
 *
 *      Convert the color option value into a string.
 *
 * Parameters:
 *      XColor *colorPtr
 *
 * Results:
 *      The static string representing the color option is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *NameOfColor(XColor *colorPtr) {
    if (colorPtr == NULL) {
        return "";
    } else if (colorPtr == COLOR_DEFAULT) {
        return "defcolor";
    } else {
        return Tk_NameOfColor(colorPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ColorPairToString --
 *
 *      Convert the color pairs into color names.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of symbol type field in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string representing the symbol color is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *ColorPairToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                     Tcl_FreeProc **freeProcPtr) {
    ColorPair *pairPtr = (ColorPair *)(widgRec + offset);
    Tcl_DString dString;
    char *result;

    Tcl_DStringInit(&dString);
    Tcl_DStringAppendElement(&dString, NameOfColor(pairPtr->fgColor));
    Tcl_DStringAppendElement(&dString, NameOfColor(pairPtr->bgColor));
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
 * Rbc_GraphExtents --
 *
 *      Generates a bounding box representing the plotting area of
 *      the graph. This data structure is used to clip the points and
 *      line segments of the line element.
 *
 *      The clip region is the plotting area plus such arbitrary extra
 *      space.  The reason we clip with a bounding box larger than the
 *      plot area is so that symbols will be drawn even if their center
 *      point isn't in the plotting area.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Extents2D *extsPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The bounding box is filled with the dimensions of the plotting
 *      area.
 *
 *----------------------------------------------------------------------
 */
void Rbc_GraphExtents(Graph *graphPtr, Extents2D *extsPtr) {
    extsPtr->left = (double)graphPtr->hOffset - (double)graphPtr->padX.side1;
    extsPtr->top = (double)graphPtr->vOffset - (double)graphPtr->padY.side1;
    extsPtr->right = (double)graphPtr->hOffset + (double)graphPtr->hRange + (double)graphPtr->padX.side2;
    extsPtr->bottom = (double)graphPtr->vOffset + (double)graphPtr->vRange + (double)graphPtr->padY.side2;
    if (graphPtr->trackScroll && graphPtr->activeTrack != NULL) {
        extsPtr->left = MAX(extsPtr->left, graphPtr->activeTrack->left);
        extsPtr->right = MIN(extsPtr->right, graphPtr->activeTrack->right);
        extsPtr->top = MAX(extsPtr->top, graphPtr->activeTrack->top);
        extsPtr->bottom = MIN(extsPtr->bottom, graphPtr->activeTrack->bottom);
    }
}
