/**
 * @file rbcRender.h
 * @brief Private graph rendering interface used by mapped components.
 *
 * Geometry is in mapped widget/display coordinates, not axis data coordinates.
 * Contexts and their borrowed resources must stay on the owning Tk thread.
 * A constructor defines the supported operations; unsupported callbacks are
 * not automatic fallbacks. Check nullable constructor results before drawing.
 * End each allocated context exactly once with Rbc_RenderEnd().
 * Document tokens, drawables, GCs and component-owned resources remain borrowed.
 *
 * This header is not installed and does not define the public vector ABI.
 * See license.terms for redistribution terms.
 */
#ifndef RBC_RENDER_H
#define RBC_RENDER_H

#include "rbcGraph.h"
#include "rbcPs.h"

typedef struct Rbc_RenderContext Rbc_RenderContext;
typedef struct Rbc_RenderTarget Rbc_RenderTarget;

/**
 * Mapping-time polygon capacity for the selected output. TCL_SIZE_MAX means
 * no request-size bound; zero means no polygon can be submitted. Components
 * reserve space for closure/clipping and preserve shared samples when splitting.
 * This query needs no drawing context and allocates no resources.
 *
 * @param[in] graphPtr Borrowed owning graph with current mapped geometry, kept alive through the operation or
 *     context lifetime.
 */
Tcl_Size Rbc_RenderPolygonPointLimit(Graph *graphPtr);

/** Native-only screen details borrow the exact GC, preserving XOR and dash
 * semantics. Arrow points include the repeated closing vertex. Text backgrounds
 * consume four outline points and add the anchor before integer conversion. */
#define RBC_RENDER_ARROW_POINTS 6
typedef int Rbc_RenderPosition[2]; /**< Full-width integer x/y, never XPoint shorts. */
/**
 * @brief Synchronous native batch callback; no argument is retained by the renderer.
 * @param[in] data Borrowed batch-specific data supplied to Rbc_RenderScreenRun.
 * @param[in] drawable Borrowed platform target, valid only until the callback returns.
 * @param[in] count Number of items in data to present.
 *
 * Must preserve graph and drawing-resource lifetime and must not retain the target.
 * @see Rbc_RenderScreenRun
 */
typedef void Rbc_RenderRunProc(void *data, Drawable drawable, Tcl_Size count);
/**
 * @brief Fill a four-corner native text background.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] gc Borrowed configured GC; raster operation, dash and clip state remain caller-selected.
 * @param[in] outline Four borrowed outline vertices relative to anchor.
 * @param[in] anchor Borrowed mapped text anchor added before conversion to signed-short coordinates.
 *
 * Caller supplies representable mapped geometry and the required clip; this adapter does not impose a plot clip.
 */
void Rbc_RenderScreenTextBackground(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *outline,
                                     const Point2D *anchor);
/**
 * @brief Scan-convert a native arrowhead without rounding its vertices first.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] gc Borrowed configured GC; raster operation, dash and clip state remain caller-selected.
 * @param[in] points Borrowed finite array of RBC_RENDER_ARROW_POINTS vertices, including the repeated closing vertex.
 *
 * Spans are bounded by graph dimensions and signed-short limits. The enclosing screen clip supplies track clipping.
 */
void Rbc_RenderScreenArrowHead(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *points);
/**
 * @brief Draw two independent native legend sample lines.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] gc Borrowed configured GC; raster operation, dash and clip state remain caller-selected.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @param[in] size Half-length of each sample line in pixels; endpoints are x-size and x+size.
 *
 * The rows are y and y+1. Each native line restarts its dash pattern.
 */
void Rbc_RenderScreenLegendLine(Graph *graphPtr, Drawable drawable, GC gc, int x, int y, int size);
/**
 * @brief Fill a native legend square with a local stipple origin.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in,out] gc Borrowed fill GC; its tile/stipple origin is changed during drawing and reset to (0,0) afterward.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @param[in] size Nonnegative side length of the sample square in pixels.
 *
 * This does not restore a nonzero previous origin. The caller must use a GC compatible with that postcondition.
 */
void Rbc_RenderScreenLegendBar(Graph *graphPtr, Drawable drawable, GC gc, int x, int y, int size);

/**
 * @brief Present a synchronous run through one platform drawing target.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] data Borrowed callback-specific batch data; NULL suppresses the run.
 * @param[in] count Number of run items; nonpositive values suppress the callback.
 * @param[in] proc Required synchronous callback; must not retain the temporary target or destroy borrowed drawing resources.
 *
 * On Windows, attempts a shared DC wrapper and falls back to the original drawable if unavailable.
 * The callback runs before this function returns. This is not a Tcl evaluation boundary.
 */
void Rbc_RenderScreenRun(Graph *graphPtr, Drawable drawable, void *data, Tcl_Size count, Rbc_RenderRunProc *proc);
/**
 * @brief Present opaque, unmasked bitmap copies with platform fallback.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] bitmap Borrowed one-bit source bitmap with the supplied width and height.
 * @param[in,out] gc Borrowed GXcopy-compatible GC; fallback clears its clip mask and resets clip origin to (0,0).
 * @param[in] width Positive source bitmap width in pixels.
 * @param[in] height Positive source bitmap height in pixels.
 * @param[in] positions Borrowed integer destination origins, one per copy; NULL suppresses drawing.
 * @param[in] count Number of array entries, not bytes.
 *
 * Nonpositive counts draw nothing. A declined optimized batch is replayed in full, so masked or XOR copies
 * are not supported. Source bitmap, positions and GC remain caller-owned.
 */
void Rbc_RenderScreenBitmapRun(Graph *graphPtr, Drawable drawable, Pixmap bitmap, GC gc, int width, int height,
                                const Rbc_RenderPosition *positions, Tcl_Size count);

/**
 * @brief Try to acquire a shared Cairo marker-pass target; NULL leaves the original drawable usable.
 *
 * @param[in,out] graphPtr Owning graph; a successful acquisition installs a temporary render target until
 *     EndMarkerPass.
 * @param[in,out] drawablePtr Required pointer to the original destination; replaced by a temporary drawable
 *     only on success.
 */
Rbc_RenderTarget *Rbc_RenderBeginMarkerPass(Graph *graphPtr, Drawable *drawablePtr);
/**
 * @brief Present and release a marker-pass target; NULL is accepted.
 *
 * @param[in,out] target Owned target returned by BeginMarkerPass, consumed here; NULL is accepted. The
 *     replacement drawable is invalid afterward.
 */
void Rbc_RenderEndMarkerPass(Rbc_RenderTarget *target);

