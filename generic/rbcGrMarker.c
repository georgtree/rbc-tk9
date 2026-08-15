/*
 * rbcGrMarker.c --
 *
 *      This module implements markers for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"
#include "rbcChain.h"
#include "rbcGrAxis.h"
#include "rbcGrElem.h"

#define MAX_OUTLINE_POINTS 12

/* Map graph coordinates to normalized coordinates [0..1] */
#define NORMALIZE(A, x) (((x) - (A)->axisRange.min) / (A)->axisRange.range)

#define DEF_MARKER_ANCHOR "center"
#define DEF_MARKER_BACKGROUND RGB_WHITE
#define DEF_MARKER_BG_MONO RGB_WHITE
#define DEF_MARKER_BITMAP (char *)NULL
#define DEF_MARKER_MASK (char *)NULL
#define DEF_MARKER_CAP_STYLE "butt"
#define DEF_MARKER_COORDS (char *)NULL
#define DEF_MARKER_DASHES (char *)NULL
#define DEF_MARKER_DASH_OFFSET "0"
#define DEF_MARKER_ELEMENT (char *)NULL
#define DEF_MARKER_FOREGROUND RGB_BLACK
#define DEF_MARKER_FG_MONO RGB_BLACK
#define DEF_MARKER_FILL_COLOR RGB_RED
#define DEF_MARKER_FILL_MONO RGB_WHITE
#define DEF_MARKER_FONT STD_FONT
#define DEF_MARKER_HEIGHT "0"
#define DEF_MARKER_HIDE "no"
#define DEF_MARKER_JOIN_STYLE "miter"
#define DEF_MARKER_JUSTIFY "left"
#define DEF_MARKER_LINE_WIDTH "1"
#define DEF_MARKER_MAP_X "x"
#define DEF_MARKER_MAP_Y "y"
#define DEF_MARKER_NAME (char *)NULL
#define DEF_MARKER_OUTLINE_COLOR RGB_BLACK
#define DEF_MARKER_OUTLINE_MONO RGB_BLACK
#define DEF_MARKER_PAD "4"
#define DEF_MARKER_ROTATE "0.0"
#define DEF_MARKER_SHADOW_COLOR (char *)NULL
#define DEF_MARKER_STATE "normal"
#define DEF_MARKER_STIPPLE (char *)NULL
#define DEF_MARKER_TEXT (char *)NULL
#define DEF_MARKER_UNDER "no"
#define DEF_MARKER_WIDTH "0"
#define DEF_MARKER_WINDOW (char *)NULL
#define DEF_MARKER_XOR "no"
#define DEF_MARKER_X_OFFSET "0"
#define DEF_MARKER_Y_OFFSET "0"

#define DEF_MARKER_TEXT_TAGS "Text all"
#define DEF_MARKER_IMAGE_TAGS "Image all"
#define DEF_MARKER_BITMAP_TAGS "Bitmap all"
#define DEF_MARKER_WINDOW_TAGS "Window all"
#define DEF_MARKER_POLYGON_TAGS "Polygon all"
#define DEF_MARKER_LINE_TAGS "Line all"

#define WINDOW_HEIGHT_CHANGED (1 << 0)
#define WINDOW_WIDTH_CHANGED (1 << 1)

typedef Marker *(MarkerCreateProc)(void);
typedef void(MarkerDrawProc)(Marker *markerPtr, Drawable drawable);
typedef void(MarkerFreeProc)(Graph *graphPtr, Marker *markerPtr);
typedef int(MarkerConfigProc)(Marker *markerPtr);
typedef void(MarkerMapProc)(Marker *markerPtr);
typedef void(MarkerPostScriptProc)(Marker *markerPtr, PsToken psToken);
typedef int(MarkerPointProc)(Marker *markerPtr, Point2D *samplePtr);
typedef int(MarkerRegionProc)(Marker *markerPtr, Extents2D *extsPtr, int enclosed);

typedef struct {
    const Tk_OptionSpec *optionSpecs;
    const Tk_OptionSpec *monoOptionSpecs;

    MarkerConfigProc *configProc;
    MarkerDrawProc *drawProc;
    MarkerFreeProc *freeProc;
    MarkerMapProc *mapProc;
    MarkerPointProc *pointProc;
    MarkerRegionProc *regionProc;
    MarkerPostScriptProc *postscriptProc;
} MarkerClass;

/*
 * -------------------------------------------------------------------
 *
 * Marker --
 *
 *      Structure defining the generic marker.  In C++ parlance this
 *      would be the base type from which all markers are derived.
 *
 * -------------------------------------------------------------------
 */
struct MarkerStruct {
    char *name;       /* Identifier for marker in list */
    Rbc_Uid classUid; /* Type of marker. */
    Graph *graphPtr;  /* Graph widget of marker. */
    unsigned int flags;
    char **tags;
    int hidden; /* If non-zero, don't display the marker. */
    Tcl_HashEntry *hashPtr;
    Rbc_ChainLink *linkPtr;
    Point2D *worldPts; /* Coordinate array to position marker */
    Tcl_Size nWorldPts;     /* Number of points in above array */
    char *elemName;    /* Element associated with marker */
    Axis2D axes;
    int drawUnder;        /* If non-zero, draw the marker
                           * underneath any elements. This can
                           * be a performance penalty because
                           * the graph must be redraw entirely
                           * each time the marker is redrawn. */
    int clipped;          /* Indicates if the marker is totally
                           * clipped by the plotting area. */
    int xOffset, yOffset; /* Pixel offset from graph position */
    MarkerClass *classPtr;
    int state;

    /*
     * Original Tcl representations for common manually converted options.
     */
    Tcl_Obj *bindTagsObjPtr;
    Tcl_Obj *coordsObjPtr;
    Tcl_Obj *mapXObjPtr;
    Tcl_Obj *mapYObjPtr;
    Tcl_Obj *stateObjPtr;
    Tcl_Obj *xOffsetObjPtr;
    Tcl_Obj *yOffsetObjPtr;

    /*
     * Option typeMask values modified by the current Tk_SetOptions()
     * transaction. Valid only while configProc is running.
     */
    int optionMask;

    /*
     * Tk option state and two-stage resource-release state.
     */
    Tk_OptionTable optionTable;
    int optionsInitialized;
    int tkResourcesReleased;    
};

/*
 * -------------------------------------------------------------------
 *
 * TextMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    Marker core;

    /*
     * Original Tcl representations of manually converted options.
     */
    Tcl_Obj *padXObjPtr;
    Tcl_Obj *padYObjPtr;
    Tcl_Obj *shadowObjPtr;
    
    char *string;        /* Text string to be display.  The string
                          * make contain newlines. */
    Tk_Anchor anchor;    /* Indicates how to translate the given
                          * marker position. */
    Point2D anchorPos;   /* Translated anchor point. */
    int width, height;   /* Dimension of bounding box.  */
    TextStyle style;     /* Text attributes (font, fg, anchor, etc) */
    TextLayout *textPtr; /* Contains information about the layout
                          * of the text. */
    Point2D outline[5];
    XColor *fillColor;
    GC fillGC;
} TextMarker;

static const Tk_OptionSpec textMarkerOptionSpecs[] = {
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR, -1, offsetof(TextMarker, anchor), 0, NULL, 0},
    {TK_OPTION_COLOR, "-background", "background", "MarkerBackground", NULL, -1, offsetof(TextMarker, fillColor),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_SYNONYM, "-bg", "background", NULL, NULL, 0, -1, 0, (ClientData) "-background", 0},
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_MARKER_TEXT_TAGS, offsetof(Marker, bindTagsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, coordsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, -1, offsetof(Marker, elemName),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_SYNONYM, "-fg", "foreground", NULL, NULL, 0, -1, 0, (ClientData) "-foreground", 0},
    {TK_OPTION_SYNONYM, "-fill", "background", NULL, NULL, 0, -1, 0, (ClientData) "-background", 0},
    {TK_OPTION_FONT, "-font", "font", "Font", DEF_MARKER_FONT, -1, offsetof(TextMarker, style.font), 0, NULL, 0},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground", DEF_MARKER_FOREGROUND, -1,
     offsetof(TextMarker, style.color), 0, (ClientData)DEF_MARKER_FG_MONO, 0},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, -1, offsetof(Marker, hidden), 0, NULL, 0},
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify", DEF_MARKER_JUSTIFY, -1, offsetof(TextMarker, style.justify),
     0, NULL, 0},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, mapXObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, mapYObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME, -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_SYNONYM, "-outline", "foreground", NULL, NULL, 0, -1, 0, (ClientData) "-foreground", 0},
    {TK_OPTION_STRING, "-padx", "padX", "PadX", DEF_MARKER_PAD, offsetof(TextMarker, padXObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-pady", "padY", "PadY", DEF_MARKER_PAD, offsetof(TextMarker, padYObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_DOUBLE, "-rotate", "rotate", "Rotate", DEF_MARKER_ROTATE, -1, offsetof(TextMarker, style.theta), 0, NULL,
     0},
    {TK_OPTION_STRING, "-shadow", "shadow", "Shadow", DEF_MARKER_SHADOW_COLOR, offsetof(TextMarker, shadowObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-text", "text", "Text", DEF_MARKER_TEXT, -1, offsetof(TextMarker, string), TK_OPTION_NULL_OK,
     NULL, 0},
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffsetObjPtr),
     offsetof(Marker, xOffset), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), 0, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/*
 * -------------------------------------------------------------------
 *
 * WindowMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    Marker core;


    /*
     * Original Tcl representations of the requested dimensions.
     */
    Tcl_Obj *reqHeightObjPtr;
    Tcl_Obj *reqWidthObjPtr;
    
    /*
     * Window specific attributes
     */
    char *pathName;          /* Name of child widget to be displayed. */
    Tk_Window tkwin;         /* Window to display. */
    int reqWidth, reqHeight; /* If non-zero, this overrides the size
                              * requested by the child widget. */
    Tk_Anchor anchor;        /* Indicates how to translate the given
                              * marker position. */
    Point2D anchorPos;       /* Translated anchor point. */
    int width, height;       /* Current size of the child window. */
} WindowMarker;

static const Tk_OptionSpec windowMarkerOptionSpecs[] = {
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR, -1, offsetof(WindowMarker, anchor), 0, NULL,
     0},
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_MARKER_WINDOW_TAGS, offsetof(Marker, bindTagsObjPtr),
     -1, TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, coordsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, -1, offsetof(Marker, elemName),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-height", "height", "Height", DEF_MARKER_HEIGHT, offsetof(WindowMarker, reqHeightObjPtr), -1, 0,
     NULL, WINDOW_HEIGHT_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, -1, offsetof(Marker, hidden), 0, NULL, 0},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, mapXObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, mapYObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME, -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},
    {TK_OPTION_STRING, "-width", "width", "Width", DEF_MARKER_WIDTH, offsetof(WindowMarker, reqWidthObjPtr), -1, 0,
     NULL, WINDOW_WIDTH_CHANGED},
    {TK_OPTION_STRING, "-window", "window", "Window", DEF_MARKER_WINDOW, -1, offsetof(WindowMarker, pathName),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffsetObjPtr),
     offsetof(Marker, xOffset), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), 0, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/*
 * -------------------------------------------------------------------
 *
 * BitmapMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    Marker core;

    /* Bitmap specific attributes */
    Pixmap srcBitmap;                    /* Original bitmap. May be further
                                          * scaled or rotated. */
    Pixmap srcMask;                      /* User-specified transparency mask */
    double rotate;                       /* Requested rotation of the bitmap */
    double theta;                        /* Normalized rotation (0..360
                                          * degrees) */
    Tk_Anchor anchor;                    /* If only one X-Y coordinate is
                                          * given, indicates how to translate
                                          * the given marker position.  Otherwise,
                                          * if there are two X-Y coordinates, then
                                          * this value is ignored. */
    Point2D anchorPos;                   /* Translated anchor point. */
    XColor *outlineColor;                /* Foreground color */
    XColor *fillColor;                   /* Background color */
    GC gc;                               /* Private graphic context */
    GC fillGC;                           /* Shared graphic context */
    Pixmap destBitmap;                   /* Bitmap to be drawn. */
    Pixmap destMask;                     /* Final scaled/rotated clip mask */
    int destBitmapOwned;
    int destMaskOwned;    
    int destWidth, destHeight;           /* Dimensions of the final bitmap */
    Point2D outline[MAX_OUTLINE_POINTS]; /* Polygon representing the background
                                          * of the bitmap. */
    int nOutlinePts;
} BitmapMarker;

static const Tk_OptionSpec bitmapMarkerOptionSpecs[] = {
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR, -1, offsetof(BitmapMarker, anchor), 0, NULL,
     0},
    {TK_OPTION_COLOR, "-background", "background", "Background", DEF_MARKER_BACKGROUND, -1,
     offsetof(BitmapMarker, fillColor), TK_OPTION_NULL_OK, (ClientData)DEF_MARKER_BG_MONO, 0},
    {TK_OPTION_SYNONYM, "-bg", "background", NULL, NULL, 0, -1, 0, (ClientData) "-background", 0},
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_MARKER_BITMAP_TAGS, offsetof(Marker, bindTagsObjPtr),
     -1, TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_BITMAP, "-bitmap", "bitmap", "Bitmap", DEF_MARKER_BITMAP, -1, offsetof(BitmapMarker, srcBitmap),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, coordsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, -1, offsetof(Marker, elemName),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_SYNONYM, "-fg", "foreground", NULL, NULL, 0, -1, 0, (ClientData) "-foreground", 0},
    {TK_OPTION_SYNONYM, "-fill", "background", NULL, NULL, 0, -1, 0, (ClientData) "-background", 0},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground", DEF_MARKER_FOREGROUND, -1,
     offsetof(BitmapMarker, outlineColor), TK_OPTION_NULL_OK, (ClientData)DEF_MARKER_FG_MONO, 0},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, -1, offsetof(Marker, hidden), 0, NULL, 0},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, mapXObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, mapYObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BITMAP, "-mask", "mask", "Mask", DEF_MARKER_MASK, -1, offsetof(BitmapMarker, srcMask), TK_OPTION_NULL_OK,
     NULL, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME, -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_SYNONYM, "-outline", "foreground", NULL, NULL, 0, -1, 0, (ClientData) "-foreground", 0},
    {TK_OPTION_DOUBLE, "-rotate", "rotate", "Rotate", DEF_MARKER_ROTATE, -1, offsetof(BitmapMarker, rotate), 0, NULL,
     0},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffsetObjPtr),
     offsetof(Marker, xOffset), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), 0, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/*
 * -------------------------------------------------------------------
 *
 * ImageMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    Marker core;

    /* Image specific attributes */
    Tcl_Obj *imageObjPtr;   /* Original Tcl representation of -image. */
    Tk_Image tkImage;  /* Tk image to be displayed. */
    Tk_Anchor anchor;  /* Indicates how to translate the given
                        * marker position. */
    Point2D anchorPos; /* Translated anchor point. */
    int width, height; /* Dimensions of the image */
    Tk_Image tmpImage;
    Pixmap pixmap;         /* Pixmap containing the scaled image */
    ColorTable colorTable; /* Pointer to color table */
    Rbc_ColorImage srcImage;
    GC gc;

} ImageMarker;

typedef struct {
    char **tags;

    Point2D *worldPts;
    Tcl_Size nWorldPts;

    Axis *xAxis;
    Axis *yAxis;

    int state;
} ParsedMarkerOptions;

static const Tk_OptionSpec imageMarkerOptionSpecs[] = {
    {TK_OPTION_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR, -1, offsetof(ImageMarker, anchor), 0, NULL, 0},
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_MARKER_IMAGE_TAGS, offsetof(Marker, bindTagsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, coordsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, -1, offsetof(Marker, elemName),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, -1, offsetof(Marker, hidden), 0, NULL, 0},
    {TK_OPTION_STRING, "-image", "image", "Image", NULL, offsetof(ImageMarker, imageObjPtr), -1, TK_OPTION_NULL_OK,
     NULL, 0},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, mapXObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, mapYObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME, -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffsetObjPtr),
     offsetof(Marker, xOffset), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), 0, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/*
 * -------------------------------------------------------------------
 *
 * LineMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    Marker core;

    /*
     * Original Tcl representations for manually converted line
     * marker options.
     */
    Tcl_Obj *capObjPtr;
    Tcl_Obj *dashesObjPtr;
    Tcl_Obj *dashOffsetObjPtr;
    Tcl_Obj *joinObjPtr;
    Tcl_Obj *lineWidthObjPtr;
    
    /* Line specific attributes */
    XColor *fillColor;
    XColor *outlineColor; /* Foreground and background colors */

    int lineWidth;     /* Line width. */
    int capStyle;      /* Cap style. */
    int joinStyle;     /* Join style.*/
    Rbc_Dashes dashes; /* Dash list values (max 11) */

    GC gc; /* Private graphic context */

    Segment2D *segments; /* Malloc'ed array of points.
                          * Represents individual line segments
                          * (2 points per segment) comprising
                          * the mapped line.  The segments may
                          * not necessarily be connected after
                          * clipping. */
    Tcl_Size nSegments;       /* # segments in the above array. */

    int xor ;
    int xorState; /* State of the XOR drawing. Indicates
                   * if the marker is currently drawn. */
} LineMarker;

