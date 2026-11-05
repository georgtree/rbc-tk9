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

/** @file rbcGrLegd.c
 * Global, external and track-local legend implementation.
 */
/** @defgroup legend_layout Legend measurement and placement
 * Internal legend services; not part of the public stubs API.
 */
/** @defgroup legend_bindings Legend picking and binding identity
 * Internal legend services; not part of the public stubs API.
 */
/** @defgroup legend_configuration Legend configuration and creation
 * Internal legend services; not part of the public stubs API.
 */
/** @defgroup legend_lifetime Legend window and resource lifetime
 * Internal legend services; not part of the public stubs API.
 */
/** @defgroup legend_output Legend screen and document output
 * Internal legend services; not part of the public stubs API.
 */
/** @defgroup legend_commands Legend command dispatch
 * Internal legend services; not part of the public stubs API.
 */

#include "rbcGraph.h"
#include "rbcGrTrack.h"
#include "rbcRender.h"
#include "rbcGrElem.h"

/**
 * @ingroup legend_lifetime
 * @brief Graph- or track-owned legend with borrowed elements and scoped bindings.
 *
 * Owns option state, derived shadow/text resources and optional external
 * associations. A local legend owns tags but borrows the shared event table;
 * a global legend owns its binding table. Cached dimensions are not entry
 * records: entries remain in the graph's element display list.
 */
struct LegendStruct {
    RbcTrack *track; /**< NULL for the graph-wide legend. */
    Tcl_HashTable tags; /**< Local binding tags, separate from global tags. */
    Tk_OptionTable optionTable; /**< Tk option table for this component configuration lifecycle. */
    /*
     * Original Tcl representations for values requiring additional
     * conversion or validation.
     */
    Tcl_Obj *activeBorderWidthObjPtr; /**< Tk option-managed Tcl representation for active border width; released with the owning option
     * state.
     */
    Tcl_Obj *borderWidthObjPtr; /**< Tk option-managed Tcl representation for border width; released with the owning option state. */
    Tcl_Obj *columnsObjPtr; /**< Tk option-managed Tcl representation for columns; released with the owning option state. */
    Tcl_Obj *ipadXObjPtr; /**< Tk option-managed Tcl representation for ipad x; released with the owning option state. */
    Tcl_Obj *ipadYObjPtr; /**< Tk option-managed Tcl representation for ipad y; released with the owning option state. */
    Tcl_Obj *padXObjPtr; /**< Tk option-managed Tcl representation for pad x; released with the owning option state. */
    Tcl_Obj *padYObjPtr; /**< Tk option-managed Tcl representation for pad y; released with the owning option state. */
    Tcl_Obj *positionObjPtr; /**< Tk option-managed Tcl representation for position; released with the owning option state. */
    Tcl_Obj *rowsObjPtr; /**< Tk option-managed Tcl representation for rows; released with the owning option state. */
    Tcl_Obj *shadowObjPtr; /**< Tk option-managed Tcl representation for shadow; released with the owning option state. */
    int optionsInitialized; /**< Nonzero after Tk option initialization. */
    int tkResourcesReleased; /**< Nonzero after guarded Tk-resource release. */
    unsigned int flags; /**< Legend state flags, including redraw scheduling. */
    Rbc_Uid classUid; /**< Type: Element or Marker. */
    int hidden; /**< If non-zero, don't display the legend. */
    int raised; /**< If non-zero, draw the legend last, above everything else. */
    int entryInset; /**< Entry width excluding its label. */
    Tcl_Size nEntries; /**< Number of element entries in table. */
    int width; /**< Dimensions of the legend */
    int height; /**< Dimensions of the legend */
    Tcl_Size nColumns; /**< Number of columns and rows in legend */
    Tcl_Size nRows; /**< Number of columns and rows in legend */
    int site; /**< Legend placement selector: margin, plot, coordinates or external window. */
    Point2D anchorPos; /**< Says how to position the legend. Indicates the site and/or x-y screen coordinates of the legend.
     * Used in conjunction with the anchor to determine its location.
     */
    Tk_Anchor anchor; /**< Anchor of legend. Used to interpret the positioning point of the legend in the graph */
    int x; /**< Computed origin of legend. */
    int y; /**< Computed origin of legend. */
    Graph *graphPtr; /**< Borrowed owning graph. */
    Tcl_Command cmdToken; /**< Token for graph's widget command. */
    int reqColumns; /**< Requested column count; zero allows automatic layout. */
    int reqRows; /**< Requested row count; zero allows automatic layout. */
    Rbc_Pad ipadX; /**< # of pixels padding around legend entries */
    Rbc_Pad ipadY; /**< # of pixels padding around legend entries */
    Rbc_Pad padX; /**< # of pixels padding to exterior of legend */
    Rbc_Pad padY; /**< # of pixels padding to exterior of legend */
    Tk_Window tkwin; /**< Optional external window to draw legend. */
    TextStyle style; /**< Entry text style and owned GC. */
    int maxSymSize; /**< Size of largest symbol to be displayed. Used to calculate size of legend */
    Tk_3DBorder activeBorder; /**< Active legend entry background color. */
    int activeRelief; /**< 3-D effect on active entry. */
    int entryBorderWidth; /**< Border width around each entry in legend. */
    Tk_3DBorder border; /**< 3-D effect of legend. */
    int borderWidth; /**< Width of legend 3-D border */
    int relief; /**< 3-d effect of border around the legend: TK_RELIEF_RAISED etc. */
    Rbc_BindTable bindTable; /**< Binding table used by this legend; local legends borrow the shared track table. */
};

/**
 * @ingroup legend_configuration
 * @brief Staged position; windowName borrows the parsed Tcl object's string.
 */
typedef struct {
    int site; /**< Legend site selector. */
    Point2D anchorPos; /**< Explicit position in widget pixels; negative values are resolved from the far edges. */
    const char *windowName; /**< Borrowed Tk pathname for an external legend, or NULL. */
} LegendPosition;

/**
 * @ingroup legend_layout
 * @brief Clamp a wide layout dimension to a nonnegative int.
 *
 * Nonpositive values return zero; larger positive values saturate at INT_MAX.
 * No allocation or graph mutation occurs.
 *
 * @param[in] value Wide pixel dimension; nonpositive values become zero and large values saturate.
 */
static int LegendLayoutSize(Tcl_WideInt value) {
    if (value <= 0) {
        return 0;
    }
    return Rbc_ClampToInt(value);
}

/**
 * @ingroup legend_layout
 * @brief Saturate a finite coordinate before conversion to int.
 *
 * Values within int bounds truncate toward zero. The caller must exclude NaN;
 * this helper does not report a conversion error.
 *
 * @param[in] value Pixel coordinate excluding NaN; finite values truncate toward zero within int bounds.
 */
static int LegendCoordinate(double value) {
    if (value >= (double)INT_MAX) {
        return INT_MAX;
    }
    if (value <= (double)INT_MIN) {
        return INT_MIN;
    }
    return (int)value;
}

/**
 * @ingroup legend_layout
 * @brief Check a cell-count dimension including border and exterior padding.
 *
 * Returns TCL_OK and writes dimensionPtr only when all inputs are nonnegative
 * and the complete dimension fits in int. Returns TCL_ERROR without changing
 * output otherwise. Uses division before multiplication; performs no allocation.
 *
 * @param[in] count Number of cells along the requested dimension; negative values are rejected.
 * @param[in] entrySize Pixel size per cell; zero is allowed, negative values are rejected.
 * @param[in] borderWidth Pixel border thickness on each side; must be nonnegative.
 * @param[in] padPtr Borrowed exterior padding with two nonnegative pixel widths.
 * @param[out] dimensionPtr Required int output; unchanged if inputs or the complete size are invalid.
 */
static int GetLegendPixelDimension(Tcl_Size count, int entrySize, int borderWidth, const Rbc_Pad *padPtr,
                                   int *dimensionPtr) {
    Tcl_WideInt base;
    Tcl_WideInt maximumCount;

    if ((count < 0) || (entrySize < 0) || (borderWidth < 0) || (padPtr->side1 < 0) || (padPtr->side2 < 0)) {
        return TCL_ERROR;
    }
    base = 2 * (Tcl_WideInt)borderWidth + (Tcl_WideInt)padPtr->side1 + (Tcl_WideInt)padPtr->side2;
    if (base > INT_MAX) {
        return TCL_ERROR;
    }
    if (entrySize > 0) {
        maximumCount = ((Tcl_WideInt)INT_MAX - base) / (Tcl_WideInt)entrySize;
        if ((Tcl_WideInt)count > maximumCount) {
            return TCL_ERROR;
        }
    }
    *dimensionPtr = (int)(base + ((Tcl_WideInt)count * (Tcl_WideInt)entrySize));
    return TCL_OK;
}

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

/* Inherit styling while keeping local legends opt-in and inside their row. */
static const Tk_OptionSpec trackLegendOptionSpecs[] = {
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", "yes", -1, offsetof(Legend, hidden), 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED},
    {TK_OPTION_STRING, "-position", "position", "Position", "plotarea", offsetof(Legend, positionObjPtr), -1, 0, NULL,
     LEGEND_REDRAW_CHANGED | LEGEND_LAYOUT_CHANGED | LEGEND_POSITION_CHANGED},
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor", "ne", -1, offsetof(Legend, anchor), 0, NULL,
     LEGEND_REDRAW_CHANGED},
    {TK_OPTION_BOOLEAN, "-raised", "raised", "Raised", "yes", -1, offsetof(Legend, raised), 0, NULL,
     LEGEND_REDRAW_CHANGED},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, legendOptionSpecs, 0}};

static Tcl_IdleProc DisplayLegend;
static Rbc_BindPickProc PickLegendEntry;
static Rbc_BindTagProc LegendTags;
static Tk_EventProc LegendEventProc;
static void EventuallyRedrawLegend(Legend *legendPtr);
static void SetLegendOrigin(Legend *legendPtr);
static int ConfigureLegend(Graph *graphPtr, Legend *legendPtr, int mask);

