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

/** Mapping-time polygon capacity for the selected output. TCL_SIZE_MAX means
 * no request-size bound; zero means no polygon can be submitted. Components
 * reserve space for closure/clipping and preserve shared samples when splitting.
 * This query needs no drawing context and allocates no resources. */
Tcl_Size Rbc_RenderPolygonPointLimit(Graph *graphPtr);

/** Native-only screen details borrow the exact GC, preserving XOR and dash
 * semantics. Arrow points include the repeated closing vertex. Text backgrounds
 * consume four outline points and add the anchor before integer conversion. */
#define RBC_RENDER_ARROW_POINTS 6
typedef int Rbc_RenderPosition[2]; /**< Full-width integer x/y, never XPoint shorts. */
/** @brief Synchronous callback receiving borrowed data, target and run count. */
typedef void Rbc_RenderRunProc(void *, Drawable, Tcl_Size);
/** @brief Draw four native text-background vertices, adding the anchor before integer conversion. */
void Rbc_RenderScreenTextBackground(Graph *, Drawable, GC, const Point2D *, const Point2D *);
/** @brief Draw a native arrowhead from the mapped, closed point array. */
void Rbc_RenderScreenArrowHead(Graph *, Drawable, GC, const Point2D *);
/** @brief Draw a native line legend sample using the borrowed GC. */
void Rbc_RenderScreenLegendLine(Graph *, Drawable, GC, int, int, int);
/** @brief Draw a native bar legend sample and restore its stipple origin. */
void Rbc_RenderScreenLegendBar(Graph *, Drawable, GC, int, int, int);

/** Runs are synchronous: callback/data/positions are borrowed only until return.
 * Run owns a shared platform target when available, otherwise uses the original
 * drawable. BitmapRun requires an opaque, unmasked GXcopy-compatible bitmap. */
void Rbc_RenderScreenRun(Graph *, Drawable, void *, Tcl_Size, Rbc_RenderRunProc *);
/** @brief Present a synchronous batch of opaque, unmasked bitmap copies. */
void Rbc_RenderScreenBitmapRun(Graph *, Drawable, Pixmap, GC, int, int, const Rbc_RenderPosition *, Tcl_Size);

/** @brief Try to acquire a shared Cairo marker-pass target; NULL leaves the original drawable usable. */
Rbc_RenderTarget *Rbc_RenderBeginMarkerPass(Graph *graphPtr, Drawable *drawablePtr);
/** @brief Present and release a marker-pass target; NULL is accepted. */
void Rbc_RenderEndMarkerPass(Rbc_RenderTarget *target);

/** Screen text uses Tk for native and Cairo graphs alike. Supports Text,
 * TextLayout, TextSize and End only. The window, drawable and cached layout
 * are borrowed; existing integer positions and clipping are retained. Reuse
 * a context for a label batch. TextSize returns the rotated layout extent. */
Rbc_RenderContext *Rbc_RenderBeginScreenText(Tk_Window tkwin, Drawable drawable);
/** @brief Draw a borrowed cached text layout through a screen text context. */
void Rbc_RenderTextLayout(Rbc_RenderContext *ctx, TextLayout *layout, TextStyle *style, int x, int y);
/** @brief Draw screen text and return its rotated layout extent. */
void Rbc_RenderTextSize(Rbc_RenderContext *ctx, const char *string, TextStyle *style, int x, int y, Dim2D *size);

/** Keep bar geometry in full-width widget coordinates. */
typedef struct {
    int x, y, width, height;
} Rbc_RenderRectangle;

/** Mapped bitmap presentation borrows the marker's private GCs and resources.
 * A rotated opaque background is already clipped/mapped by the component;
 * mask then refers to the bitmap itself, as in the legacy drawing path. */
#define RBC_RENDER_BITMAP_OUTLINE_POINTS 12
typedef struct {
    Pixmap bitmap, mask;
    GC gc, fillGC;
    const XColor *foreground, *background;
    const Point2D *backgroundPoints;
    Tcl_Size backgroundCount; /**< At most RBC_RENDER_BITMAP_OUTLINE_POINTS. */
} Rbc_RenderBitmapStyle;

/** Screen raster contexts support TkImage, MappedBitmap, Pixmap and End only.
 * Cairo is attempted for photos/bitmaps; native fallback preserves Tk image
 * types and GC masks. The context borrows its drawable and all input resources.
 * Scaling/rotation remain in component mapping, outside presentation. */
Rbc_RenderContext *Rbc_RenderBeginScreenRaster(Graph *graphPtr, Drawable drawable);
/** @brief Present a mapped bitmap through a screen raster context, retaining native fallback. */
void Rbc_RenderMappedBitmap(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *r, const Rbc_RenderBitmapStyle *style);
/** @brief Copy a mapped pixmap through a screen raster context using the borrowed GC. */
void Rbc_RenderPixmap(Rbc_RenderContext *ctx, Pixmap pixmap, GC gc, const Rbc_RenderRectangle *r);

