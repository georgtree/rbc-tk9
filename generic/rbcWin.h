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

typedef struct Rbc_WinDrawableDC Rbc_WinDrawableDC;
HDC Rbc_WinAcquireDrawableDC(Display *display, Drawable drawable, Rbc_WinDrawableDC **statePtrPtr);
void Rbc_WinReleaseDrawableDC(Rbc_WinDrawableDC *statePtr);
void Rbc_WinSetROP2(HDC dc, int function);

extern int Rbc_AsyncRead(int fd, char *buffer, unsigned int size);
extern int Rbc_AsyncWrite(int fd, char *buffer, unsigned int size);
extern void Rbc_CreateFileHandler(int fd, int flags, Tcl_FileProc *proc, ClientData clientData);
extern void Rbc_DeleteFileHandler(int fd);
extern char *Rbc_LastError(void);

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

#undef XDrawArcs
#define XDrawArcs Rbc_EmulateXDrawArcs
#undef XDrawPoints
#define XDrawPoints Rbc_EmulateXDrawPoints
#undef XDrawRectangles
#define XDrawRectangles Rbc_EmulateXDrawRectangles
#undef XDrawSegments
#define XDrawSegments Rbc_EmulateXDrawSegments
#undef XDrawString
#define XDrawString Rbc_EmulateXDrawString
#undef XFillArcs
#define XFillArcs Rbc_EmulateXFillArcs

#undef XLowerWindow
#define XLowerWindow Rbc_EmulateXLowerWindow
#define XMaxRequestSize Rbc_EmulateXMaxRequestSize


EXTERN void Rbc_EmulateXDrawArcs(Display *display, Drawable drawable, GC gc, XArc *arcArr, int nArcs);
EXTERN void Rbc_EmulateXDrawPoints(Display *display, Drawable drawable, GC gc, XPoint *pointArr, int nPoints, int mode);
EXTERN void Rbc_EmulateXDrawRectangles(Display *display, Drawable drawable, GC gc, XRectangle *rectArr, int nRects);
EXTERN void Rbc_EmulateXDrawSegments(Display *display, Drawable drawable, GC gc, XSegment *segArr, int nSegments);
EXTERN void Rbc_EmulateXDrawString(Display *display, Drawable drawable, GC gc, int x, int y, _Xconst char *string,
                                   int length);
EXTERN void Rbc_EmulateXFillArcs(Display *display, Drawable drawable, GC gc, XArc *arcArr, int nArcs);
EXTERN void Rbc_EmulateXLowerWindow(Display *display, Window window);
EXTERN long Rbc_EmulateXMaxRequestSize(Display *display);
Drawable Rbc_WinCreateDrawableFromDC(HDC dc);
void Rbc_WinFreeDrawableFromDC(Drawable drawable);
Window Rbc_WinWindowFromHWND(HWND hWnd);

extern unsigned char *Rbc_GetBitmapData(Display *display, Pixmap bitmap, int width, int height, int *pitchPtr);

extern HPALETTE Rbc_GetSystemPalette(void);

extern HPEN Rbc_GCToPen(HDC dc, GC gc);

#endif /*_RBCWIN*/
