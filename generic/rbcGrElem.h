/*
 * rbcGrElem.h --
 *
 *      TODO: Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCGRELEM
#define _RBCGRELEM

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

/*
 * -------------------------------------------------------------------
 *
 * Weight --
 *
 *    Designates a range of values by a minimum and maximum limit.
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    double min, max, range;
} Weight;

#define SetRange(l) ((l).range = ((l).max > (l).min) ? ((l).max - (l).min) : DBL_EPSILON)
#define SetScale(l) ((l).scale = 1.0 / (l).range)
#define SetWeight(l, lo, hi) ((l).min = (lo), (l).max = (hi), SetRange(l))

/*
 * An element has one or more vectors plus several attributes, such as
 * line style, thickness, color, and symbol type.  It has an
 * identifier which distinguishes it among the list of all elements.
 */
typedef struct {
    Weight weight; /* Weight range where this pen is valid. */

    Pen *penPtr; /* Pen to use. */

    Segment2D *xErrorBars; /* Point to start of this pen's X-error bar
                            * segments in the element's array. */

    Segment2D *yErrorBars; /* Point to start of this pen's Y-error bar
                            * segments in the element's array. */

    Tcl_Size xErrorBarCnt; /* # of error bars for this pen. */

    Tcl_Size yErrorBarCnt; /* # of error bars for this pen. */

    int errorBarCapWidth; /* Length of the cap ends on each
                           * error bar. */

    int symbolSize; /* Size of the pen's symbol scaled to
                     * the current graph size. */
} PenStyle;

typedef struct {
    XColor *color; /* Color of error bar */
    int lineWidth; /* Width of the error bar segments. */
    GC gc;
    int show; /* Flags for errorbars: none, x, y, or both */

} ErrorBarAttributes;

typedef struct {
    int halo; /* Maximal distance a candidate point
               * can be from the sample window
               * coordinate */

    int mode; /* Indicates whether to find the closest
               * data point or the closest point on the
               * trace by interpolating the line segments.
               * Can also be SEARCH_AUTO, indicating to
               * choose how to search.*/

    int x, y; /* Screen coordinates of test point */

    int along; /* Indicates to let search run along a
                * particular axis: x, y, or both. */

    /* Output */
    Element *elemPtr; /* Name of the closest element */

    Point2D point; /* Graph coordinates of closest point */

    Tcl_Size index; /* Index of closest data point */

    double dist; /* Distance in screen coordinates */

} ClosestSearch;

typedef void(ElementDrawProc)(Graph *graphPtr, Drawable drawable, Element *elemPtr);
typedef void(ElementToPostScriptProc)(Graph *graphPtr, PsToken psToken, Element *elemPtr);
typedef void(ElementDestroyProc)(Graph *graphPtr, Element *elemPtr);
typedef int(ElementConfigProc)(Graph *graphPtr, Element *elemPtr);
typedef void(ElementMapProc)(Graph *graphPtr, Element *elemPtr);
typedef void(ElementExtentsProc)(Element *elemPtr, Extents2D *extsPtr);
typedef void(ElementClosestProc)(Graph *graphPtr, Element *elemPtr, ClosestSearch *searchPtr);
typedef void(ElementDrawSymbolProc)(Graph *graphPtr, Drawable drawable, Element *elemPtr, int x, int y, int symbolSize);
typedef void(ElementSymbolToPostScriptProc)(Graph *graphPtr, PsToken psToken, Element *elemPtr, double x, double y,
                                            int symSize);
typedef Tcl_Size(ElementPointCountProc)(Element *elemPtr);
typedef int(ElementClosestInfoProc)(Graph *graphPtr, Element *elemPtr, const ClosestSearch *searchPtr,
                                    Tcl_Obj *varNameObjPtr);

typedef struct {
    ElementClosestProc *closestProc;
    ElementConfigProc *configProc;
    ElementDestroyProc *destroyProc;
    ElementDrawProc *drawActiveProc;
    ElementDrawProc *drawNormalProc;
    ElementDrawSymbolProc *drawSymbolProc;
    ElementExtentsProc *extentsProc;
    ElementToPostScriptProc *printActiveProc;
    ElementToPostScriptProc *printNormalProc;
    ElementSymbolToPostScriptProc *printSymbolProc;
    ElementMapProc *mapProc;
    ElementPointCountProc *pointCountProc;
    ElementClosestInfoProc *closestInfoProc;    
} ElementProcs;

