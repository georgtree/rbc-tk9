/** @file rbcInt.h
 * @brief Private shared infrastructure, including component option services.
 */
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

#include <tcl.h>

#include <tk.h>

/* Tk 9.1 requires signed pixel options to opt in; Tk 9.0 allows them. */
#ifndef TK_OPTION_NEG_OK
#define TK_OPTION_NEG_OK 0
#endif

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

#include <stdint.h>
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
#include "rbcTypes.h"

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

int Rbc_GetOpIndexFromObj(Tcl_Interp *interp, const void *specArr, Tcl_Size specSize, Tcl_Size operPos, Tcl_Size objc,
                          Tcl_Obj *const objv[], int *indexPtr);
void Rbc_Draw3DRectangle(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                         int borderWidth, int relief);


/* ---------------------------------------------------------------- */

#define PIXELS_NONNEGATIVE 0
#define PIXELS_POSITIVE 1
#define PIXELS_ANY 2

#define RBC_SCROLL_MODE_CANVAS (1 << 0)
#define RBC_SCROLL_MODE_LISTBOX (1 << 1)
#define RBC_SCROLL_MODE_HIERBOX (1 << 2)

#include "rbcDefaults.h"

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

int Rbc_GetDashesFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Rbc_Dashes *dashesPtr);
void Rbc_SetDashes(Display *display, GC gc, Rbc_Dashes *dashesPtr);
Rbc_Dashes *Rbc_GetDashes(GC gc);

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
double Rbc_RandomDouble(void);

