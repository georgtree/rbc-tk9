/*
 * rbcGrLegd.c --
 *
 *      This module implements the legend for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"
#include "rbcGrElem.h"

/*
 * -------------------------------------------------------------------
 *
 * Legend --
 *
 *      Contains information specific to how the legend will be
 *      displayed.
 *
 * -------------------------------------------------------------------
 */
struct LegendStruct {
    Tk_OptionTable optionTable;

    /*
     * Original Tcl representations for values requiring additional
     * conversion or validation.
     */
    Tcl_Obj *activeBorderWidthObjPtr;
    Tcl_Obj *borderWidthObjPtr;
    Tcl_Obj *columnsObjPtr;
    Tcl_Obj *ipadXObjPtr;
    Tcl_Obj *ipadYObjPtr;
    Tcl_Obj *padXObjPtr;
    Tcl_Obj *padYObjPtr;
    Tcl_Obj *positionObjPtr;
    Tcl_Obj *rowsObjPtr;
    Tcl_Obj *shadowObjPtr;

    int optionsInitialized;
    int tkResourcesReleased;

    unsigned int flags;
    Rbc_Uid classUid;          /* Type: Element or Marker. */
    int hidden;                /* If non-zero, don't display the legend. */
    int raised;                /* If non-zero, draw the legend last, above
                                * everything else. */
    int nEntries;              /* Number of element entries in table. */
    short int width, height;   /* Dimensions of the legend */
    short int nColumns, nRows; /* Number of columns and rows in legend */
    int site;
    Point2D anchorPos; /* Says how to position the legend. Indicates
                        * the site and/or x-y screen coordinates of
                        * the legend.  Used in conjunction with the
                        * anchor to determine its location. */
    Tk_Anchor anchor;  /* Anchor of legend. Used to interpret the
                        * positioning point of the legend in the
                        * graph*/
    int x, y;          /* Computed origin of legend. */
    Graph *graphPtr;
    Tcl_Command cmdToken; /* Token for graph's widget command. */
    int reqColumns, reqRows;
    Rbc_Pad ipadX, ipadY; /* # of pixels padding around legend entries */
    Rbc_Pad padX, padY;   /* # of pixels padding to exterior of legend */
    Tk_Window tkwin;      /* Optional external window to draw legend. */
    TextStyle style;
    int maxSymSize;           /* Size of largest symbol to be displayed.
                               * Used to calculate size of legend */
    Tk_3DBorder activeBorder; /* Active legend entry background color. */
    int activeRelief;         /* 3-D effect on active entry. */
    int entryBorderWidth;     /* Border width around each entry in legend. */
    Tk_3DBorder border;       /* 3-D effect of legend. */
    int borderWidth;          /* Width of legend 3-D border */
    int relief;               /* 3-d effect of border around the legend:
                               * TK_RELIEF_RAISED etc. */
    Rbc_BindTable bindTable;
};

typedef struct {
    int site;
    Point2D anchorPos;
    const char *windowName;
} LegendPosition;

#define padLeft padX.side1
#define padRight padX.side2
#define padTop padY.side1
#define padBottom padY.side2
#define PADDING(x) ((x).side1 + (x).side2)

#define DEF_LEGEND_ACTIVE_BACKGROUND STD_ACTIVE_BACKGROUND
#define DEF_LEGEND_ACTIVE_BG_MONO STD_ACTIVE_BG_MONO
#define DEF_LEGEND_ACTIVE_BORDERWIDTH "2"
#define DEF_LEGEND_ACTIVE_FOREGROUND STD_ACTIVE_FOREGROUND
#define DEF_LEGEND_ACTIVE_FG_MONO STD_ACTIVE_FG_MONO
#define DEF_LEGEND_ACTIVE_RELIEF "flat"
#define DEF_LEGEND_ANCHOR "n"
#define DEF_LEGEND_BACKGROUND (char *)NULL
#define DEF_LEGEND_BG_MONO (char *)NULL
#define DEF_LEGEND_BORDERWIDTH STD_BORDERWIDTH
#define DEF_LEGEND_FOREGROUND STD_NORMAL_FOREGROUND
#define DEF_LEGEND_FG_MONO STD_NORMAL_FG_MONO
#define DEF_LEGEND_FONT STD_FONT_SMALL
#define DEF_LEGEND_HIDE "no"
#define DEF_LEGEND_IPAD_X "1"
#define DEF_LEGEND_IPAD_Y "1"
#define DEF_LEGEND_PAD_X "1"
#define DEF_LEGEND_PAD_Y "1"
#define DEF_LEGEND_POSITION "rightmargin"
#define DEF_LEGEND_RAISED "no"
#define DEF_LEGEND_RELIEF "sunken"
#define DEF_LEGEND_SHADOW_COLOR (char *)NULL
#define DEF_LEGEND_ROWS "0"
#define DEF_LEGEND_COLUMNS "0"

#define LEGEND_REDRAW_CHANGED (1U << 0)
#define LEGEND_LAYOUT_CHANGED (1U << 1)
#define LEGEND_STYLE_CHANGED (1U << 2)
#define LEGEND_PADDING_CHANGED (1U << 3)
#define LEGEND_SHADOW_CHANGED (1U << 4)
#define LEGEND_POSITION_CHANGED (1U << 5)
#define LEGEND_VALIDATE_CHANGED (1U << 6)

#define LEGEND_INITIALIZE_MASK                                                                                         \
    (LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_STYLE_CHANGED | LEGEND_PADDING_CHANGED |                   \
     LEGEND_SHADOW_CHANGED | LEGEND_POSITION_CHANGED | LEGEND_VALIDATE_CHANGED)

