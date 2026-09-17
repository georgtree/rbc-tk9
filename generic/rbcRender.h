/* Private graph rendering interface. See license.terms for details. */
#ifndef RBC_RENDER_H
#define RBC_RENDER_H

#include "rbcGraph.h"
#include "rbcPs.h"

typedef struct Rbc_RenderContext Rbc_RenderContext;
typedef struct Rbc_RenderTarget Rbc_RenderTarget;
Rbc_RenderTarget *Rbc_RenderBeginMarkerPass(Graph *graphPtr, Drawable *drawablePtr);
void Rbc_RenderEndMarkerPass(Rbc_RenderTarget *target);

/* Keep bar geometry in full-width widget coordinates. */
typedef struct {
    int x, y, width, height;
} Rbc_RenderRectangle;

/* Fill policy shared by screen and export. backgroundOnly preserves the
 * legacy PostScript fallback for image-tiled areas. */
typedef struct {
    const XColor *foreground, *background;
    Pixmap stipple;
    double opacity; /* Solid polygon opacity; PostScript ignores it. */
    int backgroundOnly;
} Rbc_RenderFillStyle;

/* Fill contexts support FillPolygon/FillRectangles and End. Screen creation
 * may return NULL for native fallback; export does not require Cairo. */
Rbc_RenderContext *Rbc_RenderBeginFill(Graph *graphPtr, Drawable drawable, const Rbc_RenderFillStyle *style);
Rbc_RenderContext *Rbc_RenderBeginExportFill(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                                const Rbc_RenderFillStyle *style);
/* Supply a tile after BeginExportFill; PostScript keeps its background fallback. */
void Rbc_RenderSetFillTile(Rbc_RenderContext *ctx, Rbc_Tile tile);
void Rbc_RenderFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
void Rbc_RenderFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count);

/* Semantic symbol descriptions for export; colors are resolved by the caller. */
typedef enum {
    RBC_RENDER_SYMBOL_SQUARE, RBC_RENDER_SYMBOL_CIRCLE, RBC_RENDER_SYMBOL_DIAMOND,
    RBC_RENDER_SYMBOL_PLUS, RBC_RENDER_SYMBOL_CROSS, RBC_RENDER_SYMBOL_SPLUS,
    RBC_RENDER_SYMBOL_SCROSS, RBC_RENDER_SYMBOL_TRIANGLE, RBC_RENDER_SYMBOL_ARROW,
    RBC_RENDER_SYMBOL_BITMAP
} Rbc_RenderSymbolType;

typedef struct {
    Rbc_RenderSymbolType type;
    int size, outlineWidth;
    const XColor *outlineColor, *fillColor;
    Pixmap bitmap, mask;
} Rbc_RenderSymbolStyle;

/* Export symbol contexts support SymbolPoints and End. Screen shape batching
 * retains its existing interface. Contexts borrow bitmap/color resources.
 * Finish each symbol batch before starting another (the prolog is shared). */
Rbc_RenderContext *Rbc_RenderBeginExportSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                                  const Rbc_RenderSymbolStyle *style);
Rbc_RenderContext *Rbc_RenderBeginExportBarSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                                     const Rbc_RenderFillStyle *style, int size);
void Rbc_RenderSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count);

/* Export presentation primitives. Output contexts borrow the export state and Tk
 * resources. PlotBegin/PlotEnd must be paired; End releases only the context.
 * Document setup, trailers and I/O remain with each backend command. */
Rbc_RenderContext *Rbc_RenderBeginExportOutput(Rbc_ExportContext *exportPtr);
void Rbc_RenderText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y);
/* Non-photo image fallback; the legacy PostScript backend omits these images. */
void Rbc_RenderTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y);
void Rbc_RenderPhotoImage(Rbc_RenderContext *ctx, Tk_PhotoHandle photo, double x, double y);
void Rbc_RenderWindow(Rbc_RenderContext *ctx, Tk_Window tkwin, double x, double y);
void Rbc_RenderBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points, Tcl_Size count);
void Rbc_RenderBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                                   int borderWidth, int relief, int fill);
void Rbc_RenderClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height);
void Rbc_RenderBackgroundRectangles(Rbc_RenderContext *ctx, const XColor *color,
                                   const Rbc_RenderRectangle *rectangles, Tcl_Size count);