static const Tk_OptionSpec lineMarkerOptionSpecs[] = {
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_MARKER_LINE_TAGS, offsetof(Marker, bindTagsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-cap", "cap", "Cap", DEF_MARKER_CAP_STYLE, offsetof(LineMarker, capObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, coordsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", DEF_MARKER_DASHES, offsetof(LineMarker, dashesObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-dashoffset", "dashOffset", "DashOffset", DEF_MARKER_DASH_OFFSET,
     offsetof(LineMarker, dashOffsetObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, -1, offsetof(Marker, elemName),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_COLOR, "-fill", "fill", "Fill", NULL, -1, offsetof(LineMarker, fillColor), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-join", "join", "Join", DEF_MARKER_JOIN_STYLE, offsetof(LineMarker, joinObjPtr), -1, 0, NULL,
     0},
    {TK_OPTION_STRING, "-linewidth", "lineWidth", "LineWidth", DEF_MARKER_LINE_WIDTH,
     offsetof(LineMarker, lineWidthObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, -1, offsetof(Marker, hidden), 0, NULL, 0},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, mapXObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, mapYObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME, -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_COLOR, "-outline", "outline", "Outline", DEF_MARKER_OUTLINE_COLOR, -1,
     offsetof(LineMarker, outlineColor), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffsetObjPtr),
     offsetof(Marker, xOffset), 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-xor", "xor", "Xor", DEF_MARKER_XOR, -1, offsetof(LineMarker, xor), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), 0, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/*
 * -------------------------------------------------------------------
 *
 * PolygonMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    Marker core;

    /*
     * Original Tcl representations of manually converted options.
     */
    Tcl_Obj *capObjPtr;
    Tcl_Obj *dashesObjPtr;
    Tcl_Obj *fillObjPtr;
    Tcl_Obj *joinObjPtr;
    Tcl_Obj *lineWidthObjPtr;
    Tcl_Obj *outlineObjPtr;
    
    /* Polygon specific attributes and fields */

    Point2D *screenPts;

    ColorPair outline;
    ColorPair fill;

    Pixmap stipple; /* Stipple pattern to fill the polygon. */
    int lineWidth;  /* Width of polygon outline. */
    int capStyle;
    int joinStyle;
    Rbc_Dashes dashes; /* List of dash values.  Indicates how
                        * draw the dashed line.  If no dash
                        * values are provided, or the first value
                        * is zero, then the line is drawn solid. */

    GC outlineGC; /* Graphics context to draw the outline of
                   * the polygon. */
    GC fillGC;    /* Graphics context to draw the filled
                   * polygon. */

    Point2D *fillPts; /* Malloc'ed array of points used to draw
                       * the filled polygon. These points may
                       * form a degenerate polygon after clipping.
                       */

    Tcl_Size nFillPts; /* # points in the above array. */

    Segment2D *outlinePts; /* Malloc'ed array of points.
                            * Represents individual line segments
                            * (2 points per segment) comprising
                            * the outline of the polygon.  The
                            * segments may not necessarily be
                            * closed or connected after clipping. */

    Tcl_Size nOutlinePts; /* # points in the above array. */

    int xor ;
    int xorState; /* State of the XOR drawing. Indicates
                   * if the marker is visible. We have
                   * to drawn it again to erase it. */
} PolygonMarker;

#define POLYGON_MARKER_OPTION_ENTRIES(FILL_DEFAULT, OUTLINE_DEFAULT)                                      \
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_MARKER_POLYGON_TAGS,                      \
     offsetof(Marker, bindTagsObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},                                   \
    {TK_OPTION_STRING, "-cap", "cap", "Cap", DEF_MARKER_CAP_STYLE,                                        \
     offsetof(PolygonMarker, capObjPtr), -1, 0, NULL, 0},                                                 \
    {TK_OPTION_STRING, "-coords", "coords", "Coords", DEF_MARKER_COORDS,                                  \
     offsetof(Marker, coordsObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},                                     \
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", DEF_MARKER_DASHES,                                  \
     offsetof(PolygonMarker, dashesObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},                              \
    {TK_OPTION_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT,                              \
     -1, offsetof(Marker, elemName), TK_OPTION_NULL_OK, NULL, 0},                                         \
    {TK_OPTION_STRING, "-fill", "fill", "Fill", FILL_DEFAULT,                                             \
     offsetof(PolygonMarker, fillObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},                                \
    {TK_OPTION_STRING, "-join", "join", "Join", DEF_MARKER_JOIN_STYLE,                                    \
     offsetof(PolygonMarker, joinObjPtr), -1, 0, NULL, 0},                                                \
    {TK_OPTION_STRING, "-linewidth", "lineWidth", "LineWidth", DEF_MARKER_LINE_WIDTH,                     \
     offsetof(PolygonMarker, lineWidthObjPtr), -1, 0, NULL, 0},                                           \
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE,                                         \
     -1, offsetof(Marker, hidden), 0, NULL, 0},                                                           \
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X,                                         \
     offsetof(Marker, mapXObjPtr), -1, 0, NULL, 0},                                                       \
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y,                                         \
     offsetof(Marker, mapYObjPtr), -1, 0, NULL, 0},                                                       \
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME,                                              \
     -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},                                             \
    {TK_OPTION_STRING, "-outline", "outline", "Outline", OUTLINE_DEFAULT,                                 \
     offsetof(PolygonMarker, outlineObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},                             \
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE,                                      \
     offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},                                                      \
    {TK_OPTION_BITMAP, "-stipple", "stipple", "Stipple", DEF_MARKER_STIPPLE,                              \
     -1, offsetof(PolygonMarker, stipple), TK_OPTION_NULL_OK, NULL, 0},                                   \
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER,                                     \
     -1, offsetof(Marker, drawUnder), 0, NULL, 0},                                                        \
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET,                             \
     offsetof(Marker, xOffsetObjPtr), offsetof(Marker, xOffset), 0, NULL, 0},                             \
    {TK_OPTION_BOOLEAN, "-xor", "xor", "Xor", DEF_MARKER_XOR,                                             \
     -1, offsetof(PolygonMarker, xor), 0, NULL, 0},                                                       \
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET,                             \
     offsetof(Marker, yOffsetObjPtr), offsetof(Marker, yOffset), 0, NULL, 0},                             \
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}

static const Tk_OptionSpec polygonMarkerOptionSpecs[] = {
    POLYGON_MARKER_OPTION_ENTRIES(DEF_MARKER_FILL_COLOR, DEF_MARKER_OUTLINE_COLOR)};

static const Tk_OptionSpec polygonMarkerMonoOptionSpecs[] = {
    POLYGON_MARKER_OPTION_ENTRIES(DEF_MARKER_FILL_MONO, DEF_MARKER_OUTLINE_MONO)};

#undef POLYGON_MARKER_OPTION_ENTRIES

_Static_assert(offsetof(TextMarker, core) == 0, "Marker core must be first in TextMarker");
_Static_assert(offsetof(WindowMarker, core) == 0, "Marker core must be first in WindowMarker");
_Static_assert(offsetof(BitmapMarker, core) == 0, "Marker core must be first in BitmapMarker");
_Static_assert(offsetof(ImageMarker, core) == 0, "Marker core must be first in ImageMarker");
_Static_assert(offsetof(LineMarker, core) == 0, "Marker core must be first in LineMarker");
_Static_assert(offsetof(PolygonMarker, core) == 0, "Marker core must be first in PolygonMarker");

#define TEXT_MARKER_FROM_CORE(ptr) ((TextMarker *)((char *)(ptr) - offsetof(TextMarker, core)))
#define WINDOW_MARKER_FROM_CORE(ptr) ((WindowMarker *)((char *)(ptr) - offsetof(WindowMarker, core)))
#define BITMAP_MARKER_FROM_CORE(ptr) ((BitmapMarker *)((char *)(ptr) - offsetof(BitmapMarker, core)))
#define IMAGE_MARKER_FROM_CORE(ptr) ((ImageMarker *)((char *)(ptr) - offsetof(ImageMarker, core)))
#define LINE_MARKER_FROM_CORE(ptr) ((LineMarker *)((char *)(ptr) - offsetof(LineMarker, core)))
#define POLYGON_MARKER_FROM_CORE(ptr) ((PolygonMarker *)((char *)(ptr) - offsetof(PolygonMarker, core)))

static MarkerCreateProc CreateBitmapMarker, CreateLineMarker, CreateImageMarker, CreatePolygonMarker, CreateTextMarker,
    CreateWindowMarker;
static MarkerDrawProc DrawBitmapMarker, DrawLineMarker, DrawImageMarker, DrawPolygonMarker, DrawTextMarker,
    DrawWindowMarker;
static MarkerFreeProc FreeBitmapMarker, FreeLineMarker, FreeImageMarker, FreePolygonMarker, FreeTextMarker,
    FreeWindowMarker;
static MarkerConfigProc ConfigureBitmapMarker, ConfigureLineMarker, ConfigureImageMarker, ConfigurePolygonMarker,
    ConfigureTextMarker, ConfigureWindowMarker;
static MarkerMapProc MapBitmapMarker, MapLineMarker, MapImageMarker, MapPolygonMarker, MapTextMarker, MapWindowMarker;
static MarkerPostScriptProc BitmapMarkerToPostScript, LineMarkerToPostScript, ImageMarkerToPostScript,
    PolygonMarkerToPostScript, TextMarkerToPostScript, WindowMarkerToPostScript;
static MarkerPointProc PointInBitmapMarker, PointInLineMarker, PointInImageMarker, PointInPolygonMarker,
    PointInTextMarker, PointInWindowMarker;
static MarkerRegionProc RegionInBitmapMarker, RegionInLineMarker, RegionInImageMarker, RegionInPolygonMarker,
    RegionInTextMarker, RegionInWindowMarker;
static Tk_ImageChangedProc ImageChangedProc;

static int BoxesDontOverlap(Graph *graphPtr, Extents2D *extsPtr);
static int GetCoordinate(Tcl_Interp *interp, const char *expr, double *valuePtr);
static double HMap(Graph *graphPtr, Axis *axisPtr, double x);
static double VMap(Graph *graphPtr, Axis *axisPtr, double y);
static Point2D MapPoint(Graph *graphPtr, Point2D *pointPtr, Axis2D *axesPtr);
static Marker *CreateMarker(Graph *graphPtr, const char *name, Rbc_Uid classUid);
static void DestroyMarker(Marker *markerPtr);
static int NameToMarker(Graph *graphPtr, const char *name, Marker **markerPtrPtr);

typedef int RbcGrMarkerOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcGrMarkerOp *proc;
} MarkerOpSpec;
static RbcGrMarkerOp BindOp;
static RbcGrMarkerOp CgetOp;
static RbcGrMarkerOp ConfigureOp;
static RbcGrMarkerOp CreateOp;
static RbcGrMarkerOp DeleteOp;
static RbcGrMarkerOp ExistsOp;
static RbcGrMarkerOp FindOp;
static RbcGrMarkerOp GetOp;
static RbcGrMarkerOp NamesOp;
static RbcGrMarkerOp RelinkOp;
static RbcGrMarkerOp TypeOp;

static void ChildEventProc(ClientData clientData, XEvent *eventPtr);
static void ChildGeometryProc(ClientData clientData, Tk_Window tkwin);
static void ChildCustodyProc(ClientData clientData, Tk_Window tkwin);

static MarkerClass bitmapMarkerClass = {
    .optionSpecs = bitmapMarkerOptionSpecs,
    .monoOptionSpecs = NULL,    
    .configProc = ConfigureBitmapMarker,
    .drawProc = DrawBitmapMarker,
    .freeProc = FreeBitmapMarker,
    .mapProc = MapBitmapMarker,
    .pointProc = PointInBitmapMarker,
    .regionProc = RegionInBitmapMarker,
    .postscriptProc = BitmapMarkerToPostScript,
};

static MarkerClass imageMarkerClass = {
    .optionSpecs = imageMarkerOptionSpecs,
    .monoOptionSpecs = NULL,
    .configProc = ConfigureImageMarker,
    .drawProc = DrawImageMarker,
    .freeProc = FreeImageMarker,
    .mapProc = MapImageMarker,
    .pointProc = PointInImageMarker,
    .regionProc = RegionInImageMarker,
    .postscriptProc = ImageMarkerToPostScript,
};


static MarkerClass lineMarkerClass = {
    .optionSpecs = lineMarkerOptionSpecs,
    .monoOptionSpecs = NULL,
    .configProc = ConfigureLineMarker,
    .drawProc = DrawLineMarker,
    .freeProc = FreeLineMarker,
    .mapProc = MapLineMarker,
    .pointProc = PointInLineMarker,
    .regionProc = RegionInLineMarker,
    .postscriptProc = LineMarkerToPostScript,
};


static MarkerClass polygonMarkerClass = {
    .optionSpecs = polygonMarkerOptionSpecs,
    .monoOptionSpecs = polygonMarkerMonoOptionSpecs,
    .configProc = ConfigurePolygonMarker,
    .drawProc = DrawPolygonMarker,
    .freeProc = FreePolygonMarker,
    .mapProc = MapPolygonMarker,
    .pointProc = PointInPolygonMarker,
    .regionProc = RegionInPolygonMarker,
    .postscriptProc = PolygonMarkerToPostScript,
};

static MarkerClass textMarkerClass = {
    .optionSpecs = textMarkerOptionSpecs,
    .monoOptionSpecs = NULL,
    .configProc = ConfigureTextMarker,
    .drawProc = DrawTextMarker,
    .freeProc = FreeTextMarker,
    .mapProc = MapTextMarker,
    .pointProc = PointInTextMarker,
    .regionProc = RegionInTextMarker,
    .postscriptProc = TextMarkerToPostScript,
};

static MarkerClass windowMarkerClass = {
    .optionSpecs = windowMarkerOptionSpecs,
    .monoOptionSpecs = NULL,
    .configProc = ConfigureWindowMarker,
    .drawProc = DrawWindowMarker,
    .freeProc = FreeWindowMarker,
    .mapProc = MapWindowMarker,
    .pointProc = PointInWindowMarker,
    .regionProc = RegionInWindowMarker,
    .postscriptProc = WindowMarkerToPostScript,
};

static int InitMarkerOptions(Marker *markerPtr) {
    Graph *graphPtr;
    const Tk_OptionSpec *optionSpecs;
    char *initialName;
    char *componentName;
    int result;

    graphPtr = markerPtr->graphPtr;

    assert(markerPtr->classPtr->optionSpecs != NULL);
    assert(!markerPtr->optionsInitialized);

    optionSpecs = markerPtr->classPtr->optionSpecs;

    if ((Tk_Depth(graphPtr->tkwin) == 1) && (markerPtr->classPtr->monoOptionSpecs != NULL)) {
        optionSpecs = markerPtr->classPtr->monoOptionSpecs;
    }

    markerPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, optionSpecs);

    initialName = markerPtr->name;
    markerPtr->name = NULL;

    componentName = RbcStrdup(initialName);

    if (componentName[0] != '\0') {
        componentName[0] = (char)tolower((unsigned char)componentName[0]);
    }

    result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, componentName, markerPtr->classUid,
                                      (char *)markerPtr, markerPtr->optionTable);

    ckfree(componentName);

    if (result != TCL_OK) {
        if (markerPtr->name != NULL) {
            ckfree(markerPtr->name);
        }

        markerPtr->name = initialName;
        markerPtr->optionTable = NULL;

        return TCL_ERROR;
    }

    if (markerPtr->name != NULL) {
        ckfree(markerPtr->name);
    }

    markerPtr->name = initialName;
    markerPtr->optionsInitialized = TRUE;

    return TCL_OK;
}

static int RestoreMarkerOptions(Tcl_Interp *interp, Tk_SavedOptions *savedOptionsPtr) {
    Tcl_Obj *errorObjPtr;

    errorObjPtr = Tcl_GetObjResult(interp);
    Tcl_IncrRefCount(errorObjPtr);

    Tk_RestoreSavedOptions(savedOptionsPtr);

    Tcl_SetObjResult(interp, errorObjPtr);

    Tcl_DecrRefCount(errorObjPtr);

    return TCL_ERROR;
}

static int ConfigureMarkerOptions(Marker *markerPtr, Tcl_Size objc, Tcl_Obj *const objv[], int creating) {
    Graph *graphPtr;
    Tk_SavedOptions savedOptions;
    Tcl_HashEntry *hPtr;
    const char *oldName;
    const char *newName;
    int nameChanged;
    int mask;

    graphPtr = markerPtr->graphPtr;
    oldName = markerPtr->name;
    assert(markerPtr->classPtr->optionSpecs != NULL);
    assert(markerPtr->optionsInitialized);
    if (Tk_SetOptions(graphPtr->interp, (char *)markerPtr, markerPtr->optionTable, objc, objv, graphPtr->tkwin,
                      &savedOptions, &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    newName = markerPtr->name;
    if ((newName == NULL) || (newName[0] == '\0')) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("marker name may not be empty", -1));
        return RestoreMarkerOptions(graphPtr->interp, &savedOptions);
    }
    if (newName[0] == '-') {
        Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("name of marker \"%s\" can't start with a '-'", newName));
        return RestoreMarkerOptions(graphPtr->interp, &savedOptions);
    }
    nameChanged = (strcmp(oldName, newName) != 0);

    /*
     * Creation retains the existing replacement semantics: creating
     * a marker with an existing name replaces the old marker.
     *
     * Reconfiguration, however, may not rename a marker over another
     * existing marker.
     */
    if ((!creating) && nameChanged) {
        hPtr = Tcl_FindHashEntry(&graphPtr->markers.table, newName);
        if ((hPtr != NULL) && (Tcl_GetHashValue(hPtr) != markerPtr)) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("can't rename marker: \"%s\" already exists", newName));
            return RestoreMarkerOptions(graphPtr->interp, &savedOptions);
        }
    }
    markerPtr->optionMask = mask;
    if ((*markerPtr->classPtr->configProc)(markerPtr) != TCL_OK) {
        markerPtr->optionMask = 0;
        return RestoreMarkerOptions(graphPtr->interp, &savedOptions);
    }
    markerPtr->optionMask = 0;

    /*
     * Commit the hash-table rename only after concrete configuration
     * succeeds.
     */
    if ((!creating) && nameChanged) {
        int isNew;
        hPtr = Tcl_CreateHashEntry(&graphPtr->markers.table, newName, &isNew);
        assert(isNew);
        Tcl_SetHashValue(hPtr, markerPtr);
        if (markerPtr->hashPtr != NULL) {
            Tcl_DeleteHashEntry(markerPtr->hashPtr);
        }
        markerPtr->hashPtr = hPtr;
    }
    Tk_FreeSavedOptions(&savedOptions);
    return TCL_OK;
}

static void ReleaseMarkerTkResources(Marker *markerPtr) {
    Graph *graphPtr;

    if ((!markerPtr->optionsInitialized) || (markerPtr->tkResourcesReleased)) {
        return;
    }
    graphPtr = markerPtr->graphPtr;
    assert(graphPtr->tkwin != NULL);

    /*
     * Release concrete GCs, pixmaps, images, segments, and other
     * derived resources before Tk releases option-managed resources.
     */
    (*markerPtr->classPtr->freeProc)(graphPtr, markerPtr);
    if (markerPtr->worldPts != NULL) {
        ckfree(markerPtr->worldPts);
        markerPtr->worldPts = NULL;
        markerPtr->nWorldPts = 0;
    }
    if (markerPtr->tags != NULL) {
        ckfree(markerPtr->tags);
        markerPtr->tags = NULL;
    }
    if (markerPtr->axes.x != NULL) {
        Rbc_FreeAxisReference(graphPtr, markerPtr->axes.x);
        markerPtr->axes.x = NULL;
    }
    if (markerPtr->axes.y != NULL) {
        Rbc_FreeAxisReference(graphPtr, markerPtr->axes.y);
        markerPtr->axes.y = NULL;
    }
    Tk_FreeConfigOptions((char *)markerPtr, markerPtr->optionTable, graphPtr->tkwin);
    markerPtr->tkResourcesReleased = TRUE;
}

void Rbc_ReleaseMarkerTkResources(Graph *graphPtr) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->markers.table, &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        Marker *markerPtr;

        markerPtr = Tcl_GetHashValue(hPtr);

        ReleaseMarkerTkResources(markerPtr);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * BoxesDontOverlap --
 *
 *      Tests if the bounding box of a marker overlaps the plotting
 *      area in any way.  If so, the marker will be drawn.  Just do a
 *      min/max test on the extents of both boxes.
 *
 *      Note: It's assumed that the extents of the bounding box lie
 *            within the area.  So for a 10x10 rectangle, bottom and
 *            left would be 9.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Extents2D *extsPtr
 *
 * Results:
 *      Returns 0 is the marker is visible in the plotting area, and
 *      1 otherwise (marker is clipped).
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int BoxesDontOverlap(Graph *graphPtr, Extents2D *extsPtr) {
    assert(extsPtr->right >= extsPtr->left);
    assert(extsPtr->bottom >= extsPtr->top);
    assert(graphPtr->right >= graphPtr->left);
    assert(graphPtr->bottom >= graphPtr->top);

    return (((double)graphPtr->right < extsPtr->left) || ((double)graphPtr->bottom < extsPtr->top) ||
            (extsPtr->right < (double)graphPtr->left) || (extsPtr->bottom < (double)graphPtr->top));
}

static int GetMarkerTagsFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, char ***tagsPtrPtr) {
    const char **tags;
    Tcl_Size nTags;

    *tagsPtrPtr = NULL;

    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        return TCL_OK;
    }

    tags = NULL;
    nTags = 0;

    if (Tcl_SplitList(interp, Tcl_GetString(objPtr), &nTags, &tags) != TCL_OK) {
        return TCL_ERROR;
    }

    if (nTags == 0) {
        if (tags != NULL) {
            ckfree((char *)tags);
        }

        return TCL_OK;
    }

    *tagsPtrPtr = (char **)tags;
    return TCL_OK;
}

static int GetMarkerStateFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *statePtr) {
    const char *string;

    string = Tcl_GetString(objPtr);

    if (strcmp(string, "normal") == 0) {
        *statePtr = STATE_NORMAL;
    } else if (strcmp(string, "active") == 0) {
        *statePtr = STATE_ACTIVE;
    } else if (strcmp(string, "disabled") == 0) {
        *statePtr = STATE_DISABLED;
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad state \"%s\": should be normal, "
                                               "active, or disabled",
                                               string));

        return TCL_ERROR;
    }

    return TCL_OK;
}

static int GetMarkerCoordinatesFromObj(Marker *markerPtr, Tcl_Obj *objPtr, Point2D **worldPtsPtr, Tcl_Size *nWorldPtsPtr) {
    Tcl_Interp *interp;
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_Size i;
    Tcl_Size minArgs;
    Tcl_Size maxArgs;
    Tcl_Size nWorldPts;
    Point2D *worldPts;
    Point2D *pointPtr;

    interp = markerPtr->graphPtr->interp;

    *worldPtsPtr = NULL;
    *nWorldPtsPtr = 0;

    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        return TCL_OK;
    }

    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }

    if (objc == 0) {
        return TCL_OK;
    }

    if (objc & 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("odd number of marker coordinates specified", -1));

        return TCL_ERROR;
    }

    if (markerPtr->classUid == rbcLineMarkerUid) {
        minArgs = 4;
        maxArgs = 0;
    } else if (markerPtr->classUid == rbcPolygonMarkerUid) {
        minArgs = 6;
        maxArgs = 0;
    } else if ((markerPtr->classUid == rbcWindowMarkerUid) || (markerPtr->classUid == rbcTextMarkerUid)) {
        minArgs = 2;
        maxArgs = 2;
    } else if ((markerPtr->classUid == rbcImageMarkerUid) || (markerPtr->classUid == rbcBitmapMarkerUid)) {
        minArgs = 2;
        maxArgs = 4;
    } else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown marker type", -1));

        return TCL_ERROR;
    }

    if (objc < minArgs) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("too few marker coordinates specified", -1));

        return TCL_ERROR;
    }

    if ((maxArgs > 0) && (objc > maxArgs)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("too many marker coordinates specified", -1));

        return TCL_ERROR;
    }
    nWorldPts = objc / 2;
    if ((size_t)nWorldPts > SIZE_MAX / sizeof(*worldPts)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("too many marker coordinates specified", -1));
        return TCL_ERROR;
    }
    worldPts = Tcl_AttemptAlloc((size_t)nWorldPts * sizeof(*worldPts));
    if (worldPts == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate marker coordinate array", -1));
        return TCL_ERROR;
    }
    pointPtr = worldPts;
    for (i = 0; i < objc; i += 2) {
        if ((GetCoordinate(interp, Tcl_GetString(objv[i]), &pointPtr->x) != TCL_OK) ||
            (GetCoordinate(interp, Tcl_GetString(objv[i + 1]), &pointPtr->y) != TCL_OK)) {
            ckfree((char *)worldPts);
            return TCL_ERROR;
        }

        pointPtr++;
    }

    *worldPtsPtr = worldPts;
    *nWorldPtsPtr = nWorldPts;

    return TCL_OK;
}

static void FreeParsedMarkerOptions(Graph *graphPtr, ParsedMarkerOptions *optionsPtr) {
    if (optionsPtr->tags != NULL) {
        ckfree((char *)optionsPtr->tags);
        optionsPtr->tags = NULL;
    }
    if (optionsPtr->worldPts != NULL) {
        ckfree((char *)optionsPtr->worldPts);
        optionsPtr->worldPts = NULL;
        optionsPtr->nWorldPts = 0;
    }
    if (optionsPtr->xAxis != NULL) {
        Rbc_FreeAxisReference(graphPtr, optionsPtr->xAxis);
        optionsPtr->xAxis = NULL;
    }
    if (optionsPtr->yAxis != NULL) {
        Rbc_FreeAxisReference(graphPtr, optionsPtr->yAxis);
        optionsPtr->yAxis = NULL;
    }
}

static int ParseMarkerOptions(Marker *markerPtr, ParsedMarkerOptions *optionsPtr) {
    Graph *graphPtr;

    graphPtr = markerPtr->graphPtr;
    memset(optionsPtr, 0, sizeof(*optionsPtr));
    if (GetMarkerTagsFromObj(graphPtr->interp, markerPtr->bindTagsObjPtr, &optionsPtr->tags) != TCL_OK) {
        goto error;
    }
    if (GetMarkerCoordinatesFromObj(markerPtr, markerPtr->coordsObjPtr, &optionsPtr->worldPts,
                                    &optionsPtr->nWorldPts) != TCL_OK) {
        goto error;
    }
    if (GetMarkerStateFromObj(graphPtr->interp, markerPtr->stateObjPtr, &optionsPtr->state) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetAxisFromObj(graphPtr, markerPtr->mapXObjPtr, rbcXAxisUid, 0, &optionsPtr->xAxis) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetAxisFromObj(graphPtr, markerPtr->mapYObjPtr, rbcYAxisUid, 0, &optionsPtr->yAxis) != TCL_OK) {
        goto error;
    }
    return TCL_OK;

error:
    FreeParsedMarkerOptions(graphPtr, optionsPtr);
    return TCL_ERROR;
}

