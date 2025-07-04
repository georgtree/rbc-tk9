/*
 * rbcWin.h --
 *
 *      TODO: Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCWIN
#define _RBCWIN

#ifdef WIN32
#include <windows.h>
#include <windowsx.h>
#endif /* WIN32 */

#undef STD_NORMAL_BACKGROUND
#undef STD_NORMAL_FOREGROUND
#undef STD_SELECT_BACKGROUND
#undef STD_SELECT_FOREGROUND
#undef STD_TEXT_FOREGROUND
#undef STD_FONT
#undef STD_FONT_LARGE
#undef STD_FONT_SMALL

#define STD_NORMAL_BACKGROUND "SystemButtonFace"
#define STD_NORMAL_FOREGROUND "SystemButtonText"
#define STD_SELECT_BACKGROUND "SystemHighlight"
#define STD_SELECT_FOREGROUND "SystemHighlightText"
#define STD_TEXT_FOREGROUND "SystemWindowText"
#define STD_FONT "Arial 8"
#define STD_FONT_LARGE "Arial 12"
#define STD_FONT_SMALL "Arial 6"

#ifdef CHECK_UNICODE_CALLS
#define _UNICODE
#define UNICODE
#define __TCHAR_DEFINED
typedef float *_TCHAR;
#define _TCHAR_DEFINED
typedef float *TCHAR;
#endif /* CHECK_UNICODE_CALLS */

/* DOS Encapsulated PostScript File Header */
#pragma pack(2)
typedef struct {
    BYTE magic[4];    /* Magic number for a DOS EPS file
                       * C5,D0,D3,C6 */
    DWORD psStart;    /* Offset of PostScript section. */
    DWORD psLength;   /* Length of the PostScript section. */
    DWORD wmfStart;   /* Offset of Windows Meta File section. */
    DWORD wmfLength;  /* Length of Meta file section. */
    DWORD tiffStart;  /* Offset of TIFF section. */
    DWORD tiffLength; /* Length of TIFF section. */
    WORD checksum;    /* Checksum of header. If FFFF, ignore. */
} DOSEPSHEADER;
#pragma pack()

/* Aldus Portable Metafile Header */
#pragma pack(2)
typedef struct {
    DWORD key;       /* Type of metafile */
    WORD hmf;        /* Unused. Must be NULL. */
    SMALL_RECT bbox; /* Bounding rectangle */
    WORD inch;       /* Units per inch. */
    DWORD reserved;  /* Unused. */
    WORD checksum;   /* XOR of previous fields (10 32-bit words). */
} APMHEADER;
#pragma pack()

extern int Rbc_AsyncRead(int fd, char *buffer, unsigned int size);
extern int Rbc_AsyncWrite(int fd, char *buffer, unsigned int size);
extern void Rbc_CreateFileHandler(int fd, int flags, Tcl_FileProc *proc, ClientData clientData);
extern void Rbc_DeleteFileHandler(int fd);
extern int Rbc_GetPlatformId();
extern char *Rbc_LastError();

#undef EXPORT
#define EXPORT __declspec(dllexport)

#if defined(_MSC_VER) && _MSC_VER <= 1400
#define isnan(x) _isnan(x)
#endif /* _MSC_VER */

#ifdef __BORLANDC__
#define isnan(x) _isnan(x)
#endif

#if defined(__BORLANDC__) || defined(_MSC_VER)
#ifdef FINITE
#undef FINITE
#define FINITE(x) _finite(x)
#endif
#endif /* __BORLANDC__ || _MSC_VER */

#ifdef __GNUC__
#include <windowsx.h>
#include <wingdi.h>
#undef Status
#include <winspool.h>
#define Status int
/*
 * Add definitions missing from windgi.h, windowsx.h, and winspool.h
 */
#endif /* __GNUC__ */

/*
#define XCopyArea        Rbc_EmulateXCopyArea
#define XCopyPlane        Rbc_EmulateXCopyPlane
*/
#undef XDrawArcs
#define XDrawArcs Rbc_EmulateXDrawArcs
/*
#define XDrawLine        Rbc_EmulateXDrawLine
#define XDrawLines        Rbc_EmulateXDrawLines
*/
#undef XDrawPoints
#define XDrawPoints Rbc_EmulateXDrawPoints
/*
#define XDrawRectangle        Rbc_EmulateXDrawRectangle
*/
#undef XDrawRectangles
#define XDrawRectangles Rbc_EmulateXDrawRectangles
#undef XDrawSegments
#define XDrawSegments Rbc_EmulateXDrawSegments
#undef XDrawString
#define XDrawString Rbc_EmulateXDrawString
#undef XFillArcs
#define XFillArcs Rbc_EmulateXFillArcs
/*
#define XFillPolygon        Rbc_EmulateXFillPolygon
#define XFillRectangle        Rbc_EmulateXFillRectangle
#define XFillRectangles        Rbc_EmulateXFillRectangles
#define XFree            Rbc_EmulateXFree
#define XGetWindowAttributes    Rbc_EmulateXGetWindowAttributes
*/
#undef XLowerWindow
#define XLowerWindow Rbc_EmulateXLowerWindow
#define XMaxRequestSize Rbc_EmulateXMaxRequestSize
/*
#define XRaiseWindow        Rbc_EmulateXRaiseWindow
*/
/*
#define XSetDashes        Rbc_EmulateXSetDashes
#define XUnmapWindow        Rbc_EmulateXUnmapWindow
#define XWarpPointer        Rbc_EmulateXWarpPointer
*/