/**
 * Screen text uses Tk for native and Cairo graphs alike. Supports Text,
 * TextLayout, TextSize and End only. The window, drawable and cached layout
 * are borrowed; existing integer positions and clipping are retained. Reuse
 * a context for a label batch. TextSize returns the rotated layout extent.
 *
 * @param[in] tkwin Borrowed Tk window providing text resources, kept alive until End.
 * @param[in] drawable Borrowed destination drawable in mapped widget coordinates, kept valid through context
 *     release.
 */
Rbc_RenderContext *Rbc_RenderBeginScreenText(Tk_Window tkwin, Drawable drawable);
/**
 * @brief Draw a borrowed cached text layout through a screen text context.
 *
 * @param[in,out] ctx Live context created for screen text; its constructor must support this operation.
 * @param[in] layout Borrowed cached text layout.
 * @param[in,out] style Caller-provided text style with live font/GC resources; no ownership transfer.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 */
void Rbc_RenderTextLayout(Rbc_RenderContext *ctx, TextLayout *layout, TextStyle *style, int x, int y);
/**
 * @brief Draw screen text and return its rotated layout extent.
 *
 * @param[in,out] ctx Live context created for screen text; its constructor must support this operation.
 * @param[in] string Borrowed NUL-terminated text.
 * @param[in,out] style Caller-provided text style with live font/GC resources.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 * @param[out] size Required caller-owned output for the rotated text extent.
 */
void Rbc_RenderTextSize(Rbc_RenderContext *ctx, const char *string, TextStyle *style, int x, int y, Dim2D *size);

/** Keep bar geometry in full-width widget coordinates. */
typedef struct {
    int x; /**< Rectangle origin X in widget pixels. */
    int y; /**< Rectangle origin Y in widget pixels. */
    int width; /**< Rectangle width in pixels. */
    int height; /**< Rectangle height in pixels. */
} Rbc_RenderRectangle;

/** Maximum vertices in a mapped bitmap background outline. */
#define RBC_RENDER_BITMAP_OUTLINE_POINTS 12
/** @brief Mapped bitmap paint policy; all referenced resources remain component-owned.
 * A rotated opaque background is already clipped/mapped by the component;
 * mask then refers to the bitmap itself, as in the legacy drawing path.
 */
typedef struct {
    Pixmap bitmap; /**< Borrowed one-bit source bitmap; None means absent. */
    Pixmap mask; /**< Borrowed optional bitmap mask; None means absent. */
    GC gc; /**< Borrowed foreground GC. */
    GC fillGC; /**< Borrowed fill/background GC; absent means no fill. */
    const XColor *foreground; /**< Borrowed foreground color; NULL means absent. */
    const XColor *background; /**< Borrowed background color; NULL means absent. */
    const Point2D *backgroundPoints; /**< Borrowed mapped polygon for a rotated opaque background. */
    Tcl_Size backgroundCount; /**< Number of backgroundPoints entries; at most RBC_RENDER_BITMAP_OUTLINE_POINTS. */
} Rbc_RenderBitmapStyle;

/**
 * Screen raster contexts support TkImage, MappedBitmap, Pixmap and End only.
 * Cairo is attempted for photos/bitmaps; native fallback preserves Tk image
 * types and GC masks. The context borrows its drawable and all input resources.
 * Scaling/rotation remain in component mapping, outside presentation.
 *
 * @param[in] graphPtr Borrowed owning graph with current mapped geometry, kept alive through the operation or
 *     context lifetime.
 * @param[in] drawable Borrowed destination drawable in mapped widget coordinates, kept valid through context
 *     release.
 */
Rbc_RenderContext *Rbc_RenderBeginScreenRaster(Graph *graphPtr, Drawable drawable);
/**
 * @brief Present a mapped bitmap through a screen raster context, retaining native fallback.
 *
 * @param[in,out] ctx Live context created for screen raster presentation; its constructor must support this
 *     operation.
 * @param[in] r Borrowed mapped destination rectangle.
 * @param[in] style Borrowed bitmap/mask/style resources valid through this synchronous call.
 */
void Rbc_RenderMappedBitmap(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *r, const Rbc_RenderBitmapStyle *style);
/**
 * @brief Copy a mapped pixmap through a screen raster context using the borrowed GC.
 *
 * @param[in,out] ctx Live context created for screen raster presentation; its constructor must support this
 *     operation.
 * @param[in] pixmap Borrowed source pixmap.
 * @param[in] gc Borrowed copy GC, including its clip/mask state.
 * @param[in] r Borrowed mapped copy rectangle.
 */
void Rbc_RenderPixmap(Rbc_RenderContext *ctx, Pixmap pixmap, GC gc, const Rbc_RenderRectangle *r);

/**
 * @brief Draw native relief decoration, optionally filling its interior.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] border Borrowed Tk border supplying shading colors.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @param[in] width Rectangle width in pixels; caller supplies a nonnegative value.
 * @param[in] height Rectangle height in pixels; caller supplies a nonnegative value.
 * @param[in] borderWidth Border thickness in pixels.
 * @param[in] relief Tk relief constant.
 * @param[in] fill Nonzero selects filled decoration; zero draws only the border.
 *
 * Uses native Tk shading for both native and Cairo graphs. No allocated renderer context escapes this call.
 */
void Rbc_RenderScreenBorder(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width,
                             int height, int borderWidth, int relief, int fill);
/**
 * @brief Fill a native decoration rectangle with a tile or GC.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] gc Borrowed fill GC, required when tile is NULL; otherwise ignored.
 * @param[in] tile Optional borrowed tile taking precedence over gc; uses its existing origin.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @param[in] width Rectangle width in pixels; caller supplies a nonnegative value.
 * @param[in] height Rectangle height in pixels; caller supplies a nonnegative value.
 */
void Rbc_RenderScreenRectangle(Tk_Window tkwin, Drawable drawable, GC gc, Rbc_Tile tile, int x, int y,
                                int width, int height);

/**
 * @brief Draw independent native segments even on Cairo graphs.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] gc Borrowed configured GC; raster operation, dash and clip state remain caller-selected.
 * @param[in] segments Borrowed mapped endpoint pairs, valid for count Segment2D records.
 * @param[in] count Number of array entries, not bytes.
 *
 * Preserves the GC raster operation, including XOR. The native helper bounds protocol coordinates and batches
 * requests; this adapter does not create a geometry context.
 */
void Rbc_RenderScreenSegments(Tk_Window tkwin, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size count);

/**
 * @brief Copy completed pixels for backing-store restoration or presentation.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] source Borrowed source drawable.
 * @param[in] destination Borrowed destination drawable.
 * @param[in] gc Borrowed configured GC; raster operation, dash and clip state remain caller-selected.
 * @param[in] srcX Source rectangle X coordinate in pixels.
 * @param[in] srcY Source rectangle Y coordinate in pixels.
 * @param[in] width Rectangle width in pixels; caller supplies a nonnegative value.
 * @param[in] height Rectangle height in pixels; caller supplies a nonnegative value.
 * @param[in] dstX Destination rectangle X coordinate in pixels.
 * @param[in] dstY Destination rectangle Y coordinate in pixels.
 *
 * Caller supplies compatible drawables and GC and controls clipping and presentation order. No ownership changes.
 */