/*
 * The data structure below contains information pertaining to a line
 * vector.  It consists of an array of floating point data values and
 * for convenience, the number and minimum/maximum values.
 */

typedef struct {
    Rbc_Vector *vecPtr;

    double *valueArr;

    Tcl_Size nValues;

    Tcl_Size arraySize;

    double min, max;

    Rbc_VectorId clientId; /* If non-NULL, a client token identifying the
                            * external vector. */

    Element *elemPtr; /* Element associated with vector. */

    /*
     * Change information received from an external named vector.
     *
     * changePending is set when at least one notification has arrived
     * since the element was last mapped.
     *
     * changeAll means that no exact source range is available.
     */
    int changePending;
    int changeAll;
    Tcl_Size changedFirst;
    Tcl_Size changedLast;    

} ElemVector;

typedef struct {
    unsigned int stagedMask;

    ElemVector x;
    ElemVector y;
    ElemVector w;

    ElemVector xError;
    ElemVector xHigh;
    ElemVector xLow;

    ElemVector yError;
    ElemVector yHigh;
    ElemVector yLow;
} ElemDataTransaction;

typedef struct {
    unsigned int stagedMask;

    Pen *activePenPtr;
    Pen *normalPenPtr;
} ElemPenTransaction;

typedef struct {
    unsigned int stagedMask;

    Axis *xAxisPtr;
    Axis *yAxisPtr;
} ElemAxisTransaction;

typedef struct {
    int staged;
    int state;
} ElemStateTransaction;

typedef struct {
    int staged;
    char **tags;
} ElemTagsTransaction;

typedef struct {
    int staged;
    Rbc_Chain *palette;
} ElemStylesTransaction;

struct ElementStruct {
    char *name; /* Identifier to refer the element.
                 * Used in the "insert", "delete", or
                 * "show", commands. */

    Rbc_Uid classUid; /* Type of element */

    Graph *graphPtr; /* Graph widget of element*/

    unsigned int flags; /* Indicates if the entire element is
                         * active, or if coordinates need to
                         * be calculated */

    char **tags;

    int hidden; /* If non-zero, don't display the element. */

    int plotHidden; /* If non-zero, don't plot the element, but keep it
                     * in the display list and legend. */
    Tcl_HashEntry *hashPtr;

    char *label; /* Label displayed in legend */

    int labelRelief; /* Relief of label in legend. */

    Axis2D axes; /* X-axis and Y-axis mapping the element */

    ElemVector x, y, w; /* Contains array of floating point
                         * graph coordinate values. Also holds
                         * min/max and the number of
                         * coordinates */

    ElemVector xError;      /* Relative/symmetric X error values. */
    ElemVector yError;      /* Relative/symmetric Y error values. */
    ElemVector xHigh, xLow; /* Absolute/asymmetric X-coordinate high/low
                error values. */
    ElemVector yHigh, yLow; /* Absolute/asymmetric Y-coordinate high/low
                error values. */

    Tcl_Size *activeIndices; /* Array of indices (malloc-ed) which
                         * indicate which data points are
                         * active (drawn with "active"
                         * colors). */

    Tcl_Size nActiveIndices; /* Number of active data points.
                         * Special case: if nActiveIndices < 0
                         * and the active bit is set in
                         * "flags", then all data points are
                         * drawn active. */

    ElementProcs *procsPtr;

    /*
     * Modern option-table configuration.
     */
    const Tk_OptionSpec *optionSpecs;
    Tk_OptionTable optionTable;

    /*
     * Information about the current Tk_SetOptions transaction.
     *
     * These fields are valid only while the concrete configProc is
     * executing. optionObjv points into the caller's argument vector
     * and must never be retained after configProc returns.
     */
    int optionMask;
    Tcl_Size optionObjc;
    Tcl_Obj *const *optionObjv;

    /*
     * Indicates that the element has completed its first successful
     * modern configuration. Concrete configuration procedures can use
     * this to distinguish Tk_InitOptions defaults from later configure
     * operations.
     */
    int optionsConfigured;

    /*
     * Tcl representations for common element options that require
     * additional validation or conversion after Tk_SetOptions.
     */
    Tcl_Obj *activePenObjPtr;
    Tcl_Obj *bindTagsObjPtr;
    Tcl_Obj *dataObjPtr;
    Tcl_Obj *mapXObjPtr;
    Tcl_Obj *mapYObjPtr;
    Tcl_Obj *normalPenObjPtr;
    Tcl_Obj *stateObjPtr;
    Tcl_Obj *stylesObjPtr;

