/* Private graph rendering interface. See license.terms for details. */
#ifndef RBC_RENDER_H
#define RBC_RENDER_H

#include "rbcGraph.h"

typedef struct Rbc_RenderContext Rbc_RenderContext;

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
void Rbc_RenderSymbols(Rbc_RenderContext *ctx, const Rbc_RenderShape *shape,
                       const Point2D *centers, Tcl_Size count, const XColor *fillColor, int outline);
void Rbc_RenderEnd(Rbc_RenderContext *ctx);
/* None means a solid fill; FALSE requests native drawing. */
int Rbc_RenderArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                   const XColor *foreground, const XColor *background, Pixmap stipple);

#endif
