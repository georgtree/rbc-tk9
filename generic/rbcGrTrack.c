/*
 * rbcGrTrack.c --
 *
 *      Named track layout, ownership, scrolling and commands for trackgraph.
 *      Shared graph construction, options and rendering dispatch remain in
 *      rbcGraph.c; axis, grid, legend and marker details stay in their modules.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"
#include "rbcRender.h"
#include "rbcGrElem.h"
#include <limits.h>

#define CLAMP(value, low, high) MIN(MAX((value), (low)), (high))

typedef struct {
    Tcl_Obj *weight, *minHeight, *height, *yAxes, *barMode;
} TrackDefaults;

static const Tk_OptionSpec trackOptionSpecs[] = {
    {TK_OPTION_STRING, "-weight", "weight", "Weight", "1.0", offsetof(TrackDefaults, weight), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-minheight", "minHeight", "MinHeight", "40", offsetof(TrackDefaults, minHeight), -1, 0, NULL,
     0},
    {TK_OPTION_STRING, "-height", "height", "Height", "160", offsetof(TrackDefaults, height), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-yaxes", "yAxes", "YAxes", "", offsetof(TrackDefaults, yAxes), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-barmode", "barMode", "BarMode", "inherit", offsetof(TrackDefaults, barMode), -1, 0, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/* Validate the track-specific widget options before graph configuration commits. */
int Rbc_ConfigureTrackGraph(Graph *graphPtr, int scrollCommandChanged) {
    if (graphPtr->trackScroll && !graphPtr->trackGraph) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("scroll track layout requires a trackgraph widget", -1));
        return TCL_ERROR;
    }
    if (graphPtr->trackScrollCmdObj != NULL) {
        Tcl_Size length;
        if (Tcl_ListObjLength(graphPtr->interp, graphPtr->trackScrollCmdObj, &length) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (scrollCommandChanged) {
        graphPtr->trackFirst = -1.0;
    }
    if (graphPtr->trackSeparatorWidth < 1) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("track separator width must be positive", -1));
        return TCL_ERROR;
    }
    if (graphPtr->trackResize && !graphPtr->trackGraph) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-trackresize requires a trackgraph widget", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/* Named tracks share horizontal geometry and own independent vertical regions. */
RbcTrack *Rbc_FindTrack(Graph *graphPtr, const char *name) {
    RbcTrack *track;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        if (strcmp(track->name, name) == 0) {
            return track;
        }
    }
    return NULL;
}

/* Temporarily select a row for existing clipping, reduction and drawing code.
 * Always pair with Rbc_LeaveTrack, including when track is NULL. Axis mapping
 * itself uses the axis owner, so public transforms do not need this scope. */
void Rbc_EnterTrack(Graph *graphPtr, RbcTrack *track, RbcTrackViewport *saved) {
    saved->activeTrack = graphPtr->activeTrack;
    graphPtr->activeTrack = track;
    saved->left = graphPtr->left;
    saved->right = graphPtr->right;
    saved->hRange = graphPtr->hRange;
    saved->hOffset = graphPtr->hOffset;
    saved->hScale = graphPtr->hScale;
    saved->hPad1 = graphPtr->padX.side1;
    saved->hPad2 = graphPtr->padX.side2;
    saved->top = graphPtr->top;
    saved->bottom = graphPtr->bottom;
    saved->range = graphPtr->vRange;
    saved->offset = graphPtr->vOffset;
    saved->scale = graphPtr->vScale;
    saved->pad1 = graphPtr->padY.side1;
    saved->pad2 = graphPtr->padY.side2;
    if (track != NULL && graphPtr->inverted) {
        graphPtr->padX.side1 = graphPtr->padX.side2 = 0;
        graphPtr->left = track->left;
        graphPtr->hOffset = track->worldLeft;
        graphPtr->right = track->right;
        graphPtr->hRange = MAX(1, track->worldRight - track->worldLeft);
        graphPtr->hScale = 1.0 / graphPtr->hRange;
    } else if (track != NULL) {
        graphPtr->padY.side1 = graphPtr->padY.side2 = 0;
        graphPtr->top = track->top;
        graphPtr->vOffset = track->worldTop;
        graphPtr->bottom = track->bottom;
        graphPtr->vRange = MAX(1, track->worldBottom - track->worldTop);
        graphPtr->vScale = 1.0 / graphPtr->vRange;
    }
}

void Rbc_LeaveTrack(Graph *graphPtr, const RbcTrackViewport *saved) {
    graphPtr->activeTrack = saved->activeTrack;
    graphPtr->left = saved->left;
    graphPtr->right = saved->right;
    graphPtr->hRange = saved->hRange;
    graphPtr->hOffset = saved->hOffset;
    graphPtr->hScale = saved->hScale;
    graphPtr->padX.side1 = saved->hPad1;
    graphPtr->padX.side2 = saved->hPad2;
    graphPtr->top = saved->top;
    graphPtr->bottom = saved->bottom;
    graphPtr->vRange = saved->range;
    graphPtr->vOffset = saved->offset;
    graphPtr->vScale = saved->scale;
    graphPtr->padY.side1 = saved->pad1;
    graphPtr->padY.side2 = saved->pad2;
}

/* Fit rows inside the common plot. Minimum sizes take precedence over weights;
 * when they cannot fit, shrink them proportionally and keep the order stable. */
