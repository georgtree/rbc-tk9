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
/**
 * @brief Allocate an argument vector for nested grid or legend dispatch.
 *
 * Require objc >= 5 and the widget track component name operation form. The
 * result has objc - 2 entries and owns only a newly retained list-valued command
 * prefix at index 1; other entries borrow input objects. Release with
 * Rbc_FreeTrackComponentArgs() before those borrowed arguments expire.
 *
 * @param[in] objc Number of input objects; must be at least five.
 * @param[in] objv Borrowed full widget track component name operation argument array; keep its objects alive until the adapter is freed.
 */
Tcl_Obj **Rbc_CreateTrackComponentArgs(Tcl_Size objc, Tcl_Obj *const objv[]);
/**
 * @brief Release the owned command prefix and array from Rbc_CreateTrackComponentArgs().
 * Borrowed argument objects are not decremented. Require a non-NULL result
 * from that allocator and release it exactly once.
 *
 * @param[in] args Non-NULL owned array returned by Rbc_CreateTrackComponentArgs; consumed by this call.
 */
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

/**
 * @brief Validate track-specific widget options before configuration commits.
 *
 * Returns TCL_OK or TCL_ERROR with a graph-interpreter diagnostic. Failure after
 * the cached fraction reset does not undo that reset; the caller owns option rollback.
 * @param[in,out] graphPtr Borrowed owning graph with live component resources on its Tk thread.
 * @param[in] scrollCommandChanged Nonzero when the scroll command changed; resets the cached first fraction after validating its list syntax.
 */
int Rbc_ConfigureTrackGraph(Graph *graphPtr, int scrollCommandChanged);
/**
 * @brief Release all tracks during graph teardown, before destroying axes.
 *
 * Removes track bindings, local grids and legends, then releases axis
 * references, names and records. The caller must already have disposed of
 * dependent elements and markers and retain the component cleanup resources.
 * Unlike command deletion, this does not validate dependencies or redraw.
 *
 * @param[in,out] graphPtr Borrowed owning graph with live component resources on its Tk thread.
 */
void Rbc_DestroyTracks(Graph *graphPtr);
/**
 * @brief Repick the stationary pointer and report track scrollbar fractions.
 * Called after native display/layout work; does nothing for non-track graphs
 * or during export. Preserves the graph across binding callbacks, rechecks the
 * live window, then notifies the scrollbar. Callbacks may destroy the widget;
 * the caller must not assume the graph survives this function.
 *
 * @param[in,out] graphPtr Borrowed graph; callbacks may destroy it. A caller needing subsequent access must separately protect and revalidate it.
 */
void Rbc_NotifyTrackView(Graph *graphPtr);
/**
 * @brief Dispatch the widget track subcommand.
 *
 * Returns Tcl status and sets the interpreter result. The enclosing widget command
 * protects graph lifetime across layout and nested component callbacks.
 * @param[in,out] graphPtr Borrowed owning graph with live component resources on its Tk thread.
 * @param[in,out] interp Borrowed result interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Number of full widget-command arguments, including widget and track words.
 * @param[in] objv Borrowed full command array; operation is at index 2. See @ref rbc_command_arguments.
 */
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
void Rbc_DestroyTrackLegend(Graph *graphPtr, RbcTrack *track);
int Rbc_TrackLegendOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_PointInTrackLegend(Graph *graphPtr, int x, int y);
void Rbc_MapTrackLegends(Graph *graphPtr);
void Rbc_DrawTrackLegends(Graph *graphPtr, Drawable drawable, int raised);
void Rbc_ExportTrackLegends(Graph *graphPtr, Rbc_ExportContext *exportPtr, int raised);
/**
 * @brief Return a borrowed track by exact, case-sensitive name, or NULL.
 * Searches the owning graph list without creating or retaining an object.
 * Reordering preserves the pointer; deletion or graph teardown invalidates it.
 *
 * @param[in] graphPtr Borrowed graph whose track list is searched.
 * @param[in] name Required NUL-terminated exact track name; not retained.
 */
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
/**
 * @brief Draw dividers inside the gaps between adjacent tracks.
 *
 * Uses the same centers as sash positioning. Compressed or off-viewport strokes
 * are omitted. Does nothing for ordinary graphs or an unset separator color.
 * @param[in] graphPtr Borrowed graph with current track world bounds and plot viewport.
 * @param[in] drawable Borrowed screen destination; used by screen rendering and native fallback.
 * @param[in,out] exportPtr Borrowed export context, or NULL for screen output.
 */
void Rbc_DrawTrackSeparators(Graph *graphPtr, Drawable drawable, Rbc_ExportContext *exportPtr);
int Rbc_MarkersUseTrack(Graph *graphPtr, RbcTrack *track, Axis *axis);

#endif /* RBC_GR_TRACK_H */