typedef int RbcGrLegdOp(Legend *legendPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

/**
 * @ingroup legend_commands
 * @brief Shared dispatch header followed by a legend-specific handler pointer.
 */
typedef struct {
    Rbc_OpSpecHeader header; /**< Shared operation name, abbreviation and argument-count metadata. */
    RbcGrLegdOp *proc; /**< Typed command handler for this operation. */
} LegendOpSpec;

static RbcGrLegdOp GetOp;
static RbcGrLegdOp ActivateOp;
static RbcGrLegdOp BindOp;
static RbcGrLegdOp CgetOp;
static RbcGrLegdOp ConfigureOp;

/**
 * @ingroup legend_configuration
 * @brief Recognize a nonempty case-sensitive placement keyword prefix.
 *
 * Returns a boolean for the borrowed spelling and fullName. Empty and
 * overlong spellings fail; does not allocate or change interpreter state.
 *
 * @param[in] string Borrowed candidate spelling, readable for length bytes.
 * @param[in] length Candidate byte count; zero and overlong spellings do not match.
 * @param[in] fullName Borrowed NUL-terminated canonical placement name.
 */
static int IsLegendPositionPrefix(const char *string, Tcl_Size length, const char *fullName) {
    Tcl_Size fullLength;

    fullLength = (Tcl_Size)strlen(fullName);
    return ((length > 0) && (length <= fullLength) && (strncmp(string, fullName, (size_t)length) == 0));
}

/**
 * @ingroup legend_bindings
 * @brief Intern a stable binding tag in the owning legend scope.
 *
 * Global legends use the graph element-tag table; local legends use their
 * own string-key table. Returns a borrowed key pointer valid until that table
 * is destroyed. Identical strings in separate local tables have distinct
 * binding identities.
 *
 * @param[in,out] legendPtr Borrowed legend selecting the global or local tag registry; missing keys are
 *    interned.
 * @param[in] name Borrowed NUL-terminated tag spelling; the registry owns its retained key.
 */
static ClientData LegendTag(Legend *legendPtr, const char *name) {
    Tcl_HashEntry *entry;
    int isNew;
    if (legendPtr->track == NULL) {
        return Rbc_MakeElementTag(legendPtr->graphPtr, (char *)name);
    }
    entry = Tcl_CreateHashEntry(&legendPtr->tags, name, &isNew);
    return Tcl_GetHashKey(&legendPtr->tags, entry);
}

/**
 * @ingroup legend_bindings
 * @brief Append name, class and configured tags for a picked legend entry.
 *
 * Borrows the element object and destination list. Context identifies the
 * legend; NULL context falls back to the graph-wide legend. Interns tags
 * through LegendTag. Does not evaluate binding scripts or own the element.
 *
 * @param[in] table Borrowed binding table whose client data supplies the graph when context is NULL.
 * @param[in] object Borrowed picked Element pointer, not a tag string.
 * @param[in] context Borrowed owning Legend pointer, or NULL to use the global legend.
 * @param[in,out] list Caller-owned initialized tag list; stable borrowed keys are appended in binding
 *    order.
 */
static void LegendTags(Rbc_BindTable table, ClientData object, ClientData context, Rbc_List list) {
    Legend *legendPtr = context;
    Element *elemPtr = object;
    char **tag;
    if (legendPtr == NULL) {
        legendPtr = ((Graph *)Rbc_GetBindingData(table))->legend;
    }
    Rbc_ListAppend(list, LegendTag(legendPtr, elemPtr->name), 0);
    Rbc_ListAppend(list, LegendTag(legendPtr, elemPtr->classUid), 0);
    if (elemPtr->tags != NULL) {
        for (tag = elemPtr->tags; *tag != NULL; tag++) {
            Rbc_ListAppend(list, LegendTag(legendPtr, *tag), 0);
        }
    }
}

/**
 * @ingroup legend_layout
 * @brief Test track membership independently of label or visibility.
 *
 * A global legend accepts every borrowed element. A local legend accepts only
 * elements with the same track pointer. Callers separately check label and
 * display-list eligibility; no reference is acquired.
 *
 * @param[in] legendPtr Borrowed legend selecting global or one-track membership.
 * @param[in] elemPtr Borrowed element whose track is compared; label and display membership are not
 *    tested.
 */
static int LegendHasElement(Legend *legendPtr, Element *elemPtr) {
    return (legendPtr->track == NULL || elemPtr->track == legendPtr->track);
}

/**
 * @ingroup legend_configuration
 * @brief Parse a global placement into a temporary position record.
 *
 * Empty input selects rightmargin. Accepts keyword prefixes, a dot-prefixed
 * external path or \@x,y with Tcl long expressions. The returned windowName
 * borrows the object string. Returns TCL_OK or TCL_ERROR with a diagnostic;
 * output may be partially initialized on error. Coordinate expressions can
 * execute Tcl, so callers must maintain owner and argument lifetime.
 *
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] objPtr Borrowed non-NULL placement object; coordinate expressions may evaluate Tcl.
 * @param[out] positionPtr Required staging record; may be partially written on error. windowName borrows
 *    objPtr storage.
 */
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

/**
 * @ingroup legend_lifetime
 * @brief Coalesce a legend idle draw while its target window exists.
 *
 * Schedules DisplayLegend only with a non-NULL window and no REDRAW_PENDING
 * flag, then marks the request pending. Does not check whether the window is
 * mapped; the idle handler does that. Caller must cancel before freeing records.
 *
 * @param[in,out] legendPtr Live legend retained until idle delivery or cancellation; pending state is
 *    updated.
 */
static void EventuallyRedrawLegend(Legend *legendPtr) {
    if ((legendPtr->tkwin != NULL) && !(legendPtr->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(DisplayLegend, legendPtr);
        legendPtr->flags |= REDRAW_PENDING;
    }
}

/**
 * @ingroup legend_lifetime
 * @brief Schedule the global external legend's independent redraw.
 *
 * Borrows a graph whose legend may be absent. Does nothing for an absent,
 * resource-released or non-window legend. Coalesces idle work without drawing
 * synchronously or remapping the graph.
 *
 * @param[in,out] graphPtr Borrowed graph whose global legend may be NULL; eligible external redraw is
 *    queued.
 */
void Rbc_RedrawExternalLegend(Graph *graphPtr) {
    Legend *legendPtr = graphPtr->legend;

    if ((legendPtr != NULL) && !legendPtr->tkResourcesReleased && (legendPtr->site == LEGEND_WINDOW)) {
        EventuallyRedrawLegend(legendPtr);
    }
}

/**
 * @ingroup legend_lifetime
 * @brief Create a candidate external window, command and event association.
 *
 * Borrows interpreter, legend and path. The command forwards to the graph
 * instance dispatcher; it is not a standalone legend dispatcher. Returns
 * TCL_OK and writes both handles only on success. Returns TCL_ERROR after
 * cleaning partial associations otherwise. The caller owns adoption/cleanup
 * of successful candidates; no packing or mapping is performed.
 *
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] legendPtr Borrowed legend associated with the candidate window and graph-forwarding
 *    command.
 * @param[in] pathName Borrowed NUL-terminated Tk window path.
 * @param[out] tkwinPtr Required output for the new window; assigned only on success, caller adopts or
 *    destroys it.
 * @param[out] cmdTokenPtr Required output for the new command; assigned only on success, caller adopts
 *    or deletes it.
 */
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
    cmdToken = Tcl_CreateObjCommand2(interp, pathName, Rbc_GraphInstCmdProc, legendPtr->graphPtr, NULL);
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

/**
 * @ingroup legend_lifetime
 * @brief Detach and destroy an external legend window before replacement.
 *
 * No-op for an absent window or the graph window. Cancels idle work, resets
 * stored handles before deleting the old command/window associations, and
 * moves the binding table back to the graph. Does not set the new site or
 * position object; the configuration caller completes that transition.
 *
 * @param[in,out] legendPtr Live legend with graph and binding infrastructure; external associations are
 *    removed, record retained.
 */
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

/**
 * @ingroup legend_configuration
 * @brief Replace the retained position object after an internal placement change.
 *
 * Creates and retains a string object, swaps it into the legend and releases
 * the previous reference. Does not parse placement, change site or schedule work.
 *
 * @param[in,out] legendPtr Live legend whose retained position object is replaced.
 * @param[in] value Borrowed NUL-terminated spelling copied into a newly retained Tcl string.
 */
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

/**
 * @ingroup legend_lifetime
 * @brief Release one legend's Tk resources while the graph window is valid.
 *
 * Accepts NULL and guards absent/already released option tables. Cancels idle
 * work, frees text drawing resources before option fonts/colors, frees the
 * separately acquired shadow, and releases options/table. Clears an in-widget
 * window pointer but retains an external one for final destruction. Does not
 * free the legend record or binding table.
 *
 * @param[in] graphPtr Owning graph with usable Tk window/display for option and drawing-resource
 *    cleanup.
 * @param[in,out] legendPtr Legend to release, or NULL; record and binding table are retained.
 */
static void ReleaseLegendTkResources(Graph *graphPtr, Legend *legendPtr) {
    if ((legendPtr == NULL) || (legendPtr->optionTable == NULL) || (legendPtr->tkResourcesReleased)) {
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
    Tk_DeleteOptionTable(legendPtr->optionTable);
    legendPtr->optionTable = NULL;
    legendPtr->tkResourcesReleased = TRUE;
    /*
     * Do not retain the graph's Tk_Window after it is destroyed.
     * An external legend window remains valid.
     */
    if ((legendPtr->site != LEGEND_WINDOW) && (legendPtr->tkwin == graphPtr->tkwin)) {
        legendPtr->tkwin = NULL;
    }
}

/**
 * @ingroup legend_lifetime
 * @brief Release local and global legend options before window loss.
 *
 * Requires the owning graph's valid track list and Tk window context. Visits
 * local legends first, then the optional global legend. Per-record guards
 * prevent duplicate release; final destruction owns records and bindings.
 *
 * @param[in,out] graphPtr Owning graph with valid track list and Tk cleanup context; records remain for
 *    final destruction.
 */
void Rbc_ReleaseLegendTkResources(Graph *graphPtr) {
    RbcTrack *track;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        ReleaseLegendTkResources(graphPtr, track->legend);
    }
    if (graphPtr->legend != NULL) {
        ReleaseLegendTkResources(graphPtr, graphPtr->legend);
    }
}
/**
 * @ingroup legend_lifetime
 * @brief Handle exposure, resize and destruction of the external legend window.
 *
 * Coalesces exposes and configure events into idle draws. DestroyNotify
 * removes command/instance associations, cancels pending work, restores
 * rightmargin and its retained option string, moves bindings to the graph
 * and requests graph remapping/redrawing. Borrows a live legend record.
 *
 * @param[in,out] clientData Borrowed Legend pointer registered with the external window event handler.
 * @param[in] eventPtr Borrowed exposure or structure event; not retained or modified.
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

/**
 * @ingroup legend_layout
 * @brief Compute legend placement from cached dimensions and its owning region.
 *
 * Requires current graph/track geometry. Local placement uses full world
 * bounds; global placement uses margins, plot bounds or screen coordinates.
 * Negative explicit coordinates are relative to the graph window extent.
 * Applies the anchor and padding with saturated arithmetic. External placement
 * forces the NW anchor and origin zero. Does not remeasure entries.
 *
 * @param[in,out] legendPtr Mapped legend with current owning-region geometry; cached origin and external
 *    anchor may change.
 */
static void SetLegendOrigin(Legend *legendPtr) {
    Graph *graphPtr;
    Tcl_WideInt x, y;
    Tcl_WideInt width, height;
    int ix, iy;
    int iwidth, iheight;

    graphPtr = legendPtr->graphPtr;
    x = y = width = height = 0;
    if (legendPtr->track != NULL) {
        x = legendPtr->track->worldLeft;
        y = legendPtr->track->worldTop;
        width = (Tcl_WideInt)legendPtr->track->worldRight - legendPtr->track->worldLeft;
        height = (Tcl_WideInt)legendPtr->track->worldBottom - legendPtr->track->worldTop;
        switch (legendPtr->site) {
        case LEGEND_RIGHT:
            x += width - legendPtr->width; /* fall through */
        case LEGEND_LEFT:
            width = legendPtr->width;
            break;
        case LEGEND_BOTTOM:
            y += height - legendPtr->height; /* fall through */
        case LEGEND_TOP:
            height = legendPtr->height;
            break;
        }
    } else
        switch (legendPtr->site) {
        case LEGEND_RIGHT:
            width = (Tcl_WideInt)graphPtr->rightMargin.width - (Tcl_WideInt)graphPtr->rightMargin.axesOffset;
            height = (Tcl_WideInt)graphPtr->bottom - (Tcl_WideInt)graphPtr->top;
            x = (Tcl_WideInt)graphPtr->width - width - (Tcl_WideInt)graphPtr->inset;
            y = graphPtr->top;
            break;
        case LEGEND_LEFT:
            width = (Tcl_WideInt)graphPtr->leftMargin.width - (Tcl_WideInt)graphPtr->leftMargin.axesOffset;
            height = (Tcl_WideInt)graphPtr->bottom - (Tcl_WideInt)graphPtr->top;
            x = graphPtr->inset;
            y = graphPtr->top;
            break;
        case LEGEND_TOP:
            width = (Tcl_WideInt)graphPtr->right - (Tcl_WideInt)graphPtr->left;
            height = (Tcl_WideInt)graphPtr->topMargin.height - (Tcl_WideInt)graphPtr->topMargin.axesOffset;
            if (graphPtr->title != NULL) {
                height -= (Tcl_WideInt)graphPtr->titleTextStyle.height;
            }
            x = graphPtr->left;
            y = graphPtr->inset;
            if (graphPtr->title != NULL) {
                y += (Tcl_WideInt)graphPtr->titleTextStyle.height;
            }
            break;
        case LEGEND_BOTTOM:
            width = (Tcl_WideInt)graphPtr->right - (Tcl_WideInt)graphPtr->left;
            height = (Tcl_WideInt)graphPtr->bottomMargin.height - (Tcl_WideInt)graphPtr->bottomMargin.axesOffset;
            x = graphPtr->left;
            y = (Tcl_WideInt)graphPtr->height - height - (Tcl_WideInt)graphPtr->inset;
            break;
        case LEGEND_PLOT:
            width = (Tcl_WideInt)graphPtr->right - (Tcl_WideInt)graphPtr->left;
            height = (Tcl_WideInt)graphPtr->bottom - (Tcl_WideInt)graphPtr->top;
            x = graphPtr->left;
            y = graphPtr->top;
            break;
        case LEGEND_XY:
            width = legendPtr->width;
            height = legendPtr->height;
            x = LegendCoordinate(legendPtr->anchorPos.x);
            y = LegendCoordinate(legendPtr->anchorPos.y);
            if (x < 0) {
                x += (Tcl_WideInt)graphPtr->width;
            }
            if (y < 0) {
                y += (Tcl_WideInt)graphPtr->height;
            }
            break;
        case LEGEND_WINDOW:
            legendPtr->anchor = TK_ANCHOR_NW;
            legendPtr->x = 0;
            legendPtr->y = 0;
            return;
        }
    width = (Tcl_WideInt)legendPtr->width - width;
    height = (Tcl_WideInt)legendPtr->height - height;
    ix = Rbc_ClampToInt(x);
    iy = Rbc_ClampToInt(y);
    iwidth = Rbc_ClampToInt(width);
    iheight = Rbc_ClampToInt(height);
    Rbc_TranslateAnchor(ix, iy, iwidth, iheight, legendPtr->anchor, &ix, &iy);
    legendPtr->x = Rbc_ClampToInt((Tcl_WideInt)ix + (Tcl_WideInt)legendPtr->padLeft);
    legendPtr->y = Rbc_ClampToInt((Tcl_WideInt)iy + (Tcl_WideInt)legendPtr->padTop);
}

/**
 * @ingroup legend_layout
 * @brief Return a compact single-row width or the uniform grid-cell width.
 *
 * Borrows a labeled element and initialized text style. In one row, remeasures
 * that label and adds entryInset; otherwise returns cached style.width.
 * Mapping must already have validated the dimensions and label state.
 *
 * @param[in] legendPtr Mapped legend with initialized font/style and validated entry inset.
 * @param[in] elemPtr Borrowed element with a non-NULL label; measured only for a single-row layout.
 */
static int LegendEntryWidth(Legend *legendPtr, Element *elemPtr) {
    int width, height;

    if (legendPtr->nRows != 1) {
        return legendPtr->style.width;
    }
    Rbc_GetTextExtents(&legendPtr->style, elemPtr->label, &width, &height);
    return width + legendPtr->entryInset;
}

/**
 * @ingroup legend_bindings
 * @brief Return the borrowed element at a point within one mapped legend.
 *
 * Recomputes origin, rejects hidden/empty legends and points outside a local
 * track clip, then excludes outer borders and padding. One row scans measured
 * widths; a grid uses column-major cell indices with overflow checks. Walks
 * the display list in reverse, applying label and track filters. Returns NULL
 * for no entry; does not update graph layout or acquire an element reference.
 *
 * @param[in,out] legendPtr Borrowed mapped legend; origin may be recomputed, layout is not refreshed.
 * @param[in] x Horizontal pixel coordinate in the legend drawing window, not relative to its origin.
 * @param[in] y Vertical pixel coordinate in the same drawing window.
 */
static ClientData PickLegendIn(Legend *legendPtr, int x, int y) {
    Graph *graphPtr;
    Tcl_WideInt localX, localY;
    Tcl_WideInt width, height;
    Tcl_Size row, column;
    Tcl_Size n;

    graphPtr = legendPtr->graphPtr;
    if (legendPtr->tkwin == NULL || legendPtr->hidden || legendPtr->nEntries == 0) {
        return NULL;
    }
    SetLegendOrigin(legendPtr);
    if (legendPtr->track != NULL && (x < legendPtr->track->left || x > legendPtr->track->right ||
                                     y < legendPtr->track->top || y > legendPtr->track->bottom)) {
        return NULL;
    }
    localX = (Tcl_WideInt)x - (Tcl_WideInt)legendPtr->x - (Tcl_WideInt)legendPtr->borderWidth;
    localY = (Tcl_WideInt)y - (Tcl_WideInt)legendPtr->y - (Tcl_WideInt)legendPtr->borderWidth;
    width = (Tcl_WideInt)legendPtr->width - (2 * (Tcl_WideInt)legendPtr->borderWidth) - PADDING(legendPtr->padX);
    height = (Tcl_WideInt)legendPtr->height - (2 * (Tcl_WideInt)legendPtr->borderWidth) - PADDING(legendPtr->padY);
    if ((localX < 0) || (localX >= width) || (localY < 0) || (localY >= height)) {
        return NULL;
    }
    if ((legendPtr->style.width <= 0) || (legendPtr->style.height <= 0) || (legendPtr->nRows <= 0) ||
        (legendPtr->nColumns <= 0)) {
        return NULL;
    }
    if (legendPtr->nRows == 1) {
        Rbc_ChainLink *linkPtr;

        for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
             linkPtr = Rbc_ChainPrevLink(linkPtr)) {
            Element *elemPtr = Rbc_ChainGetValue(linkPtr);
            int entryWidth;

            if (elemPtr->label == NULL || !LegendHasElement(legendPtr, elemPtr)) {
                continue;
            }
            entryWidth = LegendEntryWidth(legendPtr, elemPtr);
            if (localX < entryWidth) {
                return elemPtr;
            }
            localX -= entryWidth;
        }
        return NULL;
    }
    row = (Tcl_Size)(localY / legendPtr->style.height);
    column = (Tcl_Size)(localX / legendPtr->style.width);
    if ((row >= legendPtr->nRows) || (column >= legendPtr->nColumns)) {
        return NULL;
    }
    if (column > (TCL_SIZE_MAX - row) / legendPtr->nRows) {
        return NULL;
    }
    n = (column * legendPtr->nRows) + row;
    if (n < legendPtr->nEntries) {
        Rbc_ChainLink *linkPtr;
        Element *elemPtr;
        Tcl_Size count;

        count = 0;
        for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
             linkPtr = Rbc_ChainPrevLink(linkPtr)) {
            elemPtr = Rbc_ChainGetValue(linkPtr);
            if (elemPtr->label == NULL || !LegendHasElement(legendPtr, elemPtr)) {
                continue;
            }
            if (count == n) {
                return elemPtr;
            }
            count++;
        }
    }
    return NULL;
}