void Rbc_RenderScreenCopyArea(Tk_Window tkwin, Drawable source, Drawable destination, GC gc, int srcX, int srcY,
                               unsigned int width, unsigned int height, int dstX, int dstY);
/**
 * @brief Draw the native Tk focus highlight.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] gc Borrowed configured GC; raster operation, dash and clip state remain caller-selected.
 * @param[in] width Focus highlight thickness in pixels, passed to Tk_DrawFocusHighlight.
 */
void Rbc_RenderScreenFocus(Tk_Window tkwin, Drawable drawable, GC gc, int width);

/** Fill policy shared by screen and export. backgroundOnly preserves the
 * legacy PostScript fallback for image-tiled areas. */
typedef struct {
    const XColor *foreground; /**< Borrowed foreground color; NULL means absent. */
    const XColor *background; /**< Borrowed background color; NULL means absent. */
    Pixmap stipple; /**< Borrowed one-bit stipple; None selects a solid fill. */
    double opacity; /**< Solid polygon opacity; PostScript ignores it. */
    int backgroundOnly; /**< Nonzero requests the legacy background-only fill fallback. */
} Rbc_RenderFillStyle;

/**
 * Fill contexts support FillPolygon/FillRectangles/FillArc and End. Screen creation
 * may return NULL for native fallback; export does not require Cairo.
 *
 * @param[in] graphPtr Borrowed owning graph with current mapped geometry, kept alive through the operation or
 *     context lifetime.
 * @param[in] drawable Borrowed destination drawable in mapped widget coordinates, kept valid through context
 *     release.
 * @param[in] style Required fill policy; referenced colors and stipple remain borrowed until End. NULL return
 *     requires caller-selected fallback.
 */
Rbc_RenderContext *Rbc_RenderBeginFill(Graph *graphPtr, Drawable drawable, const Rbc_RenderFillStyle *style);
/**
 * @brief Create a document fill context borrowing export state and style resources.
 *
 * @param[in] graphPtr Borrowed owning graph with current mapped geometry, kept alive through the operation or
 *     context lifetime.
 * @param[in] exportPtr Borrowed live document state; caller owns document setup, completion and I/O.
 * @param[in] style Required fill policy copied by value; referenced Tk resources remain borrowed until End.
 */
Rbc_RenderContext *Rbc_RenderBeginExportFill(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                             const Rbc_RenderFillStyle *style);

/**
 * Supply a tile after BeginExportFill; PostScript keeps its background fallback.
 *
 * @param[in,out] ctx Live context created for export fill; its constructor must support this operation.
 * @param[in] tile Borrowed tile retained for this fill context; PostScript retains its background fallback.
 */
void Rbc_RenderSetFillTile(Rbc_RenderContext *ctx, Rbc_Tile tile);
/**
 * @brief Fill a mapped polygon; fewer than three points draw nothing.
 *
 * @param[in,out] ctx Live context created for polygon fill; its constructor must support this operation.
 * @param[in] points Borrowed mapped point array, accessible for count entries during this call.
 * @param[in] count Number of point records, not bytes.
 */
void Rbc_RenderFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
/**
 * @brief Fill mapped rectangles; nonpositive counts draw nothing.
 *
 * @param[in,out] ctx Live context created for rectangle fill; its constructor must support this operation.
 * @param[in] rectangles Borrowed mapped rectangle array, accessible for count entries during this call.
 * @param[in] count Number of rectangle records, not bytes.
 */
void Rbc_RenderFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count);

/** Ellipse in mapped display coordinates. Zero degrees points right; positive
 * sweeps turn counterclockwise. |extent| >= 360 is one closed ellipse, without
 * a radial seam. The renderer constructs the complete path under the context's
 * clip; callers must not clip/flatten it first. Geometry transforms do not
 * scale stroke width, dashes, or fill patterns. This is a private C interface. */
typedef enum { RBC_RENDER_ARC_OPEN, RBC_RENDER_ARC_CHORD, RBC_RENDER_ARC_PIESLICE } Rbc_RenderArcStyle;

/** @brief Value-only ellipse geometry; dispatch normalizes a copy and rejects non-finite or degenerate input. */
typedef struct {
    double cx; /**< Ellipse center X in mapped display coordinates. */
    double cy; /**< Ellipse center Y in mapped display coordinates. */
    double rx; /**< Positive horizontal radius in display units. */
    double ry; /**< Positive vertical radius in display units. */
    double start; /**< Start angle in degrees, measured counterclockwise from the rightward direction. */
    double extent; /**< Signed sweep in degrees; absolute values of at least 360 select a full ellipse. */
    Rbc_RenderArcStyle style; /**< Open curve, endpoint chord or center-connected sector. */
} Rbc_RenderArcGeometry;

/**
 * Use a stroke context for Arc and a fill context for FillArc. FillArc ignores
 * OPEN. Non-finite/degenerate geometry draws nothing. Screen contexts may be
 * unavailable (native renderer); callers retain their native fallback.
 *
 * @param[in,out] ctx Context supporting arc strokes, or NULL to draw nothing.
 * @param[in] arc Borrowed ellipse geometry; invalid fields draw nothing. Valid geometry is copied and
 *     normalized before dispatch.
 */
void Rbc_RenderArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc);
/**
 * @brief Fill a normalized mapped ellipse segment; open arcs draw nothing.
 *
 * @param[in,out] ctx Context supporting arc fills, or NULL to draw nothing.
 * @param[in] arc Borrowed ellipse geometry; invalid fields draw nothing. Valid geometry is copied and
 *     normalized before dispatch.
 */
void Rbc_RenderFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc);

/** Semantic symbol descriptions for export; colors are resolved by the caller. */
typedef enum {
    RBC_RENDER_SYMBOL_SQUARE,
    RBC_RENDER_SYMBOL_CIRCLE,
    RBC_RENDER_SYMBOL_DIAMOND,
    RBC_RENDER_SYMBOL_PLUS,
    RBC_RENDER_SYMBOL_CROSS,
    RBC_RENDER_SYMBOL_SPLUS,
    RBC_RENDER_SYMBOL_SCROSS,
    RBC_RENDER_SYMBOL_TRIANGLE,
    RBC_RENDER_SYMBOL_ARROW,
    RBC_RENDER_SYMBOL_BITMAP,
    RBC_RENDER_SYMBOL_NONE
} Rbc_RenderSymbolType;

