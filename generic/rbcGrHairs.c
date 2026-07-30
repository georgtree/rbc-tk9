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
    /*
     * Modern Tk option state.
     */
    Tk_OptionTable optionTable;
    
    /*
     * Original Tcl representations.
     */
    Tcl_Obj *dashesObjPtr;
    Tcl_Obj *lineWidthObjPtr;
    Tcl_Obj *positionObjPtr;
    
    /*
     * Derived and runtime state.
     */    
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
#define HAIRS_GC_CHANGED       (1U << 0)
#define HAIRS_POSITION_CHANGED (1U << 1)
#define HAIRS_STATE_CHANGED    (1U << 2)
#define HAIRS_INITIALIZE_MASK (HAIRS_GC_CHANGED | HAIRS_POSITION_CHANGED)

static const Tk_OptionSpec crosshairsOptionSpecs[] = {
    {TK_OPTION_COLOR, "-color", "color", "Color", DEF_HAIRS_FOREGROUND, -1, offsetof(Crosshairs, colorPtr), 0,
     DEF_HAIRS_FG_MONO, HAIRS_GC_CHANGED},
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", DEF_HAIRS_DASHES, offsetof(Crosshairs, dashesObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, HAIRS_GC_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_HAIRS_HIDE, -1, offsetof(Crosshairs, hidden), 0, NULL,
     HAIRS_STATE_CHANGED},
    {TK_OPTION_PIXELS, "-linewidth", "lineWidth", "Linewidth", DEF_HAIRS_LINE_WIDTH,
     offsetof(Crosshairs, lineWidthObjPtr), -1, 0, NULL, HAIRS_GC_CHANGED},
    {TK_OPTION_STRING, "-position", "position", "Position", DEF_HAIRS_POSITION, offsetof(Crosshairs, positionObjPtr),
     -1, TK_OPTION_NULL_OK, NULL, HAIRS_POSITION_CHANGED},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

static void TurnOffHairs(Tk_Window tkwin, Crosshairs *chPtr);
static void TurnOnHairs(Graph *graphPtr, Crosshairs *chPtr);

typedef int(RbcGrHairsOp)(Graph *, Tcl_Interp *, int, Tcl_Obj *const[]);
typedef RbcGrHairsOp *RbcGrHairsOpPtr;
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