/**
 * @ingroup legend_bindings
 * @brief Test the legend rectangle for occlusion, including blank areas.
 *
 * Recomputes origin and rejects hidden/empty/unattached legends. Local legends
 * additionally require the point inside visible track bounds. This boolean
 * test deliberately differs from entry picking: padding can occlude content.
 *
 * @param[in,out] legendPtr Borrowed mapped legend; origin may be recomputed, layout is not refreshed.
 * @param[in] x Horizontal pixel coordinate in the legend drawing window, not relative to its origin.
 * @param[in] y Vertical pixel coordinate in the same drawing window.
 */
static int LegendContains(Legend *legendPtr, int x, int y) {
    if (legendPtr->tkwin == NULL || legendPtr->hidden || legendPtr->nEntries == 0) {
        return FALSE;
    }
    SetLegendOrigin(legendPtr);
    if (legendPtr->track != NULL && (x < legendPtr->track->left || x > legendPtr->track->right ||
                                     y < legendPtr->track->top || y > legendPtr->track->bottom)) {
        return FALSE;
    }
    return ((Tcl_WideInt)x >= legendPtr->x && (Tcl_WideInt)x < (Tcl_WideInt)legendPtr->x + legendPtr->width &&
            (Tcl_WideInt)y >= legendPtr->y && (Tcl_WideInt)y < (Tcl_WideInt)legendPtr->y + legendPtr->height);
}

