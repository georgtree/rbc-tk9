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

/** @brief Presentation callbacks selected by each constructor.
 * Every callback receives a live context of its constructor-selected family
 * as the first argument. Input arrays and Tk resources are borrowed for the
 * synchronous call unless the member contract says otherwise. The context and
 * its target must remain live throughout the operation; callbacks do not own
 * the graph or document. Coordinates are already mapped, not graph data values.
 *
 * NULL members mark unsupported capabilities, not successful no-ops or fallback
 * requests. The common presentation dispatchers do not test for NULL members.
 * A populated table is not permission to use every member on every context:
 * constructors must also initialize the state required by the chosen callback.
 */
typedef struct {
    /** @brief Present text through the selected output backend.
     * Screen-text and document-output contexts borrow the NUL-terminated string and text style for the
     * call. Coordinates are mapped display units; native presentation truncates them to integers. The text
     * service may refresh style GC state; document callbacks append output.
     * @see Rbc_RenderText
     */
    void (*text)(Rbc_RenderContext *, char *string, TextStyle *style, double x, double y);
    /** @brief Emit a photo image into document output.
     * Document-output contexts borrow the photo handle and consume its current pixels synchronously. The
     * mapped origin does not request resampling. No image ownership is transferred.
     * @see Rbc_RenderPhotoImage
     */
    void (*photo)(Rbc_RenderContext *, Tk_PhotoHandle photo, double x, double y);
    /** @brief Capture a window for document output when enabled.
     * Document-output contexts borrow the live Tk window and mapped origin. Capture may allocate temporary
     * raster storage; the window remains caller-owned. Export policy can suppress window capture.
     * @see Rbc_RenderWindow
     */
    void (*window)(Rbc_RenderContext *, Tk_Window tkwin, double x, double y);
    /** @brief Paint a document background polygon.
     * Document-output contexts borrow the color and count mapped vertices for this call. This explicit
     * color operation does not replace the context fill policy. Count measures vertices, not bytes.
     * @see Rbc_RenderBackgroundPolygon
     */
    void (*backgroundPolygon)(Rbc_RenderContext *, const XColor *color, const Point2D *points, Tcl_Size count);
    /** @brief Draw a relief border and optional interior.
     * Screen-decoration or document-output contexts borrow the Tk border. Origin, dimensions and border
     * width use display pixels; relief is a Tk relief selector and fill enables the interior. Native
     * drawing uses the supplied window target; export emits its representation.
     * @see Rbc_RenderBorder
     */
    void (*border)(Rbc_RenderContext *, Tk_3DBorder border, double x, double y, int width, int height, int borderWidth,
                   int relief, int fill);
    /** @brief Clear a rectangle in document output.
     * Document-output contexts receive a mapped origin and pixel dimensions. Clearing follows the selected
     * document backend, not a portable framebuffer erase or a change to the context paint policy.
     * @see Rbc_RenderClearRectangle
     */
    void (*clearRectangle)(Rbc_RenderContext *, double x, double y, int width, int height);
    /** @brief Paint document rectangles with an explicit background color.
     * Document-output contexts borrow the color and count rectangle records. Rectangles use mapped pixel
     * geometry. The array and color are consumed synchronously and are not retained.
     * @see Rbc_RenderBackgroundRectangles
     */
    void (*backgroundRectangles)(Rbc_RenderContext *, const XColor *color, const Rbc_RenderRectangle *rectangles,
                                 Tcl_Size count);
    /** @brief Open a document plot scope.
     * Document-output contexts borrow the font and optional background, and receive mapped bounds in
     * pixels. Backend graphics state or output groups may be opened. Pair with plotEnd; this scope is
     * separate from the rectangular clipDepth stack.
     * @see Rbc_RenderPlotBegin
     */
    void (*plotBegin)(Rbc_RenderContext *, Tk_Font font, double x, double y, int width, int height,
                      const XColor *background);
    /** @brief Close the matching document plot scope.
     * Requires a preceding plotBegin on the same document-output context. Emits the backend scope closure
     * without releasing the context or changing common clipDepth.
     * @see Rbc_RenderPlotEnd
     */
    void (*plotEnd)(Rbc_RenderContext *);
    /** @brief Emit a one-bit bitmap mask into document output.
     * Document-output contexts borrow the display, bitmap and color for synchronous readback/emission.
     * Origin and extent use mapped pixels; background selects the background-mask convention. Temporary
     * image storage does not transfer ownership of the input bitmap.
     * @see Rbc_RenderBitmapMask
     */
    void (*bitmapMask)(Rbc_RenderContext *, Display *display, Pixmap bitmap, double x, double y, int width, int height,
                       const XColor *color, int background);
    /** @brief Present a Tk image on a screen-raster context.
     * Borrows the image for synchronous presentation at a mapped origin. The adapter can try Cairo for
     * supported photos and otherwise use native Tk drawing. It does not retain the image or perform
     * component mapping.
     * @see Rbc_RenderTkImage
     */
    void (*image)(Rbc_RenderContext *, Tk_Image image, double x, double y);
    /** @brief Draw a cached layout through native screen text.
     * Screen-text contexts borrow the layout and mutable TextStyle; integer coordinates use the captured
     * destination. Does not rebuild or free the layout. Style GC state may be refreshed.
     * @see Rbc_RenderTextLayout
     */
    void (*textLayout)(Rbc_RenderContext *, TextLayout *, TextStyle *, int, int);
    /** @brief Draw native screen text and report its rotated extent.
     * Screen-text contexts borrow the string and mutable TextStyle and write the required caller-owned
     * Dim2D output. Integer coordinates use the captured target. This operation draws; it is not a
     * measurement-only query.
     * @see Rbc_RenderTextSize
     */
    void (*textSize)(Rbc_RenderContext *, const char *, TextStyle *, int, int, Dim2D *);
    /** @brief Present a mapped bitmap with its component paint policy.
     * Screen-raster contexts borrow the rectangle and bitmap style, including GCs, pixmaps and colors.
     * Geometry is already mapped; the adapter tries the configured renderer before its native fallback. No
     * resource is retained beyond the call.
     * @see Rbc_RenderMappedBitmap
     */
    void (*mappedBitmap)(Rbc_RenderContext *, const Rbc_RenderRectangle *, const Rbc_RenderBitmapStyle *);
    /** @brief Copy a mapped pixmap onto the screen target.
     * Screen-raster contexts borrow the pixmap, GC and mapped rectangle. Copies the selected extent from
     * source origin zero using caller-selected GC semantics; does not scale the source.
     * @see Rbc_RenderPixmap
     */
    void (*pixmap)(Rbc_RenderContext *, Pixmap, GC, const Rbc_RenderRectangle *);
    /** @brief Fill a native screen rectangle with an optional tile.
     * Screen-decoration contexts borrow the GC and tile; integer origin and dimensions use widget pixels.
     * Native tile and stipple-origin behavior is preserved. This callback is also used when the graph
     * selects Cairo.
     * @see Rbc_RenderScreenRectangle
     */
    void (*screenRectangle)(Rbc_RenderContext *, GC, Rbc_Tile, int, int, int, int);
    /** @brief Draw the native window focus highlight.
     * Screen-decoration contexts borrow the GC and receive highlight thickness in pixels. Uses the
     * captured window/drawable and native Tk behavior even for a Cairo graph.
     * @see Rbc_RenderScreenFocus
     */
    void (*focus)(Rbc_RenderContext *, GC, int);
    /** @brief Copy completed screen pixels between borrowed drawables.
     * Screen-decoration contexts capture the destination; arguments provide the source, GC, integer
     * source/destination origins and unsigned pixel extents. Preserves GC raster and clipping behavior and
     * does not rerender geometry.
     * @see Rbc_RenderScreenCopyArea
     */
    void (*copyArea)(Rbc_RenderContext *, Drawable, GC, int, int, unsigned int, unsigned int, int, int);
    /** @brief Fill a native text-marker background polygon.
     * Screen-detail contexts borrow the GC, four outline vertices and mapped anchor. The anchor is added
     * before native integer conversion. No independent track/plot clip is introduced.
     * @see Rbc_RenderScreenTextBackground
     */
    void (*textBackground)(Rbc_RenderContext *, GC, const Point2D *, const Point2D *);
    /** @brief Scan-convert a native arrowhead.
     * Screen-detail contexts borrow the GC and RBC_RENDER_ARROW_POINTS finite mapped vertices, including
     * the repeated closing point. Preserves span batching and the enclosing clip; the vertex array is not
     * retained.
     * @see Rbc_RenderScreenArrowHead
     */
    void (*arrowHead)(Rbc_RenderContext *, GC, const Point2D *);
    /** @brief Draw a native line legend sample.
     * Screen-detail contexts borrow the GC and integer center/half-length. Emits two rows at y and y+1;
     * each row restarts the native dash pattern. Uses the captured target without retaining the GC.
     * @see Rbc_RenderScreenLegendLine
     */
    void (*legendLine)(Rbc_RenderContext *, GC, int, int, int);
    /** @brief Draw a native bar legend sample.
     * Screen-detail contexts borrow the fill GC and receive integer origin/side length. The native
     * operation establishes a local stipple origin and resets it to zero, rather than restoring an
     * arbitrary previous origin.
     * @see Rbc_RenderScreenLegendBar
     */
    void (*legendBar)(Rbc_RenderContext *, GC, int, int, int);
    /** @brief Execute a synchronous native drawing run.
     * Screen-detail contexts borrow callback data and the run procedure. Count is an item count. The
     * procedure may use the supplied drawable only during the call and must preserve graph/resource
     * lifetime; the backend retains neither data nor procedure.
     * @see Rbc_RenderScreenRun
     */
    void (*run)(Rbc_RenderContext *, void *, Tcl_Size, Rbc_RenderRunProc *);
    /** @brief Present a batch of native bitmap markers.
     * Screen-detail contexts borrow the bitmap, GC and count integer positions. Width and height describe
     * the bitmap extent in pixels. Preserves native DC batching and mask/opaque-bitmap behavior; no input
     * array is retained.
     * @see Rbc_RenderScreenBitmapRun
     */
    void (*bitmapRun)(Rbc_RenderContext *, Pixmap, GC, int, int, const Rbc_RenderPosition *, Tcl_Size);
} Rbc_RenderOutputOps;

