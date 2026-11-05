/** @file rbcGrElem.h
 * @brief Private element data ownership and component dispatch.
 */
/*
 * rbcGrElem.h --
 *
 *      Shared element state, dispatch and staged resource transactions.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCGRELEM
#define _RBCGRELEM

#include "rbcGraph.h"

#define SEARCH_X 0
#define SEARCH_Y 1
#define SEARCH_BOTH 2
#define SHOW_NONE 0
#define SHOW_X 1
#define SHOW_Y 2
#define SHOW_BOTH 3

/*
 * Maximum storage used for a formatted element value label.
 *
 * Value formats are separately constrained in length, field width,
 * and precision.  Formatting is also bounded with snprintf(), so this
 * limit is a final output bound rather than a memory-safety assumption.
 */
#define RBC_VALUE_LABEL_SIZE 2048
#define SEARCH_POINTS 0 /* Search for closest data point. */
#define SEARCH_TRACES                                                                                                  \
    1 /* Search for closest point on trace.                                                                            \
       * Interpolate the connecting line segments                                                                      \
       * if necessary. */
#define SEARCH_AUTO                                                                                                    \
    2 /* Automatically determine whether to search                                                                     \
       * for data points or traces.  Look for                                                                          \
       * traces if the linewidth is > 0 and if                                                                         \
       * there is more than one data point. */
#define ELEM_ACTIVE                                                                                                    \
    (1 << 8) /* Non-zero indicates that the element                                                                    \
              * should be drawn in its active                                                                          \
              * foreground and background                                                                              \
              * colors. */
#define ACTIVE_PENDING (1 << 7)
#define LABEL_ACTIVE                                                                                                   \
    (1 << 9) /* Non-zero indicates that the                                                                            \
              * element's entry in the legend                                                                          \
              * should be drawn in its active                                                                          \
              * foreground and background                                                                              \
              * colors. */
#define SCALE_SYMBOL (1 << 10)

#define NumberOfXYPoints(e) MIN((e)->x.nValues, (e)->y.nValues)

/** @brief Cached numeric interval used for palette selection.
 * SetWeight initializes the endpoints and SetRange uses DBL_EPSILON when the endpoints do not form a positive interval.
 */
typedef struct {
    double min; /**< Lower interval endpoint in weight units. */
    double max; /**< Upper interval endpoint in weight units. */
    double range; /**< Positive cached extent; max-min or DBL_EPSILON. */
} Weight;

#define SetRange(l) ((l).range = ((l).max > (l).min) ? ((l).max - (l).min) : DBL_EPSILON)
#define SetScale(l) ((l).scale = 1.0 / (l).range)
#define SetWeight(l, lo, hi) ((l).min = (lo), (l).max = (hi), SetRange(l))

/** @brief Palette entry and per-pen slices of mapped element geometry.
 * Named pen references are released with the palette; normal/default entries can borrow an embedded pen. Error-segment pointers borrow slices of element-owned storage.
 */
typedef struct {
    Weight weight; /**< Weight range where this pen is valid. */
    Pen *penPtr; /**< Selected pen; reference ownership follows the palette entry creation path. */
    Segment2D *xErrorBars; /**< Borrowed start of this style X-error segment slice. */
    Segment2D *yErrorBars; /**< Borrowed start of this style Y-error segment slice. */
    Tcl_Size xErrorBarCnt; /**< Number of mapped X-error segments in this style slice. */
    Tcl_Size yErrorBarCnt; /**< Number of mapped Y-error segments in this style slice. */
    int errorBarCapWidth; /**< Mapped cap length in display pixels. */
    int symbolSize; /**< Mapped symbol size in display pixels. */
} PenStyle;

/** @brief Resolved error-bar drawing policy.
 * The concrete pen owns configured resources; this record does not own element data or mapped segment arrays.
 */
typedef struct {
    XColor *color; /**< Tk-managed error-stroke color or the concrete pen fallback selector. */
    int lineWidth; /**< Stroke width in display pixels. */
    GC gc; /**< Configured error-stroke GC released with the concrete pen. */
    int show; /**< SHOW_X/SHOW_Y visibility mask, or neither for no errors. */
} ErrorBarAttributes;

/** @brief Input query and mutable closest-element result.
 * A search callback improves the current candidate in place. Result element pointers are borrowed and valid only while the graph/components remain live.
 */
