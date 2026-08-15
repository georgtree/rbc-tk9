/*
 * rbcInt.h --
 *
 *      This file constructs the basic functionality of the
 *      rbc commands.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCINT
#define _RBCINT

#ifdef WIN32
#include <windows.h>
#include <windowsx.h>
#endif /* WIN32 */

#define USE_NON_CONST
#include <tcl.h>

/* Backwards Compatiablitity with previous Tk versions */
#define USE_OLD_CANVAS

#include <tk.h>
#include "rbcTkInt.h"

#if !defined(INT2PTR)
#define INT2PTR(p) ((void *)(ptrdiff_t)(p))
#endif
#if !defined(PTR2INT)
#define PTR2INT(p) ((ptrdiff_t)(p))
#endif
#if !defined(UINT2PTR)
#define UINT2PTR(p) ((void *)(size_t)(p))
#endif
#if !defined(PTR2UINT)
#define PTR2UINT(p) ((size_t)(p))
#endif

#include <stdio.h>
#include <assert.h>

#if defined(WIN32)
#include "rbcWinConfig.h"
#else
#include "rbcConfig.h"
#endif

#ifdef WIN32
#ifndef EXPORT
#if defined(_MSC_VER) || defined(__BORLANDC__)
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif /* _MSC_VER || __BORLANDC__ */
#endif /* EXPORT */

/* Misc. definitions */
#define NO_CUTBUFFER 1
#define NO_TILESCROLLBAR 1
#define NO_DND 1

#ifndef __GNUC__
#ifdef O_NONBLOCK
#define O_NONBLOCK 1
#endif
#endif /* __GNUC__ */
#endif /* WIN32 */

#include "rbc.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */

#ifdef HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif /* HAVE_ERRNO_H */

#ifdef HAVE_CTYPE_H
#include <ctype.h>
#endif /* HAVE_CTYPE_H */

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif /* HAVE_MEMORY_H */

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#include "rbcMath.h"

#undef INLINE
#ifdef __GNUC__
#define INLINE inline
#else
#define INLINE
#endif
#undef EXPORT
#define EXPORT

#ifdef __cplusplus
#define ANYARGS (...)
#else
#define ANYARGS ()
#endif /* __cplusplus */

#undef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#undef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#undef MIN3
#define MIN3(a, b, c) (((a) < (b)) ? (((a) < (c)) ? (a) : (c)) : (((b) < (c)) ? (b) : (c)))

#undef MAX3
#define MAX3(a, b, c) (((a) > (b)) ? (((a) > (c)) ? (a) : (c)) : (((b) > (c)) ? (b) : (c)))

#define TRUE 1
#define FALSE 0

/*
 * The macro below is used to modify a "char" value (e.g. by casting
 * it to an unsigned character) so that it can be used safely with
 * macros such as isspace.
 */
#define UCHAR(c) ((unsigned char) (c))

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_Op --
 *
 *     Generic function prototype of CmdOptions.
 *
 * ----------------------------------------------------------------------
 */
typedef int (*Rbc_Op)(void);

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_OpSpec --
 *
 *      Describes one operation provided by a Tcl command.
 *
 *      minArgs and maxArgs count the complete command argument vector,
 *      including the command and operation names. A maxArgs value of
 *      zero means that the operation has no upper argument limit.
 *
 *      The generic Rbc_Op procedure representation is retained
 *      temporarily. Individual command families will replace it with
 *      properly typed operation tables during their Tcl_ObjCmdProc2
 *      conversion.
 *
 * ----------------------------------------------------------------------
 */
typedef struct {
    const char *name; /* Name of operation. */
    Rbc_Op proc;      /* Operation procedure. */
    Tcl_Size minArgs; /* Minimum number of arguments required. */
    Tcl_Size maxArgs; /* Maximum number of arguments, or zero. */
    const char *usage; /* Usage message following the operation name. */
} Rbc_OpSpec;