static void CommitMarkerOptions(Marker *markerPtr, ParsedMarkerOptions *optionsPtr) {
    Graph *graphPtr;

    graphPtr = markerPtr->graphPtr;
    if (markerPtr->tags != NULL) {
        ckfree((char *)markerPtr->tags);
    }
    markerPtr->tags = optionsPtr->tags;
    optionsPtr->tags = NULL;
    if (markerPtr->worldPts != NULL) {
        ckfree((char *)markerPtr->worldPts);
    }
    markerPtr->worldPts = optionsPtr->worldPts;
    markerPtr->nWorldPts = optionsPtr->nWorldPts;
    optionsPtr->worldPts = NULL;
    optionsPtr->nWorldPts = 0;
    if (markerPtr->axes.x != NULL) {
        Rbc_FreeAxisReference(graphPtr, markerPtr->axes.x);
    }
    if (markerPtr->axes.y != NULL) {
        Rbc_FreeAxisReference(graphPtr, markerPtr->axes.y);
    }
    markerPtr->axes.x = optionsPtr->xAxis;
    markerPtr->axes.y = optionsPtr->yAxis;
    optionsPtr->xAxis = NULL;
    optionsPtr->yAxis = NULL;
    markerPtr->state = optionsPtr->state;
}

static int GetMarkerColorFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, int allowDefault,
                                 XColor **colorPtrPtr) {
    const char *string;
    Tcl_Size length;

    *colorPtrPtr = NULL;
    if (objPtr == NULL) {
        return TCL_OK;
    }
    string = Tcl_GetStringFromObj(objPtr, &length);
    if (length == 0) {
        return TCL_OK;
    }
    if (allowDefault && (string[0] == 'd') && (length <= 8) && (strncmp(string, "defcolor", (size_t)length) == 0)) {
        *colorPtrPtr = COLOR_DEFAULT;
        return TCL_OK;
    }
    *colorPtrPtr = Tk_GetColor(interp, tkwin, Tk_GetUid(string));
    return (*colorPtrPtr != NULL) ? TCL_OK : TCL_ERROR;
}

