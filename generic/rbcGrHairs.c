/**
 * @file rbcGrHairs.c
 * @brief Crosshair configuration and motion helpers.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup crosshair_internals Crosshair configuration and motion helpers
 * Private component helpers; not installed C API entry points.
 */
#include "rbcGraph.h"
#include "rbcRender.h"

/** @brief Graph-owned crosshair options and mapped XOR drawing state. */
struct CrosshairsStruct {
    /*
     * Modern Tk option state.
     */
    Tk_OptionTable optionTable; /**< Tk option table for this component configuration lifecycle. */
    /*
     * Original Tcl representations.
     */
    Tcl_Obj *dashesObjPtr; /**< Tk option-managed Tcl representation for dashes; released with the owning option state. */
    Tcl_Obj *lineWidthObjPtr; /**< Tk option-managed Tcl representation for line width; released with the owning option state. */
    Tcl_Obj *positionObjPtr; /**< Tk option-managed Tcl representation for position; released with the owning option state. */
    /*
     * Derived and runtime state.
     */
    Point2D hotSpot; /**< Hot spot for crosshairs */
    int visible; /**< Internal state of crosshairs. If non-zero, crosshairs are displayed. */
    int hidden; /**< If non-zero, crosshairs are not displayed. This is not necessarily consistent with the internal
     * state variable. This is true when the hot spot is off the graph.
     */
    Rbc_Dashes dashes; /**< Dashstyle of the crosshairs. This represents an array of alternatingly drawn pixel values. If
     * NULL, the hairs are drawn as a solid line
     */
    int lineWidth; /**< Width of the simulated crosshair lines */
    Segment2D segArr[2]; /**< Positions of line segments representing the simulated crosshairs. */
    XColor *colorPtr; /**< Foreground color of crosshairs */
    GC gc; /**< Graphics context for crosshairs. Set to GXxor to not require redraws of graph */
};

#define DEF_HAIRS_DASHES (char *)NULL
#define DEF_HAIRS_FOREGROUND RGB_BLACK
#define DEF_HAIRS_FG_MONO RGB_BLACK
#define DEF_HAIRS_LINE_WIDTH "0"
#define DEF_HAIRS_HIDE "yes"
#define DEF_HAIRS_POSITION (char *)NULL
#define HAIRS_GC_CHANGED (1U << 0)
#define HAIRS_POSITION_CHANGED (1U << 1)
#define HAIRS_STATE_CHANGED (1U << 2)
#define HAIRS_INITIALIZE_MASK (HAIRS_GC_CHANGED | HAIRS_POSITION_CHANGED)

/** @ingroup component_option_tables
 * @brief Crosshair visibility, location and stroke resources used by the screen overlay.
 * @hideinitializer
 */
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

typedef int RbcGrHairsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

/** @brief Typed command dispatch entry with shared argument validation metadata. */
typedef struct {
    Rbc_OpSpecHeader header; /**< Operation name, total argument bounds and usage suffix. */
    RbcGrHairsOp *proc; /**< Typed command handler for this operation. */
} CrosshairsOpSpec;

static RbcGrHairsOp CgetOp;
static RbcGrHairsOp ConfigureOp;
static RbcGrHairsOp OnOp;
static RbcGrHairsOp OffOp;
static RbcGrHairsOp ToggleOp;

/**
 * @brief Erase currently visible XOR segments using their original GC.
 * @ingroup crosshair_internals
 *
 * Requires a live Tk window and crosshair record. Only draws when the window
 * is mapped and visible is true, then clears visible. On an unmapped window it
 * leaves visible unchanged. Does not consult or modify requested hidden state.
 * Endpoints/GC must still describe the pixels being erased. No Tcl callbacks.
 *
 * @param[in] tkwin Live window holding the original XOR pixels.
 * @param[in,out] chPtr Prepared crosshair record with original GC/endpoints; visible is cleared only
 *    after mapped erasure.
 */
static void TurnOffHairs(Tk_Window tkwin, Crosshairs *chPtr) {
    if (Tk_IsMapped(tkwin) && chPtr->visible) {
        Rbc_RenderScreenSegments(tkwin, Tk_WindowId(tkwin), chPtr->gc, chPtr->segArr, 2);
        chPtr->visible = FALSE;
    }
}

