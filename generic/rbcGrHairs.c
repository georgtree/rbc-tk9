/*
 * rbcGrHairs.c --
 *
 *      This module implements crosshairs for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
*/

#include "rbcGraph.h"

extern Tk_CustomOption rbcPointOption;
extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcDashesOption;

/*
 * -------------------------------------------------------------------
 *
 * Crosshairs --
 *
 *      Contains the line segments positions and graphics context used
 *      to simulate crosshairs (by XORing) on the graph.
 *
 * -------------------------------------------------------------------
 */

struct CrosshairsStruct {

    XPoint hotSpot;     /* Hot spot for crosshairs */
    int visible;        /* Internal state of crosshairs. If non-zero,
                         * crosshairs are displayed. */
    int hidden;         /* If non-zero, crosshairs are not displayed.
                         * This is not necessarily consistent with the
                         * internal state variable.  This is true when
                         * the hot spot is off the graph.  */
    Rbc_Dashes dashes;  /* Dashstyle of the crosshairs. This represents
                         * an array of alternatingly drawn pixel
                         * values. If NULL, the hairs are drawn as a
                         * solid line */
    int lineWidth;      /* Width of the simulated crosshair lines */
    XSegment segArr[2]; /* Positions of line segments representing the
                         * simulated crosshairs. */
    XColor *colorPtr;   /* Foreground color of crosshairs */
    GC gc;              /* Graphics context for crosshairs. Set to
                         * GXxor to not require redraws of graph */
};

#define DEF_HAIRS_DASHES (char *)NULL
#define DEF_HAIRS_FOREGROUND RGB_BLACK
#define DEF_HAIRS_FG_MONO RGB_BLACK
#define DEF_HAIRS_LINE_WIDTH "0"
#define DEF_HAIRS_HIDE "yes"
#define DEF_HAIRS_POSITION (char *)NULL

