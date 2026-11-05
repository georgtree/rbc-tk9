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

/**
 * @brief Common first member of persistent typed operation-table entries.
 *
 * Terminate a table with a NULL name. Bounds count the complete command word
 * vector; maxArgs <= 0 is unbounded. The enclosing entry supplies its own
 * correctly typed callback, and its sizeof is the lookup stride.
 */
typedef struct {
    const char *name;    /**< Borrowed persistent operation name. */
    Tcl_Size minArgs;    /**< Minimum total argument count. */
    Tcl_Size maxArgs;    /**< Maximum total count, or nonpositive for no maximum. */
    const char *usage;   /**< Borrowed suffix for Tcl_WrongNumArgs. */
} Rbc_OpSpecHeader;

/**
 * @brief Select a typed operation-table entry and validate total argument count.
 *
 * Require a persistent NULL-name-terminated table whose entries start with
 * Rbc_OpSpecHeader and have byte stride specSize. Tcl lookup accepts unique
 * abbreviations and caches the table in the operation object. On success assign
 * indexPtr, not a function pointer; the caller dispatches through its typed table.
 * minArgs/maxArgs count all objc words; maxArgs <= 0 means unbounded. Leave the
 * output unchanged on error and set an interpreter diagnostic.
 *
 * @param[in,out] interp Borrowed result interpreter; see @ref rbc_interp_argument.
 * @param[in] specArr Persistent NULL-name-terminated table with Rbc_OpSpecHeader as the first field of each entry;
 *   Tcl may cache this address.
 * @param[in] specSize Byte stride of one complete typed entry, at least sizeof(Rbc_OpSpecHeader).
 * @param[in] operPos Nonnegative index of the operation word in the full command array.
 * @param[in] objc Number of objects in objv; counts every word when validating operation arity.
 * @param[in] objv Borrowed full command array; see @ref rbc_command_arguments.
 * @param[out] indexPtr Required output receiving the selected table index only on success.
 */
int Rbc_GetOpIndexFromObj(Tcl_Interp *interp, const void *specArr, Tcl_Size specSize, Tcl_Size operPos, Tcl_Size objc,
                          Tcl_Obj *const objv[], int *indexPtr);
/**
 * @brief Draw the legacy RBC native relief border inside a rectangle.
 *
 * Borrow window, drawable and border. For raised/sunken borders wider than one
 * pixel and dimensions above two, draw an outer edge before delegating the inset
 * to Tk_Draw3DRectangle. Edge GCs differ between X11 and Windows. This shared
 * Tk utility does not establish a graph renderer context or clipping region.
 *
 * @param[in] tkwin Borrowed Tk window supplying display and compatible resources.
 * @param[in] drawable Borrowed destination drawable.
 * @param[in] border Borrowed Tk border supplying relief colors.
 * @param[in] x Rectangle left edge in drawable pixels.
 * @param[in] y Rectangle top edge in drawable pixels.
 * @param[in] width Rectangle width in pixels.
 * @param[in] height Rectangle height in pixels.
 * @param[in] borderWidth Requested border thickness in pixels.
 * @param[in] relief Tk relief selector passed through to native/Tk border drawing.
 */
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

/**
 * @brief Convert integer width to the native thin-line convention.
 *
 * Returns a native GC line width. Does not convert physical units or mutate a GC.
 * @param[in] width Requested integer width; values at most one map to zero, larger values are unchanged.
 */
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

/**
 * @brief Parse one or two nonnegative screen distances into a staged padding value.
 *
 * One distance sets both sides; two preserve order. Borrow the Tcl list and
 * window. Only assign padPtr after all conversions succeed; errors leave the
 * previous padding unchanged. No resources are allocated into the result.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed live Tk window supplying screen-unit and resource context; no ownership is
 *   acquired.
 * @param[in] objPtr Borrowed non-NULL Tcl input object, kept valid by the caller; not retained here.
 * @param[out] padPtr Required padding output; both sides are committed together only on success.
 */
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

/**
 * @brief Parse named or expression-valued dashes into a staged descriptor.
 *
 * NULL/empty input or the singleton zero yields solid lines. Named patterns
 * are exact matches; lists have at most RBC_MAX_DASH_VALUES entries in 1..255.
 * Evaluate list entries through Tcl_ExprLongObj, so arbitrary Tcl callbacks are
 * possible. Commit only on success and reset the descriptor offset to zero.
 * The helper does not preserve the owning component across those callbacks.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed pattern or expression-list object; NULL or empty selects solid lines. Caller keeps
 *   the input and destination valid across expression callbacks.
 * @param[out] dashesPtr Required descriptor; changed only on success, with offset reset to zero.
 */