/**
 * @brief Draw requested XOR geometry when the hotspot lies inside the plot.
 * @ingroup crosshair_internals
 *
 * Borrows a live graph/window and prepared crosshair GC/segments. Draws only
 * when mapped, not already visible, and PointInGraph accepts the hotspot; then
 * sets visible. Does not test hidden: callers decide requested visibility.
 * Does not remap endpoints or schedule an idle redraw. No Tcl callbacks.
 *
 * @param[in] graphPtr Borrowed live graph providing window and plot bounds.
 * @param[in,out] chPtr Prepared GC, hotspot and segments; visible may become true, hidden is not
 *    inspected.
 */
static void TurnOnHairs(Graph *graphPtr, Crosshairs *chPtr) {
    if (Tk_IsMapped(graphPtr->tkwin) && (!chPtr->visible)) {
        if (!PointInGraph(graphPtr, chPtr->hotSpot.x, chPtr->hotSpot.y)) {
            return;
        }
        Rbc_RenderScreenSegments(graphPtr->tkwin, Tk_WindowId(graphPtr->tkwin), chPtr->gc, chPtr->segArr, 2);
        chPtr->visible = TRUE;
    }
}

/**
 * @brief Parse an optional \@x,y widget-pixel position.
 * @ingroup crosshair_internals
 *
 * NULL or empty input succeeds with INT_MIN in both output coordinates, an
 * out-of-plot sentinel. Otherwise splits at the first comma and converts both
 * parts through Tk_GetPixelsFromObj using temporary retained Tcl objects.
 * Accepts Tk screen-distance syntax, storing integer pixel results as doubles.
 *
 * Returns TCL_OK only after both coordinates convert. On TCL_ERROR leaves
 * pointPtr unchanged, releases both temporary objects, and reports syntax or
 * conversion context in interp. Does not move crosshairs or invoke Tcl code.
 *
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed Tk window used to convert screen distances.
 * @param[in] objPtr Borrowed position object; NULL or empty selects the off-plot sentinel.
 * @param[out] pointPtr Required result storage; receives integer pixel coordinates as doubles, unchanged
 *    on failure.
 */