typedef enum {
    RBC_OP_ARG0, /* Op is the first argument. */
    RBC_OP_ARG1, /* Op is the second argument. */
    RBC_OP_ARG2, /* Op is the third argument. */
    RBC_OP_ARG3, /* Op is the fourth argument. */
    RBC_OP_ARG4  /* Op is the fifth argument. */
} Rbc_OpIndex;

typedef struct {
    const char *name;
    Tcl_Size minArgs;
    Tcl_Size maxArgs;
    const char *usage;
} Rbc_OpSpecHeader;

#define RBC_OPSPEC_END {NULL, NULL, 0, 0, NULL}

Rbc_Op Rbc_GetOpFromObj(Tcl_Interp *interp, const Rbc_OpSpec *specArr, Tcl_Size operPos, Tcl_Size objc,
                        Tcl_Obj *const objv[]);
int Rbc_GetOpIndexFromObj(Tcl_Interp *interp, const void *specArr, Tcl_Size specSize, Tcl_Size operPos, Tcl_Size objc,
                          Tcl_Obj *const objv[], int *indexPtr);

void Rbc_Draw3DRectangle(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                         int borderWidth, int relief);
void Rbc_Fill3DRectangle(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                         int borderWidth, int relief);


/* ---------------------------------------------------------------- */

#define PIXELS_NONNEGATIVE 0
#define PIXELS_POSITIVE 1
#define PIXELS_ANY 2

#define RBC_SCROLL_MODE_CANVAS (1 << 0)
#define RBC_SCROLL_MODE_LISTBOX (1 << 1)
#define RBC_SCROLL_MODE_HIERBOX (1 << 2)

#define RGB_ANTIQUEWHITE1 "#ffefdb"
#define RGB_BISQUE1 "#ffe4c4"
#define RGB_BISQUE2 "#eed5b7"
#define RGB_BISQUE3 "#cdb79e"
#define RGB_BLACK "#000000"
#define RGB_BLUE "#0000ff"
#define RGB_GREEN "#00ff00"
#define RGB_GREY "#b0b0b0"
#define RGB_GREY15 "#262626"
#define RGB_GREY50 "#7f7f7f"
#define RGB_GREY64 "#a3a3a3"
#define RGB_GREY70 "#b3b3b3"
#define RGB_GREY75 "#bfbfbf"
#define RGB_GREY77 "#c3c3c3"
#define RGB_GREY82 "#d1d1d1"
#define RGB_GREY85 "#d9d9d9"
#define RGB_GREY90 "#e5e5e5"
#define RGB_GREY95 "#f2f2f2"
#define RGB_LIGHTBLUE0 "#e4f7ff"
#define RGB_LIGHTBLUE1 "#bfefff"
#define RGB_LIGHTBLUE2 "#b2dfee"
#define RGB_LIGHTSKYBLUE1 "#b0e2ff"
#define RGB_MAROON "#b03060"
#define RGB_NAVYBLUE "#000080"
#define RGB_PINK "#ffc0cb"
#define RGB_BISQUE1 "#ffe4c4"
#define RGB_RED "#ff0000"
#define RGB_WHITE "#ffffff"
#define RGB_YELLOW "#ffff00"

#ifdef OLD_TK_COLORS
#define STD_NORMAL_BACKGROUND RGB_BISQUE1
#define STD_ACTIVE_BACKGROUND RGB_BISQUE2
#define STD_SELECT_BACKGROUND RGB_LIGHTBLUE1
#define STD_DISABLE_FOREGROUND RGB_GREY64
#else
#define STD_NORMAL_BACKGROUND RGB_GREY85
#define STD_ACTIVE_BACKGROUND RGB_GREY64
#define STD_SELECT_BACKGROUND RGB_LIGHTBLUE1
#define STD_DISABLE_FOREGROUND RGB_GREY64
#endif /* OLD_TK_COLORS */