/**
 * @brief Geometry dispatch and lifetime operations for one context family.
 *
 * Populate only supported operations. Dispatch generally calls them directly;
 * a caller using the wrong context family can dereference a NULL callback.
 * Clip callbacks implement backend state; the common layer owns clipDepth.
 * Every callback receives its live constructor-selected context first. Geometry
 * arrays are borrowed for the synchronous call and counts measure records, not
 * bytes. Backend operation tables are immutable and remain live for all contexts
 * that reference them. No operation implicitly changes the context family.
 */
typedef struct {
    /** @brief Stroke one connected mapped trace.
     * Trace-capable stroke or document contexts borrow count Point2D vertices for the call. The backend
     * owns batching and integer conversion; separate calls are separate paths. Common dispatch does not
     * check for a NULL callback.
     * @see Rbc_RenderPolyline
     */
    void (*polyline)(Rbc_RenderContext *, const Point2D *, Tcl_Size);
    /** @brief Stroke independent mapped endpoint pairs.
     * Segment/strip-capable stroke, native decoration or document contexts borrow count Segment2D records.
     * Each record is independent; backend batching preserves the selected native or document semantics.
     * @see Rbc_RenderSegments
     */
    void (*segments)(Rbc_RenderContext *, const Segment2D *, Tcl_Size);
    /** @brief Update stroke cap and join state.
     * Stroke-capable contexts receive X cap/join selectors. The callback translates or emits the backend
     * state; it neither replaces the context nor takes ownership of any resource.
     * @see Rbc_RenderLineStyle
     */
    void (*lineStyle)(Rbc_RenderContext *, int, int);
    /** @brief Set or disable the off-dash stroke underlay.
     * Stroke-capable contexts borrow the color for the call; NULL disables the underlay. Implementations
     * copy or emit the color and retain the current dash pattern.
     * @see Rbc_RenderDashBackground
     */
    void (*dashBackground)(Rbc_RenderContext *, const XColor *);
    /** @brief Fill a mapped polygon using the context policy.
     * Fill-capable contexts borrow count vertices for the call. The common wrapper ignores fewer than
     * three points; a dispatched call requires a live context; backend fill rule, pattern, opacity and
     * fallback remain constructor-specific.
     * @see Rbc_RenderFillPolygon
     */
    void (*fillPolygon)(Rbc_RenderContext *, const Point2D *, Tcl_Size);
    /** @brief Fill mapped rectangle records using the context policy.
     * Fill or bar contexts borrow count rectangles. The common wrapper suppresses nonpositive counts; a
     * dispatched call requires a live context. Bar contexts can add relief borders; a generic fill
     * callback does not imply bar styling.
     * @see Rbc_RenderFillRectangles
     */
    void (*fillRectangles)(Rbc_RenderContext *, const Rbc_RenderRectangle *, Tcl_Size);
    /** @brief Present symbols at mapped centers.
     * Symbol contexts borrow count center points; type and paint resources come from construction. Native
     * sampling can update the borrowed style counter. The common wrapper suppresses nonpositive counts; a
     * dispatched call requires a live context.
     * @see Rbc_RenderSymbolPoints
     */
    void (*symbolPoints)(Rbc_RenderContext *, const Point2D *, Tcl_Size);
    /** @brief Borrow the immutable presentation-operation table.
     * NULL means this context has no presentation family. A non-NULL table still contains NULL members for
     * unsupported capabilities; callers must select a constructor supporting the specific operation. The
     * context never frees this table.
     */
    const Rbc_RenderOutputOps *output;
    /** @brief Release an owned context and its backend resources.
     * Rbc_RenderEnd unwinds common rectangular clips before invoking this callback. The callback releases
     * owned graphics state and the context allocation, while borrowed graph, document and style resources
     * survive. Stack-based one-shot contexts have no destructor and must not be ended.
     * @see Rbc_RenderEnd
     */
    void (*end)(Rbc_RenderContext *);
    /** @brief Stroke normalized ellipse geometry.
     * Arc-capable stroke contexts borrow a normalized local geometry copy for this call. Rbc_RenderArc
     * rejects non-finite or degenerate geometry before dispatch. The callback constructs the path under
     * the current clip without retaining the copy.
     * @see Rbc_RenderArc
     */
    void (*arc)(Rbc_RenderContext *, const Rbc_RenderArcGeometry *);
    /** @brief Fill a normalized closed ellipse segment.
     * Arc-capable fill contexts borrow a normalized local geometry copy. Rbc_RenderFillArc suppresses OPEN
     * style and invalid geometry before dispatch. The context fill policy supplies paint; the geometry
     * copy is not retained.
     * @see Rbc_RenderFillArc
     */
    void (*fillArc)(Rbc_RenderContext *, const Rbc_RenderArcGeometry *);
    /** @brief Push an intersecting rectangular backend clip.
     * Clip-capable contexts receive integer mapped origin and pixel dimensions. The common dispatcher
     * increments clipDepth before calling; the callback manages backend state only. Native clip contexts
     * can select a scratch target. Pair each push with clipEnd.
     * @see Rbc_RenderClipBegin
     */
    void (*clipBegin)(Rbc_RenderContext *, int, int, int, int);
    /** @brief Restore the previous backend clip state.
     * Invoked only for an outstanding common clip scope; the dispatcher decrements clipDepth before
     * calling. The backend restores or copies back its saved state without changing the common counter.
     * Rbc_RenderEnd unwinds remaining scopes.
     * @see Rbc_RenderClipEnd
     */
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