/** Native widget decorations retain Tk relief shading, GC state and tile
 * origins for both screen renderers. These one-shot operations borrow all
 * resources and do not change backing-store ownership or painter order. */
void Rbc_RenderScreenBorder(Tk_Window, Drawable, Tk_3DBorder, int, int, int, int, int, int, int);
/** @brief Paint a native solid or tiled decoration rectangle. */
void Rbc_RenderScreenRectangle(Tk_Window, Drawable, GC, Rbc_Tile, int, int, int, int);

/** Native-only, even on Cairo graphs. Borrows the GC without changing its
 * raster operation, clipping or style. Does not allocate a context. */
void Rbc_RenderScreenSegments(Tk_Window, Drawable, GC, const Segment2D *, Tcl_Size);

/** Screen-only pixel copy, including backing-store restoration and final frame
 * presentation. Borrows the GC and both drawables; never changes ownership. */
void Rbc_RenderScreenCopyArea(Tk_Window, Drawable, Drawable, GC, int, int, unsigned int, unsigned int, int, int);
/** @brief Draw the native focus highlight using the borrowed GC. */
void Rbc_RenderScreenFocus(Tk_Window, Drawable, GC, int);

/** Fill policy shared by screen and export. backgroundOnly preserves the
 * legacy PostScript fallback for image-tiled areas. */
typedef struct {
    const XColor *foreground, *background;
    Pixmap stipple;
    double opacity; /**< Solid polygon opacity; PostScript ignores it. */
    int backgroundOnly;
} Rbc_RenderFillStyle;

/** Fill contexts support FillPolygon/FillRectangles/FillArc and End. Screen creation
 * may return NULL for native fallback; export does not require Cairo. */
Rbc_RenderContext *Rbc_RenderBeginFill(Graph *graphPtr, Drawable drawable, const Rbc_RenderFillStyle *style);
/** @brief Create a document fill context borrowing export state and style resources. */
Rbc_RenderContext *Rbc_RenderBeginExportFill(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                             const Rbc_RenderFillStyle *style);

/** Supply a tile after BeginExportFill; PostScript keeps its background fallback. */
void Rbc_RenderSetFillTile(Rbc_RenderContext *ctx, Rbc_Tile tile);
/** @brief Fill a mapped polygon; fewer than three points draw nothing. */
void Rbc_RenderFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
/** @brief Fill mapped rectangles; nonpositive counts draw nothing. */
void Rbc_RenderFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count);

/** Ellipse in mapped display coordinates. Zero degrees points right; positive
 * sweeps turn counterclockwise. |extent| >= 360 is one closed ellipse, without
 * a radial seam. The renderer constructs the complete path under the context's
 * clip; callers must not clip/flatten it first. Geometry transforms do not
 * scale stroke width, dashes, or fill patterns. This is a private C interface. */
typedef enum { RBC_RENDER_ARC_OPEN, RBC_RENDER_ARC_CHORD, RBC_RENDER_ARC_PIESLICE } Rbc_RenderArcStyle;

typedef struct {
    double cx, cy, rx, ry;
    double start, extent;
    Rbc_RenderArcStyle style;
} Rbc_RenderArcGeometry;

/** Use a stroke context for Arc and a fill context for FillArc. FillArc ignores
 * OPEN. Non-finite/degenerate geometry draws nothing. Screen contexts may be
 * unavailable (native renderer); callers retain their native fallback. */
void Rbc_RenderArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc);
/** @brief Fill a normalized mapped ellipse segment; open arcs draw nothing. */
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

typedef struct {
    Rbc_RenderSymbolType type;
    int size, outlineWidth;
    const XColor *outlineColor, *fillColor;
    Pixmap bitmap, mask;
} Rbc_RenderSymbolStyle;

/** Borrowed native symbol resources. Sampling state is returned through counter;
 * preserve it across batches belonging to the same element. */
typedef struct {
    Rbc_RenderSymbolType type;
    int outlineWidth;
    GC outlineGC, fillGC;
    Pixmap bitmap, mask;
    Tcl_Size interval, counter;
} Rbc_RenderNativeSymbolStyle;

typedef struct {
    Tk_3DBorder border;
    XColor *fgColor;
    GC gc;
    Pixmap stipple;
    int borderWidth, relief;
    double fillOpacity;
} Rbc_RenderBarStyle;

/** Screen-only adapters. Native symbol contexts support SymbolPoints/End;
 * bar contexts support FillRectangles/End (including the existing bevel).
 * Fill contexts support FillPolygon/End. Native arc contexts retain mapped,
 * clipped fallback geometry and support Arc/FillArc/End. All input resources
 * and geometry are borrowed until End; no public Tcl or vector ABI changes. */