#define STD_ACTIVE_BG_MONO RGB_BLACK
#define STD_ACTIVE_FOREGROUND RGB_BLACK
#define STD_ACTIVE_FG_MONO RGB_WHITE
#define STD_BORDERWIDTH "1"

#define STD_FONT "TkDefaultFont"
#define STD_FONT_HUGE "TkCaptionFont"
#define STD_FONT_LARGE "TkHeadingFont"
#define STD_FONT_SMALL "TkSmallCaptionFont"

#define STD_INDICATOR_COLOR RGB_MAROON
#define STD_NORMAL_BG_MONO RGB_WHITE
#define STD_NORMAL_FOREGROUND RGB_BLACK
#define STD_NORMAL_FG_MONO RGB_BLACK
#define STD_SELECT_BG_MONO RGB_BLACK
#define STD_SELECT_BORDERWIDTH "1"
#define STD_SELECT_FOREGROUND RGB_BLACK
#define STD_SELECT_FG_MONO RGB_WHITE
#define STD_SHADOW_COLOR RGB_GREY64
#define STD_SHADOW_MONO RGB_BLACK

static inline int Rbc_NativeLineWidth(int width) {
    if (width <= 1) {
        /*
         * Preserve the existing RBC policy: widths zero and one use
         * the native thin-line representation.
         */
        return 0;
    }
#ifndef WIN32
    /*
     * X11 represents GC line-width as CARD16.
     */
    if (width > USHRT_MAX) {
        return USHRT_MAX;
    }
#endif
    return width;
}

#define LineWidth(w) Rbc_NativeLineWidth((w))

typedef void *DestroyData;

#ifndef TK_RELIEF_SOLID
#define TK_RELIEF_SOLID TK_RELIEF_FLAT
#endif

#define NO_FLAGS 0

typedef int(QSortCompareProc)(const void *, const void *);

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_Pad --
 *
 *     Specifies vertical and horizontal padding.
 *
 *    Padding can be specified on a per side basis.  The fields
 *    side1 and side2 refer to the opposite sides, either
 *    horizontally or vertically.
 *
 *        side1    side2
 *              -----   -----
 *      x | left    right
 *        y | top     bottom
 *
 * ----------------------------------------------------------------------
 */
typedef struct {
    int side1, side2;
} Rbc_Pad;

#define padLeft padX.side1
#define padRight padX.side2
#define padTop padY.side1
#define padBottom padY.side2
#define PADDING(x) ((Tcl_WideInt)(x).side1 + (Tcl_WideInt)(x).side2)

int Rbc_GetPadFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, Rbc_Pad *padPtr);

/*
 * ----------------------------------------------------------------------
 *
 * The following enumerated values are used as bit flags.
 *    FILL_NONE        Neither coordinate plane is specified
 *    FILL_X            Horizontal plane.
 *    FILL_Y            Vertical plane.
 *    FILL_BOTH        Both vertical and horizontal planes.
 *
 * ----------------------------------------------------------------------
 */
#define FILL_NONE 0
#define FILL_X 1
#define FILL_Y 2
#define FILL_BOTH 3

#define RBC_MAX_DASH_VALUES 10

/*
 * Rbc_Dashes --
 *
 *     List of dash values.  One additional byte terminates the list.
 */
typedef struct {
    unsigned char values[RBC_MAX_DASH_VALUES + 1];
    int offset;
} Rbc_Dashes;

#define LineIsDashed(d) ((d).values[0] != 0)

int Rbc_GetDashesFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Rbc_Dashes *dashesPtr);
void Rbc_SetDashes(Display *display, GC gc, Rbc_Dashes *dashesPtr);
Rbc_Dashes *Rbc_GetDashes(GC gc);