static Tk_ConfigSpec configSpecs[] = {{TK_CONFIG_COLOR, "-color", "color", "Color", DEF_HAIRS_FOREGROUND,
                                       offsetof(Crosshairs, colorPtr), TK_CONFIG_COLOR_ONLY},
                                      {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_HAIRS_FG_MONO,
                                       offsetof(Crosshairs, colorPtr), TK_CONFIG_MONO_ONLY},
                                      {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes", DEF_HAIRS_DASHES,
                                       offsetof(Crosshairs, dashes), TK_CONFIG_NULL_OK, &rbcDashesOption},
                                      {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_HAIRS_HIDE,
                                       offsetof(Crosshairs, hidden), TK_CONFIG_DONT_SET_DEFAULT},
                                      {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "Linewidth", DEF_HAIRS_LINE_WIDTH,
                                       offsetof(Crosshairs, lineWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
                                      {TK_CONFIG_CUSTOM, "-position", "position", "Position", DEF_HAIRS_POSITION,
                                       offsetof(Crosshairs, hotSpot), 0, &rbcPointOption},
                                      {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

static void TurnOffHairs(Tk_Window tkwin, Crosshairs *chPtr);
static void TurnOnHairs(Graph *graphPtr, Crosshairs *chPtr);

typedef int(RbcGrHairsOp)(Graph *, Tcl_Interp *, int, Tcl_Obj *const[]);
static RbcGrHairsOp CgetOp;
static RbcGrHairsOp ConfigureOp;
static RbcGrHairsOp OnOp;
static RbcGrHairsOp OffOp;
static RbcGrHairsOp ToggleOp;

/*
 *----------------------------------------------------------------------
 *
 * TurnOffHairs --
 *
 *      XOR's the existing line segments (representing the crosshairs),
 *      thereby erasing them.  The internal state of the crosshairs is
 *      tracked.
 *
 * Parameters:
 *      Tk_Window tkwin
 *      Crosshairs *chPtr
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Crosshairs are erased.
 *
 *----------------------------------------------------------------------
 */
static void TurnOffHairs(Tk_Window tkwin, Crosshairs *chPtr) {
    if (Tk_IsMapped(tkwin) && (chPtr->visible)) {
        XDrawSegments(Tk_Display(tkwin), Tk_WindowId(tkwin), chPtr->gc, chPtr->segArr, 2);
        chPtr->visible = FALSE;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TurnOnHairs --
 *
 *      Draws (by XORing) new line segments, creating the effect of
 *      crosshairs. The internal state of the crosshairs is tracked.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Crosshairs *chPtr
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Crosshairs are displayed.
 *
 *----------------------------------------------------------------------
 */
static void TurnOnHairs(Graph *graphPtr, Crosshairs *chPtr) {
    if (Tk_IsMapped(graphPtr->tkwin) && (!chPtr->visible)) {
        if (!PointInGraph(graphPtr, chPtr->hotSpot.x, chPtr->hotSpot.y)) {
            return; /* Coordinates are off the graph */
        }
        XDrawSegments(graphPtr->display, Tk_WindowId(graphPtr->tkwin), chPtr->gc, chPtr->segArr, 2);
        chPtr->visible = TRUE;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ConfigureCrosshairs --
 *
 *      Configures attributes of the crosshairs such as line width,
 *      dashes, and position.  The crosshairs are first turned off
 *      before any of the attributes changes.
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Crosshair GC is allocated.
 *
 *----------------------------------------------------------------------
 */
void Rbc_ConfigureCrosshairs(Graph *graphPtr) {
    XGCValues gcValues;
    unsigned long gcMask;
    GC newGC;
    long colorValue;
    Crosshairs *chPtr = graphPtr->crosshairs;

    /*
     * Turn off the crosshairs temporarily. This is in case the new
     * configuration changes the size, style, or position of the lines.
     */
    TurnOffHairs(graphPtr->tkwin, chPtr);

    gcValues.function = GXxor;

    if (graphPtr->plotBg == NULL) {
        /* The graph's color option may not have been set yet */
        colorValue = WhitePixelOfScreen(Tk_Screen(graphPtr->tkwin));
    } else {
        colorValue = graphPtr->plotBg->pixel;
    }
    gcValues.background = colorValue;
    gcValues.foreground = (colorValue ^ chPtr->colorPtr->pixel);

    gcValues.line_width = LineWidth(chPtr->lineWidth);
    gcMask = (GCForeground | GCBackground | GCFunction | GCLineWidth);
    if (LineIsDashed(chPtr->dashes)) {
        gcValues.line_style = LineOnOffDash;
        gcMask |= GCLineStyle;
    }
    newGC = Rbc_GetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (LineIsDashed(chPtr->dashes)) {
        Rbc_SetDashes(graphPtr->display, newGC, &(chPtr->dashes));
    }
    if (chPtr->gc != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, chPtr->gc);
    }
    chPtr->gc = newGC;

    /*
     * Are the new coordinates on the graph?
     */
    chPtr->segArr[0].x2 = chPtr->segArr[0].x1 = chPtr->hotSpot.x;
    chPtr->segArr[0].y1 = graphPtr->bottom;
    chPtr->segArr[0].y2 = graphPtr->top;
    chPtr->segArr[1].y2 = chPtr->segArr[1].y1 = chPtr->hotSpot.y;
    chPtr->segArr[1].x1 = graphPtr->left;
    chPtr->segArr[1].x2 = graphPtr->right;

    if (!chPtr->hidden) {
        TurnOnHairs(graphPtr, chPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_EnableCrosshairs --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_EnableCrosshairs(Graph *graphPtr) {
    if (!graphPtr->crosshairs->hidden) {
        TurnOnHairs(graphPtr, graphPtr->crosshairs);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DisableCrosshairs --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_DisableCrosshairs(Graph *graphPtr) {
    if (!graphPtr->crosshairs->hidden) {
        TurnOffHairs(graphPtr->tkwin, graphPtr->crosshairs);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_UpdateCrosshairs --
 *
 *      Update the length of the hairs (not the hot spot).
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_UpdateCrosshairs(Graph *graphPtr) {
    Crosshairs *chPtr = graphPtr->crosshairs;

    chPtr->segArr[0].y1 = graphPtr->bottom;
    chPtr->segArr[0].y2 = graphPtr->top;
    chPtr->segArr[1].x1 = graphPtr->left;
    chPtr->segArr[1].x2 = graphPtr->right;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DestroyCrosshairs --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Crosshair GC is allocated.
 *
 *----------------------------------------------------------------------
 */
void Rbc_DestroyCrosshairs(Graph *graphPtr) {
    Crosshairs *chPtr = graphPtr->crosshairs;

    Tk_FreeOptions(configSpecs, (char *)chPtr, graphPtr->display, 0);
    if (chPtr->gc != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, chPtr->gc);
    }
    ckfree((char *)chPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CreateCrosshairs --
 *
 *      Creates and initializes a new crosshair structure.
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      Returns TCL_ERROR if the crosshair structure can't be created,
 *      otherwise TCL_OK.
 *
 * Side Effects:
 *      Crosshair GC is allocated.
 *
 *----------------------------------------------------------------------
 */
int Rbc_CreateCrosshairs(Graph *graphPtr) {
    Crosshairs *chPtr;

    chPtr = RbcCalloc(1, sizeof(Crosshairs));
    assert(chPtr);
    chPtr->hidden = TRUE;
    chPtr->hotSpot.x = chPtr->hotSpot.y = -1;
    graphPtr->crosshairs = chPtr;

    if (Rbc_ConfigureWidgetComponent(graphPtr->interp, graphPtr->tkwin, "crosshairs", "Crosshairs", configSpecs, 0,
                                     NULL, (char *)chPtr, 0) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries configuration attributes of the crosshairs such as
 *      line width, dashes, and position.
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
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Crosshairs *chPtr = graphPtr->crosshairs;

    return Tk_ConfigureValue(interp, graphPtr->tkwin, configSpecs, (char *)chPtr, Tcl_GetString(objv[3]), 0);
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Queries or resets configuration attributes of the crosshairs
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
 *      Crosshairs are reset.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Crosshairs *chPtr = graphPtr->crosshairs;

    if (objc == 3) {
        return Tk_ConfigureInfo(interp, graphPtr->tkwin, configSpecs, (char *)chPtr, (char *)NULL, 0);
    } else if (objc == 4) {
        return Tk_ConfigureInfo(interp, graphPtr->tkwin, configSpecs, (char *)chPtr, Tcl_GetString(objv[3]), 0);
    }
    if (Tk_ConfigureWidget(interp, graphPtr->tkwin, configSpecs, objc - 3, objv + 3, (char *)chPtr,
                           TK_CONFIG_ARGV_ONLY) != TCL_OK) {
        return TCL_ERROR;
    }
    Rbc_ConfigureCrosshairs(graphPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * OnOp --
 *
 *      Maps the crosshairs.
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
 *      Crosshairs are reset if necessary.
 *
 *----------------------------------------------------------------------
 */
static int OnOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Crosshairs *chPtr = graphPtr->crosshairs;

    if (chPtr->hidden) {
        TurnOnHairs(graphPtr, chPtr);
        chPtr->hidden = FALSE;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * OffOp --
 *
 *      Unmaps the crosshairs.
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
 *      Crosshairs are reset if necessary.
 *
 *----------------------------------------------------------------------
 */
static int OffOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Crosshairs *chPtr = graphPtr->crosshairs;

    if (!chPtr->hidden) {
        TurnOffHairs(graphPtr->tkwin, chPtr);
        chPtr->hidden = TRUE;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ToggleOp --
 *
 *      Toggles the state of the crosshairs.
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
 *      Crosshairs are reset.
 *
 *----------------------------------------------------------------------
 */
static int ToggleOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Crosshairs *chPtr = graphPtr->crosshairs;

    chPtr->hidden = (chPtr->hidden == 0);
    if (chPtr->hidden) {
        TurnOffHairs(graphPtr->tkwin, chPtr);
    } else {
        TurnOnHairs(graphPtr, chPtr);
    }
    return TCL_OK;
}

static Rbc_OpSpec xhairOps[] = {
    {"cget", (Rbc_Op)CgetOp, 4, 4, "option"}, {"configure", (Rbc_Op)ConfigureOp, 3, 0, "?options...?"},
    {"off", (Rbc_Op)OffOp, 3, 3, ""},         {"on", (Rbc_Op)OnOp, 3, 3, ""},
    {"toggle", (Rbc_Op)ToggleOp, 3, 3, ""},   RBC_OPSPEC_END};

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CrosshairsOp --
 *
 *      User routine to configure crosshair simulation.  Crosshairs
 *      are simulated by drawing line segments parallel to both axes
 *      using the XOR drawing function. The allows the lines to be
 *      erased (by drawing them again) without redrawing the entire
 *      graph.  Care must be taken to erase crosshairs before redrawing
 *      the graph and redraw them after the graph is redraw.
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
 *      Crosshairs may be drawn in the plotting area.
 *
 *----------------------------------------------------------------------
 */
int Rbc_CrosshairsOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Rbc_Op proc;

    proc = Rbc_GetOpFromObj(interp, xhairOps, RBC_OP_ARG2, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    return (*proc)(graphPtr, interp, objc, objv);
}