/** @brief Export symbol policy copied into a context; referenced colors and pixmaps remain borrowed. */
typedef struct {
    Rbc_RenderSymbolType type; /**< Semantic symbol shape. */
    int size; /**< Symbol size in display pixels. */
    int outlineWidth; /**< Outline width in display pixels. */
    const XColor *outlineColor; /**< Borrowed outline color; NULL suppresses the outline. */
    const XColor *fillColor; /**< Borrowed fill color; NULL suppresses the fill. */
    Pixmap bitmap; /**< Borrowed one-bit source bitmap; None means absent. */
    Pixmap mask; /**< Borrowed optional bitmap mask; None means absent. */
} Rbc_RenderSymbolStyle;

/** Borrowed native symbol resources. Sampling state is returned through counter;
 * preserve it across batches belonging to the same element. */
typedef struct {
    Rbc_RenderSymbolType type; /**< Semantic symbol shape. */
    int outlineWidth; /**< Outline width in pixels. */
    GC outlineGC; /**< Borrowed outline GC; absent means no outline. */
    GC fillGC; /**< Borrowed fill/background GC; absent means no fill. */
    Pixmap bitmap; /**< Borrowed one-bit source bitmap; None means absent. */
    Pixmap mask; /**< Borrowed optional bitmap mask; None means absent. */
    Tcl_Size interval; /**< Positive values enable interval sampling; nonpositive values draw every supplied center. */
    Tcl_Size counter; /**< Mutable point counter; preserve across batches of the same element. */
} Rbc_RenderNativeSymbolStyle;

/** @brief Copied bar paint policy; the element retains ownership of all Tk resources. */
typedef struct {
    Tk_3DBorder border; /**< Borrowed Tk border supplying fill and relief colors. */
    XColor *fgColor; /**< Borrowed resolved foreground color. */
    GC gc; /**< Borrowed fill GC, including stipple and raster state. */
    Pixmap stipple; /**< Borrowed fill stipple; None means no stipple. */
    int borderWidth; /**< Border thickness in pixels. */
    int relief; /**< Tk relief selector. */
    double fillOpacity; /**< Fill alpha used by capable backends; native fallback retains its opaque behavior. */
} Rbc_RenderBarStyle;

/**
 * @brief Create a native SymbolPoints/End context.
 *
 * Returns an owned context; release exactly once with Rbc_RenderEnd.
 * This adapter does not take ownership of graph or drawing resources.
 * @param[in] graphPtr Borrowed live graph; keep it valid until Rbc_RenderEnd.
 * @param[in] drawable Borrowed destination drawable; keep it valid until Rbc_RenderEnd.
 * @param[in,out] style Borrowed symbol style and its resources, retained by pointer until End; drawing updates its sampling counter.
 * @param[in] size Symbol size in screen pixels, passed unchanged to native symbol drawing.
 */
Rbc_RenderContext *Rbc_RenderBeginNativeSymbols(Graph *graphPtr, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, int size);
/**
 * @brief Create a native FillRectangles/End context.
 *
 * Returns an owned context; release exactly once with Rbc_RenderEnd.
 * This adapter does not take ownership of graph or drawing resources.
 * The caller may discard the style record after construction. Native bevel and fill behavior are retained.
 * @param[in] graphPtr Borrowed live graph; keep it valid until Rbc_RenderEnd.
 * @param[in] drawable Borrowed destination drawable; keep it valid until Rbc_RenderEnd.
 * @param[in] style Required bar-style record copied by value; its GC, border and stipple resources remain borrowed until End.
 */
Rbc_RenderContext *Rbc_RenderBeginBars(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *style);
/**
 * @brief Create a native FillPolygon/End context.
 *
 * Returns an owned context; release exactly once with Rbc_RenderEnd.
 * This adapter does not take ownership of graph or drawing resources.
 * @param[in] graphPtr Borrowed live graph; keep it valid until Rbc_RenderEnd.
 * @param[in] drawable Borrowed destination drawable; keep it valid until Rbc_RenderEnd.
 * @param[in] gc Borrowed native GC, including its raster operation and clipping; keep it valid until Rbc_RenderEnd.
 * @param[in] tile Borrowed tile, or NULL for the GC/stipple path.
 * @param[in] stipple Non-None enables GC polygon filling when tile is NULL. None does not select solid filling; not acquired or freed here.
 */
Rbc_RenderContext *Rbc_RenderBeginNativeFill(Graph *graphPtr, Drawable drawable, GC gc, Rbc_Tile tile, Pixmap stipple);

/**
 * @brief Create a native Segments/End context.
 *
 * Returns an owned context; release exactly once with Rbc_RenderEnd.
 * This adapter does not take ownership of graph or drawing resources.
 * Preserves the supplied GC raster operation, including XOR, and its installed clip.
 * @param[in] graphPtr Borrowed live graph; keep it valid until Rbc_RenderEnd.
 * @param[in] drawable Borrowed destination drawable; keep it valid until Rbc_RenderEnd.
 * @param[in] gc Borrowed native GC, including its raster operation and clipping; keep it valid until Rbc_RenderEnd.
 */
Rbc_RenderContext *Rbc_RenderBeginNativeSegments(Graph *graphPtr, Drawable drawable, GC gc);
/**
 * @brief Create a native Arc/FillArc/End context.
 *
 * Returns an owned context; release exactly once with Rbc_RenderEnd.
 * This adapter does not take ownership of graph or drawing resources.
 * Keep both arrays alive until End and consistent with the analytic geometry passed
 * to Arc/FillArc. The native adapter draws these arrays rather than remapping the ellipse.
 * @param[in] graphPtr Borrowed live graph; keep it valid until Rbc_RenderEnd.
 * @param[in] drawable Borrowed destination drawable; keep it valid until Rbc_RenderEnd.
 * @param[in] gc Borrowed native GC, including its raster operation and clipping; keep it valid until Rbc_RenderEnd.
 * @param[in] fillPoints Borrowed mapped, clipped fill polygon; may be NULL when fillCount is zero.
 * @param[in] fillCount Nonnegative number of fill points.
 * @param[in] segments Borrowed mapped, clipped outline segments; may be NULL when count is zero.
 * @param[in] count Nonnegative number of outline segments.
 */
Rbc_RenderContext *Rbc_RenderBeginNativeArc(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *fillPoints,
                                            Tcl_Size fillCount, const Segment2D *segments, Tcl_Size count);
