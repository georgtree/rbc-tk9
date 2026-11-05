/**
 * @file rbcGrPs.c
 * @brief Graph PostScript options, page layout, previews and delivery.
 * @defgroup graph_postscript Graph PostScript command internals
 *
 *      This module implements the "postscript" operation for rbc
 *      graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"
#include "rbcRender.h"
#include <X11/Xutil.h>
#include <stdarg.h>
#ifdef WIN32
#include <tkPlatDecls.h>
#endif

#define PS_PREVIEW_EPSI 0
#define PS_PREVIEW_WMF 1

#define DEF_PS_CENTER "yes"
#define DEF_PS_COLOR_MAP (char *)NULL
#define DEF_PS_COLOR_MODE "color"
#define DEF_PS_DECORATIONS "yes"
#define DEF_PS_FONT_MAP (char *)NULL
#define DEF_PS_FOOTER "no"
#define DEF_PS_HEIGHT "0"
#define DEF_PS_LANDSCAPE "no"
#define DEF_PS_MAXPECT "no"
#define DEF_PS_PADX "1.0i"
#define DEF_PS_PADY "1.0i"
#define DEF_PS_PAPERHEIGHT "11.0i"
#define DEF_PS_PAPERWIDTH "8.5i"
#define DEF_PS_PREVIEW "no"
#define DEF_PS_PREVIEW_FORMAT "epsi"
#define DEF_PS_WIDTH "0"

#define PS_DIMENSIONS_CHANGED (1U << 0)
#define PS_PADDING_CHANGED (1U << 1)
#define PS_COLOR_MODE_CHANGED (1U << 2)
#define PS_PREVIEW_FORMAT_CHANGED (1U << 3)
#define PS_INITIALIZE_MASK                                                                                             \
    (PS_DIMENSIONS_CHANGED | PS_PADDING_CHANGED | PS_COLOR_MODE_CHANGED | PS_PREVIEW_FORMAT_CHANGED)

static const Tk_OptionSpec postScriptOptionSpecs[] = {
    {TK_OPTION_BOOLEAN, "-center", "center", "Center", DEF_PS_CENTER, -1, offsetof(PostScript, center), 0, NULL, 0},
    {TK_OPTION_STRING, "-colormap", "colorMap", "ColorMap", DEF_PS_COLOR_MAP, -1, offsetof(PostScript, colorVarName),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-colormode", "colorMode", "ColorMode", DEF_PS_COLOR_MODE, offsetof(PostScript, colorModeObjPtr),
     -1, 0, NULL, PS_COLOR_MODE_CHANGED},
    {TK_OPTION_BOOLEAN, "-decorations", "decorations", "Decorations", DEF_PS_DECORATIONS, -1,
     offsetof(PostScript, decorations), 0, NULL, 0},
    {TK_OPTION_STRING, "-fontmap", "fontMap", "FontMap", DEF_PS_FONT_MAP, -1, offsetof(PostScript, fontVarName),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_BOOLEAN, "-footer", "footer", "Footer", DEF_PS_FOOTER, -1, offsetof(PostScript, footer), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-height", "height", "Height", DEF_PS_HEIGHT, offsetof(PostScript, heightObjPtr),
     offsetof(PostScript, reqHeight), 0, NULL, PS_DIMENSIONS_CHANGED},
    {TK_OPTION_BOOLEAN, "-landscape", "landscape", "Landscape", DEF_PS_LANDSCAPE, -1, offsetof(PostScript, landscape),
     0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-maxpect", "maxpect", "Maxpect", DEF_PS_MAXPECT, -1, offsetof(PostScript, maxpect), 0, NULL,
     0},
    {TK_OPTION_STRING, "-padx", "padX", "PadX", DEF_PS_PADX, offsetof(PostScript, padXObjPtr), -1, 0, NULL,
     PS_PADDING_CHANGED},
    {TK_OPTION_STRING, "-pady", "padY", "PadY", DEF_PS_PADY, offsetof(PostScript, padYObjPtr), -1, 0, NULL,
     PS_PADDING_CHANGED},
    {TK_OPTION_PIXELS, "-paperheight", "paperHeight", "PaperHeight", DEF_PS_PAPERHEIGHT,
     offsetof(PostScript, paperHeightObjPtr), offsetof(PostScript, reqPaperHeight), 0, NULL, PS_DIMENSIONS_CHANGED},
    {TK_OPTION_PIXELS, "-paperwidth", "paperWidth", "PaperWidth", DEF_PS_PAPERWIDTH,
     offsetof(PostScript, paperWidthObjPtr), offsetof(PostScript, reqPaperWidth), 0, NULL, PS_DIMENSIONS_CHANGED},
    {TK_OPTION_BOOLEAN, "-preview", "preview", "Preview", DEF_PS_PREVIEW, -1, offsetof(PostScript, addPreview), 0, NULL,
     0},
    {TK_OPTION_STRING, "-previewformat", "previewFormat", "PreviewFormat", DEF_PS_PREVIEW_FORMAT,
     offsetof(PostScript, previewFormatObjPtr), -1, 0, NULL, PS_PREVIEW_FORMAT_CHANGED},
    {TK_OPTION_PIXELS, "-width", "width", "Width", DEF_PS_WIDTH, offsetof(PostScript, widthObjPtr),
     offsetof(PostScript, reqWidth), 0, NULL, PS_DIMENSIONS_CHANGED},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

typedef int RbcGrPsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcGrPsOp *proc;
} PostScriptOpSpec;

static RbcGrPsOp CgetOp;
static RbcGrPsOp ConfigureOp;
static RbcGrPsOp OutputOp;

static int ComputeBoundingBox(Graph *graphPtr, PostScript *psPtr);
static void PreviewImage(Graph *graphPtr, PsToken psToken);
static int PostScriptPreamble(Graph *graphPtr, const char *fileName, PsToken psToken);
static int GraphToPostScript(Graph *graphPtr, const char *ident, PsToken psToken);

#ifdef WIN32
static int CreateWindowsEPS(Graph *graphPtr, PsToken psToken, Tcl_DString *document);
#endif

/**
 * @brief Match a nonempty, case-sensitive prefix of an option value.
 * @ingroup graph_postscript
 *
 * Borrows both strings. The supplied length must describe readable string
 * storage; lengths beyond fullName are rejected before comparison. Returns
 * a boolean without changing the interpreter or either string.
 *
 * @param[in] string Borrowed candidate bytes, readable for length bytes.
 * @param[in] length Candidate byte count; nonpositive lengths never match.
 * @param[in] fullName Borrowed NUL-terminated full option spelling.
 */