/**
 * @ingroup legend_bindings
 * @brief Find the topmost in-widget legend rectangle at a graph point.
 *
 * Requires a live graph-wide legend. Checks raised then non-raised layers,
 * global before local within each layer, and tracks in list order. Excludes
 * external windows. Returns a borrowed legend or NULL, without picking an entry.
 *
 * @param[in,out] graphPtr Live graph with a global legend and prepared global/local layouts; tested
 *    origins may change.
 * @param[in] x Horizontal graph-window pixel coordinate.
 * @param[in] y Vertical graph-window pixel coordinate.
 */
static Legend *LegendAtPoint(Graph *graphPtr, int x, int y) {
    RbcTrack *track;
    Legend *global = graphPtr->legend;
    int raised;
    for (raised = 1; raised >= 0; raised--) {
        if (global->site != LEGEND_WINDOW && global->raised == raised && LegendContains(global, x, y)) {
            return global;
        }
        for (track = graphPtr->tracks; track != NULL; track = track->next) {
            if (track->legend != NULL && track->legend->raised == raised && LegendContains(track->legend, x, y)) {
                return track->legend;
            }
        }
    }
    return NULL;
}

/**
 * @ingroup legend_bindings
 * @brief Test whether any in-widget legend occludes a graph point.
 *
 * Delegates to LegendAtPoint, including blank legend areas. Does not remap
 * the graph or test external legend windows.
 *
 * @param[in,out] graphPtr Live graph with a global legend and prepared global/local layouts; tested
 *    origins may change.
 * @param[in] x Horizontal graph-window pixel coordinate.
 * @param[in] y Vertical graph-window pixel coordinate.
 */
int Rbc_PointInLegend(Graph *graphPtr, int x, int y) { return LegendAtPoint(graphPtr, x, y) != NULL; }

/**
 * @ingroup legend_bindings
 * @brief Test whether the topmost legend at a graph point is local.
 *
 * Returns false when a global legend occludes a local one. Uses current
 * mapped geometry and borrows the graph; no binding scripts are run.
 *
 * @param[in,out] graphPtr Live graph with a global legend and prepared global/local layouts; tested
 *    origins may change.
 * @param[in] x Horizontal graph-window pixel coordinate.
 * @param[in] y Vertical graph-window pixel coordinate.
 */
int Rbc_PointInTrackLegend(Graph *graphPtr, int x, int y) {
    Legend *legendPtr = LegendAtPoint(graphPtr, x, y);
    return (legendPtr != NULL && legendPtr->track != NULL);
}

/**
 * @ingroup legend_bindings
 * @brief Adapt global or external legend picking to the binding interface.
 *
 * clientData is the graph. Writes the global legend to optional contextPtr,
 * even if no entry is found. In-widget picking requires it to be topmost;
 * external picking uses that window's coordinates directly. Returns a
 * borrowed element or NULL.
 *
 * @param[in,out] clientData Borrowed Graph pointer supplied by the binding table.
 * @param[in] x Horizontal event-window pixel coordinate; graph coordinates for local legends.
 * @param[in] y Vertical event-window pixel coordinate.
 * @param[out] contextPtr Optional output for the borrowed global legend, including when no item is
 *    found.
 */
static ClientData PickLegendEntry(ClientData clientData, int x, int y, ClientData *contextPtr) {
    Graph *graphPtr = clientData;
    Legend *legendPtr = graphPtr->legend;
    if (contextPtr != NULL) {
        *contextPtr = legendPtr;
    }
    if (legendPtr->site != LEGEND_WINDOW && LegendAtPoint(graphPtr, x, y) != legendPtr) {
        return NULL;
    }
    return PickLegendIn(legendPtr, x, y);
}

/**
 * @ingroup legend_bindings
 * @brief Adapt local legend picking with an explicit legend context.
 *
 * clientData is the graph and contextPtr must be writable. Stores the
 * topmost legend, then returns a borrowed entry only if that legend is local.
 * Context can identify a global legend even when the returned item is NULL.
 *
 * @param[in,out] clientData Borrowed Graph pointer supplied by the binding table.
 * @param[in] x Horizontal event-window pixel coordinate; graph coordinates for local legends.
 * @param[in] y Vertical event-window pixel coordinate.
 * @param[out] contextPtr Required output for the borrowed topmost legend or NULL; can identify a global
 *    legend with no returned item.
 */
static ClientData PickTrackLegendEntry(ClientData clientData, int x, int y, ClientData *contextPtr) {
    Graph *graphPtr = clientData;
    Legend *legendPtr = LegendAtPoint(graphPtr, x, y);
    *contextPtr = legendPtr;
    if (legendPtr == NULL || legendPtr->track == NULL) {
        return NULL;
    }
    return PickLegendIn(legendPtr, x, y);
}

/**
 * @ingroup legend_layout
 * @brief Measure entries and replace cached legend dimensions.
 *
 * Borrows an initialized legend and owning graph. plotWidth/plotHeight are
 * available dimensions; an external window larger than one pixel overrides
 * them. Resets layout first, then counts labeled display-list elements in scope.
 * Single rows use individual widths; multiple rows use maximum-size cells.
 * Unusable or overflowing dimensions leave an empty layout. Does not set the
 * origin or draw; may request new external-window geometry.
 *
 * @param[in,out] legendPtr Initialized legend with live graph and display-list elements; cached layout
 *    is replaced.
 * @param[in] plotWidth Available pixel width; a usable external-window width overrides it.
 * @param[in] plotHeight Available pixel height; a usable external-window height overrides it.
 */