Rbc_RenderContext *Rbc_RenderBeginNativeSymbols(Graph *, Drawable, Rbc_RenderNativeSymbolStyle *, int size);
/** @brief Create a native bar context supporting FillRectangles and End. */
Rbc_RenderContext *Rbc_RenderBeginBars(Graph *, Drawable, const Rbc_RenderBarStyle *);
/** @brief Create a native polygon-fill context borrowing its GC, tile and stipple. */
Rbc_RenderContext *Rbc_RenderBeginNativeFill(Graph *, Drawable, GC, Rbc_Tile, Pixmap);

/** Native-only Segments/End; also preserves XOR and preinstalled GC clips. */
Rbc_RenderContext *Rbc_RenderBeginNativeSegments(Graph *, Drawable, GC);
/** @brief Create a native arc context borrowing pre-mapped fill points and outline segments until End. */
Rbc_RenderContext *Rbc_RenderBeginNativeArc(Graph *, Drawable, GC, const Point2D *, Tcl_Size, const Segment2D *,
                                            Tcl_Size);
/** @brief Create a native polygon batch with the supplied maximum point capacity. */
Rbc_RenderContext *Rbc_RenderBeginNativePolygonBatch(Graph *, Drawable, GC, Tcl_Size maxPoints);

/** Export symbol contexts support SymbolPoints and End. Screen shape batching
 * retains its existing interface. Contexts borrow bitmap/color resources.
 * Finish each symbol batch before starting another (the prolog is shared). */
Rbc_RenderContext *Rbc_RenderBeginExportSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                               const Rbc_RenderSymbolStyle *style);
/** @brief Create a document bar-symbol context; finish this batch before another symbol batch. */
Rbc_RenderContext *Rbc_RenderBeginExportBarSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                                  const Rbc_RenderFillStyle *style, int size);
/** @brief Draw symbols at mapped centers; native sampling may update the borrowed style counter. */
void Rbc_RenderSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count);

/** Export presentation primitives. Output contexts borrow the export state and Tk
 * resources. PlotBegin/PlotEnd must be paired; End releases only the context.
 * Document setup, trailers and I/O remain with each backend command. */
Rbc_RenderContext *Rbc_RenderBeginExportOutput(Rbc_ExportContext *exportPtr);
/** @brief Draw text through an export-output or screen-text context. */
void Rbc_RenderText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y);

/** Non-photo image fallback; the legacy PostScript backend omits these images. */
void Rbc_RenderTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y);
/** @brief Write a borrowed Tk photo through an export-output context. */
void Rbc_RenderPhotoImage(Rbc_RenderContext *ctx, Tk_PhotoHandle photo, double x, double y);
/** @brief Capture a borrowed Tk window through an export-output context, subject to backend policy. */
void Rbc_RenderWindow(Rbc_RenderContext *ctx, Tk_Window tkwin, double x, double y);
/** @brief Paint a mapped background polygon through an export-output context. */
void Rbc_RenderBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points, Tcl_Size count);
/** @brief Draw a border through an export-output context. */
void Rbc_RenderBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                      int borderWidth, int relief, int fill);
/** @brief Clear a mapped rectangle through an export-output context. */
void Rbc_RenderClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height);
/** @brief Paint mapped background rectangles through an export-output context. */
void Rbc_RenderBackgroundRectangles(Rbc_RenderContext *ctx, const XColor *color, const Rbc_RenderRectangle *rectangles,
                                    Tcl_Size count);
/** @brief Open an export plot scope; pair with PlotEnd before releasing the context. */
void Rbc_RenderPlotBegin(Rbc_RenderContext *ctx, Tk_Font font, double x, double y, int width, int height,
                         const XColor *background);
/** @brief Close the export plot scope opened by PlotBegin. */
void Rbc_RenderPlotEnd(Rbc_RenderContext *ctx);

/** Screen clip scopes preserve absolute coordinates, shared GCs and bitmap
 * masks. copyGC is borrowed, or NULL to create a private copy GC. useDC keeps
 * the existing Windows symbol path; other scopes use reusable scratch pixmaps.
 * Only ClipBegin/ClipEnd/ClipDrawable/End are supported by this context.
 * Fetch ClipDrawable after each push; None means the clip is empty or failed.
 * End balances any outstanding pushes. Native/Cairo drawing into the returned
 * drawable is committed to its parent on ClipEnd. */
Rbc_RenderContext *Rbc_RenderBeginScreenClip(Graph *, Drawable, GC copyGC, int useDC);
/** @brief Return the current drawable for a screen clip context; None means empty or failed. */
Drawable Rbc_RenderClipDrawable(Rbc_RenderContext *);