static int IsOptionPrefix(const char *string, Tcl_Size length, const char *fullName) {
    Tcl_Size fullLength;

    fullLength = (Tcl_Size)strlen(fullName);
    return ((length > 0) && (length <= fullLength) && (strncmp(string, fullName, (size_t)length) == 0));
}

/**
 * @brief Parse a color level without changing the destination on failure.
 * @ingroup graph_postscript
 *
 * Accepts nonempty prefixes of color, grayscale, greyscale and monochrome.
 * Borrows objPtr and writes modePtr only on TCL_OK; TCL_ERROR leaves a
 * diagnostic in interp. Does not retain the object.
 *
 * @param[in] interp Interpreter receiving conversion errors; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed option object; not retained.
 * @param[out] modePtr Required result storage; unchanged on failure.
 */
static int GetColorModeFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, PsColorMode *modePtr) {
    const char *string;
    Tcl_Size length;

    string = Tcl_GetStringFromObj(objPtr, &length);
    if (IsOptionPrefix(string, length, "color")) {
        *modePtr = PS_MODE_COLOR;
    } else if (IsOptionPrefix(string, length, "grayscale") || IsOptionPrefix(string, length, "greyscale")) {
        *modePtr = PS_MODE_GREYSCALE;
    } else if (IsOptionPrefix(string, length, "monochrome")) {
        *modePtr = PS_MODE_MONOCHROME;
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad color mode \"%s\": should be "
                                               "\"color\", \"grayscale\", \"greyscale\", "
                                               "or \"monochrome\"",
                                               string));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @brief Parse a preview format supported by this build.
 * @ingroup graph_postscript
 *
 * EPSI is available everywhere and WMF on Windows. Accepts nonempty
 * prefixes. Writes formatPtr only on TCL_OK; TCL_ERROR reports the
 * build-specific choices in interp. TIFF has no encoder and is rejected
 * regardless of HAVE_TIFF_H.
 *
 * @param[in] interp Interpreter receiving unsupported-format errors; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed preview-format object; not retained.
 * @param[out] formatPtr Required result storage; changed only on TCL_OK.
 */
static int GetPreviewFormatFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *formatPtr) {
    const char *string;
    Tcl_Size length;

    string = Tcl_GetStringFromObj(objPtr, &length);
    if (IsOptionPrefix(string, length, "epsi")) {
        *formatPtr = PS_PREVIEW_EPSI;
#ifdef WIN32
    } else if (IsOptionPrefix(string, length, "wmf")) {
        *formatPtr = PS_PREVIEW_WMF;
#endif
    } else {
#ifdef WIN32
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad preview format \"%s\": should be "
                                               "\"epsi\" or \"wmf\"",
                                               string));
#else
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad preview format \"%s\": should be "
                                               "\"epsi\"",
                                               string));
#endif
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @brief Validate changed option objects before committing derived fields.
 * @ingroup graph_postscript
 *
 * Borrows the graph and its PostScript record. Converts only mask-selected
 * dimensions, padding, color mode and preview format into local values.
 * Plot dimensions may be zero; paper dimensions must be positive. On
 * TCL_ERROR no derived field is committed. This does not restore the Tk
 * option objects themselves; SetPostScriptOptions handles that rollback.
 *
 * @param[in] graphPtr Borrowed graph providing interpreter and Tk distance-conversion context.
 * @param[in,out] psPtr Initialized component; selected derived fields commit together after validation.
 * @param[in] mask PS_*_CHANGED bits selecting conversions; unselected derived values are retained.
 */
