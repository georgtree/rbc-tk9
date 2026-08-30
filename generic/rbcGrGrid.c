/*
 * rbcGrGrid.c --
 *
 *      This module implements grid lines for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"

#define DEF_GRID_DASHES "dot"
#define DEF_GRID_FOREGROUND RGB_GREY64
#define DEF_GRID_FG_MONO RGB_BLACK
#define DEF_GRID_LINE_WIDTH "0"
#define DEF_GRID_HIDE_BARCHART "no"
#define DEF_GRID_HIDE_GRAPH "yes"
#define DEF_GRID_MINOR "yes"
#define DEF_GRID_MAP_X_GRAPH "x"
#define DEF_GRID_MAP_X_BARCHART (char *)NULL
#define DEF_GRID_MAP_Y "y"
#define GRID_REDRAW (1U << 0)
#define GRID_GC_CHANGED (1U << 1)
#define GRID_AXES_CHANGED (1U << 2)
#define GRID_INITIALIZE_MASK (GRID_GC_CHANGED | GRID_AXES_CHANGED)

static const Tk_OptionSpec graphGridOptionSpecs[] = {
    {TK_OPTION_COLOR, "-color", "color", "Color", DEF_GRID_FOREGROUND, -1, offsetof(Grid, colorPtr), 0,
     DEF_GRID_FG_MONO, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", DEF_GRID_DASHES, offsetof(Grid, dashesObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_GRID_HIDE_GRAPH, -1, offsetof(Grid, hidden), 0, NULL,
     GRID_REDRAW},
    {TK_OPTION_PIXELS, "-linewidth", "lineWidth", "Linewidth", DEF_GRID_LINE_WIDTH, offsetof(Grid, lineWidthObjPtr),
     -1, 0, NULL, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_GRID_MAP_X_GRAPH, offsetof(Grid, mapXObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_GRID_MAP_Y, offsetof(Grid, mapYObjPtr), -1, TK_OPTION_NULL_OK,
     NULL, GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_BOOLEAN, "-minor", "minor", "Minor", DEF_GRID_MINOR, -1, offsetof(Grid, minorGrid), 0, NULL,
     GRID_REDRAW},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

static const Tk_OptionSpec barGridOptionSpecs[] = {
    {TK_OPTION_COLOR, "-color", "color", "Color", DEF_GRID_FOREGROUND, -1, offsetof(Grid, colorPtr), 0,
     DEF_GRID_FG_MONO, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", DEF_GRID_DASHES, offsetof(Grid, dashesObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_GRID_HIDE_BARCHART, -1, offsetof(Grid, hidden), 0, NULL,
     GRID_REDRAW},
    {TK_OPTION_PIXELS, "-linewidth", "lineWidth", "Linewidth", DEF_GRID_LINE_WIDTH, offsetof(Grid, lineWidthObjPtr),
     -1, 0, NULL, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_GRID_MAP_X_BARCHART, offsetof(Grid, mapXObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_GRID_MAP_Y, offsetof(Grid, mapYObjPtr), -1, TK_OPTION_NULL_OK,
     NULL, GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_BOOLEAN, "-minor", "minor", "Minor", DEF_GRID_MINOR, -1, offsetof(Grid, minorGrid), 0, NULL,
     GRID_REDRAW},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

static int ConfigureGrid(Graph *graphPtr, Grid *gridPtr, int mask);

typedef int RbcGrGridOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcGrGridOp *proc;
} GridOpSpec;
static RbcGrGridOp CgetOp;
static RbcGrGridOp ConfigureOp;
static RbcGrGridOp MapOp;
static RbcGrGridOp UnmapOp;
static RbcGrGridOp ToggleOp;

/*
 *----------------------------------------------------------------------
 *
 * ConfigureGrid --
 *
 *      Configures attributes of the grid such as line width,
 *      dashes, and position.  The grid are first turned off
 *      before any of the attributes changes.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Grid *gridPtr
 *
 * Results:
 *      TCL_OK if the grid was configured successfully.
 *      TCL_ERROR if a retained value or axis mapping is invalid.
 *
 * Side Effects:
 *      May replace the grid GC and the retained X and Y axis references.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureGrid(Graph *graphPtr, Grid *gridPtr, int mask) {
    Rbc_Dashes newDashes;
    int newLineWidth;
    Axis *newXAxis;
    Axis *newYAxis;
    Axis *oldXAxis;
    Axis *oldYAxis;
    XGCValues gcValues;
    unsigned long gcMask;
    GC newGC;

    newDashes = gridPtr->dashes;
    newLineWidth = gridPtr->lineWidth;
    newXAxis = NULL;
    newYAxis = NULL;
    newGC = NULL;
    /*
     * Parse all values into temporary storage first. Nothing in the
     * active rendering state is modified until all validation succeeds.
     */
    if (mask & GRID_GC_CHANGED) {
        if (Rbc_GetDashesFromObj(graphPtr->interp, gridPtr->dashesObjPtr, &newDashes) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, gridPtr->lineWidthObjPtr, PIXELS_NONNEGATIVE,
                                 &newLineWidth) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (mask & GRID_AXES_CHANGED) {
        if (Rbc_GetAxisFromObj(graphPtr, gridPtr->mapXObjPtr, rbcXAxisUid, TRUE, &newXAxis) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetAxisFromObj(graphPtr, gridPtr->mapYObjPtr, rbcYAxisUid, TRUE, &newYAxis) != TCL_OK) {
            Rbc_FreeAxisReference(graphPtr, newXAxis);
            return TCL_ERROR;
        }
    }

    /*
     * Allocate the replacement GC only after every fallible conversion
     * and axis lookup has succeeded.
     */
    if (mask & GRID_GC_CHANGED) {
        gcValues.background = gcValues.foreground = gridPtr->colorPtr->pixel;
        gcValues.line_width = LineWidth(newLineWidth);
        gcMask = GCForeground | GCBackground | GCLineWidth;
        if (LineIsDashed(newDashes)) {
            gcValues.line_style = LineOnOffDash;
            gcMask |= GCLineStyle;
        }
        newGC = Rbc_GetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
        if (LineIsDashed(newDashes)) {
            Rbc_SetDashes(graphPtr->display, newGC, &newDashes);
        }
    }
    /*
     * Commit the new axis references.
     */
    if (mask & GRID_AXES_CHANGED) {
        oldXAxis = gridPtr->axes.x;
        oldYAxis = gridPtr->axes.y;
        gridPtr->axes.x = newXAxis;
        gridPtr->axes.y = newYAxis;
        Rbc_FreeAxisReference(graphPtr, oldXAxis);
        Rbc_FreeAxisReference(graphPtr, oldYAxis);
    }

    /*
     * Commit the new drawing state.
     */
    if (mask & GRID_GC_CHANGED) {
        GC oldGC;
        oldGC = gridPtr->gc;
        gridPtr->dashes = newDashes;
        gridPtr->lineWidth = newLineWidth;
        gridPtr->gc = newGC;
        if (oldGC != NULL) {
            Rbc_FreePrivateGC(graphPtr->display, oldGC);
        }
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MapGrid --
 *
 *      Determines the coordinates of the line segments corresponding
 *      to the grid lines for each axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_MapGrid(Graph *graphPtr) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    Tcl_Size nSegments;
    Segment2D *segments;

    if (gridPtr->x.segments != NULL) {
        ckfree((char *)gridPtr->x.segments);
        gridPtr->x.segments = NULL;
    }
    if (gridPtr->y.segments != NULL) {
        ckfree((char *)gridPtr->y.segments);
        gridPtr->y.segments = NULL;
    }
    gridPtr->x.nSegments = gridPtr->y.nSegments = 0;
    if (graphPtr->classUid == rbcPolarElementUid) {
        switch (graphPtr->representation) {
        case POLAR_REPRESENTATION_POLAR:
            Rbc_MapPolarGrid(graphPtr, gridPtr);
            break;
        case POLAR_REPRESENTATION_SMITH:
            Rbc_MapSmithGrid(graphPtr, gridPtr);
            break;
        }
        return;
    }
    /*
     * Generate line segments to represent the grid.  Line segments
     * are calculated from the major tick intervals of each axis mapped.
     */
    Rbc_GetAxisSegments(graphPtr, gridPtr->axes.x, &segments, &nSegments);
    if (nSegments > 0) {
        gridPtr->x.nSegments = nSegments;
        gridPtr->x.segments = segments;
    }
    Rbc_GetAxisSegments(graphPtr, gridPtr->axes.y, &segments, &nSegments);
    if (nSegments > 0) {
        gridPtr->y.nSegments = nSegments;
        gridPtr->y.segments = segments;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DrawGrid --
 *
 *      Draws the grid lines associated with each axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_DrawGrid(Graph *graphPtr, Drawable drawable) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;

    if (gridPtr->hidden) {
        return;
    }
    if (gridPtr->x.nSegments > 0) {
        Rbc_Draw2DSegments(graphPtr->display, drawable, gridPtr->gc, gridPtr->x.segments, gridPtr->x.nSegments);
    }
    if (gridPtr->y.nSegments > 0) {
        Rbc_Draw2DSegments(graphPtr->display, drawable, gridPtr->gc, gridPtr->y.segments, gridPtr->y.nSegments);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GridToPostScript --
 *
 *      Prints the grid lines associated with each axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_GridToPostScript(Graph *graphPtr, PsToken psToken) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;

    if (gridPtr->hidden) {
        return;
    }
    Rbc_LineAttributesToPostScript(psToken, gridPtr->colorPtr, gridPtr->lineWidth, &(gridPtr->dashes), CapButt,
                                   JoinMiter);
    if (gridPtr->x.nSegments > 0) {
        Rbc_2DSegmentsToPostScript(psToken, gridPtr->x.segments, gridPtr->x.nSegments);
    }
    if (gridPtr->y.nSegments > 0) {
        Rbc_2DSegmentsToPostScript(psToken, gridPtr->y.segments, gridPtr->y.nSegments);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DestroyGrid --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Grid GC is released.
 *
 *----------------------------------------------------------------------
 */
void Rbc_DestroyGrid(Graph *graphPtr) {
    Grid *gridPtr;

    gridPtr = (Grid *)graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    /*
     * Prevent accidental re-entry and tell DestroyGraph that this
     * component has already been released.
     */
    graphPtr->gridPtr = NULL;
    Rbc_FreeAxisReference(graphPtr, gridPtr->axes.x);
    Rbc_FreeAxisReference(graphPtr, gridPtr->axes.y);
    gridPtr->axes.x = NULL;
    gridPtr->axes.y = NULL;
    /*
     * This must run while graphPtr->tkwin is still valid.
     */
    Tk_FreeConfigOptions((char *)gridPtr, gridPtr->optionTable, graphPtr->tkwin);
    if (gridPtr->gc != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, gridPtr->gc);
    }
    if (gridPtr->x.segments != NULL) {
        ckfree((char *)gridPtr->x.segments);
    }
    if (gridPtr->y.segments != NULL) {
        ckfree((char *)gridPtr->y.segments);
    }
    ckfree((char *)gridPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CreateGrid --
 *
 *      Creates and initializes a new grid structure.
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      Returns TCL_ERROR if the configuration failed, otherwise TCL_OK.
 *
 * Side Effects:
 *      Memory for grid structure is allocated.
 *
 *----------------------------------------------------------------------
 */
int Rbc_CreateGrid(Graph *graphPtr) {
    Grid *gridPtr;
    const Tk_OptionSpec *specsPtr;

    gridPtr = RbcCalloc(1, sizeof(Grid));
    assert(gridPtr != NULL);
    graphPtr->gridPtr = gridPtr;
    if (graphPtr->classUid == rbcBarElementUid) {
        specsPtr = barGridOptionSpecs;
    } else {
        specsPtr = graphGridOptionSpecs;
    }
    /*
     * Tk creates/caches an interpreter-specific table from this
     * static template.
     */
    gridPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, specsPtr);
    if (Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, "grid", "Grid", (char *)gridPtr,
                                 gridPtr->optionTable) != TCL_OK) {
        goto error;
    }
    if (ConfigureGrid(graphPtr, gridPtr, GRID_INITIALIZE_MASK) != TCL_OK) {
        goto error;
    }
    return TCL_OK;

error:
    Rbc_DestroyGrid(graphPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries configuration attributes of the grid such as line
 *      width, dashes, and position.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr;
    Tcl_Obj *resultObjPtr;

    gridPtr = (Grid *)graphPtr->gridPtr;
    resultObjPtr = Tk_GetOptionValue(interp, (char *)gridPtr, gridPtr->optionTable, objv[3], graphPtr->tkwin);
    if (resultObjPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, resultObjPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Queries or resets configuration attributes of the grid
 *      such as line width, dashes, and position.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Grid attributes are reset.  The graph is redrawn at the
 *      next idle point.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr;
    Tcl_Obj *resultObjPtr;
    Tk_SavedOptions savedOptions;
    int mask;

    gridPtr = (Grid *)graphPtr->gridPtr;
    if (objc == 3) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)gridPtr, gridPtr->optionTable, NULL, graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (objc == 4) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)gridPtr, gridPtr->optionTable, objv[3], graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (Tk_SetOptions(interp, (char *)gridPtr, gridPtr->optionTable, objc - 3, objv + 3, graphPtr->tkwin, &savedOptions,
                      &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    if (ConfigureGrid(graphPtr, gridPtr, mask) != TCL_OK) {
        Tcl_Obj *errorObjPtr;
        /*
         * Preserve the error from post-configuration validation while
         * restoring the option values.
         */
        errorObjPtr = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(errorObjPtr);
        Tk_RestoreSavedOptions(&savedOptions);
        Tcl_SetObjResult(interp, errorObjPtr);
        Tcl_DecrRefCount(errorObjPtr);
        return TCL_ERROR;
    }
    Tk_FreeSavedOptions(&savedOptions);
    if (mask & GRID_REDRAW) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MapOp --
 *
 *      Maps the grid.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Grid attributes are reset and the graph is redrawn if necessary.
 *
 *----------------------------------------------------------------------
 */
static int MapOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    if (gridPtr->hidden) {
        gridPtr->hidden = FALSE; /* Changes "-hide" configuration option */
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * MapOp --
 *
 *      Maps or unmaps the grid (off or on).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Grid attributes are reset and the graph is redrawn if necessary.
 *
 *----------------------------------------------------------------------
 */
static int UnmapOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;

    if (!gridPtr->hidden) {
        gridPtr->hidden = TRUE; /* Changes "-hide" configuration option */
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ToggleOp --
 *
 *      Toggles the state of the grid shown/hidden.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Grid is hidden/displayed. The graph is redrawn at the next
 *      idle time.
 *
 *----------------------------------------------------------------------
 */
static int ToggleOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;

    gridPtr->hidden = (!gridPtr->hidden);
    graphPtr->flags |= REDRAW_BACKING_STORE;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

static const GridOpSpec gridOps[] = {{{"cget", 4, 4, "option"}, CgetOp},
                                     {{"configure", 3, 0, "?options...?"}, ConfigureOp},
                                     {{"off", 3, 3, ""}, UnmapOp},
                                     {{"on", 3, 3, ""}, MapOp},
                                     {{"toggle", 3, 3, ""}, ToggleOp},
                                     {{NULL, 0, 0, NULL}, NULL}};

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GridOp --
 *
 *      User routine to configure grid lines.  Grids are drawn
 *      at major tick intervals across the graph.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Grid may be drawn in the plotting area.
 *
 *----------------------------------------------------------------------
 */
int Rbc_GridOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, gridOps, (Tcl_Size)sizeof(gridOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }

    return gridOps[index].proc(graphPtr, interp, objc, objv);
}
