/*
 * rbcPs.h --
 *
 *      TODO: Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCPS
#define _RBCPS

#include "rbcImage.h"

typedef enum {
    PS_MODE_MONOCHROME,
    PS_MODE_GREYSCALE,
    PS_MODE_COLOR
} PsColorMode;

typedef struct PsTokenStruct *PsToken;

struct PsTokenStruct {
    Tcl_Interp *interp;        /* Interpreter to report errors to. */

    Tk_Window tkwin;        /* Tk_Window used to get font and color
                 * information */

    Tcl_DString dString;    /* Dynamic string used to contain the
                 * PostScript generated. */

    char *fontVarName;        /* Name of a Tcl array variable to convert
                 * X font names to PostScript fonts. */

    char *colorVarName;        /* Name of a Tcl array variable to convert
                 * X color names to PostScript. */

    PsColorMode colorMode;    /* Mode: color or greyscale */

#define PSTOKEN_BUFSIZ    ((BUFSIZ*2)-1)
    /*
     * Utility space for building strings.  Currently used to create
     * PostScript output for the "postscript" command.
     */
    char scratchArr[PSTOKEN_BUFSIZ+1];
};

PsToken Rbc_GetPsToken (Tcl_Interp *interp,
                    Tk_Window tkwin);

void Rbc_ReleasePsToken (PsToken psToken);
char *Rbc_PostScriptFromToken (PsToken psToken);
char *Rbc_ScratchBufferFromToken (PsToken psToken);

void Rbc_AppendToPostScript (PsToken, ...);

void Rbc_FormatToPostScript (PsToken, char *, ...);

void Rbc_Draw3DRectangleToPostScript (PsToken psToken,
    Tk_3DBorder border, double x, double y, int width, int height,
    int borderWidth, int relief);

void Rbc_Fill3DRectangleToPostScript (PsToken psToken,
    Tk_3DBorder border, double x, double y, int width, int height,
    int borderWidth, int relief);

void Rbc_BackgroundToPostScript (PsToken psToken,
    XColor *colorPtr);

void Rbc_BitmapDataToPostScript (PsToken psToken,
    Display *display, Pixmap bitmap, int width, int height);

void Rbc_ClearBackgroundToPostScript (PsToken psToken);

int Rbc_ColorImageToPsData (Rbc_ColorImage image,
                    int nComponents, Tcl_DString * resultPtr, char *prefix);

void Rbc_ColorImageToPostScript (PsToken psToken,
    Rbc_ColorImage image, double x, double y);

void Rbc_ForegroundToPostScript (PsToken psToken,
    XColor *colorPtr);

void Rbc_FontToPostScript (PsToken psToken, Tk_Font font);

void Rbc_WindowToPostScript (PsToken psToken,
                    Tk_Window tkwin, double x, double y);

void Rbc_LineDashesToPostScript (PsToken psToken,
    Rbc_Dashes *dashesPtr);

void Rbc_LineWidthToPostScript (PsToken psToken,
    int lineWidth);

void Rbc_PathToPostScript (PsToken psToken,
                       Point2D *screenPts, int nScreenPts);

void Rbc_PhotoToPostScript (PsToken psToken,
                    Tk_PhotoHandle photoToken, double x, double y);
void Rbc_PolygonToPostScript (PsToken psToken,
    Point2D *screenPts, int nScreenPts);

void Rbc_LineToPostScript (PsToken psToken,
                       XPoint *pointArr, int nPoints);

void Rbc_TextToPostScript (PsToken psToken, char *string,
                       TextStyle *attrPtr, double x, double y);

void Rbc_RectangleToPostScript (PsToken psToken, double x,
    double y, int width, int height);

void Rbc_RegionToPostScript (PsToken psToken, double x,
                    double y, int width, int height);

void Rbc_RectanglesToPostScript (PsToken psToken,
    XRectangle *rectArr, int nRects);

void Rbc_BitmapToPostScript (PsToken psToken,
                    Display *display, Pixmap bitmap, double scaleX, double scaleY);

void Rbc_SegmentsToPostScript (PsToken psToken,
    XSegment *segArr, int nSegs);

void Rbc_StippleToPostScript (PsToken psToken,
    Display *display, Pixmap bitmap);

void Rbc_LineAttributesToPostScript (PsToken psToken,
    XColor *colorPtr, int lineWidth, Rbc_Dashes *dashesPtr, int capStyle,
    int joinStyle);

int Rbc_FileToPostScript (PsToken psToken,
                      char *fileName);

void Rbc_2DSegmentsToPostScript (PsToken psToken,
    Segment2D *segments, int nSegments);

#endif /* _RBCPS */