typedef struct {
    int halo; /**< Maximum accepted distance in display pixels. */
    int mode; /**<
     * Indicates whether to find the closest data point or the closest point on the trace by interpolating the line
     * segments. Can also be SEARCH_AUTO, indicating to choose how to search.
     */
    int x; /**< Test-point X in display pixels. */
    int y; /**< Test-point Y in display pixels. */
    int along; /**< Indicates to let search run along a particular axis: x, y, or both. */
    Element *elemPtr; /**< Borrowed winning element pointer, or NULL before a match. */
    Point2D point; /**< Winning point in graph data coordinates, possibly interpolated. */
    Tcl_Size index; /**< Original source-point index associated with the candidate. */
    double dist; /**< Current candidate distance in display pixels. */
} ClosestSearch;

/** @brief Draw mapped normal or active element geometry to a screen drawable.
 * @param[inout] graphPtr Borrowed live owning graph; the callback must respect its current lifecycle phase.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[inout] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 */
typedef void(ElementDrawProc)(Graph *graphPtr, Drawable drawable, Element *elemPtr);
/** @brief Append mapped normal or active element geometry to a document.
 * @param[inout] graphPtr Borrowed live owning graph; the callback must respect its current lifecycle phase.
 * @param[inout] exportPtr Borrowed initialized document context; output is appended without transferring ownership.
 * @param[inout] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 */
typedef void(ElementExportProc)(Graph *graphPtr, Rbc_ExportContext *exportPtr, Element *elemPtr);
/** @brief Release concrete element resources before generic record reclamation.
 * @param[inout] graphPtr Borrowed live owning graph; the callback must respect its current lifecycle phase.
 * @param[inout] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 */
typedef void(ElementDestroyProc)(Graph *graphPtr, Element *elemPtr);
/** @brief Validate element option state and prepare concrete resources.
 * @param[inout] graphPtr Borrowed live owning graph; the callback must respect its current lifecycle phase.
 * @param[inout] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 * @return TCL_OK on success or TCL_ERROR with graph interpreter diagnostics.
 */
typedef int(ElementConfigProc)(Graph *graphPtr, Element *elemPtr);
/** @brief Rebuild mapped element geometry after data or layout changes.
 * @param[inout] graphPtr Borrowed live owning graph; the callback must respect its current lifecycle phase.
 * @param[inout] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 */
typedef void(ElementMapProc)(Graph *graphPtr, Element *elemPtr);
/** @brief Compute source-data extents for automatic axis limits.
 * @param[in] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 * @param[out] extsPtr Required caller-owned source-data bounds.
 */
typedef void(ElementExtentsProc)(Element *elemPtr, Extents2D *extsPtr);
/** @brief Improve the caller closest-point candidate using mapped geometry.
 * @param[inout] graphPtr Borrowed live owning graph; the callback must respect its current lifecycle phase.
 * @param[inout] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 * @param[inout] searchPtr Borrowed query and current candidate; update only according to the concrete closest-search rules.
 */
typedef void(ElementClosestProc)(Graph *graphPtr, Element *elemPtr, ClosestSearch *searchPtr);
/** @brief Draw a concrete element legend sample on the screen.
 * @param[inout] graphPtr Borrowed live owning graph; the callback must respect its current lifecycle phase.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[inout] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 * @param[in] x Mapped sample X coordinate in display pixels.
 * @param[in] y Mapped sample Y coordinate in display pixels.
 * @param[in] symbolSize Requested legend symbol size in display pixels.
 * @param[in] width Available screen target width in pixels.
 * @param[in] height Available screen target height in pixels.
 */
typedef void(ElementDrawSymbolProc)(Graph *graphPtr, Drawable drawable, Element *elemPtr, int x, int y, int symbolSize,
                                    int width, int height);
/** @brief Append a concrete element legend sample to a document.
 * @param[inout] graphPtr Borrowed live owning graph; the callback must respect its current lifecycle phase.
 * @param[inout] exportPtr Borrowed initialized document context; output is appended without transferring ownership.
 * @param[inout] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 * @param[in] x Mapped sample X coordinate in display pixels.
 * @param[in] y Mapped sample Y coordinate in display pixels.
 * @param[in] symSize Requested legend symbol size in display pixels.
 */
typedef void(ElementSymbolExportProc)(Graph *graphPtr, Rbc_ExportContext *exportPtr, Element *elemPtr, double x, double y,
                                            int symSize);