static const Tk_OptionSpec legendOptionSpecs[] = {
    {TK_OPTION_BORDER, "-activebackground", "activeBackground", "ActiveBackground", DEF_LEGEND_ACTIVE_BACKGROUND, -1,
     offsetof(Legend, activeBorder), 0, DEF_LEGEND_ACTIVE_BG_MONO, LEGEND_REDRAW_CHANGED},
    {TK_OPTION_PIXELS, "-activeborderwidth", "activeBorderWidth", "BorderWidth", DEF_LEGEND_ACTIVE_BORDERWIDTH,
     offsetof(Legend, activeBorderWidthObjPtr), offsetof(Legend, entryBorderWidth), 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_VALIDATE_CHANGED},
    {TK_OPTION_COLOR, "-activeforeground", "activeForeground", "ActiveForeground", DEF_LEGEND_ACTIVE_FOREGROUND, -1,
     offsetof(Legend, style.activeColor), 0, DEF_LEGEND_ACTIVE_FG_MONO, LEGEND_REDRAW_CHANGED},
    {TK_OPTION_RELIEF, "-activerelief", "activeRelief", "Relief", DEF_LEGEND_ACTIVE_RELIEF, -1,
     offsetof(Legend, activeRelief), 0, NULL, LEGEND_REDRAW_CHANGED},
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor", DEF_LEGEND_ANCHOR, -1, offsetof(Legend, anchor), 0, NULL,
     LEGEND_REDRAW_CHANGED},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL, NULL, -1, -1, 0, "-background", 0},
    {TK_OPTION_BORDER, "-background", "background", "Background", DEF_LEGEND_BACKGROUND, -1, offsetof(Legend, border),
     TK_OPTION_NULL_OK, DEF_LEGEND_BG_MONO, LEGEND_REDRAW_CHANGED},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth", DEF_LEGEND_BORDERWIDTH,
     offsetof(Legend, borderWidthObjPtr), offsetof(Legend, borderWidth), 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_VALIDATE_CHANGED},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL, NULL, -1, -1, 0, "-borderwidth", 0},
    {TK_OPTION_INT, "-columns", "columns", "Columns", DEF_LEGEND_COLUMNS, offsetof(Legend, columnsObjPtr),
     offsetof(Legend, reqColumns), 0, NULL, LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_VALIDATE_CHANGED},
    {TK_OPTION_FONT, "-font", "font", "Font", DEF_LEGEND_FONT, -1, offsetof(Legend, style.font), 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_STYLE_CHANGED},
    {TK_OPTION_SYNONYM, "-fg", NULL, NULL, NULL, -1, -1, 0, "-foreground", 0},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground", DEF_LEGEND_FOREGROUND, -1,
     offsetof(Legend, style.color), 0, DEF_LEGEND_FG_MONO, LEGEND_REDRAW_CHANGED | LEGEND_STYLE_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_LEGEND_HIDE, -1, offsetof(Legend, hidden), 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED},
    {TK_OPTION_STRING, "-ipadx", "iPadX", "Pad", DEF_LEGEND_IPAD_X, offsetof(Legend, ipadXObjPtr), -1, 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_PADDING_CHANGED},
    {TK_OPTION_STRING, "-ipady", "iPadY", "Pad", DEF_LEGEND_IPAD_Y, offsetof(Legend, ipadYObjPtr), -1, 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_PADDING_CHANGED},
    {TK_OPTION_STRING, "-padx", "padX", "Pad", DEF_LEGEND_PAD_X, offsetof(Legend, padXObjPtr), -1, 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_PADDING_CHANGED},
    {TK_OPTION_STRING, "-pady", "padY", "Pad", DEF_LEGEND_PAD_Y, offsetof(Legend, padYObjPtr), -1, 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_PADDING_CHANGED},
    {TK_OPTION_STRING, "-position", "position", "Position", DEF_LEGEND_POSITION, offsetof(Legend, positionObjPtr), -1,
     0, NULL, LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_POSITION_CHANGED},
    {TK_OPTION_BOOLEAN, "-raised", "raised", "Raised", DEF_LEGEND_RAISED, -1, offsetof(Legend, raised), 0, NULL,
     LEGEND_REDRAW_CHANGED},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief", DEF_LEGEND_RELIEF, -1, offsetof(Legend, relief), 0, NULL,
     LEGEND_REDRAW_CHANGED},
    {TK_OPTION_INT, "-rows", "rows", "Rows", DEF_LEGEND_ROWS, offsetof(Legend, rowsObjPtr), offsetof(Legend, reqRows),
     0, NULL, LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_VALIDATE_CHANGED},
    {TK_OPTION_STRING, "-shadow", "shadow", "Shadow", DEF_LEGEND_SHADOW_COLOR, offsetof(Legend, shadowObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_SHADOW_CHANGED},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

static Tcl_IdleProc DisplayLegend;
static Rbc_BindPickProc PickLegendEntry;
static Tk_EventProc LegendEventProc;

static void EventuallyRedrawLegend(Legend *legendPtr);
static void SetLegendOrigin(Legend *legendPtr);
static int ConfigureLegend(Graph *graphPtr, Legend *legendPtr, int mask);

typedef int(RbcGrLegdOp)(Graph *, Tcl_Interp *, int, Tcl_Obj *const[]);
typedef RbcGrLegdOp *RbcGrLegdOpPtr;
static RbcGrLegdOp GetOp;
static RbcGrLegdOp ActivateOp;
static RbcGrLegdOp BindOp;
static RbcGrLegdOp CgetOp;
static RbcGrLegdOp ConfigureOp;

static int IsLegendPositionPrefix(const char *string, Tcl_Size length, const char *fullName) {
    Tcl_Size fullLength;

    fullLength = (Tcl_Size)strlen(fullName);
    return ((length > 0) && (length <= fullLength) && (strncmp(string, fullName, (size_t)length) == 0));
}

static int GetLegendPositionFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, LegendPosition *positionPtr) {
    const char *string;
    Tcl_Size length;

    string = Tcl_GetStringFromObj(objPtr, &length);
    positionPtr->site = LEGEND_RIGHT;
    positionPtr->anchorPos.x = -SHRT_MAX;
    positionPtr->anchorPos.y = -SHRT_MAX;
    positionPtr->windowName = NULL;
    if (length == 0) {
        return TCL_OK;
    }
    if (IsLegendPositionPrefix(string, length, "leftmargin")) {
        positionPtr->site = LEGEND_LEFT;
        return TCL_OK;
    }
    if (IsLegendPositionPrefix(string, length, "rightmargin")) {
        positionPtr->site = LEGEND_RIGHT;
        return TCL_OK;
    }
    if (IsLegendPositionPrefix(string, length, "topmargin")) {
        positionPtr->site = LEGEND_TOP;
        return TCL_OK;
    }
    if (IsLegendPositionPrefix(string, length, "bottommargin")) {
        positionPtr->site = LEGEND_BOTTOM;
        return TCL_OK;
    }
    if (IsLegendPositionPrefix(string, length, "plotarea")) {
        positionPtr->site = LEGEND_PLOT;
        return TCL_OK;
    }
    if (string[0] == '.') {
        positionPtr->site = LEGEND_WINDOW;
        positionPtr->windowName = string;
        return TCL_OK;
    }
    if (string[0] == '@') {
        const char *comma;
        Tcl_Obj *xObjPtr;
        Tcl_Obj *yObjPtr;
        Tcl_Size xLength;
        Tcl_Size yLength;
        long x, y;
        int result;
        comma = memchr(string + 1, ',', (size_t)(length - 1));
        if ((comma == NULL) || (comma == string + 1) || (comma == string + length - 1)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad screen position \"%s\": "
                                                   "should be @x,y",
                                                   string));
            return TCL_ERROR;
        }
        xLength = (Tcl_Size)(comma - (string + 1));
        yLength = length - (Tcl_Size)(comma - string) - 1;
        xObjPtr = Tcl_NewStringObj(string + 1, xLength);
        yObjPtr = Tcl_NewStringObj(comma + 1, yLength);
        Tcl_IncrRefCount(xObjPtr);
        Tcl_IncrRefCount(yObjPtr);
        result = Tcl_ExprLongObj(interp, xObjPtr, &x);
        if (result == TCL_OK) {
            result = Tcl_ExprLongObj(interp, yObjPtr, &y);
        }
        Tcl_DecrRefCount(xObjPtr);
        Tcl_DecrRefCount(yObjPtr);
        if (result != TCL_OK) {
            return TCL_ERROR;
        }
        positionPtr->site = LEGEND_XY;
        positionPtr->anchorPos.x = (double)x;
        positionPtr->anchorPos.y = (double)y;
        return TCL_OK;
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad position \"%s\": should be "
                                           "\"leftmargin\", \"rightmargin\", "
                                           "\"topmargin\", \"bottommargin\", "
                                           "\"plotarea\", .window, or @x,y",
                                           string));
    return TCL_ERROR;
}