/**
 * @brief Create a native polygon-fill batch context.
 *
 * Returns an owned context; release exactly once with Rbc_RenderEnd.
 * This adapter does not take ownership of graph or drawing resources.
 * A missing platform batch or failed batch allocation falls back to ordinary native
 * polygon filling. The context itself is still returned and must be ended.
 * @param[in] graphPtr Borrowed live graph; keep it valid until Rbc_RenderEnd.
 * @param[in] drawable Borrowed destination drawable; keep it valid until Rbc_RenderEnd.
 * @param[in] gc Borrowed native GC, including its raster operation and clipping; keep it valid until Rbc_RenderEnd.
 * @param[in] maxPoints Maximum vertex count for each submitted polygon; use a positive representable capacity.
 */
Rbc_RenderContext *Rbc_RenderBeginNativePolygonBatch(Graph *graphPtr, Drawable drawable, GC gc, Tcl_Size maxPoints);

/**
 * Export symbol contexts support SymbolPoints and End. Screen shape batching
 * retains its existing interface. Contexts borrow bitmap/color resources.
 * Finish each symbol batch before starting another (the prolog is shared).
 *
 * @param[in] graphPtr Borrowed owning graph with current mapped geometry, kept alive through the operation or
 *     context lifetime.
 * @param[in] exportPtr Borrowed live document state; caller owns document setup, completion and I/O.
 * @param[in] style Required symbol description copied by value; bitmap and color resources remain borrowed
 *     until End.
 */
Rbc_RenderContext *Rbc_RenderBeginExportSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                               const Rbc_RenderSymbolStyle *style);
/**
 * @brief Create a document bar-symbol context; finish this batch before another symbol batch.
 *
 * @param[in] graphPtr Borrowed owning graph with current mapped geometry, kept alive through the operation or
 *     context lifetime.
 * @param[in] exportPtr Borrowed live document state; caller owns document setup, completion and I/O.
 * @param[in] style Required fill policy copied by value; referenced resources remain borrowed until End.
 * @param[in] size Legend swatch size in mapped display units.
 */
Rbc_RenderContext *Rbc_RenderBeginExportBarSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                                  const Rbc_RenderFillStyle *style, int size);
/**
 * @brief Draw symbols at mapped centers; native sampling may update the borrowed style counter.
 *
 * @param[in,out] ctx Live context created for native or export symbol batches; its constructor must support
 *     this operation.
 * @param[in] centers Borrowed mapped symbol centers; native sampling may update the context-associated style
 *     counter.
 * @param[in] count Number of centers; nonpositive counts draw nothing.
 */
void Rbc_RenderSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count);

/**
 * Export presentation primitives. Output contexts borrow the export state and Tk
 * resources. PlotBegin/PlotEnd must be paired; End releases only the context.
 * Document setup, trailers and I/O remain with each backend command.
 *
 * @param[in] exportPtr Borrowed live document state; caller owns document setup, completion and I/O.
 */
Rbc_RenderContext *Rbc_RenderBeginExportOutput(Rbc_ExportContext *exportPtr);
/**
 * @brief Draw text through an export-output or screen-text context.
 *
 * @param[in,out] ctx Live context created for screen text or export output; its constructor must support this
 *     operation.
 * @param[in] string Borrowed NUL-terminated text; ownership is not transferred.
 * @param[in,out] style Caller-provided text style and live resources; backend text preparation may update
 *     style state.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 */
void Rbc_RenderText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y);

/**
 * Non-photo image fallback; the legacy PostScript backend omits these images.
 *
 * @param[in,out] ctx Live context created for screen raster or export output; its constructor must support
 *     this operation.
 * @param[in] image Borrowed live Tk image.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 */
void Rbc_RenderTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y);
/**
 * @brief Write a borrowed Tk photo through an export-output context.
 *
 * @param[in,out] ctx Live context created for export output; its constructor must support this operation.
 * @param[in] photo Borrowed live Tk photo handle.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 */
void Rbc_RenderPhotoImage(Rbc_RenderContext *ctx, Tk_PhotoHandle photo, double x, double y);
/**
 * @brief Capture a borrowed Tk window through an export-output context, subject to backend policy.
 *
 * @param[in,out] ctx Live context created for export output; its constructor must support this operation.
 * @param[in] tkwin Borrowed live Tk window to capture, subject to backend export policy.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 */
void Rbc_RenderWindow(Rbc_RenderContext *ctx, Tk_Window tkwin, double x, double y);
/**
 * @brief Paint a mapped background polygon through an export-output context.
 *
 * @param[in,out] ctx Live context created for export output; its constructor must support this operation.
 * @param[in] color Borrowed resolved background color.
 * @param[in] points Borrowed mapped point array, accessible for count entries during this call.
 * @param[in] count Number of point records, not bytes.
 */
void Rbc_RenderBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points, Tcl_Size count);
/**
 * @brief Draw a border through an export-output context.
 *
 * @param[in,out] ctx Live context created for export output; its constructor must support this operation.
 * @param[in] border Borrowed live Tk border resource.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 * @param[in] width Rectangle width in display units.
 * @param[in] height Rectangle height in display units.
 * @param[in] borderWidth Border thickness in display units.
 * @param[in] relief Tk relief value.
 * @param[in] fill Nonzero requests filling as well as border drawing.
 */
void Rbc_RenderBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                      int borderWidth, int relief, int fill);
/**
 * @brief Clear a mapped rectangle through an export-output context.
 *
 * @param[in,out] ctx Live context created for export output; its constructor must support this operation.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 * @param[in] width Rectangle width in display units.
 * @param[in] height Rectangle height in display units.
 */
void Rbc_RenderClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height);
/**
 * @brief Paint mapped background rectangles through an export-output context.
 *
 * @param[in,out] ctx Live context created for export output; its constructor must support this operation.
 * @param[in] color Borrowed resolved background color.
 * @param[in] rectangles Borrowed mapped rectangle array, accessible for count entries during this call.
 * @param[in] count Number of rectangle records, not bytes.
 */
void Rbc_RenderBackgroundRectangles(Rbc_RenderContext *ctx, const XColor *color, const Rbc_RenderRectangle *rectangles,
                                    Tcl_Size count);
/**
 * @brief Open an export plot scope; pair with PlotEnd before releasing the context.
 *
 * @param[in,out] ctx Live context created for export output; its constructor must support this operation.
 * @param[in] font Borrowed font used by backend plot setup.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 * @param[in] width Rectangle width in display units.
 * @param[in] height Rectangle height in display units.
 * @param[in] background Borrowed background color used by backend plot setup.
 */
void Rbc_RenderPlotBegin(Rbc_RenderContext *ctx, Tk_Font font, double x, double y, int width, int height,
                         const XColor *background);
/**
 * @brief Close the export plot scope opened by PlotBegin.
 *
 * @param[in,out] ctx Live context created for export output with an outstanding PlotBegin scope; its
 *     constructor must support this operation.
 */
void Rbc_RenderPlotEnd(Rbc_RenderContext *ctx);