/** @brief Report the concrete element source-point count.
 * @param[in] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 * @return Number of source points; does not transfer or allocate data storage.
 */
typedef Tcl_Size(ElementPointCountProc)(Element *elemPtr);
/* Append owned values to a private result dictionary; never write Tcl variables
 * or run callbacks. The caller publishes it after all element reads finish. */
/** @brief Append concrete closest-point fields to a private result dictionary.
 * Do not run Tcl callbacks or publish Tcl variables while reading element state.
 * @param[inout] graphPtr Borrowed live owning graph; the callback must respect its current lifecycle phase.
 * @param[inout] elemPtr Borrowed concrete element; generic lifecycle code owns record reclamation.
 * @param[in] searchPtr Borrowed completed closest-search result; not modified.
 * @param[inout] resultObjPtr Caller-owned private result dictionary; append owned Tcl values without writing variables or running callbacks.
 * @return Tcl status; the caller publishes the dictionary only after element reads finish.
 */
typedef int(ElementClosestInfoProc)(Graph *graphPtr, Element *elemPtr, const ClosestSearch *searchPtr,
                                    Tcl_Obj *resultObjPtr);

/** @brief Immutable operation table for a concrete element type.
 * Callbacks borrow graph and element pointers; table dispatch does not itself protect against callback deletion. Optional entries must be checked by their caller before use.
 */
typedef struct {
    ElementClosestProc *closestProc; /**<
     * Improve a ClosestSearch candidate using this concrete element; the search and element are borrowed.
     */
    ElementConfigProc *configProc; /**<
     * Validate configured values and rebuild concrete resources; returns Tcl status.
     */
    ElementDestroyProc *destroyProc; /**<
     * Release concrete element resources; generic lifecycle code owns registry and record teardown.
     */
    ElementDrawProc *drawActiveProc; /**< Draw active element geometry to the borrowed screen drawable. */
    ElementDrawProc *drawNormalProc; /**< Draw normal element geometry to the borrowed screen drawable. */
    ElementDrawSymbolProc *drawSymbolProc; /**< Draw a legend sample using borrowed element/style resources. */
    ElementExtentsProc *extentsProc; /**< Write source-data limits to caller-owned Extents2D storage. */
    ElementExportProc *exportActiveProc; /**< Append active geometry to the borrowed document context. */
    ElementExportProc *exportNormalProc; /**< Append normal geometry to the borrowed document context. */
    ElementSymbolExportProc *exportSymbolProc; /**<
     * Append the element legend sample to the borrowed document context.
     */
    ElementMapProc *mapProc; /**< Rebuild element-owned screen geometry from current data and axis mapping. */
    ElementPointCountProc *pointCountProc; /**<
     * Return the concrete element source-point count without transferring storage.
     */
    ElementClosestInfoProc *closestInfoProc; /**<
     * Append concrete closest-point fields to the caller result object; returns Tcl status.
     */
} ElementProcs;

/** @brief Element data slot backed by a list copy or an observed RBC vector.
 * List-backed slots own value storage; vector-backed slots retain a client token and borrow current vector storage. Free through Rbc_FreeElemVector rather than freeing valueArr unconditionally.
 */