int Rbc_GetDashesFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Rbc_Dashes *dashesPtr);
/**
 * @brief Set dash bytes and offset on a caller-mutable GC.
 *
 * Read at most RBC_MAX_DASH_VALUES bytes, stopping at zero, and call XSetDashes.
 * Borrow the descriptor; do not alter a shared Tk GC unless its owner explicitly
 * permits mutation. This helper does not select the GC line style.
 *
 * @param[in] display Borrowed display owning gc.
 * @param[in,out] gc Caller-mutable GC receiving dash bytes and offset; ownership is unchanged.
 * @param[in] dashesPtr Borrowed dash descriptor with a readable RBC_MAX_DASH_VALUES array and pixel offset.
 */
void Rbc_SetDashes(Display *display, GC gc, Rbc_Dashes *dashesPtr);

/** @brief Foreground/background color pair; ownership belongs to the option or drawing owner. */
typedef struct {
    XColor *fgColor; /**< Foreground color, possibly a component-specific sentinel. */
    XColor *bgColor; /**< Background color, or NULL when no background is requested. */
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

/**
 * @brief Format an int into borrowed shared scratch storage.
 *
 * Return a pointer to the same process-static buffer used by Rbc_Utoa and
 * Rbc_Dtoa. A subsequent call to any of them overwrites it. Do not free or
 * retain it without copying; concurrent calls are not synchronized.
 *
 * @param[in] value Signed integer to format in decimal; the returned shared buffer is overwritten by the next
 *   numeric formatting call.
 */
char *Rbc_Itoa(int value);
/**
 * @brief Format an unsigned int into borrowed shared scratch storage.
 *
 * Shares the buffer and overwrite/threading restrictions of Rbc_Itoa.
 * The returned decimal string is not caller-owned.
 *
 * @param[in] value Unsigned integer to format in decimal; no separate result allocation is made.
 */
char *Rbc_Utoa(unsigned int value);
/**
 * @brief Format a double through Tcl_PrintDouble into shared scratch storage.
 *
 * Borrow interp. Shares the buffer with Rbc_Itoa and Rbc_Utoa; copy the result
 * before another formatting call and never free the returned pointer.
 *
 * @param[in] interp Borrowed Tcl numeric-formatting context passed to Tcl_PrintDouble; not retained.
 * @param[in] value Double to format using Tcl rules into the shared numeric scratch buffer.
 */
char *Rbc_Dtoa(Tcl_Interp *interp, double value);
/**
 * @brief Return a rand-derived value in the half-open interval [0, 1).
 *
 * Consumes the C library PRNG state and does not seed it. No interpreter-local
 * state, reproducibility across platforms or cryptographic quality is promised.
 */
double Rbc_RandomDouble(void);

/**
 * Evaluate a natural cubic spline at caller-supplied abscissas.
 * @param[in] origPts Borrowed, separate source array with finite coordinates and
 * strictly increasing X; at least three points are required.
 * @param[in] nOrigPts Number of source points.
 * @param[in,out] intpPts Caller-owned non-NULL array; X supplies finite query positions
 * in any order and is retained, Y receives interpolated values.
 * @param[in] nIntpPts Number of queries, nonnegative; zero succeeds after validation.
 * @return TRUE on success, FALSE on invalid input, allocation or arithmetic
 * failure. Output may be partially modified on failure; no Tcl diagnostic.
 *
 * Preserves exact source-knot values and extrapolates with the first/last
 * interval outside the source domain. Allocates and frees private workspace;
 * does not own either caller array or invoke Tcl callbacks.
 */
int Rbc_NaturalSpline(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
/**
 * Evaluate a shape-preserving quadratic spline at caller-supplied abscissas.
 * @param[in] origPts Borrowed, separate source array with finite coordinates and
 * strictly increasing X; at least three points are required.
 * @param[in] nOrigPts Number of source points.
 * @param[in,out] intpPts Caller-owned non-NULL array; X supplies finite query positions
 * in any order and is retained, Y receives interpolated values.
 * @param[in] nIntpPts Number of queries, nonnegative; zero succeeds after validation.
 * @return TRUE on success, FALSE on invalid input, allocation or arithmetic
 * failure. Output may be partially modified on failure; no Tcl diagnostic.
 *
 * Preserves exact source-knot values and extrapolates with the first/last
 * interval outside the source domain. Allocates and frees private workspace;
 * does not own either caller array or invoke Tcl callbacks.
 */
int Rbc_QuadraticSpline(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
/**
 * Evaluate an open natural cubic path at interval/fraction parameters.
 * @param[in] origPts Borrowed finite source points, at least three; X need not increase.
 * @param[in] nOrigPts Number of source points.
 * @param[in,out] intpPts Caller-owned non-NULL array. On entry X is an integral source
 * interval and Y is a fraction in [0,1]; on success both become coordinates.
 * The final knot is represented by interval nOrigPts-1 and fraction zero.
 * @param[in] nIntpPts Nonnegative number of parameters; zero succeeds after validation.
 * @return TRUE on success, FALSE on invalid input, allocation or arithmetic
 * failure. Output parameters are unchanged on failure; no Tcl diagnostic.
 *
 * Uses isotropic Euclidean chord lengths, rejecting degenerate intervals or
 * unusable accumulated lengths. Source knots are copied exactly. Stages output
 * separately before copying it back; releases all private workspace and invokes
 * no Tcl callbacks. Caller retains ownership of both arrays.
 */
int Rbc_NaturalParametricSplineEval(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
/**
 * Evaluate an open quadratic path at interval/fraction parameters.
 * @param[in] origPts Borrowed finite source points, at least three; X need not increase.
 * @param[in] nOrigPts Number of source points.
 * @param[in,out] intpPts Caller-owned non-NULL array. On entry X is an integral source
 * interval and Y is a fraction in [0,1]; on success both become coordinates.
 * The final knot is represented by interval nOrigPts-1 and fraction zero.
 * @param[in] nIntpPts Nonnegative number of parameters; zero succeeds after validation.
 * @return TRUE on success, FALSE on invalid input, allocation or arithmetic
 * failure. Output parameters are unchanged on failure; no Tcl diagnostic.
 *
 * Uses isotropic Euclidean chord lengths, rejecting degenerate intervals or
 * unusable accumulated lengths. Source knots are copied exactly. Stages output
 * separately before copying it back; releases all private workspace and invokes
 * no Tcl callbacks. Caller retains ownership of both arrays.
 */
int Rbc_QuadraticParametricSplineEval(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
/**
 * Sample a natural parametric path using an extent-scaled chord metric.
 * @param[in] origPts Borrowed finite source array, separate from output; at least three points.
 * @param[in] nOrigPts Number of source points.
 * @param[in] extsPtr Required extents defining X/Y metric scales. Absolute spans
 * smaller than FLT_EPSILON or non-finite spans are replaced by FLT_EPSILON.
 * @param[in] isClosed Nonzero requests a closed curve; a missing final copy of the
 * first point is appended in private storage.
 * @param[out] intpPts Caller-owned output array with room for nIntpPts coordinates.
 * @param[in] nIntpPts Requested sample count, at least two.
 * @return Generated count, or zero on failure. Require the full requested count
 * before accepting output; partial output may remain on failure.
 *
 * Samples approximately uniformly in chord parameter, not exact curve arc length.
 * Copies the first point and makes the last exact when the full count is produced.
 * Frees internal coefficients and any closure copy. No Tcl callbacks or diagnostic.
 */
Tcl_Size Rbc_NaturalParametricSpline(const Point2D *origPts, Tcl_Size nOrigPts, const Extents2D *extsPtr, int isClosed,
                                     Point2D *intpPts, Tcl_Size nIntpPts);
/**
 * Evaluate a Catmull-Rom path at interval/fraction parameters.
 * @param[in] origPts Borrowed finite source points; at least one is required.
 * @param[in] nOrigPts Number of source points.
 * @param[in,out] intpPts Non-NULL caller-owned parameters, replaced by coordinates.
 * X is an integral interval and Y is in [0,1]; the final knot uses interval
 * nOrigPts-1 with fraction zero.
 * @param[in] nIntpPts Nonnegative number of evaluations.
 * @return TRUE on success, FALSE on validation, allocation or arithmetic
 * failure. Arithmetic failure may leave partially replaced output.
 *
 * Validates parameters before writing, copies and extends source endpoints
 * privately, then evaluates cubic coefficients. Does not use chord-length
 * parameterization. Caller retains both arrays; no Tcl callbacks or diagnostic.
 */
int Rbc_CatromParametricSpline(const Point2D *origPts, Tcl_Size nOrigPts, Point2D *intpPts, Tcl_Size nIntpPts);
/**
 * @brief Find the clamped nearest point on a finite segment and return distance.
 * @param[in] samplePtr Borrowed finite query point in the same coordinate system as the segment.
 * @param[in] p Borrowed finite first endpoint.
 * @param[in] q Borrowed finite second endpoint.
 * @param[out] closestPtr Required caller-owned nearest-point output; rejected inputs/intermediates leave it
 *     unchanged.
 *
 * Inputs and closestPtr are borrowed; closestPtr must be non-NULL. Segments of
 * length at most DBL_EPSILON use p. Invalid inputs or rejected intermediate
 * arithmetic return DBL_MAX without writing the result. The final hypot may
 * still overflow for extreme finite coordinates; this is not a Tcl error API.
 */
double Rbc_GetClosestPointOnSegment(const Point2D *samplePtr, const Point2D *p, const Point2D *q, Point2D *closestPtr);
/**
 * @brief Clip a segment to inclusive extents using Liang-Barsky parameters.
 * @param[in] extsPtr Borrowed finite ordered clipping bounds; zero width or height is allowed.
 * @param[in] p Borrowed finite first endpoint.
 * @param[in] q Borrowed finite second endpoint.
 * @param[out] clippedPtr Optional clipped segment; NULL requests a visibility test. Rejection leaves separate
 *     output untouched.
 *
 * Requires finite, ordered extents and finite endpoints. Returns false for
 * invalid/disjoint input, leaving output untouched. On success, writes the
 * clipped segment if clippedPtr is non-NULL; NULL requests visibility only.
 * Input endpoints are not modified when output storage is separate.
 */
int Rbc_LineRectClip(const Extents2D *extsPtr, const Point2D *p, const Point2D *q, Segment2D *clippedPtr);
/**
 * @brief Clip a polygon into a separate caller-owned output array.
 * @param[in] extsPtr Borrowed finite ordered bounds; right and bottom are expanded by one coordinate unit
 *     internally.
 * @param[in] inputPts Borrowed source vertices, disjoint from output; no repeated closing vertex is required.
 * @param[in] nInputPts Number of accessible source vertices, positive for nonempty output.
 * @param[out] outputPts Caller-owned output buffer; failure may leave a prefix that must be discarded.
 * @param[in] outputCapacity Number of available Point2D slots including the repeated closing vertex.
 *
 * Input contains exactly nInputPts vertices; the closing edge is implicit.
 * Uses left/top and historical right+1/bottom+1 clipping boundaries. Output is
 * explicitly closed by repeating its first vertex. outputCapacity counts all
 * available Point2D entries, including closure. Returns the output count, or
 * zero for empty/invalid input or insufficient capacity. Capacity failure can
 * leave partial output; discard it. Input and output must not overlap.
 */
Tcl_Size Rbc_PolyRectClip(const Extents2D *extsPtr, const Point2D *inputPts, Tcl_Size nInputPts, Point2D *outputPts,
                          Tcl_Size outputCapacity);
/**
 * @brief Test polygon membership by an even-odd horizontal ray crossing.
 * @param[in] samplePtr Borrowed finite query point in the polygon coordinate system.
 * @param[in] points Borrowed finite polygon vertices; the closing edge is implicit.
 * @param[in] nPoints Number of accessible vertices, at least three for membership/enclosure tests.
 *
 * Requires at least three finite vertices and a finite sample; invalid input
 * returns false. The closing edge is implicit. Half-open crossing comparisons
 * do not promise that every point on an edge is inside. Does not modify input.
 */
int Rbc_PointInPolygon(const Point2D *samplePtr, const Point2D *points, Tcl_Size nPoints);
/**
 * @brief Test polygon enclosure by, or overlap with, a rectangle.
 * @param[in] extsPtr Borrowed finite ordered query rectangle.
 * @param[in] points Borrowed finite polygon vertices; the closing edge is implicit.
 * @param[in] nPoints Number of accessible vertices, at least three for membership/enclosure tests.
 * @param[in] enclosed Nonzero tests whether the rectangle encloses the polygon; zero tests overlap or
 *     containment.
 *
 * With enclosed nonzero, every polygon vertex must be inside inclusive extents;
 * this means the rectangle encloses the polygon, not the reverse. Otherwise
 * test edge intersection and then rectangle-inside-polygon containment.
 * Invalid extents or fewer than three finite vertices return false.
 */
int Rbc_RegionInPolygon(const Extents2D *extsPtr, const Point2D *points, Tcl_Size nPoints, int enclosed);
/**
 * @brief Return true if any segment is strictly closer than halo to the sample.
 * @param[in] samplePtr Borrowed finite query point.
 * @param[in] segments Borrowed array of accessible segment records.
 * @param[in] nSegments Number of segment records; zero or negative counts return false.
 * @param[in] halo Finite positive strict distance threshold in the input coordinate units; zero never matches.
 *
 * Requires a finite positive halo; zero or invalid halo, no segments or an
 * invalid sample returns false. Uses clamped segment distance, including
 * degenerate segments. Does not acquire or modify the borrowed arrays.
 */
int Rbc_PointInSegments(const Point2D *samplePtr, const Segment2D *segments, Tcl_Size nSegments, double halo);
/**
 * @brief Return retained original indices for an inclusive input interval.
 * @param[in] origPts Borrowed finite point array accessible through high; no length argument validates that
 *     capacity.
 * @param[in] low Inclusive first original index, nonnegative.
 * @param[in] high Inclusive final original index, at least low.
 * @param[in] tolerance Finite nonnegative distance tolerance in the same units as the points.
 * @param[out] indices Caller-owned room for high-low+1 original indices; discard partial output when the
 *     returned count is zero.
 *
 * Caller supplies valid finite points through high and space for high-low+1
 * indices; no array lengths are supplied or checked here. Use a finite,
 * nonnegative tolerance in the same coordinate units as input. The algorithm
 * measures distance to the infinite endpoint line, or to the endpoint when
 * endpoints coincide. Retains interval endpoints in input order, splitting
 * only when squared distance exceeds squared tolerance. Returns zero on
 * rejected bounds/allocation failure; discard any partial output. Input is
 * borrowed and unchanged; temporary stack storage is released before return.
 */
Tcl_Size Rbc_SimplifyLine(const Point2D *origPts, Tcl_Size low, Tcl_Size high, double tolerance, Tcl_Size indices[]);
/**
 * @brief Parse a color and optional nonnegative pixel offset into an owned shadow.
 *
 * NULL or empty input yields no color and offset zero. A color alone defaults
 * to offset one. Acquire a Tk color reference and release it on offset failure.
 * Only success assigns shadowPtr. The caller must release any previous shadow
 * separately and ultimately Tk_FreeColor the returned non-NULL color.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed live Tk window supplying screen-unit and resource context; no ownership is
 *   acquired.
 * @param[in] objPtr Borrowed color/offset list, or NULL or empty to disable the shadow.
 * @param[out] shadowPtr Required result receiving an owned Tk color reference on success; previous contents are
 *   not released.
 */
int Rbc_GetShadowFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, Shadow *shadowPtr);
/**
 * @brief Parse a nonempty fill-name prefix into an integer mode.
 *
 * Accept none (also legacy no), x, y or both using case-sensitive prefix
 * matching. Commit fillPtr only on success. This is directional fill selection,
 * not the graph area-fill brush or uncertainty-band parser.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed non-NULL Tcl input object, kept valid by the caller; not retained here.
 * @param[out] fillPtr Required FILL_* mode output; unchanged on failure.
 */
int Rbc_GetFillFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *fillPtr);
/**
 * @brief Parse an exact normal, active or disabled state name.
 *
 * Borrow objPtr and commit statePtr only on success. Abbreviations and case
 * variants are not accepted. Return TCL_ERROR with a diagnostic otherwise.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed non-NULL Tcl input object, kept valid by the caller; not retained here.
 * @param[out] statePtr Required STATE_NORMAL, STATE_ACTIVE or STATE_DISABLED output; unchanged on failure.
 */
int Rbc_GetStateFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *statePtr);
/**
 * @brief Copy a Tcl list into one caller-owned pointer-and-string allocation.
 *
 * NULL input and an empty list yield NULL. Set listPtrPtr to NULL before parsing,
 * including on failure; do not pass the sole owning field of an existing list
 * unless its storage is managed separately. Release a nonempty result with one
 * ckfree, never by freeing individual entries. Input strings are NUL-terminated.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed Tcl list object, or NULL for an empty list; string bytes are copied.
 * @param[out] listPtrPtr Required output cleared before parsing; receives one ckfree-owned pointer/string block
 *   for a nonempty list, not separate entry allocations.
 */
int Rbc_GetStringListFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, char ***listPtrPtr);
/**
 * @brief Set hexadecimal digit entries in a caller-owned character table.
 *
 * Require storage addressable by the character codes for 0-9, a-f and A-F
 * (typically 256 entries). Set only those slots to 0 through 15. All other
 * slots keep their previous contents; callers initialize invalid-digit sentinels.
 *
 * @param[in,out] table Caller-owned char array addressable at every hexadecimal digit character code; only
 *   those slots are overwritten.
 */
void Rbc_InitHexTable(char *table);
/**
 * @brief Allocate an unshared GC compatible with a Tk window depth.
 *
 * Borrow tkwin and XGCValues. For an unrealized window use the root drawable
 * at matching depth, otherwise a temporary 1x1 pixmap. Release that temporary
 * pixmap before returning. The caller owns the GC and must use
 * Rbc_FreePrivateGC(), not Tk_FreeGC(). Does not force window realization.
 *
 * @param[in] tkwin Borrowed Tk window supplying screen and depth; need not be realized.
 * @param[in] gcMask XCreateGC mask selecting the fields to read from valuePtr.
 * @param[in] valuePtr Borrowed XGCValues with all mask-selected fields initialized.
 */
GC Rbc_GetPrivateGC(Tk_Window tkwin, unsigned long gcMask, XGCValues *valuePtr);
/**
 * @brief Allocate an unshared GC using a borrowed compatible drawable.
 *
 * Pass gcMask and valuePtr to XCreateGC. The caller owns the returned GC;
 * release it with Rbc_FreePrivateGC(). No Tk GC-cache reference is created.
 *
 * @param[in] display Borrowed display owning drawable.
 * @param[in] drawable Borrowed valid drawable supplying GC depth compatibility.
 * @param[in] gcMask XCreateGC mask selecting fields in valuePtr.
 * @param[in] valuePtr Borrowed XGCValues with all selected fields initialized.
 */
GC Rbc_GetPrivateGCFromDrawable(Display *display, Drawable drawable, unsigned long gcMask, XGCValues *valuePtr);
/**
 * @brief Release a non-NULL private GC with XFreeGC.
 *
 * Borrow display. Do not pass a Tk_GetGC cache reference or reuse the GC
 * after release. No resource-ownership tracking or NULL check is performed.
 *
 * @param[in] display Borrowed display that owns gc.
 * @param[in] gc Owned non-NULL private GC consumed by XFreeGC; never a Tk GC-cache reference.
 */
void Rbc_FreePrivateGC(Display *display, GC gc);
/**
 * @brief Parse a Tk screen distance and validate its converted integer value.
 *
 * Borrow window/string. Apply PIXELS_ANY, PIXELS_NONNEGATIVE or PIXELS_POSITIVE
 * after Tk_GetPixels succeeds; these flags do not override Tk parser policy.
 * Commit valuePtr only on success. Return Tcl status with an error diagnostic.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed live Tk window supplying screen-unit and resource context; no ownership is
 *   acquired.
 * @param[in] string Borrowed NUL-terminated Tk screen-distance text.
 * @param[in] check PIXELS_ANY, PIXELS_NONNEGATIVE or PIXELS_POSITIVE, applied after successful Tk conversion.
 * @param[out] valuePtr Required converted pixel output; unchanged on conversion or sign-check failure.
 */
int Rbc_GetPixels(Tcl_Interp *, Tk_Window, const char *, int, int *);
/**
 * @brief Parse an object screen distance with the same checks as Rbc_GetPixels.
 *
 * Borrow object/window and use Tk_GetPixelsFromObj before RBC range checks.
 * Only success assigns valuePtr; the object may acquire Tk conversion state.
 * No expression evaluation or graph-axis conversion is performed here.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed live Tk window supplying screen-unit and resource context; no ownership is
 *   acquired.
 * @param[in] pixelsObj Borrowed non-NULL Tcl input object, kept valid by the caller; not retained here.
 * @param[in] check PIXELS_ANY, PIXELS_NONNEGATIVE or PIXELS_POSITIVE, applied after successful Tk conversion.
 * @param[out] valuePtr Required converted pixel output; unchanged on conversion or sign-check failure.
 */
int Rbc_GetPixelsFromObj(Tcl_Interp *, Tk_Window, Tcl_Obj *, int, int *);
/**
 * @brief Parse an optional \@x,y position using Tk screen-distance conversion.
 *
 * NULL/empty input succeeds with both outputs set to -SHRT_MAX. Otherwise copy
 * the X substring without modifying the input and parse both components. Commit
 * x/y only after both succeed. These are screen pixels, not graph coordinates;
 * conversion errors append position context to the interpreter diagnostic.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed live Tk window supplying screen-unit and resource context; no ownership is
 *   acquired.
 * @param[in] string Borrowed NUL-terminated `@x,y` screen-position text; NULL or empty selects the
 *   unspecified-position sentinel.
 * @param[out] x Required X pixel output; failure leaves it unchanged, empty input assigns -SHRT_MAX.
 * @param[out] y Required distinct Y pixel output; committed with X only after both conversions succeed.
 */
int Rbc_GetXY(Tcl_Interp *interp, Tk_Window tkwin, const char *string, int *x, int *y);
/**
 * @brief Append NUL-terminated strings as Tcl list elements.
 *
 * Borrow the initialized DString and arguments, ending varargs with a NULL
 * string pointer. Tcl_DStringAppendElement supplies list quoting. Existing
 * contents remain; the caller owns the DString and eventually frees it.
 *
 * @param[in,out] dsPtr Required initialized caller-owned DString; quoted list elements are appended without
 *   clearing it.
 * @param[in] ... Borrowed NUL-terminated element strings, terminated by a NULL string pointer; no lengths are
 *   supplied.
 */
void Rbc_DStringAppendElements(Tcl_DString *dsPtr, ...);
/**
 * @brief Append raw string fragments to a duplicate of the interpreter result.
 *
 * Borrow interp and NUL-terminated arguments ending with a NULL string pointer.
 * Duplicate the current object before appending, then install and release the
 * local reference. Shared original objects remain unchanged. Fragments are not
 * list-quoted, and this helper does not set structured Tcl error metadata.
 *
 * @param[in,out] interp Borrowed interpreter whose result is copied and replaced; see @ref rbc_interp_argument.
 * @param[in] ... Borrowed NUL-terminated raw fragments, terminated by a NULL string pointer; fragments are not
 *   list-quoted.
 */
void Rbc_AppendResultStrings(Tcl_Interp *interp, ...);
void Rbc_GetBoundingBox(int width, int height, double theta, double *widthPtr, double *heightPtr, Point2D *points);
void Rbc_InitEpsCanvasItem(Tcl_Interp *interp);
void Rbc_TranslateAnchor(int x, int y, int width, int height, Tk_Anchor anchor, int *transXPtr, int *transYPtr);
Point2D Rbc_TranslatePoint(Point2D *pointPtr, int width, int height, Tk_Anchor anchor);
/**
 * @brief Resolve a relative child path beneath a borrowed parent window.
 *
 * Returns a borrowed window or NULL when lookup fails. The root parent uses a
 * single leading dot. No window is created or retained, and lookup has no result
 * interpreter. A dotted relative name can resolve a deeper descendant.
 * @param[in] parent Borrowed parent window, or NULL to return NULL immediately.
 * @param[in] name Borrowed NUL-terminated relative child path, without a leading dot; not retained.
 */
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
/**
 * @brief Saturate an already-computed wide integer to INT_MIN..INT_MAX.
 *
 * Pure conversion with no interpreter state. Does not round floating values,
 * check finiteness or impose widget-specific minimum sizes.
 *
 * @param[in] value Already-computed wide integer to saturate; this function does not protect earlier arithmetic
 *   from overflow.
 */
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
/**
 * @brief Estimate how many elemSize-byte records fit in an X request.
 *
 * Use the extended limit when supported and nonzero, otherwise the ordinary
 * limit. Reserve four protocol words, convert to bytes and divide by elemSize.
 * Return zero for zero element size, unusable limits or multiplication overflow;
 * clamp the count to INT_MAX. Callers still own request batching and headers.
 *
 * @param[in] display Borrowed live display queried for the protocol request limit.
 * @param[in] elemSize Bytes per element; zero returns zero rather than dividing.
 */
int Rbc_MaxRequestSize(Display *display, unsigned int elemSize);
Window Rbc_GetRealWindowId(Tk_Window tkwin);
ClientData Rbc_GetWindowInstanceData(Tk_Window tkwin);
void Rbc_SetWindowInstanceData(Tk_Window tkwin, ClientData instanceData);
void Rbc_DeleteWindowInstanceData(Tk_Window tkwin);
/**
 * @brief Clamp an integer offset according to the selected legacy scroll mode.
 * Caller supplies meaningful nonnegative world/window sizes and positive unit
 * size. Canvas mode permits negative offsets when the world is smaller than
 * the window; listbox permits trailing blank space; hierbox clamps to zero
 * when the world fits. Unknown modes leave the offset unchanged. Arithmetic
 * uses wide intermediates and saturates the returned int; no state is mutated.
 *
 * @param[in] offset Requested pixel offset to clamp; negative values are meaningful for some canvas layouts.
 * @param[in] worldSize Nonnegative total content extent in pixels, supplied by the caller.
 * @param[in] windowSize Nonnegative visible extent in pixels, supplied by the caller.
 * @param[in] scrollUnits Positive pixel distance per scroll unit.
 * @param[in] scrollMode RBC_SCROLL_MODE_CANVAS, RBC_SCROLL_MODE_LISTBOX or RBC_SCROLL_MODE_HIERBOX; unknown
 *   modes leave the computed offset unclamped.
 */
int Rbc_AdjustViewport(int offset, int worldSize, int windowSize, int scrollUnits, int scrollMode);
typedef enum {RBC_SCROLL_MOVETO, RBC_SCROLL_UNITS, RBC_SCROLL_PAGES} Rbc_ScrollKind;
/** @brief Parsed scrollbar request, independent of axis application. */
typedef struct {
    Rbc_ScrollKind kind; /**< Request kind: absolute fraction, unit increment or page increment. */
    double value; /**< Fraction for moveto, or signed count for scrolling. */
} Rbc_ScrollRequest;
/**
 * @brief Parse a finite double-valued scroll request without moving a viewport.
 * Requires a valid argument array, 0 <= first <= objc, and a non-NULL output.
 * Queries are handled by callers. Legacy mode allows nonempty abbreviations
 * and one numeric unit count; strict mode requires exact Tk-style words.
 * Returns TCL_OK and assigns the complete request, or TCL_ERROR with a Tcl
 * diagnostic and the output unchanged. Fractions and counts are not clamped.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Total number of accessible objv entries.
 * @param[in] objv Borrowed full argument vector; requests start at first, earlier entries supply usage context.
 *   See @ref rbc_command_arguments.
 * @param[in] first Request start index, with 0 <= first <= objc; an empty suffix reports usage rather than
 *   querying.
 * @param[in] legacy Nonzero accepts nonempty abbreviations and a single numeric unit count; zero requires exact
 *   command words.
 * @param[out] request Required kind/value record, assigned only on success; finite values are not clamped to a
 *   viewport.
 */
int Rbc_ParseScrollRequest(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Tcl_Size first,
                           int legacy, Rbc_ScrollRequest *request);

/**
 * @brief Parse legacy integer scrolling and update a caller-owned pixel offset.
 * Unit/page counts are integers; moveto accepts a finite double fraction.
 * A page is 90 percent of windowSize. Delta truncation precedes addition, with
 * saturation before AdjustViewport applies mode bounds. On TCL_ERROR the offset
 * is unchanged; some malformed-arity paths do not set a diagnostic. This is
 * separate from the double-valued axis/track request parser.
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Count of request objects only; standard scroll/moveto forms enforce their arity.
 * @param[in] objv Borrowed request slice starting with scroll, moveto or a numeric count; NULL is rejected.
 * @param[in,out] offsetPtr Required current pixel offset; replaced with the saturated, mode-adjusted result
 *   only on success.
 * @param[in] worldSize Nonnegative total content extent in pixels, supplied by the caller.
 * @param[in] windowSize Nonnegative visible extent in pixels, supplied by the caller.
 * @param[in] scrollUnits Positive pixel distance per scroll unit.
 * @param[in] scrollMode RBC_SCROLL_MODE_CANVAS, RBC_SCROLL_MODE_LISTBOX or RBC_SCROLL_MODE_HIERBOX; unknown
 *   modes leave the computed offset unclamped.
 */
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
 *
 * @param[in,out] interp Borrowed interpreter for conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed non-NULL Tcl input object, kept valid by the caller; not retained here.
 * @param[in] specs Persistent Tk_OptionSpec array terminated by TK_OPTION_END, containing concrete options;
 *   must outlive objects caching indexed-name lookup.
 * @param[in] message Borrowed NUL-terminated label for unknown/ambiguous-option diagnostics.
 * @param[out] indexPtr Required specification index output, meaningful only on TCL_OK.
 */
int Rbc_GetComponentOptionIndex(Tcl_Interp *interp, Tcl_Obj *objPtr, const Tk_OptionSpec *specs,
                                const char *message, int *indexPtr);
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
 *
 * @param[in] spec Borrowed concrete specification with non-NULL optionName, dbName, dbClass and defValue
 *   strings; their text is copied.
 * @param[in] valueObj Required current value; the returned list retains it without consuming references already
 *   held by the caller.
 */
Tcl_Obj *Rbc_GetComponentOptionInfo(const Tk_OptionSpec *spec, Tcl_Obj *valueObj);
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

/**
 * @brief Copy a non-NULL NUL-terminated string into Tcl-owned storage.
 *
 * Borrow the input and copy through its first NUL, including the terminator.
 * The caller releases the result with ckfree, not libc free. Uses ckalloc
 * failure policy; this is not a binary-data or length-delimited copy.
 *
 * @param[in] ptr Borrowed non-NULL NUL-terminated source; the result owns a separate copy through its first
 *   NUL.
 */
char *RbcStrdup(const char *ptr);
/**
 * @brief Allocate a zero-initialized Tcl-owned block.
 *
 * Check nElem * size for size_t overflow and panic on overflow. A zero logical
 * size allocates one zero byte, preserving a non-NULL ckalloc-style result.
 * The caller releases the allocation with ckfree. Allocation failure is fatal,
 * not a recoverable NULL result.
 *
 * @param[in] nElem Logical element count; multiplication by size is checked for size_t overflow.
 * @param[in] size Bytes per element; either zero argument still yields a one-byte zeroed allocation.
 */
void *RbcCalloc(size_t nElem, size_t size);

#ifdef WIN32
#include "rbcWin.h"
#endif

#ifndef WIN32
#define PurifyPrintf printf
#endif /* WIN32 */

#endif /* _RBCINT */
