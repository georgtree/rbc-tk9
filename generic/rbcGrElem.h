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

#define NumberOfPoints(e) MIN((e)->x.nValues, (e)->y.nValues)

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

    int xErrorBarCnt; /* # of error bars for this pen. */

    int yErrorBarCnt; /* # of error bars for this pen. */

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

    int index; /* Index of closest data point */

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
} ElementProcs;

/*
 * The data structure below contains information pertaining to a line
 * vector.  It consists of an array of floating point data values and
 * for convenience, the number and minimum/maximum values.
 */

typedef struct {
    Rbc_Vector *vecPtr;

    double *valueArr;

    int nValues;

    int arraySize;

    double min, max;

    Rbc_VectorId clientId; /* If non-NULL, a client token identifying the
                            * external vector. */

    Element *elemPtr; /* Element associated with vector. */

} ElemVector;

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

    int *activeIndices; /* Array of indices (malloc-ed) which
                         * indicate which data points are
                         * active (drawn with "active"
                         * colors). */

    int nActiveIndices; /* Number of active data points.
                         * Special case: if nActiveIndices < 0
                         * and the active bit is set in
                         * "flags", then all data points are
                         * drawn active. */

    ElementProcs *procsPtr;

    Tk_ConfigSpec *specsPtr; /* Configuration specifications. */

    Segment2D *xErrorBars; /* Point to start of this pen's X-error bar
                            * segments in the element's array. */
    Segment2D *yErrorBars; /* Point to start of this pen's Y-error bar
                            * segments in the element's array. */
    int xErrorBarCnt;      /* # of error bars for this pen. */
    int yErrorBarCnt;      /* # of error bars for this pen. */

    int *xErrorToData; /* Maps error bar segments back to the data
                        * point. */
    int *yErrorToData; /* Maps error bar segments back to the data
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

Element *Rbc_BarElement();
Element *Rbc_LineElement();

extern Tk_OptionParseProc Rbc_StringToStyles;
extern Tk_OptionPrintProc Rbc_StylesToString;

double Rbc_FindElemVectorMinimum(ElemVector *vecPtr, double minLimit);
void Rbc_FreePalette(Graph *graphPtr, Rbc_Chain *palette);
PenStyle **Rbc_StyleMap(Element *elemPtr);
void Rbc_MapErrorBars(Graph *graphPtr, Element *elemPtr, PenStyle **dataToStyle);

#endif /* _RBCGRELEM */
