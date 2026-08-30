/*
 * rbcGrPs.c --
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
#include <X11/Xutil.h>
#include <stdarg.h>
#ifdef WIN32
#include <tkPlatDecls.h>
#endif

#define PS_PREVIEW_EPSI 0
#define PS_PREVIEW_WMF 1
#define PS_PREVIEW_TIFF 2

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

#define PS_DIMENSIONS_CHANGED     (1U << 0)
#define PS_PADDING_CHANGED        (1U << 1)
#define PS_COLOR_MODE_CHANGED     (1U << 2)
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

/* TODO: These do not belong here */
extern void Rbc_MarkersToPostScript(Graph *graphPtr, PsToken psToken, int under);
extern void Rbc_ElementsToPostScript(Graph *graphPtr, PsToken psToken);
extern void Rbc_ActiveElementsToPostScript(Graph *graphPtr, PsToken psToken);
extern void Rbc_LegendToPostScript(Legend *legendPtr, PsToken psToken);
extern void Rbc_GridToPostScript(Graph *graphPtr, PsToken psToken);
extern void Rbc_AxesToPostScript(Graph *graphPtr, PsToken psToken);
extern void Rbc_AxisLimitsToPostScript(Graph *graphPtr, PsToken psToken);

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
static void MarginsToPostScript(Graph *graphPtr, PsToken psToken);
static int GraphToPostScript(Graph *graphPtr, const char *ident, PsToken psToken);

#ifdef WIN32
static int CreateWindowsEPS(Graph *graphPtr, PsToken psToken, FILE *f);
#endif

static int IsOptionPrefix(const char *string, Tcl_Size length, const char *fullName) {
    Tcl_Size fullLength;

    fullLength = (Tcl_Size)strlen(fullName);
    return ((length > 0) && (length <= fullLength) && (strncmp(string, fullName, (size_t)length) == 0));
}

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

static int GetPreviewFormatFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *formatPtr) {
    const char *string;
    Tcl_Size length;

    string = Tcl_GetStringFromObj(objPtr, &length);
    if (IsOptionPrefix(string, length, "epsi")) {
        *formatPtr = PS_PREVIEW_EPSI;
#ifdef WIN32
#ifdef HAVE_TIFF_H
    } else if (IsOptionPrefix(string, length, "tiff")) {
        *formatPtr = PS_PREVIEW_TIFF;
#endif
    } else if (IsOptionPrefix(string, length, "wmf")) {
        *formatPtr = PS_PREVIEW_WMF;
#endif
    } else {
#ifdef WIN32
#ifdef HAVE_TIFF_H
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad preview format \"%s\": should be "
                                               "\"epsi\", \"wmf\", or \"tiff\"",
                                               string));
#else
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad preview format \"%s\": should be "
                                               "\"epsi\" or \"wmf\"",
                                               string));
#endif
#else
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad preview format \"%s\": should be "
                                               "\"epsi\"",
                                               string));
#endif
        return TCL_ERROR;
    }
    return TCL_OK;
}

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