/*
 * -------------------------------------------------------------------
 *
 * Point2D --
 *
 *    2-D coordinate.
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    double x, y;
} Point2D;

/*
 * -------------------------------------------------------------------
 *
 * Point3D --
 *
 *    3-D coordinate.
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    double x, y, z;
} Point3D;

/*
 * -------------------------------------------------------------------
 *
 * Segment2D --
 *
 *    2-D line segment.
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    Point2D p, q; /* The two end points of the segment. */
} Segment2D;

/*
 * -------------------------------------------------------------------
 *
 * Dim2D --
 *
 *    2-D dimension.
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    int width, height;
} Dim2D;

/*
 *----------------------------------------------------------------------
 *
 * Region2D --
 *
 *      2-D region.  Used to copy parts of images.
 *
 *----------------------------------------------------------------------
 */
typedef struct {
    int left, right, top, bottom;
} Region2D;

#define RegionWidth(r) ((r)->right - (r)->left + 1)
#define RegionHeight(r) ((r)->bottom - (r)->top + 1)

typedef struct {
    double left, right, top, bottom;
} Extents2D;

typedef struct {
    double left, right, top, bottom, front, back;
} Extents3D;

#define PointInRegion(e, x, y) (((x) <= (e)->right) && ((x) >= (e)->left) && ((y) <= (e)->bottom) && ((y) >= (e)->top))

#define PointInRectangle(r, x0, y0)                                                                                    \
    (((x0) <= (int)((r)->x + (r)->width - 1)) && ((x0) >= (int)(r)->x) && ((y0) <= (int)((r)->y + (r)->height - 1)) && \
     ((y0) >= (int)(r)->y))

/* -------------------------------------------------------------------
 *
 * ColorPair --
 *
 *    Holds a pair of foreground, background colors.
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    XColor *fgColor, *bgColor;
} ColorPair;

#define COLOR_NONE (XColor *)0
#define COLOR_DEFAULT (XColor *)1
#define COLOR_ALLOW_DEFAULTS 1

void Rbc_FreeColorPair(ColorPair *pairPtr);

#define STATE_NORMAL 0
#define STATE_ACTIVE (1 << 0)
#define STATE_DISABLED (1 << 1)
#define STATE_EMPHASIS (1 << 2)

#include "rbcText.h"

/*
 * ----------------------------------------------------------------------
 *
 *     X11/Xosdefs.h requires XNOSTDHDRS be set for some systems.
 *    This is a guess.  If I can't find STDC headers or unistd.h,
 *    assume that this is non-POSIX and non-STDC environment.
 *    (needed for Encore Umax 3.4 ?)
 *
 * ----------------------------------------------------------------------
 */
#if !defined(STDC_HEADERS) && !defined(HAVE_UNISTD_H)
#define XNOSTDHDRS 1
#endif

char *Rbc_Itoa(int value);
char *Rbc_Utoa(unsigned int value);
char *Rbc_Dtoa(Tcl_Interp *interp, double value);

int Rbc_NaturalSpline(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
int Rbc_QuadraticSpline(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
Tcl_Size Rbc_SimplifyLine(const Point2D *origPts, Tcl_Size low, Tcl_Size high, double tolerance, Tcl_Size indices[]);
Tcl_Size Rbc_NaturalParametricSpline(const Point2D *origPts, Tcl_Size nOrigPts, const Extents2D *extsPtr, int isClosed,
                                     Point2D *intpPts, Tcl_Size nIntpPts);
int Rbc_CatromParametricSpline(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
int Rbc_GetShadowFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, Shadow *shadowPtr);
int Rbc_GetFillFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *fillPtr);
int Rbc_GetStateFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *statePtr);
int Rbc_GetStringListFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, char ***listPtrPtr);
void Rbc_InitHexTable(char *table);
GC Rbc_GetPrivateGC(Tk_Window tkwin, unsigned long gcMask, XGCValues *valuePtr);
GC Rbc_GetPrivateGCFromDrawable(Display *display, Drawable drawable, unsigned long gcMask, XGCValues *valuePtr);
void Rbc_FreePrivateGC(Display *display, GC gc);
int Rbc_GetPixels(Tcl_Interp *, Tk_Window, const char *, int, int *);
int Rbc_GetPixelsFromObj(Tcl_Interp *, Tk_Window, Tcl_Obj *, int, int *);
int Rbc_GetXY(Tcl_Interp *interp, Tk_Window tkwin, const char *string, int *x, int *y);
void Rbc_DStringAppendElements(Tcl_DString *, ...);
void Rbc_GetBoundingBox(int width, int height, double theta, double *widthPtr, double *heightPtr, Point2D *points);
void Rbc_InitEpsCanvasItem(Tcl_Interp *interp);
void Rbc_TranslateAnchor(int x, int y, int width, int height, Tk_Anchor anchor, int *transXPtr, int *transYPtr);
Point2D Rbc_TranslatePoint(Point2D *pointPtr, int width, int height, Tk_Anchor anchor);
Tk_Window Rbc_FindChild(Tk_Window parent, const char *name);
int Rbc_InitComponentOptions(Tcl_Interp *interp, Tk_Window parent, const char *name, const char *className,
                             char *recordPtr, Tk_OptionTable optionTable);