/**
 * @brief Create an owned native screen clip stack.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive until Rbc_RenderEnd releases the returned context.
 * @param[in] drawable Borrowed destination in mapped display coordinates, kept valid until context release.
 * @param[in] copyGC Borrowed pixel-copy GC, or NULL to create a private GC.
 * @param[in] useDC Nonzero requests the Windows DC clip path; other scopes use scratch pixmaps.
 * @return Owned clip context to release with Rbc_RenderEnd; graph and drawable remain borrowed.
 *
 * Only ClipBegin, ClipEnd, ClipDrawable and End are supported. Fetch the current target after each push;
 * None means empty or failed coverage. Absolute coordinates and bitmap masks are preserved. ClipEnd commits
 * drawing to the parent target; End balances outstanding pushes and releases the context.
 * @see Rbc_RenderClipDrawable
 * @see Rbc_RenderEnd
 */
Rbc_RenderContext *Rbc_RenderBeginScreenClip(Graph *graphPtr, Drawable drawable, GC copyGC, int useDC);
/**
 * @brief Query the current target of a screen clip stack.
 *
 * @param[in] ctx Live context returned by Rbc_RenderBeginScreenClip.
 * @return Borrowed current drawable, or None for empty or failed coverage.
 *
 * Requery after changing the clip stack; do not retain a temporary target after popping its scope.
 */
Drawable Rbc_RenderClipDrawable(Rbc_RenderContext *ctx);

/**
 * Nest a rectangular clip without painting a background. Supported by screen
 * clip scopes, Cairo geometry contexts and export contexts.
 *
 * @param[in,out] ctx Live context created for rectangular clipping; its constructor must support this
 *     operation.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 * @param[in] width Rectangle width in display units.
 * @param[in] height Rectangle height in display units.
 */
void Rbc_RenderClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height);
/**
 * @brief Pop one rectangular clip; an empty stack is a no-op.
 * @param[in] ctx Live context with clip support.
 *
 * Screen clip contexts commit their drawing to the parent target on pop.
 * The common dispatcher decrements nesting before calling the backend.
 * @see Rbc_RenderClipBegin
 * @see Rbc_RenderEnd
 */
void Rbc_RenderClipEnd(Rbc_RenderContext *ctx);
/**
 * @brief Emit a bitmap mask through an export-output context.
 *
 * @param[in,out] ctx Live context created for export output; its constructor must support this operation.
 * @param[in] display Borrowed display owning the bitmap.
 * @param[in] bitmap Borrowed bitmap to emit as a mask.
 * @param[in] x Mapped horizontal position in destination coordinates.
 * @param[in] y Mapped vertical position in destination coordinates.
 * @param[in] width Rectangle width in display units.
 * @param[in] height Rectangle height in display units.
 * @param[in] color Borrowed paint color.
 * @param[in] background Backend background/foreground pass selector; preserve the component-provided value.
 */
void Rbc_RenderBitmapMask(Rbc_RenderContext *ctx, Display *display, Pixmap bitmap, double x, double y, int width,
                          int height, const XColor *color, int background);

/** Screen-space symbol template; segment vertices are endpoint pairs. */
typedef enum { RBC_RENDER_CIRCLE, RBC_RENDER_POLYGON, RBC_RENDER_SEGMENTS } Rbc_RenderShapeType;

/** @brief Small value-only symbol geometry record shared by drawing backends. */
typedef struct {
    Rbc_RenderShapeType type; /**< Circle, polygon or independent-segment geometry selector. */
    double radius; /**< Circle radius in display units, used for circle shapes. */
    int nPoints; /**< Number of initialized entries in points, bounded by twelve. */
    Point2D points[12]; /**< Symbol-local vertex offsets; segment endpoints are stored in pairs. */
} Rbc_RenderShape;

/** Native/Cairo element strokes. The GC is borrowed, including its existing
 * clip, dashes and raster operation. TRACES supports Polyline; STRIPS and
 * SEGMENTS support Segments, retaining their respective native batching.
 * Only these operations and End are valid on the returned context. Other
 * screen capabilities continue to use Begin/BeginFill and native fallbacks. */
typedef enum { RBC_RENDER_STROKE_TRACES, RBC_RENDER_STROKE_STRIPS, RBC_RENDER_STROKE_SEGMENTS } Rbc_RenderStrokeKind;

/**
 * @brief Create a batch for element traces, strips or independent segments.
 *
 * Attempts Cairo first, then the native path. Returns NULL when the target
 * or native trace setup cannot be used. The caller handles that failure.
 * For native drawing, the supplied GC retains its raster operation and clip.
 * TRACES permits Polyline; STRIPS and SEGMENTS permit Segments. End the
 * successful context once with Rbc_RenderEnd().
 *
 * @param[in] graphPtr Borrowed owning graph with current mapped geometry, kept alive through the operation or
 *     context lifetime.
 * @param[in] drawable Borrowed destination drawable in mapped widget coordinates, kept valid through context
 *     release.
 * @param[in] gc Borrowed native fallback GC retaining raster operation, dashes and clipping.
 * @param[in] kind TRACES permits Polyline; STRIPS and SEGMENTS permit Segments.
 * @param[in] color Borrowed stroke color for Cairo; native fallback uses the supplied GC.
 * @param[in] width Stroke width in display units; not scaled with geometry.
 * @param[in] dashes Borrowed dash specification, or NULL for a solid stroke.
 * @param[in] offColor Optional borrowed off-dash color.
 */
Rbc_RenderContext *Rbc_RenderBeginStroke(Graph *graphPtr, Drawable drawable, GC gc, Rbc_RenderStrokeKind kind,
                                         const XColor *color, double width, const Rbc_Dashes *dashes,
                                         const XColor *offColor);

/**
 * @brief Try to create a Cairo geometry context; NULL requests caller-selected native fallback.
 *
 * @param[in] graphPtr Borrowed owning graph with current mapped geometry, kept alive through the operation or
 *     context lifetime.
 * @param[in] drawable Borrowed destination drawable in mapped widget coordinates, kept valid through context
 *     release.
 * @param[in] colorPtr Borrowed stroke color required by Cairo setup.
 * @param[in] width Stroke width in display units.
 * @param[in] dashesPtr Borrowed dash specification, or NULL for solid.
 * @param[in] offColorPtr Optional borrowed off-dash color.
 */
Rbc_RenderContext *Rbc_RenderBegin(Graph *graphPtr, Drawable drawable, const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr);

/**
 * Export stroke context: Polyline, Segments, Arc, LineStyle, DashBackground and End only.
 * Available without Cairo; borrows export state and dispatches to its backend.
 *
 * @param[in] exportPtr Borrowed live document state; caller owns document setup, completion and I/O.
 * @param[in] color Borrowed stroke color; backend-specific omission policy applies for NULL.
 * @param[in] lineWidth Stroke width in display units.
 * @param[in] dashes Borrowed dash specification, or NULL for solid.
 * @param[in] capStyle X/Tk line cap style.
 * @param[in] joinStyle X/Tk line join style.
 */