typedef struct {
    Rbc_Vector *vecPtr; /**< Borrowed external RBC vector record when vector-backed, otherwise NULL. */
    double *valueArr; /**< Owned list-value array or borrowed external vector values; ownership follows clientId. */
    Tcl_Size nValues; /**< Number of available scalar values in this slot. */
    Tcl_Size arraySize; /**< Allocated or external storage capacity associated with valueArr. */
    double min; /**< Cached minimum value used for element limits. */
    double max; /**< Cached maximum value used for element limits. */
    Rbc_VectorId clientId; /**< Owned observation token for an external vector, or NULL for list-backed data. */
    Element *elemPtr; /**< Borrowed element receiving data-change notifications. */
    int changePending; /**< Nonzero after at least one notification since the last mapping. */
    int changeAll; /**< Nonzero when notifications do not provide an exact source range. */
    Tcl_Size changedFirst; /**< First source index covered by the accumulated change range. */
    Tcl_Size changedLast; /**< Last source index covered by the accumulated change range. */
} ElemVector;

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    unsigned int stagedMask; /**< Bit mask selecting staged replacements to commit or release. */
    ElemVector x; /**< Replacement X-data descriptor staged for commit or abort. */
    ElemVector y; /**< Replacement Y-data descriptor staged for commit or abort. */
    ElemVector w; /**< Replacement weight-data descriptor staged for commit or abort. */
    ElemVector xError; /**< Replacement symmetric X-error data staged for commit or abort. */
    ElemVector xHigh; /**< Replacement explicit X-high data staged for commit or abort. */
    ElemVector xLow; /**< Replacement explicit X-low data staged for commit or abort. */
    ElemVector yError; /**< Replacement symmetric Y-error data staged for commit or abort. */
    ElemVector yHigh; /**< Replacement explicit Y-high data staged for commit or abort. */
    ElemVector yLow; /**< Replacement explicit Y-low data staged for commit or abort. */
} ElemDataTransaction;

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    unsigned int stagedMask; /**< Bit mask selecting staged replacements to commit or release. */
    Pen *activePenPtr; /**< Retained replacement active pen staged for commit or abort. */
    Pen *normalPenPtr; /**< Retained replacement normal pen staged for commit or abort. */
} ElemPenTransaction;

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    unsigned int stagedMask; /**< Bit mask selecting staged replacements to commit or release. */
    Axis *xAxisPtr; /**< Retained replacement X axis staged for commit or abort. */
    Axis *yAxisPtr; /**< Retained replacement Y axis staged for commit or abort. */
} ElemAxisTransaction;

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    int staged; /**< Nonzero when the transaction contains a replacement to commit or release. */
    int state; /**< Validated interaction state staged for commit or abort. */
} ElemStateTransaction;

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    int staged; /**< Nonzero when the transaction contains a replacement to commit or release. */
    char **tags; /**< Owned replacement binding-tag array staged for commit or abort. */
} ElemTagsTransaction;

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    int staged; /**< Nonzero when the transaction contains a replacement to commit or release. */
    Rbc_Chain *palette; /**< Owned replacement style palette staged for commit or abort. */
} ElemStylesTransaction;

/** @brief Shared prefix of graph element implementations.
 * Element is the typedef for this record. The graph owns registered elements; concrete records extend this prefix. Data slots, named-pen references and mapped geometry have distinct cleanup paths.
 */