static int GetColorPairFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, int allowDefault,
                               ColorPair *pairPtr) {
    ColorPair newPair;
    Tcl_Obj **objv;
    Tcl_Size objc;

    memset(&newPair, 0, sizeof(newPair));
    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        *pairPtr = newPair;
        return TCL_OK;
    }
    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc > 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("too many names in colors list", -1));
        return TCL_ERROR;
    }
    if (objc > 0) {
        if (GetMarkerColorFromObj(interp, tkwin, objv[0], allowDefault, &newPair.fgColor) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (objc > 1) {
        if (GetMarkerColorFromObj(interp, tkwin, objv[1], allowDefault, &newPair.bgColor) != TCL_OK) {
            Rbc_FreeColorPair(&newPair);
            return TCL_ERROR;
        }
    }
    *pairPtr = newPair;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * GetCoordinate --
 *
 *      Convert the expression string into a floating point value. The
 *      only reason we use this routine instead of Rbc_ExprDouble is to
 *      handle "elastic" bounds.  That is, convert the strings "-Inf",
 *      "Inf" into -(DBL_MAX) and DBL_MAX respectively.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      const char *expr - Numeric expression string to parse
 *      double *valuePtr - Real-valued result of expression
 *
 * Results:
 *      The return value is a standard Tcl result.  The value of the
 *      expression is passed back via valuePtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int GetCoordinate(Tcl_Interp *interp, const char *expr, double *valuePtr) {
    char c;

    c = expr[0];
    if ((c == 'I') && (strcmp(expr, "Inf") == 0)) {
        *valuePtr = DBL_MAX; /* Elastic upper bound */
    } else if ((c == '-') && (expr[1] == 'I') && (strcmp(expr, "-Inf") == 0)) {
        *valuePtr = -DBL_MAX; /* Elastic lower bound */
    } else if ((c == '+') && (expr[1] == 'I') && (strcmp(expr, "+Inf") == 0)) {
        *valuePtr = DBL_MAX; /* Elastic upper bound */
    } else if (Tcl_ExprDouble(interp, expr, valuePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * HMap --
 *
 *      Map the given graph coordinate value to its axis, returning a
 *      window position.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      double x
 *
 * Results:
 *      Returns a floating point number representing the window
 *      coordinate position on the given axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static double HMap(Graph *graphPtr, Axis *axisPtr, double x) {
    register double norm;

    if (x == DBL_MAX) {
        norm = 1.0;
    } else if (x == -DBL_MAX) {
        norm = 0.0;
    } else {
        if (axisPtr->logScale) {
            if (x > 0.0) {
                x = log10(x);
            } else if (x < 0.0) {
                x = 0.0;
            }
        }
        norm = NORMALIZE(axisPtr, x);
    }
    if (axisPtr->descending) {
        norm = 1.0 - norm;
    }
    /* Horizontal transformation */
    return ((norm * graphPtr->hRange) + graphPtr->hOffset);
}

/*
 * ----------------------------------------------------------------------
 *
 * VMap --
 *
 *      Map the given graph coordinate value to its axis, returning a
 *      window position.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      double y
 *
 * Results:
 *      Returns a double precision number representing the window
 *      coordinate position on the given axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static double VMap(Graph *graphPtr, Axis *axisPtr, double y) {
    register double norm;

    if (y == DBL_MAX) {
        norm = 1.0;
    } else if (y == -DBL_MAX) {
        norm = 0.0;
    } else {
        if (axisPtr->logScale) {
            if (y > 0.0) {
                y = log10(y);
            } else if (y < 0.0) {
                y = 0.0;
            }
        }
        norm = NORMALIZE(axisPtr, y);
    }
    if (axisPtr->descending) {
        norm = 1.0 - norm;
    }
    /* Vertical transformation */
    return (((1.0 - norm) * graphPtr->vRange) + graphPtr->vOffset);
}

/*
 * ----------------------------------------------------------------------
 *
 * MapPoint --
 *
 *      Maps the given graph x,y coordinate values to a window position.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Point2D *pointPtr - Graph X-Y coordinate.
 *      Axis2D *axesPtr - Specifies which axes to use
 *
 * Results:
 *      Returns a XPoint structure containing the window coordinates
 *      of the given graph x,y coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static Point2D MapPoint(Graph *graphPtr, Point2D *pointPtr, Axis2D *axesPtr) {
    Point2D result;

    if (graphPtr->inverted) {
        result.x = HMap(graphPtr, axesPtr->y, pointPtr->y);
        result.y = VMap(graphPtr, axesPtr->x, pointPtr->x);
    } else {
        result.x = HMap(graphPtr, axesPtr->x, pointPtr->x);
        result.y = VMap(graphPtr, axesPtr->y, pointPtr->y);
    }
    return result; /* Result is screen coordinate. */
}

/*
 *----------------------------------------------------------------------
 *
 * CreateMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *name
 *      Rbc_Uid classUid
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static Marker *CreateMarker(Graph *graphPtr, const char *name, Rbc_Uid classUid) {
    Marker *markerPtr;

    /* Create the new marker based upon the given type */
    if (classUid == rbcBitmapMarkerUid) {
        markerPtr = CreateBitmapMarker(); /* bitmap */
    } else if (classUid == rbcLineMarkerUid) {
        markerPtr = CreateLineMarker(); /* line */
    } else if (classUid == rbcImageMarkerUid) {
        markerPtr = CreateImageMarker(); /* image */
    } else if (classUid == rbcTextMarkerUid) {
        markerPtr = CreateTextMarker(); /* text */
    } else if (classUid == rbcPolygonMarkerUid) {
        markerPtr = CreatePolygonMarker(); /* polygon */
    } else if (classUid == rbcWindowMarkerUid) {
        markerPtr = CreateWindowMarker(); /* window */
    } else {
        return NULL;
    }
    assert(markerPtr);
    markerPtr->graphPtr = graphPtr;
    markerPtr->hidden = markerPtr->drawUnder = FALSE;
    markerPtr->optionMask = 0;
    markerPtr->flags |= MAP_ITEM;
    markerPtr->name = RbcStrdup(name);
    markerPtr->classUid = classUid;
    markerPtr->optionTable = NULL;
    markerPtr->optionsInitialized = FALSE;
    markerPtr->tkResourcesReleased = FALSE;
    return markerPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DestroyMarker(Marker *markerPtr) {
    Graph *graphPtr;

    graphPtr = markerPtr->graphPtr;
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_DeleteBindings(graphPtr->bindTable, markerPtr);
    if (markerPtr->optionsInitialized) {
        /*
         * During normal deletion the graph window is alive. During
         * graph teardown, the resources have already been released by
         * Rbc_ReleaseMarkerTkResources().
         */
        if ((!markerPtr->tkResourcesReleased) && (graphPtr->tkwin != NULL)) {
            ReleaseMarkerTkResources(markerPtr);
        }
    } else {
        /*
         * Cleanup after failure before Tk option initialisation
         * completed. These fields are not yet owned by a successfully
         * initialised Tk option record.
         */
        (*markerPtr->classPtr->freeProc)(graphPtr, markerPtr);
        if (markerPtr->worldPts != NULL) {
            ckfree((char *)markerPtr->worldPts);
        }
        if (markerPtr->tags != NULL) {
            ckfree((char *)markerPtr->tags);
        }
        if (markerPtr->name != NULL) {
            ckfree(markerPtr->name);
        }
        if (markerPtr->elemName != NULL) {
            ckfree(markerPtr->elemName);
        }
        if (markerPtr->axes.x != NULL) {
            Rbc_FreeAxisReference(graphPtr, markerPtr->axes.x);
        }
        if (markerPtr->axes.y != NULL) {
            Rbc_FreeAxisReference(graphPtr, markerPtr->axes.y);
        }
    }
    if (markerPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(markerPtr->hashPtr);
    }
    if (markerPtr->linkPtr != NULL) {
        Rbc_ChainDeleteLink(graphPtr->markers.displayList, markerPtr->linkPtr);
    }
    ckfree(markerPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * FreeMappedBitmapResources --
 *
 *      Releases bitmap resources generated while mapping a bitmap
 *      marker. Configured Tk bitmaps such as srcBitmap and srcMask
 *      are not owned by this function.
 *
 *----------------------------------------------------------------------
 */
static void
FreeMappedBitmapResources(
    Graph *graphPtr,
    BitmapMarker *bmPtr)
{
    /*
     * destMask may alias destBitmap, but in that case only
     * destBitmapOwned may be true.
     */
    assert(!((bmPtr->destMaskOwned) &&
             (bmPtr->destBitmapOwned) &&
             (bmPtr->destMask == bmPtr->destBitmap)));

    if ((bmPtr->destMaskOwned) &&
        (bmPtr->destMask != None)) {
        Tk_FreePixmap(
            graphPtr->display,
            bmPtr->destMask);
    }

    if ((bmPtr->destBitmapOwned) &&
        (bmPtr->destBitmap != None)) {
        Tk_FreePixmap(
            graphPtr->display,
            bmPtr->destBitmap);
    }

    bmPtr->destMask = None;
    bmPtr->destBitmap = None;
    bmPtr->destMaskOwned = FALSE;
    bmPtr->destBitmapOwned = FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * AndBitmapMasks --
 *
 *      Creates a one-bit pixmap containing mask1 AND mask2.
 *
 * Results:
 *      Returns a newly allocated pixmap. The caller owns it and
 *      must release it with Tk_FreePixmap().
 *
 *----------------------------------------------------------------------
 */
static Pixmap AndBitmapMasks(Tk_Window tkwin, Pixmap mask1, Pixmap mask2, int width, int height) {
    Display *display;
    Drawable root;
    Pixmap result;
    XGCValues gcValues;
    GC gc;

    display = Tk_Display(tkwin);
    root = RootWindowOfScreen(Tk_Screen(tkwin));

    result = Tk_GetPixmap(display, root, width, height, 1);

    if (result == None) {
        return None;
    }

    /*
     * All three pixmaps are depth-one pixmaps, so XCopyArea can
     * copy their actual bits without foreground/background
     * expansion.
     */
    gcValues.function = GXcopy;

    gc = XCreateGC(display, result, GCFunction, &gcValues);

    if (gc == NULL) {
        Tk_FreePixmap(display, result);
        return None;
    }

    /*
     * result = mask1
     */
    XCopyArea(display, mask1, result, gc, 0, 0, (unsigned int)width, (unsigned int)height, 0, 0);

    /*
     * result = result AND mask2
     */
    XSetFunction(display, gc, GXand);

    XCopyArea(display, mask2, result, gc, 0, 0, (unsigned int)width, (unsigned int)height, 0, 0);

    XFreeGC(display, gc);

    return result;
}
/*
 * ----------------------------------------------------------------------
 *
 * ConfigureBitmapMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a bitmap marker.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      the interpreter result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as bitmap pixmap, colors,
 *      rotation, etc. get set for markerPtr; old resources get freed,
 *      if there were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureBitmapMarker(Marker *markerPtr) {
    Graph *graphPtr;
    BitmapMarker *bmPtr;
    ParsedMarkerOptions markerOptions;
    XGCValues gcValues;
    unsigned long gcMask;
    GC newGC;
    GC newFillGC;
    double newTheta;

    graphPtr = markerPtr->graphPtr;
    bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);
    newGC = NULL;
    newFillGC = NULL;
    /*
     * Parse and acquire the common marker options without changing
     * the active marker.
     */
    if (ParseMarkerOptions(markerPtr, &markerOptions) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * A configured mask must have the same dimensions as the source
     * bitmap. Do not invalidate the old mapped resources until this
     * validation succeeds.
     */
    if ((bmPtr->srcBitmap != None) && (bmPtr->srcMask != None)) {
        int bitmapWidth;
        int bitmapHeight;
        int maskWidth;
        int maskHeight;
        Tk_SizeOfBitmap(graphPtr->display, bmPtr->srcBitmap, &bitmapWidth, &bitmapHeight);
        Tk_SizeOfBitmap(graphPtr->display, bmPtr->srcMask, &maskWidth, &maskHeight);
        if ((bitmapWidth != maskWidth) || (bitmapHeight != maskHeight)) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("bitmap mask has size %dx%d, expected %dx%d", maskWidth,
                                                             maskHeight, bitmapWidth, bitmapHeight));
            goto error;
        }
    }
    /*
     * Store a normalised angle separately from the configured value.
     * The configured -rotate value remains unchanged for cget.
     */
    newTheta = FMOD(bmPtr->rotate, 360.0);
    if (newTheta < 0.0) {
        newTheta += 360.0;
    }
    /*
     * GCs are needed only when a source bitmap exists.
     */
    if (bmPtr->srcBitmap != None) {
        memset(&gcValues, 0, sizeof(gcValues));
        gcMask = 0;
        if (bmPtr->outlineColor != NULL) {
            gcValues.foreground = bmPtr->outlineColor->pixel;
            gcMask |= GCForeground;
        }
        if (bmPtr->fillColor != NULL) {
            gcValues.background = bmPtr->fillColor->pixel;
            gcMask |= GCBackground;
        }
        /*
         * DrawBitmapMarker() changes the clip mask and clip origin
         * before drawing, matching the existing bitmap-marker logic.
         */
        newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
        /*
         * fillGC is used only to paint the background polygon of an
         * arbitrarily rotated bitmap.
         */
        if (bmPtr->fillColor != NULL) {
            memset(&gcValues, 0, sizeof(gcValues));
            gcValues.foreground = bmPtr->fillColor->pixel;
            newFillGC = Tk_GetGC(graphPtr->tkwin, GCForeground, &gcValues);
        }
    }
    /*
     * All validation has succeeded. Commit common marker resources.
     */
    CommitMarkerOptions(markerPtr, &markerOptions);
    /*
     * Previously mapped, scaled, rotated, or combined-mask resources
     * are no longer valid.
     */
    FreeMappedBitmapResources(graphPtr, bmPtr);
    bmPtr->destWidth = 0;
    bmPtr->destHeight = 0;
    bmPtr->nOutlinePts = 0;
    /*
     * Replace the old drawing GCs.
     */
    if (bmPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->gc);
    }
    if (bmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->fillGC);
    }
    bmPtr->gc = newGC;
    bmPtr->fillGC = newFillGC;
    newGC = NULL;
    newFillGC = NULL;
    bmPtr->theta = newTheta;
    markerPtr->flags |= MAP_ITEM;
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;

error:
    if (newGC != NULL) {
        Tk_FreeGC(graphPtr->display, newGC);
    }
    if (newFillGC != NULL) {
        Tk_FreeGC(graphPtr->display, newFillGC);
    }
    FreeParsedMarkerOptions(graphPtr, &markerOptions);
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapBitmapMarker --
 *
 *      This procedure gets called each time the layout of the graph
 *      changes.  The x, y window coordinates of the bitmap marker are
 *      saved in the marker structure.
 *
 *      Additionly, if no background color was specified, the
 *      GCTileStipXOrigin and GCTileStipYOrigin attributes are set in
 *      the private GC.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Window coordinates are saved and if no background color was
 *      set, the GC stipple origins are changed to calculated window
 *      coordinates.
 *
 * ----------------------------------------------------------------------
 */
static void MapBitmapMarker(Marker *markerPtr) {
    BitmapMarker *bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);
    Extents2D exts;
    Graph *graphPtr = markerPtr->graphPtr;
    Pixmap effectiveMask;
    int effectiveMaskOwned;
    Point2D anchorPos;
    Point2D corner1, corner2;
    int destWidth, destHeight;
    int srcWidth, srcHeight;
    register int i;

    FreeMappedBitmapResources(graphPtr, bmPtr);

    if (bmPtr->srcBitmap == None) {
        return;
    }

    effectiveMask = None;
    effectiveMaskOwned = FALSE;
    /*
     * Collect the coordinates.  The number of coordinates will determine
     * the calculations to be made.
     *
     *       x1 y1    A single pair of X-Y coordinates.  They represent
     *            the anchor position of the bitmap.
     *
     *    x1 y1 x2 y2    Two pairs of X-Y coordinates.  They represent
     *            two opposite corners of a bounding rectangle. The
     *            bitmap is possibly rotated and scaled to fit into
     *            this box.
     *
     */
    Tk_SizeOfBitmap(graphPtr->display, bmPtr->srcBitmap, &srcWidth, &srcHeight);
    corner1 = MapPoint(graphPtr, bmPtr->core.worldPts, &bmPtr->core.axes);
    if (bmPtr->core.nWorldPts > 1) {
        double hold;

        corner2 = MapPoint(graphPtr, bmPtr->core.worldPts + 1, &bmPtr->core.axes);
        /* Flip the corners if necessary */
        if (corner1.x > corner2.x) {
            hold = corner1.x, corner1.x = corner2.x, corner2.x = hold;
        }
        if (corner1.y > corner2.y) {
            hold = corner1.y, corner1.y = corner2.y, corner2.y = hold;
        }
    } else {
        corner2.x = corner1.x + srcWidth - 1;
        corner2.y = corner1.y + srcHeight - 1;
    }
    destWidth = (int)(corner2.x - corner1.x) + 1;
    destHeight = (int)(corner2.y - corner1.y) + 1;

    if (bmPtr->core.nWorldPts == 1) {
        anchorPos = Rbc_TranslatePoint(&corner1, destWidth, destHeight, bmPtr->anchor);
    } else {
        anchorPos = corner1;
    }
    anchorPos.x += bmPtr->core.xOffset;
    anchorPos.y += bmPtr->core.yOffset;

    /* Check if the bitmap sits at least partially in the plot area. */
    exts.left = anchorPos.x;
    exts.top = anchorPos.y;
    exts.right = anchorPos.x + destWidth - 1;
    exts.bottom = anchorPos.y + destHeight - 1;

    bmPtr->core.clipped = BoxesDontOverlap(graphPtr, &exts);
    if (bmPtr->core.clipped) {
        return; /* Bitmap is offscreen. Don't generate
                 * rotated or scaled bitmaps. */
    }

    /*
     * Select the source mask to be transformed.
     *
     * With no background, zero source-bitmap bits must remain
     * transparent, so an explicit mask is combined with the bitmap.
     */
    if (bmPtr->srcMask != None) {
        if (bmPtr->fillColor == NULL) {
            effectiveMask = AndBitmapMasks(graphPtr->tkwin, bmPtr->srcBitmap, bmPtr->srcMask, srcWidth, srcHeight);

            effectiveMaskOwned = TRUE;
        } else {
            effectiveMask = bmPtr->srcMask;
        }
    } else if (bmPtr->fillColor == NULL) {
        /*
         * Preserve existing transparent-background behaviour.
         */
        effectiveMask = bmPtr->srcBitmap;
    }

    /*
     * Scale the bitmap if necessary. It's a little tricky because we
     * only want to scale what's visible on the screen, not the entire
     * bitmap.
     */
    if ((bmPtr->theta != 0.0) || (destWidth != srcWidth) || (destHeight != srcHeight)) {
        int regionWidth, regionHeight;
        Region2D region; /* Indicates the portion of the scaled
                          * bitmap that we want to display. */
        double left, right, top, bottom;

        /*
         * Determine the region of the bitmap visible in the plot area.
         */
        left = MAX(graphPtr->left, exts.left);
        right = MIN(graphPtr->right, exts.right);
        top = MAX(graphPtr->top, exts.top);
        bottom = MIN(graphPtr->bottom, exts.bottom);

        region.left = region.top = 0;
        if (graphPtr->left > exts.left) {
            region.left = (int)(graphPtr->left - exts.left);
        }
        if (graphPtr->top > exts.top) {
            region.top = (int)(graphPtr->top - exts.top);
        }
        regionWidth = (int)(right - left) + 1;
        regionHeight = (int)(bottom - top) + 1;
        region.right = region.left + (int)(right - left);
        region.bottom = region.top + (int)(bottom - top);

        anchorPos.x = left;
        anchorPos.y = top;
        bmPtr->destBitmap =
            Rbc_ScaleRotateBitmapRegion(graphPtr->tkwin, bmPtr->srcBitmap, srcWidth, srcHeight, region.left, region.top,
                                        regionWidth, regionHeight, destWidth, destHeight, bmPtr->theta);

        bmPtr->destBitmapOwned = TRUE;

        if (effectiveMask == bmPtr->srcBitmap) {
            /*
             * The transformed bitmap itself is also the transparency
             * mask. Do not give destMask separate ownership.
             */
            bmPtr->destMask = bmPtr->destBitmap;
            bmPtr->destMaskOwned = FALSE;
        } else if (effectiveMask != None) {
            bmPtr->destMask =
                Rbc_ScaleRotateBitmapRegion(graphPtr->tkwin, effectiveMask, srcWidth, srcHeight, region.left,
                                            region.top, regionWidth, regionHeight, destWidth, destHeight, bmPtr->theta);

            bmPtr->destMaskOwned = TRUE;
        }

        /*
         * A combined source mask is temporary in this branch because
         * destMask now contains a transformed copy.
         */
        if (effectiveMaskOwned) {
            Tk_FreePixmap(graphPtr->display, effectiveMask);

            effectiveMask = None;
            effectiveMaskOwned = FALSE;
        }

        bmPtr->destWidth = regionWidth;
        bmPtr->destHeight = regionHeight;
    } else {
        bmPtr->destWidth = srcWidth;
        bmPtr->destHeight = srcHeight;

        bmPtr->destBitmap = bmPtr->srcBitmap;
        bmPtr->destBitmapOwned = FALSE;

        /*
         * Transfer ownership of a generated combined mask to
         * destMask. Do not free effectiveMask afterward.
         */
        bmPtr->destMask = effectiveMask;
        bmPtr->destMaskOwned = effectiveMaskOwned;

        effectiveMask = None;
        effectiveMaskOwned = FALSE;
    }
    bmPtr->anchorPos = anchorPos;
    {
        double xScale, yScale;
        double tx, ty;
        double rotWidth, rotHeight;
        Point2D polygon[4];
        Tcl_Size n;

        /*
         * Compute a polygon to represent the background area of the bitmap.
         * This is needed for backgrounds of arbitrarily rotated bitmaps.
         * We also use it to print a background in PostScript.
         */
        Rbc_GetBoundingBox(srcWidth, srcHeight, bmPtr->theta, &rotWidth, &rotHeight, polygon);
        xScale = (double)destWidth / rotWidth;
        yScale = (double)destHeight / rotHeight;

        /*
         * Adjust each point of the polygon. Both scale it to the new size
         * and translate it to the actual screen position of the bitmap.
         */
        tx = exts.left + destWidth * 0.5;
        ty = exts.top + destHeight * 0.5;
        for (i = 0; i < 4; i++) {
            polygon[i].x = (polygon[i].x * xScale) + tx;
            polygon[i].y = (polygon[i].y * yScale) + ty;
        }
        Rbc_GraphExtents(graphPtr, &exts);
        n = Rbc_PolyRectClip(&exts, polygon, 4, bmPtr->outline, MAX_OUTLINE_POINTS);
        assert(n <= MAX_OUTLINE_POINTS);
        if (n < 3) {
            memcpy(&bmPtr->outline, polygon, sizeof(Point2D) * 4);
            bmPtr->nOutlinePts = 4;
        } else {
            assert(n <= MAX_OUTLINE_POINTS);
            bmPtr->nOutlinePts = (int)n;
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * PointInBitmapMarker --
 *
 *      Indicates if the given point is over the bitmap marker.  The
 *      area of the bitmap is the rectangle.
 *
 * Parameters:
 *      Marker *markerPtr
 *      Point2D *samplePtr
 *
 * Results:
 *      Returns 1 is the point is over the bitmap marker, 0 otherwise.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int PointInBitmapMarker(Marker *markerPtr, Point2D *samplePtr) {
    BitmapMarker *bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);

    if (bmPtr->srcBitmap == None) {
        return 0;
    }
    if (bmPtr->theta != 0.0) {
        Point2D points[MAX_OUTLINE_POINTS];
        register int i;

        /*
         * Generate the bounding polygon (isolateral) for the bitmap
         * and see if the point is inside of it.
         */
        for (i = 0; i < bmPtr->nOutlinePts; i++) {
            points[i].x = bmPtr->outline[i].x + bmPtr->anchorPos.x;
            points[i].y = bmPtr->outline[i].y + bmPtr->anchorPos.y;
        }
        return Rbc_PointInPolygon(samplePtr, points, bmPtr->nOutlinePts);
    }
    return ((samplePtr->x >= bmPtr->anchorPos.x) && (samplePtr->x < (bmPtr->anchorPos.x + bmPtr->destWidth)) &&
            (samplePtr->y >= bmPtr->anchorPos.y) && (samplePtr->y < (bmPtr->anchorPos.y + bmPtr->destHeight)));
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInBitmapMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Extents2D *extsPtr
 *      int enclosed
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int RegionInBitmapMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    BitmapMarker *bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);

    if (bmPtr->core.nWorldPts < 1) {
        return FALSE;
    }
    if (bmPtr->theta != 0.0) {
        Point2D points[MAX_OUTLINE_POINTS];
        register int i;

        /*
         * Generate the bounding polygon (isolateral) for the bitmap
         * and see if the point is inside of it.
         */
        for (i = 0; i < bmPtr->nOutlinePts; i++) {
            points[i].x = bmPtr->outline[i].x + bmPtr->anchorPos.x;
            points[i].y = bmPtr->outline[i].y + bmPtr->anchorPos.y;
        }
        return Rbc_RegionInPolygon(extsPtr, points, bmPtr->nOutlinePts, enclosed);
    }
    if (enclosed) {
        return ((bmPtr->anchorPos.x >= extsPtr->left) && (bmPtr->anchorPos.y >= extsPtr->top) &&
                ((bmPtr->anchorPos.x + bmPtr->destWidth) <= extsPtr->right) &&
                ((bmPtr->anchorPos.y + bmPtr->destHeight) <= extsPtr->bottom));
    }
    return !((bmPtr->anchorPos.x >= extsPtr->right) || (bmPtr->anchorPos.y >= extsPtr->bottom) ||
             ((bmPtr->anchorPos.x + bmPtr->destWidth) <= extsPtr->left) ||
             ((bmPtr->anchorPos.y + bmPtr->destHeight) <= extsPtr->top));
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawBitmapMarker --
 *
 *      Draws the bitmap marker that have a transparent of filled
 *      background.
 *
 * Parameters:
 *      Marker *markerPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      GC stipple origins are changed to current window coordinates.
 *      Commands are output to X to draw the marker in its current
 *      mode.
 *
 * ----------------------------------------------------------------------
 */
static void DrawBitmapMarker(Marker *markerPtr, Drawable drawable) {
    Graph *graphPtr = markerPtr->graphPtr;
    BitmapMarker *bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);
    Pixmap clipMask;
    double theta;

    if ((bmPtr->destBitmap == None) || (bmPtr->destWidth < 1) || (bmPtr->destHeight < 1)) {
        return;
    }

    theta = FMOD(bmPtr->theta, 90.0);
    clipMask = bmPtr->destMask;

    /*
     * Preserve the existing arbitrary-rotation workaround when
     * there is a background colour but no explicit mask.
     *
     * The polygon supplies the background, and the rotated bitmap
     * supplies the foreground through its own bitmap mask.
     */
    if ((bmPtr->srcMask == None) && (bmPtr->fillColor != NULL) && (theta != 0.0)) {
        XPoint polygon[MAX_OUTLINE_POINTS];
        int i;

        for (i = 0; i < bmPtr->nOutlinePts; i++) {
            polygon[i].x = (short int)bmPtr->outline[i].x;
            polygon[i].y = (short int)bmPtr->outline[i].y;
        }

        XFillPolygon(graphPtr->display, drawable, bmPtr->fillGC, polygon, bmPtr->nOutlinePts, Convex, CoordModeOrigin);

        clipMask = bmPtr->destBitmap;
    }

    if (clipMask != None) {
        XSetClipMask(graphPtr->display, bmPtr->gc, clipMask);

        XSetClipOrigin(graphPtr->display, bmPtr->gc, (int)bmPtr->anchorPos.x, (int)bmPtr->anchorPos.y);
    } else {
        XSetClipMask(graphPtr->display, bmPtr->gc, None);

        XSetClipOrigin(graphPtr->display, bmPtr->gc, 0, 0);
    }

    XCopyPlane(graphPtr->display, bmPtr->destBitmap, drawable, bmPtr->gc, 0, 0, bmPtr->destWidth, bmPtr->destHeight,
               (int)bmPtr->anchorPos.x, (int)bmPtr->anchorPos.y, 1);
}

/*
 *----------------------------------------------------------------------
 *
 * BitmapMaskToPostScript --
 *
 *      Emits a one-bit pixmap as a PostScript imagemask at the
 *      specified marker position and size. Bits set to one are
 *      painted using the current PostScript colour; zero bits leave
 *      the destination unchanged.
 *
 * Parameters:
 *      PsToken psToken - PostScript output token.
 *      Display *display - Display owning the pixmap.
 *      Pixmap bitmap - One-bit pixmap to emit.
 *      double x, y - Top-left destination position.
 *      int width, height - Destination dimensions.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Appends PostScript commands and bitmap data to psToken.
 *
 *----------------------------------------------------------------------
 */
static void BitmapMaskToPostScript(PsToken psToken, Display *display, Pixmap bitmap, double x, double y, int width,
                                   int height) {
    if ((bitmap == None) || (width < 1) || (height < 1)) {
        return;
    }

    Rbc_FormatToPostScript(psToken,
                           " gsave\n"
                           " %g %g translate\n"
                           " %d %d scale\n",
                           x, y + height, width, -height);

    Rbc_FormatToPostScript(psToken, " %d %d true [%d 0 0 %d 0 %d] {", width, height, width, -height, height);

    Rbc_BitmapDataToPostScript(psToken, display, bitmap, width, height);

    Rbc_AppendToPostScript(psToken, " } imagemask\n", " grestore\n", (char *)NULL);
}

/*
 * ----------------------------------------------------------------------
 *
 * BitmapMarkerToPostScript --
 *
 *      Generates PostScript to print a bitmap marker.
 *
 * Parameters:
 *      Marker *markerPtr - Marker to be printed
 *      PsToken psToken
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void BitmapMarkerToPostScript(Marker *markerPtr, PsToken psToken) {
    Graph *graphPtr;
    BitmapMarker *bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);
    Pixmap foregroundMask;
    int foregroundMaskOwned;

    graphPtr = markerPtr->graphPtr;

    if ((bmPtr->destBitmap == None) || (bmPtr->destWidth < 1) || (bmPtr->destHeight < 1)) {
        return;
    }

    foregroundMask = bmPtr->destBitmap;
    foregroundMaskOwned = FALSE;

    if (bmPtr->destMask != None) {
        /*
         * A destination mask exists in either of these cases:
         *
         *   1. The user specified -mask.
         *   2. The background is transparent, in which case
         *      destMask is already the effective foreground mask.
         */
        if (bmPtr->fillColor != NULL) {
            /*
             * Paint the background only through the explicit mask.
             *
             * This replaces the old polygon background because the
             * user-provided mask, rather than the rotated bitmap
             * bounding polygon, defines the visible region.
             */
            Rbc_BackgroundToPostScript(psToken, bmPtr->fillColor);

            BitmapMaskToPostScript(psToken, graphPtr->display, bmPtr->destMask, bmPtr->anchorPos.x, bmPtr->anchorPos.y,
                                   bmPtr->destWidth, bmPtr->destHeight);

            /*
             * The foreground is visible only where both the source
             * bitmap and the explicit mask contain set bits.
             */
            if (bmPtr->destMask != bmPtr->destBitmap) {
                foregroundMask = AndBitmapMasks(graphPtr->tkwin, bmPtr->destBitmap, bmPtr->destMask, bmPtr->destWidth,
                                                bmPtr->destHeight);

                foregroundMaskOwned = TRUE;
            }
        } else {
            /*
             * With a transparent background, MapBitmapMarker()
             * has already made destMask equal to:
             *
             *      destBitmap AND configured mask
             *
             * or has made it alias destBitmap when no explicit
             * mask was specified.
             */
            foregroundMask = bmPtr->destMask;
        }
    } else if (bmPtr->fillColor != NULL) {
        /*
         * Preserve the original behaviour when there is no explicit
         * mask: paint the entire rotated bitmap background polygon.
         */
        Rbc_BackgroundToPostScript(psToken, bmPtr->fillColor);

        Rbc_PolygonToPostScript(psToken, bmPtr->outline, bmPtr->nOutlinePts);
    }

    if ((bmPtr->outlineColor != NULL) && (foregroundMask != None)) {
        Rbc_ForegroundToPostScript(psToken, bmPtr->outlineColor);

        BitmapMaskToPostScript(psToken, graphPtr->display, foregroundMask, bmPtr->anchorPos.x, bmPtr->anchorPos.y,
                               bmPtr->destWidth, bmPtr->destHeight);
    }

    if (foregroundMaskOwned) {
        Tk_FreePixmap(graphPtr->display, foregroundMask);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeBitmapMarker --
 *
 *      Releases the memory and attributes of the bitmap marker.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Bitmap attributes (GCs, colors, bitmap, etc) get destroyed.
 *      Memory is released, X resources are freed, and the graph is
 *      redrawn.
 *
 * ----------------------------------------------------------------------
 */
static void FreeBitmapMarker(Graph *graphPtr, Marker *markerPtr) {
    BitmapMarker *bmPtr;

    bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);
    FreeMappedBitmapResources(graphPtr, bmPtr);
    if (bmPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->gc);
        bmPtr->gc = NULL;
    }
    if (bmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->fillGC);
        bmPtr->fillGC = NULL;
    }
    bmPtr->destWidth = 0;
    bmPtr->destHeight = 0;
    bmPtr->nOutlinePts = 0;
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateBitmapMarker --
 *
 *      Allocate memory and initialize methods for the new bitmap marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the bitmap marker structure.
 *
 * ----------------------------------------------------------------------
 */
static Marker *CreateBitmapMarker(void) {
    BitmapMarker *bmPtr;

    bmPtr = RbcCalloc(1, sizeof(BitmapMarker));
    if (bmPtr != NULL) {
        bmPtr->core.classPtr = &bitmapMarkerClass;
    }
    return (bmPtr != NULL) ? &bmPtr->core : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * ImageChangedProc
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      int x
 *      int y
 *      int width
 *      int height - Not used. 
 *      int imageWidth
 *      int imageHeight - Not used.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void ImageChangedProc(ClientData clientData, int x, int y, int width, int height, int imageWidth,
                             int imageHeight) {
    ImageMarker *imPtr;
    Graph *graphPtr;
    Rbc_ColorImage newSrcImage;
    Tk_PhotoHandle photo;
    const char *imageName;

    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)imageWidth;
    (void)imageHeight;
    imPtr = clientData;
    graphPtr = imPtr->core.graphPtr;
    newSrcImage = NULL;
    if (imPtr->imageObjPtr != NULL) {
        imageName = Tcl_GetString(imPtr->imageObjPtr);
        if (imageName[0] != '\0') {
            photo = Tk_FindPhoto(graphPtr->interp, imageName);
            if (photo != NULL) {
                newSrcImage = Rbc_PhotoToColorImage(photo);
            }
        }
    }

    /*
     * Always replace srcImage. In particular, an image that is no
     * longer a photo must not retain a stale photo representation.
     */
    if (imPtr->srcImage != NULL) {
        Rbc_FreeColorImage(imPtr->srcImage);
    }
    imPtr->srcImage = newSrcImage;
    graphPtr->flags |= REDRAW_BACKING_STORE;
    imPtr->core.flags |= MAP_ITEM;
    Rbc_EventuallyRedrawGraph(graphPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureImageMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a image marker.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      the interpreter result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as image pixmap, colors,
 *      rotation, etc. get set for markerPtr; old resources get freed,
 *      if there were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureImageMarker(Marker *markerPtr) {
    Graph *graphPtr;
    ImageMarker *imPtr;
    ParsedMarkerOptions markerOptions;
    const char *imageName;
    Tk_Image newTkImage;
    Rbc_ColorImage newSrcImage;
    Tk_PhotoHandle photo;
    GC newGC;

    graphPtr = markerPtr->graphPtr;
    imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);
    newTkImage = NULL;
    newSrcImage = NULL;
    newGC = NULL;

    /*
     * Parse common manually converted options without modifying the
     * active marker.
     */
    if (ParseMarkerOptions(markerPtr, &markerOptions) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Acquire all image resources before releasing the old image.
     */
    imageName = NULL;
    if (imPtr->imageObjPtr != NULL) {
        imageName = Tcl_GetString(imPtr->imageObjPtr);
    }
    if ((imageName != NULL) && (imageName[0] != '\0')) {
        newTkImage = Tk_GetImage(graphPtr->interp, graphPtr->tkwin, imageName, ImageChangedProc, imPtr);
        if (newTkImage == NULL) {
            Rbc_AppendResultStrings(graphPtr->interp, "can't find an image \"", imageName, "\"", (char *)NULL);
            goto error;
        }
        photo = Tk_FindPhoto(graphPtr->interp, imageName);
        if (photo != NULL) {
            newSrcImage = Rbc_PhotoToColorImage(photo);
        }
        newGC = Tk_GetGC(graphPtr->tkwin, 0L, NULL);
    }

    /*
     * No fallible operation remains. Commit the common marker state.
     */
    CommitMarkerOptions(markerPtr, &markerOptions);

    /*
     * Release resources derived from the previous image.
     */
    if (imPtr->pixmap != None) {
        Tk_FreePixmap(graphPtr->display, imPtr->pixmap);
        imPtr->pixmap = None;
    }
    if (imPtr->tmpImage != NULL) {
        Rbc_DestroyTemporaryImage(graphPtr->interp, imPtr->tmpImage);
        imPtr->tmpImage = NULL;
    }
    if (imPtr->srcImage != NULL) {
        Rbc_FreeColorImage(imPtr->srcImage);
        imPtr->srcImage = NULL;
    }
    if (imPtr->tkImage != NULL) {
        Tk_FreeImage(imPtr->tkImage);
        imPtr->tkImage = NULL;
    }
    if (imPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, imPtr->gc);
        imPtr->gc = NULL;
    }

    /*
     * Install the newly acquired image resources.
     */
    imPtr->tkImage = newTkImage;
    newTkImage = NULL;
    imPtr->srcImage = newSrcImage;
    newSrcImage = NULL;
    imPtr->gc = newGC;
    newGC = NULL;
    imPtr->width = 0;
    imPtr->height = 0;
    markerPtr->flags |= MAP_ITEM;
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;

error:
    if (newGC != NULL) {
        Tk_FreeGC(graphPtr->display, newGC);
    }
    if (newSrcImage != NULL) {
        Rbc_FreeColorImage(newSrcImage);
    }
    if (newTkImage != NULL) {
        Tk_FreeImage(newTkImage);
    }
    FreeParsedMarkerOptions(graphPtr, &markerOptions);
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapImageMarker --
 *
 *      This procedure gets called each time the layout of the graph
 *      changes.  The x, y window coordinates of the image marker are
 *      saved in the marker structure.
 *
 *      Additionly, if no background color was specified, the
 *      GCTileStipXOrigin and GCTileStipYOrigin attributes are set in
 *      the private GC.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Window coordinates are saved and if no background color was *
 *      set, the GC stipple origins are changed to calculated window
 *      coordinates.
 *
 * ----------------------------------------------------------------------
 */
static void MapImageMarker(Marker *markerPtr) {
    Extents2D exts;
    Graph *graphPtr;
    ImageMarker *imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);
    Point2D anchorPos;
    Point2D corner1, corner2;
    int scaledWidth, scaledHeight;
    int srcWidth, srcHeight;

    if (imPtr->tkImage == NULL) {
        return;
    }
    graphPtr = imPtr->core.graphPtr;
    corner1 = MapPoint(graphPtr, imPtr->core.worldPts, &imPtr->core.axes);
    if (imPtr->srcImage == NULL) {
        /*
         * Don't scale or rotate non-photo images.
         */
        Tk_SizeOfImage(imPtr->tkImage, &srcWidth, &srcHeight);
        imPtr->width = srcWidth;
        imPtr->height = srcHeight;
        imPtr->anchorPos.x = corner1.x + imPtr->core.xOffset;
        imPtr->anchorPos.y = corner1.y + imPtr->core.yOffset;
        exts.left = imPtr->anchorPos.x;
        exts.top = imPtr->anchorPos.y;
        exts.right = exts.left + srcWidth - 1;
        exts.bottom = exts.top + srcHeight - 1;
        imPtr->core.clipped = BoxesDontOverlap(graphPtr, &exts);
        return;
    }

    imPtr->width = srcWidth = Rbc_ColorImageWidth(imPtr->srcImage);
    imPtr->height = srcHeight = Rbc_ColorImageHeight(imPtr->srcImage);
    if ((srcWidth == 0) && (srcHeight == 0)) {
        imPtr->core.clipped = TRUE;
        return; /* Empty image. */
    }
    if (imPtr->core.nWorldPts > 1) {
        double hold;

        corner2 = MapPoint(graphPtr, imPtr->core.worldPts + 1, &imPtr->core.axes);
        /* Flip the corners if necessary */
        if (corner1.x > corner2.x) {
            hold = corner1.x, corner1.x = corner2.x, corner2.x = hold;
        }
        if (corner1.y > corner2.y) {
            hold = corner1.y, corner1.y = corner2.y, corner2.y = hold;
        }
    } else {
        corner2.x = corner1.x + srcWidth - 1;
        corner2.y = corner1.y + srcHeight - 1;
    }
    scaledWidth = (int)(corner2.x - corner1.x) + 1;
    scaledHeight = (int)(corner2.y - corner1.y) + 1;

    if (imPtr->core.nWorldPts == 1) {
        anchorPos = Rbc_TranslatePoint(&corner1, scaledWidth, scaledHeight, imPtr->anchor);
    } else {
        anchorPos = corner1;
    }
    anchorPos.x += imPtr->core.xOffset;
    anchorPos.y += imPtr->core.yOffset;

    /* Check if the image sits at least partially in the plot area. */
    exts.left = anchorPos.x;
    exts.top = anchorPos.y;
    exts.right = anchorPos.x + scaledWidth - 1;
    exts.bottom = anchorPos.y + scaledHeight - 1;

    imPtr->core.clipped = BoxesDontOverlap(graphPtr, &exts);
    if (imPtr->core.clipped) {
        return; /* Image is offscreen. Don't generate
                 * rotated or scaled images. */
    }
    if ((scaledWidth != srcWidth) || (scaledHeight != srcHeight)) {
        Tk_PhotoHandle photo;
        Rbc_ColorImage destImage;
        int x, y, width, height;
        int left, right, top, bottom;

        /* Determine the region of the subimage inside of the
         * destination image. */
        left = MAX((int)exts.left, graphPtr->left);
        top = MAX((int)exts.top, graphPtr->top);
        right = MIN((int)exts.right, graphPtr->right);
        bottom = MIN((int)exts.bottom, graphPtr->bottom);

        /* Reset image location and coordinates to that of the region */
        anchorPos.x = left;
        anchorPos.y = top;

        x = y = 0;
        if (graphPtr->left > (int)exts.left) {
            x = graphPtr->left - (int)exts.left;
        }
        if (graphPtr->top > (int)exts.top) {
            y = graphPtr->top - (int)exts.top;
        }
        width = (int)(right - left + 1);
        height = (int)(bottom - top + 1);

        destImage = Rbc_ResizeColorSubimage(imPtr->srcImage, x, y, width, height, scaledWidth, scaledHeight);
#ifdef notyet
        /* Now convert the color image into a pixmap */
        if (imPtr->pixmap != None) {
            Rbc_FreeColorTable(imPtr->colorTable);
            Tk_FreePixmap(Tk_Display(graphPtr->tkwin), imPtr->pixmap);
            imPtr->colorTable = NULL;
        }
        imPtr->pixmap = Rbc_ColorImageToPixmap(graphPtr->interp, graphPtr->tkwin, destImage, &imPtr->colorTable);
#else
        imPtr->pixmap = None;
        if (imPtr->tmpImage == NULL) {
            imPtr->tmpImage = Rbc_CreateTemporaryImage(graphPtr->interp, graphPtr->tkwin, imPtr);
            if (imPtr->tmpImage == NULL) {
                return;
            }
        }
        /* Put the scaled colorimage into the photo. */
        photo = Tk_FindPhoto(graphPtr->interp, Rbc_NameOfImage(imPtr->tmpImage));
        Rbc_ColorImageToPhoto(graphPtr->interp, destImage, photo);
#endif
        Rbc_FreeColorImage(destImage);
        imPtr->width = width;
        imPtr->height = height;
    }
    imPtr->anchorPos = anchorPos;
}

static int GetWindowMarkerDimension(Marker *markerPtr, Tcl_Obj *objPtr, int explicitlyChanged, int *valuePtr) {
    Graph *graphPtr;
    int value;

    graphPtr = markerPtr->graphPtr;

    /*
     * Zero is the internal default: use the child window's requested
     * dimension. It is not valid when explicitly configured.
     */
    if (objPtr == NULL) {
        *valuePtr = 0;
        return TCL_OK;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, objPtr, PIXELS_ANY, &value) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((value < 0) || ((value == 0) && explicitlyChanged)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("bad distance \"%d\": must be positive", value));
        return TCL_ERROR;
    }
    *valuePtr = value;
    return TCL_OK;
}

static void ReleaseWindowMarkerChild(WindowMarker *wmPtr, int destroyWindow) {
    Tk_Window tkwin;

    tkwin = wmPtr->tkwin;
    if (tkwin == NULL) {
        return;
    }

    /*
     * Clear this before invoking Tk operations so a DestroyNotify
     * callback cannot leave a stale token.
     */
    wmPtr->tkwin = NULL;
    Tk_DeleteEventHandler(tkwin, StructureNotifyMask, ChildEventProc, wmPtr);

    /*
     * Cancelling geometry management with a NULL manager does not
     * invoke our lost-slave callback.
     */
    Tk_ManageGeometry(tkwin, NULL, NULL);
    if (destroyWindow) {
        Tk_DestroyWindow(tkwin);
    } else {
        Tk_UnmapWindow(tkwin);
    }
    wmPtr->width = 0;
    wmPtr->height = 0;
}

/*
 * ----------------------------------------------------------------------
 *
 * PointInWindowMarker --
 *
 *      Indicates if the given point is over the window marker.  The
 *      area of the window is the rectangle.
 *
 * Parameters:
 *      Marker *markerPtr
 *      Point2D *samplePtr
 *
 * Results:
 *      Returns 1 is the point is over the window marker, 0 otherwise.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int PointInImageMarker(Marker *markerPtr, Point2D *samplePtr) {
    ImageMarker *imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);

    return ((samplePtr->x >= imPtr->anchorPos.x) && (samplePtr->x < (imPtr->anchorPos.x + imPtr->width)) &&
            (samplePtr->y >= imPtr->anchorPos.y) && (samplePtr->y < (imPtr->anchorPos.y + imPtr->height)));
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInImageMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Extents2D *extsPtr
 *      int enclosed
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int RegionInImageMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    ImageMarker *imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);

    if (imPtr->core.nWorldPts < 1) {
        return FALSE;
    }
    if (enclosed) {
        return ((imPtr->anchorPos.x >= extsPtr->left) && (imPtr->anchorPos.y >= extsPtr->top) &&
                ((imPtr->anchorPos.x + imPtr->width) <= extsPtr->right) &&
                ((imPtr->anchorPos.y + imPtr->height) <= extsPtr->bottom));
    }
    return !((imPtr->anchorPos.x >= extsPtr->right) || (imPtr->anchorPos.y >= extsPtr->bottom) ||
             ((imPtr->anchorPos.x + imPtr->width) <= extsPtr->left) ||
             ((imPtr->anchorPos.y + imPtr->height) <= extsPtr->top));
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawImageMarker --
 *
 *      This procedure is invoked to draw a image marker.
 *
 * Parameters:
 *      Marker *markerPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      GC stipple origins are changed to current window coordinates.
 *      Commands are output to X to draw the marker in its current mode.
 *
 * ----------------------------------------------------------------------
 */
static void DrawImageMarker(Marker *markerPtr, Drawable drawable) {
    ImageMarker *imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);
    int width, height;

    if ((imPtr->tkImage == NULL) || (Tk_ImageIsDeleted(imPtr->tkImage))) {
        return;
    }
    if (imPtr->pixmap == None) {
        Pixmap pixmap;
        Tk_Image tkImage;

        tkImage = (imPtr->tmpImage != NULL) ? imPtr->tmpImage : imPtr->tkImage;
        Tk_SizeOfImage(tkImage, &width, &height);
        /* pixmap = Tk_ImageGetPhotoPixmap(tkImage); */
        pixmap = None;
        if (pixmap == None) { /* May not be a "photo" image. */
            Tk_RedrawImage(tkImage, 0, 0, width, height, drawable, (int)imPtr->anchorPos.x, (int)imPtr->anchorPos.y);
        } else {
            XCopyArea(imPtr->core.graphPtr->display, pixmap, drawable, imPtr->gc, 0, 0, width, height,
                      (int)imPtr->anchorPos.x, (int)imPtr->anchorPos.y);
        }
    } else {
        XCopyArea(imPtr->core.graphPtr->display, imPtr->pixmap, drawable, imPtr->gc, 0, 0, imPtr->width, imPtr->height,
                  (int)imPtr->anchorPos.x, (int)imPtr->anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ImageMarkerToPostScript --
 *
 *      This procedure is invoked to print a image marker.
 *
 * Parameters:
 *      Marker *markerPtr - Marker to be printed
 *      PsToken psToken
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void ImageMarkerToPostScript(Marker *markerPtr, PsToken psToken) {
    ImageMarker *imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);
    const char *imageName;
    Tk_PhotoHandle photo;

    if ((imPtr->tkImage == NULL) || (Tk_ImageIsDeleted(imPtr->tkImage))) {
        return; /* Image doesn't exist anymore */
    }
    imageName = (imPtr->tmpImage == NULL) ? Rbc_NameOfImage(imPtr->tkImage) : Rbc_NameOfImage(imPtr->tmpImage);
    photo = Tk_FindPhoto(markerPtr->graphPtr->interp, imageName);
    if (photo == NULL) {
        return; /* Image isn't a photo image */
    }
    Rbc_PhotoToPostScript(psToken, photo, imPtr->anchorPos.x, imPtr->anchorPos.y);
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeImageMarker --
 *
 *      Destroys the structure containing the attributes of the image
 *      marker.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Image attributes (GCs, colors, image, etc) get destroyed.
 *      Memory is released, X resources are freed, and the graph is
 *      redrawn.
 *
 * ----------------------------------------------------------------------
 */
static void FreeImageMarker(Graph *graphPtr, Marker *markerPtr) {
    ImageMarker *imPtr;

    imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);
    if (imPtr->pixmap != None) {
        Tk_FreePixmap(graphPtr->display, imPtr->pixmap);
        imPtr->pixmap = None;
    }
    if (imPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, imPtr->gc);
        imPtr->gc = NULL;
    }
    if (imPtr->tkImage != NULL) {
        Tk_FreeImage(imPtr->tkImage);
        imPtr->tkImage = NULL;
    }
    if (imPtr->tmpImage != NULL) {
        Rbc_DestroyTemporaryImage(graphPtr->interp, imPtr->tmpImage);
        imPtr->tmpImage = NULL;
    }
    if (imPtr->srcImage != NULL) {
        Rbc_FreeColorImage(imPtr->srcImage);
        imPtr->srcImage = NULL;
    }
    imPtr->width = 0;
    imPtr->height = 0;
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateImageMarker --
 *
 *      Allocate memory and initialize methods for the new image marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the image marker structure.
 *
 * ----------------------------------------------------------------------
 */
static Marker *CreateImageMarker(void) {
    ImageMarker *imPtr;

    imPtr = RbcCalloc(1, sizeof(ImageMarker));
    if (imPtr != NULL) {
        imPtr->core.classPtr = &imageMarkerClass;
    }
    return (imPtr != NULL) ? &imPtr->core : NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureTextMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or
 *      reconfigure) a text marker.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      the interpreter result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as text string, colors, font,
 *      etc. get set for markerPtr;  old resources get freed, if there
 *      were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureTextMarker(Marker *markerPtr) {
    Graph *graphPtr;
    TextMarker *tmPtr;
    ParsedMarkerOptions markerOptions;
    Rbc_Pad newPadX;
    Rbc_Pad newPadY;
    Shadow newShadow;
    TextStyle layoutStyle;
    TextLayout *newTextPtr;
    Point2D newOutline[5];
    int newWidth;
    int newHeight;
    double newTheta;
    XGCValues gcValues;
    unsigned long gcMask;
    GC newTextGC;
    GC newFillGC;

    graphPtr = markerPtr->graphPtr;
    tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);
    memset(&newShadow, 0, sizeof(newShadow));
    memset(newOutline, 0, sizeof(newOutline));
    newTextPtr = NULL;
    newTextGC = NULL;
    newFillGC = NULL;
    newWidth = 0;
    newHeight = 0;

    /*
     * Parse common marker options without modifying the currently
     * committed tags, coordinates, axes, or state.
     */
    if (ParseMarkerOptions(markerPtr, &markerOptions) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Parse text-specific manually converted options.
     */
    if (Rbc_GetPadFromObj(graphPtr->interp, graphPtr->tkwin, tmPtr->padXObjPtr, &newPadX) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetPadFromObj(graphPtr->interp, graphPtr->tkwin, tmPtr->padYObjPtr, &newPadY) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetShadowFromObj(graphPtr->interp, graphPtr->tkwin, tmPtr->shadowObjPtr, &newShadow) != TCL_OK) {
        goto error;
    }

    /*
     * Preserve the existing normalised -rotate behaviour.
     */
    newTheta = FMOD(tmPtr->style.theta, 360.0);
    if (newTheta < 0.0) {
        newTheta += 360.0;
    }

    /*
     * Build a temporary style for layout calculation. Tk_SetOptions()
     * has already installed the proposed font, foreground,
     * justification, and rotation in tmPtr->style.
     */
    layoutStyle = tmPtr->style;
    layoutStyle.padX = newPadX;
    layoutStyle.padY = newPadY;
    layoutStyle.shadow = newShadow;
    layoutStyle.theta = newTheta;

    /*
     * Rebuild the layout for every successful configuration because
     * text, font, justification, padding, shadow, and rotation can all
     * affect its dimensions or bounding polygon.
     */
    if (tmPtr->string != NULL) {
        double rotWidth;
        double rotHeight;
        int i;
        newTextPtr = Rbc_GetTextLayout(tmPtr->string, &layoutStyle);
        Rbc_GetBoundingBox(newTextPtr->width, newTextPtr->height, newTheta, &rotWidth, &rotHeight, newOutline);
        newWidth = ROUND(rotWidth);
        newHeight = ROUND(rotHeight);
        for (i = 0; i < 4; i++) {
            newOutline[i].x += ROUND(rotWidth * 0.5);
            newOutline[i].y += ROUND(rotHeight * 0.5);
        }
        newOutline[4] = newOutline[0];
    }

    /*
     * Construct the replacement text GC without releasing the old GC.
     */
    memset(&gcValues, 0, sizeof(gcValues));
    gcMask = GCFont;
    gcValues.font = Tk_FontId(tmPtr->style.font);
    if (tmPtr->style.color != NULL) {
        gcValues.foreground = tmPtr->style.color->pixel;
        gcMask |= GCForeground;
    }
    newTextGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);

    /*
     * Construct the optional background GC.
     */
    if (tmPtr->fillColor != NULL) {
        memset(&gcValues, 0, sizeof(gcValues));
        gcValues.foreground = tmPtr->fillColor->pixel;
        newFillGC = Tk_GetGC(graphPtr->tkwin, GCForeground, &gcValues);
    }

    /*
     * All fallible parsing has succeeded. Commit common marker state.
     */
    CommitMarkerOptions(markerPtr, &markerOptions);

    /*
     * Replace derived text resources while the old Tk-managed string,
     * font, and colours are still retained by Tk_SavedOptions.
     */
    if (tmPtr->textPtr != NULL) {
        ckfree((char *)tmPtr->textPtr);
    }
    tmPtr->textPtr = newTextPtr;
    newTextPtr = NULL;
    if (tmPtr->style.gc != NULL) {
        Tk_FreeGC(graphPtr->display, tmPtr->style.gc);
    }
    tmPtr->style.gc = newTextGC;
    newTextGC = NULL;
    if (tmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, tmPtr->fillGC);
    }
    tmPtr->fillGC = newFillGC;
    newFillGC = NULL;

    /*
     * Shadow colours are manually owned, unlike font, foreground,
     * background, text, and other Tk-managed option resources.
     */
    if (tmPtr->style.shadow.color != NULL) {
        Tk_FreeColor(tmPtr->style.shadow.color);
    }
    tmPtr->style.shadow = newShadow;
    newShadow.color = NULL;
    tmPtr->style.padX = newPadX;
    tmPtr->style.padY = newPadY;
    tmPtr->style.theta = newTheta;
    tmPtr->width = newWidth;
    tmPtr->height = newHeight;
    memcpy(tmPtr->outline, newOutline, sizeof(newOutline));
    markerPtr->flags |= MAP_ITEM;
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;

error:
    if (newTextPtr != NULL) {
        ckfree((char *)newTextPtr);
    }
    if (newTextGC != NULL) {
        Tk_FreeGC(graphPtr->display, newTextGC);
    }
    if (newFillGC != NULL) {
        Tk_FreeGC(graphPtr->display, newFillGC);
    }
    if (newShadow.color != NULL) {
        Tk_FreeColor(newShadow.color);
    }
    FreeParsedMarkerOptions(graphPtr, &markerOptions);
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapTextMarker --
 *
 *      Calculate the layout position for a text marker.  Positional
 *      information is saved in the marker.  If the text is rotated,
 *      a bitmap containing the text is created.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      If no background color has been specified, the GC stipple
 *      origins are changed to current window coordinates. For both
 *      rotated and non-rotated text, if any old bitmap is leftover,
 *      it is freed.
 *
 * ----------------------------------------------------------------------
 */
static void MapTextMarker(Marker *markerPtr) {
    Graph *graphPtr = markerPtr->graphPtr;
    TextMarker *tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);
    Extents2D exts;
    Point2D anchorPos;

    if (tmPtr->string == NULL) {
        return;
    }
    anchorPos = MapPoint(graphPtr, tmPtr->core.worldPts, &tmPtr->core.axes);
    anchorPos = Rbc_TranslatePoint(&anchorPos, tmPtr->width, tmPtr->height, tmPtr->anchor);
    anchorPos.x += tmPtr->core.xOffset;
    anchorPos.y += tmPtr->core.yOffset;
    /*
     * Determine the bounding box of the text and test to see if it
     * is at least partially contained within the plotting area.
     */
    exts.left = anchorPos.x;
    exts.top = anchorPos.y;
    exts.right = anchorPos.x + tmPtr->width - 1;
    exts.bottom = anchorPos.y + tmPtr->height - 1;
    tmPtr->core.clipped = BoxesDontOverlap(graphPtr, &exts);
    tmPtr->anchorPos = anchorPos;
}