Rbc_RenderContext *Rbc_RenderBeginExport(Rbc_ExportContext *exportPtr, const XColor *color, int lineWidth,
                                         const Rbc_Dashes *dashes, int capStyle, int joinStyle);
/**
 * @brief Query a GC foreground color for Cairo setup; return false if unavailable.
 *
 * @param[in] graphPtr Borrowed owning graph with current mapped geometry, kept alive through the operation or
 *     context lifetime.
 * @param[in] gc Borrowed GC to query.
 * @param[out] color Caller-owned XColor storage for the resolved foreground on success.
 */
int Rbc_RenderGCForeground(Graph *graphPtr, GC gc, XColor *color);
/**
 * @brief Draw mapped points using a Cairo geometry context.
 *
 * @param[in,out] ctx Live context created for Cairo geometry; its constructor must support this operation.
 * @param[in] points Borrowed mapped point array, accessible for count entries during this call.
 * @param[in] count Number of point records, not bytes.
 */
void Rbc_RenderPoints(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
/**
 * @brief Submit a mapped trace to a context supporting polyline operations.
 *
 * @param[in,out] ctx Live context created for polyline drawing; its constructor must support this operation.
 * @param[in] points Borrowed mapped point array, accessible for count entries during this call.
 * @param[in] count Number of point records, not bytes.
 */
void Rbc_RenderPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
/**
 * @brief Submit independent mapped segments to a supporting context.
 *
 * @param[in,out] ctx Live context created for independent segment drawing; its constructor must support this
 *     operation.
 * @param[in] segments Borrowed mapped endpoint pairs, accessible for count records during this call.
 * @param[in] count Number of Segment2D records, not number of endpoints.
 */
void Rbc_RenderSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count);
/**
 * @brief Set cap and join style on a supporting Cairo or export stroke context.
 *
 * @param[in,out] ctx Live context created for Cairo or export stroke styling; its constructor must support
 *     this operation.
 * @param[in] capStyle X/Tk line cap style.
 * @param[in] joinStyle X/Tk line join style.
 */
void Rbc_RenderLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle);
/**
 * @brief Set the off-dash color on a supporting Cairo or export stroke context.
 *
 * @param[in,out] ctx Live context created for Cairo or export stroke styling; its constructor must support
 *     this operation.
 * @param[in] color Optional borrowed off-dash color.
 */
void Rbc_RenderDashBackground(Rbc_RenderContext *ctx, const XColor *color);
/**
 * @brief Draw shape-template symbols using a Cairo geometry context.
 *
 * @param[in,out] ctx Live context created for Cairo geometry; its constructor must support this operation.
 * @param[in] shape Borrowed screen-space symbol template; its points describe polygon vertices or endpoint
 *     pairs.
 * @param[in] centers Borrowed mapped center array.
 * @param[in] count Number of center records.
 * @param[in] fillColor Optional borrowed fill color; NULL suppresses filling.
 * @param[in] outline Nonzero requests outlining with the context stroke color.
 */
void Rbc_RenderSymbols(Rbc_RenderContext *ctx, const Rbc_RenderShape *shape, const Point2D *centers, Tcl_Size count,
                       const XColor *fillColor, int outline);
/**
 * @brief Unwind remaining clip scopes and destroy an allocated context.
 * @param[in] ctx Live, non-NULL context returned by a renderer constructor.
 *
 * The context is invalid after this call. Backend-owned temporary resources
 * are released; borrowed graph, document, GC and image resources are not.
 * PlotBegin/PlotEnd scopes must already be balanced. Stack-based one-shot
 * screen adapters manage their own lifetime and must not be passed here.
 * @see Rbc_RenderClipEnd
 */
void Rbc_RenderEnd(Rbc_RenderContext *ctx);

/**
 * @brief Attempt an opaque Cairo polygon fill.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] points Borrowed mapped polygon vertices, valid for count entries during this call.
 * @param[in] count Number of array entries, not bytes.
 * @param[in] foreground Borrowed foreground color; NULL declines the Cairo attempt.
 * @param[in] background Optional borrowed background color; NULL leaves unpainted pattern bits transparent.
 * @param[in] stipple Borrowed one-bit fill pattern, or None for a solid fill.
 * @return Nonzero when handled (including documented empty no-ops); zero requests caller-selected native fallback.
 *
 * Equivalent to Rbc_RenderAreaOpacity with opacity 1.0. Fewer than three vertices decline the attempt.
 * @see Rbc_RenderAreaOpacity
 */
int Rbc_RenderArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count, const XColor *foreground,
                   const XColor *background, Pixmap stipple);

/**
 * @brief Attempt a Cairo polygon fill with solid-fill opacity.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] points Borrowed mapped polygon vertices, valid for count entries during this call.
 * @param[in] count Number of array entries, not bytes.
 * @param[in] foreground Borrowed foreground color; NULL declines the Cairo attempt.
 * @param[in] background Optional borrowed background color; NULL leaves unpainted pattern bits transparent.
 * @param[in] stipple Borrowed one-bit fill pattern, or None for a solid fill.
 * @param[in] opacity Solid-fill alpha in [0,1]; ignored when stipple is not None.
 * @return Nonzero when handled (including documented empty no-ops); zero requests caller-selected native fallback.
 *
 * Fewer than three vertices decline the attempt. Uses the plot clip and even-odd polygon filling;
 * stipple pixels retain their own alpha and widget-relative origin.
 */
int Rbc_RenderAreaOpacity(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                          const XColor *foreground, const XColor *background, Pixmap stipple, double opacity);
/**
 * @brief Attempt a polygon filled with a repeating photo tile.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] points Borrowed mapped polygon vertices, valid for count entries during this call.
 * @param[in] count Number of array entries, not bytes.
 * @param[in] block Borrowed readable photo block, including valid pitch, channel offsets and pixel storage.
 * @return Nonzero when handled (including documented empty no-ops); zero requests caller-selected native fallback.
 *
 * Uses the plot clip and a toplevel-relative pattern origin. On a Cairo graph, fewer than three vertices
 * decline the attempt; an empty photo block is handled without painting, preserving tile precedence.
 */
int Rbc_RenderTileArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                       const Tk_PhotoImageBlock *block);
/**
 * @brief Attempt to paint a mapped photo block once.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] block Borrowed readable photo block, including valid pitch, channel offsets and pixel storage.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @return Nonzero when handled (including documented empty no-ops); zero requests caller-selected native fallback.
 *
 * Uses the plot clip without scaling or rotation. An empty block on a Cairo graph is a handled no-op.
 * Pixel conversion is temporary; the caller retains its photo and block storage.
 */