void Rbc_LayoutTracks(Graph *graphPtr) {
    RbcTrack *track;
    double weights = 0.0, minima = 0.0, requested = 0.0, cursor, offset;
    int n = 0, gap, available;
    int range = graphPtr->inverted ? graphPtr->hRange : graphPtr->vRange;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        weights += track->weight;
        minima += track->minHeight;
        requested += MAX(track->height, track->minHeight);
        n++;
    }
    if (n == 0) {
        graphPtr->trackExtent = graphPtr->trackOffset = 0.0;
        return;
    }
    gap = graphPtr->trackScroll ? 12 : MIN(12, MAX(0, range / (n * 4)));
    available = MAX(0, range - gap * (n - 1));
    graphPtr->trackExtent = graphPtr->trackScroll ? requested + gap * (double)(n - 1) : range;
    offset = graphPtr->trackScroll ? CLAMP(graphPtr->trackOffset, 0.0, MAX(0.0, graphPtr->trackExtent - range)) : 0.0;
    if (!(graphPtr->flags & GRAPH_EXPORT)) {
        graphPtr->trackOffset = offset;
    }
    cursor = (graphPtr->inverted ? graphPtr->hOffset : graphPtr->vOffset) - offset;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        double height;
        if (graphPtr->trackScroll) {
            height = MAX(track->height, track->minHeight);
        } else if (minima > available) {
            height = available * (track->minHeight / minima);
        } else {
            height = track->minHeight + (available - minima) * (track->weight / weights);
        }
        track->left = graphPtr->left;
        track->right = graphPtr->right;
        track->top = graphPtr->top;
        track->bottom = graphPtr->bottom;
        if (graphPtr->inverted) {
            track->left = (int)CLAMP(floor(cursor + 0.5), -INT_MAX / 4, INT_MAX / 4);
            track->right = (int)CLAMP(floor(cursor + height + 0.5), -INT_MAX / 4, INT_MAX / 4);
        } else {
            track->top = (int)CLAMP(floor(cursor + 0.5), -INT_MAX / 4, INT_MAX / 4);
            track->bottom = (int)CLAMP(floor(cursor + height + 0.5), -INT_MAX / 4, INT_MAX / 4);
        }
        track->worldLeft = track->left;
        track->worldRight = track->right;
        track->worldTop = track->top;
        track->worldBottom = track->bottom;
        track->left = CLAMP(track->left, graphPtr->left, graphPtr->right);
        track->right = CLAMP(track->right, graphPtr->left, graphPtr->right);
        track->top = CLAMP(track->top, graphPtr->top, graphPtr->bottom);
        track->bottom = CLAMP(track->bottom, graphPtr->top, graphPtr->bottom);
        cursor += height;
        track->axisOffset = 0;
        cursor += gap;
    }
}

/* Keep track-owned Y axes in layout order, independent of orientation. */
static void SyncTrackAxes(Graph *graphPtr) {
    Rbc_Chain *chain = graphPtr->axisChain[1];
    Rbc_ChainLink *link;
    RbcTrack *track;
    Tcl_Size i;
    for (link = Rbc_ChainFirstLink(chain); link != NULL; link = Rbc_ChainNextLink(link)) {
        Axis *axis = Rbc_ChainGetValue(link);
        axis->linkPtr = NULL;
        axis->chainPtr = NULL;
        axis->flags &= ~AXIS_ONSCREEN;
    }
    Rbc_ChainReset(chain);
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        for (i = 0; i < track->nAxes; i++) {
            Axis *axis = track->axes[i];
            axis->chainPtr = chain;
            axis->linkPtr = Rbc_ChainAppend(chain, axis);
            axis->flags |= AXIS_ONSCREEN;
        }
    }
    graphPtr->flags |= RESET_WORLD;
    Rbc_EventuallyRedrawGraph(graphPtr);
}

static void FreeTrack(Graph *graphPtr, RbcTrack *track) {
    Tcl_Size i;
    if (graphPtr->bindTable != NULL) {
        Rbc_DeleteBindings(graphPtr->bindTable, track);
    }
    Rbc_DestroyTrackGrid(graphPtr, track);
    Rbc_DestroyTrackLegend(graphPtr, track);
    for (i = 0; i < track->nAxes; i++) {
        track->axes[i]->track = NULL;
        Rbc_FreeAxisReference(graphPtr, track->axes[i]);
    }
    if (track->axes != NULL) {
        ckfree(track->axes);
    }
    ckfree(track->name);
    ckfree(track);
}

/* Share option names and metadata with creation defaults. */
static int TrackOptionIndex(Tcl_Interp *interp, Tcl_Obj *obj, int *indexPtr) {
    static const char *const names[] = {"-weight", "-minheight", "-height", "-yaxes", "-barmode", NULL};
    return Tcl_GetIndexFromObj(interp, obj, names, "track option", 0, indexPtr);
}

static Tcl_Obj *TrackOption(RbcTrack *track, const char *option) {
    Tcl_Obj *result;
    Tcl_Size i;
    if (strcmp(option, "-barmode") == 0) {
        const char *names[] = {"normal", "stacked", "aligned", "overlap"};
        return Tcl_NewStringObj(track->barMode < 0 ? "inherit" : names[track->barMode], -1);
    }
    if (strcmp(option, "-height") == 0) {
        return Tcl_NewIntObj(track->height);
    }
    if (strcmp(option, "-weight") == 0) {
        return Tcl_NewDoubleObj(track->weight);
    }
    if (strcmp(option, "-minheight") == 0) {
        return Tcl_NewIntObj(track->minHeight);
    }
    if (strcmp(option, "-yaxes") != 0) {
        return NULL;
    }
    result = Tcl_NewListObj(0, NULL);
    for (i = 0; i < track->nAxes; i++) {
        Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj(track->axes[i]->name, -1));
    }
    return result;
}

/* Standard Tk five-field descriptions; current values come from live track
 * state, including heights/weights changed by separator dragging. */
static Tcl_Obj *TrackOptionInfo(RbcTrack *track, int index) {
    const Tk_OptionSpec *spec = trackOptionSpecs + index;
    Tcl_Obj *items[5];
    items[0] = Tcl_NewStringObj(spec->optionName, -1);
    items[1] = Tcl_NewStringObj(spec->dbName, -1);
    items[2] = Tcl_NewStringObj(spec->dbClass, -1);
    items[3] = Tcl_NewStringObj(spec->defValue, -1);
    items[4] = TrackOption(track, spec->optionName);
    return Tcl_NewListObj(5, items);
}

