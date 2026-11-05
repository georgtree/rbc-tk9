/** @file rbcGrMisc.c
 * @brief Shared graph coordinate and legacy color-pair services.
 */
/** @defgroup rbcGrMisc_internals rbcGrMisc helper contracts
 * Shared implementation helpers and legacy adapters.
 */

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

/**
 * @ingroup rbcGrMisc_internals
 * @brief Acquire a staged foreground/background pair.
 * @param interp Interpreter for Tk color errors.
 * @param tkwin Window supplying color context.
 * @param fgStr Non-NULL foreground name; empty means no color.
 * @param bgStr Non-NULL background name; empty means no color.
 * @param[out] pairPtr Receives both fields only after successful acquisition.
 * @param allowDefault Accept nonempty prefixes of defcolor when nonzero.
 * @return TCL_OK on success; TCL_ERROR with destination unchanged on failure.
 * Failure of the background lookup releases an acquired foreground reference.
 * Success transfers both references, even for identical names, but does not
 * release any previous destination. NULL and COLOR_DEFAULT are not references.
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
            /* The pair is not committed yet; release only an acquired color. */
            if ((fgColor != NULL) && (fgColor != COLOR_DEFAULT)) {
                Tk_FreeColor(fgColor);
            }
            return TCL_ERROR;
        }
    }
    pairPtr->fgColor = fgColor;
    pairPtr->bgColor = bgColor;
    return TCL_OK;
}

/**
 * @ingroup rbcGrMisc_internals
 * @brief Release both owned color references and clear a pair.
 * @param[in,out] pairPtr Initialized pair, with one reference per real color field.
 * Skips NULL and COLOR_DEFAULT. Identical real pointers still represent two
 * acquisitions and are released twice. Both fields become NULL, making another
 * call safe. The pair record itself is not freed.
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

/**
 * @ingroup rbcGrMisc_internals
 * @brief Transactionally replace a legacy color-pair option field.
 * @param clientData Nonzero permits the default-color sentinel.
 * @param interp Interpreter for list and color diagnostics.
 * @param tkwin Window supplying color context.
 * @param string Borrowed list of zero, one or two names; NULL/empty clears it.
 * @param widgRec Writable record containing an initialized ColorPair.
 * @param offset Byte offset of that field.
 * @return TCL_OK on replacement; TCL_ERROR leaves the old pair intact.
 * One name sets only the foreground. Stages acquisition, releases the temporary
 * list on all parsed paths, then frees the old pair and commits the new one.
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

/**
 * @ingroup rbcGrMisc_internals
 * @brief Obtain the borrowed printable name of a color or sentinel.
 * @param colorPtr Real Tk color, NULL or COLOR_DEFAULT.
 * @return Empty string for NULL, defcolor for the sentinel, otherwise Tk's name.
 * No reference is acquired. Do not free the returned characters; copy them if
 * they must outlive the color's ownership.
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

/**
 * @ingroup rbcGrMisc_internals
 * @brief Serialize two color fields into an owned two-element Tcl list.
 * @param clientData Unused.
 * @param tkwin Unused.
 * @param widgRec Borrowed record containing a ColorPair.
 * @param offset Byte offset of that field.
 * @param[out] freeProcPtr Receives Tcl_Free for the returned string.
 * @return Allocated list, including empty or defcolor sentinel elements.
 * Copies an inline DString buffer or transfers its heap buffer to the caller.
 * The local DString is intentionally not freed after that ownership transfer.
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

/**
 * @ingroup rbcGrMisc_internals
 * @brief Read the padded mapping viewport as a clipping rectangle.
 *
 * For a scrolling active track, intersects the padded rectangle with its visible
 * bounds. Does not install a renderer clip or normalize an empty intersection.
 * @param[in] graphPtr Borrowed graph with current mapping offsets, ranges and padding; enter the owning track before
 *   calling.
 * @param[out] extsPtr Required output receiving double-precision screen-coordinate bounds.
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
