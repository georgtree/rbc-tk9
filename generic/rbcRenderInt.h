/**
 * @file rbcRenderInt.h
 * @brief Private backend operation tables and common context prefix.
 *
 * A NULL operation denotes an unsupported capability, not a fallback request.
 * Component-facing contracts belong to rbcRender.h. See license.terms.
 */
#ifndef RBC_RENDER_INT_H
#define RBC_RENDER_INT_H
#include "rbcRender.h"

/* Backend contexts embed this common prefix as their first member. Each
 * constructor owns its complete context; End releases it after unwinding clips.
 * NULL operations denote unsupported capabilities, not implicit fallbacks. */
/** @brief Presentation callbacks selected by each constructor. */
typedef struct {
    void (*text)(Rbc_RenderContext *, char *string, TextStyle *style, double x, double y);
    void (*photo)(Rbc_RenderContext *, Tk_PhotoHandle photo, double x, double y);
    void (*window)(Rbc_RenderContext *, Tk_Window tkwin, double x, double y);
    void (*backgroundPolygon)(Rbc_RenderContext *, const XColor *color, const Point2D *points, Tcl_Size count);
    void (*border)(Rbc_RenderContext *, Tk_3DBorder border, double x, double y, int width, int height, int borderWidth,
                   int relief, int fill);
    void (*clearRectangle)(Rbc_RenderContext *, double x, double y, int width, int height);
    void (*backgroundRectangles)(Rbc_RenderContext *, const XColor *color, const Rbc_RenderRectangle *rectangles,
                                 Tcl_Size count);
    void (*plotBegin)(Rbc_RenderContext *, Tk_Font font, double x, double y, int width, int height,
                      const XColor *background);
    void (*plotEnd)(Rbc_RenderContext *);
    void (*bitmapMask)(Rbc_RenderContext *, Display *display, Pixmap bitmap, double x, double y, int width, int height,
                       const XColor *color, int background);
    void (*image)(Rbc_RenderContext *, Tk_Image image, double x, double y);
    /* Screen-only text capabilities preserve cached layouts and measured size. */
    void (*textLayout)(Rbc_RenderContext *, TextLayout *, TextStyle *, int, int);
    void (*textSize)(Rbc_RenderContext *, const char *, TextStyle *, int, int, Dim2D *);
    /* Screen-only mapped raster presentation. */
    void (*mappedBitmap)(Rbc_RenderContext *, const Rbc_RenderRectangle *, const Rbc_RenderBitmapStyle *);
    void (*pixmap)(Rbc_RenderContext *, Pixmap, GC, const Rbc_RenderRectangle *);
    void (*screenRectangle)(Rbc_RenderContext *, GC, Rbc_Tile, int, int, int, int);
    void (*focus)(Rbc_RenderContext *, GC, int);
    void (*copyArea)(Rbc_RenderContext *, Drawable, GC, int, int, unsigned int, unsigned int, int, int);
    void (*textBackground)(Rbc_RenderContext *, GC, const Point2D *, const Point2D *);
    void (*arrowHead)(Rbc_RenderContext *, GC, const Point2D *);
    void (*legendLine)(Rbc_RenderContext *, GC, int, int, int);
    void (*legendBar)(Rbc_RenderContext *, GC, int, int, int);
    void (*run)(Rbc_RenderContext *, void *, Tcl_Size, Rbc_RenderRunProc *);
    void (*bitmapRun)(Rbc_RenderContext *, Pixmap, GC, int, int, const Rbc_RenderPosition *, Tcl_Size);
} Rbc_RenderOutputOps;

/**
 * @brief Geometry dispatch and lifetime operations for one context family.
 *
 * Populate only supported operations. Dispatch generally calls them directly;
 * a caller using the wrong context family can dereference a NULL callback.
 * Clip callbacks implement backend state; the common layer owns clipDepth.
 */
typedef struct {
    void (*polyline)(Rbc_RenderContext *, const Point2D *, Tcl_Size);
    void (*segments)(Rbc_RenderContext *, const Segment2D *, Tcl_Size);
    void (*lineStyle)(Rbc_RenderContext *, int, int);
    void (*dashBackground)(Rbc_RenderContext *, const XColor *);
    void (*fillPolygon)(Rbc_RenderContext *, const Point2D *, Tcl_Size);
    void (*fillRectangles)(Rbc_RenderContext *, const Rbc_RenderRectangle *, Tcl_Size);
    void (*symbolPoints)(Rbc_RenderContext *, const Point2D *, Tcl_Size);
    const Rbc_RenderOutputOps *output;
    void (*end)(Rbc_RenderContext *);
    void (*arc)(Rbc_RenderContext *, const Rbc_RenderArcGeometry *);
    void (*fillArc)(Rbc_RenderContext *, const Rbc_RenderArcGeometry *);
    void (*clipBegin)(Rbc_RenderContext *, int, int, int, int);
    void (*clipEnd)(Rbc_RenderContext *);
} Rbc_RenderOps;

/**
 * @brief Prefix embedded first in every allocated backend context.
 *
 * Constructors initialize the entire allocation. Rbc_RenderEnd() unwinds
 * clips and calls ops->end, which releases backend state and the allocation.
 * Borrowed graph/document resources must remain alive until that completes.
 */
struct Rbc_RenderContext {
    const Rbc_RenderOps *ops; /**< Borrowed immutable dispatch table. */
    Graph *graphPtr; /**< Borrowed graph, or NULL for contexts that do not need it. */
    unsigned int clipDepth; /**< Outstanding rectangular clip scopes. */
};

#endif