/* Stage all options and axis references before committing a track change. */
static int ConfigureTrack(Graph *graphPtr, RbcTrack *track, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int barMode = track->barMode;
    double weight = track->weight;
    int minHeight = track->minHeight;
    int height = track->height;
    Tcl_Obj *axesObj = NULL;
    Axis **axes = NULL;
    Tcl_Size nAxes = 0, i, j;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    if (objc % 2) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("expected track option/value pairs", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i += 2) {
        int index;
        const char *option;
        if (TrackOptionIndex(graphPtr->interp, objv[i], &index) != TCL_OK) {
            return TCL_ERROR;
        }
        option = trackOptionSpecs[index].optionName;
        if (strcmp(option, "-weight") == 0) {
            if (Tcl_GetDoubleFromObj(graphPtr->interp, objv[i + 1], &weight) != TCL_OK) {
                return TCL_ERROR;
            }
            if (!FINITE(weight) || weight <= 0.0 || weight > 1e6) {
                Tcl_SetObjResult(graphPtr->interp,
                                 Tcl_NewStringObj("track weight must be greater than 0 and at most 1000000", -1));
                return TCL_ERROR;
            }
        } else if (strcmp(option, "-minheight") == 0) {
            if (Tk_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, objv[i + 1], &minHeight) != TCL_OK) {
                return TCL_ERROR;
            }
            if (minHeight < 1) {
                Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("track minimum height must be positive", -1));
                return TCL_ERROR;
            }
        } else if (strcmp(option, "-height") == 0) {
            if (Tk_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, objv[i + 1], &height) != TCL_OK) {
                return TCL_ERROR;
            }
            if (height < 1 || height > 1000000) {
                Tcl_SetObjResult(graphPtr->interp,
                                 Tcl_NewStringObj("track height must be between 1 and 1000000 pixels", -1));
                return TCL_ERROR;
            }
        } else if (strcmp(option, "-barmode") == 0) {
            static const char *const modes[] = {"inherit", "normal", "stacked", "aligned", "overlap", "infront", NULL};
            int index;
            if (Tcl_GetIndexFromObj(graphPtr->interp, objv[i + 1], modes, "bar mode", 0, &index) != TCL_OK) {
                return TCL_ERROR;
            }
            barMode = index == 5 ? MODE_INFRONT : index - 1;
        } else if (strcmp(option, "-yaxes") == 0) {
            axesObj = objv[i + 1];
        } else {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("unknown track option \"%s\"", option));
            return TCL_ERROR;
        }
    }
    if (axesObj != NULL) {
        Tcl_Obj **names;
        if (Tcl_ListObjGetElements(graphPtr->interp, axesObj, &nAxes, &names) != TCL_OK) {
            return TCL_ERROR;
        }
        if ((size_t)nAxes > SIZE_MAX / sizeof(*axes)) {
            return TCL_ERROR;
        }
        axes = ckalloc(MAX(1, (size_t)nAxes * sizeof(*axes)));
        for (i = 0; i < nAxes; i++) {
            entry = Tcl_FindHashEntry(&graphPtr->axes.table, Tcl_GetString(names[i]));
            if (entry == NULL) {
                Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("unknown Y axis \"%s\"", Tcl_GetString(names[i])));
                goto error;
            }
            axes[i] = Tcl_GetHashValue(entry);
            if (Rbc_MarkersUseTrack(graphPtr, NULL, axes[i])) {
                Tcl_SetObjResult(graphPtr->interp,
                                 Tcl_NewStringObj("cannot assign a Y axis used by a graph-wide marker", -1));
                goto error;
            }
            if (axes[i]->deletePending || (axes[i]->track != NULL && axes[i]->track != track) ||
                (axes[i]->refCount > 0 && axes[i]->classUid != rbcYAxisUid) ||
                (axes[i]->chainPtr != NULL && axes[i]->chainPtr != graphPtr->axisChain[1])) {
                Tcl_SetObjResult(graphPtr->interp,
                                 Tcl_NewStringObj("axis is already assigned to another track or margin", -1));
                goto error;
            }
            for (j = 0; j < i; j++) {
                if (axes[j] == axes[i]) {
                    Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("duplicate track Y axis", -1));
                    goto error;
                }
            }
        }
        for (entry = Tcl_FirstHashEntry(&graphPtr->elements.table, &search); entry != NULL;
             entry = Tcl_NextHashEntry(&search)) {
            Element *element = Tcl_GetHashValue(entry);
            if (element->track == track) {
                for (i = 0; i < nAxes && axes[i] != element->axes.y; i++) {
                }
                if (i == nAxes) {
                    Tcl_SetObjResult(graphPtr->interp,
                                     Tcl_NewStringObj("cannot detach a Y axis used by a track element", -1));
                    goto error;
                }
            }
        }
        for (i = 0; i < track->nAxes; i++) {
            for (j = 0; j < nAxes && axes[j] != track->axes[i]; j++) {
            }
            if (j == nAxes && track->grid != NULL && track->grid->axes.y == track->axes[i]) {
                Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("cannot detach a Y axis used by a track grid", -1));
                goto error;
            }
            if (j == nAxes && Rbc_MarkersUseTrack(graphPtr, track, track->axes[i])) {
                Tcl_SetObjResult(graphPtr->interp,
                                 Tcl_NewStringObj("cannot detach a Y axis used by a track marker", -1));
                goto error;
            }
        }
        for (i = 0; i < nAxes; i++) {
            axes[i]->classUid = rbcYAxisUid;
            axes[i]->refCount++;
        }
        for (i = 0; i < track->nAxes; i++) {
            track->axes[i]->track = NULL;
            Rbc_FreeAxisReference(graphPtr, track->axes[i]);
        }
        if (track->axes != NULL) {
            ckfree(track->axes);
        }
        track->axes = axes;
        track->nAxes = nAxes;
        for (i = 0; i < nAxes; i++) {
            axes[i]->track = track;
        }
    }
    track->barMode = barMode;
    track->weight = weight;
    track->minHeight = minHeight;
    track->height = height;
    return TCL_OK;