void Rbc_MapLegend(Legend *legendPtr, int plotWidth, int plotHeight) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;
    Tcl_Size nRows;
    Tcl_Size nColumns;
    Tcl_Size nEntries;
    int legendWidth, legendHeight;
    int entryWidth, entryHeight;
    int symbolWidth;
    Tcl_WideInt dimension, rowWidth = 0;
    int entryInset;
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
        if (elemPtr->label == NULL || !LegendHasElement(legendPtr, elemPtr)) {
            continue; /* Element has no legend entry. */
        }
        Rbc_GetTextExtents(&legendPtr->style, elemPtr->label, &width, &height);
        /* Saturate the sum: an oversized row may still fit as a grid. */
        rowWidth += width;
        if (rowWidth > INT_MAX) {
            rowWidth = (Tcl_WideInt)INT_MAX + 1;
        }
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
    dimension = 2 * (Tcl_WideInt)fontMetrics.ascent;
    if ((dimension < 0) || (dimension > INT_MAX)) {
        return;
    }
    symbolWidth = (int)dimension;
    dimension = (Tcl_WideInt)entryWidth + (2 * (Tcl_WideInt)legendPtr->entryBorderWidth) +
                (Tcl_WideInt)legendPtr->ipadX.side1 + (Tcl_WideInt)legendPtr->ipadX.side2 + 5 +
                (Tcl_WideInt)symbolWidth;
    if ((dimension < 1) || (dimension > INT_MAX)) {
        return;
    }
    entryInset = (int)dimension - entryWidth;
    entryWidth = (int)dimension;
    if ((rowWidth > INT_MAX) || ((entryInset > 0) && (nEntries > (INT_MAX - rowWidth) / entryInset))) {
        rowWidth = (Tcl_WideInt)INT_MAX + 1;
    } else {
        rowWidth += (Tcl_WideInt)nEntries * entryInset;
    }
    dimension = (Tcl_WideInt)entryHeight + 2 * (Tcl_WideInt)legendPtr->entryBorderWidth +
                (Tcl_WideInt)legendPtr->ipadY.side1 + (Tcl_WideInt)legendPtr->ipadY.side2;
    if ((dimension < 1) || (dimension > INT_MAX)) {
        return;
    }
    entryHeight = (int)dimension;
    dimension = (Tcl_WideInt)plotWidth - 2 * (Tcl_WideInt)legendPtr->borderWidth - (Tcl_WideInt)legendPtr->padX.side1 -
                (Tcl_WideInt)legendPtr->padX.side2;
    if ((dimension < 1) || (dimension > INT_MAX)) {
        return;
    }
    legendWidth = (int)dimension;
    dimension = (Tcl_WideInt)plotHeight - 2 * (Tcl_WideInt)legendPtr->borderWidth - (Tcl_WideInt)legendPtr->padY.side1 -
                (Tcl_WideInt)legendPtr->padY.side2;
    if ((dimension < 1) || (dimension > INT_MAX)) {
        return;
    }
    legendHeight = (int)dimension;
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
        nRows = (Tcl_Size)legendPtr->reqRows;
        if (nRows > nEntries) {
            nRows = nEntries;
        }
        if (legendPtr->reqColumns > 0) {
            nColumns = (Tcl_Size)legendPtr->reqColumns;
            if (nColumns > nEntries) {
                nColumns = nEntries; /* Both -rows, -columns set. */
            }
        } else {
            nColumns = ((nEntries - 1) / nRows) + 1; /* Only -rows. */
        }
    } else if (legendPtr->reqColumns > 0) { /* Only -columns. */
        nColumns = (Tcl_Size)legendPtr->reqColumns;
        if (nColumns > nEntries) {
            nColumns = nEntries;
        }
        nRows = ((nEntries - 1) / nColumns) + 1;
    } else {
        /* Compute # of rows and columns from the legend size. */
        nRows = (Tcl_Size)(legendHeight / entryHeight);
        nColumns = (Tcl_Size)(legendWidth / entryWidth);
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
            if (rowWidth <= legendWidth) {
                nColumns = nEntries;
                nRows = 1;
            } else {
                nRows = ((nEntries - 1) / nColumns) + 1;
            }
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
    if (nRows == 1) {
        if ((rowWidth > INT_MAX) || (GetLegendPixelDimension(1, (int)rowWidth, legendPtr->borderWidth, &legendPtr->padX,
                                                             &legendWidth) != TCL_OK)) {
            return;
        }
        nColumns = nEntries;
    } else if (GetLegendPixelDimension(nColumns, entryWidth, legendPtr->borderWidth, &legendPtr->padX, &legendWidth) !=
               TCL_OK) {
        return;
    }
    if (GetLegendPixelDimension(nRows, entryHeight, legendPtr->borderWidth, &legendPtr->padY, &legendHeight) !=
        TCL_OK) {
        return;
    }
    legendPtr->entryInset = entryInset;
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

/**
 * @ingroup legend_output
 * @brief Compose a mapped legend in a temporary pixmap and copy it to output.
 *
 * Requires live window, graph and element records and current layout. Clears
 * DRAW_LEGEND even when hidden or empty. Sets origin, composes background,
 * active/relief entry borders, element symbol callbacks, text and outer border.
 * Local copies are intersected with the visible track rectangle. Frees the
 * pixmap and closes the text context. Borrows entries; does not own elements
 * or provide a mutation-safe snapshot across symbol callbacks.
 *
 * @param[in,out] legendPtr Mapped legend with live window, graph and element records; origin and draw
 *    flags may change.
 * @param[in] drawable Borrowed compatible destination receiving the composed legend; not freed by this
 *    call.
 */
void Rbc_DrawLegend(Legend *legendPtr, Drawable drawable) {
    Graph *graphPtr;
    Rbc_ChainLink *linkPtr;
    Pixmap pixmap;
    Tk_3DBorder border;
    Tk_FontMetrics fontMetrics;
    Tk_Window tkwin;
    Tcl_Size count;
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
    midX = Rbc_ClampToInt((Tcl_WideInt)symbolSize + 1 + (Tcl_WideInt)legendPtr->entryBorderWidth);
    midY = Rbc_ClampToInt(((Tcl_WideInt)symbolSize / 2) + 1 + (Tcl_WideInt)legendPtr->entryBorderWidth);
    labelX = Rbc_ClampToInt((2 * (Tcl_WideInt)symbolSize) + (Tcl_WideInt)legendPtr->entryBorderWidth +
                             (Tcl_WideInt)legendPtr->ipadX.side1 + 5);
    symbolY = Rbc_ClampToInt((Tcl_WideInt)midY + (Tcl_WideInt)legendPtr->ipadY.side1);
    symbolX = Rbc_ClampToInt((Tcl_WideInt)midX + (Tcl_WideInt)legendPtr->ipadX.side1);
    pixmap = Tk_GetPixmap(graphPtr->display, Tk_WindowId(legendPtr->tkwin), width, height, Tk_Depth(legendPtr->tkwin));
    Rbc_RenderContext *text = Rbc_RenderBeginScreenText(legendPtr->tkwin, pixmap);
    if (legendPtr->border != NULL) {
        /* Background color and relief. */
        Rbc_RenderScreenBorder(legendPtr->tkwin, pixmap, legendPtr->border, 0, 0, width, height, 0, TK_RELIEF_FLAT, 1);
    } else if (legendPtr->track != NULL || (legendPtr->site & LEGEND_IN_PLOT)) {
        /*
         * Legend background is transparent and is positioned over the
         * the plot area.  Either copy the part of the background from
         * the backing store pixmap or (if no backing store exists)
         * just fill it with the background color of the plot.
         */
        if (graphPtr->backPixmap != None) {
            if (legendPtr->track != NULL) {
                int left = MAX(0, legendPtr->x), top = MAX(0, legendPtr->y);
                int right = MIN(graphPtr->width, Rbc_ClampToInt((Tcl_WideInt)legendPtr->x + width));
                int bottom = MIN(graphPtr->height, Rbc_ClampToInt((Tcl_WideInt)legendPtr->y + height));
                Rbc_RenderScreenRectangle(legendPtr->tkwin, pixmap, graphPtr->plotFillGC, NULL, 0, 0, width, height);
                if (right > left && bottom > top) {
                    Rbc_RenderScreenCopyArea(graphPtr->tkwin, graphPtr->backPixmap, pixmap, graphPtr->drawGC, left, top,
                                             right - left, bottom - top, left - legendPtr->x, top - legendPtr->y);
                }
            } else {
                Rbc_RenderScreenCopyArea(graphPtr->tkwin, graphPtr->backPixmap, pixmap, graphPtr->drawGC, legendPtr->x,
                                         legendPtr->y, width, height, 0, 0);
            }
        } else {
            Rbc_RenderScreenRectangle(legendPtr->tkwin, pixmap, graphPtr->plotFillGC, NULL, 0, 0, width, height);
        }
    } else {
        /*
         * The legend is positioned in one of the margins or the
         * external window.  Draw either the solid or tiled background
         * with the the border.
         */
        if (graphPtr->tile != NULL) {
            Rbc_SetTileOrigin(legendPtr->tkwin, graphPtr->tile, legendPtr->x, legendPtr->y);
            Rbc_RenderScreenRectangle(legendPtr->tkwin, pixmap, NULL, graphPtr->tile, 0, 0, width, height);
        } else {
            Rbc_RenderScreenRectangle(legendPtr->tkwin, pixmap, graphPtr->fillGC, NULL, 0, 0, width, height);
        }
    }
    x = Rbc_ClampToInt((Tcl_WideInt)legendPtr->padLeft + (Tcl_WideInt)legendPtr->borderWidth);
    y = Rbc_ClampToInt((Tcl_WideInt)legendPtr->padTop + (Tcl_WideInt)legendPtr->borderWidth);
    count = 0;
    startY = y;
    for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainPrevLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->label == NULL || !LegendHasElement(legendPtr, elemPtr)) {
            continue; /* Skip this entry */
        }
        if (elemPtr->flags & LABEL_ACTIVE) {
            legendPtr->style.state |= STATE_ACTIVE;
            Rbc_RenderScreenBorder(legendPtr->tkwin, pixmap, legendPtr->activeBorder, x, y,
                                   LegendEntryWidth(legendPtr, elemPtr), legendPtr->style.height,
                                   legendPtr->entryBorderWidth, legendPtr->activeRelief, 1);
        } else {
            legendPtr->style.state &= ~STATE_ACTIVE;
            if (elemPtr->labelRelief != TK_RELIEF_FLAT) {
                Rbc_RenderScreenBorder(legendPtr->tkwin, pixmap, graphPtr->border, x, y,
                                       LegendEntryWidth(legendPtr, elemPtr), legendPtr->style.height,
                                       legendPtr->entryBorderWidth, elemPtr->labelRelief, 0);
            }
        }
        (*elemPtr->procsPtr->drawSymbolProc)(graphPtr, pixmap, elemPtr, x + symbolX, y + symbolY, symbolSize, width,
                                             height);
        Rbc_RenderText(text, elemPtr->label, &legendPtr->style, x + labelX,
                       y + legendPtr->entryBorderWidth + legendPtr->ipadY.side1);
        count++;
        /* Check when to move to the next column */
        if ((count % legendPtr->nRows) > 0) {
            y += legendPtr->style.height;
        } else {
            x += LegendEntryWidth(legendPtr, elemPtr);
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
    Rbc_RenderScreenBorder(legendPtr->tkwin, pixmap, border, 0, 0, width, height, legendPtr->borderWidth,
                           legendPtr->relief, 0);
    if (legendPtr->track != NULL) {
        int left = MAX(legendPtr->track->left, legendPtr->x), top = MAX(legendPtr->track->top, legendPtr->y);
        int right = MIN(legendPtr->track->right + 1, Rbc_ClampToInt((Tcl_WideInt)legendPtr->x + width));
        int bottom = MIN(legendPtr->track->bottom + 1, Rbc_ClampToInt((Tcl_WideInt)legendPtr->y + height));
        if (right > left && bottom > top) {
            Rbc_RenderScreenCopyArea(graphPtr->tkwin, pixmap, drawable, graphPtr->drawGC, left - legendPtr->x,
                                     top - legendPtr->y, right - left, bottom - top, left, top);
        }
    } else {
        Rbc_RenderScreenCopyArea(graphPtr->tkwin, pixmap, drawable, graphPtr->drawGC, 0, 0, width, height, legendPtr->x,
                                 legendPtr->y);
    }
    Tk_FreePixmap(graphPtr->display, pixmap);
    Rbc_RenderEnd(text);
}

/**
 * @ingroup legend_output
 * @brief Emit mapped legend decoration, symbols and labels to an export backend.
 *
 * Borrows legend, export context and displayed elements. Hidden/empty layouts
 * emit nothing. Subtracts exterior padding from the output box, sets origin
 * and handles decorations before visiting entries in column-major order.
 * Uses each concrete element's exportSymbolProc and closes its own output
 * context. Local clipping and external-legend exclusion belong to callers.
 *
 * @param[in,out] legendPtr Mapped legend with valid styles and displayed elements; origin is recomputed.
 * @param[in,out] exportPtr Borrowed initialized document context; caller supplies local clipping and
 *    finalization.
 */
void Rbc_LegendExport(Legend *legendPtr, Rbc_ExportContext *exportPtr) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    Graph *graphPtr;
    double x, y, startY;
    Element *elemPtr;
    int labelX, symbolX, symbolY;
    Tcl_Size count;
    Rbc_ChainLink *linkPtr;
    int symbolSize, midX, midY;
    int width, height;
    Tk_FontMetrics fontMetrics;

    if ((legendPtr->hidden) || (legendPtr->nEntries == 0)) {
        Rbc_RenderEnd(output);
        return;
    }
    SetLegendOrigin(legendPtr);
    x = legendPtr->x, y = legendPtr->y;
    width = LegendLayoutSize((Tcl_WideInt)legendPtr->width - PADDING(legendPtr->padX));
    height = LegendLayoutSize((Tcl_WideInt)legendPtr->height - PADDING(legendPtr->padY));
    if ((width <= 0) || (height <= 0)) {
        Rbc_RenderEnd(output);
        return;
    }
    graphPtr = legendPtr->graphPtr;
    if (exportPtr->decorations) {
        if (legendPtr->border != NULL) {
            Rbc_RenderBorder(output, legendPtr->border, x, y, width, height, legendPtr->borderWidth, legendPtr->relief,
                             TRUE);
        } else {
            Rbc_RenderBorder(output, graphPtr->border, x, y, width, height, legendPtr->borderWidth, legendPtr->relief,
                             FALSE);
        }
    } else {
        Rbc_RenderClearRectangle(output, x, y, width, height);
    }
    x += legendPtr->borderWidth;
    y += legendPtr->borderWidth;
    Tk_GetFontMetrics(legendPtr->style.font, &fontMetrics);
    symbolSize = fontMetrics.ascent;
    midX = Rbc_ClampToInt((Tcl_WideInt)symbolSize + 1 + (Tcl_WideInt)legendPtr->entryBorderWidth);
    midY = Rbc_ClampToInt(((Tcl_WideInt)symbolSize / 2) + 1 + (Tcl_WideInt)legendPtr->entryBorderWidth);
    labelX = Rbc_ClampToInt((2 * (Tcl_WideInt)symbolSize) + (Tcl_WideInt)legendPtr->entryBorderWidth +
                             (Tcl_WideInt)legendPtr->ipadX.side1 + 5);
    symbolY = Rbc_ClampToInt((Tcl_WideInt)midY + (Tcl_WideInt)legendPtr->ipadY.side1);
    symbolX = Rbc_ClampToInt((Tcl_WideInt)midX + (Tcl_WideInt)legendPtr->ipadX.side1);
    count = 0;
    startY = y;
    for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainPrevLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->label == NULL || !LegendHasElement(legendPtr, elemPtr)) {
            continue; /* Skip this label */
        }
        if (elemPtr->flags & LABEL_ACTIVE) {
            legendPtr->style.state |= STATE_ACTIVE;
            Rbc_RenderBorder(output, legendPtr->activeBorder, x, y, LegendEntryWidth(legendPtr, elemPtr),
                             legendPtr->style.height, legendPtr->entryBorderWidth, legendPtr->activeRelief, TRUE);
        } else {
            legendPtr->style.state &= ~STATE_ACTIVE;
            if (elemPtr->labelRelief != TK_RELIEF_FLAT) {
                Rbc_RenderBorder(output, graphPtr->border, x, y, LegendEntryWidth(legendPtr, elemPtr),
                                 legendPtr->style.height, legendPtr->entryBorderWidth, elemPtr->labelRelief, FALSE);
            }
        }
        (*elemPtr->procsPtr->exportSymbolProc)(graphPtr, exportPtr, elemPtr, x + symbolX, y + symbolY, symbolSize);
        Rbc_RenderText(output, elemPtr->label, &(legendPtr->style), x + labelX,
                       y + legendPtr->entryBorderWidth + legendPtr->ipadY.side1);
        count++;
        if ((count % legendPtr->nRows) > 0) {
            y += legendPtr->style.height;
        } else {
            x += LegendEntryWidth(legendPtr, elemPtr);
            y = startY;
        }
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup legend_lifetime
 * @brief Consume an idle redraw and paint a mapped legend window.
 *
 * Clears REDRAW_PENDING first. Missing windows are skipped. External windows
 * of at most one pixel are skipped; changed dimensions trigger remapping.
 * Unmapped windows are not painted. The scheduled record must remain live
 * until the callback runs or is cancelled.
 *
 * @param[in,out] clientData Scheduled Legend pointer retained by its owner until callback or
 *    cancellation.
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

