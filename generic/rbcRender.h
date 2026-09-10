/* Private graph rendering interface. See license.terms for details. */
#ifndef RBC_RENDER_H
#define RBC_RENDER_H

#include "rbcGraph.h"

typedef struct Rbc_RenderContext Rbc_RenderContext;

/* Keep bar geometry in full-width widget coordinates. */
typedef struct {
    int x, y, width, height;
} Rbc_RenderRectangle;

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
void Rbc_RenderPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
void Rbc_RenderSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count);
void Rbc_RenderLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle);
void Rbc_RenderSymbols(Rbc_RenderContext *ctx, const Rbc_RenderShape *shape,
                       const Point2D *centers, Tcl_Size count, const XColor *fillColor, int outline);
void Rbc_RenderEnd(Rbc_RenderContext *ctx);
/* None means a solid fill; FALSE requests native drawing. */
int Rbc_RenderArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                   const XColor *foreground, const XColor *background, Pixmap stipple);
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

#endif
