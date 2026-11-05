/**
 * @file rbcTypes.h
 * @brief Shared geometry, padding and dash records.
 *
 *      Shared padding and geometry types.  This private header has no
 *      dependency on graph, renderer, or text implementation headers.
 *
 * See "license.terms" for details.
 */

#ifndef RBC_TYPES_H
#define RBC_TYPES_H

#include <tcl.h>

/** @brief Opposite-side padding in pixels. */
typedef struct {
    int side1; /**< Left padding for X, top padding for Y. */
    int side2; /**< Right padding for X, bottom padding for Y. */
} Rbc_Pad;

#define padLeft padX.side1
#define padRight padX.side2
#define padTop padY.side1
#define padBottom padY.side2
#define PADDING(x) ((Tcl_WideInt)(x).side1 + (Tcl_WideInt)(x).side2)

/** @brief Coordinate value; the containing operation determines units. */
typedef struct {
    double x; /**< X coordinate. */
    double y; /**< Y coordinate. */
} Point2D;

/** @brief Coordinate value; the containing operation determines units. */
typedef struct {
    double x; /**< X coordinate. */
    double y; /**< Y coordinate. */
    double z; /**< Z coordinate. */
} Point3D;

/** @brief Two endpoints of a line segment in the caller coordinate system. */
typedef struct {
    Point2D p; /**< The two end points of the segment. */
    Point2D q; /**< The two end points of the segment. */
} Segment2D;

/** @brief Integer dimensions in pixels. */
typedef struct {
    int width; /**< Horizontal extent. */
    int height; /**< Vertical extent. */
} Dim2D;

/** @brief Rectangle with inclusive integer pixel edges. */
typedef struct {
    int left; /**< Left boundary. */
    int right; /**< Right boundary. */
    int top; /**< Top boundary. */
    int bottom; /**< Bottom boundary. */
} Region2D;

#define RegionWidth(r) ((r)->right - (r)->left + 1)
#define RegionHeight(r) ((r)->bottom - (r)->top + 1)

/** @brief Coordinate bounds; units and axis direction follow the caller. */
typedef struct {
    double left; /**< Left boundary. */
    double right; /**< Right boundary. */
    double top; /**< Top boundary. */
    double bottom; /**< Bottom boundary. */
} Extents2D;

/** @brief Coordinate bounds; units and axis direction follow the caller. */
typedef struct {
    double left; /**< Left boundary. */
    double right; /**< Right boundary. */
    double top; /**< Top boundary. */
    double bottom; /**< Bottom boundary. */
    double front; /**< Front boundary. */
    double back; /**< Back boundary. */
} Extents3D;

#define PointInRegion(e, x, y) (((x) <= (e)->right) && ((x) >= (e)->left) && ((y) <= (e)->bottom) && ((y) >= (e)->top))

#define PointInRectangle(r, x0, y0)                                                                                    \
    (((x0) <= (int)((r)->x + (r)->width - 1)) && ((x0) >= (int)(r)->x) && ((y0) <= (int)((r)->y + (r)->height - 1)) && \
     ((y0) >= (int)(r)->y))

#define RBC_MAX_DASH_VALUES 10

/** @brief Native dash pattern with a zero terminator and phase. */
typedef struct {
    unsigned char values[RBC_MAX_DASH_VALUES + 1]; /**< Dash lengths in pixels followed by zero; first zero selects a solid line. */
    int offset; /**< Dash phase offset in pixels. */
} Rbc_Dashes;

#define LineIsDashed(d) ((d).values[0] != 0)

#endif /* RBC_TYPES_H */