static int GetCrosshairPositionFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, Point2D *pointPtr) {
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
        pointPtr->x = (double)INT_MIN;
        pointPtr->y = (double)INT_MIN;
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
        Rbc_AppendResultStrings(interp, ": can't parse position \"", string, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    pointPtr->x = (double)x;
    pointPtr->y = (double)y;
    return TCL_OK;
}

#ifdef WIN32
/**
 * @brief Batch Windows position-only XOR erasure and drawing.
 * @ingroup crosshair_internals
 *
 * Requires unchanged drawing style/GC and a live graph/window. Computes new
 * segments from hotSpotPtr and current graph bounds; compares both visibility
 * and endpoints to avoid redrawing identical geometry. Submits up to four old/new
 * segments in one renderer call, preserving a single native drawing batch.
 *
 * Always updates hotspot, segments and visible, including when unmapped or
 * hidden. Does not alter hidden or allocate a replacement GC. Only appropriate
 * for position-only configuration; style changes must erase with the old GC.
 * No Tcl callbacks or event processing may intervene in this transition.
 *
 * @param[in] graphPtr Borrowed live graph with current plot bounds.
 * @param[in,out] chPtr Prepared crosshairs with unchanged style/GC; hotspot, segments and visible state
 *    are replaced.
 * @param[in] hotSpotPtr Borrowed new hotspot in widget pixels; copied, not retained.
 */
static void MoveCrosshairs(Graph *graphPtr, Crosshairs *chPtr, const Point2D *hotSpotPtr) {
    Segment2D newSegments[2];
    Segment2D segments[4];
    int mapped;
    int oldVisible;
    int newVisible;
    int unchanged;
    int nSegments;
    int i;

    newSegments[0].p.x = newSegments[0].q.x = hotSpotPtr->x;
    newSegments[0].p.y = (double)graphPtr->bottom;
    newSegments[0].q.y = (double)graphPtr->top;
    newSegments[1].p.y = newSegments[1].q.y = hotSpotPtr->y;
    newSegments[1].p.x = (double)graphPtr->left;
    newSegments[1].q.x = (double)graphPtr->right;
    mapped = Tk_IsMapped(graphPtr->tkwin);
    oldVisible = mapped && chPtr->visible;
    newVisible = mapped && !chPtr->hidden && PointInGraph(graphPtr, hotSpotPtr->x, hotSpotPtr->y);
    unchanged = (oldVisible == newVisible);
    if (unchanged && oldVisible) {
        for (i = 0; i < 2; i++) {
            if ((chPtr->segArr[i].p.x != newSegments[i].p.x) || (chPtr->segArr[i].p.y != newSegments[i].p.y) ||
                (chPtr->segArr[i].q.x != newSegments[i].q.x) || (chPtr->segArr[i].q.y != newSegments[i].q.y)) {
                unchanged = FALSE;
                break;
            }
        }
    }
    nSegments = 0;
    if (!unchanged) {
        if (oldVisible) {
            segments[nSegments++] = chPtr->segArr[0];
            segments[nSegments++] = chPtr->segArr[1];
        }
        if (newVisible) {
            segments[nSegments++] = newSegments[0];
            segments[nSegments++] = newSegments[1];
        }
        if (nSegments > 0) {
            Rbc_RenderScreenSegments(graphPtr->tkwin, Tk_WindowId(graphPtr->tkwin), chPtr->gc, segments, nSegments);
        }
    }
    chPtr->hotSpot = *hotSpotPtr;
    chPtr->segArr[0] = newSegments[0];
    chPtr->segArr[1] = newSegments[1];
    chPtr->visible = newVisible;
}
#endif /* WIN32 */

/**
 * @brief Validate derived crosshair state before changing XOR pixels.
 * @ingroup crosshair_internals
 *
 * Requires a live graph/window and Tk-parsed option fields. Stages dash, width
 * and hotspot conversions selected by mask; TCL_ERROR leaves active GC/geometry
 * and displayed pixels unchanged, while caller restores Tk options separately.
 * Windows exact position-only changes delegate to MoveCrosshairs.
 *
 * Otherwise prepares a replacement XOR GC, erases with old GC/endpoints, commits
 * new state, rebuilds both segments, and draws when requested and eligible.
 * Releases replaced GC; returns TCL_OK without scheduling a graph redraw.
 * No Tcl callback is evaluated between erase and replacement.
 *
 * @param[in] graphPtr Borrowed live graph supplying background, bounds and Tk context.
 * @param[in,out] chPtr Initialized crosshairs with candidate Tk fields; active geometry/GC change only
 *    after conversion succeeds.
 * @param[in] mask HAIRS_*_CHANGED bits; exactly HAIRS_POSITION_CHANGED selects Windows motion batching.
 */
static int ConfigureCrosshairs(Graph *graphPtr, Crosshairs *chPtr, int mask) {
    Rbc_Dashes newDashes;
    Point2D newHotSpot;
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
#ifdef WIN32
    /*
     * Ordinary pointer motion changes only -position. Erase and
     * redraw with one submission, avoiding a second DC acquisition
     * and pen allocation after the old hairs have been erased.
     *
     * Changes involving GC attributes or visibility continue through
     * the general configuration path below.
     */
    if (mask == HAIRS_POSITION_CHANGED) {
        MoveCrosshairs(graphPtr, chPtr, &newHotSpot);
        return TCL_OK;
    }
#endif
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
    chPtr->segArr[0].p.x = chPtr->segArr[0].q.x = chPtr->hotSpot.x;
    chPtr->segArr[0].p.y = (double)graphPtr->bottom;
    chPtr->segArr[0].q.y = (double)graphPtr->top;
    chPtr->segArr[1].p.y = chPtr->segArr[1].q.y = chPtr->hotSpot.y;
    chPtr->segArr[1].p.x = (double)graphPtr->left;
    chPtr->segArr[1].q.x = (double)graphPtr->right;
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

void Rbc_EnableCrosshairs(Graph *graphPtr) {
    if (!graphPtr->crosshairs->hidden) {
        TurnOnHairs(graphPtr, graphPtr->crosshairs);
    }
}

void Rbc_DisableCrosshairs(Graph *graphPtr) {
    if (!graphPtr->crosshairs->hidden) {
        TurnOffHairs(graphPtr->tkwin, graphPtr->crosshairs);
    }
}

void Rbc_UpdateCrosshairs(Graph *graphPtr) {
    Crosshairs *chPtr;

    chPtr = graphPtr->crosshairs;
    chPtr->segArr[0].p.y = (double)graphPtr->bottom;
    chPtr->segArr[0].q.y = (double)graphPtr->top;
    chPtr->segArr[1].p.x = (double)graphPtr->left;
    chPtr->segArr[1].q.x = (double)graphPtr->right;
}

int Rbc_CrosshairsNeedFullRedraw(Graph *graphPtr) {
    Crosshairs *chPtr = graphPtr->crosshairs;
    int i;

    if (chPtr == NULL) {
        return FALSE;
    }
    if ((chPtr->lineWidth > 1) && (chPtr->visible || !chPtr->hidden)) {
        return TRUE;
    }
    if (!chPtr->visible) {
        return FALSE;
    }
    for (i = 0; i < 2; i++) {
        const Segment2D *segPtr = chPtr->segArr + i;
        if ((segPtr->p.x < graphPtr->left) || (segPtr->p.x > graphPtr->right) || (segPtr->p.y < graphPtr->top) ||
            (segPtr->p.y > graphPtr->bottom) || (segPtr->q.x < graphPtr->left) || (segPtr->q.x > graphPtr->right) ||
            (segPtr->q.y < graphPtr->top) || (segPtr->q.y > graphPtr->bottom)) {
            return TRUE;
        }
    }
    return FALSE;
}

void Rbc_PresentGraphWithCrosshairs(Graph *graphPtr, Drawable drawable) {
    Crosshairs *chPtr = graphPtr->crosshairs;
    int visible = FALSE;
    assert(drawable != Tk_WindowId(graphPtr->tkwin));
    assert(Tk_IsMapped(graphPtr->tkwin));
    if (chPtr != NULL) {
        Rbc_UpdateCrosshairs(graphPtr);
        if (!chPtr->hidden && PointInGraph(graphPtr, chPtr->hotSpot.x, chPtr->hotSpot.y)) {
            Rbc_RenderScreenSegments(graphPtr->tkwin, drawable, chPtr->gc, chPtr->segArr, 2);
            visible = TRUE;
        }
    }
    /*
     * Replace the old window pixels, including its old XOR hairs,
     * with the completed frame in a single copy.
     */
    if (graphPtr->flags & DRAW_MARGINS) {
        Rbc_RenderScreenCopyArea(graphPtr->tkwin, drawable, Tk_WindowId(graphPtr->tkwin), graphPtr->drawGC, 0, 0,
                                 graphPtr->width, graphPtr->height, 0, 0);
    } else {
        Rbc_RenderScreenCopyArea(graphPtr->tkwin, drawable, Tk_WindowId(graphPtr->tkwin), graphPtr->drawGC,
                                 graphPtr->left, graphPtr->top, graphPtr->right - graphPtr->left + 1,
                                 graphPtr->bottom - graphPtr->top + 1, graphPtr->left, graphPtr->top);
    }
    /*
     * The next direct XOR move must erase the hairs just copied to
     * the window. Do not call TurnOnHairs: that would erase them.
     */
    if (chPtr != NULL) {
        chPtr->visible = visible;
    }
}

void Rbc_DestroyCrosshairs(Graph *graphPtr) {
    Crosshairs *chPtr;

    chPtr = graphPtr->crosshairs;
    if (chPtr == NULL) {
        return;
    }
    graphPtr->crosshairs = NULL;
    Tk_FreeConfigOptions((char *)chPtr, chPtr->optionTable, graphPtr->tkwin);
    Tk_DeleteOptionTable(chPtr->optionTable);
    chPtr->optionTable = NULL;
    if (chPtr->gc != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, chPtr->gc);
    }
    ckfree((char *)chPtr);
}

int Rbc_CreateCrosshairs(Graph *graphPtr) {
    Crosshairs *chPtr;

    chPtr = Rbc_Calloc(1, sizeof(Crosshairs));
    assert(chPtr != NULL);
    graphPtr->crosshairs = chPtr;
    chPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, crosshairsOptionSpecs);
    if (Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, "crosshairs", "Crosshairs",
                                 (char *)chPtr, chPtr->optionTable) != TCL_OK) {
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

/**
 * @ingroup crosshair_internals
 * @brief Return one component option value.
 * The typed dispatcher has validated argument count. Borrows the graph-owned component and its option
 * table, returning Tk option errors unchanged. This query does not configure resources or schedule
 * drawing.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected crosshair record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Validated count of four; option name is objv[3].
 * @param[in] objv Borrowed full widget-command vector; see @ref rbc_command_arguments.
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
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

/**
 * @ingroup crosshair_internals
 * @brief Query or transactionally configure crosshair options.
 * Three or four arguments return Tk option metadata. Updates stage Tk fields and invoke
 * ConfigureCrosshairs to validate derived state before visual replacement. Failure restores saved
 * options and the diagnostic; success frees saved values. Presentation is handled by the helper rather
 * than scheduling a whole-graph redraw here.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected crosshair record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Command count; operation is at index two, optional arguments start at index three.
 * @param[in] objv Borrowed full widget-command vector; see @ref rbc_command_arguments.
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
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
        return Rbc_RestoreOptionsOnError(interp, &savedOptions);
    }
    Tk_FreeSavedOptions(&savedOptions);
    return TCL_OK;
}

/**
 * @ingroup crosshair_internals
 * @brief Request crosshairs to be shown.
 * When previously hidden, attempts drawing with TurnOnHairs before clearing hidden. Actual visibility
 * still depends on window mapping and hotspot location. Repeated on is a no-op even if requested hairs
 * are not currently visible; display/motion paths own later presentation.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected crosshair record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Unused by this visibility handler.
 * @param[in] objc Validated count of three; unused by this visibility handler.
 * @param[in] objv Unused validated command vector.
 */
static int OnOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Crosshairs *chPtr = graphPtr->crosshairs;
    if (chPtr->hidden) {
        TurnOnHairs(graphPtr, chPtr);
        chPtr->hidden = FALSE;
    }
    return TCL_OK;
}

