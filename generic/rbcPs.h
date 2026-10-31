/*
 * rbcPs.h --
 *
 *      Private PostScript output token and drawing services.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCPS
#define _RBCPS

#include "rbcImage.h"

#include "rbcText.h"

typedef enum { PS_MODE_MONOCHROME, PS_MODE_GREYSCALE, PS_MODE_COLOR } PsColorMode;

typedef struct PsTokenStruct *PsToken;

struct PsTokenStruct {
    Tcl_Interp *interp; /* Interpreter to report errors to. */
    Tk_Window tkwin; /* Tk_Window used to get font and color
                      * information */
    Tcl_DString dString; /* Dynamic string used to contain the
                          * PostScript generated. */
    char *fontVarName; /* Name of a Tcl array variable to convert
                        * X font names to PostScript fonts. */
    char *colorVarName; /* Name of a Tcl array variable to convert
                         * X color names to PostScript. */
    PsColorMode colorMode; /* Mode: color or greyscale */

#define PSTOKEN_BUFSIZ ((BUFSIZ * 2) - 1)
    /*
     * Utility space for building strings.  Currently used to create
     * PostScript output for the "postscript" command.
     */
    char scratchArr[PSTOKEN_BUFSIZ + 1];
};

/**
 * Allocate an empty, color-mode PostScript token.
 *
 * The caller owns the token and releases it with Rbc_ReleasePsToken(). The
 * interpreter and window are borrowed; this function does not preserve them.
 * Font and color mapping variable names initially are NULL.
 */
PsToken Rbc_GetPsToken(Tcl_Interp *interp, Tk_Window tkwin);
/**
 * Release a non-NULL token and its output buffer.
 *
 * Borrowed interpreter, window and mapping variable names are not released.
 * All pointers returned into the token become invalid.
 */
void Rbc_ReleasePsToken(PsToken psToken);
/**
 * Return the borrowed, NUL-terminated output buffer.
 *
 * Do not free the result. Appending output may relocate it; releasing the
 * token invalidates it. This accessor does not finalize a document.
 */
char *Rbc_PostScriptFromToken(PsToken psToken);
/**
 * Return borrowed mutable scratch storage of PSTOKEN_BUFSIZ + 1 bytes.
 *
 * Formatting and prolog reads reuse this storage. It is not the output buffer
 * and must not be retained across those operations or token release.
 */
char *Rbc_ScratchBufferFromToken(PsToken psToken);
/**
 * Append a sequence of NUL-terminated strings, ending with a NULL pointer.
 *
 * Strings are copied without escaping. Callers supply PostScript syntax and
 * must not pass binary data or pointers invalidated by output growth.
 */
void Rbc_AppendToPostScript(PsToken, ...);
/**
 * Append printf-formatted PostScript, using scratch storage or a temporary
 * allocation when the formatted result exceeds the scratch capacity.
 *
 * This void helper does not report a formatting failure to the interpreter.
 * It performs no PostScript string escaping.
 */
void Rbc_FormatToPostScript(PsToken, const char *, ...);
/**
 * Append escaped PostScript literal-string contents from Tcl UTF text.
 *
 * A negative length selects strlen; otherwise length is a byte count ending
 * at a character boundary. NULL input is ignored. The caller supplies the
 * surrounding parentheses. Delimiters, controls and non-ASCII characters use
 * octal escapes; characters above ISO-8859-1 are replaced with a space.
 */
void Rbc_AppendPostScriptString(PsToken psToken, const char *string, Tcl_Size length);
void Rbc_Draw3DRectangleToPostScript(PsToken psToken, Tk_3DBorder border, double x, double y, int width, int height,
                                     int borderWidth, int relief);
void Rbc_Fill3DRectangleToPostScript(PsToken psToken, Tk_3DBorder border, double x, double y, int width, int height,
                                     int borderWidth, int relief);
void Rbc_BackgroundToPostScript(PsToken psToken, XColor *colorPtr);
void Rbc_BitmapDataToPostScript(PsToken psToken, Display *display, Pixmap bitmap, int width, int height);
void Rbc_ClearBackgroundToPostScript(PsToken psToken);
int Rbc_ColorImageToPsData(Rbc_ColorImage image, int nComponents, Tcl_DString *resultPtr, char *prefix);
void Rbc_ColorImageToPostScript(PsToken psToken, Rbc_ColorImage image, double x, double y);
void Rbc_ForegroundToPostScript(PsToken psToken, XColor *colorPtr);
void Rbc_FontToPostScript(PsToken psToken, Tk_Font font);
void Rbc_WindowToPostScript(PsToken psToken, Tk_Window tkwin, double x, double y);
void Rbc_LineDashesToPostScript(PsToken psToken, Rbc_Dashes *dashesPtr);
void Rbc_LineWidthToPostScript(PsToken psToken, int lineWidth);
void Rbc_PathToPostScript(PsToken psToken, Point2D *screenPts, Tcl_Size nScreenPts);
void Rbc_PhotoToPostScript(PsToken psToken, Tk_PhotoHandle photoToken, double x, double y);
void Rbc_PolygonToPostScript(PsToken psToken, Point2D *screenPts, Tcl_Size nScreenPts);
void Rbc_LineToPostScript(PsToken psToken, XPoint *pointArr, int nPoints);
void Rbc_TextToPostScript(PsToken psToken, char *string, TextStyle *attrPtr, double x, double y);
void Rbc_RectangleToPostScript(PsToken psToken, double x, double y, int width, int height);
void Rbc_RegionToPostScript(PsToken psToken, double x, double y, int width, int height);
void Rbc_RectanglesToPostScript(PsToken psToken, XRectangle *rectArr, int nRects);
void Rbc_BitmapToPostScript(PsToken psToken, Display *display, Pixmap bitmap, double scaleX, double scaleY);
void Rbc_SegmentsToPostScript(PsToken psToken, XSegment *segArr, int nSegs);
void Rbc_StippleToPostScript(PsToken psToken, Display *display, Pixmap bitmap);
void Rbc_CapStyleToPostScript(PsToken psToken, int capStyle);
void Rbc_JoinStyleToPostScript(PsToken psToken, int joinStyle);
void Rbc_LineAttributesToPostScript(PsToken psToken, XColor *colorPtr, int lineWidth, Rbc_Dashes *dashesPtr,
                                    int capStyle, int joinStyle);
/**
 * Append a text prolog from fileName relative to the rbc::library variable.
 *
 * Return TCL_ERROR for library lookup, open or read failure, with an
 * interpreter diagnostic; otherwise return TCL_OK. Partial output is not
 * rolled back. The channel is closed, but its close status is not propagated.
 * Reads overwrite token scratch storage. Tcl variable and channel operations
 * may execute callbacks; the enclosing export owns lifetime protection.
 */
int Rbc_FileToPostScript(PsToken psToken, char *fileName);
void Rbc_2DSegmentsToPostScript(PsToken psToken, Segment2D *segments, Tcl_Size nSegments);

#endif /* _RBCPS */
