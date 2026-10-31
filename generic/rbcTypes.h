/*
 * rbcTypes.h --
 *
 *      Shared padding and geometry types.  This private header has no
 *      dependency on graph, renderer, or text implementation headers.
 *
 * See "license.terms" for details.
 */

#ifndef RBC_TYPES_H
#define RBC_TYPES_H

#include <tcl.h>

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

#endif /* RBC_TYPES_H */