struct ElementStruct {
    char *name; /**< Element identifier used by the graph registry and widget subcommands. */
    Rbc_Uid classUid; /**< Type of element */
    Graph *graphPtr; /**< Borrowed owning graph; does not retain the widget. */
    unsigned int flags; /**< Indicates if the entire element is active, or if coordinates need to be calculated */
    char **tags; /**< Owned configured binding-tag list; tag keys are resolved during dispatch. */
    int hidden; /**< If non-zero, don't display the element. */
    int plotHidden; /**< If non-zero, don't plot the element, but keep it in the display list and legend. */
    Tcl_HashEntry *hashPtr; /**< Registry entry referring to this record; NULL after detachment. */
    char *label; /**< Tk option-managed legend label. */
    int labelRelief; /**< Relief of label in legend. */
    Tcl_Obj *trackObjPtr; /**< Tk option-managed Tcl representation for track; released with option state. */
    RbcTrack *track; /**< Borrowed owning track, or NULL for an ordinary graph element. */
    Axis2D axes; /**< Retained X/Y axis references, released during element cleanup. */
    ElemVector x; /**< X-coordinate data slot. */
    ElemVector y; /**< Y-coordinate data slot. */
    ElemVector w; /**< Palette weight data slot. */
    ElemVector xError; /**< Symmetric X-error magnitude data slot. */
    ElemVector yError; /**< Symmetric Y-error magnitude data slot. */
    ElemVector xHigh; /**< Explicit high X-error endpoint data slot. */
    ElemVector xLow; /**< Explicit low X-error endpoint data slot. */
    ElemVector yHigh; /**< Explicit high Y-error endpoint data slot. */
    ElemVector yLow; /**< Explicit low Y-error endpoint data slot. */
    Tcl_Size *activeIndices; /**<
     * Array of indices (malloc-ed) which indicate which data points are active (drawn with "active" colors).
     */
    Tcl_Size nActiveIndices; /**<
     * Number of active data points. Special case: if nActiveIndices < 0 and the active bit is set in "flags", then
     * all data points are drawn active.
     */
    ElementProcs *procsPtr; /**< Borrowed immutable concrete-element operation table. */
    const Tk_OptionSpec *optionSpecs; /**< Borrowed immutable option specifications for the concrete component type. */
    Tk_OptionTable optionTable; /**<
     * Tk option table associated with this record; managed by its configuration lifecycle.
     */
    int optionMask; /**< Change mask from the current option transaction. */
    Tcl_Size optionObjc; /**< Number of objects in the current borrowed configure argument slice. */
    Tcl_Obj *const *optionObjv; /**<
     * Borrowed configure arguments, valid only during the synchronous configuration transaction.
     */
    int optionsConfigured; /**< Nonzero after the first successful configuration. */
    Tcl_Obj *activePenObjPtr; /**< Tk option-managed Tcl representation for active pen; released with option state. */
    Tcl_Obj *bindTagsObjPtr; /**< Tk option-managed Tcl representation for bind tags; released with option state. */
    Tcl_Obj *dataObjPtr; /**< Tk option-managed Tcl representation for data; released with option state. */
    Tcl_Obj *mapXObjPtr; /**< Tk option-managed Tcl representation for map x; released with option state. */
    Tcl_Obj *mapYObjPtr; /**< Tk option-managed Tcl representation for map y; released with option state. */
    Tcl_Obj *normalPenObjPtr; /**< Tk option-managed Tcl representation for normal pen; released with option state. */
    Tcl_Obj *stateObjPtr; /**< Tk option-managed Tcl representation for state; released with option state. */
    Tcl_Obj *stylesObjPtr; /**< Tk option-managed Tcl representation for styles; released with option state. */
    Tcl_Obj *weightsObjPtr; /**< Tk option-managed Tcl representation for weights; released with option state. */
    Tcl_Obj *xObjPtr; /**< Tk option-managed Tcl representation for x; released with option state. */
    Tcl_Obj *yObjPtr; /**< Tk option-managed Tcl representation for y; released with option state. */
    Tcl_Obj *xErrorObjPtr; /**< Tk option-managed Tcl representation for x error; released with option state. */
    Tcl_Obj *xHighObjPtr; /**< Tk option-managed Tcl representation for x high; released with option state. */
    Tcl_Obj *xLowObjPtr; /**< Tk option-managed Tcl representation for x low; released with option state. */
    Tcl_Obj *yErrorObjPtr; /**< Tk option-managed Tcl representation for y error; released with option state. */
    Tcl_Obj *yHighObjPtr; /**< Tk option-managed Tcl representation for y high; released with option state. */
    Tcl_Obj *yLowObjPtr; /**< Tk option-managed Tcl representation for y low; released with option state. */
    int optionsInitialized; /**< Nonzero once Tk option initialization has completed. */
    int tkResourcesReleased; /**< Nonzero after the guarded Tk-resource release pass. */
    Segment2D *xErrorBars; /**< Owned mapped X-error segment array, including cap segments. */
    Segment2D *yErrorBars; /**< Owned mapped Y-error segment array, including cap segments. */
    Tcl_Size xErrorBarCnt; /**< Number of initialized X-error Segment2D records. */
    Tcl_Size yErrorBarCnt; /**< Number of initialized Y-error Segment2D records. */
    Tcl_Size *xErrorToData; /**< Owned mapping from each X-error segment to its original data-point index. */
    Tcl_Size *yErrorToData; /**< Owned mapping from each Y-error segment to its original data-point index. */
    int errorBarCapWidth; /**< Length of cap on error bars */
    Pen *activePenPtr; /**< Retained named active pen, or NULL when no active pen is selected. */
    Pen *normalPenPtr; /**< Selected normal pen; a named pen is retained, an embedded default pen is borrowed. */
    Rbc_Chain *palette; /**<
     * Owned chain of per-weight pen styles, with named-pen references and borrowed geometry slices.
     */
    int scaleSymbols; /**< If non-zero, the symbols will scale in size as the graph is zoomed in/out. */
    double xRange; /**< Initial X-axis range used for symbol scaling. */
    double yRange; /**< Initial Y-axis range used for symbol scaling. */
    int state; /**< Element display state selector, maintained with derived visibility flags. */
};

#define NumberOfPoints(e)                                                                                              \
    (((e)->classUid == rbcPolarElementUid)                                                                             \
         ? ((((e)->procsPtr != NULL) && ((e)->procsPtr->pointCountProc != NULL)) ? (e)->procsPtr->pointCountProc((e))  \
                                                                                 : NumberOfXYPoints((e)))              \
         : NumberOfXYPoints((e)))