/*
 *--------------------------------------------------------------
 *
 * EventuallyRedrawLegend --
 *
 *      Tells the Tk dispatcher to call the graph display routine at
 *      the next idle point.  This request is made only if the window
 *      is displayed and no other redraw request is pending.
 *
 * Parameters:
 *      Legend *legendPtr - Legend record
 *
 * Results:
 *       None.
 *
 * Side effects:
 *      The window is eventually redisplayed.
 *
 *--------------------------------------------------------------
 */
static void EventuallyRedrawLegend(Legend *legendPtr) {
    if ((legendPtr->tkwin != NULL) && !(legendPtr->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(DisplayLegend, legendPtr);
        legendPtr->flags |= REDRAW_PENDING;
    }
}

static int PrepareLegendWindow(Tcl_Interp *interp, Legend *legendPtr, const char *pathName, Tk_Window *tkwinPtr,
                               Tcl_Command *cmdTokenPtr) {
    Tk_Window mainWindow;
    Tk_Window tkwin;
    Tcl_Command cmdToken;
    
    mainWindow = Tk_MainWindow(interp);
    tkwin = Tk_CreateWindowFromPath(interp, mainWindow, pathName, NULL);
    if (tkwin == NULL) {
        return TCL_ERROR;
    }
    Rbc_SetWindowInstanceData(tkwin, legendPtr);
    Tk_CreateEventHandler(tkwin, ExposureMask | StructureNotifyMask, LegendEventProc, legendPtr);
    cmdToken = Tcl_CreateObjCommand(interp, pathName, Rbc_GraphInstCmdProc, legendPtr->graphPtr, NULL);
    if (cmdToken == NULL) {
        Tk_DeleteEventHandler(tkwin, ExposureMask | StructureNotifyMask, LegendEventProc, legendPtr);
        Rbc_DeleteWindowInstanceData(tkwin);
        Tk_DestroyWindow(tkwin);
        return TCL_ERROR;
    }
    *tkwinPtr = tkwin;
    *cmdTokenPtr = cmdToken;
    return TCL_OK;
}

static void CloseExternalLegendWindow(Legend *legendPtr) {
    Graph *graphPtr;
    Tk_Window oldWindow;
    Tcl_Command oldCommand;

    graphPtr = legendPtr->graphPtr;
    if ((legendPtr->tkwin == NULL) || (legendPtr->tkwin == graphPtr->tkwin)) {
        return;
    }
    if (legendPtr->flags & REDRAW_PENDING) {
        Tcl_CancelIdleCall(DisplayLegend, legendPtr);
        legendPtr->flags &= ~REDRAW_PENDING;
    }
    oldWindow = legendPtr->tkwin;
    oldCommand = legendPtr->cmdToken;
    /*
     * Change the record before destroying the old window so that
     * no callback can mistake it for the active legend window.
     */
    legendPtr->tkwin = graphPtr->tkwin;
    legendPtr->cmdToken = NULL;
    Tk_DeleteEventHandler(oldWindow, ExposureMask | StructureNotifyMask, LegendEventProc, legendPtr);
    Rbc_DeleteWindowInstanceData(oldWindow);
    if (oldCommand != NULL) {
        Tcl_DeleteCommandFromToken(graphPtr->interp, oldCommand);
    }
    Tk_DestroyWindow(oldWindow);
    Rbc_MoveBindingTable(legendPtr->bindTable, graphPtr->tkwin);
}

static void SetLegendPositionObject(Legend *legendPtr, const char *value) {
    Tcl_Obj *newObjPtr;
    Tcl_Obj *oldObjPtr;

    newObjPtr = Tcl_NewStringObj(value, -1);
    Tcl_IncrRefCount(newObjPtr);
    oldObjPtr = legendPtr->positionObjPtr;
    legendPtr->positionObjPtr = newObjPtr;
    if (oldObjPtr != NULL) {
        Tcl_DecrRefCount(oldObjPtr);
    }
}

static void ReleaseLegendTkResources(Graph *graphPtr, Legend *legendPtr) {
    if ((legendPtr == NULL) || (!legendPtr->optionsInitialized) || (legendPtr->tkResourcesReleased)) {
        return;
    }

    if (legendPtr->flags & REDRAW_PENDING) {
        Tcl_CancelIdleCall(DisplayLegend, legendPtr);

        legendPtr->flags &= ~REDRAW_PENDING;
    }

    /*
     * Release the GC before Tk frees the font, colours, and borders
     * referenced by the text style.
     */
    Rbc_FreeTextStyle(graphPtr->display, &legendPtr->style);

    legendPtr->style.gc = NULL;

    /*
     * The shadow colour was allocated by Rbc_GetShadowFromObj(),
     * not by Tk's option table.
     */
    if (legendPtr->style.shadow.color != NULL) {
        Tk_FreeColor(legendPtr->style.shadow.color);

        legendPtr->style.shadow.color = NULL;
    }

    Tk_FreeConfigOptions((char *)legendPtr, legendPtr->optionTable, graphPtr->tkwin);

    legendPtr->tkResourcesReleased = TRUE;

    /*
     * Do not retain the graph's Tk_Window after it is destroyed.
     * An external legend window remains valid.
     */
    if ((legendPtr->site != LEGEND_WINDOW) && (legendPtr->tkwin == graphPtr->tkwin)) {
        legendPtr->tkwin = NULL;
    }
}

void Rbc_ReleaseLegendTkResources(Graph *graphPtr) {
    if (graphPtr->legend != NULL) {
        ReleaseLegendTkResources(graphPtr, graphPtr->legend);
    }
}
/*
 *--------------------------------------------------------------
 *
 * LegendEventProc --
 *
 *      This procedure is invoked by the Tk dispatcher for various
 *      events on graphs.
 *
 * Parameters:
 *      ClientData clientData - Legend record
 *      register XEvent *eventPtr - Event which triggered call to routine
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      When the window gets deleted, internal structures get
 *      cleaned up.  When it gets exposed, the graph is eventually
 *      redisplayed.
 *
 *--------------------------------------------------------------
 */
static void LegendEventProc(ClientData clientData, register XEvent *eventPtr) {
    Legend *legendPtr = clientData;

    if (eventPtr->type == Expose) {
        if (eventPtr->xexpose.count == 0) {
            EventuallyRedrawLegend(legendPtr);
        }
    } else if (eventPtr->type == DestroyNotify) {
        Graph *graphPtr;

        graphPtr = legendPtr->graphPtr;
        if (legendPtr->tkwin != graphPtr->tkwin) {
            Rbc_DeleteWindowInstanceData(legendPtr->tkwin);
            if (legendPtr->cmdToken != NULL) {
                Tcl_DeleteCommandFromToken(graphPtr->interp, legendPtr->cmdToken);
                legendPtr->cmdToken = NULL;
            }
            legendPtr->tkwin = graphPtr->tkwin;
        }
        if (legendPtr->flags & REDRAW_PENDING) {
            Tcl_CancelIdleCall(DisplayLegend, legendPtr);
            legendPtr->flags &= ~REDRAW_PENDING;
        }
        legendPtr->site = LEGEND_RIGHT;
        SetLegendPositionObject(legendPtr, "rightmargin");
        graphPtr->flags |= MAP_WORLD | REDRAW_WORLD | REDRAW_BACKING_STORE;
        Rbc_MoveBindingTable(legendPtr->bindTable, graphPtr->tkwin);
        Rbc_EventuallyRedrawGraph(graphPtr);
    } else if (eventPtr->type == ConfigureNotify) {
        EventuallyRedrawLegend(legendPtr);
    }
}



/*
 *----------------------------------------------------------------------
 *
 * SetLegendOrigin --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void SetLegendOrigin(Legend *legendPtr) {
    Graph *graphPtr;
    int x, y, width, height;

    graphPtr = legendPtr->graphPtr;
    x = y = width = height = 0; /* Suppress compiler warning. */
    switch (legendPtr->site) {
    case LEGEND_RIGHT:
        width = graphPtr->rightMargin.width - graphPtr->rightMargin.axesOffset;
        height = graphPtr->bottom - graphPtr->top;
        x = graphPtr->width - (width + graphPtr->inset);
        y = graphPtr->top;
        break;
    case LEGEND_LEFT:
        width = graphPtr->leftMargin.width - graphPtr->leftMargin.axesOffset;
        height = graphPtr->bottom - graphPtr->top;
        x = graphPtr->inset;
        y = graphPtr->top;
        break;
    case LEGEND_TOP:
        width = graphPtr->right - graphPtr->left;
        height = graphPtr->topMargin.height - graphPtr->topMargin.axesOffset;
        if (graphPtr->title != NULL) {
            height -= graphPtr->titleTextStyle.height;
        }
        x = graphPtr->left;
        y = graphPtr->inset;
        if (graphPtr->title != NULL) {
            y += graphPtr->titleTextStyle.height;
        }
        break;
    case LEGEND_BOTTOM:
        width = graphPtr->right - graphPtr->left;
        height = graphPtr->bottomMargin.height - graphPtr->bottomMargin.axesOffset;
        x = graphPtr->left;
        y = graphPtr->height - (height + graphPtr->inset);
        break;
    case LEGEND_PLOT:
        width = graphPtr->right - graphPtr->left;
        height = graphPtr->bottom - graphPtr->top;
        x = graphPtr->left;
        y = graphPtr->top;
        break;
    case LEGEND_XY:
        width = legendPtr->width;
        height = legendPtr->height;
        x = (int)legendPtr->anchorPos.x;
        y = (int)legendPtr->anchorPos.y;
        if (x < 0) {
            x += graphPtr->width;
        }
        if (y < 0) {
            y += graphPtr->height;
        }
        break;
    case LEGEND_WINDOW:
        legendPtr->anchor = TK_ANCHOR_NW;
        legendPtr->x = legendPtr->y = 0;
        return;
    }
    width = legendPtr->width - width;
    height = legendPtr->height - height;
    Rbc_TranslateAnchor(x, y, width, height, legendPtr->anchor, &x, &y);

    legendPtr->x = x + legendPtr->padLeft;
    legendPtr->y = y + legendPtr->padTop;
}