/**
 * @ingroup crosshair_internals
 * @brief Request crosshairs to be hidden.
 * When previously requested on, erases through TurnOffHairs before setting hidden. Repeated off is a
 * no-op. The low-level helper may leave actual visibility unchanged on an unmapped window; requested
 * state is not a direct pixel-state assertion.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected crosshair record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Unused by this visibility handler.
 * @param[in] objc Validated count of three; unused by this visibility handler.
 * @param[in] objv Unused validated command vector.
 */
static int OffOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Crosshairs *chPtr = graphPtr->crosshairs;
    if (!chPtr->hidden) {
        TurnOffHairs(graphPtr->tkwin, chPtr);
        chPtr->hidden = TRUE;
    }
    return TCL_OK;
}

/**
 * @ingroup crosshair_internals
 * @brief Toggle requested crosshair state and perform its visual transition.
 * Updates hidden first, then erases or draws through the low-level helpers. Those helpers do not
 * consult hidden; they use actual visibility, mapping and hotspot eligibility. Does not invalidate the
 * reusable graph backing store.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected crosshair record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Unused by this visibility handler.
 * @param[in] objc Validated count of three; unused by this visibility handler.
 * @param[in] objv Unused validated command vector.
 */
static int ToggleOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Crosshairs *chPtr = graphPtr->crosshairs;
    chPtr->hidden = (chPtr->hidden == 0);
    if (chPtr->hidden) {
        TurnOffHairs(graphPtr->tkwin, chPtr);
    } else {
        TurnOnHairs(graphPtr, chPtr);
    }
    return TCL_OK;
}

static const CrosshairsOpSpec xhairOps[] = {
    {{"cget", 4, 4, "option"}, CgetOp}, {{"configure", 3, 0, "?options...?"}, ConfigureOp},
    {{"off", 3, 3, ""}, OffOp},         {{"on", 3, 3, ""}, OnOp},
    {{"toggle", 3, 3, ""}, ToggleOp},   {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup crosshair_internals
 * @brief Validate and dispatch a crosshair subcommand.
 * The typed table validates operation name and argument count before calling a handler. Borrowed graph
 * state must remain live. Dispatch itself does not update segment geometry, process events or perform
 * buffered presentation.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected crosshair record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Command count; operation is at index two, optional arguments start at index three.
 * @param[in] objv Borrowed full widget-command vector; see @ref rbc_command_arguments.
 */
int Rbc_CrosshairsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, xhairOps, (Tcl_Size)sizeof(xhairOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    return xhairOps[index].proc(graphPtr, interp, objc, objv);
}
