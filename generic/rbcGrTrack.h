/** @file rbcGrTrack.h
 * @brief Private track state and scoped viewport services.
 */
/* Private track layout, state and component integration. Not installed.
 * Include this header when track fields or track-specific services are needed.
 * See license.terms for details. */
#ifndef RBC_GR_TRACK_H
#define RBC_GR_TRACK_H

#include "rbcGraph.h"

/* Paired nested-command adapter; see ownership contract in rbcGrTrack.c. */
Tcl_Obj **Rbc_CreateTrackComponentArgs(Tcl_Size objc, Tcl_Obj *const objv[]);
void Rbc_FreeTrackComponentArgs(Tcl_Obj **args);

/**
 * @brief Saved graph viewport for one paired track-entry scope.
 *
 * Stack storage owned by the caller. Holds borrowed track identity and copies
 * of graph extents, scales and padding; it neither retains nor frees resources.
 * Restore in reverse entry order when nesting scopes.
 */
typedef struct {
    RbcTrack *activeTrack;
    int top, bottom, range, offset, pad1, pad2;
    int left, right, hRange, hOffset, hPad1, hPad2;
    double scale, hScale;
} RbcTrackViewport;

struct RbcTrack {
    int barMode; /* -1 inherits the graph bar mode. */
    Grid *grid;  /* NULL inherits the graph grid. */
    struct GridSegments gridX, gridY;
    Legend *legend;
    char *name;
    double weight;
    int minHeight;
    int height; /* Requested plot size along the track stacking direction. */
    int worldLeft, worldRight, worldTop, worldBottom;
    int left, right, top, bottom; /* Visible intersection with the plot viewport. */
    int axisOffset;
    Axis **axes;
    Tcl_Size nAxes;
    RbcTrack *next;
};

int Rbc_ConfigureTrackGraph(Graph *graphPtr, int scrollCommandChanged);
void Rbc_DestroyTracks(Graph *graphPtr);
/**
 * @brief Repick the stationary pointer and report track scrollbar fractions.
 * Called after native display/layout work; does nothing for non-track graphs
 * or during export. Preserves the graph across binding callbacks, rechecks the
 * live window, then notifies the scrollbar. Callbacks may destroy the widget;
 * the caller must not assume the graph survives this function.
 */
void Rbc_NotifyTrackView(Graph *graphPtr);
int Rbc_TrackOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
/**
 * @brief Release a track's local grid and both mapped segment caches.
 * @param graphPtr Owning graph with live Tk window for local option cleanup.
 * @param track Track whose local grid pointer and caches are cleared.
 *
 * A NULL local grid is allowed. Does not destroy the track or global grid.
 */
void Rbc_DestroyTrackGrid(Graph *graphPtr, RbcTrack *track);
int Rbc_TrackGridOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
/**
 * @brief Query or move the data viewport of a track-owned Y axis or axes.
 * Borrow graph, track and arguments; the enclosing widget command protects
 * lifetime across layout callbacks. An optional -mapy selects one owned axis.
 * Queries use the first selected axis; movement applies to all selected axes.
 * Strict request parsing precedes layout. A later axis failure restores saved
 * requested limits and flags, retaining any already scheduled redraw. Returns
 * TCL_OK or TCL_ERROR with an interpreter result.
 */
int Rbc_TrackYViewOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_CreateTrackLegend(Graph *graphPtr, RbcTrack *track);
/**
 * @brief Destroy one local legend and clear its owning track pointer.
 * @param graphPtr Owning graph; shared local binding table must still be live.
 * @param track Track whose optional legend is destroyed.
 *
 * Cancels idle work and removes the legend's tag bindings, but retains the
 * shared track-legend binding table. Release Tk resources before window loss.
 */
void Rbc_DestroyTrackLegend(Graph *graphPtr, RbcTrack *track);
int Rbc_TrackLegendOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_PointInTrackLegend(Graph *graphPtr, int x, int y);
/**
 * @brief Map local legends after full track rectangles have been laid out.
 * @param graphPtr Borrowed graph with current track geometry.
 *
 * Uses full world width and height and sets each legend origin. Local legends
 * do not contribute graph margins. Does not draw or schedule redisplay.
 */
void Rbc_MapTrackLegends(Graph *graphPtr);
void Rbc_DrawTrackLegends(Graph *graphPtr, Drawable drawable, int raised);
void Rbc_ExportTrackLegends(Graph *graphPtr, Rbc_ExportContext *exportPtr, int raised);
RbcTrack *Rbc_FindTrack(Graph *graphPtr, const char *name);
/**
 * @brief Compute full and visible track rectangles from the current plot.
 * @param graphPtr Borrowed graph with current plot geometry and track settings.
 *
 * Updates track extents and resets axis offsets. Fitted tracks distribute the
 * available stacking range by minima and weights; scrolling tracks use their
 * requested sizes and minima. Export computes a clamped offset without writing
 * it back to the screen's trackOffset. Does not draw or execute Tcl.
 */
void Rbc_LayoutTracks(Graph *graphPtr);
/**
 * @brief Temporarily select a track viewport for clipping and component work.
 * @param graphPtr Borrowed graph whose viewport fields are temporarily changed.
 * @param track Borrowed track, or NULL to select no active track.
 * @param saved Caller-owned storage receiving the previous viewport.
 *
 * Uses visible bounds for clipping and full world bounds for the stacking
 * direction's offset and scale, clearing padding in that direction. Inversion
 * changes the stacking direction from vertical to horizontal. NULL still saves
 * state and changes activeTrack. Always pair with Rbc_LeaveTrack, including on
 * early returns. Does not install a renderer clip or provide lifetime protection.
 */
void Rbc_EnterTrack(Graph *graphPtr, RbcTrack *track, RbcTrackViewport *saved);
/**
 * @brief Restore the graph fields saved by a matching Rbc_EnterTrack call.
 * @param graphPtr Same live graph used on entry.
 * @param saved Intact saved viewport, restored in reverse order of nesting.
 *
 * Restores active track, plot bounds, offsets, ranges, scales and padding.
 * Does not undo other graph mutations, layout work or renderer clipping.
 */
void Rbc_LeaveTrack(Graph *graphPtr, const RbcTrackViewport *saved);
void Rbc_DrawTrackSeparators(Graph *graphPtr, Drawable drawable, Rbc_ExportContext *exportPtr);
int Rbc_MarkersUseTrack(Graph *graphPtr, RbcTrack *track, Axis *axis);

#endif /* RBC_GR_TRACK_H */