/*
 *----------------------------------------------------------------------
 *
 * PickLegendEntry --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      int x
 *      int y - Point to be tested
 *      ClientData *contextPtr - Not used.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static ClientData PickLegendEntry(ClientData clientData, int x, int y, ClientData *contextPtr) {
    Graph *graphPtr = clientData;
    Legend *legendPtr;
    int width, height;

    legendPtr = graphPtr->legend;
    width = legendPtr->width;
    height = legendPtr->height;

    x -= legendPtr->x + legendPtr->borderWidth;
    y -= legendPtr->y + legendPtr->borderWidth;
    width -= 2 * legendPtr->borderWidth + PADDING(legendPtr->padX);
    height -= 2 * legendPtr->borderWidth + PADDING(legendPtr->padY);

    if ((x >= 0) && (x < width) && (y >= 0) && (y < height)) {
        int row, column;
        int n;

        /*
         * It's in the bounding box, so compute the index.
         */
        row = y / legendPtr->style.height;
        column = x / legendPtr->style.width;
        n = (column * legendPtr->nRows) + row;
        if (n < legendPtr->nEntries) {
            Rbc_ChainLink *linkPtr;
            Element *elemPtr;
            int count;

            /* Legend entries are stored in reverse. */
            count = 0;
            for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
                 linkPtr = Rbc_ChainPrevLink(linkPtr)) {
                elemPtr = Rbc_ChainGetValue(linkPtr);
                if (elemPtr->label != NULL) {
                    if (count == n) {
                        return elemPtr;
                    }
                    count++;
                }
            }
            if (linkPtr != NULL) {
                return Rbc_ChainGetValue(linkPtr);
            }
        }
    }
    return NULL;
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_MapLegend --
 *
 *      Calculates the dimensions (width and height) needed for
 *      the legend.  Also determines the number of rows and columns
 *      necessary to list all the valid element labels.
 *
 * Parameters:
 *      Legend *legendPtr
 *      int plotWidth - Maximum width available in window to draw the legend. Will calculate number of columns from this.
 *                          
 *      int plotHeight - Maximum height available in window to draw the legend. Will calculate number of rows from this.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The following fields of the legend are calculated and set.
 *
 *      nEntries   - number of valid labels of elements in the
 *                display list.
 *      nRows        - number of rows of entries
 *      nColumns    - number of columns of entries
 *      style.height - height of each entry
 *      style.width  - width of each entry
 *      height        - width of legend (includes borders and padding)
 *      width        - height of legend (includes borders and padding)
 *
 * -----------------------------------------------------------------
 */