static int ConfigurePostScript(Graph *graphPtr, PostScript *psPtr, int mask) {
    Rbc_Pad newPadX;
    Rbc_Pad newPadY;
    PsColorMode newColorMode;
    int newPreviewFormat;
    int newReqWidth;
    int newReqHeight;
    int newReqPaperWidth;
    int newReqPaperHeight;

    newPadX = psPtr->padX;
    newPadY = psPtr->padY;
    newColorMode = psPtr->colorMode;
    newPreviewFormat = psPtr->previewFormat;
    newReqWidth = psPtr->reqWidth;
    newReqHeight = psPtr->reqHeight;
    newReqPaperWidth = psPtr->reqPaperWidth;
    newReqPaperHeight = psPtr->reqPaperHeight;
    /*
     * Convert and validate into temporary values first.
     */
    if (mask & PS_DIMENSIONS_CHANGED) {
        if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, psPtr->widthObjPtr, PIXELS_NONNEGATIVE,
                                 &newReqWidth) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, psPtr->heightObjPtr, PIXELS_NONNEGATIVE,
                                 &newReqHeight) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, psPtr->paperWidthObjPtr, PIXELS_POSITIVE,
                                 &newReqPaperWidth) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, psPtr->paperHeightObjPtr, PIXELS_POSITIVE,
                                 &newReqPaperHeight) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (mask & PS_PADDING_CHANGED) {
        if (Rbc_GetPadFromObj(graphPtr->interp, graphPtr->tkwin, psPtr->padXObjPtr, &newPadX) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPadFromObj(graphPtr->interp, graphPtr->tkwin, psPtr->padYObjPtr, &newPadY) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (mask & PS_COLOR_MODE_CHANGED) {
        if (GetColorModeFromObj(graphPtr->interp, psPtr->colorModeObjPtr, &newColorMode) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (mask & PS_PREVIEW_FORMAT_CHANGED) {
        if (GetPreviewFormatFromObj(graphPtr->interp, psPtr->previewFormatObjPtr, &newPreviewFormat) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /*
     * Commit only after every conversion succeeds.
     */
    psPtr->reqWidth = newReqWidth;
    psPtr->reqHeight = newReqHeight;
    psPtr->reqPaperWidth = newReqPaperWidth;
    psPtr->reqPaperHeight = newReqPaperHeight;
    psPtr->padX = newPadX;
    psPtr->padY = newPadY;
    psPtr->colorMode = newColorMode;
    psPtr->previewFormat = newPreviewFormat;
    return TCL_OK;
}

/**
 * @brief Apply Tk options and validate their derived PostScript values.
 * @ingroup graph_postscript
 *
 * Requires initialized component options and caller-provided saved-options
 * and mask storage. Returns TCL_ERROR on Tk or conversion failure; derived
 * conversion failure restores saved Tk options while preserving the error.
 * On TCL_OK the caller must release saved options with Tk_FreeSavedOptions.
 * Successful settings belong to the component, not a temporary export.
 *
 * @param[in] graphPtr Borrowed graph providing the interpreter and live Tk configuration context.
 * @param[in,out] psPtr Initialized component receiving persistent option values.
 * @param[in] objc Number of objects in the option/value slice, not the full widget command.
 * @param[in] objv Borrowed option/value slice; see @ref rbc_command_arguments for object lifetime.
 * @param[out] savedOptionsPtr Required Tk saved-options storage; caller frees it after TCL_OK.
 * @param[out] maskPtr Required storage for Tk changed-option bits; not a success indicator.
 */
static int SetPostScriptOptions(Graph *graphPtr, PostScript *psPtr, Tcl_Size objc, Tcl_Obj *const objv[],
                                Tk_SavedOptions *savedOptionsPtr, int *maskPtr) {
    if (Tk_SetOptions(graphPtr->interp, (char *)psPtr, psPtr->optionTable, objc, objv, graphPtr->tkwin, savedOptionsPtr,
                      maskPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (ConfigurePostScript(graphPtr, psPtr, *maskPtr) != TCL_OK) {
        return Rbc_RestoreOptionsOnError(graphPtr->interp, savedOptionsPtr);
    }
    return TCL_OK;
}

/**
 * @brief Detach and release the graph-owned PostScript option record.
 * @ingroup graph_postscript
 *
 * A NULL graph->postscript is a no-op. Otherwise clears that pointer before
 * freeing Tk configuration resources, deleting the option table and freeing
 * the record. Requires the graph and Tk option context to remain valid;
 * does not destroy the graph, its window or an independently owned token.
 *
 * @param[in,out] graphPtr Graph whose component is detached and freed; a NULL component is allowed, a
 *    NULL graph is not.
 */
void Rbc_DestroyPostScript(Graph *graphPtr) {
    PostScript *psPtr;

    psPtr = graphPtr->postscript;
    if (psPtr == NULL) {
        return;
    }
    graphPtr->postscript = NULL;
    Tk_FreeConfigOptions((char *)psPtr, psPtr->optionTable, graphPtr->tkwin);
    Tk_DeleteOptionTable(psPtr->optionTable);
    psPtr->optionTable = NULL;
    ckfree((char *)psPtr);
}

/**
 * @brief Return one PostScript option value through the Tcl result.
 * @ingroup graph_postscript
 *
 * Requires dispatch-validated arguments with the option at objv[3]. Borrows
 * the component and arguments. Returns TCL_OK with the Tk option value or
 * TCL_ERROR with the lookup diagnostic; does not configure or render.
 *
 * @param[in,out] graphPtr Borrowed graph with initialized PostScript component; enclosing widget
 *    dispatch owns lifetime protection.
 * @param[in] interp Interpreter receiving command results and errors; see @ref rbc_interp_argument.
 * @param[in] objc Exactly four full-command objects; option name is objv[3].
 * @param[in] objv Borrowed full widget-command vector; see @ref rbc_command_arguments.
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    PostScript *psPtr;
    Tcl_Obj *resultObjPtr;

    psPtr = graphPtr->postscript;
    resultObjPtr = Tk_GetOptionValue(interp, (char *)psPtr, psPtr->optionTable, objv[3], graphPtr->tkwin);
    if (resultObjPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, resultObjPtr);
    return TCL_OK;
}

/**
 * @brief Query or update the persistent PostScript component options.
 * @ingroup graph_postscript
 *
 * With three words returns all option descriptions; with four returns one.
 * Otherwise applies option/value pairs transactionally and frees saved
 * options on success. Returns the Tk query or configuration status. Does
 * not emit a document or open a destination file.
 *
 * @param[in,out] graphPtr Borrowed graph with initialized PostScript component; enclosing widget
 *    dispatch owns lifetime protection.
 * @param[in] interp Interpreter receiving command results and errors; see @ref rbc_interp_argument.
 * @param[in] objc Full-command count: three lists all options, four queries one, otherwise option/value
 *    pairs follow objv[2].
 * @param[in] objv Borrowed full widget-command vector; see @ref rbc_command_arguments.
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    PostScript *psPtr;
    Tcl_Obj *resultObjPtr;
    Tk_SavedOptions savedOptions;
    int mask;

    psPtr = graphPtr->postscript;
    if (objc == 3) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)psPtr, psPtr->optionTable, NULL, graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (objc == 4) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)psPtr, psPtr->optionTable, objv[3], graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (SetPostScriptOptions(graphPtr, psPtr, objc - 3, objv + 3, &savedOptions, &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    Tk_FreeSavedOptions(&savedOptions);
    return TCL_OK;
}

/**
 * @brief Clamp a wide layout size to the positive int range.
 * @ingroup graph_postscript
 *
 * Returns at least one and at most INT_MAX, represented as Tcl_WideInt.
 * Used before narrowing page and plot dimensions; has no side effects.
 *
 * @param[in] value Wide intermediate pixel size, including zero or negative values.
 */
static Tcl_WideInt PostScriptLayoutSize(Tcl_WideInt value) {
    if (value < 1) {
        return 1;
    }
    return Rbc_ClampToInt(value);
}

/**
 * @brief Compute page bounds and lay out the graph at export dimensions.
 * @ingroup graph_postscript
 *
 * Uses requested plot dimensions when positive, exchanges oriented extents
 * for landscape, and fits within paper minus padding. Maxpect also permits
 * enlargement. Stores pageScale and inclusive bounds in psPtr; centering
 * replaces the initial padding origin when paper is larger than the plot.
 * Wide arithmetic and clamping bound intermediate layout sizes. Sets layout
 * and mapping flags, invokes Rbc_LayoutGraph, and returns paper height in
 * pixels or -1 on layout failure. The caller must restore screen geometry
 * through the export lifecycle; this function does not save it.
 *
 * @param[in,out] graphPtr Borrowed graph whose export dimensions, flags and mapped layout are changed.
 * @param[in,out] psPtr Component supplying page settings and receiving scale and inclusive pixel bounds.
 */
static int ComputeBoundingBox(Graph *graphPtr, PostScript *psPtr) {
    Tcl_WideInt paperWidth;
    Tcl_WideInt paperHeight;
    Tcl_WideInt x, y;
    Tcl_WideInt hSize, vSize;
    Tcl_WideInt hBorder, vBorder;
    Tcl_WideInt availableWidth;
    Tcl_WideInt availableHeight;
    double hScale, vScale;
    double scale;

    x = psPtr->padLeft;
    y = psPtr->padTop;
    hBorder = PADDING(psPtr->padX);
    vBorder = PADDING(psPtr->padY);
    if (psPtr->reqWidth > 0) {
        graphPtr->width = psPtr->reqWidth;
    }
    if (psPtr->reqHeight > 0) {
        graphPtr->height = psPtr->reqHeight;
    }
    if (psPtr->landscape) {
        hSize = graphPtr->height;
        vSize = graphPtr->width;
    } else {
        hSize = graphPtr->width;
        vSize = graphPtr->height;
    }
    hSize = PostScriptLayoutSize(hSize);
    vSize = PostScriptLayoutSize(vSize);
    paperWidth = psPtr->reqPaperWidth;
    paperHeight = psPtr->reqPaperHeight;
    if (paperWidth < 1) {
        paperWidth = hSize + hBorder;
    }
    if (paperHeight < 1) {
        paperHeight = vSize + vBorder;
    }
    paperWidth = PostScriptLayoutSize(paperWidth);
    paperHeight = PostScriptLayoutSize(paperHeight);
    availableWidth = paperWidth - hBorder;
    availableHeight = paperHeight - vBorder;
    if (availableWidth < 1) {
        availableWidth = 1;
    }
    if (availableHeight < 1) {
        availableHeight = 1;
    }
    hScale = 1.0;
    vScale = 1.0;
    if ((psPtr->maxpect) || ((hSize + hBorder) > paperWidth)) {
        hScale = (double)availableWidth / (double)hSize;
    }
    if ((psPtr->maxpect) || ((vSize + vBorder) > paperHeight)) {
        vScale = (double)availableHeight / (double)vSize;
    }
    scale = MIN(hScale, vScale);
    if ((!FINITE(scale)) || (scale <= 0.0)) {
        scale = 1.0;
    }
    if (scale != 1.0) {
        double value;

        value = ((double)hSize * scale) + 0.5;
        if (value < 1.0) {
            hSize = 1;
        } else if (value > (double)INT_MAX) {
            hSize = INT_MAX;
        } else {
            hSize = (Tcl_WideInt)value;
        }
        value = ((double)vSize * scale) + 0.5;
        if (value < 1.0) {
            vSize = 1;
        } else if (value > (double)INT_MAX) {
            vSize = INT_MAX;
        } else {
            vSize = (Tcl_WideInt)value;
        }
    }
    psPtr->pageScale = scale;
    if (psPtr->center) {
        if (paperWidth > hSize) {
            x = (paperWidth - hSize) / 2;
        }
        if (paperHeight > vSize) {
            y = (paperHeight - vSize) / 2;
        }
    }
    psPtr->left = Rbc_ClampToInt(x);
    psPtr->bottom = Rbc_ClampToInt(y);
    psPtr->right = Rbc_ClampToInt(x + hSize - 1);
    psPtr->top = Rbc_ClampToInt(y + vSize - 1);
    graphPtr->flags |= LAYOUT_NEEDED | MAP_WORLD;
    if (Rbc_LayoutGraph(graphPtr) != TCL_OK) {
        return -1;
    }
    return (int)paperHeight;
}

/**
 * @brief Append an eight-bit grayscale EPSI preview from a native snapshot.
 * @ingroup graph_postscript
 *
 * Borrows the graph and token. Owns and frees the temporary pixmap, captured
 * color image and encoding DString. Drawing may run callbacks; invalidation
 * is checked before capture. Capture failure silently omits the preview.
 * THUMBNAIL_PREVIEW enables optional resampling; otherwise uses the export
 * layout size. Landscape rotates the grayscale image by 90 degrees. The
 * caller checks graph validity again because this void helper has no status.
 *
 * @param[in,out] graphPtr Borrowed, caller-protected graph at export layout dimensions; drawing may
 *    invoke callbacks.
 * @param[in,out] psToken Borrowed output token receiving EPSI comments; remains owned by the caller.
 */
static void PreviewImage(Graph *graphPtr, PsToken psToken) {
    PostScript *psPtr = (PostScript *)graphPtr->postscript;
    int noBackingStore = 0;
    Pixmap drawable;
    Rbc_ColorImage image;
    int nLines;
    Tcl_DString dString;

    /* Create a pixmap and draw the graph into it. */

    drawable = Tk_GetPixmap(graphPtr->display, Tk_WindowId(graphPtr->tkwin), graphPtr->width, graphPtr->height,
                            Tk_Depth(graphPtr->tkwin));
    Rbc_DrawGraph(graphPtr, drawable, noBackingStore);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        Tk_FreePixmap(graphPtr->display, drawable);
        return;
    }
    /* Get a color image from the pixmap */
    image = Rbc_DrawableToColorImage(graphPtr->tkwin, drawable, 0, 0, graphPtr->width, graphPtr->height, 1.0);
    Tk_FreePixmap(graphPtr->display, drawable);
    if (image == NULL) {
        return; /* Can't grab pixmap? */
    }
#ifdef THUMBNAIL_PREVIEW
    {
        double scale, xScale, yScale;
        int width, height;
        Rbc_ColorImage destImage;

        /* Scale the source image into a size appropriate for a thumbnail. */
#define PS_MAX_PREVIEW_WIDTH 300.0
#define PS_MAX_PREVIEW_HEIGHT 300.0
        xScale = PS_MAX_PREVIEW_WIDTH / (double)graphPtr->width;
        yScale = PS_MAX_PREVIEW_HEIGHT / (double)graphPtr->height;
        scale = MIN(xScale, yScale);
        width = (int)(scale * graphPtr->width + 0.5);
        height = (int)(scale * graphPtr->height + 0.5);
        destImage = Rbc_ResampleColorImage(image, width, height, rbcBoxFilterPtr, rbcBoxFilterPtr);
        Rbc_FreeColorImage(image);
        image = destImage;
    }
#endif /* THUMBNAIL_PREVIEW */
    Rbc_ColorImageToGreyscale(image);
    if (psPtr->landscape) {
        Rbc_ColorImage oldImage;

        oldImage = image;
        image = Rbc_RotateColorImage(image, 90.0);
        Rbc_FreeColorImage(oldImage);
    }
    Tcl_DStringInit(&dString);
    /* Finally, we can generate PostScript for the image */
    nLines = Rbc_ColorImageToPsData(image, 1, &dString, "%");
    Rbc_AppendToPostScript(psToken, "%%BeginPreview: ", (char *)NULL);
    Rbc_FormatToPostScript(psToken, "%d %d 8 %d\n", Rbc_ColorImageWidth(image), Rbc_ColorImageHeight(image), nLines);
    Rbc_AppendToPostScript(psToken, Tcl_DStringValue(&dString), (char *)NULL);
    Rbc_AppendToPostScript(psToken, "%%EndPreview\n\n", (char *)NULL);
    Tcl_DStringFree(&dString);
    Rbc_FreeColorImage(image);
}

#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#else
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif /* HAVE_SYS_TIME_H */
#endif /* TIME_WITH_SYS_TIME */

/**
 * @brief Lay out the export and append EPS metadata, prolog and transforms.
 * @ingroup graph_postscript
 *
 * Borrows fileName (NULL selects the widget path) and psToken. Returns
 * TCL_ERROR on layout failure, preview invalidation or prolog load failure.
 * Converts display pixels to points using screen pixel/mm dimensions,
 * escapes metadata strings, optionally appends EPSI, loads rbcGraph.pro,
 * then emits any footer and establishes page orientation and scaling.
 * Reads rbc_version and the prolog library path through Tcl, so variable
 * traces are possible. Checks graph validity after successful callback-capable
 * reads before resuming component access. Partial
 * output remains in the token on failure; the enclosing operation owns
 * graph lifetime protection and export cleanup.
 *
 * @param[in,out] graphPtr Caller-protected graph with initialized PostScript component; layout is
 *    changed.
 * @param[in] fileName Borrowed NUL-terminated title, or NULL to use the widget path.
 * @param[in,out] psToken Borrowed token receiving metadata, prolog and transforms; partial output
 *    remains on error.
 */
static int PostScriptPreamble(Graph *graphPtr, const char *fileName, PsToken psToken) {
    PostScript *psPtr = (PostScript *)graphPtr->postscript;
    time_t ticks;
    const char *dateString;
    char date[200]; /* Hold the date string from ctime() */
    const char *version;
    double dpiX, dpiY;
    double xPixelsToPica, yPixelsToPica; /* Scales to convert pixels to pica */
    Screen *screenPtr;
    char *nl;
    int paperHeightPixels;

    paperHeightPixels = ComputeBoundingBox(graphPtr, psPtr);
    if (paperHeightPixels < 0) {
        return TCL_ERROR;
    }
    if (fileName == NULL) {
        fileName = Tk_PathName(graphPtr->tkwin);
    }
    Rbc_AppendToPostScript(psToken, "%!PS-Adobe-3.0 EPSF-3.0\n", (char *)NULL);
    /*
     * Compute the scale factors to convert PostScript to X11 coordinates.
     * Round the pixels per inch (dpi) to an integral value before computing
     * the scale.
     */
#define MM_INCH 25.4
#define PICA_INCH 72.0
    screenPtr = Tk_Screen(graphPtr->tkwin);
    dpiX = (WidthOfScreen(screenPtr) * MM_INCH) / WidthMMOfScreen(screenPtr);
    xPixelsToPica = PICA_INCH / dpiX;
    dpiY = (HeightOfScreen(screenPtr) * MM_INCH) / HeightMMOfScreen(screenPtr);
    yPixelsToPica = PICA_INCH / dpiY;
    /*
     * The "BoundingBox" comment is required for EPS files. The box
     * coordinates are integers, so we need round away from the
     * center of the box.
     */
    Rbc_FormatToPostScript(psToken, "%%%%BoundingBox: %d %d %d %d\n", (int)floor(psPtr->left * xPixelsToPica),
                           (int)floor((paperHeightPixels - psPtr->top) * yPixelsToPica),
                           (int)ceil(psPtr->right * xPixelsToPica),
                           (int)ceil((paperHeightPixels - psPtr->bottom) * yPixelsToPica));
    Rbc_AppendToPostScript(psToken, "%%Pages: 0\n", (char *)NULL);
    version = Tcl_GetVar(graphPtr->interp, "rbc_version", TCL_GLOBAL_ONLY);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return TCL_ERROR;
    }
    if (version == NULL) {
        version = "???";
    }
    Rbc_AppendToPostScript(psToken, "%%Creator: (Rbc ", (char *)NULL);
    Rbc_AppendPostScriptString(psToken, version, -1);
    Rbc_AppendToPostScript(psToken, " ", (char *)NULL);
    Rbc_AppendPostScriptString(psToken, Tk_Class(graphPtr->tkwin), -1);
    Rbc_AppendToPostScript(psToken, ")\n", (char *)NULL);
    ticks = time((time_t *)NULL);
    dateString = ctime(&ticks);
    if (dateString == NULL) {
        snprintf(date, sizeof(date), "???");
    } else {
        snprintf(date, sizeof(date), "%s", dateString);
        nl = strchr(date, '\n');
        if (nl != NULL) {
            *nl = '\0';
        }
    }
    Rbc_AppendToPostScript(psToken, "%%CreationDate: (", (char *)NULL);
    Rbc_AppendPostScriptString(psToken, date, -1);
    Rbc_AppendToPostScript(psToken, ")\n", (char *)NULL);
    Rbc_AppendToPostScript(psToken, "%%Title: (", (char *)NULL);
    Rbc_AppendPostScriptString(psToken, fileName, -1);
    Rbc_AppendToPostScript(psToken, ")\n", (char *)NULL);
    Rbc_AppendToPostScript(psToken, "%%DocumentData: Clean7Bit\n", (char *)NULL);
    if (psPtr->landscape) {
        Rbc_AppendToPostScript(psToken, "%%Orientation: Landscape\n", (char *)NULL);
    } else {
        Rbc_AppendToPostScript(psToken, "%%Orientation: Portrait\n", (char *)NULL);
    }
    Rbc_AppendToPostScript(psToken, "%%DocumentNeededResources: font Helvetica Courier\n", (char *)NULL);
    Rbc_AppendToPostScript(psToken, "%%EndComments\n\n", (char *)NULL);
    if ((psPtr->addPreview) && (psPtr->previewFormat == PS_PREVIEW_EPSI)) {
        PreviewImage(graphPtr, psToken);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return TCL_ERROR;
        }
    }
    if (Rbc_FileToPostScript(psToken, "rbcGraph.pro") != TCL_OK ||
        !Rbc_GraphCallbackValid(graphPtr)) {
        return TCL_ERROR;
    }
    if (psPtr->footer) {
        char *who;

        who = getenv("LOGNAME");
        if (who == NULL) {
            who = "???";
        }
        if (psPtr->footer) {
            const char *who;
            const char *host;
            who = getenv("LOGNAME");
            if (who == NULL) {
                who = "???";
            }
            host = Tcl_GetHostName();
            if (host == NULL) {
                host = "???";
            }
            Rbc_AppendToPostScript(psToken,
                                   "8 /Helvetica SetFont\n"
                                   "10 30 moveto\n"
                                   "(Date: ",
                                   (char *)NULL);
            Rbc_AppendPostScriptString(psToken, date, -1);
            Rbc_AppendToPostScript(psToken,
                                   ") show\n"
                                   "10 20 moveto\n"
                                   "(File: ",
                                   (char *)NULL);
            Rbc_AppendPostScriptString(psToken, fileName, -1);
            Rbc_AppendToPostScript(psToken,
                                   ") show\n"
                                   "10 10 moveto\n"
                                   "(Created by: ",
                                   (char *)NULL);
            Rbc_AppendPostScriptString(psToken, who, -1);
            Rbc_AppendToPostScript(psToken, "@", (char *)NULL);
            Rbc_AppendPostScriptString(psToken, host, -1);
            Rbc_AppendToPostScript(psToken,
                                   ") show\n"
                                   "0 0 moveto\n",
                                   (char *)NULL);
        }
    }
    /*
     * Set the conversion from PostScript to X11 coordinates.  Scale
     * pica to pixels and flip the y-axis (the origin is the upperleft
     * corner).
     */
    Rbc_AppendToPostScript(psToken, "% Transform coordinate system to use X11 coordinates\n\n",
                           "% 1. Flip y-axis over by reversing the scale,\n",
                           "% 2. Translate the origin to the other side of the page,\n",
                           "%    making the origin the upper left corner\n", (char *)NULL);
    Rbc_FormatToPostScript(psToken, "%g -%g scale\n", xPixelsToPica, yPixelsToPica);
    /* Papersize is in pixels.  Translate the new origin *after*
     * changing the scale. */
    Rbc_FormatToPostScript(psToken, "0 %d translate\n\n", -paperHeightPixels);
    Rbc_AppendToPostScript(psToken, "% User defined page layout\n\n", "% Set color level\n", (char *)NULL);
    Rbc_FormatToPostScript(psToken, "/CL %d def\n\n", psPtr->colorMode);
    Rbc_FormatToPostScript(psToken, "%% Set origin\n%d %d translate\n\n", psPtr->left, psPtr->bottom);
    if (psPtr->landscape) {
        Rbc_FormatToPostScript(psToken, "%% Landscape orientation\n0 %g translate\n-90 rotate\n",
                               ((double)graphPtr->width * psPtr->pageScale));
    }
    if (psPtr->pageScale != 1.0) {
        Rbc_AppendToPostScript(psToken, "\n% Setting graph scale factor\n", (char *)NULL);
        Rbc_FormatToPostScript(psToken, " %g %g scale\n", psPtr->pageScale, psPtr->pageScale);
    }
    Rbc_AppendToPostScript(psToken, "\n%%EndSetup\n\n", (char *)NULL);
    return TCL_OK;
}

/**
 * @brief Render a complete PostScript graph through the shared export layer.
 * @ingroup graph_postscript
 *
 * Borrows the token as backendData and its DString as the output buffer.
 * Begins graph export, emits the preamble and shared graph traversal, then
 * appends the trailer only on success. Always ends graph export and frees
 * the export context, including on error. EndGraph restores live window
 * dimensions and schedules remapping rather than synchronously restoring
 * every screen coordinate. Returns the first failing stage or TCL_OK.
 *
 * @param[in,out] graphPtr Caller-protected graph; export begin/end manage its temporary layout state.
 * @param[in] ident Borrowed document title, or NULL to select the widget path.
 * @param[in,out] psToken Caller-owned token borrowed by renderer contexts; not released by export
 *    cleanup.
 */
static int GraphToPostScript(Graph *graphPtr, const char *ident, PsToken psToken) {
    Rbc_ExportContext export;
    int result;

    Rbc_ExportInit(&export, RBC_EXPORT_POSTSCRIPT, graphPtr->interp, graphPtr->tkwin,
                   graphPtr->postscript->decorations);
    export.backendData = psToken;
    export.buffer = &psToken->dString;
    Rbc_ExportBeginGraph(graphPtr);
    result = PostScriptPreamble(graphPtr, ident, psToken);
    if (result == TCL_OK) {
        result = Rbc_ExportGraph(graphPtr, &export);
        if (result == TCL_OK) {
            Rbc_AppendToPostScript(psToken, "showpage\n", "%Trailer\n", "grestore\n", "end\n", "%EOF\n", (char *)NULL);
        }
    }
    Rbc_ExportEndGraph(graphPtr);
    Rbc_ExportFree(&export);
    return result;
}

#ifdef WIN32
/*
 * TODO: Determine if neccessary
 *
 *static void
 *InitAPMHeader(tkwin, width, height, headerPtr)
 *    Tk_Window tkwin;
 *    int width, height;
 *    APMHEADER *headerPtr;
 *{
 *    unsigned int *p;
 *    unsigned int sum;
 *    Screen *screen;
 *#define MM_INCH        25.4
 *    double dpiX, dpiY;
 *
 *    headerPtr->key = 0x9ac6cdd7L;
 *    headerPtr->hmf = 0;
 *    headerPtr->inch = 1440;
 *
 *    screen = Tk_Screen(tkwin);
 *    dpiX = (WidthOfScreen(screen) * MM_INCH) / WidthMMOfScreen(screen);
 *    dpiY = (HeightOfScreen(screen) * MM_INCH) / HeightMMOfScreen(screen);
 *
 *    headerPtr->bbox.Left = headerPtr->bbox.Top = 0;
 *    headerPtr->bbox.Bottom = (SHORT)((width * 1440) / dpiX);
 *    headerPtr->bbox.Right = (SHORT)((height * 1440) / dpiY);
 *    headerPtr->reserved = 0;
 *    sum = 0;
 *    for (p = (unsigned int *)headerPtr;
 *            p < (unsigned int *)&(headerPtr->checksum); p++) {
 *        sum ^= *p;
 *    }
 *    headerPtr->checksum = sum;
 *}
 */

/** @defgroup graph_postscript_windows Windows EPS preview packaging
 * Private native preview assembly for the graph PostScript command.
 */
/**
 * @ingroup graph_postscript_windows
 * @brief Append a DOS EPS header, PostScript payload and Windows metafile preview.
 *
 * Windows-only packaging used for file output with a WMF preview. Appends a
 * newline to the token, captures a separate preview at live widget dimensions,
 * and copies the header and both payloads into document. The PostScript byte
 * count includes its terminating NUL. This is not the EPSI snapshot path.
 *
 * Owns the temporary drawable wrapper, metafile, reference DC and global-memory
 * block, releasing them before return. The caller retains graph lifetime
 * protection and owns both output buffers. Once preview drawing has begun,
 * cleanup marks mapping work and schedules redraw. Returns TCL_OK after
 * assembly, or TCL_ERROR for the checked native failures or graph invalidation.
 *
 * @param[in,out] graphPtr Caller-protected graph with a live, realized Tk window.
 * @param[in,out] psToken Caller-owned completed PostScript token; receives a trailing newline.
 * @param[in,out] document Initialized caller-owned DString receiving binary bytes; not freed here.
 */
static int CreateWindowsEPS(Graph *graphPtr, PsToken psToken, Tcl_DString *document) {
    DWORD size;
    DOSEPSHEADER epsHeader;
    HANDLE hMem = NULL;
    HDC hRefDC, hDC;
    HENHMETAFILE hMetaFile;
    Tcl_DString dString;
    Drawable metaDrawable;
    HWND hWnd;
    int result;
    unsigned char *buffer;
    char *psBuffer;

    Rbc_AppendToPostScript(psToken, "\n", (char *)NULL);
    psBuffer = Rbc_PostScriptFromToken(psToken);
    /*
     * Fill out as much information as we can into the DOS EPS header.
     * We won't know the start of the length of the WMF segment until
     * we create the metafile.
     */
    epsHeader.magic[0] = 0xC5;
    epsHeader.magic[1] = 0xD0;
    epsHeader.magic[2] = 0xD3;
    epsHeader.magic[3] = 0xC6;
    epsHeader.psStart = sizeof(epsHeader);
    epsHeader.psLength = strlen(psBuffer) + 1;
    epsHeader.wmfStart = epsHeader.psStart + epsHeader.psLength;
    epsHeader.wmfLength = 0L; /* Fill in later. */
    epsHeader.tiffStart = 0L;
    epsHeader.tiffLength = 0L;
    epsHeader.checksum = 0xFFFF;
    result = TCL_ERROR;
    hWnd = Tk_GetHWND(Tk_WindowId(graphPtr->tkwin));
    hRefDC = GetDC(hWnd);
    /* Build a description string. */
    Tcl_DStringInit(&dString);
    Tcl_DStringAppend(&dString, "Rbc Graph ", -1);
    Tcl_DStringAppend(&dString, RBC_VERSION, -1);
    Tcl_DStringAppend(&dString, "\0", -1);
    Tcl_DStringAppend(&dString, Tk_PathName(graphPtr->tkwin), -1);
    Tcl_DStringAppend(&dString, "\0", -1);
    hDC = CreateEnhMetaFileA(hRefDC, NULL, NULL, Tcl_DStringValue(&dString));
    Tcl_DStringFree(&dString);
    if (hDC == NULL) {
        Rbc_AppendResultStrings(graphPtr->interp, "can't create metafile: ", Rbc_LastError(), (char *)NULL);
        ReleaseDC(hWnd, hRefDC);
        return TCL_ERROR;
    }
    /* Assemble a Tk drawable that points to the metafile and let the
     * graph's drawing routine draw into it. */
    metaDrawable = Rbc_WinCreateDrawableFromDC(hDC);
    graphPtr->width = Tk_Width(graphPtr->tkwin);
    graphPtr->height = Tk_Height(graphPtr->tkwin);
    graphPtr->flags |= RESET_WORLD;
    if (Rbc_LayoutGraph(graphPtr) == TCL_OK) {
        Rbc_DrawGraph(graphPtr, metaDrawable, FALSE);
    }
    GdiFlush();
    Rbc_WinFreeDrawableFromDC(metaDrawable);
    hMetaFile = CloseEnhMetaFile(hDC);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        goto error;
    }
    if (hMetaFile == NULL) {
        Rbc_AppendResultStrings(graphPtr->interp, "can't close metafile: ", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    size = GetWinMetaFileBits(hMetaFile, 0, NULL, MM_ANISOTROPIC, hRefDC);
    if (size == 0) {
        Rbc_AppendResultStrings(graphPtr->interp, "can't get metafile size: ", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    hMem = GlobalAlloc(GHND, size);
    if (hMem == NULL) {
        Rbc_AppendResultStrings(graphPtr->interp, "can't allocate global memory:", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    buffer = (LPVOID)GlobalLock(hMem);
    if (buffer == NULL || !GetWinMetaFileBits(hMetaFile, size, buffer, MM_ANISOTROPIC, hRefDC)) {
        Rbc_AppendResultStrings(graphPtr->interp, "can't get metafile data:", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    /*
     * Fix up the EPS header with the correct metafile length and PS
     * offset (now that we what they are).
     */
    epsHeader.wmfLength = size;
    epsHeader.wmfStart = epsHeader.psStart + epsHeader.psLength;
    /* Preserve the DOS EPS header, including the terminating PostScript NUL. */
    Tcl_DStringAppend(document, (const char *)&epsHeader, sizeof(epsHeader));
    Tcl_DStringAppend(document, psBuffer, epsHeader.psLength);
    Tcl_DStringAppend(document, (const char *)buffer, size);
    result = TCL_OK;

error:
    if (hMetaFile != NULL) {
        DeleteEnhMetaFile(hMetaFile);
    }
    ReleaseDC(hWnd, hRefDC);
    if (hMem != NULL) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
    }
    graphPtr->flags = MAP_WORLD;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return result;
}

#endif /*WIN32*/

/**
 * @brief Return PostScript text or write a completed document to a Tcl file.
 * @ingroup graph_postscript
 *
 * A fourth word not starting with a dash is the filename; remaining words
 * configure persistent output options. Successful options remain installed
 * even if rendering or delivery fails. Owns the token and staging DString;
 * token mapping names borrow component storage. Renders before opening the
 * destination. String results omit the Windows binary-preview wrapper.
 * File output uses binary channel translation, checks write and close
 * failures, and preserves an earlier error across cleanup. A delivery
 * failure can leave a partial file: this is not atomic file replacement.
 * Returns TCL_OK with text or an empty file-output result, else TCL_ERROR.
 * Caller must protect the graph across callbacks during rendering.
 *
 * @param[in,out] graphPtr Borrowed graph with initialized PostScript component; enclosing widget
 *    dispatch owns lifetime protection.
 * @param[in] interp Interpreter receiving command results and errors; see @ref rbc_interp_argument.
 * @param[in] objc Full-command count; optional filename and option/value pairs follow objv[2].
 * @param[in] objv Borrowed full widget-command vector; see @ref rbc_command_arguments.
 */
static int OutputOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    PostScript *psPtr;
    PsToken psToken;
    Tcl_Obj *fileName;
    Tcl_Channel channel;
    Tcl_DString document;
    Tcl_Size optionIndex;
    int result;

    psPtr = graphPtr->postscript;
    fileName = NULL;
    optionIndex = 3;
    if (objc > 3) {
        const char *arg;

        arg = Tcl_GetString(objv[3]);
        if (arg[0] != '-') {
            fileName = objv[3];
            optionIndex = 4;
        }
    }
    if (objc > optionIndex) {
        Tk_SavedOptions savedOptions;
        int mask;

        if (SetPostScriptOptions(graphPtr, psPtr, objc - optionIndex, objv + optionIndex, &savedOptions, &mask) !=
            TCL_OK) {
            return TCL_ERROR;
        }
        /*
         * Successful output options remain installed, matching the
         * existing command semantics.
         */
        Tk_FreeSavedOptions(&savedOptions);
    }
    psToken = Rbc_GetPsToken(graphPtr->interp, graphPtr->tkwin);
    psToken->fontVarName = psPtr->fontVarName;
    psToken->colorVarName = psPtr->colorVarName;
    psToken->colorMode = psPtr->colorMode;
    Tcl_DStringInit(&document);
    result = GraphToPostScript(graphPtr, fileName == NULL ? NULL : Tcl_GetString(fileName), psToken);
    if (result != TCL_OK) {
        goto done;
    }
    if (fileName == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Rbc_PostScriptFromToken(psToken), -1));
        goto done;
    }
#ifdef WIN32
    if (psPtr->addPreview && psPtr->previewFormat == PS_PREVIEW_WMF) {
        result = CreateWindowsEPS(graphPtr, psToken, &document);
        if (result != TCL_OK) {
            goto done;
        }
    } else
#endif
    {
        Tcl_DStringAppend(&document, Rbc_PostScriptFromToken(psToken), -1);
    }
    /*
     * Finish rendering (including the Windows preview) before opening the
     * destination.  Binary translation preserves both ordinary PostScript
     * bytes and the DOS EPS header/metafile, on every Tcl filesystem.
     */
    channel = Tcl_FSOpenFileChannel(interp, fileName, "w", 0666);
    if (channel == NULL) {
        result = TCL_ERROR;
        goto done;
    }
    result = Tcl_SetChannelOption(interp, channel, "-translation", "binary");
    if (result == TCL_OK &&
        Tcl_Write(channel, Tcl_DStringValue(&document), Tcl_DStringLength(&document)) !=
            Tcl_DStringLength(&document)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("error writing PostScript: %s", Tcl_PosixError(interp)));
        result = TCL_ERROR;
    }
    if (result != TCL_OK) {
        Tcl_InterpState saved = Tcl_SaveInterpState(interp, result);

        Tcl_Close(NULL, channel);
        Tcl_RestoreInterpState(interp, saved);
    } else {
        /* Closing may flush buffered output and report a late write failure. */
        result = Tcl_Close(interp, channel);
        if (result == TCL_OK) {
            Tcl_ResetResult(interp);
        }
    }

done:
    Tcl_DStringFree(&document);
    Rbc_ReleasePsToken(psToken);
    return result;
}

/**
 * @brief Create and initialize the graph-owned named PostScript component.
 * @ingroup graph_postscript
 *
 * Requires a graph without an existing PostScript record and a live option
 * context. Allocates the record/table, initializes resource name postscript
 * and class Postscript through the option proxy, then validates derived
 * fields. Returns TCL_OK on success. On TCL_ERROR destroys the partial
 * component and leaves graph->postscript NULL; diagnostics remain in interp.
 *
 * @param[in,out] graphPtr Graph with no existing PostScript component and a usable Tk configuration
 *    context; owns the new record on success.
 */
int Rbc_CreatePostScript(Graph *graphPtr) {
    PostScript *psPtr;

    psPtr = RbcCalloc(1, sizeof(PostScript));
    assert(psPtr != NULL);
    graphPtr->postscript = psPtr;
    psPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, postScriptOptionSpecs);
    if (Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, "postscript", "Postscript",
                                 (char *)psPtr, psPtr->optionTable) != TCL_OK) {
        goto error;
    }
    if (ConfigurePostScript(graphPtr, psPtr, PS_INITIALIZE_MASK) != TCL_OK) {
        goto error;
    }
    return TCL_OK;

error:
    Rbc_DestroyPostScript(graphPtr);
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_PostScriptOp --
 *
 *      This procedure is invoked to process the Tcl command
 *      that corresponds to a widget managed by this module.
 *      See the user documentation for details on what it does.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *--------------------------------------------------------------
 */
static const PostScriptOpSpec psOps[] = {{{"cget", 4, 4, "option"}, CgetOp},
                                         {{"configure", 3, 0, "?option value?..."}, ConfigureOp},
                                         {{"output", 3, 0, "?fileName? ?option value?..."}, OutputOp},
                                         {{NULL, 0, 0, NULL}, NULL}};

/**
 * @brief Validate and dispatch the graph postscript subcommand.
 * @ingroup graph_postscript
 *
 * Borrows graph, interpreter and argument objects. Uses the strided psOps
 * table to validate the operation at argument two and its arity, then
 * returns the selected handler status. Does not independently preserve
 * the graph or interpreter; the enclosing widget command owns that guard.
 *
 * @param[in,out] graphPtr Borrowed graph with initialized PostScript component; enclosing widget
 *    dispatch owns lifetime protection.
 * @param[in] interp Interpreter receiving command results and errors; see @ref rbc_interp_argument.
 * @param[in] objc Full widget-command count, including the postscript operation at objv[2].
 * @param[in] objv Borrowed full widget-command vector; see @ref rbc_command_arguments.
 */
int Rbc_PostScriptOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, psOps, (Tcl_Size)sizeof(psOps[0]), RBC_OP_ARG2, objc, objv, &index) != TCL_OK) {
        return TCL_ERROR;
    }
    return psOps[index].proc(graphPtr, interp, objc, objv);
}