error:
    ckfree(axes);
    return TCL_ERROR;
}

/* Read creation defaults through the same named component proxy as axes and
 * markers. Keep these as objects: ConfigureTrack performs conversion and axis
 * ownership validation together, before committing any of the effective values.
 * Explicit creation options replace database defaults before validation. */
static int InitTrackOptions(Graph *graphPtr, RbcTrack *track, Tcl_Size objc, Tcl_Obj *const objv[]) {
    TrackDefaults defaults = {0};
    Tk_OptionTable table = Tk_CreateOptionTable(graphPtr->interp, trackOptionSpecs);
    Tcl_Obj *options = Tcl_NewListObj(0, NULL);
    Tcl_Obj **values;
    Tcl_Size count, i, j;
    int result;

    Tcl_IncrRefCount(options);
    if (objc % 2) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("expected track option/value pairs", -1));
        result = TCL_ERROR;
        goto done;
    }
    for (i = 0; i < objc; i += 2) {
        int index;
        if (TrackOptionIndex(graphPtr->interp, objv[i], &index) != TCL_OK) {
            result = TCL_ERROR;
            goto done;
        }
    }
    result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, track->name, "Track",
                                      (char *)&defaults, table);
    if (result == TCL_OK) {
        for (i = 0; trackOptionSpecs[i].type != TK_OPTION_END; i++) {
            for (j = 0; j + 1 < objc; j += 2) {
                int index;
                TrackOptionIndex(graphPtr->interp, objv[j], &index);
                if (index == i) {
                    break;
                }
            }
            if (j + 1 >= objc) {
                Tcl_Obj *value = *(Tcl_Obj **)((char *)&defaults + trackOptionSpecs[i].objOffset);
                Tcl_ListObjAppendElement(NULL, options, Tcl_NewStringObj(trackOptionSpecs[i].optionName, -1));
                Tcl_ListObjAppendElement(NULL, options, value != NULL ? value : Tcl_NewObj());
            }
        }
        for (i = 0; i < objc; i++) {
            Tcl_ListObjAppendElement(NULL, options, objv[i]);
        }
        Tcl_ListObjGetElements(NULL, options, &count, &values);
        result = ConfigureTrack(graphPtr, track, count, values);
    }
done:
    Tk_FreeConfigOptions((char *)&defaults, table, graphPtr->tkwin);
    Tk_DeleteOptionTable(table);
    Tcl_DecrRefCount(options);
    return result;
}

/* Validate the complete permutation before modifying the linked list. Track
 * objects, axes, elements and markers retain their identity and ownership. */
static int OrderTracks(Graph *graphPtr, Tcl_Obj *namesObj) {
    Tcl_Obj **names;
    Tcl_Size count, n = 0, i, j;
    RbcTrack *track, **order;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        n++;
    }
    if (Tcl_ListObjGetElements(graphPtr->interp, namesObj, &count, &names) != TCL_OK) {
        return TCL_ERROR;
    }
    if (count != n) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("track order must list every track exactly once", -1));
        return TCL_ERROR;
    }
    if ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / sizeof(*order))) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("too many tracks", -1));
        return TCL_ERROR;
    }
    order = ckalloc(MAX((size_t)1, (size_t)count * sizeof(*order)));
    for (i = 0; i < count; i++) {
        order[i] = Rbc_FindTrack(graphPtr, Tcl_GetString(names[i]));
        if (order[i] == NULL) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("unknown track \"%s\"", Tcl_GetString(names[i])));
            ckfree(order);
            return TCL_ERROR;
        }
        for (j = 0; j < i; j++) {
            if (order[j] == order[i]) {
                Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("track order contains a duplicate track", -1));
                ckfree(order);
                return TCL_ERROR;
            }
        }
    }
    for (i = 0; i < count; i++) {
        order[i]->next = (i + 1 < count) ? order[i + 1] : NULL;
    }
    graphPtr->tracks = (count > 0) ? order[0] : NULL;
    ckfree(order);
    SyncTrackAxes(graphPtr);
    return TCL_OK;
}

/* Physical limits along the dimension divided between tracks. */
static int TrackStart(Graph *graphPtr, RbcTrack *track) {
    return graphPtr->inverted ? track->worldLeft : track->worldTop;
}

static int TrackEnd(Graph *graphPtr, RbcTrack *track) {
    return graphPtr->inverted ? track->worldRight : track->worldBottom;
}

/* Draw optional dividers at the same centres used by track sashpos. The
 * stroke is limited to the gap so compressed layouts cannot cover traces.
 * Export uses the same geometry through the renderer abstraction. */
void Rbc_DrawTrackSeparators(Graph *graphPtr, Drawable drawable, Rbc_ExportContext *exportPtr) {
    RbcTrack *track;
    if (!graphPtr->trackGraph || graphPtr->trackSeparatorColor == NULL) {
        return;
    }
    for (track = graphPtr->tracks; track != NULL && track->next != NULL; track = track->next) {
        int end = TrackEnd(graphPtr, track);
        int gap = TrackStart(graphPtr, track->next) - end;
        int width = MIN(graphPtr->trackSeparatorWidth, 2 * (gap / 2) - 1);
        int center = end + gap / 2;
        Segment2D segment;
        Rbc_RenderContext *ctx;
        if (width < 1 || center - width / 2 < (graphPtr->inverted ? graphPtr->left : graphPtr->top) ||
            center + width / 2 > (graphPtr->inverted ? graphPtr->right : graphPtr->bottom)) {
            continue;
        }
        if (graphPtr->inverted) {
            segment.p.x = segment.q.x = center;
            segment.p.y = graphPtr->top;
            segment.q.y = graphPtr->bottom;
        } else {
            segment.p.y = segment.q.y = center;
            segment.p.x = graphPtr->left;
            segment.q.x = graphPtr->right;
        }
        ctx = exportPtr != NULL
                  ? Rbc_RenderBeginExport(exportPtr, graphPtr->trackSeparatorColor, width, NULL, CapButt, JoinMiter)
                  : Rbc_RenderBegin(graphPtr, drawable, graphPtr->trackSeparatorColor, width, NULL, NULL);
        if (ctx != NULL) {
            Rbc_RenderSegments(ctx, &segment, 1);
            Rbc_RenderEnd(ctx);
        } else {
            XGCValues values;
            GC gc;
            values.foreground = graphPtr->trackSeparatorColor->pixel;
            values.line_width = width;
            values.cap_style = CapButt;
            gc = Tk_GetGC(graphPtr->tkwin, GCForeground | GCLineWidth | GCCapStyle, &values);
            ctx = Rbc_RenderBeginStroke(graphPtr, drawable, gc, RBC_RENDER_STROKE_SEGMENTS,
                                        graphPtr->trackSeparatorColor, width, NULL, NULL);
            if (ctx != NULL) {
                Rbc_RenderSegments(ctx, &segment, 1);
                Rbc_RenderEnd(ctx);
            }
            Tk_FreeGC(graphPtr->display, gc);
        }
    }
}