EXTERN GC Rbc_EmulateXCreateGC(Display *display, Drawable drawable, unsigned long mask, XGCValues *valuesPtr);
EXTERN void Rbc_EmulateXCopyArea(Display *display, Drawable src, Drawable dest, GC gc, int src_x, int src_y,
                                 unsigned int width, unsigned int height, int dest_x, int dest_y);
EXTERN void Rbc_EmulateXCopyPlane(Display *display, Drawable src, Drawable dest, GC gc, int src_x, int src_y,
                                  unsigned int width, unsigned int height, int dest_x, int dest_y, unsigned long plane);
EXTERN void Rbc_EmulateXDrawArcs(Display *display, Drawable drawable, GC gc, XArc *arcArr, int nArcs);
EXTERN void Rbc_EmulateXDrawLine(Display *display, Drawable drawable, GC gc, int x1, int y1, int x2, int y2);
EXTERN void Rbc_EmulateXDrawLines(Display *display, Drawable drawable, GC gc, XPoint *pointArr, int nPoints, int mode);
EXTERN void Rbc_EmulateXDrawPoints(Display *display, Drawable drawable, GC gc, XPoint *pointArr, int nPoints, int mode);
EXTERN void Rbc_EmulateXDrawRectangle(Display *display, Drawable drawable, GC gc, int x, int y, unsigned int width,
                                      unsigned int height);
EXTERN void Rbc_EmulateXDrawRectangles(Display *display, Drawable drawable, GC gc, XRectangle *rectArr, int nRects);
EXTERN void Rbc_EmulateXDrawSegments(Display *display, Drawable drawable, GC gc, XSegment *segArr, int nSegments);
EXTERN void Rbc_EmulateXDrawSegments(Display *display, Drawable drawable, GC gc, XSegment *segArr, int nSegments);
EXTERN void Rbc_EmulateXDrawString(Display *display, Drawable drawable, GC gc, int x, int y, _Xconst char *string,
                                   int length);
EXTERN void Rbc_EmulateXFillArcs(Display *display, Drawable drawable, GC gc, XArc *arcArr, int nArcs);
EXTERN void Rbc_EmulateXFillPolygon(Display *display, Drawable drawable, GC gc, XPoint *points, int nPoints, int shape,
                                    int mode);
EXTERN void Rbc_EmulateXFillRectangle(Display *display, Drawable drawable, GC gc, int x, int y, unsigned int width,
                                      unsigned int height);
EXTERN void Rbc_EmulateXFillRectangles(Display *display, Drawable drawable, GC gc, XRectangle *rectArr, int nRects);
EXTERN void Rbc_EmulateXFree(void *ptr);
EXTERN int Rbc_EmulateXGetWindowAttributes(Display *display, Window window, XWindowAttributes *attrsPtr);
EXTERN void Rbc_EmulateXLowerWindow(Display *display, Window window);
EXTERN void Rbc_EmulateXMapWindow(Display *display, Window window);
EXTERN long Rbc_EmulateXMaxRequestSize(Display *display);
EXTERN void Rbc_EmulateXRaiseWindow(Display *display, Window window);
EXTERN void Rbc_EmulateXSetDashes(Display *display, GC gc, int dashOffset, _Xconst char *dashList, int n);
EXTERN void Rbc_EmulateXUnmapWindow(Display *display, Window window);
EXTERN void Rbc_EmulateXWarpPointer(Display *display, Window srcWindow, Window destWindow, int srcX, int srcY,
                                    unsigned int srcWidth, unsigned int srcHeight, int destX, int destY);

extern unsigned char *Rbc_GetBitmapData(Display *display, Pixmap bitmap, int width, int height, int *pitchPtr);

extern HPALETTE Rbc_GetSystemPalette();

extern HPEN Rbc_GCToPen(HDC dc, GC gc);

#endif /*_RBCWIN*/