static int GetCrosshairPositionFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, XPoint *pointPtr) {
    const char *string;
    const char *comma;
    Tcl_Size length;
    Tcl_Size xLength;
    Tcl_Size yLength;
    Tcl_Obj *xObjPtr;
    Tcl_Obj *yObjPtr;
    int x, y;
    int result;

    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        pointPtr->x = -SHRT_MAX;
        pointPtr->y = -SHRT_MAX;
        return TCL_OK;
    }
    string = Tcl_GetStringFromObj(objPtr, &length);
    if ((length < 4) || (string[0] != '@')) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad position \"%s\": should be \"@x,y\"", string));
        return TCL_ERROR;
    }
    comma = memchr(string + 1, ',', (size_t)(length - 1));
    if ((comma == NULL) || (comma == string + 1) || (comma == string + length - 1)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad position \"%s\": should be \"@x,y\"", string));
        return TCL_ERROR;
    }
    xLength = (Tcl_Size)(comma - (string + 1));
    yLength = length - (Tcl_Size)(comma - string) - 1;
    xObjPtr = Tcl_NewStringObj(string + 1, xLength);
    yObjPtr = Tcl_NewStringObj(comma + 1, yLength);
    Tcl_IncrRefCount(xObjPtr);
    Tcl_IncrRefCount(yObjPtr);
    result = Tk_GetPixelsFromObj(interp, tkwin, xObjPtr, &x);
    if (result == TCL_OK) {
        result = Tk_GetPixelsFromObj(interp, tkwin, yObjPtr, &y);
    }
    Tcl_DecrRefCount(xObjPtr);
    Tcl_DecrRefCount(yObjPtr);
    if (result != TCL_OK) {
        Tcl_AppendResult(interp, ": can't parse position \"", string, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    if ((x < SHRT_MIN) || (x > SHRT_MAX) || (y < SHRT_MIN) || (y > SHRT_MAX)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("position \"%s\" is outside the supported range", string));
        return TCL_ERROR;
    }
    pointPtr->x = (short)x;
    pointPtr->y = (short)y;
    return TCL_OK;
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

static int ConfigureCrosshairs(Graph *graphPtr, Crosshairs *chPtr, int mask) {
    Rbc_Dashes newDashes;
    XPoint newHotSpot;
    int newLineWidth;
    XGCValues gcValues;
    unsigned long gcMask;
    GC newGC;
    long colorValue;

    newDashes = chPtr->dashes;
    newHotSpot = chPtr->hotSpot;
    newLineWidth = chPtr->lineWidth;
    newGC = NULL;
    /*
     * Parse and validate all fallible values before modifying the
     * currently displayed XOR crosshairs.
     */
    if (mask & HAIRS_GC_CHANGED) {
        if (Rbc_GetDashesFromObj(graphPtr->interp, chPtr->dashesObjPtr, &newDashes) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, chPtr->lineWidthObjPtr, PIXELS_NONNEGATIVE,
                                 &newLineWidth) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (mask & HAIRS_POSITION_CHANGED) {
        if (GetCrosshairPositionFromObj(graphPtr->interp, graphPtr->tkwin, chPtr->positionObjPtr, &newHotSpot) !=
            TCL_OK) {
            return TCL_ERROR;
        }
    }
    /*
     * Build the replacement GC before erasing the old crosshairs.
     */
    if (mask & HAIRS_GC_CHANGED) {
        gcValues.function = GXxor;
        if (graphPtr->plotBg == NULL) {
            colorValue = WhitePixelOfScreen(Tk_Screen(graphPtr->tkwin));
        } else {
            colorValue = graphPtr->plotBg->pixel;
        }
        gcValues.background = colorValue;
        gcValues.foreground = colorValue ^ chPtr->colorPtr->pixel;
        gcValues.line_width = LineWidth(newLineWidth);
        gcMask = GCForeground | GCBackground | GCFunction | GCLineWidth;
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
     * Erase the old XOR image while its old GC and coordinates are
     * still active.
     */
    TurnOffHairs(graphPtr->tkwin, chPtr);
    if (mask & HAIRS_GC_CHANGED) {
        GC oldGC;
        oldGC = chPtr->gc;
        chPtr->dashes = newDashes;
        chPtr->lineWidth = newLineWidth;
        chPtr->gc = newGC;
        if (oldGC != NULL) {
            Rbc_FreePrivateGC(graphPtr->display, oldGC);
        }
    }
    if (mask & HAIRS_POSITION_CHANGED) {
        chPtr->hotSpot = newHotSpot;
    }
    /*
     * Rebuild all segment coordinates. This also handles calls made
     * when the graph background or plot geometry has changed.
     */
    chPtr->segArr[0].x1 = chPtr->segArr[0].x2 = chPtr->hotSpot.x;
    chPtr->segArr[0].y1 = graphPtr->bottom;
    chPtr->segArr[0].y2 = graphPtr->top;
    chPtr->segArr[1].y1 = chPtr->segArr[1].y2 = chPtr->hotSpot.y;
    chPtr->segArr[1].x1 = graphPtr->left;
    chPtr->segArr[1].x2 = graphPtr->right;
    if (!chPtr->hidden) {
        TurnOnHairs(graphPtr, chPtr);
    }
    return TCL_OK;
}

void Rbc_ConfigureCrosshairs(Graph *graphPtr) {
    Crosshairs *chPtr;

    chPtr = graphPtr->crosshairs;
    if (chPtr == NULL) {
        return;
    }
    /*
     * A graph-level background change only requires rebuilding the GC.
     * Existing option values have already been validated.
     */
    (void)ConfigureCrosshairs(graphPtr, chPtr, HAIRS_GC_CHANGED);
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
    Crosshairs *chPtr;

    chPtr = graphPtr->crosshairs;
    if (chPtr == NULL) {
        return;
    }
    graphPtr->crosshairs = NULL;
    Tk_FreeConfigOptions((char *)chPtr, chPtr->optionTable, graphPtr->tkwin);
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
    assert(chPtr != NULL);
    graphPtr->crosshairs = chPtr;
    chPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, crosshairsOptionSpecs);
    if (Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, "crosshairs", "Crosshairs", (char *)chPtr,
                                 chPtr->optionTable) != TCL_OK) {
        goto error;
    }
    if (ConfigureCrosshairs(graphPtr, chPtr, HAIRS_INITIALIZE_MASK) != TCL_OK) {
        goto error;
    }
    return TCL_OK;

error:
    Rbc_DestroyCrosshairs(graphPtr);
    return TCL_ERROR;
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
    Crosshairs *chPtr;
    Tcl_Obj *resultObjPtr;

    chPtr = graphPtr->crosshairs;
    resultObjPtr = Tk_GetOptionValue(interp, (char *)chPtr, chPtr->optionTable, objv[3], graphPtr->tkwin);
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
    Crosshairs *chPtr;
    Tcl_Obj *resultObjPtr;
    Tk_SavedOptions savedOptions;
    int mask;

    chPtr = graphPtr->crosshairs;
    if (objc == 3) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)chPtr, chPtr->optionTable, NULL, graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (objc == 4) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)chPtr, chPtr->optionTable, objv[3], graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (Tk_SetOptions(interp, (char *)chPtr, chPtr->optionTable, objc - 3, objv + 3, graphPtr->tkwin, &savedOptions,
                      &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    if (ConfigureCrosshairs(graphPtr, chPtr, mask) != TCL_OK) {
        Tcl_Obj *errorObjPtr;
        errorObjPtr = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(errorObjPtr);
        Tk_RestoreSavedOptions(&savedOptions);
        Tcl_SetObjResult(interp, errorObjPtr);
        Tcl_DecrRefCount(errorObjPtr);
        return TCL_ERROR;
    }
    Tk_FreeSavedOptions(&savedOptions);
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

static const Rbc_OpSpec xhairOps[] = {
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
int Rbc_CrosshairsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    RbcGrHairsOpPtr proc;

    proc = (RbcGrHairsOpPtr)Rbc_GetOpFromObj(interp, xhairOps, RBC_OP_ARG2, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    return (*proc)(graphPtr, interp, objc, objv);
}