static int SetPostScriptOptions(Graph *graphPtr, PostScript *psPtr, Tcl_Size objc, Tcl_Obj *const objv[],
                                Tk_SavedOptions *savedOptionsPtr, int *maskPtr) {
    if (Tk_SetOptions(graphPtr->interp, (char *)psPtr, psPtr->optionTable, objc, objv, graphPtr->tkwin, savedOptionsPtr,
                      maskPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (ConfigurePostScript(graphPtr, psPtr, *maskPtr) != TCL_OK) {
        Tcl_Obj *errorObjPtr;
        errorObjPtr = Tcl_GetObjResult(graphPtr->interp);
        Tcl_IncrRefCount(errorObjPtr);
        Tk_RestoreSavedOptions(savedOptionsPtr);
        Tcl_SetObjResult(graphPtr->interp, errorObjPtr);
        Tcl_DecrRefCount(errorObjPtr);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_DestroyPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_DestroyPostScript(Graph *graphPtr) {
    PostScript *psPtr;

    psPtr = graphPtr->postscript;
    if (psPtr == NULL) {
        return;
    }
    graphPtr->postscript = NULL;
    Tk_FreeConfigOptions((char *)psPtr, psPtr->optionTable, graphPtr->tkwin);
    ckfree((char *)psPtr);
}

/*
 *--------------------------------------------------------------
 *
 * CgetOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      This procedure is invoked to print the graph in a file.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard TCL result.
 *
 * Side effects:
 *      A new PostScript file is created.
 *
 * ----------------------------------------------------------------------
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

static int PostScriptLayoutInt(Tcl_WideInt value) {
    if (value > INT_MAX) {
        return INT_MAX;
    }
    if (value < INT_MIN) {
        return INT_MIN;
    }
    return (int)value;
}

static Tcl_WideInt PostScriptLayoutSize(Tcl_WideInt value) {
    if (value < 1) {
        return 1;
    }
    if (value > INT_MAX) {
        return INT_MAX;
    }
    return value;
}

/*
 * --------------------------------------------------------------------------
 *
 * ComputeBoundingBox --
 *
 *      Computes the bounding box for the PostScript plot.  First get
 *      the size of the plot (by default, it's the size of graph's X
 *      window).  If the plot plus the page border is bigger than the
 *      designated paper size, or if the "-maxpect" option is turned
 *      on, scale the plot to the page.
 *
 *      Note: All coordinates/sizes are in screen coordinates, not
 *            PostScript coordinates.  This includes the computed
 *            bounding box and paper size.  They will be scaled to
 *            printer points later.
 *
 * Parameters:
 *      Graph *graphPtr
 *      PostScript *psPtr
 *
 * Results:
 *      Returns the height of the paper in screen coordinates.
 *
 * Side Effects:
 *      The graph dimensions (width and height) are changed to the
 *      requested PostScript plot size.
 *
 * --------------------------------------------------------------------------
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
    psPtr->left = PostScriptLayoutInt(x);
    psPtr->bottom = PostScriptLayoutInt(y);
    psPtr->right = PostScriptLayoutInt(x + hSize - 1);
    psPtr->top = PostScriptLayoutInt(y + vSize - 1);
    graphPtr->flags |= LAYOUT_NEEDED | MAP_WORLD;
    Rbc_LayoutGraph(graphPtr);
    return (int)paperHeight;
}

/*
 * --------------------------------------------------------------------------
 *
 * PreviewImage --
 *
 *      Generates a EPSI thumbnail of the graph.  The thumbnail is
 *      restricted to a certain size.  This is to keep the size of the
 *      PostScript file small and the processing time low.
 *
 *      The graph is drawn into a pixmap.  We then take a snapshot
 *      of that pixmap, and rescale it to a smaller image.  Finally,
 *       the image is dumped to PostScript.
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * --------------------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * PostScriptPreamble --
 *
 *      The PostScript preamble calculates the needed
 *      translation and scaling to make X11 coordinates
 *      compatible with PostScript.
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *fileName
 *      PsToken psToken
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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
    }
    if (Rbc_FileToPostScript(psToken, "rbcGraph.pro") != TCL_OK) {
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

/*
 *--------------------------------------------------------------
 *
 * MarginsToPostScript --
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
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void MarginsToPostScript(Graph *graphPtr, PsToken psToken) {
    PostScript *psPtr = (PostScript *)graphPtr->postscript;
    XRectangle margin[4];

    margin[0].x = margin[0].y = margin[3].x = margin[1].x = 0;
    margin[0].width = margin[3].width = graphPtr->width;
    margin[0].height = graphPtr->top;
    margin[3].y = graphPtr->bottom;
    margin[3].height = graphPtr->height - graphPtr->bottom;
    margin[2].y = margin[1].y = graphPtr->top;
    margin[1].width = graphPtr->left;
    margin[2].height = margin[1].height = graphPtr->bottom - graphPtr->top;
    margin[2].x = graphPtr->right;
    margin[2].width = graphPtr->width - graphPtr->right;

    /* Clear the surrounding margins and clip the plotting surface */
    if (psPtr->decorations) {
        Rbc_BackgroundToPostScript(psToken, Tk_3DBorderColor(graphPtr->border));
    } else {
        Rbc_ClearBackgroundToPostScript(psToken);
    }
    Rbc_RectanglesToPostScript(psToken, margin, 4);

    /* Interior 3D border */
    if ((psPtr->decorations) && (graphPtr->plotBorderWidth > 0)) {
        int x, y, width, height;

        x = graphPtr->left - graphPtr->plotBorderWidth;
        y = graphPtr->top - graphPtr->plotBorderWidth;
        width = (graphPtr->right - graphPtr->left) + (2 * graphPtr->plotBorderWidth);
        height = (graphPtr->bottom - graphPtr->top) + (2 * graphPtr->plotBorderWidth);
        Rbc_Draw3DRectangleToPostScript(psToken, graphPtr->border, (double)x, (double)y, width, height,
                                        graphPtr->plotBorderWidth, graphPtr->plotRelief);
    }
    if (Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_MARGIN) {
        /*
         * Print the legend if we're using a site which lies in one
         * of the margins (left, right, top, or bottom) of the graph.
         */
        Rbc_LegendToPostScript(graphPtr->legend, psToken);
    }
    if (graphPtr->title != NULL) {
        Rbc_TextToPostScript(psToken, graphPtr->title, &graphPtr->titleTextStyle, (double)graphPtr->titleX,
                             (double)graphPtr->titleY);
    }
    Rbc_AxesToPostScript(graphPtr, psToken);
}

/*
 *--------------------------------------------------------------
 *
 * GraphToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *ident - Identifier string (usually the filename)
 *      PsToken psToken
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int GraphToPostScript(Graph *graphPtr, const char *ident, PsToken psToken) {
    int x, y, width, height;
    int result;

    /*
     * We need to know how big a graph to print.  If the graph hasn't
     * been drawn yet, the width and height will be 1.  Instead use
     * the requested size of the widget.  The user can still override
     * this with the -width and -height postscript options.
     */
    if (graphPtr->height <= 1) {
        graphPtr->height = Tk_ReqHeight(graphPtr->tkwin);
    }
    if (graphPtr->width <= 1) {
        graphPtr->width = Tk_ReqWidth(graphPtr->tkwin);
    }
    result = PostScriptPreamble(graphPtr, ident, psToken);
    if (result != TCL_OK) {
        goto error;
    }
    /*
     * Determine rectangle of the plotting area for the graph window
     */
    x = graphPtr->left - graphPtr->plotBorderWidth;
    y = graphPtr->top - graphPtr->plotBorderWidth;

    width = (graphPtr->right - graphPtr->left + 1) + (2 * graphPtr->plotBorderWidth);
    height = (graphPtr->bottom - graphPtr->top + 1) + (2 * graphPtr->plotBorderWidth);

    Rbc_FontToPostScript(psToken, graphPtr->titleTextStyle.font);
    Rbc_RegionToPostScript(psToken, (double)x, (double)y, width, height);
    if (graphPtr->postscript->decorations) {
        Rbc_BackgroundToPostScript(psToken, graphPtr->plotBg);
    } else {
        Rbc_ClearBackgroundToPostScript(psToken);
    }
    Rbc_AppendToPostScript(psToken, "Fill\n", (char *)NULL);
    Rbc_AppendToPostScript(psToken, "gsave clip\n\n", (char *)NULL);
    /* Draw the grid, elements, and markers in the plotting area. */
    if (!graphPtr->gridPtr->hidden) {
        Rbc_GridToPostScript(graphPtr, psToken);
    }
    if ((graphPtr->classUid == rbcPolarElementUid) && (graphPtr->representation == POLAR_REPRESENTATION_POLAR)) {
        Rbc_PolarLabelsToPostScript(graphPtr, psToken);
    }
    Rbc_MarkersToPostScript(graphPtr, psToken, TRUE);
    if ((Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_PLOT) && (!Rbc_LegendIsRaised(graphPtr->legend))) {
        /* Print legend underneath elements and markers */
        Rbc_LegendToPostScript(graphPtr->legend, psToken);
    }
    Rbc_AxisLimitsToPostScript(graphPtr, psToken);
    Rbc_ElementsToPostScript(graphPtr, psToken);
    if ((Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_PLOT) && (Rbc_LegendIsRaised(graphPtr->legend))) {
        /* Print legend above elements (but not markers) */
        Rbc_LegendToPostScript(graphPtr->legend, psToken);
    }
    Rbc_MarkersToPostScript(graphPtr, psToken, FALSE);
    Rbc_ActiveElementsToPostScript(graphPtr, psToken);
    Rbc_AppendToPostScript(psToken, "\n", "% Unset clipping\n", "grestore\n\n", (char *)NULL);
    MarginsToPostScript(graphPtr, psToken);
    Rbc_AppendToPostScript(psToken, "showpage\n", "%Trailer\n", "grestore\n", "end\n", "%EOF\n", (char *)NULL);
error:
    /* Reset height and width of graph window */
    graphPtr->width = Tk_Width(graphPtr->tkwin);
    graphPtr->height = Tk_Height(graphPtr->tkwin);
    graphPtr->flags = MAP_WORLD;

    /*
     * Redraw the graph in order to re-calculate the layout as soon as
     * possible. This is in the case the crosshairs are active.
     */
    Rbc_EventuallyRedrawGraph(graphPtr);
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

/*
 * --------------------------------------------------------------------------
 *
 * CreateWindowEPS --
 *
 *      Generates an EPS file with a Window metafile preview.
 *
 *      Windows metafiles aren't very robust.  Including exactly the
 *      same metafile (one embedded in a DOS EPS, the other as .wmf
 *      file) will play back differently.
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *      FILE *f
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * --------------------------------------------------------------------------
 */
static int CreateWindowsEPS(Graph *graphPtr, PsToken psToken, FILE *f) {
    DWORD size;
    DOSEPSHEADER epsHeader;
    HANDLE hMem;
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
        return TCL_ERROR;
    }
    /* Assemble a Tk drawable that points to the metafile and let the
     * graph's drawing routine draw into it. */
    metaDrawable = Rbc_WinCreateDrawableFromDC(hDC);

    graphPtr->width = Tk_Width(graphPtr->tkwin);
    graphPtr->height = Tk_Height(graphPtr->tkwin);
    graphPtr->flags |= RESET_WORLD;
    Rbc_LayoutGraph(graphPtr);
    Rbc_DrawGraph(graphPtr, metaDrawable, FALSE);
    GdiFlush();
    Rbc_WinFreeDrawableFromDC(metaDrawable);
    hMetaFile = CloseEnhMetaFile(hDC);

    size = GetWinMetaFileBits(hMetaFile, 0, NULL, MM_ANISOTROPIC, hRefDC);
    hMem = GlobalAlloc(GHND, size);
    if (hMem == NULL) {
        Rbc_AppendResultStrings(graphPtr->interp, "can't allocate global memory:", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    buffer = (LPVOID)GlobalLock(hMem);
    if (!GetWinMetaFileBits(hMetaFile, size, buffer, MM_ANISOTROPIC, hRefDC)) {
        Rbc_AppendResultStrings(graphPtr->interp, "can't get metafile data:", Rbc_LastError(), (char *)NULL);
        goto error;
    }

    /*
     * Fix up the EPS header with the correct metafile length and PS
     * offset (now that we what they are).
     */
    epsHeader.wmfLength = size;
    epsHeader.wmfStart = epsHeader.psStart + epsHeader.psLength;

    /* Write out the eps header, */
    if (fwrite(&epsHeader, 1, sizeof(epsHeader), f) != sizeof(epsHeader)) {
        Rbc_AppendResultStrings(graphPtr->interp, "error writing eps header:", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    /* the PostScript, */
    if (fwrite(psBuffer, 1, epsHeader.psLength, f) != epsHeader.psLength) {
        Rbc_AppendResultStrings(graphPtr->interp, "error writing PostScript data:", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    /* and finally the metadata itself. */
    if (fwrite(buffer, 1, size, f) != size) {
        Rbc_AppendResultStrings(graphPtr->interp, "error writing metafile data:", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    result = TCL_OK;

error:
    DeleteEnhMetaFile(hMetaFile);
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

/*
 *----------------------------------------------------------------------
 *
 * OutputOp --
 *
 *      This procedure is invoked to print the graph in a file.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Standard TCL result.  TCL_OK if plot was successfully printed,
 *      TCL_ERROR otherwise.
 *
 * Side effects:
 *      A new PostScript file is created.
 *
 *----------------------------------------------------------------------
 */
static int OutputOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    PostScript *psPtr;
    FILE *f;
    PsToken psToken;
    const char *fileName;
    Tcl_Size optionIndex;

    psPtr = graphPtr->postscript;
    f = NULL;
    psToken = NULL;
    fileName = NULL;
    optionIndex = 3;

    if (objc > 3) {
        const char *arg;

        arg = Tcl_GetString(objv[3]);

        if (arg[0] != '-') {
            fileName = arg;
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
    if (fileName != NULL) {
#ifdef WIN32
        f = fopen(fileName, "wb");
#else
        f = fopen(fileName, "w");
#endif
        if (f == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't create \"%s\": %s", fileName, Tcl_PosixError(interp)));
            return TCL_ERROR;
        }
    }
    psToken = Rbc_GetPsToken(graphPtr->interp, graphPtr->tkwin);
    psToken->fontVarName = psPtr->fontVarName;
    psToken->colorVarName = psPtr->colorVarName;
    psToken->colorMode = psPtr->colorMode;

    if (GraphToPostScript(graphPtr, fileName, psToken) != TCL_OK) {
        goto error;
    }
    /*
     * If a file name was given, write the results to that file
     */
    if (f != NULL) {
#ifdef WIN32
        if ((psPtr->addPreview) && (psPtr->previewFormat != PS_PREVIEW_EPSI)) {
            if (CreateWindowsEPS(graphPtr, psToken, f) != TCL_OK) {
                goto error;
            }
        } else {
            fputs(Rbc_PostScriptFromToken(psToken), f);
            if (ferror(f)) {
                Tcl_SetObjResult(interp,
                                 Tcl_ObjPrintf("error writing file \"%s\": %s", fileName, Tcl_PosixError(interp)));
                goto error;
            }
        }
#else
        fputs(Rbc_PostScriptFromToken(psToken), f);
        if (ferror(f)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("error writing file \"%s\": %s", fileName, Tcl_PosixError(interp)));
            goto error;
        }
#endif /* WIN32 */
        fclose(f);
    } else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Rbc_PostScriptFromToken(psToken), -1));
    }
    Rbc_ReleasePsToken(psToken);
    return TCL_OK;

error:
    if (f != NULL) {
        fclose(f);
    }
    Rbc_ReleasePsToken(psToken);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CreatePostScript --
 *
 *      Creates a postscript structure.
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      Always TCL_OK.
 *
 * Side effects:
 *      A new PostScript structure is created.
 *
 *----------------------------------------------------------------------
 */
int Rbc_CreatePostScript(Graph *graphPtr) {
    PostScript *psPtr;

    psPtr = RbcCalloc(1, sizeof(PostScript));
    assert(psPtr != NULL);
    graphPtr->postscript = psPtr;
    psPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, postScriptOptionSpecs);
    if (Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, "postscript", "Postscript", (char *)psPtr,
                                 psPtr->optionTable) != TCL_OK) {
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

/*
 *--------------------------------------------------------------
 *
 * Rbc_PostScriptOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Tcl_Interp *interp
 *      int objc - # arguments
 *      Tcl_Obj *const objv[] - Argument list
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_PostScriptOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, psOps, (Tcl_Size)sizeof(psOps[0]), RBC_OP_ARG2, objc, objv, &index) != TCL_OK) {
        return TCL_ERROR;
    }

    return psOps[index].proc(graphPtr, interp, objc, objv);
}