int Rbc_MaxRequestSize(Display *display, unsigned int elemSize);
Window Rbc_GetRealWindowId(Tk_Window tkwin);
ClientData Rbc_GetWindowInstanceData(Tk_Window tkwin);
void Rbc_SetWindowInstanceData(Tk_Window tkwin, ClientData instanceData);
void Rbc_DeleteWindowInstanceData(Tk_Window tkwin);
int Rbc_AdjustViewport(int offset, int worldSize, int windowSize, int scrollUnits, int scrollMode);
int Rbc_GetScrollInfo(Tcl_Interp *interp, int argc, char **argv, int *offsetPtr, int worldSize, int windowSize,
                      int scrollUnits, int scrollMode);
int Rbc_GetScrollInfoFromObj(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], int *offsetPtr, int worldSize,
                             int windowSize, int scrollUnits, int scrollMode);
const char *Rbc_GetCanonicalOptionName(Tcl_Obj *objPtr, const Tk_OptionSpec *specs);

#if defined(HAVE_JPEGLIB_H) || defined(HAVE_IJL_H)
#define HAVE_JPEG 1
int Rbc_JPEGToPhoto(Tcl_Interp *interp, const char *fileName, Tk_PhotoHandle photo);
#endif /* HAVE_JPEGLIB_H || HAVE_IJL_H */

#define Rbc_SetBooleanResult(i, b) Tcl_SetResult((i), (b) ? "1" : "0", TCL_STATIC)

/*
 * Define this if you want to be able to tile to the main window "."
 * This will cause a conflict with Tk if you try to compile and link
 * statically.
 */
#undef TILE_MAINWINDOW

#ifdef WIN32
#if (TCL_MAJOR_VERSION == 8) && (TCL_MINOR_VERSION == 0)
#else
#define NO_DDE 1
#endif
#else
#define NO_DDE 1
#define NO_PRINTER 1
#endif /* WIN32 */

#if (TCL_MAJOR_VERSION == 7)
#define NO_TREE 1
#define NO_ARRAY 1
#define NO_TREEVIEW 1
#endif

#ifndef NO_GRAPH
Tcl_AppInitProc Rbc_GraphInit;
#endif
#ifndef NO_VECTOR
Tcl_AppInitProc Rbc_VectorInit;
#endif
#ifndef NO_WINOP
Tcl_AppInitProc Rbc_WinopInit;
#endif
#ifndef NO_SPLINE
Tcl_AppInitProc Rbc_SplineInit;
#endif

char *RbcStrdup(const char *ptr);
void *RbcCalloc(size_t nElem, size_t size);

#ifdef WIN32
#include "rbcWin.h"
#endif

#ifndef WIN32
#define PurifyPrintf printf
#endif /* WIN32 */

#endif /* _RBCINT */