/*
 *----------------------------------------------------------------------
 *
 * PointInTextMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Point2D *samplePtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int PointInTextMarker(Marker *markerPtr, Point2D *samplePtr) {
    TextMarker *tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);

    if (tmPtr->string == NULL) {
        return 0;
    }
    if (tmPtr->style.theta != 0.0) {
        Point2D points[5];
        register int i;

        /*
         * Figure out the bounding polygon (isolateral) for the text
         * and see if the point is inside of it.
         */

        for (i = 0; i < 5; i++) {
            points[i].x = tmPtr->outline[i].x + tmPtr->anchorPos.x;
            points[i].y = tmPtr->outline[i].y + tmPtr->anchorPos.y;
        }
        return Rbc_PointInPolygon(samplePtr, points, 5);
    }
    return ((samplePtr->x >= tmPtr->anchorPos.x) && (samplePtr->x < (tmPtr->anchorPos.x + tmPtr->width)) &&
            (samplePtr->y >= tmPtr->anchorPos.y) && (samplePtr->y < (tmPtr->anchorPos.y + tmPtr->height)));
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInTextMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Extents2D *extsPtr
 *      int enclosed
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int RegionInTextMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    TextMarker *tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);

    if (tmPtr->core.nWorldPts < 1) {
        return FALSE;
    }
    if (tmPtr->style.theta != 0.0) {
        Point2D points[5];
        register int i;

        /*
         * Generate the bounding polygon (isolateral) for the bitmap
         * and see if the point is inside of it.
         */
        for (i = 0; i < 4; i++) {
            points[i].x = tmPtr->outline[i].x + tmPtr->anchorPos.x;
            points[i].y = tmPtr->outline[i].y + tmPtr->anchorPos.y;
        }
        return Rbc_RegionInPolygon(extsPtr, points, 4, enclosed);
    }
    if (enclosed) {
        return ((tmPtr->anchorPos.x >= extsPtr->left) && (tmPtr->anchorPos.y >= extsPtr->top) &&
                ((tmPtr->anchorPos.x + tmPtr->width) <= extsPtr->right) &&
                ((tmPtr->anchorPos.y + tmPtr->height) <= extsPtr->bottom));
    }
    return !((tmPtr->anchorPos.x >= extsPtr->right) || (tmPtr->anchorPos.y >= extsPtr->bottom) ||
             ((tmPtr->anchorPos.x + tmPtr->width) <= extsPtr->left) ||
             ((tmPtr->anchorPos.y + tmPtr->height) <= extsPtr->top));
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawTextMarker --
 *
 *      Draws the text marker on the graph.
 *
 * Parameters:
 *      Marker *markerPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Commands are output to X to draw the marker in its current
 *      mode.
 *
 * ----------------------------------------------------------------------
 */
static void DrawTextMarker(Marker *markerPtr, Drawable drawable) {
    TextMarker *tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);
    Graph *graphPtr = markerPtr->graphPtr;

    if (tmPtr->string == NULL) {
        return;
    }
    if (tmPtr->fillGC != NULL) {
        XPoint pointArr[4];
        register int i;

        /*
         * Simulate the rotated background of the bitmap by
         * filling a bounding polygon with the background color.
         */
        for (i = 0; i < 4; i++) {
            pointArr[i].x = (short int)(tmPtr->outline[i].x + tmPtr->anchorPos.x);
            pointArr[i].y = (short int)(tmPtr->outline[i].y + tmPtr->anchorPos.y);
        }
        XFillPolygon(graphPtr->display, drawable, tmPtr->fillGC, pointArr, 4, Convex, CoordModeOrigin);
    }
    if (tmPtr->style.color != NULL) {
        Rbc_DrawTextLayout(graphPtr->tkwin, drawable, tmPtr->textPtr, &tmPtr->style, (int)tmPtr->anchorPos.x,
                           (int)tmPtr->anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * TextMarkerToPostScript --
 *
 *      Outputs PostScript commands to draw a text marker at a given
 *      x,y coordinate, rotation, anchor, and font.
 *
 * Parameters:
 *      Marker *markerPtr
 *      PsToken psToken
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      PostScript font and color settings are changed.
 *
 * ----------------------------------------------------------------------
 */
static void TextMarkerToPostScript(Marker *markerPtr, PsToken psToken) {
    TextMarker *tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);

    if (tmPtr->string == NULL) {
        return;
    }
    if (tmPtr->fillGC != NULL) {
        Point2D polygon[4];
        register int i;

        /*
         * Simulate the rotated background of the bitmap by
         * filling a bounding polygon with the background color.
         */
        for (i = 0; i < 4; i++) {
            polygon[i].x = tmPtr->outline[i].x + tmPtr->anchorPos.x;
            polygon[i].y = tmPtr->outline[i].y + tmPtr->anchorPos.y;
        }
        Rbc_BackgroundToPostScript(psToken, tmPtr->fillColor);
        Rbc_PolygonToPostScript(psToken, polygon, 4);
    }
    Rbc_TextToPostScript(psToken, tmPtr->string, &tmPtr->style, tmPtr->anchorPos.x, tmPtr->anchorPos.y);
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeTextMarker --
 *
 *      Destroys the structure containing the attributes of the text
 *      marker.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Text attributes (GCs, colors, stipple, font, etc) get destroyed.
 *      Memory is released, X resources are freed, and the graph is
 *      redrawn.
 *
 * ----------------------------------------------------------------------
 */
static void FreeTextMarker(Graph *graphPtr, Marker *markerPtr) {
    TextMarker *tmPtr;

    tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);
    if (tmPtr->style.gc != NULL) {
        Tk_FreeGC(graphPtr->display, tmPtr->style.gc);
        tmPtr->style.gc = NULL;
    }
    if (tmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, tmPtr->fillGC);
        tmPtr->fillGC = NULL;
    }
    if (tmPtr->style.shadow.color != NULL) {
        Tk_FreeColor(tmPtr->style.shadow.color);
        tmPtr->style.shadow.color = NULL;
        tmPtr->style.shadow.offset = 0;
    }
    if (tmPtr->textPtr != NULL) {
        ckfree((char *)tmPtr->textPtr);
        tmPtr->textPtr = NULL;
    }
    tmPtr->width = 0;
    tmPtr->height = 0;
    memset(tmPtr->outline, 0, sizeof(tmPtr->outline));
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateTextMarker --
 *
 *      Allocate memory and initialize methods for the new text marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the text marker structure.
 *
 * ----------------------------------------------------------------------
 */
static Marker *CreateTextMarker(void) {
    TextMarker *tmPtr;

    tmPtr = RbcCalloc(1, sizeof(TextMarker));
    if (tmPtr != NULL) {
        tmPtr->core.classPtr = &textMarkerClass;
        Rbc_InitTextStyle(&tmPtr->style);

        /*
         * Internal text-layout anchor. The marker's externally
         * configured -anchor is stored separately in tmPtr->anchor.
         */
        tmPtr->style.anchor = TK_ANCHOR_NW;
        tmPtr->style.padLeft = 4;
        tmPtr->style.padRight = 4;
        tmPtr->style.padTop = 4;
        tmPtr->style.padBottom = 4;
    }
    return (tmPtr != NULL) ? &tmPtr->core : NULL;
}

static Tk_GeomMgr winMarkerMgrInfo = {
    "graph",           /* Name of geometry manager used by winfo */
    ChildGeometryProc, /* Procedure to for new geometry requests */
    ChildCustodyProc,  /* Procedure when window is taken away */
};

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureWindowMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a window marker.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      the interpreter result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as window pathname, placement,
 *      etc. get set for markerPtr; old resources get freed, if there
 *      were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureWindowMarker(Marker *markerPtr) {
    Graph *graphPtr;
    WindowMarker *wmPtr;
    ParsedMarkerOptions markerOptions;
    Tk_Window newTkwin;
    int newReqWidth;
    int newReqHeight;

    graphPtr = markerPtr->graphPtr;
    wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);
    newTkwin = NULL;
    newReqWidth = 0;
    newReqHeight = 0;

    /*
     * Parse common marker options transactionally.
     */
    if (ParseMarkerOptions(markerPtr, &markerOptions) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * The zero defaults mean "use requested size", but an explicitly
     * configured zero remains invalid for compatibility.
     */
    if (GetWindowMarkerDimension(markerPtr, wmPtr->reqWidthObjPtr, markerPtr->optionMask & WINDOW_WIDTH_CHANGED,
                                 &newReqWidth) != TCL_OK) {
        goto error;
    }
    if (GetWindowMarkerDimension(markerPtr, wmPtr->reqHeightObjPtr, markerPtr->optionMask & WINDOW_HEIGHT_CHANGED,
                                 &newReqHeight) != TCL_OK) {
        goto error;
    }

    /*
     * Resolve and validate the proposed child before detaching the
     * currently managed child.
     */
    if ((wmPtr->pathName != NULL) && (wmPtr->pathName[0] != '\0')) {
        newTkwin = Tk_NameToWindow(graphPtr->interp, wmPtr->pathName, graphPtr->tkwin);
        if (newTkwin == NULL) {
            goto error;
        }
        if (Tk_Parent(newTkwin) != graphPtr->tkwin) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("\"%s\" is not a child of \"%s\"", wmPtr->pathName,
                                                             Tk_PathName(graphPtr->tkwin)));
            goto error;
        }
    }

    /*
     * No fallible processing remains.
     */
    CommitMarkerOptions(markerPtr, &markerOptions);
    wmPtr->reqWidth = newReqWidth;
    wmPtr->reqHeight = newReqHeight;

    /*
     * Transfer geometry ownership only after all validation succeeds.
     */
    if (newTkwin != wmPtr->tkwin) {
        ReleaseWindowMarkerChild(wmPtr, FALSE);
        if (newTkwin != NULL) {
            /*
             * Install the token before registering callbacks. If the
             * child is destroyed during geometry-manager transfer,
             * ChildEventProc will correctly clear it.
             */
            wmPtr->tkwin = newTkwin;
            Tk_CreateEventHandler(newTkwin, StructureNotifyMask, ChildEventProc, wmPtr);
            Tk_ManageGeometry(newTkwin, &winMarkerMgrInfo, wmPtr);
        }
    }
    markerPtr->flags |= MAP_ITEM;
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;

