/* Private track layout, state and component integration. Not installed.
 * Include this header when track fields or track-specific services are needed.
 * See license.terms for details. */
#ifndef RBC_GR_TRACK_H
#define RBC_GR_TRACK_H

#include "rbcGraph.h"

/* Paired nested-command adapter; see ownership contract in rbcGrTrack.c. */
Tcl_Obj **Rbc_CreateTrackComponentArgs(Tcl_Size objc, Tcl_Obj *const objv[]);
void Rbc_FreeTrackComponentArgs(Tcl_Obj **args);

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
void Rbc_NotifyTrackView(Graph *graphPtr);
int Rbc_TrackOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
void Rbc_DestroyTrackGrid(Graph *graphPtr, RbcTrack *track);
int Rbc_TrackGridOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_TrackYViewOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_CreateTrackLegend(Graph *graphPtr, RbcTrack *track);
void Rbc_DestroyTrackLegend(Graph *graphPtr, RbcTrack *track);
int Rbc_TrackLegendOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_PointInTrackLegend(Graph *graphPtr, int x, int y);
void Rbc_MapTrackLegends(Graph *graphPtr);
void Rbc_DrawTrackLegends(Graph *graphPtr, Drawable drawable, int raised);
void Rbc_ExportTrackLegends(Graph *graphPtr, Rbc_ExportContext *exportPtr, int raised);
RbcTrack *Rbc_FindTrack(Graph *graphPtr, const char *name);
void Rbc_LayoutTracks(Graph *graphPtr);
void Rbc_EnterTrack(Graph *graphPtr, RbcTrack *track, RbcTrackViewport *saved);
void Rbc_LeaveTrack(Graph *graphPtr, const RbcTrackViewport *saved);
void Rbc_DrawTrackSeparators(Graph *graphPtr, Drawable drawable, Rbc_ExportContext *exportPtr);
int Rbc_MarkersUseTrack(Graph *graphPtr, RbcTrack *track, Axis *axis);

#endif /* RBC_GR_TRACK_H */