/* Move a separator by changing only adjacent weights, preserving their sum.
 * Integer rounding may move a boundary by one pixel. Compressed minima cannot resize. */
static int SetTrackSash(Graph *graphPtr, RbcTrack *track, int y) {
    RbcTrack *next = track->next;
    double pairHeight, extra, height, ratio, sum, first, second;
    int gap = TrackStart(graphPtr, next) - TrackEnd(graphPtr, track);
    if (y == TrackEnd(graphPtr, track) + gap / 2) {
        return TCL_OK;
    }
    if (graphPtr->trackScroll) {
        track->height = (int)CLAMP((double)y - gap / 2 - TrackStart(graphPtr, track), track->minHeight, 1000000);
        graphPtr->flags |= RESET_WORLD;
        Rbc_EventuallyRedrawGraph(graphPtr);
        return TCL_OK;
    }
    pairHeight = (double)(TrackEnd(graphPtr, track) - TrackStart(graphPtr, track)) + TrackEnd(graphPtr, next) -
                 TrackStart(graphPtr, next);
    extra = pairHeight - track->minHeight - next->minHeight;
    if (extra <= 0.0) {
        return TCL_OK;
    }
    height = (double)y - gap / 2 - TrackStart(graphPtr, track);
    ratio = (height - track->minHeight) / extra;
    sum = track->weight + next->weight;
    /* Keep weights positive and inside the same limits as track configure. */
    ratio = MAX(MAX(1e-9, 1.0 - 1e6 / sum), MIN(MIN(1.0 - 1e-9, 1e6 / sum), ratio));
    first = sum * ratio;
    second = sum - first;
    if (first <= 0.0 || second <= 0.0) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_NewStringObj("track weights are too small to resize this separator", -1));
        return TCL_ERROR;
    }
    track->weight = first;
    next->weight = second;
    graphPtr->flags |= RESET_WORLD;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/* Fractions describe the list viewport, independently of all data-axis limits. */
static void TrackViewFractions(Graph *graphPtr, double *first, double *last) {
    double range = MAX(1, graphPtr->inverted ? graphPtr->hRange : graphPtr->vRange);
    double total = MAX(range, graphPtr->trackExtent);
    double offset = graphPtr->trackScroll ? CLAMP(graphPtr->trackOffset, 0.0, total - range) : 0.0;
    *first = offset / total;
    *last = (offset + range) / total;
}

static int TrackViewOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    double first, last, value, offset;
    double range;
    Rbc_ScrollRequest request;
    if (objc > 3 && Rbc_ParseScrollRequest(interp, objc, objv, 3, FALSE, &request) != TCL_OK) {
        return TCL_ERROR;
    }
    Rbc_LayoutGraph(graphPtr);
    TrackViewFractions(graphPtr, &first, &last);
    if (objc == 3) {
        Tcl_Obj *values[2] = {Tcl_NewDoubleObj(first), Tcl_NewDoubleObj(last)};
        Tcl_SetObjResult(interp, Tcl_NewListObj(2, values));
        return TCL_OK;
    }
    offset = graphPtr->trackOffset;
    range = MAX(1, graphPtr->inverted ? graphPtr->hRange : graphPtr->vRange);
    value = request.value;
    if (request.kind == RBC_SCROLL_MOVETO) {
        offset = value * MAX(range, graphPtr->trackExtent);
    } else if (request.kind == RBC_SCROLL_PAGES) {
        offset += value * range * 0.9;
    } else {
        offset += value * 20.0;
    }
    if (!FINITE(value) || !FINITE(offset)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("track scroll request must be finite", -1));
        return TCL_ERROR;
    }
    graphPtr->trackOffset = graphPtr->trackScroll ? CLAMP(offset, 0.0, MAX(0.0, graphPtr->trackExtent - range)) : 0.0;
    graphPtr->flags |= RESET_WORLD;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/* Run the scrollbar prefix only after drawing, with no subsequent graph access.
 * Cache before evaluation so a callback-triggered redraw does not notify forever. */
static void NotifyTrackScrollbar(Graph *graphPtr) {
    Tcl_Obj *command;
    Tcl_Interp *interp = graphPtr->interp;
    Tcl_InterpState state;
    double first, last;
    Tcl_Size length;
    int code;
    if (!graphPtr->trackGraph || (graphPtr->flags & GRAPH_EXPORT) || graphPtr->trackScrollCmdObj == NULL) {
        return;
    }
    TrackViewFractions(graphPtr, &first, &last);
    if (first == graphPtr->trackFirst && last == graphPtr->trackLast) {
        return;
    }
    graphPtr->trackFirst = first;
    graphPtr->trackLast = last;
    Tcl_ListObjLength(NULL, graphPtr->trackScrollCmdObj, &length);
    if (length == 0) {
        return;
    }
    command = Tcl_DuplicateObj(graphPtr->trackScrollCmdObj);
    Tcl_IncrRefCount(command);
    Tcl_ListObjAppendElement(NULL, command, Tcl_NewDoubleObj(first));
    Tcl_ListObjAppendElement(NULL, command, Tcl_NewDoubleObj(last));
    Tcl_Preserve(interp);
    Tcl_Preserve(graphPtr);
    state = Tcl_SaveInterpState(interp, TCL_OK);
    code = Tcl_EvalObjEx(interp, command, TCL_EVAL_GLOBAL);
    if (code != TCL_OK) {
        Tcl_BackgroundException(interp, code);
    }
    Tcl_RestoreInterpState(interp, state);
    Tcl_DecrRefCount(command);
    Tcl_Release(graphPtr);
    Tcl_Release(interp);
}