/* Shared element services. Candidate parsing and commit helpers used only by
 * rbcGrElem.c remain private to that implementation. */
Element *Rbc_BarElement(Graph *, const char *, Rbc_Uid);
Element *Rbc_LineElement(Graph *, const char *, Rbc_Uid);
int Rbc_GetBarRectangle(Element *, Tcl_Size, Extents2D *);
/**
 * @brief Refresh element arrays with pending vector updates before layout.
 * Does not deliver notifications or run Tcl callbacks. Marks affected mapping
 * and axis ranges dirty; covers coordinates, weights and error vectors.
 */
void Rbc_SyncElementVectors(Graph *graphPtr);

/**
 * @brief Release an element data slot and zero its record.
 * @param vPtr Initialized slot or zeroed candidate owned by the caller.
 *
 * Releases a named-vector client ID when present; otherwise frees the owned
 * literal array. Does not destroy the source vector or free borrowed storage.
 */
void Rbc_FreeElemVector(ElemVector *vPtr);
/**
 * @brief Stage a real named vector or a list of numeric expressions.
 * @param interp Interpreter used for lookup, conversion and errors.
 * @param elemPtr Borrowed element that will own the staged data.
 * @param objPtr Borrowed option value; vector-name lookup precedes list parsing.
 * @param candidatePtr Empty caller-owned slot overwritten by initialization.
 * @return TCL_OK with owned candidate resources, or TCL_ERROR on failure.
 *
 * Never pass a populated slot: initialization would discard its resources.
 * Complex vectors are rejected here. Named data borrows storage and owns a
 * client ID; literal data owns an allocated double array. No notification
 * callback is installed and the live element is unchanged. Release an unused
 * successful candidate with Rbc_FreeElemVector.
 */
int Rbc_ParseElemVectorObj(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr, ElemVector *candidatePtr);
double Rbc_FindElemVectorMinimum(ElemVector *vecPtr, double minLimit);
void Rbc_SyncElemDataOptionObjects(Element *elemPtr);
int Rbc_PrepareElemPenTransaction(Graph *graphPtr, Element *elemPtr, Rbc_Uid penType,
                                  ElemPenTransaction *transactionPtr);
void Rbc_CommitElemPenTransaction(Graph *graphPtr, Element *elemPtr, Pen *builtinPenPtr,
                                  ElemPenTransaction *transactionPtr);
void Rbc_FreeElemPenTransaction(Graph *graphPtr, ElemPenTransaction *transactionPtr);
int Rbc_PrepareElemAxisTransaction(Graph *graphPtr, Element *elemPtr, ElemAxisTransaction *transactionPtr);
void Rbc_CommitElemAxisTransaction(Graph *graphPtr, Element *elemPtr, ElemAxisTransaction *transactionPtr);
void Rbc_FreeElemAxisTransaction(Graph *graphPtr, ElemAxisTransaction *transactionPtr);
int Rbc_PrepareElemStateTransaction(Graph *graphPtr, Element *elemPtr, ElemStateTransaction *transactionPtr);
void Rbc_CommitElemStateTransaction(Element *elemPtr, ElemStateTransaction *transactionPtr);
int Rbc_PrepareElemTagsTransaction(Graph *graphPtr, Element *elemPtr, ElemTagsTransaction *transactionPtr);
void Rbc_CommitElemTagsTransaction(Element *elemPtr, ElemTagsTransaction *transactionPtr);
void Rbc_FreeElemTagsTransaction(ElemTagsTransaction *transactionPtr);
int Rbc_PrepareElemStylesTransaction(Graph *graphPtr, Element *elemPtr, Rbc_Uid penType, size_t styleSize,
                                     ElemStylesTransaction *transactionPtr);
void Rbc_CommitElemStylesTransaction(Graph *graphPtr, Element *elemPtr, ElemStylesTransaction *transactionPtr);
void Rbc_FreeElemStylesTransaction(Graph *graphPtr, ElemStylesTransaction *transactionPtr);
/**
 * @brief Stage common element data options without replacing live data.
 * @param graphPtr Borrowed owning graph and interpreter context.
 * @param elemPtr Element with current configuration arguments and option state.
 * @param transactionPtr Empty caller-owned transaction initialized by this call.
 * @return TCL_OK with staged resources, or TCL_ERROR after freeing candidates.
 *
 * Processes defaults during initial configuration and explicit options in
 * their original argument order. Repeated options are each validated. Commit
 * only after all other fallible component validation succeeds; otherwise free
 * the transaction. The borrowed option argument vector must remain valid.
 */