/**
 * @ingroup legend_configuration
 * @brief Prepare derived options and external placement before committing.
 *
 * Candidate Tk fields are already installed. Validates nonnegative counts
 * and widths, stages padding/shadow/position and prepares any new external
 * window. Local positions use exact names and reject windows/coordinates.
 * Commits derived state after ordinary failures are past, then resets style
 * and schedules layout/redraw as the mask requires. Returns TCL_OK or
 * TCL_ERROR, freeing staged resources on error. Caller restores Tk fields;
 * callback side effects from expressions/window operations are not rolled back.
 *
 * @param[in,out] graphPtr Borrowed live owning graph; mapping/redraw flags and external associations may
 *    change.
 * @param[in,out] legendPtr Initialized legend with candidate Tk fields; caller restores Tk saved options
 *    on error.
 * @param[in] mask LEGEND_*_CHANGED bits selecting conversion, placement, text-style and layout work.
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
        if (legendPtr->track != NULL) {
            const char *position = Tcl_GetString(legendPtr->positionObjPtr);
            memset(&newPosition, 0, sizeof(newPosition));
            if (strcmp(position, "plotarea") == 0) {
                newPosition.site = LEGEND_PLOT;
            } else if (strcmp(position, "left") == 0) {
                newPosition.site = LEGEND_LEFT;
            } else if (strcmp(position, "right") == 0) {
                newPosition.site = LEGEND_RIGHT;
            } else if (strcmp(position, "top") == 0) {
                newPosition.site = LEGEND_TOP;
            } else if (strcmp(position, "bottom") == 0) {
                newPosition.site = LEGEND_BOTTOM;
            } else {
                Tcl_SetObjResult(
                    graphPtr->interp,
                    Tcl_NewStringObj("bad track legend position: must be plotarea, left, right, top or bottom", -1));
                goto error;
            }
        } else if (GetLegendPositionFromObj(graphPtr->interp, legendPtr->positionObjPtr, &newPosition) != TCL_OK) {
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

/**
 * @ingroup legend_lifetime
 * @brief Free one legend after releasing resources and external associations.
 *
 * Accepts NULL. Cancels idle work, releases Tk resources if the graph window
 * still exists, and destroys any external command/window after clearing its
 * stored handles. Local legends remove their private tag bindings and table;
 * global legends destroy their owned binding table. Frees the record, but
 * does not clear owner pointers or destroy the shared local binding table.
 *
 * @param[in,out] graphPtr Owning graph; Tk resources must already be released if its window is gone.
 * @param[in,out] legendPtr Owned legend to free, or NULL; caller clears its owner pointer and retains
 *    shared local binding infrastructure.
 */
static void DestroyLegend(Graph *graphPtr, Legend *legendPtr) {
    if (legendPtr == NULL) {
        return;
    }
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
    if (legendPtr->track != NULL) {
        Tcl_HashSearch search;
        Tcl_HashEntry *entry;
        for (entry = Tcl_FirstHashEntry(&legendPtr->tags, &search); entry != NULL; entry = Tcl_NextHashEntry(&search)) {
            Rbc_DeleteBindings(legendPtr->bindTable, Tcl_GetHashKey(&legendPtr->tags, entry));
        }
        Tcl_DeleteHashTable(&legendPtr->tags);
    } else if (legendPtr->bindTable != NULL) {
        Rbc_DestroyBindingTable(legendPtr->bindTable);
    }
    ckfree((char *)legendPtr);
}

/**
 * @ingroup legend_lifetime
 * @brief Destroy the global legend and shared local-legend binding table.
 *
 * Clears graph-owned pointers. Local legends must already have been destroyed
 * before their shared binding table disappears. Tk resources should have
 * been released while the graph window was valid.
 *
 * @param[in,out] graphPtr Owning graph after local legends are destroyed; global legend and shared
 *    binding pointers are cleared.
 */
void Rbc_DestroyLegend(Graph *graphPtr) {
    DestroyLegend(graphPtr, graphPtr->legend);
    graphPtr->legend = NULL;
    if (graphPtr->trackLegendBindings != NULL) {
        Rbc_DestroyBindingTable(graphPtr->trackLegendBindings);
        graphPtr->trackLegendBindings = NULL;
    }
}

/**
 * @ingroup legend_lifetime
 * @brief Destroy a local legend and clear its track pointer.
 *
 * Requires the shared local binding table to remain live. An absent legend
 * is harmless. Releases local tag bindings, not the shared binding table;
 * Tk resources must be released before window loss.
 *
 * @param[in,out] graphPtr Owning graph whose shared local-legend binding table remains live.
 * @param[in,out] track Live owning track; its legend is freed if present and its pointer cleared.
 */
void Rbc_DestroyTrackLegend(Graph *graphPtr, RbcTrack *track) {
    DestroyLegend(graphPtr, track->legend);
    track->legend = NULL;
}

/**
 * @ingroup legend_configuration
 * @brief Create and configure the graph-owned legend and its binding table.
 *
 * Requires a graph window and option proxy. Allocates zeroed storage, installs
 * the graph pointer, initializes text style and component options under
 * legend/Legend, then configures derived state. Returns TCL_OK on success.
 * On failure destroys partial legend state and returns TCL_ERROR. Allocation
 * is asserted; this is an internal constructor, not a public stubs API.
 *
 * @param[in,out] graphPtr Live graph with option proxy and no global legend; owns the new record and
 *    bindings on success.
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
        Rbc_CreateBindingTable(graphPtr->interp, graphPtr->tkwin, graphPtr, PickLegendEntry, LegendTags);
    legendPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, legendOptionSpecs);
    if (Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, "legend", "Legend",
                                 (char *)legendPtr, legendPtr->optionTable) != TCL_OK) {
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

/**
 * @ingroup legend_configuration
 * @brief Create an opt-in local legend sharing graph event infrastructure.
 *
 * Requires a track without an existing legend. Creates a private tag table,
 * borrows or lazily creates trackLegendBindings and initializes component
 * options under trackLegend/TrackLegend. Returns TCL_OK on success; failure
 * destroys the partial local legend but retains the graph-owned shared table.
 *
 * @param[in,out] graphPtr Live owning graph; shared local binding table may be created and retained even
 *    after failure.
 * @param[in,out] track Live graph member with no legend; receives the new record on success, NULL after
 *    failure.
 */
int Rbc_CreateTrackLegend(Graph *graphPtr, RbcTrack *track) {
    Legend *legendPtr;

    legendPtr = RbcCalloc(1, sizeof(Legend));
    assert(legendPtr != NULL);
    track->legend = legendPtr;
    legendPtr->track = track;
    Tcl_InitHashTable(&legendPtr->tags, TCL_STRING_KEYS);
    legendPtr->graphPtr = graphPtr;
    legendPtr->tkwin = graphPtr->tkwin;
    Rbc_InitTextStyle(&legendPtr->style);
    legendPtr->style.justify = TK_JUSTIFY_LEFT;
    legendPtr->style.anchor = TK_ANCHOR_NW;
    if (graphPtr->trackLegendBindings == NULL) {
        graphPtr->trackLegendBindings =
            Rbc_CreateBindingTable(graphPtr->interp, graphPtr->tkwin, graphPtr, PickTrackLegendEntry, LegendTags);
    }
    legendPtr->bindTable = graphPtr->trackLegendBindings;
    legendPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, trackLegendOptionSpecs);
    if (Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, "trackLegend", "TrackLegend",
                                 (char *)legendPtr, legendPtr->optionTable) != TCL_OK) {
        goto error;
    }
    legendPtr->optionsInitialized = TRUE;
    if (ConfigureLegend(graphPtr, legendPtr, LEGEND_INITIALIZE_MASK) != TCL_OK) {
        goto error;
    }
    return TCL_OK;

error:
    Rbc_DestroyTrackLegend(graphPtr, track);
    return TCL_ERROR;
}