int Rbc_RenderPhoto(Graph *graphPtr, Drawable drawable, const Tk_PhotoImageBlock *block, int x, int y);
/**
 * @brief Attempt a Cairo rectangle batch with group opacity.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] rectangles Borrowed mapped rectangle array, valid for count records.
 * @param[in] count Number of array entries, not bytes.
 * @param[in] foreground Borrowed foreground color; NULL declines the Cairo attempt.
 * @param[in] background Optional borrowed background color; NULL leaves unpainted pattern bits transparent.
 * @param[in] stipple Borrowed one-bit fill pattern, or None for a solid fill.
 * @param[in] opacity Group alpha in [0,1], applied to solid and stippled rectangles together.
 * @return Nonzero when handled (including documented empty no-ops); zero requests caller-selected native fallback.
 *
 * Nonpositive counts decline the attempt. Nonpositive rectangle dimensions are skipped. Uses the plot clip;
 * opacity is applied once to the composed batch, not separately at each overlap.
 */
int Rbc_RenderRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *rectangles, Tcl_Size count,
                         const XColor *foreground, const XColor *background, Pixmap stipple, double opacity);
/**
 * @brief Attempt a Cairo context covering an explicit drawable extent.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive until Rbc_RenderEnd releases the returned context.
 * @param[in] drawable Borrowed destination in mapped display coordinates, kept valid until context release.
 * @param[in] width Positive destination width in pixels.
 * @param[in] height Positive destination height in pixels.
 * @param[in] color Borrowed required stroke color.
 * @param[in] lineWidth Stroke width in display units.
 * @param[in] dashes Borrowed dash specification, or NULL for solid.
 * @param[in] offColor Optional borrowed off-dash color.
 * @return Owned context, or NULL when unavailable or dimensions are nonpositive.
 *
 * Uses the full drawable rectangle instead of the plot clip. Supports the Cairo geometry operations;
 * release a successful context with Rbc_RenderEnd.
 * @see Rbc_RenderBegin
 */
Rbc_RenderContext *Rbc_RenderBeginDrawable(Graph *graphPtr, Drawable drawable, int width, int height,
                                           const XColor *color, double lineWidth, const Rbc_Dashes *dashes,
                                           const XColor *offColor);
/**
 * @brief Attempt a Cairo legend swatch with a sample-local pattern origin.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] width Positive destination drawable width in pixels.
 * @param[in] height Positive destination drawable height in pixels.
 * @param[in] r Borrowed sample rectangle in destination coordinates.
 * @param[in] foreground Borrowed foreground color; NULL declines the Cairo attempt.
 * @param[in] background Optional borrowed background color; NULL leaves unpainted pattern bits transparent.
 * @param[in] stipple Borrowed one-bit fill pattern, or None for a solid fill.
 * @param[in] opacity Group alpha in [0,1], including stipple paint.
 * @return Nonzero when handled (including documented empty no-ops); zero requests caller-selected native fallback.
 *
 * Uses the full drawable clip; stipple restarts at the sample top-left corner. For an eligible Cairo graph,
 * nonpositive sample dimensions are a handled no-op.
 */
int Rbc_RenderLegendBar(Graph *graphPtr, Drawable drawable, int width, int height, const Rbc_RenderRectangle *r,
                        const XColor *foreground, const XColor *background, Pixmap stipple, double opacity);
/**
 * @brief Attempt a mapped Cairo bitmap with optional rotated background.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive throughout this synchronous operation.
 * @param[in] drawable Borrowed destination in mapped display coordinates.
 * @param[in] r Borrowed mapped rectangle; its dimensions also specify the source bitmap extent.
 * @param[in] bitmap Borrowed one-bit bitmap; None declines the attempt.
 * @param[in] mask Optional borrowed one-bit visibility mask, or None.
 * @param[in] foreground Borrowed foreground color; NULL declines the Cairo attempt.
 * @param[in] background Optional borrowed background color; NULL leaves unpainted pattern bits transparent.
 * @param[in] polygon Borrowed mapped background polygon, or NULL when absent.
 * @param[in] nPoints Number of background polygon vertices; use zero when no polygon is supplied.
 * @return Nonzero when handled (including documented empty no-ops); zero requests caller-selected native fallback.
 *
 * Uses the plot clip. A polygon with at least three vertices and a background color supplies the rotated
 * background; otherwise the bitmap background policy applies. Source bits are converted before painting.
 * For an eligible Cairo graph, nonpositive rectangle dimensions are a handled no-op.
 */
int Rbc_RenderBitmap(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *r, Pixmap bitmap, Pixmap mask,
                     const XColor *foreground, const XColor *background, const Point2D *polygon, Tcl_Size nPoints);
/**
 * @brief Convert a bitmap once for a Cairo symbol batch.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive until Rbc_RenderEnd releases the returned context.
 * @param[in] drawable Borrowed destination in mapped display coordinates, kept valid until context release.
 * @param[in] bitmap Borrowed one-bit source bitmap; None declines the attempt.
 * @param[in] mask Optional borrowed one-bit visibility mask, or None.
 * @param[in] width Positive source bitmap width in pixels.
 * @param[in] height Positive source bitmap height in pixels.
 * @param[in] foreground Borrowed foreground color; NULL declines the Cairo attempt.
 * @param[in] background Optional borrowed background color; NULL leaves unpainted pattern bits transparent.
 * @param[in] targetWidth Positive selects the explicit drawable clip; zero selects the graph plot clip.
 * @param[in] targetHeight Positive explicit drawable height when targetWidth is positive; otherwise ignored.
 * @return Owned bitmap-symbol context, or NULL requesting native fallback.
 *
 * Source bitmap pixels are copied into an owned pattern. End the returned context with Rbc_RenderEnd.
 * @see Rbc_RenderBitmapSymbols
 */
Rbc_RenderContext *Rbc_RenderBeginBitmapSymbols(Graph *graphPtr, Drawable drawable, Pixmap bitmap, Pixmap mask,
                                                int width, int height, const XColor *foreground,
                                                const XColor *background, int targetWidth, int targetHeight);
/**
 * @brief Paint a prepared bitmap at mapped symbol centers in input order.
 *
 * @param[in,out] ctx Live non-NULL context returned by Rbc_RenderBeginBitmapSymbols.
 * @param[in] centers Borrowed mapped center array, valid for count entries.
 * @param[in] count Number of array entries, not bytes.
 *
 * Converts each center to int before subtracting half the bitmap size. Does not resample or repeat pixels;
 * nonpositive counts paint nothing. The context remains owned by the caller.
 */
void Rbc_RenderBitmapSymbols(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count);

#endif