/** Nest a rectangular clip without painting a background. Supported by screen
 * clip scopes, Cairo geometry contexts and export contexts. */
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
/** @brief Emit a bitmap mask through an export-output context. */
void Rbc_RenderBitmapMask(Rbc_RenderContext *ctx, Display *display, Pixmap bitmap, double x, double y, int width,
                          int height, const XColor *color, int background);

/** Screen-space symbol template; segment vertices are endpoint pairs. */
typedef enum { RBC_RENDER_CIRCLE, RBC_RENDER_POLYGON, RBC_RENDER_SEGMENTS } Rbc_RenderShapeType;

typedef struct {
    Rbc_RenderShapeType type;
    double radius;
    int nPoints;
    Point2D points[12];
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
 */
Rbc_RenderContext *Rbc_RenderBeginStroke(Graph *graphPtr, Drawable drawable, GC gc, Rbc_RenderStrokeKind kind,
                                         const XColor *color, double width, const Rbc_Dashes *dashes,
                                         const XColor *offColor);

/** @brief Try to create a Cairo geometry context; NULL requests caller-selected native fallback. */
Rbc_RenderContext *Rbc_RenderBegin(Graph *graphPtr, Drawable drawable, const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr);

/** Export stroke context: Polyline, Segments, Arc, LineStyle, DashBackground and End only.
 * Available without Cairo; borrows export state and dispatches to its backend. */
Rbc_RenderContext *Rbc_RenderBeginExport(Rbc_ExportContext *exportPtr, const XColor *color, int lineWidth,
                                         const Rbc_Dashes *dashes, int capStyle, int joinStyle);
/** @brief Query a GC foreground color for Cairo setup; return false if unavailable. */
int Rbc_RenderGCForeground(Graph *graphPtr, GC gc, XColor *color);
/** @brief Draw mapped points using a Cairo geometry context. */
void Rbc_RenderPoints(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
/** @brief Submit a mapped trace to a context supporting polyline operations. */
void Rbc_RenderPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
/** @brief Submit independent mapped segments to a supporting context. */
void Rbc_RenderSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count);
/** @brief Set cap and join style on a supporting Cairo or export stroke context. */
void Rbc_RenderLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle);
/** @brief Set the off-dash color on a supporting Cairo or export stroke context. */
void Rbc_RenderDashBackground(Rbc_RenderContext *ctx, const XColor *color);
/** @brief Draw shape-template symbols using a Cairo geometry context. */
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

/** None means a solid fill; FALSE requests native drawing. */
int Rbc_RenderArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count, const XColor *foreground,
                   const XColor *background, Pixmap stipple);

/** Opacity applies only to solid fills (stipple == None). */
int Rbc_RenderAreaOpacity(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                          const XColor *foreground, const XColor *background, Pixmap stipple, double opacity);
/** @brief Try to paint a photo-tiled polygon with Cairo; false requests native fallback. */
int Rbc_RenderTileArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                       const Tk_PhotoImageBlock *block);
/** @brief Try to paint a photo block with Cairo; false requests native fallback. */
int Rbc_RenderPhoto(Graph *graphPtr, Drawable drawable, const Tk_PhotoImageBlock *block, int x, int y);
/** @brief Try to fill rectangles with Cairo; false requests native fallback. */
int Rbc_RenderRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *rectangles, Tcl_Size count,
                         const XColor *foreground, const XColor *background, Pixmap stipple, double opacity);
/** @brief Try to create a Cairo context for a drawable with explicit dimensions. */
Rbc_RenderContext *Rbc_RenderBeginDrawable(Graph *graphPtr, Drawable drawable, int width, int height,
                                           const XColor *color, double lineWidth, const Rbc_Dashes *dashes,
                                           const XColor *offColor);
/** @brief Try to paint a Cairo bar legend sample; false requests native fallback. */
int Rbc_RenderLegendBar(Graph *graphPtr, Drawable drawable, int width, int height, const Rbc_RenderRectangle *r,
                        const XColor *foreground, const XColor *background, Pixmap stipple, double opacity);
/** @brief Try to paint a mapped bitmap with Cairo; false requests native fallback. */
int Rbc_RenderBitmap(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *r, Pixmap bitmap, Pixmap mask,
                     const XColor *foreground, const XColor *background, const Point2D *polygon, Tcl_Size nPoints);
/** @brief Try to create a Cairo bitmap-symbol batch; NULL requests native fallback. */
Rbc_RenderContext *Rbc_RenderBeginBitmapSymbols(Graph *graphPtr, Drawable drawable, Pixmap bitmap, Pixmap mask,
                                                int width, int height, const XColor *foreground,
                                                const XColor *background, int targetWidth, int targetHeight);
/** @brief Draw mapped centers using a Cairo bitmap-symbol context. */
void Rbc_RenderBitmapSymbols(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count);

#endif