/* Repick stationary pointers after native layout changes, never during export.
 * Enter/Leave scripts may delete tracks or the widget, so preserve the graph
 * across both notifications and do not use it after releasing it. */
void Rbc_NotifyTrackView(Graph *graphPtr) {
    if (!graphPtr->trackGraph || (graphPtr->flags & GRAPH_EXPORT)) {
        return;
    }
    Tcl_Preserve(graphPtr);
    if (graphPtr->tkwin != NULL && graphPtr->bindTable != NULL) {
        Rbc_PickCurrentItem(graphPtr->bindTable);
    }
    if (graphPtr->tkwin != NULL) {
        NotifyTrackScrollbar(graphPtr);
    }
    Tcl_Release(graphPtr);
}

/* Validate the whole batch before unlinking anything. Repeated names are
 * harmless; missing or occupied tracks leave the complete batch unchanged. */
static int DeleteTracks(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    RbcTrack **tracks, **slot;
    Tcl_Size i, j, count = 0;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    if (objc == 0) {
        return TCL_OK;
    }
    tracks = ckalloc((size_t)objc * sizeof(*tracks));
    for (i = 0; i < objc; i++) {
        RbcTrack *track = Rbc_FindTrack(graphPtr, Tcl_GetString(objv[i]));
        if (track == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown track \"%s\"", Tcl_GetString(objv[i])));
            goto error;
        }
        for (j = 0; j < count && tracks[j] != track; j++) {
        }
        if (j < count) {
            continue;
        }
        if (Rbc_MarkersUseTrack(graphPtr, track, NULL)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("delete or move track markers before deleting their track", -1));
            goto error;
        }
        for (entry = Tcl_FirstHashEntry(&graphPtr->elements.table, &search); entry != NULL;
             entry = Tcl_NextHashEntry(&search)) {
            Element *element = Tcl_GetHashValue(entry);
            if (element->track == track) {
                Tcl_SetObjResult(interp,
                                 Tcl_NewStringObj("delete or move track elements before deleting their track", -1));
                goto error;
            }
        }
        tracks[count++] = track;
    }
    for (i = 0; i < count; i++) {
        for (slot = &graphPtr->tracks; *slot != tracks[i]; slot = &(*slot)->next) {
        }
        *slot = tracks[i]->next;
        FreeTrack(graphPtr, tracks[i]);
    }
    ckfree(tracks);
    SyncTrackAxes(graphPtr);
    return TCL_OK;
error:
    ckfree(tracks);
    return TCL_ERROR;
}