/**
 * @ingroup legend_commands
 * @brief Query the current entry or pick an entry at \@x,y in this legend.
 *
 * Uses normalized arguments with the selector at objv[3]. Hidden/empty
 * layouts return TCL_OK without setting a name. Local current lookup checks
 * binding context; coordinate lookup uses PickLegendIn. Does not resolve
 * ordinary element-name strings. Malformed coordinates return TCL_ERROR
 * with the parser diagnostic, even for hidden or empty legends. Valid queries
 * with no matching entry return TCL_OK without setting a name.
 *
 * @param[in,out] legendPtr Borrowed live legend selected by global or nested track dispatch.
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] objc Validated count of four; selector is objv[3].
 * @param[in] objv Borrowed normalized command vector; see @ref rbc_command_arguments.
 */
static int GetOp(Legend *legendPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Graph *graphPtr = legendPtr->graphPtr;
    Element *elemPtr;
    int x, y;

    const char *str = Tcl_GetString(objv[3]);
    char c = str[0];
    /* Validate coordinates even when no legend entry can be displayed. */
    if ((c == '@') && (Rbc_GetXY(interp, graphPtr->tkwin, str, &x, &y) != TCL_OK)) {
        return TCL_ERROR;
    }
    if ((legendPtr->hidden) || (legendPtr->nEntries == 0)) {
        return TCL_OK;
    }
    elemPtr = NULL;
    if ((c == 'c') && (strcmp(str, "current") == 0)) {
        if (legendPtr->track == NULL || Rbc_GetCurrentContext(legendPtr->bindTable) == legendPtr) {
            elemPtr = (Element *)Rbc_GetCurrentItem(legendPtr->bindTable);
        }
    } else if (c == '@') {
        elemPtr = (Element *)PickLegendIn(legendPtr, x, y);
    }
    if (elemPtr != NULL && LegendHasElement(legendPtr, elemPtr)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->name, -1));
    }
    return TCL_OK;
}

/**
 * @ingroup legend_commands
 * @brief Set matching element label flags and return active names in scope.
 *
 * Normalized operation name selects activation or deactivation. Matches
 * patterns against the complete element table, filtered by track, and changes
 * shared LABEL_ACTIVE flags. No patterns changes nothing. Returns a Tcl list
 * of all active names in scope, in hash order, including undisplayed elements.
 * Schedules graph/backing redraw for plot legends and cheaper legend redraw
 * when appropriate; track graphs also refresh other copies of shared entries.
 *
 * @param[in,out] legendPtr Borrowed live legend selected by global or nested track dispatch.
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] objc Normalized count including operation at index two and zero or more patterns from index
 *    three.
 * @param[in] objv Borrowed normalized command vector; see @ref rbc_command_arguments.
 */
static int ActivateOp(Legend *legendPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Graph *graphPtr = legendPtr->graphPtr;
    Element *elemPtr;
    unsigned int active, redraw;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Tcl_Size i;
    Tcl_Obj *resultObj;

    const char *str = Tcl_GetString(objv[2]);
    active = (str[0] == 'a') ? LABEL_ACTIVE : 0;
    redraw = 0;
    for (hPtr = Tcl_FirstHashEntry(&(graphPtr->elements.table), &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = Tcl_GetHashValue(hPtr);
        if (!LegendHasElement(legendPtr, elemPtr)) {
            continue;
        }
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
    if (redraw && graphPtr->trackGraph) {
        graphPtr->flags |= REDRAW_WORLD | REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
        Rbc_RedrawExternalLegend(graphPtr);
    }
    if ((redraw) && (!legendPtr->hidden)) {
        /*
         * A legend drawn inside the plotting area is part of the plot
         * composition.  It cannot be redrawn directly into the graph
         * window:
         *
         *  - the backing store would retain the old legend state;
         *  - element/marker drawing order would not be preserved;
         *  - the legend would not be clipped to the plotting area.
         *
         * Rebuild the plot backing store instead.  Legends in margins
         * or external windows can still use the cheaper legend-only
         * redraw when no graph redraw is already pending.
         */
        if (legendPtr->site & LEGEND_IN_PLOT) {
            graphPtr->flags |= REDRAW_WORLD | REDRAW_BACKING_STORE;
            Rbc_EventuallyRedrawGraph(graphPtr);
        } else if (graphPtr->flags & REDRAW_PENDING) {
            graphPtr->flags |= REDRAW_WORLD;
        } else {
            EventuallyRedrawLegend(legendPtr);
        }
    }
    /* Return the names of all the active legend entries */
    resultObj = Tcl_NewListObj(0, NULL);
    for (hPtr = Tcl_FirstHashEntry(&(graphPtr->elements.table), &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = Tcl_GetHashValue(hPtr);
        if (!LegendHasElement(legendPtr, elemPtr)) {
            continue;
        }
        if (elemPtr->flags & LABEL_ACTIVE) {
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(elemPtr->name, -1));
        }
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/**
 * @ingroup legend_commands
 * @brief List interned tags or delegate a legend binding query/update.
 *
 * Normalized arguments begin with the operation at objv[2]. With no tag,
 * returns the owning tag table's keys in hash order; these need not all have
 * scripts. Otherwise interns the supplied tag and delegates remaining
 * arguments to the shared binding service. Returns its Tcl status.
 *
 * @param[in,out] legendPtr Borrowed live legend selected by global or nested track dispatch.
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] objc Validated normalized count from three through six: optional tag, sequence and script.
 * @param[in] objv Borrowed normalized command vector; see @ref rbc_command_arguments.
 */
static int BindOp(Legend *legendPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Graph *graphPtr = legendPtr->graphPtr;
    if (objc == 3) {
        Tcl_HashEntry *hPtr;
        Tcl_HashSearch cursor;
        const char *tagName;

        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
        for (hPtr = Tcl_FirstHashEntry(legendPtr->track ? &legendPtr->tags : &graphPtr->elements.tagTable, &cursor);
             hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
            tagName = Tcl_GetHashKey(legendPtr->track ? &legendPtr->tags : &graphPtr->elements.tagTable, hPtr);
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(tagName, -1));
        }
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    return Rbc_ConfigureBindingsFromObj(interp, legendPtr->bindTable, LegendTag(legendPtr, Tcl_GetString(objv[3])),
                                        objc - 4, objv + 4);
}

/**
 * @ingroup legend_commands
 * @brief Return a retained legend option value through Tk.
 *
 * Borrows a live legend and normalized option name at objv[3]. Returns
 * TCL_OK with the value or TCL_ERROR from Tk. Does not configure or redraw.
 *
 * @param[in,out] legendPtr Borrowed live legend selected by global or nested track dispatch.
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] objc Validated count of four; option name is objv[3].
 * @param[in] objv Borrowed normalized command vector; see @ref rbc_command_arguments.
 */
static int CgetOp(Legend *legendPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Graph *graphPtr = legendPtr->graphPtr;
    Tcl_Obj *resultObjPtr;

    resultObjPtr = Tk_GetOptionValue(interp, (char *)legendPtr, legendPtr->optionTable, objv[3], graphPtr->tkwin);
    if (resultObjPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, resultObjPtr);
    return TCL_OK;
}

/**
 * @ingroup legend_commands
 * @brief Query options or apply a saved-options configuration transaction.
 *
 * Normalized objc of three queries all options; four queries one. Other
 * calls install candidate values with Tk_SetOptions and ConfigureLegend.
 * Derived failure restores saved options while preserving the diagnostic;
 * success frees saved options. Returns a Tcl status. Does not undo arbitrary
 * application side effects from coordinate expressions or window callbacks.
 *
 * @param[in,out] legendPtr Borrowed live legend selected by global or nested track dispatch.
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] objc Normalized count: three queries all options, four queries one, otherwise option/value
 *    pairs start at three.
 * @param[in] objv Borrowed normalized command vector; see @ref rbc_command_arguments.
 */
static int ConfigureOp(Legend *legendPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Graph *graphPtr = legendPtr->graphPtr;
    Tcl_Obj *resultObjPtr;
    Tk_SavedOptions savedOptions;
    int mask;

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
        return Rbc_RestoreOptionsOnError(interp, &savedOptions);
    }
    Tk_FreeSavedOptions(&savedOptions);
    return TCL_OK;
}

static const LegendOpSpec legendOps[] = {{{"activate", 3, 0, "?pattern?..."}, ActivateOp},
                                         {{"bind", 3, 6, "elemName sequence command"}, BindOp},
                                         {{"cget", 4, 4, "option"}, CgetOp},
                                         {{"configure", 3, 0, "?option value?..."}, ConfigureOp},
                                         {{"deactivate", 3, 0, "?pattern?..."}, ActivateOp},
                                         {{"get", 4, 4, "index"}, GetOp},
                                         {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup legend_commands
 * @brief Validate and dispatch a graph-wide legend operation.
 *
 * Borrows the complete graph-command argument vector, with operation at
 * index two. Shared dispatch checks spelling and arity, then calls the
 * selected handler. Returns TCL_OK/TCL_ERROR; this dispatcher adds no layout
 * refresh or object-lifetime preservation around the handler.
 *
 * @param[in,out] graphPtr Live graph with initialized global legend; enclosing caller owns lifetime
 *    protection.
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] objc Full widget-command count; operation is at index two.
 * @param[in] objv Borrowed full widget-command vector; see @ref rbc_command_arguments.
 */
int Rbc_LegendOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, legendOps, (Tcl_Size)sizeof(legendOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    return legendOps[index].proc(graphPtr->legend, interp, objc, objv);
}

/**
 * @ingroup legend_layout
 * @brief Return the legend's placement code.
 *
 * Requires a non-NULL live legend. Reads site without remapping, scheduling
 * work or acquiring a reference; cached geometry need not be current.
 *
 * @param[in] legendPtr Non-NULL live legend; reads cached state without refreshing layout or acquiring a
 *    reference.
 */
int Rbc_LegendSite(Legend *legendPtr) { return legendPtr->site; }

/**
 * @ingroup legend_layout
 * @brief Return the legend's cached width.
 *
 * Requires a non-NULL live legend. Reads width without remapping, scheduling
 * work or acquiring a reference; cached geometry need not be current.
 *
 * @param[in] legendPtr Non-NULL live legend; reads cached state without refreshing layout or acquiring a
 *    reference.
 */
int Rbc_LegendWidth(Legend *legendPtr) { return legendPtr->width; }

/**
 * @ingroup legend_layout
 * @brief Return the legend's cached height.
 *
 * Requires a non-NULL live legend. Reads height without remapping, scheduling
 * work or acquiring a reference; cached geometry need not be current.
 *
 * @param[in] legendPtr Non-NULL live legend; reads cached state without refreshing layout or acquiring a
 *    reference.
 */
int Rbc_LegendHeight(Legend *legendPtr) { return legendPtr->height; }

/**
 * @ingroup legend_layout
 * @brief Return the legend's hidden flag.
 *
 * Requires a non-NULL live legend. Reads hidden without remapping, scheduling
 * work or acquiring a reference; cached geometry need not be current.
 *
 * @param[in] legendPtr Non-NULL live legend; reads cached state without refreshing layout or acquiring a
 *    reference.
 */
int Rbc_LegendIsHidden(Legend *legendPtr) { return legendPtr->hidden; }

/**
 * @ingroup legend_layout
 * @brief Return the legend's raised flag.
 *
 * Requires a non-NULL live legend. Reads raised without remapping, scheduling
 * work or acquiring a reference; cached geometry need not be current.
 *
 * @param[in] legendPtr Non-NULL live legend; reads cached state without refreshing layout or acquiring a
 *    reference.
 */
int Rbc_LegendIsRaised(Legend *legendPtr) { return legendPtr->raised; }

/**
 * @ingroup legend_layout
 * @brief Return the legend's cached origin X.
 *
 * Requires a non-NULL live legend. Reads x without remapping, scheduling
 * work or acquiring a reference; cached geometry need not be current.
 *
 * @param[in] legendPtr Non-NULL live legend; reads cached state without refreshing layout or acquiring a
 *    reference.
 */
int Rbc_LegendX(Legend *legendPtr) { return legendPtr->x; }

/**
 * @ingroup legend_layout
 * @brief Return the legend's cached origin Y.
 *
 * Requires a non-NULL live legend. Reads y without remapping, scheduling
 * work or acquiring a reference; cached geometry need not be current.
 *
 * @param[in] legendPtr Non-NULL live legend; reads cached state without refreshing layout or acquiring a
 *    reference.
 */
int Rbc_LegendY(Legend *legendPtr) { return legendPtr->y; }

/**
 * @ingroup legend_bindings
 * @brief Forget an element object in global and shared local binding state.
 *
 * Passes the borrowed element pointer to Rbc_DeleteBindings in both relevant
 * tables, clearing current/new item state as needed. Does not remove interned
 * string tags, erase tag scripts by name, free the element or edit display order.
 *
 * @param[in,out] legendPtr Live legend identifying the global/shared-local binding state to clear.
 * @param[in] elemPtr Borrowed element identity to forget; neither the record nor its string tags are
 *    freed.
 */
void Rbc_LegendRemoveElement(Legend *legendPtr, Element *elemPtr) {
    Rbc_DeleteBindings(legendPtr->bindTable, elemPtr);
    if (legendPtr->graphPtr->trackLegendBindings != NULL) {
        Rbc_DeleteBindings(legendPtr->graphPtr->trackLegendBindings, elemPtr);
    }
}

/**
 * @ingroup legend_lifetime
 * @brief Refresh local/global text styles and external redraw scheduling.
 *
 * Borrows graph and track records. Resets styles only when window and font
 * are available; an external global legend receives an idle redraw request.
 * Does not itself remap legends or schedule in-widget graph redisplay.
 *
 * @param[in,out] graphPtr Borrowed graph with valid track list; available styles are reset and external
 *    redraw may be queued.
 */
void Rbc_LegendFontsChanged(Graph *graphPtr) {
    RbcTrack *track;
    Legend *legendPtr = graphPtr->legend;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        if (track->legend != NULL && track->legend->tkwin != NULL && track->legend->style.font != NULL) {
            Rbc_ResetTextStyle(track->legend->tkwin, &track->legend->style);
        }
    }

    if ((legendPtr == NULL) || (legendPtr->tkwin == NULL) || (legendPtr->style.font == NULL)) {
        return;
    }
    Rbc_ResetTextStyle(legendPtr->tkwin, &legendPtr->style);
    if (legendPtr->site == LEGEND_WINDOW) {
        EventuallyRedrawLegend(legendPtr);
    }
}

/**
 * @ingroup legend_layout
 * @brief Map local legends after full track geometry is available.
 *
 * Uses world widths/heights, then computes origins. Local legends do not
 * reserve graph margins. Borrows graph and tracks, and does not draw or
 * schedule redisplay; visible clipping is handled separately.
 *
 * @param[in,out] graphPtr Borrowed graph with prepared full track world rectangles; local dimensions and
 *    origins are updated.
 */
void Rbc_MapTrackLegends(Graph *graphPtr) {
    RbcTrack *track;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        if (track->legend != NULL) {
            Rbc_MapLegend(track->legend, track->worldRight - track->worldLeft, track->worldBottom - track->worldTop);
            SetLegendOrigin(track->legend);
        }
    }
}

/**
 * @ingroup legend_output
 * @brief Draw local legends in one requested raised layer.
 *
 * Borrows tracks in list order. Selects legends with matching raised flag
 * and a noncollapsed visible track rectangle; Rbc_DrawLegend handles hidden
 * and empty legends and clips the final copy. Does not remap tracks.
 *
 * @param[in,out] graphPtr Borrowed graph with mapped local legends and current visible track bounds.
 * @param[in] drawable Borrowed compatible graph destination; each local copy is clipped to its visible
 *    track.
 * @param[in] raised Exact raised flag to select, normally zero or one; not an arbitrary truth value.
 */
void Rbc_DrawTrackLegends(Graph *graphPtr, Drawable drawable, int raised) {
    RbcTrack *track;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        if (track->legend != NULL && track->legend->raised == raised && track->bottom > track->top &&
            track->right > track->left) {
            Rbc_DrawLegend(track->legend, drawable);
        }
    }
}

/**
 * @ingroup legend_output
 * @brief Export a local legend layer within each visible track clip.
 *
 * Selects matching raised flags and noncollapsed track rectangles. Opens an
 * output context, pushes the inclusive visible rectangle as a clip, exports
 * the legend, then balances clip/context cleanup. Does not remap track geometry.
 *
 * @param[in,out] graphPtr Borrowed graph with mapped local legends and visible track bounds.
 * @param[in,out] exportPtr Borrowed initialized export context; each local clip is balanced before
 *    return.
 * @param[in] raised Exact raised flag to select, normally zero or one.
 */
void Rbc_ExportTrackLegends(Graph *graphPtr, Rbc_ExportContext *exportPtr, int raised) {
    RbcTrack *track;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        if (track->legend != NULL && track->legend->raised == raised && track->bottom > track->top &&
            track->right > track->left) {
            Rbc_RenderContext *context = Rbc_RenderBeginExportOutput(exportPtr);
            Rbc_RenderClipBegin(context, track->left, track->top, track->right - track->left + 1,
                                track->bottom - track->top + 1);
            Rbc_LegendExport(track->legend, exportPtr);
            Rbc_RenderClipEnd(context);
            Rbc_RenderEnd(context);
        }
    }
}