    Tcl_Obj *weightsObjPtr;
    Tcl_Obj *xObjPtr;
    Tcl_Obj *yObjPtr;
    Tcl_Obj *xErrorObjPtr;
    Tcl_Obj *xHighObjPtr;
    Tcl_Obj *xLowObjPtr;
    Tcl_Obj *yErrorObjPtr;
    Tcl_Obj *yHighObjPtr;
    Tcl_Obj *yLowObjPtr;

    int optionsInitialized;
    int tkResourcesReleased;

    Segment2D *xErrorBars; /* Point to start of this pen's X-error bar
                            * segments in the element's array. */
    Segment2D *yErrorBars; /* Point to start of this pen's Y-error bar
                            * segments in the element's array. */
    
    Tcl_Size xErrorBarCnt;      /* # of error bars for this pen. */
    Tcl_Size yErrorBarCnt;      /* # of error bars for this pen. */

    Tcl_Size *xErrorToData; /* Maps error bar segments back to the data
                        * point. */
    Tcl_Size *yErrorToData; /* Maps error bar segments back to the data
                        * point. */

    int errorBarCapWidth; /* Length of cap on error bars */

    Pen *activePenPtr; /* Standard Pens */
    Pen *normalPenPtr;

    Rbc_Chain *palette; /* Palette of pens. */

    /* Symbol scaling */
    int scaleSymbols; /* If non-zero, the symbols will scale
                       * in size as the graph is zoomed
                       * in/out.  */

    double xRange, yRange; /* Initial X-axis and Y-axis ranges:
                            * used to scale the size of element's
                            * symbol. */
    int state;
};

#define NumberOfPoints(e)                                                                                              \
    (((e)->classUid == rbcPolarElementUid)                                                                             \
         ? ((((e)->procsPtr != NULL) && ((e)->procsPtr->pointCountProc != NULL)) ? (e)->procsPtr->pointCountProc((e))  \
                                                                                 : NumberOfXYPoints((e)))              \
         : NumberOfXYPoints((e)))

Element *Rbc_BarElement(Graph *, const char *, Rbc_Uid);
Element *Rbc_LineElement(Graph *, const char *, Rbc_Uid);

int Rbc_GetBarRectangle(Element *, Tcl_Size, Extents2D *);
void Rbc_FreeElemVector(ElemVector *vPtr);
int Rbc_ParseElemVectorObj(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr, ElemVector *candidatePtr);
int Rbc_ParseElemVectorPairsObj(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr, ElemVector *xCandidatePtr,
                                ElemVector *yCandidatePtr);
void Rbc_CommitElemVector(Element *elemPtr, ElemVector *destPtr, ElemVector *candidatePtr);
double Rbc_FindElemVectorMinimum(ElemVector *vecPtr, double minLimit);
int Rbc_PrepareElemDataTransaction(Graph *graphPtr, Element *elemPtr, ElemDataTransaction *transactionPtr);
void Rbc_CommitElemDataTransaction(Element *elemPtr, ElemDataTransaction *transactionPtr);
void Rbc_FreeElemDataTransaction(ElemDataTransaction *transactionPtr);
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
int Rbc_PrepareElemDataTransaction(Graph *graphPtr, Element *elemPtr, ElemDataTransaction *transactionPtr);
Tcl_Size Rbc_ElemDataTransactionPointCount(Element *elemPtr, const ElemDataTransaction *transactionPtr);
void Rbc_CommitElemDataTransaction(Element *elemPtr, ElemDataTransaction *transactionPtr);
void Rbc_FreeElemDataTransaction(ElemDataTransaction *transactionPtr);
void Rbc_FreePalette(Graph *graphPtr, Rbc_Chain *palette);
void Rbc_DestroyPalette(Graph *graphPtr, Rbc_Chain *palette);
int Rbc_ParseStylesObj(Graph *graphPtr, Element *elemPtr, Tcl_Obj *objPtr, size_t styleSize, Rbc_Chain **palettePtrPtr);
PenStyle **Rbc_StyleMap(Element *elemPtr);
void Rbc_MapErrorBars(Graph *graphPtr, Element *elemPtr, PenStyle **dataToStyle);
void Rbc_ExpandErrorBarExtents(Element *elemPtr, Extents2D *extsPtr);
int Rbc_ValidateValueFormat(Tcl_Interp *interp, const char *format);
void Rbc_FormatValueLabel(char *buffer, size_t bufferSize, const char *format, int show, double x, double y);

#endif /* _RBCGRELEM */