int Rbc_PrepareElemDataTransaction(Graph *graphPtr, Element *elemPtr, ElemDataTransaction *transactionPtr);
/**
 * @brief Count the ordinary X/Y pairs after a proposed data transaction.
 * @param elemPtr Borrowed element supplying unchanged live dimensions.
 * @param transactionPtr Staged transaction, or NULL to use only live data.
 * @return Minimum of the effective X and Y lengths.
 *
 * Does not commit data or invoke a type-specific polar point-count callback.
 * Named sources use current vector lengths, including pending resizes; literal
 * slots use stored counts. Does not refresh arrays or deliver notifications.
 */
Tcl_Size Rbc_ElemDataTransactionPointCount(Element *elemPtr, const ElemDataTransaction *transactionPtr);
/**
 * @brief Transfer staged common data into the element's stable member slots.
 * @param elemPtr Live destination element.
 * @param transactionPtr Successfully prepared transaction to consume.
 *
 * Replaced slots are released. Candidate ownership moves to the destination,
 * named-vector callbacks are installed there, and stagedMask is cleared.
 * No fallible validation or redraw scheduling is performed here.
 */
void Rbc_CommitElemDataTransaction(Element *elemPtr, ElemDataTransaction *transactionPtr);
/**
 * @brief Release all remaining staged arrays and vector client IDs.
 * @param transactionPtr Initialized or zeroed caller-owned transaction.
 *
 * Zeros its slots and staged mask. Safe after commit, which has transferred
 * ownership out of the candidates. Does not modify the live element.
 */
void Rbc_FreeElemDataTransaction(ElemDataTransaction *transactionPtr);
/**
 * @brief Release named style entries while retaining the normal palette entry.
 * @param graphPtr Owning graph used to release pen references.
 * @param palette Valid palette chain; its first entry borrows the normal pen.
 *
 * Releases pen references and removes links after the first entry. Does not
 * release the first entry's pen or destroy the chain itself. The caller owns
 * the remaining chain and must destroy it when appropriate.
 */
void Rbc_FreePalette(Graph *graphPtr, Rbc_Chain *palette);
/**
 * @brief Allocate a per-data-point map of borrowed palette style pointers.
 * @param elemPtr Element with a valid nonempty palette and current data.
 * @return Allocated pointer array owned by the caller; release with ckfree,
 * including for zero points. Entries borrow styles from the element palette.
 *
 * Unmatched or unweighted points retain the first (normal) style. Weighted
 * points search entries from last to first using normalized range tests with
 * DBL_EPSILON tolerance. The map acquires no pen references and must not outlive
 * its palette entries. Allocation-size overflow invokes Tcl_Panic.
 */
PenStyle **Rbc_StyleMap(Element *elemPtr);
void Rbc_MapErrorBars(Graph *graphPtr, Element *elemPtr, PenStyle **dataToStyle);
void Rbc_ExpandErrorBarExtents(Element *elemPtr, Extents2D *extsPtr);
void Rbc_ExpandErrorBarExtentsOffset(Element *elemPtr, Extents2D *extsPtr, double xOffset);
void Rbc_ExpandErrorBarExtentsTranslated(Element *elemPtr, Extents2D *extsPtr, double xOffset,
                                       const Point2D *offsets);
void Rbc_MapErrorBarsTranslated(Graph *graphPtr, Element *elemPtr, PenStyle **dataToStyle, double xOffset,
                               const Point2D *offsets);
void Rbc_MapErrorBarsOffset(Graph *graphPtr, Element *elemPtr, PenStyle **dataToStyle, double xOffset);
int Rbc_GetValueOffset(Tcl_Interp *interp, Tcl_Obj *objPtr, Point2D *offsetPtr);
int Rbc_ValidateValueCommand(Tcl_Interp *interp, Tcl_Obj *commandObjPtr);
Tcl_Obj *Rbc_GetElementValueLabel(Element *elemPtr, Tcl_Obj *commandObjPtr, const char *format,
                                 int show, Tcl_Size index, double x, double y);
int Rbc_ValidateValueFormat(Tcl_Interp *interp, const char *format);

#endif /* _RBCGRELEM */