/**
 * @ingroup legend_commands
 * @brief Return the clipped local legend box as x y width height.
 *
 * Requires current track/legend layout and a local legend. Hidden, empty or
 * fully clipped legends leave no result. Intersects the legend extent with
 * inclusive visible track bounds and returns pixel dimensions, not right and
 * bottom coordinates. Recomputes origin but does not remap.
 *
 * @param[in,out] legendPtr Borrowed live local legend with current track layout.
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] objc Validated count of three; unused by this handler.
 * @param[in] objv Unused normalized command vector.
 */
static int TrackLegendBboxOp(Legend *legendPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    RbcTrack *track = legendPtr->track;
    int left, top, right, bottom;

    if (legendPtr->hidden || legendPtr->nEntries == 0) {
        return TCL_OK;
    }
    SetLegendOrigin(legendPtr);
    left = MAX(track->left, legendPtr->x);
    top = MAX(track->top, legendPtr->y);
    right = MIN(track->right + 1, Rbc_ClampToInt((Tcl_WideInt)legendPtr->x + legendPtr->width));
    bottom = MIN(track->bottom + 1, Rbc_ClampToInt((Tcl_WideInt)legendPtr->y + legendPtr->height));
    if (right > left && bottom > top) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%d %d %d %d", left, top, right - left, bottom - top));
    }
    return TCL_OK;
}

/* Local legends share handlers with the global legend, but also expose bbox. */
static const LegendOpSpec trackLegendOps[] = {{{"activate", 3, 0, "?pattern?..."}, ActivateOp},
                                              {{"bbox", 3, 3, ""}, TrackLegendBboxOp},
                                              {{"bind", 3, 6, "elemName sequence command"}, BindOp},
                                              {{"cget", 4, 4, "option"}, CgetOp},
                                              {{"configure", 3, 0, "?option value?..."}, ConfigureOp},
                                              {{"deactivate", 3, 0, "?pattern?..."}, ActivateOp},
                                              {{"get", 4, 4, "index"}, GetOp},
                                              {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup legend_commands
 * @brief Normalize a nested track-legend command and refresh layout before dispatch.
 *
 * Requires a track with a live legend. Creates an owned normalized argument
 * vector, validates operation/arity, refreshes layout and dispatches only on
 * success, then releases the vector. Returns the Tcl status of the failing
 * stage or handler. Borrows graph/track/legend; adds no preservation boundary
 * around layout callbacks.
 *
 * @param[in,out] graphPtr Live graph retained across layout callbacks by the enclosing caller.
 * @param[in,out] track Live graph member with an existing legend; borrowed across layout and handler
 *    dispatch.
 * @param[in] interp Borrowed interpreter receiving results and diagnostics; see @ref
 *    rbc_interp_argument.
 * @param[in] objc Full nested-command count; operation is at index four.
 * @param[in] objv Borrowed nested command; a temporary adapted vector is freed before return. See @ref
 *    rbc_command_arguments.
 */
int Rbc_TrackLegendOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj **args;
    Tcl_Size count = objc - 2;
    int index, code;
    Legend *legendPtr = track->legend;
    if (objc < 5) {
        Tcl_WrongNumArgs(interp, 4, objv, "operation ?arg ...?");
        return TCL_ERROR;
    }
    args = Rbc_CreateTrackComponentArgs(objc, objv);
    code = Rbc_GetOpIndexFromObj(interp, trackLegendOps, (Tcl_Size)sizeof(trackLegendOps[0]), RBC_OP_ARG2, count,
                                 args, &index);
    if (code == TCL_OK) {
        code = Rbc_LayoutGraph(graphPtr);
        if (code == TCL_OK) {
            code = trackLegendOps[index].proc(legendPtr, interp, count, args);
        }
    }
    Rbc_FreeTrackComponentArgs(args);
    return code;
}