int Rbc_TrackOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const Rbc_OpSpecHeader operations[] = {
        {"create", 4, 0, "name ?option value ...?"},
        {"configure", 4, 0, "name ?option ?value ...??"},
        {"cget", 5, 5, "name option"},
        {"delete", 3, 0, "?name ...?"},
        {"names", 3, 0, "?pattern ...?"},
        {"bbox", 4, 4, "name"},
        {"identify", 4, 4, "coordinate"},
        {"transform", 6, 10, "name x y ?-mapx axis? ?-mapy axis?"},
        {"invtransform", 6, 10, "name x y ?-mapx axis? ?-mapy axis?"},
        {"order", 3, 4, "?nameList?"},
        {"sashpos", 4, 5, "name ?coordinate?"},
        {"separator", 5, 5, "x y"},
        {"legend", 5, 0, "name operation ?arg ...?"},
        {"grid", 5, 0, "name operation ?arg ...?"},
        {"view", 3, 6, "?moveto fraction|scroll number units|pages?"},
        {"yview", 4, 9, "name ?-mapy axis? ?moveto fraction|scroll number units|pages?"},
        {"exists", 4, 4, "name"},
        {"bind", 4, 6, "name|all ?sequence ?command??"},
        {"get", 4, 4, "current"},
        {NULL, 0, 0, NULL}};
    enum {
        TRACK_OP_CREATE,
        TRACK_OP_CONFIGURE,
        TRACK_OP_CGET,
        TRACK_OP_DELETE,
        TRACK_OP_NAMES,
        TRACK_OP_BBOX,
        TRACK_OP_IDENTIFY,
        TRACK_OP_TRANSFORM,
        TRACK_OP_INVTRANSFORM,
        TRACK_OP_ORDER,
        TRACK_OP_SASHPOS,
        TRACK_OP_SEPARATOR,
        TRACK_OP_LEGEND,
        TRACK_OP_GRID,
        TRACK_OP_VIEW,
        TRACK_OP_YVIEW,
        TRACK_OP_EXISTS,
        TRACK_OP_BIND,
        TRACK_OP_GET
    };
    RbcTrack *track, **slot;
    int operation;
    Tcl_Obj *result;
    if (!graphPtr->trackGraph) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("track operations require a trackgraph widget", -1));
        return TCL_ERROR;
    }
    if (Rbc_GetOpIndexFromObj(interp, operations, sizeof(operations[0]), RBC_OP_ARG2, objc, objv, &operation) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    if (operation == TRACK_OP_GET) {
        if (strcmp(Tcl_GetString(objv[3]), "current") != 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("expected current", -1));
            return TCL_ERROR;
        }
        if (PTR2INT(Rbc_GetCurrentContext(graphPtr->bindTable)) == GRAPH_BIND_CONTEXT_TRACK) {
            track = Rbc_GetCurrentItem(graphPtr->bindTable);
            if (track != NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(track->name, -1));
            }
        }
        return TCL_OK;
    }
    if (operation == TRACK_OP_BIND) {
        ClientData target;
        const char *name = Tcl_GetString(objv[3]);
        if (strcmp(name, "all") == 0) {
            target = &graphPtr->tracks;
        } else {
            target = Rbc_FindTrack(graphPtr, name);
            if (target == NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown track \"%s\"", name));
                return TCL_ERROR;
            }
        }
        return Rbc_ConfigureBindingsFromObj(interp, graphPtr->bindTable, target, objc - 4, objv + 4);
    }
    if (operation == TRACK_OP_EXISTS) {
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(Rbc_FindTrack(graphPtr, Tcl_GetString(objv[3])) != NULL));
        return TCL_OK;
    }
    if (operation == TRACK_OP_DELETE) {
        return DeleteTracks(graphPtr, interp, objc - 3, objv + 3);
    }
    if (operation == TRACK_OP_VIEW) {
        return TrackViewOp(graphPtr, interp, objc, objv);
    }
    if (operation == TRACK_OP_ORDER && objc == 4) {
        if (OrderTracks(graphPtr, objv[3]) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (operation == TRACK_OP_NAMES || operation == TRACK_OP_ORDER) {
        result = Tcl_NewListObj(0, NULL);
        for (track = graphPtr->tracks; track != NULL; track = track->next) {
            Tcl_Size i;
            if (operation == TRACK_OP_NAMES && objc > 3) {
                for (i = 3; i < objc; i++) {
                    if (Tcl_StringMatch(track->name, Tcl_GetString(objv[i]))) {
                        break;
                    }
                }
                if (i == objc) {
                    continue;
                }
            }
            Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj(track->name, -1));
        }
        Tcl_SetObjResult(interp, result);
        return TCL_OK;
    }
    if (operation == TRACK_OP_SEPARATOR) {
        int x, y;
        if (objc != 5) {
            Tcl_WrongNumArgs(interp, 3, objv, "x y");
            return TCL_ERROR;
        }
        if (Tcl_GetIntFromObj(interp, objv[3], &x) != TCL_OK || Tcl_GetIntFromObj(interp, objv[4], &y) != TCL_OK) {
            return TCL_ERROR;
        }
        Rbc_LayoutGraph(graphPtr);
        if (graphPtr->inverted) {
            int tmp = x;
            x = y;
            y = tmp;
        }
        if (x >= (graphPtr->inverted ? graphPtr->top : graphPtr->left) &&
            x <= (graphPtr->inverted ? graphPtr->bottom : graphPtr->right)) {
            for (track = graphPtr->tracks; track != NULL && track->next != NULL; track = track->next) {
                if (y >= (graphPtr->inverted ? graphPtr->left : graphPtr->top) &&
                    y <= (graphPtr->inverted ? graphPtr->right : graphPtr->bottom) && y > TrackEnd(graphPtr, track) &&
                    y < TrackStart(graphPtr, track->next)) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj(track->name, -1));
                    return TCL_OK;
                }
            }
        }
        Tcl_ResetResult(interp);
        return TCL_OK;
    }
    if (operation == TRACK_OP_IDENTIFY && objc == 4) {
        int y;
        if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[3], &y) != TCL_OK) {
            return TCL_ERROR;
        }
        Rbc_LayoutGraph(graphPtr);
        for (track = graphPtr->tracks; track != NULL; track = track->next) {
            if (y >= (graphPtr->inverted ? graphPtr->left : graphPtr->top) &&
                y <= (graphPtr->inverted ? graphPtr->right : graphPtr->bottom) && y >= TrackStart(graphPtr, track) &&
                y <= TrackEnd(graphPtr, track) && TrackEnd(graphPtr, track) > TrackStart(graphPtr, track)) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj(track->name, -1));
                return TCL_OK;
            }
        }
        Tcl_ResetResult(interp);
        return TCL_OK;
    }
    if (objc < 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "operation name ?options?");
        return TCL_ERROR;
    }
    track = Rbc_FindTrack(graphPtr, Tcl_GetString(objv[3]));
    if (operation == TRACK_OP_CREATE) {
        const char *name = Tcl_GetString(objv[3]);
        if (track != NULL || name[0] == '\0' || name[0] == '-') {
            Tcl_SetObjResult(interp,
                             Tcl_NewStringObj("track name must be nonempty, unique and not start with '-'", -1));
            return TCL_ERROR;
        }
        track = ckalloc(sizeof(*track));
        memset(track, 0, sizeof(*track));
        track->name = ckalloc(strlen(name) + 1);
        strcpy(track->name, name);
        if (InitTrackOptions(graphPtr, track, objc - 4, objv + 4) != TCL_OK ||
            Rbc_CreateTrackLegend(graphPtr, track) != TCL_OK) {
            FreeTrack(graphPtr, track);
            return TCL_ERROR;
        }
        for (slot = &graphPtr->tracks; *slot != NULL; slot = &(*slot)->next) {
        }
        *slot = track;
        SyncTrackAxes(graphPtr);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(name, -1));
        return TCL_OK;
    }
    if (track == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown track \"%s\"", Tcl_GetString(objv[3])));
        return TCL_ERROR;
    }
    if (operation == TRACK_OP_YVIEW) {
        return Rbc_TrackYViewOp(graphPtr, track, interp, objc, objv);
    }
    if (operation == TRACK_OP_GRID) {
        return Rbc_TrackGridOp(graphPtr, track, interp, objc, objv);
    }
    if (operation == TRACK_OP_LEGEND) {
        return Rbc_TrackLegendOp(graphPtr, track, interp, objc, objv);
    }
    if (operation == TRACK_OP_SASHPOS) {
        int y;
        if (objc != 4 && objc != 5) {
            Tcl_WrongNumArgs(interp, 3, objv, "name ?y?");
            return TCL_ERROR;
        }
        if (track->next == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("last track has no separator", -1));
            return TCL_ERROR;
        }
        if (objc == 5 && Tcl_GetIntFromObj(interp, objv[4], &y) != TCL_OK) {
            return TCL_ERROR;
        }
        Rbc_LayoutGraph(graphPtr);
        if (objc == 5) {
            if (SetTrackSash(graphPtr, track, y) != TCL_OK) {
                return TCL_ERROR;
            }
            Rbc_LayoutGraph(graphPtr);
        }
        Tcl_SetObjResult(interp, Tcl_NewIntObj(TrackEnd(graphPtr, track) +
                                               (TrackStart(graphPtr, track->next) - TrackEnd(graphPtr, track)) / 2));
        return TCL_OK;
    }
    if (operation == TRACK_OP_TRANSFORM || operation == TRACK_OP_INVTRANSFORM) {
        Axis2D axes = {NULL, NULL};
        Tcl_Obj *xName, *yName;
        Point2D point;
        Tcl_Size i;
        double x, y;
        int code = TCL_ERROR;
        if (objc < 6 || (objc - 6) % 2) {
            Tcl_WrongNumArgs(interp, 2, objv, "transform|invtransform track x y ?-mapx axis? ?-mapy axis?");
            return TCL_ERROR;
        }
        if (track->nAxes == 0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("track has no Y axis", -1));
            return TCL_ERROR;
        }
        xName = Tcl_NewStringObj("x", -1);
        yName = Tcl_NewStringObj(track->axes[0]->name, -1);
        Tcl_IncrRefCount(xName);
        Tcl_IncrRefCount(yName);
        for (i = 6; i < objc; i += 2) {
            const char *option = Tcl_GetString(objv[i]);
            Tcl_Obj **namePtr;
            if (strcmp(option, "-mapx") == 0) {
                namePtr = &xName;
            } else if (strcmp(option, "-mapy") == 0) {
                namePtr = &yName;
            } else {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("expected -mapx or -mapy", -1));
                goto transformDone;
            }
            Tcl_DecrRefCount(*namePtr);
            *namePtr = objv[i + 1];
            Tcl_IncrRefCount(*namePtr);
        }
        if (Tcl_GetDoubleFromObj(interp, objv[4], &x) != TCL_OK ||
            Tcl_GetDoubleFromObj(interp, objv[5], &y) != TCL_OK ||
            Rbc_GetAxisFromObj(graphPtr, xName, rbcXAxisUid, FALSE, &axes.x) != TCL_OK ||
            Rbc_GetAxisFromObj(graphPtr, yName, rbcYAxisUid, FALSE, &axes.y) != TCL_OK) {
            goto transformDone;
        }
        if (!FINITE(x) || !FINITE(y) || axes.y->track != track) {
            Tcl_SetObjResult(interp,
                             Tcl_NewStringObj("expected finite coordinates and a Y axis belonging to the track", -1));
            goto transformDone;
        }
        Rbc_LayoutGraph(graphPtr);
        point =
            (operation == TRACK_OP_TRANSFORM) ? Rbc_Map2D(graphPtr, x, y, &axes) : Rbc_InvMap2D(graphPtr, x, y, &axes);
        if (!FINITE(point.x) || !FINITE(point.y)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("coordinates cannot be mapped by the selected axes", -1));
            goto transformDone;
        }
        result = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(interp, result, Tcl_NewDoubleObj(point.x));
        Tcl_ListObjAppendElement(interp, result, Tcl_NewDoubleObj(point.y));
        Tcl_SetObjResult(interp, result);
        code = TCL_OK;
    transformDone:
        Rbc_FreeAxisReference(graphPtr, axes.x);
        Rbc_FreeAxisReference(graphPtr, axes.y);
        Tcl_DecrRefCount(xName);
        Tcl_DecrRefCount(yName);
        return code;
    }
    if ((operation == TRACK_OP_CONFIGURE && objc == 5) || (operation == TRACK_OP_CGET && objc == 5)) {
        int index;
        if (TrackOptionIndex(interp, objv[4], &index) != TCL_OK) {
            return TCL_ERROR;
        }
        result = operation == TRACK_OP_CGET ? TrackOption(track, trackOptionSpecs[index].optionName)
                                            : TrackOptionInfo(track, index);
        Tcl_SetObjResult(interp, result);
        return TCL_OK;
    }
    if (operation == TRACK_OP_CONFIGURE) {
        if (objc == 4) {
            int i;
            result = Tcl_NewListObj(0, NULL);
            for (i = 0; trackOptionSpecs[i].type != TK_OPTION_END; i++) {
                Tcl_ListObjAppendElement(interp, result, TrackOptionInfo(track, i));
            }
            Tcl_SetObjResult(interp, result);
            return TCL_OK;
        }
        if (ConfigureTrack(graphPtr, track, objc - 4, objv + 4) != TCL_OK) {
            return TCL_ERROR;
        }
        SyncTrackAxes(graphPtr);
        return TCL_OK;
    }
    if (operation == TRACK_OP_BBOX && objc == 4) {
        int bounds[4], i;
        Rbc_LayoutGraph(graphPtr);
        bounds[0] = track->left;
        bounds[1] = track->top;
        bounds[2] = track->right;
        bounds[3] = track->bottom;
        result = Tcl_NewListObj(0, NULL);
        for (i = 0; i < 4; i++) {
            Tcl_ListObjAppendElement(interp, result, Tcl_NewIntObj(bounds[i]));
        }
        Tcl_SetObjResult(interp, result);
        return TCL_OK;
    }
    Tcl_WrongNumArgs(interp, 2, objv, "operation name ?options?");
    return TCL_ERROR;
}

/* Release track-owned components and axis references before destroying axes. */
void Rbc_DestroyTracks(Graph *graphPtr) {
    while (graphPtr->tracks != NULL) {
        RbcTrack *track = graphPtr->tracks;
        graphPtr->tracks = track->next;
        FreeTrack(graphPtr, track);
    }
}