int Rbc_NaturalSpline(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
int Rbc_QuadraticSpline(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
int Rbc_NaturalParametricSplineEval(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
int Rbc_QuadraticParametricSplineEval(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
Tcl_Size Rbc_NaturalParametricSpline(const Point2D *origPts, Tcl_Size nOrigPts, const Extents2D *extsPtr, int isClosed,
                                     Point2D *intpPts, Tcl_Size nIntpPts);
int Rbc_CatromParametricSpline(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
double Rbc_GetClosestPointOnSegment(const Point2D *samplePtr, const Point2D *p, const Point2D *q, Point2D *closestPtr);
int Rbc_LineRectClip(const Extents2D *extsPtr, const Point2D *p, const Point2D *q, Segment2D *clippedPtr);
Tcl_Size Rbc_PolyRectClip(const Extents2D *extsPtr, const Point2D *inputPts, Tcl_Size nInputPts, Point2D *outputPts,
                          Tcl_Size outputCapacity);
int Rbc_PointInPolygon(const Point2D *samplePtr, const Point2D *points, Tcl_Size nPoints);
int Rbc_RegionInPolygon(const Extents2D *extsPtr, const Point2D *points, Tcl_Size nPoints, int enclosed);
int Rbc_PointInSegments(const Point2D *samplePtr, const Segment2D *segments, Tcl_Size nSegments, double halo);
Tcl_Size Rbc_SimplifyLine(const Point2D *origPts, Tcl_Size low, Tcl_Size high, double tolerance, Tcl_Size indices[]);
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
void Rbc_AppendResultStrings(Tcl_Interp *interp, ...);
void Rbc_GetBoundingBox(int width, int height, double theta, double *widthPtr, double *heightPtr, Point2D *points);
void Rbc_InitEpsCanvasItem(Tcl_Interp *interp);
void Rbc_TranslateAnchor(int x, int y, int width, int height, Tk_Anchor anchor, int *transXPtr, int *transYPtr);
Point2D Rbc_TranslatePoint(Point2D *pointPtr, int width, int height, Tk_Anchor anchor);
Tk_Window Rbc_FindChild(Tk_Window parent, const char *name);
/**
 * @brief Roll back options after validation of a successful Tk_SetOptions fails.
 * @param interp Interpreter holding the original validation error.
 * @param savedOptionsPtr Successful option transaction to consume and restore.
 * @return TCL_ERROR with the original result and return options preserved.
 *
 * Clear borrowed argument vectors and transaction context before calling.
 * Derived resources remain the caller's responsibility: do not commit them
 * before all fallible validation succeeds. Do not call after Tk_SetOptions
 * itself fails, or call Tk_FreeSavedOptions after this helper.
 */
int Rbc_RestoreOptionsOnError(Tcl_Interp *interp, Tk_SavedOptions *savedOptionsPtr);
/**
 * @brief Copy a component name using the legacy resource-name convention.
 * @param name Non-NULL component name; component identity is not modified.
 * @return Allocated copy owned by the caller; release with ckfree.
 *
 * Only the first byte is lowercased using tolower. The rest is unchanged;
 * this is not Unicode case folding. An empty name produces an empty copy.
 */
char *Rbc_GetComponentResourceName(const char *name);
int Rbc_ClampToInt(Tcl_WideInt value);
/**
 * @brief Initialize a component record with its option-database identity.
 * @param interp Interpreter receiving initialization errors.
 * @param parent Owning widget window.
 * @param proxy Graph-owned anonymous option window, or NULL for parent fallback.
 * @param name Resource name, already normalized by the caller if required.
 * @param className Component resource class used for the proxy.
 * @param recordPtr Component record accepted by Tk_InitOptions.
 * @param optionTable Tk option table describing the record.
 * @return Result of Tk_InitOptions (TCL_OK or TCL_ERROR).
 *
 * A matching real child is used first, retaining its class. Otherwise reuse
 * the proxy with the supplied name and class, or use parent when proxy is NULL.
 * No window is created or destroyed here. The temporary proxy diagnostic path
 * is cleared before returning. Record resources and proxy lifetime remain with
 * the caller; this helper does not configure explicit command arguments.
 */
int Rbc_InitComponentOptions(Tcl_Interp *interp, Tk_Window parent, Tk_Window proxy, const char *name,
                             const char *className, char *recordPtr, Tk_OptionTable optionTable);
int Rbc_MaxRequestSize(Display *display, unsigned int elemSize);
Window Rbc_GetRealWindowId(Tk_Window tkwin);
ClientData Rbc_GetWindowInstanceData(Tk_Window tkwin);
void Rbc_SetWindowInstanceData(Tk_Window tkwin, ClientData instanceData);
void Rbc_DeleteWindowInstanceData(Tk_Window tkwin);
int Rbc_AdjustViewport(int offset, int worldSize, int windowSize, int scrollUnits, int scrollMode);
typedef enum {RBC_SCROLL_MOVETO, RBC_SCROLL_UNITS, RBC_SCROLL_PAGES} Rbc_ScrollKind;
typedef struct {
    Rbc_ScrollKind kind;
    double value;
} Rbc_ScrollRequest;
/* legacy permits axis abbreviations and a single numeric unit count. */
int Rbc_ParseScrollRequest(Tcl_Interp *, Tcl_Size, Tcl_Obj *const [], Tcl_Size, int, Rbc_ScrollRequest *);

int Rbc_GetScrollInfoFromObj(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], int *offsetPtr, int worldSize,
                             int windowSize, int scrollUnits, int scrollMode);
/**
 * @brief Look up a custom component option using Tcl's indexed-name matching.
 *
 * Arguments are interpreter, option-name object, specification array,
 * diagnostic label and output index. The array must contain concrete options
 * (no synonyms), terminate with TK_OPTION_END, and outlive Tcl objects caching
 * its entries. Unique abbreviations are accepted.
 * @return TCL_OK with the selected index, or TCL_ERROR with a diagnostic.
 */
int Rbc_GetComponentOptionIndex(Tcl_Interp *, Tcl_Obj *, const Tk_OptionSpec *, const char *, int *);
/**
 * @brief Build configure metadata from one specification and its live value.
 *
 * Arguments are a concrete specification with non-NULL optionName, dbName,
 * dbClass and defValue strings, and a valid Tcl value object.
 * @return New zero-reference-count list containing option name, resource name,
 * resource class, default and supplied value. The list retains the value;
 * the caller retains any references it already owns.
 *
 * No option table or synthetic component record is allocated for this query.
 */
Tcl_Obj *Rbc_GetComponentOptionInfo(const Tk_OptionSpec *, Tcl_Obj *);
/**
 * @brief Resolve an option spelling for configuration bookkeeping.
 * @param objPtr Option-name object, or NULL.
 * @param specs Complete specification table, or NULL; chained tables are followed.
 * @return Borrowed canonical name from the specification, or NULL for an empty,
 * missing or ambiguous match. No interpreter diagnostic is produced.
 *
 * Resolves synonyms through their clientData canonical name. Use the same
 * specification as Tk_SetOptions; this helper does not parse option values.
 */
const char *Rbc_GetCanonicalOptionName(Tcl_Obj *objPtr, const Tk_OptionSpec *specs);

#if defined(HAVE_JPEGLIB_H) || defined(HAVE_IJL_H)
#define HAVE_JPEG 1
int Rbc_JPEGToPhoto(Tcl_Interp *interp, const char *fileName, Tk_PhotoHandle photo);
#endif /* HAVE_JPEGLIB_H || HAVE_IJL_H */

#define Rbc_SetBooleanResult(i, b) Tcl_SetObjResult((i), Tcl_NewBooleanObj((b) != 0))

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