void Rbc_RenderPlotBegin(Rbc_RenderContext *ctx, Tk_Font font, double x, double y, int width, int height,
                                   const XColor *background);
void Rbc_RenderPlotEnd(Rbc_RenderContext *ctx);
void Rbc_RenderBitmapMask(Rbc_RenderContext *ctx, Display *display, Pixmap bitmap, double x, double y, int width,
                                   int height, const XColor *color, int background);

/* Screen-space symbol template; segment vertices are endpoint pairs. */
typedef enum {
    RBC_RENDER_CIRCLE, RBC_RENDER_POLYGON, RBC_RENDER_SEGMENTS
} Rbc_RenderShapeType;

typedef struct {
    Rbc_RenderShapeType type;
    double radius;
    int nPoints;
    Point2D points[12];
} Rbc_RenderShape;

Rbc_RenderContext *Rbc_RenderBegin(Graph *graphPtr, Drawable drawable,
                                   const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr);
/* Export stroke context: Polyline, Segments, LineStyle, DashBackground and End only.
 * Available without Cairo; borrows export state and dispatches to its backend. */
Rbc_RenderContext *Rbc_RenderBeginExport(Rbc_ExportContext *exportPtr, const XColor *color, int lineWidth,
                                            const Rbc_Dashes *dashes, int capStyle, int joinStyle);
int Rbc_RenderGCForeground(Graph *graphPtr, GC gc, XColor *color);
void Rbc_RenderPoints(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
void Rbc_RenderPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
void Rbc_RenderSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count);
void Rbc_RenderLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle);
void Rbc_RenderDashBackground(Rbc_RenderContext *ctx, const XColor *color);
void Rbc_RenderSymbols(Rbc_RenderContext *ctx, const Rbc_RenderShape *shape,
                       const Point2D *centers, Tcl_Size count, const XColor *fillColor, int outline);
void Rbc_RenderEnd(Rbc_RenderContext *ctx);
/* None means a solid fill; FALSE requests native drawing. */
int Rbc_RenderArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                   const XColor *foreground, const XColor *background, Pixmap stipple);
/* Opacity applies only to solid fills (stipple == None). */
int Rbc_RenderAreaOpacity(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                          const XColor *foreground, const XColor *background, Pixmap stipple, double opacity);
int Rbc_RenderTileArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                       const Tk_PhotoImageBlock *block);
int Rbc_RenderPhoto(Graph *graphPtr, Drawable drawable, const Tk_PhotoImageBlock *block, int x, int y);
int Rbc_RenderRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *rectangles,
                         Tcl_Size count, const XColor *foreground, const XColor *background, Pixmap stipple);
Rbc_RenderContext *Rbc_RenderBeginDrawable(Graph *graphPtr, Drawable drawable, int width, int height,
                                         const XColor *color, double lineWidth,
                                         const Rbc_Dashes *dashes, const XColor *offColor);
int Rbc_RenderLegendBar(Graph *graphPtr, Drawable drawable, int width, int height,
                        const Rbc_RenderRectangle *r, const XColor *foreground,
                        const XColor *background, Pixmap stipple);
int Rbc_RenderBitmap(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *r,
                      Pixmap bitmap, Pixmap mask, const XColor *foreground, const XColor *background,
                      const Point2D *polygon, Tcl_Size nPoints);
Rbc_RenderContext *Rbc_RenderBeginBitmapSymbols(Graph *graphPtr, Drawable drawable,
    Pixmap bitmap, Pixmap mask, int width, int height, const XColor *foreground,
    const XColor *background, int targetWidth, int targetHeight);
void Rbc_RenderBitmapSymbols(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count);

#endif