void Rbc_MapLegend(Legend *legendPtr, int plotWidth, int plotHeight) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;
    int nRows, nColumns, nEntries;
    int legendWidth, legendHeight;
    int entryWidth, entryHeight;
    int symbolWidth;
    Tk_FontMetrics fontMetrics;

    /* Initialize legend values to default (no legend displayed) */

    legendPtr->style.width = legendPtr->style.height = 0;
    legendPtr->nRows = legendPtr->nColumns = 0;
    legendPtr->nEntries = 0;
    legendPtr->height = legendPtr->width = 0;

    if (legendPtr->site == LEGEND_WINDOW) {
        if (Tk_Width(legendPtr->tkwin) > 1) {
            plotWidth = Tk_Width(legendPtr->tkwin);
        }
        if (Tk_Height(legendPtr->tkwin) > 1) {
            plotHeight = Tk_Height(legendPtr->tkwin);
        }
    }
    if ((legendPtr->hidden) || (plotWidth < 1) || (plotHeight < 1)) {
        return; /* Legend is not being displayed */
    }

    /*
     * Count the number of legend entries and determine the widest and
     * tallest label.  The number of entries would normally be the
     * number of elements, but 1) elements can be hidden and 2)
     * elements can have no legend entry (-label "").
     */
    nEntries = 0;
    entryWidth = entryHeight = 0;
    for (linkPtr = Rbc_ChainLastLink(legendPtr->graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainPrevLink(linkPtr)) {
        int width, height;

        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->label == NULL) {
            continue; /* Element has no legend entry. */
        }
        Rbc_GetTextExtents(&legendPtr->style, elemPtr->label, &width, &height);
        if (entryWidth < width) {
            entryWidth = width;
        }
        if (entryHeight < height) {
            entryHeight = height;
        }
        nEntries++;
    }

    if (nEntries == 0) {
        return; /* No legend entries. */
    }

    Tk_GetFontMetrics(legendPtr->style.font, &fontMetrics);
    symbolWidth = 2 * fontMetrics.ascent;

    entryWidth += 2 * legendPtr->entryBorderWidth + PADDING(legendPtr->ipadX) + 5 + symbolWidth;
    entryHeight += 2 * legendPtr->entryBorderWidth + PADDING(legendPtr->ipadY);

    legendWidth = plotWidth - 2 * legendPtr->borderWidth - PADDING(legendPtr->padX);
    legendHeight = plotHeight - 2 * legendPtr->borderWidth - PADDING(legendPtr->padY);

    /*
     * The number of rows and columns is computed as one of the following:
     *
     *    both options set        User defined.
     *  -rows                Compute columns from rows.
     *  -columns            Compute rows from columns.
     *    neither set            Compute rows and columns from
     *                    size of plot.
     */
    if (legendPtr->reqRows > 0) {
        nRows = legendPtr->reqRows;
        if (nRows > nEntries) {
            nRows = nEntries;
        }
        if (legendPtr->reqColumns > 0) {
            nColumns = legendPtr->reqColumns;
            if (nColumns > nEntries) {
                nColumns = nEntries; /* Both -rows, -columns set. */
            }
        } else {
            nColumns = ((nEntries - 1) / nRows) + 1; /* Only -rows. */
        }
    } else if (legendPtr->reqColumns > 0) { /* Only -columns. */
        nColumns = legendPtr->reqColumns;
        if (nColumns > nEntries) {
            nColumns = nEntries;
        }
        nRows = ((nEntries - 1) / nColumns) + 1;
    } else {
        /* Compute # of rows and columns from the legend size. */
        nRows = legendHeight / entryHeight;
        nColumns = legendWidth / entryWidth;

        if (nRows > nEntries) {
            nRows = nEntries;
        } else if (nRows < 1) {
            nRows = 1;
        }
        if (nColumns > nEntries) {
            nColumns = nEntries;
        } else if (nColumns < 1) {
            nColumns = 1;
        }
        if ((legendPtr->site == LEGEND_TOP) || (legendPtr->site == LEGEND_BOTTOM)) {
            nRows = ((nEntries - 1) / nColumns) + 1;
        } else {
            nColumns = ((nEntries - 1) / nRows) + 1;
        }
    }
    if (nRows < 1) {
        nRows = 1;
    }
    if (nColumns < 1) {
        nColumns = 1;
    }
    legendWidth = 2 * legendPtr->borderWidth + PADDING(legendPtr->padX);
    legendHeight = 2 * legendPtr->borderWidth + PADDING(legendPtr->padY);
    legendHeight += nRows * entryHeight;
    legendWidth += nColumns * entryWidth;

    legendPtr->height = legendHeight;
    legendPtr->width = legendWidth;
    legendPtr->nRows = nRows;
    legendPtr->nColumns = nColumns;
    legendPtr->nEntries = nEntries;
    legendPtr->style.height = entryHeight;
    legendPtr->style.width = entryWidth;

    if ((legendPtr->tkwin != legendPtr->graphPtr->tkwin) &&
        ((Tk_ReqWidth(legendPtr->tkwin) != legendWidth) || (Tk_ReqHeight(legendPtr->tkwin) != legendHeight))) {
        Tk_GeometryRequest(legendPtr->tkwin, legendWidth, legendHeight);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DrawLegend --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_DrawLegend(Legend *legendPtr, Drawable drawable) {
    Graph *graphPtr;
    Rbc_ChainLink *linkPtr;
    Pixmap pixmap;
    Tk_3DBorder border;
    Tk_FontMetrics fontMetrics;
    Tk_Window tkwin;
    int count;
    int labelX, startY, symbolX, symbolY;
    int symbolSize, midX, midY;
    int width, height;
    int x, y;
    register Element *elemPtr;

    graphPtr = legendPtr->graphPtr;
    graphPtr->flags &= ~DRAW_LEGEND;
    if ((legendPtr->hidden) || (legendPtr->nEntries == 0)) {
        return;
    }
    SetLegendOrigin(legendPtr);

    if (legendPtr->tkwin != graphPtr->tkwin) {
        tkwin = legendPtr->tkwin;
        width = Tk_Width(tkwin);
        if (width < 1) {
            width = legendPtr->width;
        }
        height = Tk_Height(tkwin);
        if (height < 1) {
            height = legendPtr->height;
        }
    } else {
        width = legendPtr->width;
        height = legendPtr->height;
    }
    Tk_GetFontMetrics(legendPtr->style.font, &fontMetrics);

    symbolSize = fontMetrics.ascent;
    midX = symbolSize + 1 + legendPtr->entryBorderWidth;
    midY = (symbolSize / 2) + 1 + legendPtr->entryBorderWidth;
    labelX = 2 * symbolSize + legendPtr->entryBorderWidth + legendPtr->ipadX.side1 + 5;
    symbolY = midY + legendPtr->ipadY.side1;
    symbolX = midX + legendPtr->ipadX.side1;

    pixmap = Tk_GetPixmap(graphPtr->display, Tk_WindowId(legendPtr->tkwin), width, height, Tk_Depth(legendPtr->tkwin));

    if (legendPtr->border != NULL) {
        /* Background color and relief. */
        Rbc_Fill3DRectangle(legendPtr->tkwin, pixmap, legendPtr->border, 0, 0, width, height, 0, TK_RELIEF_FLAT);
    } else if (legendPtr->site & LEGEND_IN_PLOT) {
        /*
         * Legend background is transparent and is positioned over the
         * the plot area.  Either copy the part of the background from
         * the backing store pixmap or (if no backing store exists)
         * just fill it with the background color of the plot.
         */
        if (graphPtr->backPixmap != None) {
            XCopyArea(graphPtr->display, graphPtr->backPixmap, pixmap, graphPtr->drawGC, legendPtr->x, legendPtr->y,
                      width, height, 0, 0);
        } else {
            XFillRectangle(graphPtr->display, pixmap, graphPtr->plotFillGC, 0, 0, width, height);
        }
    } else {
        /*
         * The legend is positioned in one of the margins or the
         * external window.  Draw either the solid or tiled background
         * with the the border.
         */
        if (graphPtr->tile != NULL) {
            Rbc_SetTileOrigin(legendPtr->tkwin, graphPtr->tile, legendPtr->x, legendPtr->y);
            Rbc_TileRectangle(legendPtr->tkwin, pixmap, graphPtr->tile, 0, 0, width, height);
        } else {
            XFillRectangle(graphPtr->display, pixmap, graphPtr->fillGC, 0, 0, width, height);
        }
    }
    x = legendPtr->padLeft + legendPtr->borderWidth;
    y = legendPtr->padTop + legendPtr->borderWidth;
    count = 0;
    startY = y;
    for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainPrevLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->label == NULL) {
            continue; /* Skip this entry */
        }
        if (elemPtr->flags & LABEL_ACTIVE) {
            legendPtr->style.state |= STATE_ACTIVE;
            Rbc_Fill3DRectangle(legendPtr->tkwin, pixmap, legendPtr->activeBorder, x, y, legendPtr->style.width,
                                legendPtr->style.height, legendPtr->entryBorderWidth, legendPtr->activeRelief);
        } else {
            legendPtr->style.state &= ~STATE_ACTIVE;
            if (elemPtr->labelRelief != TK_RELIEF_FLAT) {
                Rbc_Draw3DRectangle(legendPtr->tkwin, pixmap, graphPtr->border, x, y, legendPtr->style.width,
                                    legendPtr->style.height, legendPtr->entryBorderWidth, elemPtr->labelRelief);
            }
        }
        (*elemPtr->procsPtr->drawSymbolProc)(graphPtr, pixmap, elemPtr, x + symbolX, y + symbolY, symbolSize);
        Rbc_DrawText(legendPtr->tkwin, pixmap, elemPtr->label, &legendPtr->style, x + labelX,
                     y + legendPtr->entryBorderWidth + legendPtr->ipadY.side1);
        count++;

        /* Check when to move to the next column */
        if ((count % legendPtr->nRows) > 0) {
            y += legendPtr->style.height;
        } else {
            x += legendPtr->style.width;
            y = startY;
        }
    }
    /*
     * Draw the border and/or background of the legend.
     */
    border = legendPtr->border;
    if (border == NULL) {
        border = graphPtr->border;
    }
    Rbc_Draw3DRectangle(legendPtr->tkwin, pixmap, border, 0, 0, width, height, legendPtr->borderWidth,
                        legendPtr->relief);

    XCopyArea(graphPtr->display, pixmap, drawable, graphPtr->drawGC, 0, 0, width, height, legendPtr->x, legendPtr->y);
    Tk_FreePixmap(graphPtr->display, pixmap);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *      PsToken psToken
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_LegendToPostScript(Legend *legendPtr, PsToken psToken) {
    Graph *graphPtr;
    double x, y, startY;
    Element *elemPtr;
    int labelX, symbolX, symbolY;
    int count;
    Rbc_ChainLink *linkPtr;
    int symbolSize, midX, midY;
    int width, height;
    Tk_FontMetrics fontMetrics;

    if ((legendPtr->hidden) || (legendPtr->nEntries == 0)) {
        return;
    }
    SetLegendOrigin(legendPtr);

    x = legendPtr->x, y = legendPtr->y;
    width = legendPtr->width - PADDING(legendPtr->padX);
    height = legendPtr->height - PADDING(legendPtr->padY);

    graphPtr = legendPtr->graphPtr;
    if (graphPtr->postscript->decorations) {
        if (legendPtr->border != NULL) {
            Rbc_Fill3DRectangleToPostScript(psToken, legendPtr->border, x, y, width, height, legendPtr->borderWidth,
                                            legendPtr->relief);
        } else {
            Rbc_Draw3DRectangleToPostScript(psToken, graphPtr->border, x, y, width, height, legendPtr->borderWidth,
                                            legendPtr->relief);
        }
    } else {
        Rbc_ClearBackgroundToPostScript(psToken);
        Rbc_RectangleToPostScript(psToken, x, y, width, height);
    }
    x += legendPtr->borderWidth;
    y += legendPtr->borderWidth;

    Tk_GetFontMetrics(legendPtr->style.font, &fontMetrics);
    symbolSize = fontMetrics.ascent;
    midX = symbolSize + 1 + legendPtr->entryBorderWidth;
    midY = (symbolSize / 2) + 1 + legendPtr->entryBorderWidth;
    labelX = 2 * symbolSize + legendPtr->entryBorderWidth + legendPtr->ipadX.side1 + 5;
    symbolY = midY + legendPtr->ipadY.side1;
    symbolX = midX + legendPtr->ipadX.side1;

    count = 0;
    startY = y;
    for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainPrevLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->label == NULL) {
            continue; /* Skip this label */
        }
        if (elemPtr->flags & LABEL_ACTIVE) {
            legendPtr->style.state |= STATE_ACTIVE;
            Rbc_Fill3DRectangleToPostScript(psToken, legendPtr->activeBorder, x, y, legendPtr->style.width,
                                            legendPtr->style.height, legendPtr->entryBorderWidth,
                                            legendPtr->activeRelief);
        } else {
            legendPtr->style.state &= ~STATE_ACTIVE;
            if (elemPtr->labelRelief != TK_RELIEF_FLAT) {
                Rbc_Draw3DRectangleToPostScript(psToken, graphPtr->border, x, y, legendPtr->style.width,
                                                legendPtr->style.height, legendPtr->entryBorderWidth,
                                                elemPtr->labelRelief);
            }
        }
        (*elemPtr->procsPtr->printSymbolProc)(graphPtr, psToken, elemPtr, x + symbolX, y + symbolY, symbolSize);
        Rbc_TextToPostScript(psToken, elemPtr->label, &(legendPtr->style), x + labelX,
                             y + legendPtr->entryBorderWidth + legendPtr->ipadY.side1);
        count++;
        if ((count % legendPtr->nRows) > 0) {
            y += legendPtr->style.height;
        } else {
            x += legendPtr->style.width;
            y = startY;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DisplayLegend --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DisplayLegend(ClientData clientData) {
    Legend *legendPtr = clientData;
    int width, height;

    legendPtr->flags &= ~REDRAW_PENDING;

    if (legendPtr->tkwin == NULL) {
        return; /* Window has been destroyed. */
    }
    if (legendPtr->site == LEGEND_WINDOW) {
        width = Tk_Width(legendPtr->tkwin);
        height = Tk_Height(legendPtr->tkwin);
        if ((width <= 1) || (height <= 1)) {
            return;
        }
        if ((width != legendPtr->width) || (height != legendPtr->height)) {
            Rbc_MapLegend(legendPtr, width, height);
        }
    }
    if (!Tk_IsMapped(legendPtr->tkwin)) {
        return;
    }
    Rbc_DrawLegend(legendPtr, Tk_WindowId(legendPtr->tkwin));
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureLegend --
 *
 *      Routine to configure the legend.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Legend *legendPtr
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      Graph will be redrawn to reflect the new legend attributes.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureLegend(Graph *graphPtr, Legend *legendPtr, int mask) {
    Rbc_Pad newIpadX;
    Rbc_Pad newIpadY;
    Rbc_Pad newPadX;
    Rbc_Pad newPadY;

    Shadow newShadow;
    LegendPosition newPosition;

    Tk_Window preparedWindow;
    Tcl_Command preparedCommand;

    int shadowChanged;
    int positionChanged;

    newIpadX = legendPtr->ipadX;
    newIpadY = legendPtr->ipadY;
    newPadX = legendPtr->padX;
    newPadY = legendPtr->padY;
    newShadow.color = NULL;
    newShadow.offset = 0;
    preparedWindow = NULL;
    preparedCommand = NULL;
    shadowChanged = ((mask & LEGEND_SHADOW_CHANGED) != 0);
    positionChanged = ((mask & LEGEND_POSITION_CHANGED) != 0);

    /*
     * Validate Tk-parsed integer and pixel values.
     */
    if (mask & LEGEND_VALIDATE_CHANGED) {
        if (legendPtr->entryBorderWidth < 0) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("active border width can't be negative", -1));
            return TCL_ERROR;
        }
        if (legendPtr->borderWidth < 0) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("border width can't be negative", -1));
            return TCL_ERROR;
        }
        if (legendPtr->reqColumns < 0) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("number of columns can't be negative", -1));

            return TCL_ERROR;
        }
        if (legendPtr->reqRows < 0) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("number of rows can't be negative", -1));
            return TCL_ERROR;
        }
    }

    /*
     * Parse all derived values into temporary storage.
     */
    if (mask & LEGEND_PADDING_CHANGED) {
        if (Rbc_GetPadFromObj(graphPtr->interp, graphPtr->tkwin, legendPtr->ipadXObjPtr, &newIpadX) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPadFromObj(graphPtr->interp, graphPtr->tkwin, legendPtr->ipadYObjPtr, &newIpadY) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPadFromObj(graphPtr->interp, graphPtr->tkwin, legendPtr->padXObjPtr, &newPadX) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPadFromObj(graphPtr->interp, graphPtr->tkwin, legendPtr->padYObjPtr, &newPadY) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (shadowChanged) {
        if (Rbc_GetShadowFromObj(graphPtr->interp, graphPtr->tkwin, legendPtr->shadowObjPtr, &newShadow) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (positionChanged) {
        if (GetLegendPositionFromObj(graphPtr->interp, legendPtr->positionObjPtr, &newPosition) != TCL_OK) {
            goto error;
        }
        if (newPosition.site == LEGEND_WINDOW) {
            int sameWindow;
            sameWindow = ((legendPtr->tkwin != graphPtr->tkwin) &&
                          (strcmp(Tk_PathName(legendPtr->tkwin), newPosition.windowName) == 0));
            if (!sameWindow) {
                if (PrepareLegendWindow(graphPtr->interp, legendPtr, newPosition.windowName, &preparedWindow,
                                        &preparedCommand) != TCL_OK) {
                    goto error;
                }
            }
        }
    }

    /*
     * All fallible operations have succeeded. Commit derived values.
     */
    if (mask & LEGEND_PADDING_CHANGED) {
        legendPtr->ipadX = newIpadX;
        legendPtr->ipadY = newIpadY;
        legendPtr->padX = newPadX;
        legendPtr->padY = newPadY;
    }
    if (shadowChanged) {
        Shadow oldShadow;
        oldShadow = legendPtr->style.shadow;
        legendPtr->style.shadow = newShadow;
        newShadow.color = NULL;
        if (oldShadow.color != NULL) {
            Tk_FreeColor(oldShadow.color);
        }
    }
    if (positionChanged) {
        if (newPosition.site == LEGEND_WINDOW) {
            if (preparedWindow != NULL) {
                CloseExternalLegendWindow(legendPtr);
                legendPtr->tkwin = preparedWindow;
                legendPtr->cmdToken = preparedCommand;
                preparedWindow = NULL;
                preparedCommand = NULL;
                Rbc_MoveBindingTable(legendPtr->bindTable, legendPtr->tkwin);
            }
            legendPtr->site = LEGEND_WINDOW;
        } else {
            CloseExternalLegendWindow(legendPtr);
            legendPtr->site = newPosition.site;
            legendPtr->anchorPos = newPosition.anchorPos;
        }
    }
    if (mask & LEGEND_STYLE_CHANGED) {
        Rbc_ResetTextStyle(legendPtr->tkwin, &legendPtr->style);
    }
    /*
     * Position and size changes affect the graph layout even when
     * the new legend position is an external window.
     */
    if (mask & LEGEND_LAYOUT_CHANGED) {
        graphPtr->flags |= MAP_WORLD;
    }
    if (legendPtr->site == LEGEND_WINDOW) {
        EventuallyRedrawLegend(legendPtr);
        if (mask & LEGEND_POSITION_CHANGED) {
            graphPtr->flags |= REDRAW_WORLD | REDRAW_BACKING_STORE;
            Rbc_EventuallyRedrawGraph(graphPtr);
        }
    } else {
        graphPtr->flags |= REDRAW_WORLD | REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;

error:
    if (newShadow.color != NULL) {
        Tk_FreeColor(newShadow.color);
    }
    if (preparedWindow != NULL) {
        Tk_DeleteEventHandler(preparedWindow, ExposureMask | StructureNotifyMask, LegendEventProc, legendPtr);
        Rbc_DeleteWindowInstanceData(preparedWindow);
        if (preparedCommand != NULL) {
            Tcl_DeleteCommandFromToken(graphPtr->interp, preparedCommand);
        }
        Tk_DestroyWindow(preparedWindow);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DestroyLegend --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Resources associated with the legend are freed.
 *
 *----------------------------------------------------------------------
 */
void Rbc_DestroyLegend(Graph *graphPtr) {
    Legend *legendPtr;

    legendPtr = graphPtr->legend;
    if (legendPtr == NULL) {
        return;
    }
    graphPtr->legend = NULL;
    if (legendPtr->flags & REDRAW_PENDING) {
        Tcl_CancelIdleCall(DisplayLegend, legendPtr);

        legendPtr->flags &= ~REDRAW_PENDING;
    }

    /*
     * This normally did nothing during final destruction because
     * ReleaseLegendTkResources() already ran while tkwin was valid.
     * It still handles graph-creation failure, where tkwin remains
     * valid.
     */
    if ((!legendPtr->tkResourcesReleased) && (graphPtr->tkwin != NULL)) {
        ReleaseLegendTkResources(graphPtr, legendPtr);
    }
    if ((legendPtr->site == LEGEND_WINDOW) && (legendPtr->tkwin != NULL)) {
        Tk_Window externalWindow;
        Tcl_Command externalCommand;
        externalWindow = legendPtr->tkwin;
        externalCommand = legendPtr->cmdToken;
        legendPtr->tkwin = NULL;
        legendPtr->cmdToken = NULL;
        Tk_DeleteEventHandler(externalWindow, ExposureMask | StructureNotifyMask, LegendEventProc, legendPtr);
        Rbc_DeleteWindowInstanceData(externalWindow);
        if (externalCommand != NULL) {
            Tcl_DeleteCommandFromToken(graphPtr->interp, externalCommand);
        }
        Tk_DestroyWindow(externalWindow);
    }
    if (legendPtr->bindTable != NULL) {
        Rbc_DestroyBindingTable(legendPtr->bindTable);
    }
    ckfree((char *)legendPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CreateLegend --
 *
 *      Creates and initializes a legend structure with default settings
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_CreateLegend(Graph *graphPtr) {
    Legend *legendPtr;

    legendPtr = RbcCalloc(1, sizeof(Legend));
    assert(legendPtr != NULL);
    graphPtr->legend = legendPtr;
    legendPtr->graphPtr = graphPtr;
    legendPtr->tkwin = graphPtr->tkwin;
    Rbc_InitTextStyle(&legendPtr->style);
    legendPtr->style.justify = TK_JUSTIFY_LEFT;
    legendPtr->style.anchor = TK_ANCHOR_NW;
    legendPtr->bindTable =
        Rbc_CreateBindingTable(graphPtr->interp, graphPtr->tkwin, graphPtr, PickLegendEntry, Rbc_GraphTags);
    legendPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, legendOptionSpecs);
    if (Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, "legend", "Legend", (char *)legendPtr,
                                 legendPtr->optionTable) != TCL_OK) {
        goto error;
    }
    legendPtr->optionsInitialized = TRUE;
    if (ConfigureLegend(graphPtr, legendPtr, LEGEND_INITIALIZE_MASK) != TCL_OK) {
        goto error;
    }
    return TCL_OK;

error:
    Rbc_DestroyLegend(graphPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * GetOp --
 *
 *      Find the legend entry from the given argument.  The argument
 *      can be either a screen position "@x,y" or the name of an
 *      element.
 *
 *      I don't know how useful it is to test with the name of an
 *      element.
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
 *      Graph will be redrawn to reflect the new legend attributes.
 *
 *----------------------------------------------------------------------
 */
static int GetOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    register Element *elemPtr;
    Legend *legendPtr = graphPtr->legend;
    int x, y;
    const char *str = Tcl_GetString(objv[3]);
    char c = str[0];

    if ((legendPtr->hidden) || (legendPtr->nEntries == 0)) {
        return TCL_OK;
    }
    elemPtr = NULL;

    if ((c == 'c') && (strcmp(str, "current") == 0)) {
        elemPtr = (Element *)Rbc_GetCurrentItem(legendPtr->bindTable);
    } else if ((c == '@') && (Rbc_GetXY(interp, graphPtr->tkwin, str, &x, &y) == TCL_OK)) {
        elemPtr = (Element *)PickLegendEntry(graphPtr, x, y, NULL);
    }
    if (elemPtr != NULL) {
        Tcl_SetResult(interp, elemPtr->name, TCL_VOLATILE);
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ActivateOp --
 *
 *      Activates a particular label in the legend.
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
 *      Graph will be redrawn to reflect the new legend attributes.
 *
 *----------------------------------------------------------------------
 */
static int ActivateOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Legend *legendPtr = graphPtr->legend;
    Element *elemPtr;
    unsigned int active, redraw;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    register int i;
    const char *str = Tcl_GetString(objv[2]);
    Tcl_Obj *resultObj;

    active = (str[0] == 'a') ? LABEL_ACTIVE : 0;
    redraw = 0;
    for (hPtr = Tcl_FirstHashEntry(&(graphPtr->elements.table), &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = Tcl_GetHashValue(hPtr);
        for (i = 3; i < objc; i++) {
            str = Tcl_GetString(objv[i]);
            if (Tcl_StringMatch(elemPtr->name, str)) {
                break;
            }
        }
        if ((i < objc) && (active != (elemPtr->flags & LABEL_ACTIVE))) {
            elemPtr->flags ^= LABEL_ACTIVE;
            if (elemPtr->label != NULL) {
                redraw++;
            }
        }
    }
    if ((redraw) && (!legendPtr->hidden)) {
        /*
         * See if how much we need to draw. If the graph is already
         * schedule for a redraw, just make sure the right flags are
         * set.  Otherwise redraw only the legend: it's either in an
         * external window or it's the only thing that need updating.
         */
        if (graphPtr->flags & REDRAW_PENDING) {
            if (legendPtr->site & LEGEND_IN_PLOT) {
                graphPtr->flags |= REDRAW_BACKING_STORE;
            }
            graphPtr->flags |= REDRAW_WORLD; /* Redraw entire graph. */
        } else {
            EventuallyRedrawLegend(legendPtr);
        }
    }
    /* Return the names of all the active legend entries */
    resultObj = Tcl_NewListObj(0, NULL);
    for (hPtr = Tcl_FirstHashEntry(&(graphPtr->elements.table), &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = Tcl_GetHashValue(hPtr);
        if (elemPtr->flags & LABEL_ACTIVE) {
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(elemPtr->name, -1));
        }
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * BindOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int BindOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        Tcl_HashEntry *hPtr;
        Tcl_HashSearch cursor;
        char *tagName;
        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

        for (hPtr = Tcl_FirstHashEntry(&(graphPtr->elements.tagTable), &cursor); hPtr != NULL;
             hPtr = Tcl_NextHashEntry(&cursor)) {
            tagName = Tcl_GetHashKey(&(graphPtr->elements.tagTable), hPtr);
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(tagName, -1));
        }
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    return Rbc_ConfigureBindingsFromObj(interp, graphPtr->legend->bindTable,
                                        Rbc_MakeElementTag(graphPtr, Tcl_GetString(objv[3])), objc - 4, objv + 4);
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries or resets options for the legend.
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
 *      Graph will be redrawn to reflect the new legend attributes.
 *
 *----------------------------------------------------------------------
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Legend *legendPtr;
    Tcl_Obj *resultObjPtr;

    legendPtr = graphPtr->legend;
    resultObjPtr = Tk_GetOptionValue(interp, (char *)legendPtr, legendPtr->optionTable, objv[3], graphPtr->tkwin);
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
 *      Queries or resets options for the legend.
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
 *      Graph will be redrawn to reflect the new legend attributes.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Legend *legendPtr;
    Tcl_Obj *resultObjPtr;
    Tk_SavedOptions savedOptions;
    int mask;

    legendPtr = graphPtr->legend;
    if (objc == 3) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)legendPtr, legendPtr->optionTable, NULL, graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (objc == 4) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)legendPtr, legendPtr->optionTable, objv[3], graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (Tk_SetOptions(interp, (char *)legendPtr, legendPtr->optionTable, objc - 3, objv + 3, graphPtr->tkwin,
                      &savedOptions, &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    if (ConfigureLegend(graphPtr, legendPtr, mask) != TCL_OK) {
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

static Rbc_OpSpec legendOps[] = {{"activate", (Rbc_Op)ActivateOp, 3, 0, "?pattern?..."},
                                 {"bind", (Rbc_Op)BindOp, 3, 6, "elemName sequence command"},
                                 {"cget", (Rbc_Op)CgetOp, 4, 4, "option"},
                                 {"configure", (Rbc_Op)ConfigureOp, 3, 0, "?option value?..."},
                                 {"deactivate", (Rbc_Op)ActivateOp, 3, 0, "?pattern?..."},
                                 {"get", (Rbc_Op)GetOp, 4, 4, "index"},
                                 RBC_OPSPEC_END};

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendOp --
 *
 *      TODO: Description
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
 *      Legend is possibly redrawn.
 *
 *----------------------------------------------------------------------
 */
int Rbc_LegendOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    RbcGrLegdOpPtr proc;
    int result;

    proc = (RbcGrLegdOpPtr)Rbc_GetOpFromObj(interp, legendOps, RBC_OP_ARG2, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    result = (*proc)(graphPtr, interp, objc, objv);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendSite --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_LegendSite(Legend *legendPtr) { return legendPtr->site; }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendWidth --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_LegendWidth(Legend *legendPtr) { return legendPtr->width; }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendHeight --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_LegendHeight(Legend *legendPtr) { return legendPtr->height; }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendIsHidden --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_LegendIsHidden(Legend *legendPtr) { return legendPtr->hidden; }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendIsRaised --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_LegendIsRaised(Legend *legendPtr) { return legendPtr->raised; }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendX --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_LegendX(Legend *legendPtr) { return legendPtr->x; }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendY --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_LegendY(Legend *legendPtr) { return legendPtr->y; }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LegendRemoveElement --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Legend *legendPtr
 *      Element *elemPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_LegendRemoveElement(Legend *legendPtr, Element *elemPtr) { Rbc_DeleteBindings(legendPtr->bindTable, elemPtr); }