error:
    FreeParsedMarkerOptions(graphPtr, &markerOptions);
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapWindowMarker --
 *
 *      Calculate the layout position for a window marker.  Positional
 *      information is saved in the marker.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void MapWindowMarker(Marker *markerPtr) {
    WindowMarker *wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);
    Graph *graphPtr = markerPtr->graphPtr;
    Extents2D exts;
    int width, height;

    if (wmPtr->tkwin == (Tk_Window)NULL) {
        return;
    }
    wmPtr->anchorPos = MapPoint(graphPtr, wmPtr->core.worldPts, &wmPtr->core.axes);

    width = Tk_ReqWidth(wmPtr->tkwin);
    height = Tk_ReqHeight(wmPtr->tkwin);
    if (wmPtr->reqWidth > 0) {
        width = wmPtr->reqWidth;
    }
    if (wmPtr->reqHeight > 0) {
        height = wmPtr->reqHeight;
    }
    wmPtr->anchorPos = Rbc_TranslatePoint(&wmPtr->anchorPos, width, height, wmPtr->anchor);
    wmPtr->anchorPos.x += wmPtr->core.xOffset;
    wmPtr->anchorPos.y += wmPtr->core.yOffset;
    wmPtr->width = width;
    wmPtr->height = height;

    /*
     * Determine the bounding box of the window and test to see if it
     * is at least partially contained within the plotting area.
     */
    exts.left = wmPtr->anchorPos.x;
    exts.top = wmPtr->anchorPos.y;
    exts.right = wmPtr->anchorPos.x + wmPtr->width - 1;
    exts.bottom = wmPtr->anchorPos.y + wmPtr->height - 1;
    wmPtr->core.clipped = BoxesDontOverlap(graphPtr, &exts);
}

/*
 *----------------------------------------------------------------------
 *
 * PointInWindowMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Point2D *samplePtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int PointInWindowMarker(Marker *markerPtr, Point2D *samplePtr) {
    WindowMarker *wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);

    return ((samplePtr->x >= wmPtr->anchorPos.x) && (samplePtr->x < (wmPtr->anchorPos.x + wmPtr->width)) &&
            (samplePtr->y >= wmPtr->anchorPos.y) && (samplePtr->y < (wmPtr->anchorPos.y + wmPtr->height)));
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInWindowMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Extents2D *extsPtr
 *      int enclosed
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int RegionInWindowMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    WindowMarker *wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);

    if (wmPtr->core.nWorldPts < 1) {
        return FALSE;
    }
    if (enclosed) {
        return ((wmPtr->anchorPos.x >= extsPtr->left) && (wmPtr->anchorPos.y >= extsPtr->top) &&
                ((wmPtr->anchorPos.x + wmPtr->width) <= extsPtr->right) &&
                ((wmPtr->anchorPos.y + wmPtr->height) <= extsPtr->bottom));
    }
    return !((wmPtr->anchorPos.x >= extsPtr->right) || (wmPtr->anchorPos.y >= extsPtr->bottom) ||
             ((wmPtr->anchorPos.x + wmPtr->width) <= extsPtr->left) ||
             ((wmPtr->anchorPos.y + wmPtr->height) <= extsPtr->top));
}

/*
 *----------------------------------------------------------------------
 *
 * DrawWindowMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
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
static void DrawWindowMarker(Marker *markerPtr, Drawable drawable) {
    WindowMarker *wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);

    if (wmPtr->tkwin == NULL) {
        return;
    }
    if ((wmPtr->height != Tk_Height(wmPtr->tkwin)) || (wmPtr->width != Tk_Width(wmPtr->tkwin)) ||
        ((int)wmPtr->anchorPos.x != Tk_X(wmPtr->tkwin)) || ((int)wmPtr->anchorPos.y != Tk_Y(wmPtr->tkwin))) {
        Tk_MoveResizeWindow(wmPtr->tkwin, (int)wmPtr->anchorPos.x, (int)wmPtr->anchorPos.y, wmPtr->width,
                            wmPtr->height);
    }
    if (!Tk_IsMapped(wmPtr->tkwin)) {
        Tk_MapWindow(wmPtr->tkwin);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * WindowMarkerToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
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
static void WindowMarkerToPostScript(Marker *markerPtr, PsToken psToken) {
    WindowMarker *wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);

    if (wmPtr->tkwin == NULL) {
        return;
    }
    if (Tk_IsMapped(wmPtr->tkwin)) {
        Rbc_WindowToPostScript(psToken, wmPtr->tkwin, wmPtr->anchorPos.x, wmPtr->anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeWindowMarker --
 *
 *      Destroys the structure containing the attributes of the window
 *      marker.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Window is destroyed and removed from the screen.
 *
 * ----------------------------------------------------------------------
 */
static void FreeWindowMarker(Graph *graphPtr, Marker *markerPtr) {
    WindowMarker *wmPtr;

    (void)graphPtr;
    wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);

    /*
     * Preserve the existing marker-deletion behaviour: deleting the
     * marker destroys the currently embedded child window.
     */
    ReleaseWindowMarkerChild(wmPtr, TRUE);
    wmPtr->anchorPos.x = 0.0;
    wmPtr->anchorPos.y = 0.0;
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateWindowMarker --
 *
 *      Allocate memory and initialize methods for the new window marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the window marker structure.
 *
 * ----------------------------------------------------------------------
 */
static Marker *CreateWindowMarker(void) {
    WindowMarker *wmPtr;

    wmPtr = RbcCalloc(1, sizeof(WindowMarker));
    if (wmPtr != NULL) {
        wmPtr->core.classPtr = &windowMarkerClass;
    }
    return (wmPtr != NULL) ? &wmPtr->core : NULL;
}

/*
 * ----------------------------------------------------------------------
 *
 * ChildEventProc --
 *
 *      This procedure is invoked whenever StructureNotify events
 *      occur for a window that's managed as part of a graph window
 *      marker. This procedure's only purpose is to clean up when
 *      windows are deleted.
 *
 * Parameters:
 *      ClientData clientData - Pointer to record describing window item.
 *      XEvent *eventPtr - Describes what just happened.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The window is disassociated from the window item when it is
 *      deleted.
 *
 * ----------------------------------------------------------------------
 */
static void ChildEventProc(ClientData clientData, XEvent *eventPtr) {
    WindowMarker *wmPtr;

    wmPtr = clientData;
    if (eventPtr->type == DestroyNotify) {
        wmPtr->tkwin = NULL;
        wmPtr->width = 0;
        wmPtr->height = 0;
        wmPtr->core.flags |= MAP_ITEM;
        if (wmPtr->core.graphPtr->tkwin != NULL) {
            Rbc_EventuallyRedrawGraph(wmPtr->core.graphPtr);
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ChildGeometryProc --
 *
 *      This procedure is invoked whenever a window that's associated
 *      with a window item changes its requested dimensions.
 *
 * Parameters:
 *      ClientData clientData - Pointer to record for window item.
 *      Tk_Window tkwin - Window that changed its desired size.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The size and location on the window of the window may change,
 *      depending on the options specified for the window item.
 *
 * ----------------------------------------------------------------------
 */
static void ChildGeometryProc(ClientData clientData, Tk_Window tkwin) {
    WindowMarker *wmPtr = clientData;

    if (wmPtr->reqWidth == 0) {
        wmPtr->width = Tk_ReqWidth(tkwin);
    }
    if (wmPtr->reqHeight == 0) {
        wmPtr->height = Tk_ReqHeight(tkwin);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ChildCustodyProc --
 *
 *      This procedure is invoked when an embedded window has been
 *      stolen by another geometry manager.  The information and
 *      memory associated with the widget is released.
 *
 * Parameters:
 *      ClientData clientData - Window marker to be destroyed.
 *      Tk_Window tkwin - Not used.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Arranges for the graph to be redrawn without the embedded
 *      widget at the next idle point.
 *
 * ----------------------------------------------------------------------
 */
static void ChildCustodyProc(ClientData clientData, Tk_Window tkwin) {
    WindowMarker *wmPtr;
    Marker *markerPtr;
    Graph *graphPtr;

    wmPtr = clientData;
    markerPtr = &wmPtr->core;
    graphPtr = markerPtr->graphPtr;

    /*
     * Another geometry manager has taken ownership. Remove our event
     * handler before freeing the marker, but leave the child itself
     * alive for its new manager.
     */
    Tk_DeleteEventHandler(tkwin, StructureNotifyMask, ChildEventProc, wmPtr);
    if (wmPtr->tkwin == tkwin) {
        wmPtr->tkwin = NULL;
    }
    DestroyMarker(markerPtr);
    Rbc_EventuallyRedrawGraph(graphPtr);
}

/*
 * ----------------------------------------------------------------------
 *
 * MapLineMarker --
 *
 *      Calculate the layout position for a line marker.  Positional
 *      information is saved in the marker.  The line positions are
 *      stored in an array of points (malloc'ed).
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void MapLineMarker(Marker *markerPtr) {
    Graph *graphPtr;
    LineMarker *lmPtr;
    Point2D *srcPtr;
    Point2D *endPtr;
    Segment2D *segments;
    Segment2D *segPtr;
    Point2D p, next;
    Extents2D exts;
    Tcl_Size capacity;

    graphPtr = markerPtr->graphPtr;
    lmPtr = LINE_MARKER_FROM_CORE(markerPtr);
    lmPtr->nSegments = 0;
    lmPtr->core.clipped = TRUE;
    if (lmPtr->segments != NULL) {
        ckfree(lmPtr->segments);
        lmPtr->segments = NULL;
    }
    if (lmPtr->core.nWorldPts < 2) {
        return;
    }
    /*
     * One source edge can produce at most one clipped segment.
     */
    capacity = lmPtr->core.nWorldPts - 1;
    if ((size_t)capacity > SIZE_MAX / sizeof(*segments)) {
        return;
    }
    segments = Tcl_AttemptAlloc((size_t)capacity * sizeof(*segments));
    if (segments == NULL) {
        return;
    }
    Rbc_GraphExtents(graphPtr, &exts);
    srcPtr = lmPtr->core.worldPts;
    p = MapPoint(graphPtr, srcPtr, &lmPtr->core.axes);
    p.x += lmPtr->core.xOffset;
    p.y += lmPtr->core.yOffset;
    segPtr = segments;
    for (srcPtr++, endPtr = lmPtr->core.worldPts + lmPtr->core.nWorldPts; srcPtr < endPtr; srcPtr++) {
        next = MapPoint(graphPtr, srcPtr, &lmPtr->core.axes);
        next.x += lmPtr->core.xOffset;
        next.y += lmPtr->core.yOffset;
        if (Rbc_LineRectClip(&exts, &p, &next, segPtr)) {
            segPtr++;
        }
        p = next;
    }
    lmPtr->nSegments = (Tcl_Size)(segPtr - segments);
    if (lmPtr->nSegments == 0) {
        ckfree(segments);
        return;
    }
    lmPtr->segments = segments;
    lmPtr->core.clipped = FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * PointInLineMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Point2D *samplePtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int PointInLineMarker(Marker *markerPtr, Point2D *samplePtr) {
    LineMarker *lmPtr = LINE_MARKER_FROM_CORE(markerPtr);

    return Rbc_PointInSegments(samplePtr, lmPtr->segments, lmPtr->nSegments, (double)lmPtr->core.graphPtr->halo);
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInLineMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Extents2D *extsPtr
 *      int enclosed
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int RegionInLineMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    LineMarker *lmPtr;
    Point2D *pointPtr;
    Point2D *endPtr;

    lmPtr = LINE_MARKER_FROM_CORE(markerPtr);
    if (lmPtr->core.nWorldPts < 2) {
        return FALSE;
    }
    if (enclosed) {
        Point2D p;

        for (pointPtr = lmPtr->core.worldPts, endPtr = lmPtr->core.worldPts + lmPtr->core.nWorldPts; pointPtr < endPtr;
             pointPtr++) {
            p = MapPoint(lmPtr->core.graphPtr, pointPtr, &lmPtr->core.axes);
            p.x += lmPtr->core.xOffset;
            p.y += lmPtr->core.yOffset;
            if ((p.x < extsPtr->left) || (p.x > extsPtr->right) || (p.y < extsPtr->top) || (p.y > extsPtr->bottom)) {
                return FALSE;
            }
        }
        return TRUE;
    } else {
        Point2D p;
        Point2D q;

        endPtr = lmPtr->core.worldPts + lmPtr->core.nWorldPts - 1;
        for (pointPtr = lmPtr->core.worldPts; pointPtr < endPtr; pointPtr++) {
            p = MapPoint(lmPtr->core.graphPtr, pointPtr, &lmPtr->core.axes);
            q = MapPoint(lmPtr->core.graphPtr, pointPtr + 1, &lmPtr->core.axes);
            p.x += lmPtr->core.xOffset;
            p.y += lmPtr->core.yOffset;
            q.x += lmPtr->core.xOffset;
            q.y += lmPtr->core.yOffset;
            if (Rbc_LineRectClip(extsPtr, &p, &q, NULL)) {
                return TRUE;
            }
        }
        return FALSE;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawLineMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
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
static void DrawLineMarker(Marker *markerPtr, Drawable drawable) {
    LineMarker *lmPtr = LINE_MARKER_FROM_CORE(markerPtr);

    if (lmPtr->nSegments > 0) {
        Graph *graphPtr = markerPtr->graphPtr;

        Rbc_Draw2DSegments(graphPtr->display, drawable, lmPtr->gc, lmPtr->segments, lmPtr->nSegments);
        if (lmPtr->xor) { /* Toggle the drawing state */
            lmPtr->xorState = (lmPtr->xorState == 0);
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureLineMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a line marker.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      the interpreter result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as line width, colors, dashes,
 *      etc. get set for markerPtr; old resources get freed, if there
 *      were any.  The marker is eventually redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureLineMarker(Marker *markerPtr) {
    Graph *graphPtr;
    LineMarker *lmPtr;
    ParsedMarkerOptions markerOptions;
    Rbc_Dashes newDashes;
    int newCapStyle;
    int newJoinStyle;
    int newLineWidth;
    int newDashOffset;
    XGCValues gcValues;
    unsigned long gcMask;
    GC newGC;
    Drawable drawable;

    graphPtr = markerPtr->graphPtr;
    lmPtr = LINE_MARKER_FROM_CORE(markerPtr);
    newGC = NULL;

    /*
     * Parse all common manually converted marker options without
     * modifying the currently active marker.
     *
     * This validates and acquires:
     *
     *      -bindtags
     *      -coords
     *      -mapx
     *      -mapy
     *      -state
     */
    if (ParseMarkerOptions(markerPtr, &markerOptions) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Parse all line-marker-specific values without modifying the
     * currently active derived state.
     */
    if (Tk_GetCapStyle(graphPtr->interp, Tcl_GetString(lmPtr->capObjPtr), &newCapStyle) != TCL_OK) {
        goto error;
    }
    if (Tk_GetJoinStyle(graphPtr->interp, Tcl_GetString(lmPtr->joinObjPtr), &newJoinStyle) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetDashesFromObj(graphPtr->interp, lmPtr->dashesObjPtr, &newDashes) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, lmPtr->dashOffsetObjPtr, PIXELS_NONNEGATIVE,
                             &newDashOffset) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, lmPtr->lineWidthObjPtr, PIXELS_NONNEGATIVE,
                             &newLineWidth) != TCL_OK) {
        goto error;
    }
    newDashes.offset = newDashOffset;

    /*
     * Construct the replacement GC. At this point all fallible option
     * parsing has succeeded.
     */
    memset(&gcValues, 0, sizeof(gcValues));
    gcMask = GCLineWidth | GCLineStyle | GCCapStyle | GCJoinStyle;
    if (lmPtr->outlineColor != NULL) {
        gcValues.foreground = lmPtr->outlineColor->pixel;
        gcMask |= GCForeground;
    }
    if (lmPtr->fillColor != NULL) {
        gcValues.background = lmPtr->fillColor->pixel;
        gcMask |= GCBackground;
    }
    gcValues.cap_style = newCapStyle;
    gcValues.join_style = newJoinStyle;
    gcValues.line_width = LineWidth(newLineWidth);
    gcValues.line_style = LineSolid;
    if (LineIsDashed(newDashes)) {
        gcValues.line_style = (gcMask & GCBackground) ? LineDoubleDash : LineOnOffDash;
    }
    if (lmPtr->xor) {
        unsigned long pixel;
        gcValues.function = GXxor;
        gcMask |= GCFunction;
        if (graphPtr->plotBg == NULL) {
            pixel = WhitePixelOfScreen(Tk_Screen(graphPtr->tkwin));
        } else {
            pixel = graphPtr->plotBg->pixel;
        }
        if (gcMask & GCBackground) {
            gcValues.background ^= pixel;
        }
        if (gcMask & GCForeground) {
            gcValues.foreground ^= pixel;
        }
    }
    newGC = Rbc_GetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (LineIsDashed(newDashes)) {
        Rbc_SetDashes(graphPtr->display, newGC, &newDashes);
    }
    drawable = Tk_WindowId(graphPtr->tkwin);

    /*
     * Remove the old XOR drawing before changing its GC or geometry.
     *
     * Do not call DrawLineMarker() here. Tk_SetOptions() has already
     * installed the newly requested -xor value, whereas xorState says
     * whether an old XOR image is currently present in the window.
     */
    if (lmPtr->xorState && (drawable != None) && (lmPtr->gc != NULL) && (lmPtr->nSegments > 0)) {
        Rbc_Draw2DSegments(graphPtr->display, drawable, lmPtr->gc, lmPtr->segments, lmPtr->nSegments);
        lmPtr->xorState = FALSE;
    }

    /*
     * Commit the common marker options. This transfers ownership of
     * tags, coordinates, and axis references to markerPtr.
     */
    CommitMarkerOptions(markerPtr, &markerOptions);

    /*
     * Commit line-marker-specific derived values.
     */
    lmPtr->capStyle = newCapStyle;
    lmPtr->joinStyle = newJoinStyle;
    lmPtr->lineWidth = newLineWidth;
    lmPtr->dashes = newDashes;
    if (lmPtr->gc != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, lmPtr->gc);
    }
    lmPtr->gc = newGC;
    newGC = NULL;
    markerPtr->flags |= MAP_ITEM;

    /*
     * XOR markers are mapped and drawn immediately. DrawLineMarker()
     * updates xorState after drawing.
     */
    if (lmPtr->xor &&(drawable != None)) {
        MapLineMarker(markerPtr);
        DrawLineMarker(markerPtr, drawable);
        return TCL_OK;
    }

    /*
     * Non-XOR markers are rendered through the normal graph redraw
     * path.
     */
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;

error:
    if (newGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, newGC);
    }
    FreeParsedMarkerOptions(graphPtr, &markerOptions);
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * LineMarkerToPostScript --
 *
 *      Prints postscript commands to display the connect line.
 *      Dashed lines need to be handled specially, especially if a
 *      background color is designated.
 *
 * Parameters:
 *      Marker *markerPtr
 *      PsToken psToken
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      PostScript output commands are saved in the interpreter
 *      (infoPtr->interp) result field.
 *
 * ----------------------------------------------------------------------
 */
static void LineMarkerToPostScript(Marker *markerPtr, PsToken psToken) {
    LineMarker *lmPtr = LINE_MARKER_FROM_CORE(markerPtr);

    if (lmPtr->nSegments > 0) {
        Rbc_LineAttributesToPostScript(psToken, lmPtr->outlineColor, lmPtr->lineWidth, &lmPtr->dashes, lmPtr->capStyle,
                                       lmPtr->joinStyle);
        if ((LineIsDashed(lmPtr->dashes)) && (lmPtr->fillColor != NULL)) {
            Rbc_AppendToPostScript(psToken, "/DashesProc {\n  gsave\n    ", (char *)NULL);
            Rbc_BackgroundToPostScript(psToken, lmPtr->fillColor);
            Rbc_AppendToPostScript(psToken, "    ", (char *)NULL);
            Rbc_LineDashesToPostScript(psToken, (Rbc_Dashes *)NULL);
            Rbc_AppendToPostScript(psToken, "stroke\n", "  grestore\n", "} def\n", (char *)NULL);
        } else {
            Rbc_AppendToPostScript(psToken, "/DashesProc {} def\n", (char *)NULL);
        }
        Rbc_2DSegmentsToPostScript(psToken, lmPtr->segments, lmPtr->nSegments);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * FreeLineMarker --
 *
 *      Destroys the structure and attributes of a line marker.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Line attributes (GCs, colors, stipple, etc) get released.
 *      Memory is deallocated, X resources are freed.
 *
 * ----------------------------------------------------------------------
 */
static void FreeLineMarker(Graph *graphPtr, Marker *markerPtr) {
    LineMarker *lmPtr;
    Drawable drawable;

    lmPtr = LINE_MARKER_FROM_CORE(markerPtr);

    /*
     * Remove an immediately drawn XOR marker before releasing its GC.
     */
    if (lmPtr->xorState && (graphPtr->tkwin != NULL) && (lmPtr->gc != NULL) && (lmPtr->nSegments > 0)) {
        drawable = Tk_WindowId(graphPtr->tkwin);

        if (drawable != None) {
            Rbc_Draw2DSegments(graphPtr->display, drawable, lmPtr->gc, lmPtr->segments, lmPtr->nSegments);
        }

        lmPtr->xorState = FALSE;
    }

    if (lmPtr->gc != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, lmPtr->gc);

        lmPtr->gc = NULL;
    }

    if (lmPtr->segments != NULL) {
        ckfree((char *)lmPtr->segments);

        lmPtr->segments = NULL;
        lmPtr->nSegments = 0;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateLineMarker --
 *
 *      Allocate memory and initialize methods for a new line marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the line marker structure.
 *
 * ----------------------------------------------------------------------
 */
static Marker *CreateLineMarker() {
    LineMarker *lmPtr;

    lmPtr = RbcCalloc(1, sizeof(LineMarker));
    if (lmPtr != NULL) {
        lmPtr->core.classPtr = &lineMarkerClass;
        lmPtr->xor = FALSE;
        lmPtr->capStyle = CapButt;
        lmPtr->joinStyle = JoinMiter;
    }
    return &lmPtr->core;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapPolygonMarker --
 *
 *      Calculate the layout position for a polygon marker.  Positional
 *      information is saved in the polygon in an array of points
 *      (malloc'ed).
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void MapPolygonMarker(Marker *markerPtr) {
    Graph *graphPtr;
    PolygonMarker *pmPtr;
    Point2D *srcPtr;
    Point2D *destPtr;
    Point2D *endPtr;
    Point2D *screenPts;
    Extents2D exts;
    Tcl_Size nVertices;

    graphPtr = markerPtr->graphPtr;
    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if (pmPtr->outlinePts != NULL) {
        ckfree(pmPtr->outlinePts);
        pmPtr->outlinePts = NULL;
    }
    pmPtr->nOutlinePts = 0;
    if (pmPtr->fillPts != NULL) {
        ckfree(pmPtr->fillPts);
        pmPtr->fillPts = NULL;
    }
    pmPtr->nFillPts = 0;
    if (pmPtr->screenPts != NULL) {
        ckfree(pmPtr->screenPts);
        pmPtr->screenPts = NULL;
    }
    pmPtr->core.clipped = TRUE;
    nVertices = pmPtr->core.nWorldPts;
    if (nVertices < 3) {
        return;
    }
    if ((size_t)nVertices > SIZE_MAX / sizeof(*screenPts)) {
        return;
    }
    screenPts = Tcl_AttemptAlloc((size_t)nVertices * sizeof(*screenPts));
    if (screenPts == NULL) {
        return;
    }
    destPtr = screenPts;
    endPtr = pmPtr->core.worldPts + nVertices;
    for (srcPtr = pmPtr->core.worldPts; srcPtr < endPtr; srcPtr++, destPtr++) {
        *destPtr = MapPoint(graphPtr, srcPtr, &pmPtr->core.axes);
        destPtr->x += pmPtr->core.xOffset;
        destPtr->y += pmPtr->core.yOffset;
    }
    pmPtr->screenPts = screenPts;
    Rbc_GraphExtents(graphPtr, &exts);
    if (pmPtr->fill.fgColor != NULL) {
        Point2D *fillPts;
        Tcl_Size fillCapacity;
        Tcl_Size nFillPts;

        if (nVertices <= (TCL_SIZE_MAX - 1) / 3) {
            fillCapacity = nVertices * 3 + 1;
            if ((size_t)fillCapacity <= SIZE_MAX / sizeof(*fillPts)) {
                fillPts = Tcl_AttemptAlloc((size_t)fillCapacity * sizeof(*fillPts));
                if (fillPts != NULL) {
                    nFillPts = Rbc_PolyRectClip(&exts, screenPts, nVertices, fillPts, fillCapacity);
                    if (nFillPts >= 3) {
                        pmPtr->fillPts = fillPts;
                        pmPtr->nFillPts = nFillPts;
                        pmPtr->core.clipped = FALSE;
                    } else {
                        ckfree(fillPts);
                    }
                }
            }
        }
    }
    if ((pmPtr->outline.fgColor != NULL) && (pmPtr->lineWidth > 0)) {
        Segment2D *outlinePts;
        Segment2D *segPtr;

        if ((size_t)nVertices <= SIZE_MAX / sizeof(*outlinePts)) {
            outlinePts = Tcl_AttemptAlloc((size_t)nVertices * sizeof(*outlinePts));
            if (outlinePts != NULL) {
                segPtr = outlinePts;
                endPtr = screenPts + nVertices;
                for (srcPtr = screenPts; srcPtr < endPtr; srcPtr++) {
                    Point2D *nextPtr;

                    nextPtr = (srcPtr + 1 < endPtr) ? srcPtr + 1 : screenPts;
                    if (Rbc_LineRectClip(&exts, srcPtr, nextPtr, segPtr)) {
                        segPtr++;
                    }
                }
                pmPtr->nOutlinePts = (Tcl_Size)(segPtr - outlinePts);
                if (pmPtr->nOutlinePts > 0) {
                    pmPtr->outlinePts = outlinePts;
                    pmPtr->core.clipped = FALSE;
                } else {
                    ckfree(outlinePts);
                }
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PointInPolygonMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Point2D *samplePtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int PointInPolygonMarker(Marker *markerPtr, Point2D *samplePtr) {
    PolygonMarker *pmPtr;

    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if ((pmPtr->screenPts == NULL) || (pmPtr->core.nWorldPts < 3)) {
        return FALSE;
    }
    return Rbc_PointInPolygon(samplePtr, pmPtr->screenPts, pmPtr->core.nWorldPts);
}

/*
 *----------------------------------------------------------------------
 *
 * RegionInPolygonMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
 *      Extents2D *extsPtr
 *      int enclosed
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int RegionInPolygonMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    PolygonMarker *pmPtr;

    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if ((pmPtr->screenPts == NULL) || (pmPtr->core.nWorldPts < 3)) {
        return FALSE;
    }
    return Rbc_RegionInPolygon(extsPtr, pmPtr->screenPts, pmPtr->core.nWorldPts, enclosed);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawPolygonMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
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

static int GetMarkerPolygonPointCount(Display *display, Tcl_Size nPoints) {
    if ((nPoints < 3) || (nPoints > (Tcl_Size)INT_MAX)) {
        return 0;
    }
#ifndef WIN32
    {
        int maxPoints;

        maxPoints = Rbc_MaxRequestSize(display, sizeof(XPoint));
        if ((maxPoints < 3) || (nPoints > (Tcl_Size)maxPoints)) {
            return 0;
        }
    }
#else
    (void)display;
#endif
    return (int)nPoints;
}
static void DrawPolygonMarker(Marker *markerPtr, Drawable drawable) {
    Graph *graphPtr;
    PolygonMarker *pmPtr;

    graphPtr = markerPtr->graphPtr;
    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    /*
     * Draw the polygon fill.  XFillPolygon requires an int count and
     * the complete polygon must fit in one native request.
     */
    if ((pmPtr->nFillPts >= 3) && (pmPtr->fill.fgColor != NULL)) {
        XPoint *pointArr;
        Tcl_Size i;
        int nPoints;

        nPoints = GetMarkerPolygonPointCount(graphPtr->display, pmPtr->nFillPts);
        if (nPoints > 0) {
            pointArr = Tcl_AttemptAlloc((size_t)nPoints * sizeof(*pointArr));
            if (pointArr != NULL) {
                for (i = 0; i < pmPtr->nFillPts; i++) {
                    pointArr[i].x = (short int)pmPtr->fillPts[i].x;
                    pointArr[i].y = (short int)pmPtr->fillPts[i].y;
                }
                XFillPolygon(graphPtr->display, drawable, pmPtr->fillGC, pointArr, nPoints, Complex, CoordModeOrigin);
                ckfree(pointArr);
            }
        }
    }
    /*
     * Outline segments can be safely chunked by
     * Rbc_Draw2DSegments().
     */
    if ((pmPtr->nOutlinePts > 0) && (pmPtr->lineWidth > 0) && (pmPtr->outline.fgColor != NULL)) {
        Rbc_Draw2DSegments(graphPtr->display, drawable, pmPtr->outlineGC, pmPtr->outlinePts, pmPtr->nOutlinePts);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PolygonMarkerToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Marker *markerPtr
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
static void PolygonMarkerToPostScript(Marker *markerPtr, PsToken psToken) {
    Graph *graphPtr = markerPtr->graphPtr;
    PolygonMarker *pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);

    if ((pmPtr->nFillPts >= 3) && (pmPtr->fill.fgColor != NULL)) {
        /*
         * Options:  fg bg
         *            Draw outline only.
         *         x          Draw solid or stipple.
         *         x  x       Draw solid or stipple.
         */
        /* Create a path to use for both the polygon and its outline. */
        Rbc_PathToPostScript(psToken, pmPtr->fillPts, pmPtr->nFillPts);
        Rbc_AppendToPostScript(psToken, "closepath\n", (char *)NULL);

        /* If the background fill color was specified, draw the
         * polygon in a solid fashion with that color.  */
        if (pmPtr->fill.bgColor != NULL) {
            Rbc_BackgroundToPostScript(psToken, pmPtr->fill.bgColor);
            Rbc_AppendToPostScript(psToken, "Fill\n", (char *)NULL);
        }
        Rbc_ForegroundToPostScript(psToken, pmPtr->fill.fgColor);
        if (pmPtr->stipple != None) {
            /* Draw the stipple in the foreground color. */
            Rbc_StippleToPostScript(psToken, graphPtr->display, pmPtr->stipple);
        } else {
            Rbc_AppendToPostScript(psToken, "Fill\n", (char *)NULL);
        }
    }

    /* Draw the outline in the foreground color.  */
    if ((pmPtr->nOutlinePts > 0) && (pmPtr->lineWidth > 0) && (pmPtr->outline.fgColor != NULL)) {

        /*  Set up the line attributes.  */
        Rbc_LineAttributesToPostScript(psToken, pmPtr->outline.fgColor, pmPtr->lineWidth, &pmPtr->dashes,
                                       pmPtr->capStyle, pmPtr->joinStyle);

        /*
         * Define on-the-fly a PostScript macro "DashesProc" that
         * will be executed for each call to the Polygon drawing
         * routine.  If the line isn't dashed, simply make this an
         * empty definition.
         */
        if ((pmPtr->outline.bgColor != NULL) && (LineIsDashed(pmPtr->dashes))) {
            Rbc_AppendToPostScript(psToken, "/DashesProc {\n", "gsave\n    ", (char *)NULL);
            Rbc_BackgroundToPostScript(psToken, pmPtr->outline.bgColor);
            Rbc_AppendToPostScript(psToken, "    ", (char *)NULL);
            Rbc_LineDashesToPostScript(psToken, (Rbc_Dashes *)NULL);
            Rbc_AppendToPostScript(psToken, "stroke\n", "  grestore\n", "} def\n", (char *)NULL);
        } else {
            Rbc_AppendToPostScript(psToken, "/DashesProc {} def\n", (char *)NULL);
        }
        Rbc_2DSegmentsToPostScript(psToken, pmPtr->outlinePts, pmPtr->nOutlinePts);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigurePolygonMarker --
 *
 *      This procedure is called to process an argv/argc list, plus
 *      the Tk option database, in order to configure (or reconfigure)
 *      a polygon marker.
 *
 * Parameters:
 *      Marker *markerPtr
 *
 * Results:
 *      A standard Tcl result.  If TCL_ERROR is returned, then
 *      the interpreter result contains an error message.
 *
 * Side effects:
 *      Configuration information, such as polygon color, dashes,
 *      fillstyle, etc. get set for markerPtr; old resources get
 *      freed, if there were any.  The marker is eventually
 *      redisplayed.
 *
 * ----------------------------------------------------------------------
 */
static int ConfigurePolygonMarker(Marker *markerPtr) {
    Graph *graphPtr;
    PolygonMarker *pmPtr;
    ParsedMarkerOptions markerOptions;
    ColorPair newOutline;
    ColorPair newFill;
    Rbc_Dashes newDashes;
    int newCapStyle;
    int newJoinStyle;
    int newLineWidth;
    XGCValues gcValues;
    unsigned long outlineMask;
    unsigned long fillMask;
    GC newOutlineGC;
    GC newFillGC;
    Drawable drawable;
    int outlineOnly;

    graphPtr = markerPtr->graphPtr;
    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    memset(&newOutline, 0, sizeof(newOutline));
    memset(&newFill, 0, sizeof(newFill));
    newOutlineGC = NULL;
    newFillGC = NULL;

    /*
     * Parse all common marker options without modifying the active
     * marker.
     */
    if (ParseMarkerOptions(markerPtr, &markerOptions) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Parse all polygon-specific manually converted values.
     */
    if (Tk_GetCapStyle(graphPtr->interp, Tcl_GetString(pmPtr->capObjPtr), &newCapStyle) != TCL_OK) {
        goto error;
    }
    if (Tk_GetJoinStyle(graphPtr->interp, Tcl_GetString(pmPtr->joinObjPtr), &newJoinStyle) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetDashesFromObj(graphPtr->interp, pmPtr->dashesObjPtr, &newDashes) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, pmPtr->lineWidthObjPtr, PIXELS_NONNEGATIVE,
                             &newLineWidth) != TCL_OK) {
        goto error;
    }
    if (GetColorPairFromObj(graphPtr->interp, graphPtr->tkwin, pmPtr->outlineObjPtr, FALSE, &newOutline) != TCL_OK) {
        goto error;
    }
    if (GetColorPairFromObj(graphPtr->interp, graphPtr->tkwin, pmPtr->fillObjPtr, FALSE, &newFill) != TCL_OK) {
        goto error;
    }

    /*
     * Construct the replacement outline GC.
     */
    memset(&gcValues, 0, sizeof(gcValues));
    outlineMask = GCLineWidth | GCLineStyle | GCCapStyle | GCJoinStyle;
    gcValues.line_width = LineWidth(newLineWidth);
    gcValues.line_style = LineSolid;
    gcValues.cap_style = newCapStyle;
    gcValues.join_style = newJoinStyle;
    if (newOutline.fgColor != NULL) {
        gcValues.foreground = newOutline.fgColor->pixel;
        outlineMask |= GCForeground;
    }
    if (newOutline.bgColor != NULL) {
        gcValues.background = newOutline.bgColor->pixel;
        outlineMask |= GCBackground;
    }
    if (LineIsDashed(newDashes)) {
        gcValues.line_style = (outlineMask & GCBackground) ? LineDoubleDash : LineOnOffDash;
    }
    if (pmPtr->xor) {
        unsigned long pixel;
        gcValues.function = GXxor;
        outlineMask |= GCFunction;
        if (graphPtr->plotBg == NULL) {
            pixel = WhitePixelOfScreen(Tk_Screen(graphPtr->tkwin));
        } else {
            pixel = graphPtr->plotBg->pixel;
        }
        if (outlineMask & GCForeground) {
            gcValues.foreground ^= pixel;
        }
        if (outlineMask & GCBackground) {
            gcValues.background ^= pixel;
        }
    }
    newOutlineGC = Rbc_GetPrivateGC(graphPtr->tkwin, outlineMask, &gcValues);
    if (LineIsDashed(newDashes)) {
        Rbc_SetDashes(graphPtr->display, newOutlineGC, &newDashes);
    }
    /*
     * Construct the replacement fill GC.
     */
    memset(&gcValues, 0, sizeof(gcValues));
    fillMask = 0;
    if (newFill.fgColor != NULL) {
        gcValues.foreground = newFill.fgColor->pixel;
        fillMask |= GCForeground;
    }
    if (newFill.bgColor != NULL) {
        gcValues.background = newFill.bgColor->pixel;
        fillMask |= GCBackground;
    }
    if (pmPtr->stipple != None) {
        gcValues.stipple = pmPtr->stipple;
        gcValues.fill_style = FillStippled;
        if (newFill.bgColor != NULL) {
            gcValues.fill_style = FillOpaqueStippled;
        }
        fillMask |= GCStipple | GCFillStyle;
    }
    newFillGC = Tk_GetGC(graphPtr->tkwin, fillMask, &gcValues);
    outlineOnly = (fillMask == 0);
    drawable = Tk_WindowId(graphPtr->tkwin);

    /*
     * Erase an old immediately drawn XOR polygon before replacing its
     * GC or mapped outline.
     */
    if (pmPtr->xorState && (drawable != None) && (pmPtr->outlineGC != NULL) && (pmPtr->outlinePts != NULL) &&
        (pmPtr->nOutlinePts > 0)) {
        Rbc_Draw2DSegments(graphPtr->display, drawable, pmPtr->outlineGC, pmPtr->outlinePts, pmPtr->nOutlinePts);
        pmPtr->xorState = FALSE;
    }

    /*
     * No fallible operation remains. Commit the common marker
     * resources.
     */
    CommitMarkerOptions(markerPtr, &markerOptions);

    /*
     * Replace manually managed colour pairs.
     */
    Rbc_FreeColorPair(&pmPtr->outline);
    pmPtr->outline = newOutline;
    memset(&newOutline, 0, sizeof(newOutline));
    Rbc_FreeColorPair(&pmPtr->fill);
    pmPtr->fill = newFill;
    memset(&newFill, 0, sizeof(newFill));

    /*
     * Commit scalar derived values.
     */
    pmPtr->capStyle = newCapStyle;
    pmPtr->joinStyle = newJoinStyle;
    pmPtr->lineWidth = newLineWidth;
    pmPtr->dashes = newDashes;

    /*
     * Replace GCs.
     */
    if (pmPtr->outlineGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, pmPtr->outlineGC);
    }
    pmPtr->outlineGC = newOutlineGC;
    newOutlineGC = NULL;
    if (pmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, pmPtr->fillGC);
    }
    pmPtr->fillGC = newFillGC;
    newFillGC = NULL;
    markerPtr->flags |= MAP_ITEM;

    /*
     * Preserve the old immediate-XOR behaviour: it applies only when
     * no fill GC attributes were requested.
     */
    if (pmPtr->xor &&outlineOnly && !(graphPtr->flags & RESET_AXES) && (drawable != None)) {
        MapPolygonMarker(markerPtr);
        if ((pmPtr->outlineGC != NULL) && (pmPtr->outlinePts != NULL) && (pmPtr->nOutlinePts > 0)) {
            Rbc_Draw2DSegments(graphPtr->display, drawable, pmPtr->outlineGC, pmPtr->outlinePts, pmPtr->nOutlinePts);
            pmPtr->xorState = TRUE;
        }
        return TCL_OK;
    }
    pmPtr->xorState = FALSE;
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;

error:
    if (newOutlineGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, newOutlineGC);
    }
    if (newFillGC != NULL) {
        Tk_FreeGC(graphPtr->display, newFillGC);
    }
    Rbc_FreeColorPair(&newOutline);
    Rbc_FreeColorPair(&newFill);
    FreeParsedMarkerOptions(graphPtr, &markerOptions);
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * FreePolygonMarker --
 *
 *      Release memory and resources allocated for the polygon element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Marker *markerPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the polygon element is freed up.
 *
 * ----------------------------------------------------------------------
 */
static void FreePolygonMarker(Graph *graphPtr, Marker *markerPtr) {
    PolygonMarker *pmPtr;
    Drawable drawable;

    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);

    /*
     * Remove an immediately drawn XOR outline.
     */
    if (pmPtr->xorState && (graphPtr->tkwin != NULL) && (pmPtr->outlineGC != NULL) && (pmPtr->outlinePts != NULL) &&
        (pmPtr->nOutlinePts > 0)) {
        drawable = Tk_WindowId(graphPtr->tkwin);
        if (drawable != None) {
            Rbc_Draw2DSegments(graphPtr->display, drawable, pmPtr->outlineGC, pmPtr->outlinePts, pmPtr->nOutlinePts);
        }
        pmPtr->xorState = FALSE;
    }
    if (pmPtr->outlineGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, pmPtr->outlineGC);
        pmPtr->outlineGC = NULL;
    }
    if (pmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, pmPtr->fillGC);
        pmPtr->fillGC = NULL;
    }
    if (pmPtr->screenPts != NULL) {
        ckfree((char *)pmPtr->screenPts);
        pmPtr->screenPts = NULL;
    }
    if (pmPtr->fillPts != NULL) {
        ckfree((char *)pmPtr->fillPts);
        pmPtr->fillPts = NULL;
        pmPtr->nFillPts = 0;
    }
    if (pmPtr->outlinePts != NULL) {
        ckfree((char *)pmPtr->outlinePts);
        pmPtr->outlinePts = NULL;
        pmPtr->nOutlinePts = 0;
    }
    Rbc_FreeColorPair(&pmPtr->outline);
    Rbc_FreeColorPair(&pmPtr->fill);
}

/*
 * ----------------------------------------------------------------------
 *
 * CreatePolygonMarker --
 *
 *      Allocate memory and initialize methods for the new polygon
 *      marker.
 *
 * Results:
 *      The pointer to the newly allocated marker structure is
 *      returned.
 *
 * Side effects:
 *      Memory is allocated for the polygon marker structure.
 *
 * ----------------------------------------------------------------------
 */
static Marker *CreatePolygonMarker(void) {
    PolygonMarker *pmPtr;

    pmPtr = RbcCalloc(1, sizeof(PolygonMarker));
    if (pmPtr != NULL) {
        pmPtr->core.classPtr = &polygonMarkerClass;
        pmPtr->capStyle = CapButt;
        pmPtr->joinStyle = JoinMiter;
    }
    return (pmPtr != NULL) ? &pmPtr->core : NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * NameToMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      const char *name
 *      Marker **markerPtrPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int NameToMarker(Graph *graphPtr, const char *name, Marker **markerPtrPtr) {
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->markers.table, name);
    if (hPtr != NULL) {
        *markerPtrPtr = (Marker *)Tcl_GetHashValue(hPtr);
        return TCL_OK;
    }
    Tcl_SetObjResult(graphPtr->interp,
                     Tcl_ObjPrintf("can't find marker \"%s\" in \"%s\"", name, Tk_PathName(graphPtr->tkwin)));
    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * NamesOp --
 *
 *      Returns a list of marker identifiers in the interpreter result;
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
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int NamesOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;
    Rbc_ChainLink *linkPtr;
    Tcl_Size i;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    Tcl_ResetResult(interp);
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->markers.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        markerPtr = Rbc_ChainGetValue(linkPtr);
        if (objc == 3) {
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(markerPtr->name, -1));
            continue;
        }
        for (i = 3; i < objc; i++) {
            if (Tcl_StringMatch(markerPtr->name, Tcl_GetString(objv[i]))) {
                Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(markerPtr->name, -1));
                break;
            }
        }
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MakeMarkerTag --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *tagName
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
ClientData Rbc_MakeMarkerTag(Graph *graphPtr, char *tagName) {
    Tcl_HashEntry *hPtr;
    int isNew;

    hPtr = Tcl_CreateHashEntry(&graphPtr->markers.tagTable, tagName, &isNew);
    assert(hPtr);
    return Tcl_GetHashKey(&graphPtr->markers.tagTable, hPtr);
}

void Rbc_AppendMarkerBindingTags(Graph *graphPtr, Marker *markerPtr, Rbc_List list) {
    char **p;

    Rbc_ListAppend(list, Rbc_MakeMarkerTag(graphPtr, markerPtr->name), 0);
    Rbc_ListAppend(list, Rbc_MakeMarkerTag(graphPtr, markerPtr->classUid), 0);
    if (markerPtr->tags == NULL) {
        return;
    }
    for (p = markerPtr->tags; *p != NULL; p++) {
        Rbc_ListAppend(list, Rbc_MakeMarkerTag(graphPtr, *p), 0);
    }
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
static int BindOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        Tcl_HashEntry *hPtr;
        Tcl_HashSearch cursor;
        char *tag;
        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

        for (hPtr = Tcl_FirstHashEntry(&graphPtr->markers.tagTable, &cursor); hPtr != NULL;
             hPtr = Tcl_NextHashEntry(&cursor)) {
            tag = Tcl_GetHashKey(&graphPtr->markers.tagTable, hPtr);
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(tag, -1));
        }
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    return Rbc_ConfigureBindingsFromObj(interp, graphPtr->bindTable,
                                        Rbc_MakeMarkerTag(graphPtr, Tcl_GetString(objv[3])), objc - 4, objv + 4);
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
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
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;
    Tcl_Obj *resultObjPtr;

    if (NameToMarker(graphPtr, Tcl_GetString(objv[3]), &markerPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    resultObjPtr = Tk_GetOptionValue(interp, (char *)markerPtr, markerPtr->optionTable, objv[4], graphPtr->tkwin);
    if (resultObjPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, resultObjPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureOp --
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
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;
    Tcl_Size nNames;
    Tcl_Size nOpts;
    Tcl_Obj *const *options;
    Tcl_Size i;
    const char *string;

    /*
     * Find where the option/value pairs begin.
     */
    objc -= 3;
    objv += 3;
    for (i = 0; i < objc; i++) {
        string = Tcl_GetString(objv[i]);
        if (string[0] == '-') {
            break;
        }
        if (NameToMarker(graphPtr, string, &markerPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    nNames = i;
    nOpts = objc - i;
    options = objv + nNames;
    for (i = 0; i < nNames; i++) {
        Tcl_Obj *resultObjPtr;
        string = Tcl_GetString(objv[i]);
        if (NameToMarker(graphPtr, string, &markerPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (nOpts == 0) {
            resultObjPtr = Tk_GetOptionInfo(interp, (char *)markerPtr, markerPtr->optionTable, NULL, graphPtr->tkwin);
            if (resultObjPtr == NULL) {
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, resultObjPtr);
            return TCL_OK;
        }
        if (nOpts == 1) {
            resultObjPtr =
                Tk_GetOptionInfo(interp, (char *)markerPtr, markerPtr->optionTable, options[0], graphPtr->tkwin);
            if (resultObjPtr == NULL) {
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, resultObjPtr);
            return TCL_OK;
        }
        if (ConfigureMarkerOptions(markerPtr, nOpts, options, FALSE) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateOp --
 *
 *      This procedure creates and initializes a new marker.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      The return value is a pointer to a structure describing
 *      the new element.  If an error occurred, then the return
 *      value is NULL and an error message is left in the interpreter
 *      result.
 *
 * Side effects:
 *      Memory is allocated, etc.
 *
 * ----------------------------------------------------------------------
 */
static int CreateOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;
    Tcl_HashEntry *hPtr;
    Rbc_Uid classUid;
    const char *name;
    char generatedName[200];
    int isNew;
    int index;
    Tcl_Size i;

    static const struct markerTypeMap {
        const char *name;
        Rbc_Uid *uid;
    } typeMap[] = {{"text", &rbcTextMarkerUid},
                   {"line", &rbcLineMarkerUid},
                   {"polygon", &rbcPolygonMarkerUid},
                   {"image", &rbcImageMarkerUid},
                   {"bitmap", &rbcBitmapMarkerUid},
                   {"window", &rbcWindowMarkerUid},
                   {NULL, NULL}};

    /*
     * Determine the concrete marker class.
     */
    if (Tcl_GetIndexFromObjStruct(interp, objv[3], typeMap, sizeof(struct markerTypeMap), "marker type", 0, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    classUid = *typeMap[index].uid;

    /*
     * Scan the option/value pairs for -name. We need the name before
     * initialising the marker because it is also used as the temporary
     * component-window name.
     *
     * The final -name occurrence wins, matching Tk option processing.
     * Stop at objc - 1 so a malformed final option cannot cause an
     * out-of-bounds read. The actual configuration call will report
     * the missing value.
     */
    name = NULL;
    for (i = 4; (i + 1) < objc; i += 2) {
        const char *option;
        Tcl_Size length;
        option = Tcl_GetStringFromObj(objv[i], &length);
        if ((length >= 2) && (length <= 5) && (strncmp(option, "-name", (size_t)length) == 0)) {
            name = Tcl_GetString(objv[i + 1]);
        }
    }

    /*
     * Generate a default marker name when -name was not supplied.
     */
    if (name == NULL) {
        snprintf(generatedName, sizeof(generatedName), "marker%d", graphPtr->nextMarkerId++);
        name = generatedName;
    } else if (name[0] == '-') {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("name of marker \"%s\" can't start with a '-'", name));
        return TCL_ERROR;
    }

    /*
     * Allocate the concrete marker record. CreateMarker() duplicates
     * the supplied name, so it is safe when name points into a Tcl
     * object or into generatedName.
     */
    markerPtr = CreateMarker(graphPtr, name, classUid);
    if (markerPtr == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't create marker of type \"%s\"", Tcl_GetString(objv[3])));
        return TCL_ERROR;
    }

    /*
     * Initialise the Tk option record and transactionally apply the
     * supplied marker options.
     */
    if (InitMarkerOptions(markerPtr) != TCL_OK) {
        DestroyMarker(markerPtr);
        return TCL_ERROR;
    }
    if (ConfigureMarkerOptions(markerPtr, objc - 4, objv + 4, TRUE) != TCL_OK) {
        DestroyMarker(markerPtr);
        return TCL_ERROR;
    }

    /*
     * Use the configured name rather than the preliminary name.
     * This matters because the option processing owns and may replace
     * markerPtr->name.
     */
    if ((markerPtr->name == NULL) || (markerPtr->name[0] == '\0')) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("marker name may not be empty", -1));

        DestroyMarker(markerPtr);
        return TCL_ERROR;
    }
    if (markerPtr->name[0] == '-') {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("name of marker \"%s\" can't start with a '-'", markerPtr->name));
        DestroyMarker(markerPtr);
        return TCL_ERROR;
    }

    /*
     * Create the hash-table entry. Preserve the existing RBC behaviour:
     * creating a marker with an existing name replaces the old marker.
     */
    hPtr = Tcl_CreateHashEntry(&graphPtr->markers.table, markerPtr->name, &isNew);
    if (!isNew) {
        Marker *oldMarkerPtr;
        oldMarkerPtr = Tcl_GetHashValue(hPtr);

        /*
         * Keep hPtr alive for the new marker. Otherwise DestroyMarker()
         * would delete the hash entry along with the old marker.
         */
        oldMarkerPtr->hashPtr = NULL;
        DestroyMarker(oldMarkerPtr);
    }
    Tcl_SetHashValue(hPtr, markerPtr);
    markerPtr->hashPtr = hPtr;

    /*
     * Add the new marker to the end of the display list.
     */
    markerPtr->linkPtr = Rbc_ChainAppend(graphPtr->markers.displayList, markerPtr);
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->name, -1));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * DeleteOp --
 *
 *      Deletes the marker given by markerId.
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
 *      Graph will be redrawn to reflect the new display list.
 *
 * ----------------------------------------------------------------------
 */
static int DeleteOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;
    Tcl_Size i;

    for (i = 3; i < objc; i++) {
        if (NameToMarker(graphPtr, Tcl_GetString(objv[i]), &markerPtr) == TCL_OK) {
            DestroyMarker(markerPtr);
        }
    }
    Tcl_ResetResult(interp);
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
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
static int GetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    register Marker *markerPtr;
    const char *str = Tcl_GetString(objv[3]);

    if ((str[0] == 'c') && (strcmp(str, "current") == 0)) {
        markerPtr = (Marker *)Rbc_GetCurrentItem(graphPtr->bindTable);
        /* Report only on markers. */
        if (markerPtr == NULL) {
            return TCL_OK;
        }
        if ((markerPtr->classUid == rbcBitmapMarkerUid) || (markerPtr->classUid == rbcLineMarkerUid) ||
            (markerPtr->classUid == rbcWindowMarkerUid) || (markerPtr->classUid == rbcPolygonMarkerUid) ||
            (markerPtr->classUid == rbcTextMarkerUid) || (markerPtr->classUid == rbcImageMarkerUid)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->name, -1));
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * RelinkOp --
 *
 *      Reorders the marker (given by the first name) before/after
 *      the another marker (given by the second name) in the
 *      marker display list.  If no second name is given, the
 *      marker is placed at the beginning/end of the list.
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
 *      Graph will be redrawn to reflect the new display list.
 *
 * ----------------------------------------------------------------------
 */
static int RelinkOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_ChainLink *linkPtr, *placePtr;
    Marker *markerPtr;
    const char *str;

    /* Find the marker to be raised or lowered. */
    if (NameToMarker(graphPtr, Tcl_GetString(objv[3]), &markerPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    /* Right now it's assumed that all markers are always in the
       display list. */
    linkPtr = markerPtr->linkPtr;
    Rbc_ChainUnlinkLink(graphPtr->markers.displayList, markerPtr->linkPtr);

    placePtr = NULL;
    if (objc == 5) {
        if (NameToMarker(graphPtr, Tcl_GetString(objv[4]), &markerPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        placePtr = markerPtr->linkPtr;
    }

    /* Link the marker at its new position. */
    str = Tcl_GetString(objv[2]);
    if (str[0] == 'a') {
        Rbc_ChainLinkAfter(graphPtr->markers.displayList, linkPtr, placePtr);
    } else {
        Rbc_ChainLinkBefore(graphPtr->markers.displayList, linkPtr, placePtr);
    }
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * FindOp --
 *
 *      Returns if marker by a given ID currently exists.
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
 * ----------------------------------------------------------------------
 */
static int FindOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_ChainLink *linkPtr;
    Extents2D exts;
    Marker *markerPtr;
    int mode;
    int left, right, top, bottom;
    int enclosed;

#define FIND_ENCLOSED (1 << 0)
#define FIND_OVERLAPPING (1 << 1)
    static const struct markerSearchMap {
        const char *name;
        int mode;
    } searchMap[] = {{"enclosed", FIND_ENCLOSED}, {"overlapping", FIND_OVERLAPPING}, {NULL, 0}};
    int index;

    if (Tcl_GetIndexFromObjStruct(interp, objv[3], searchMap, sizeof(struct markerSearchMap), "search type", 0,
                                  &index) != TCL_OK) {
        return TCL_ERROR;
    }
    mode = searchMap[index].mode;

    /*
        if (strcmp(argv[3], "enclosed") == 0) {
        mode = FIND_ENCLOSED;
        } else if (strcmp(argv[3], "overlapping") == 0) {
        mode = FIND_OVERLAPPING;
        } else {
        Rbc_AppendResultStrings(interp, "bad search type \"", argv[3],
                 ": should be \"enclosed\", or \"overlapping\"", (char *)NULL);
        return TCL_ERROR;
        }
    */

    if ((Tcl_GetIntFromObj(interp, objv[4], &left) != TCL_OK) || (Tcl_GetIntFromObj(interp, objv[5], &top) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[6], &right) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[7], &bottom) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (left < right) {
        exts.left = (double)left;
        exts.right = (double)right;
    } else {
        exts.left = (double)right;
        exts.right = (double)left;
    }
    if (top < bottom) {
        exts.top = (double)top;
        exts.bottom = (double)bottom;
    } else {
        exts.top = (double)bottom;
        exts.bottom = (double)top;
    }
    enclosed = (mode == FIND_ENCLOSED);
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->markers.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        markerPtr = Rbc_ChainGetValue(linkPtr);
        if (markerPtr->hidden) {
            continue;
        }
        if (markerPtr->elemName != NULL) {
            Tcl_HashEntry *hPtr;

            hPtr = Tcl_FindHashEntry(&graphPtr->elements.table, markerPtr->elemName);
            if (hPtr != NULL) {
                Element *elemPtr;

                elemPtr = (Element *)Tcl_GetHashValue(hPtr);
                if (elemPtr->hidden) {
                    continue;
                }
            }
        }
        if ((*markerPtr->classPtr->regionProc)(markerPtr, &exts, enclosed)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->name, -1));
            return TCL_OK;
        }
    }
    Tcl_ResetResult(interp);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * ExistsOp --
 *
 *      Returns if marker by a given ID currently exists.
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
 * ----------------------------------------------------------------------
 */
static int ExistsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->markers.table, Tcl_GetString(objv[3]));
    Tcl_SetObjResult(interp, Tcl_NewIntObj(hPtr != NULL));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TypeOp --
 *
 *      Returns a symbolic name for the type of the marker whose ID is
 *      given.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result. The interpreter result will contain
 *      the symbolic type of the marker.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int TypeOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;

    if (NameToMarker(graphPtr, Tcl_GetString(objv[3]), &markerPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->classUid, -1));
    return TCL_OK;
}

/* Public routines */

static const MarkerOpSpec markerOps[] = {{{"after", 4, 5, "marker ?afterMarker?"}, RelinkOp},
                                         {{"before", 4, 5, "marker ?beforeMarker?"}, RelinkOp},
                                         {{"bind", 3, 6, "marker sequence command"}, BindOp},
                                         {{"cget", 5, 5, "marker option"}, CgetOp},
                                         {{"configure", 4, 0, "marker ?marker?... ?option value?..."}, ConfigureOp},
                                         {{"create", 4, 0, "type ?option value?..."}, CreateOp},
                                         {{"delete", 3, 0, "?marker?..."}, DeleteOp},
                                         {{"exists", 4, 4, "marker"}, ExistsOp},
                                         {{"find", 8, 8, "enclosed|overlapping x1 y1 x2 y2"}, FindOp},
                                         {{"get", 4, 4, "name"}, GetOp},
                                         {{"names", 3, 0, "?pattern?..."}, NamesOp},
                                         {{"type", 4, 4, "marker"}, TypeOp},
                                         {{NULL, 0, 0, NULL}, NULL}};

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_MarkerOp --
 *
 *      This procedure is invoked to process the Tcl command
 *      that corresponds to a widget managed by this module.
 *      See the user documentation for details on what it does.
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
 * Side effects:
 *      See the user documentation.
 *
 * ----------------------------------------------------------------------
 */
int Rbc_MarkerOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, markerOps, (Tcl_Size)sizeof(markerOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }

    return markerOps[index].proc(graphPtr, interp, objc, objv);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MarkersToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *      int under
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_MarkersToPostScript(Graph *graphPtr, PsToken psToken, int under) {
    Rbc_ChainLink *linkPtr;
    register Marker *markerPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->markers.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        markerPtr = Rbc_ChainGetValue(linkPtr);
        if ((markerPtr->classPtr->postscriptProc == NULL) || (markerPtr->nWorldPts == 0)) {
            continue;
        }
        if (markerPtr->drawUnder != under) {
            continue;
        }
        if (markerPtr->hidden) {
            continue;
        }
        if (markerPtr->elemName != NULL) {
            Tcl_HashEntry *hPtr;

            hPtr = Tcl_FindHashEntry(&graphPtr->elements.table, markerPtr->elemName);
            if (hPtr != NULL) {
                Element *elemPtr;

                elemPtr = (Element *)Tcl_GetHashValue(hPtr);
                if (elemPtr->hidden) {
                    continue;
                }
            }
        }
        Rbc_AppendToPostScript(psToken, "\n% Marker \"", markerPtr->name, "\" is a ", markerPtr->classUid, " marker\n",
                               (char *)NULL);
        (*markerPtr->classPtr->postscriptProc)(markerPtr, psToken);
    }
}

/*
 * -------------------------------------------------------------------------
 *
 * Rbc_DrawMarkers --
 *
 *      Calls the individual drawing routines (based on marker type)
 *      for each marker in the display list.
 *
 *      A marker will not be drawn if
 *
 *      1) An element linked to the marker (indicated by elemName)
 *         is currently hidden.
 *
 *      2) No coordinates have been specified for the marker.
 *
 *      3) The marker is requesting to be drawn at a different level
 *         (above/below the elements) from the current mode.
 *
 *      4) The marker is configured as hidden (-hide option).
 *
 *      5) The marker isn't visible in the current viewport
 *         (i.e. clipped).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *      int under
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      Markers are drawn into the drawable (pixmap) which will eventually
 *      be displayed in the graph window.
 *
 * -------------------------------------------------------------------------
 */
void Rbc_DrawMarkers(Graph *graphPtr, Drawable drawable, int under) {
    Rbc_ChainLink *linkPtr;
    Marker *markerPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->markers.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        markerPtr = Rbc_ChainGetValue(linkPtr);

        if ((markerPtr->nWorldPts == 0) || (markerPtr->drawUnder != under) || (markerPtr->hidden) ||
            (markerPtr->clipped)) {
            continue;
        }
        if (markerPtr->elemName != NULL) {
            Tcl_HashEntry *hPtr;

            /* Look up the named element and see if it's hidden */
            hPtr = Tcl_FindHashEntry(&graphPtr->elements.table, markerPtr->elemName);
            if (hPtr != NULL) {
                Element *elemPtr;

                elemPtr = (Element *)Tcl_GetHashValue(hPtr);
                if (elemPtr->hidden) {
                    continue;
                }
            }
        }

        (*markerPtr->classPtr->drawProc)(markerPtr, drawable);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MapMarkers --
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
void Rbc_MapMarkers(Graph *graphPtr) {
    Rbc_ChainLink *linkPtr;
    Marker *markerPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->markers.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        markerPtr = Rbc_ChainGetValue(linkPtr);
        if ((markerPtr->nWorldPts == 0) || (markerPtr->hidden)) {
            continue;
        }
        if ((graphPtr->flags & MAP_ALL) || (markerPtr->flags & MAP_ITEM)) {
            (*markerPtr->classPtr->mapProc)(markerPtr);
            markerPtr->flags &= ~MAP_ITEM;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DestroyMarkers --
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
void Rbc_DestroyMarkers(Graph *graphPtr) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Marker *markerPtr;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->markers.table, &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        markerPtr = (Marker *)Tcl_GetHashValue(hPtr);
        /*
         * Dereferencing the pointer to the hash table prevents the
         * hash table entry from being automatically deleted.
         */
        markerPtr->hashPtr = NULL;
        DestroyMarker(markerPtr);
    }
    Tcl_DeleteHashTable(&graphPtr->markers.table);
    Tcl_DeleteHashTable(&graphPtr->markers.tagTable);
    Rbc_ChainDestroy(graphPtr->markers.displayList);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_NearestMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      int x - Screen coordinate
 *      int y -Screen coordinate
 *      int under
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Marker *Rbc_NearestMarker(Graph *graphPtr, int x, int y, int under) {
    Rbc_ChainLink *linkPtr;
    Marker *markerPtr;
    Point2D point;

    point.x = (double)x;
    point.y = (double)y;
    for (linkPtr = Rbc_ChainLastLink(graphPtr->markers.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainPrevLink(linkPtr)) {
        markerPtr = Rbc_ChainGetValue(linkPtr);
        if ((markerPtr->drawUnder == under) && (markerPtr->nWorldPts > 0) && (!markerPtr->hidden) &&
            (markerPtr->state == STATE_NORMAL)) {
            if ((*markerPtr->classPtr->pointProc)(markerPtr, &point)) {
                return markerPtr;
            }
        }
    }
    return NULL;
}
