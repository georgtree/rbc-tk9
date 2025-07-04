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
#include "rbcGrElem.h"

#define MAX_OUTLINE_POINTS 12

/* Map graph coordinates to normalized coordinates [0..1] */
#define NORMALIZE(A, x) (((x) - (A)->axisRange.min) / (A)->axisRange.range)

#define DEF_MARKER_ANCHOR "center"
#define DEF_MARKER_BACKGROUND RGB_WHITE
#define DEF_MARKER_BG_MONO RGB_WHITE
#define DEF_MARKER_BITMAP (char *)NULL
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
#define DEF_MARKER_GAP_COLOR RGB_PINK
#define DEF_MARKER_GAP_MONO RGB_BLACK
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
#define DEF_MARKER_SCALE "1.0"
#define DEF_MARKER_SHADOW_COLOR (char *)NULL
#define DEF_MARKER_SHADOW_MONO (char *)NULL
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

static Tk_OptionParseProc StringToCoordinates;
static Tk_OptionPrintProc CoordinatesToString;
static Tk_CustomOption coordsOption = {StringToCoordinates, CoordinatesToString, (ClientData)0};
extern Tk_CustomOption rbcColorPairOption;
extern Tk_CustomOption rbcDashesOption;
extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcListOption;
extern Tk_CustomOption rbcPadOption;
extern Tk_CustomOption rbcPositiveDistanceOption;
extern Tk_CustomOption rbcShadowOption;
extern Tk_CustomOption rbcStateOption;
extern Tk_CustomOption rbcXAxisOption;
extern Tk_CustomOption rbcYAxisOption;

typedef Marker *(MarkerCreateProc)(void);
typedef void(MarkerDrawProc)(Marker *markerPtr, Drawable drawable);
typedef void(MarkerFreeProc)(Graph *graphPtr, Marker *markerPtr);
typedef int(MarkerConfigProc)(Marker *markerPtr);
typedef void(MarkerMapProc)(Marker *markerPtr);
typedef void(MarkerPostScriptProc)(Marker *markerPtr, PsToken psToken);
typedef int(MarkerPointProc)(Marker *markerPtr, Point2D *samplePtr);
typedef int(MarkerRegionProc)(Marker *markerPtr, Extents2D *extsPtr, int enclosed);

typedef struct {
    Tk_ConfigSpec *configSpecs; /* Marker configuration specifications */
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
 *      This structure corresponds with the specific types of markers.
 *      Don't change this structure without changing the individual
 *      marker structures of each type below.
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
    int nWorldPts;     /* Number of points in above array */
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
};

/*
 * -------------------------------------------------------------------
 *
 * TextMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char *name;       /* Identifier for marker */
    Rbc_Uid classUid; /* Type of marker */
    Graph *graphPtr;  /* The graph this marker belongs to */
    unsigned int flags;
    char **tags;
    int hidden; /* If non-zero, don't display the
                 * marker. */
    Tcl_HashEntry *hashPtr;
    Rbc_ChainLink *linkPtr;
    Point2D *worldPts; /* Position of marker (1 X-Y coordinate) in
                        * world (graph) coordinates. */
    int nWorldPts;     /* Number of points */
    char *elemName;    /* Element associated with marker */
    Axis2D axes;
    int drawUnder;        /* If non-zero, draw the marker
                           * underneath any elements. There can
                           * be a performance because the graph
                           * must be redraw entirely each time
                           * this marker is redrawn. */
    int clipped;          /* Indicates if the marker is totally
                           * clipped by the plotting area. */
    int xOffset, yOffset; /* pixel offset from anchor */
    MarkerClass *classPtr;
    int state;

    /*
     * Text specific fields and attributes
     */
#ifdef notdef
    char *textVarName; /* Name of variable (malloc'ed) or
                        * NULL. If non-NULL, graph displays
                        * the contents of this variable. */
#endif
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

static Tk_ConfigSpec textConfigSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR, offsetof(TextMarker, anchor), 0},
    {TK_CONFIG_COLOR, "-background", "background", "MarkerBackground", (char *)NULL, offsetof(TextMarker, fillColor),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-bg", "background", "Background", (char *)NULL, 0, 0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_MARKER_TEXT_TAGS, offsetof(Marker, tags),
     TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, worldPts), TK_CONFIG_NULL_OK,
     &coordsOption},
    {TK_CONFIG_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, offsetof(Marker, elemName),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-fg", "foreground", "Foreground", (char *)NULL, 0, 0},
    {TK_CONFIG_SYNONYM, "-fill", "background", (char *)NULL, (char *)NULL, 0, 0},
    {TK_CONFIG_FONT, "-font", "font", "Font", DEF_MARKER_FONT, offsetof(TextMarker, style.font), 0},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground", DEF_MARKER_FOREGROUND,
     offsetof(TextMarker, style.color), TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground", DEF_MARKER_FG_MONO, offsetof(TextMarker, style.color),
     TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_JUSTIFY, "-justify", "justify", "Justify", DEF_MARKER_JUSTIFY, offsetof(TextMarker, style.justify),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, offsetof(Marker, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *)NULL, (char *)NULL, DEF_MARKER_NAME, offsetof(Marker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-outline", "foreground", (char *)NULL, (char *)NULL, 0, 0},
    {TK_CONFIG_CUSTOM, "-padx", "padX", "PadX", DEF_MARKER_PAD, offsetof(TextMarker, style.padX),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcPadOption},
    {TK_CONFIG_CUSTOM, "-pady", "padY", "PadY", DEF_MARKER_PAD, offsetof(TextMarker, style.padY),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcPadOption},
    {TK_CONFIG_DOUBLE, "-rotate", "rotate", "Rotate", DEF_MARKER_ROTATE, offsetof(TextMarker, style.theta),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", DEF_MARKER_SHADOW_COLOR, offsetof(TextMarker, style.shadow),
     TK_CONFIG_COLOR_ONLY, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", DEF_MARKER_SHADOW_MONO, offsetof(TextMarker, style.shadow),
     TK_CONFIG_MONO_ONLY, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, state),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_STRING, "-text", "text", "Text", DEF_MARKER_TEXT, offsetof(TextMarker, string), TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, offsetof(Marker, drawUnder),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

/*
 * -------------------------------------------------------------------
 *
 * WindowMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char *name;       /* Identifier for marker */
    Rbc_Uid classUid; /* Type of marker */
    Graph *graphPtr;  /* Graph marker belongs to */
    unsigned int flags;
    char **tags;
    int hidden; /* Indicates if the marker is
                 * currently hidden or not. */
    Tcl_HashEntry *hashPtr;
    Rbc_ChainLink *linkPtr;
    Point2D *worldPts; /* Position of marker (1 X-Y coordinate) in
                        * world (graph) coordinates. */
    int nWorldPts;     /* Number of points */
    char *elemName;    /* Element associated with marker */
    Axis2D axes;
    int drawUnder;        /* If non-zero, draw the marker
                           * underneath any elements. There can
                           * be a performance because the graph
                           * must be redraw entirely each time
                           * this marker is redrawn. */
    int clipped;          /* Indicates if the marker is totally
                           * clipped by the plotting area. */
    int xOffset, yOffset; /* Pixel offset from anchor. */
    MarkerClass *classPtr;
    int state;

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

static Tk_ConfigSpec windowConfigSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR, offsetof(WindowMarker, anchor), 0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_MARKER_WINDOW_TAGS, offsetof(Marker, tags),
     TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(WindowMarker, worldPts),
     TK_CONFIG_NULL_OK, &coordsOption},
    {TK_CONFIG_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, offsetof(Marker, elemName),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-height", "height", "Height", DEF_MARKER_HEIGHT, offsetof(WindowMarker, reqHeight),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcPositiveDistanceOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, offsetof(Marker, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *)NULL, (char *)NULL, DEF_MARKER_NAME, offsetof(Marker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, state),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, offsetof(Marker, drawUnder),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-width", "width", "Width", DEF_MARKER_WIDTH, offsetof(WindowMarker, reqWidth),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcPositiveDistanceOption},
    {TK_CONFIG_STRING, "-window", "window", "Window", DEF_MARKER_WINDOW, offsetof(WindowMarker, pathName),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

/*
 * -------------------------------------------------------------------
 *
 * BitmapMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char *name;       /* Identifier for marker */
    Rbc_Uid classUid; /* Type of marker */
    Graph *graphPtr;  /* Graph marker belongs to */
    unsigned int flags;
    char **tags;
    int hidden; /* Indicates if the marker is currently
                 * hidden or not. */
    Tcl_HashEntry *hashPtr;
    Rbc_ChainLink *linkPtr;
    Point2D *worldPts; /* Position of marker in world (graph)
                        * coordinates. If 2 pairs of X-Y
                        * coordinates are specified, then the
                        * bitmap is resized to fit this area.
                        * Otherwise if 1 pair, then the bitmap
                        * is positioned at the coordinate at its
                        * normal size. */
    int nWorldPts;     /* Number of points */
    char *elemName;    /* Element associated with marker */
    Axis2D axes;
    int drawUnder;        /* If non-zero, draw the marker
                           * underneath any elements. There can
                           * be a performance because the graph
                           * must be redraw entirely each time
                           * this marker is redrawn. */
    int clipped;          /* Indicates if the marker is totally
                           * clipped by the plotting area. */
    int xOffset, yOffset; /* Pixel offset from origin of bitmap */
    MarkerClass *classPtr;
    int state;

    /* Bitmap specific attributes */
    Pixmap srcBitmap;                    /* Original bitmap. May be further
                                          * scaled or rotated. */
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
    int destWidth, destHeight;           /* Dimensions of the final bitmap */
    Point2D outline[MAX_OUTLINE_POINTS]; /* Polygon representing the background
                                          * of the bitmap. */
    int nOutlinePts;
} BitmapMarker;

static Tk_ConfigSpec bitmapConfigSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR, offsetof(BitmapMarker, anchor), 0},
    {TK_CONFIG_COLOR, "-background", "background", "Background", DEF_MARKER_BACKGROUND,
     offsetof(BitmapMarker, fillColor), TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-background", "background", "Background", DEF_MARKER_BG_MONO, offsetof(BitmapMarker, fillColor),
     TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-bg", "background", (char *)NULL, (char *)NULL, 0, 0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_MARKER_BITMAP_TAGS, offsetof(Marker, tags),
     TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_BITMAP, "-bitmap", "bitmap", "Bitmap", DEF_MARKER_BITMAP, offsetof(BitmapMarker, srcBitmap),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, worldPts), TK_CONFIG_NULL_OK,
     &coordsOption},
    {TK_CONFIG_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, offsetof(Marker, elemName),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-fg", "foreground", (char *)NULL, (char *)NULL, 0, 0},
    {TK_CONFIG_SYNONYM, "-fill", "background", (char *)NULL, (char *)NULL, 0, 0},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground", DEF_MARKER_FOREGROUND,
     offsetof(BitmapMarker, outlineColor), TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-foreground", "foreground", "Foreground", DEF_MARKER_FG_MONO,
     offsetof(BitmapMarker, outlineColor), TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, offsetof(Marker, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *)NULL, (char *)NULL, DEF_MARKER_NAME, offsetof(Marker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-outline", "foreground", (char *)NULL, (char *)NULL, 0, 0},
    {TK_CONFIG_DOUBLE, "-rotate", "rotate", "Rotate", DEF_MARKER_ROTATE, offsetof(BitmapMarker, rotate),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, state),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, offsetof(Marker, drawUnder),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

/*
 * -------------------------------------------------------------------
 *
 * ImageMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char *name;       /* Identifier for marker */
    Rbc_Uid classUid; /* Type of marker */
    Graph *graphPtr;  /* Graph marker belongs to */
    unsigned int flags;
    char **tags;
    int hidden; /* Indicates if the marker is
                 * currently hidden or not. */

    Tcl_HashEntry *hashPtr;
    Rbc_ChainLink *linkPtr;
    Point2D *worldPts; /* Position of marker in world (graph)
                        * coordinates. If 2 pairs of X-Y
                        * coordinates are specified, then the
                        * image is resized to fit this area.
                        * Otherwise if 1 pair, then the image
                        * is positioned at the coordinate at
                        * its normal size. */
    int nWorldPts;     /* Number of points */

    char *elemName; /* Element associated with marker */
    Axis2D axes;
    int drawUnder;        /* If non-zero, draw the marker
                           * underneath any elements. There can
                           * be a performance because the graph
                           * must be redraw entirely each time
                           * this marker is redrawn. */
    int clipped;          /* Indicates if the marker is totally
                           * clipped by the plotting area. */
    int xOffset, yOffset; /* Pixel offset from anchor */

    MarkerClass *classPtr;

    int state;

    /* Image specific attributes */
    char *imageName;   /* Name of image to be displayed. */
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

static Tk_ConfigSpec imageConfigSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", "anchor", "Anchor", DEF_MARKER_ANCHOR, offsetof(ImageMarker, anchor), 0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_MARKER_IMAGE_TAGS, offsetof(Marker, tags),
     TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, worldPts), TK_CONFIG_NULL_OK,
     &coordsOption},
    {TK_CONFIG_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, offsetof(Marker, elemName),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, offsetof(Marker, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_STRING, "-image", "image", "Image", (char *)NULL, offsetof(ImageMarker, imageName), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *)NULL, (char *)NULL, DEF_MARKER_NAME, offsetof(Marker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, state),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, offsetof(Marker, drawUnder),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

/*
 * -------------------------------------------------------------------
 *
 * LineMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char *name;       /* Identifier for marker */
    Rbc_Uid classUid; /* Type is "linemarker" */
    Graph *graphPtr;  /* Graph marker belongs to */
    unsigned int flags;
    char **tags;
    int hidden; /* Indicates if the marker is currently
                 * hidden or not. */

    Tcl_HashEntry *hashPtr;
    Rbc_ChainLink *linkPtr;

    Point2D *worldPts; /* Position of marker (X-Y coordinates) in
                        * world (graph) coordinates. */
    int nWorldPts;     /* Number of points */

    char *elemName; /* Element associated with marker */
    Axis2D axes;
    int drawUnder;        /* If non-zero, draw the marker
                           * underneath any elements. There can
                           * be a performance because the graph
                           * must be redraw entirely each time
                           * this marker is redrawn. */
    int clipped;          /* Indicates if the marker is totally
                           * clipped by the plotting area. */
    int xOffset, yOffset; /* Pixel offset */

    MarkerClass *classPtr;

    int state;

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
    int nSegments;       /* # segments in the above array. */

    int xor ;
    int xorState; /* State of the XOR drawing. Indicates
                   * if the marker is currently drawn. */
} LineMarker;

static Tk_ConfigSpec lineConfigSpecs[] = {
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_MARKER_LINE_TAGS, offsetof(Marker, tags),
     TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CAP_STYLE, "-cap", "cap", "Cap", DEF_MARKER_CAP_STYLE, offsetof(LineMarker, capStyle),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, worldPts), TK_CONFIG_NULL_OK,
     &coordsOption},
    {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes", DEF_MARKER_DASHES, offsetof(LineMarker, dashes),
     TK_CONFIG_NULL_OK, &rbcDashesOption},
    {TK_CONFIG_CUSTOM, "-dashoffset", "dashOffset", "DashOffset", DEF_MARKER_DASH_OFFSET,
     offsetof(LineMarker, dashes.offset), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, offsetof(Marker, elemName),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-fill", "fill", "Fill", (char *)NULL, offsetof(LineMarker, fillColor), TK_CONFIG_NULL_OK},
    {TK_CONFIG_JOIN_STYLE, "-join", "join", "Join", DEF_MARKER_JOIN_STYLE, offsetof(LineMarker, joinStyle),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "LineWidth", DEF_MARKER_LINE_WIDTH, offsetof(LineMarker, lineWidth),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, offsetof(Marker, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *)NULL, (char *)NULL, DEF_MARKER_NAME, offsetof(Marker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-outline", "outline", "Outline", DEF_MARKER_OUTLINE_COLOR, offsetof(LineMarker, outlineColor),
     TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-outline", "outline", "Outline", DEF_MARKER_OUTLINE_MONO, offsetof(LineMarker, outlineColor),
     TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, state),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, offsetof(Marker, drawUnder),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-xor", "xor", "Xor", DEF_MARKER_XOR, offsetof(LineMarker, xor), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

/*
 * -------------------------------------------------------------------
 *
 * PolygonMarker --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    char *name;       /* Identifier for marker */
    Rbc_Uid classUid; /* Type of marker */
    Graph *graphPtr;  /* Graph marker belongs to */
    unsigned int flags;
    char **tags;
    int hidden; /* Indicates if the marker is currently
                 * hidden or not. */

    Tcl_HashEntry *hashPtr;
    Rbc_ChainLink *linkPtr;

    Point2D *worldPts; /* Position of marker (X-Y coordinates) in
                        * world (graph) coordinates. */
    int nWorldPts;     /* Number of points */

    char *elemName; /* Element associated with marker */
    Axis2D axes;
    int drawUnder;        /* If non-zero, draw the marker
                           * underneath any elements. There can
                           * be a performance because the graph
                           * must be redraw entirely each time
                           * this marker is redrawn. */
    int clipped;          /* Indicates if the marker is totally
                           * clipped by the plotting area. */
    int xOffset, yOffset; /* Pixel offset */

    MarkerClass *classPtr;

    int state;

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

    int nFillPts; /* # points in the above array. */

    Segment2D *outlinePts; /* Malloc'ed array of points.
                            * Represents individual line segments
                            * (2 points per segment) comprising
                            * the outline of the polygon.  The
                            * segments may not necessarily be
                            * closed or connected after clipping. */

    int nOutlinePts; /* # points in the above array. */

    int xor ;
    int xorState; /* State of the XOR drawing. Indicates
                   * if the marker is visible. We have
                   * to drawn it again to erase it. */
} PolygonMarker;

static Tk_ConfigSpec polygonConfigSpecs[] = {
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_MARKER_POLYGON_TAGS, offsetof(Marker, tags),
     TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_CAP_STYLE, "-cap", "cap", "Cap", DEF_MARKER_CAP_STYLE, offsetof(PolygonMarker, capStyle),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, worldPts), TK_CONFIG_NULL_OK,
     &coordsOption},
    {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes", DEF_MARKER_DASHES, offsetof(PolygonMarker, dashes),
     TK_CONFIG_NULL_OK, &rbcDashesOption},
    {TK_CONFIG_STRING, "-element", "element", "Element", DEF_MARKER_ELEMENT, offsetof(Marker, elemName),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_MARKER_FILL_COLOR, offsetof(PolygonMarker, fill),
     TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK, &rbcColorPairOption},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_MARKER_FILL_MONO, offsetof(PolygonMarker, fill),
     TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK, &rbcColorPairOption},
    {TK_CONFIG_JOIN_STYLE, "-join", "join", "Join", DEF_MARKER_JOIN_STYLE, offsetof(PolygonMarker, joinStyle),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "LineWidth", DEF_MARKER_LINE_WIDTH,
     offsetof(PolygonMarker, lineWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, offsetof(Marker, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_STRING, "-name", (char *)NULL, (char *)NULL, DEF_MARKER_NAME, offsetof(Marker, name), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline", DEF_MARKER_OUTLINE_COLOR, offsetof(PolygonMarker, outline),
     TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK, &rbcColorPairOption},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline", DEF_MARKER_OUTLINE_MONO, offsetof(PolygonMarker, outline),
     TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK, &rbcColorPairOption},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, state),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcStateOption},
    {TK_CONFIG_BITMAP, "-stipple", "stipple", "Stipple", DEF_MARKER_STIPPLE, offsetof(PolygonMarker, stipple),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, offsetof(Marker, drawUnder),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-xor", "xor", "Xor", DEF_MARKER_XOR, offsetof(PolygonMarker, xor), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffset),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

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
static char *PrintCoordinate(Tcl_Interp *interp, double x);
static int ParseCoordinates(Tcl_Interp *interp, Marker *markerPtr, Tcl_Size nExprs, const char **exprArr);
static double HMap(Graph *graphPtr, Axis *axisPtr, double x);
static double VMap(Graph *graphPtr, Axis *axisPtr, double y);
static Point2D MapPoint(Graph *graphPtr, Point2D *pointPtr, Axis2D *axesPtr);
static Marker *CreateMarker(Graph *graphPtr, char *name, Rbc_Uid classUid);
static void DestroyMarker(Marker *markerPtr);
static int NameToMarker(Graph *graphPtr, char *name, Marker **markerPtrPtr);
static int RenameMarker(Graph *graphPtr, Marker *markerPtr, char *oldName, char *newName);

typedef int(RbcGrMarkerOp)(Graph *, Tcl_Interp *, int, Tcl_Obj *const[]);
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
    bitmapConfigSpecs, ConfigureBitmapMarker, DrawBitmapMarker,     FreeBitmapMarker,
    MapBitmapMarker,   PointInBitmapMarker,   RegionInBitmapMarker, BitmapMarkerToPostScript,
};

static MarkerClass imageMarkerClass = {
    imageConfigSpecs, ConfigureImageMarker, DrawImageMarker,     FreeImageMarker,
    MapImageMarker,   PointInImageMarker,   RegionInImageMarker, ImageMarkerToPostScript,
};

static MarkerClass lineMarkerClass = {
    lineConfigSpecs, ConfigureLineMarker, DrawLineMarker,     FreeLineMarker,
    MapLineMarker,   PointInLineMarker,   RegionInLineMarker, LineMarkerToPostScript,
};

static MarkerClass polygonMarkerClass = {
    polygonConfigSpecs, ConfigurePolygonMarker, DrawPolygonMarker,     FreePolygonMarker,
    MapPolygonMarker,   PointInPolygonMarker,   RegionInPolygonMarker, PolygonMarkerToPostScript,
};

static MarkerClass textMarkerClass = {
    textConfigSpecs, ConfigureTextMarker, DrawTextMarker,     FreeTextMarker,
    MapTextMarker,   PointInTextMarker,   RegionInTextMarker, TextMarkerToPostScript,
};

static MarkerClass windowMarkerClass = {
    windowConfigSpecs, ConfigureWindowMarker, DrawWindowMarker,     FreeWindowMarker,
    MapWindowMarker,   PointInWindowMarker,   RegionInWindowMarker, WindowMarkerToPostScript,
};

#ifdef notdef
static MarkerClass rectangleMarkerClass = {
    rectangleConfigSpecs, ConfigureRectangleMarker, DrawRectangleMarker,     FreeRectangleMarker,
    MapRectangleMarker,   PointInRectangleMarker,   RegionInRectangleMarker, RectangleMarkerToPostScript,
};

static MarkerClass ovalMarkerClass = {
    ovalConfigSpecs, ConfigureOvalMarker, DrawOvalMarker,     FreeOvalMarker,
    MapOvalMarker,   PointInOvalMarker,   RegionInOvalMarker, OvalMarkerToPostScript,
};
#endif

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
 * PrintCoordinate --
 *
 *      Convert the floating point value into its string
 *      representation.  The only reason this routine is used in
 *      instead of sprintf, is to handle the "elastic" bounds.  That
 *      is, convert the values DBL_MAX and -(DBL_MAX) into "+Inf" and
 *      "-Inf" respectively.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      double x - Numeric value
 *
 * Results:
 *      The return value is a standard Tcl result.  The string of the
 *      expression is passed back via string.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ---------------------------------------------------------------------- */
static char *PrintCoordinate(Tcl_Interp *interp, double x) {
    if (x == DBL_MAX) {
        return "+Inf";
    } else if (x == -DBL_MAX) {
        return "-Inf";
    } else {
        static char string[TCL_DOUBLE_SPACE + 1];

        Tcl_PrintDouble(interp, (double)x, string);
        return string;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ParseCoordinates --
 *
 *      The Tcl coordinate list is converted to their floating point
 *      values. It will then replace the current marker coordinates.
 *
 *      Since different marker types require different number of
 *      coordinates this must be checked here.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Marker *markerPtr
 *      Tcl_Size nExprs
 *      const char **exprArr
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side effects:
 *      If the marker coordinates are reset, the graph is eventually
 *      redrawn with at the new marker coordinates.
 *
 * ----------------------------------------------------------------------
 */
static int ParseCoordinates(Tcl_Interp *interp, Marker *markerPtr, Tcl_Size nExprs, const char **exprArr) {
    int nWorldPts;
    int minArgs, maxArgs;
    Point2D *worldPts;
    register int i;
    register Point2D *pointPtr;
    double x, y;

    if (nExprs == 0) {
        return TCL_OK;
    }
    if (nExprs & 1) {
        Tcl_AppendResult(interp, "odd number of marker coordinates specified", (char *)NULL);
        return TCL_ERROR;
    }
    if (markerPtr->classUid == rbcLineMarkerUid) {
        minArgs = 4, maxArgs = 0;
    } else if (markerPtr->classUid == rbcPolygonMarkerUid) {
        minArgs = 6, maxArgs = 0;
    } else if ((markerPtr->classUid == rbcWindowMarkerUid) || (markerPtr->classUid == rbcTextMarkerUid)) {
        minArgs = 2, maxArgs = 2;
    } else if ((markerPtr->classUid == rbcImageMarkerUid) || (markerPtr->classUid == rbcBitmapMarkerUid)) {
        minArgs = 2, maxArgs = 4;
    } else {
        Tcl_AppendResult(interp, "unknown marker type", (char *)NULL);
        return TCL_ERROR;
    }

    if (nExprs < minArgs) {
        Tcl_AppendResult(interp, "too few marker coordinates specified", (char *)NULL);
        return TCL_ERROR;
    }
    if ((maxArgs > 0) && (nExprs > maxArgs)) {
        Tcl_AppendResult(interp, "too many marker coordinates specified", (char *)NULL);
        return TCL_ERROR;
    }
    nWorldPts = nExprs / 2;
    worldPts = (Point2D *)ckalloc(nWorldPts * sizeof(Point2D));
    if (worldPts == NULL) {
        Tcl_AppendResult(interp, "can't allocate new coordinate array", (char *)NULL);
        return TCL_ERROR;
    }

    /* Don't free the old coordinate array until we've parsed the new
     * coordinates without errors.  */
    pointPtr = worldPts;
    for (i = 0; i < nExprs; i += 2) {
        if ((GetCoordinate(interp, exprArr[i], &x) != TCL_OK) ||
            (GetCoordinate(interp, exprArr[i + 1], &y) != TCL_OK)) {
            ckfree((char *)worldPts);
            return TCL_ERROR;
        }
        pointPtr->x = x, pointPtr->y = y;
        pointPtr++;
    }
    if (markerPtr->worldPts != NULL) {
        ckfree((char *)markerPtr->worldPts);
    }
    markerPtr->worldPts = worldPts;
    markerPtr->nWorldPts = nWorldPts;
    markerPtr->flags |= MAP_ITEM;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * StringToCoordinates --
 *
 *      Given a Tcl list of numeric expression representing the
 *      element values, convert into an array of floating point
 *      values. In addition, the minimum and maximum values are saved.
 *      Since elastic values are allow (values which translate to the
 *      min/max of the graph), we must try to get the non-elastic
 *      minimum and maximum.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - Tcl list of numeric expressions
 *      char *widgRec - Marker record
 *      Tcl_Size offset - Not used.
 *
 * Results:
 *      The return value is a standard Tcl result.  The vector is
 *      passed back via the vecPtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int StringToCoordinates(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string,
                               char *widgRec, Tcl_Size offset) {
    Marker *markerPtr = (Marker *)widgRec;
    Tcl_Size nExprs;
    const char **exprArr;
    int result;

    nExprs = 0;
    if ((string != NULL) && (Tcl_SplitList(interp, string, &nExprs, &exprArr) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (nExprs == 0) {
        if (markerPtr->worldPts != NULL) {
            ckfree((char *)markerPtr->worldPts);
            markerPtr->worldPts = NULL;
        }
        markerPtr->nWorldPts = 0;
        return TCL_OK;
    }
    result = ParseCoordinates(interp, markerPtr, nExprs, exprArr);
    ckfree((char *)exprArr);
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * CoordinatesToString --
 *
 *      Convert the vector of floating point values into a Tcl list.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Marker record
 *      Tcl_Size offset - Not used.
 *      Tcl_FreeProc **freeProcPtr - Memory deallocation scheme to use
 *
 * Results:
 *      The string representation of the vector is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static const char *CoordinatesToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                       Tcl_FreeProc **freeProcPtr) {
    Marker *markerPtr = (Marker *)widgRec;
    Tcl_Interp *interp;
    Tcl_DString dString;
    char *result;
    register int i;
    register Point2D *p;

    if (markerPtr->nWorldPts < 1) {
        return "";
    }
    interp = markerPtr->graphPtr->interp;

    Tcl_DStringInit(&dString);
    p = markerPtr->worldPts;
    for (i = 0; i < markerPtr->nWorldPts; i++) {
        Tcl_DStringAppendElement(&dString, PrintCoordinate(interp, p->x));
        Tcl_DStringAppendElement(&dString, PrintCoordinate(interp, p->y));
        p++;
    }
    result = Tcl_DStringValue(&dString);

    /*
     * If memory wasn't allocated for the dynamic string, do it here (it's
     * currently on the stack), so that Tcl can free it normally.
     */
    if (result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
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
static Marker *CreateMarker(Graph *graphPtr, char *name, Rbc_Uid classUid) {
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
    markerPtr->flags |= MAP_ITEM;
    markerPtr->name = RbcStrdup(name);
    markerPtr->classUid = classUid;
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
    Graph *graphPtr = markerPtr->graphPtr;

    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    /* Free the resources allocated for the particular type of marker */
    (*markerPtr->classPtr->freeProc)(graphPtr, markerPtr);
    if (markerPtr->worldPts != NULL) {
        ckfree((char *)markerPtr->worldPts);
    }
    Rbc_DeleteBindings(graphPtr->bindTable, markerPtr);
    Tk_FreeOptions(markerPtr->classPtr->configSpecs, (char *)markerPtr, graphPtr->display, 0);
    if (markerPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(markerPtr->hashPtr);
    }
    if (markerPtr->linkPtr != NULL) {
        Rbc_ChainDeleteLink(graphPtr->markers.displayList, markerPtr->linkPtr);
    }
    if (markerPtr->name != NULL) {
        ckfree((char *)markerPtr->name);
    }
    if (markerPtr->elemName != NULL) {
        ckfree((char *)markerPtr->elemName);
    }
    if (markerPtr->tags != NULL) {
        ckfree((char *)markerPtr->tags);
    }
    ckfree((char *)markerPtr);
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
    Graph *graphPtr = markerPtr->graphPtr;
    BitmapMarker *bmPtr = (BitmapMarker *)markerPtr;
    GC newGC;
    XGCValues gcValues;
    unsigned long gcMask;

    if (bmPtr->srcBitmap == None) {
        return TCL_OK;
    }
    if (bmPtr->destBitmap == None) {
        bmPtr->destBitmap = bmPtr->srcBitmap;
    }
    bmPtr->theta = FMOD(bmPtr->rotate, 360.0);
    if (bmPtr->theta < 0.0) {
        bmPtr->theta += 360.0;
    }
    gcMask = 0;
    if (bmPtr->outlineColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = bmPtr->outlineColor->pixel;
    }
    if (bmPtr->fillColor != NULL) {
        gcValues.background = bmPtr->fillColor->pixel;
        gcMask |= GCBackground;
    } else {
        gcValues.clip_mask = bmPtr->srcBitmap;
        gcMask |= GCClipMask;
    }

    /* Note that while this is a "shared" GC, we're going to change
     * the clip origin right before the bitmap is drawn anyways.  This
     * assumes that any drawing code using this GC (with GCClipMask
     * set) is going to want to set the clip origin anyways.  */
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (bmPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->gc);
    }
    bmPtr->gc = newGC;

    /* Create background GC color */

    if (bmPtr->fillColor != NULL) {
        gcValues.foreground = bmPtr->fillColor->pixel;
        newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
        if (bmPtr->fillGC != NULL) {
            Tk_FreeGC(graphPtr->display, bmPtr->fillGC);
        }
        bmPtr->fillGC = newGC;
    }
    bmPtr->flags |= MAP_ITEM;
    if (bmPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
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
    BitmapMarker *bmPtr = (BitmapMarker *)markerPtr;
    Extents2D exts;
    Graph *graphPtr = markerPtr->graphPtr;
    Point2D anchorPos;
    Point2D corner1, corner2;
    int destWidth, destHeight;
    int srcWidth, srcHeight;
    register int i;

    if (bmPtr->srcBitmap == None) {
        return;
    }
    if (bmPtr->destBitmap != bmPtr->srcBitmap) {
        Tk_FreePixmap(graphPtr->display, bmPtr->destBitmap);
        bmPtr->destBitmap = bmPtr->srcBitmap;
    }
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
    corner1 = MapPoint(graphPtr, bmPtr->worldPts, &bmPtr->axes);
    if (bmPtr->nWorldPts > 1) {
        double hold;

        corner2 = MapPoint(graphPtr, bmPtr->worldPts + 1, &bmPtr->axes);
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

    if (bmPtr->nWorldPts == 1) {
        anchorPos = Rbc_TranslatePoint(&corner1, destWidth, destHeight, bmPtr->anchor);
    } else {
        anchorPos = corner1;
    }
    anchorPos.x += bmPtr->xOffset;
    anchorPos.y += bmPtr->yOffset;

    /* Check if the bitmap sits at least partially in the plot area. */
    exts.left = anchorPos.x;
    exts.top = anchorPos.y;
    exts.right = anchorPos.x + destWidth - 1;
    exts.bottom = anchorPos.y + destHeight - 1;

    bmPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
    if (bmPtr->clipped) {
        return; /* Bitmap is offscreen. Don't generate
                 * rotated or scaled bitmaps. */
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
        bmPtr->destWidth = regionWidth;
        bmPtr->destHeight = regionHeight;
    } else {
        bmPtr->destWidth = srcWidth;
        bmPtr->destHeight = srcHeight;
        bmPtr->destBitmap = bmPtr->srcBitmap;
    }
    bmPtr->anchorPos = anchorPos;
    {
        double xScale, yScale;
        double tx, ty;
        double rotWidth, rotHeight;
        Point2D polygon[5];
        int n;

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
        n = Rbc_PolyRectClip(&exts, polygon, 4, bmPtr->outline);
        assert(n <= MAX_OUTLINE_POINTS);
        if (n < 3) {
            memcpy(&bmPtr->outline, polygon, sizeof(Point2D) * 4);
            bmPtr->nOutlinePts = 4;
        } else {
            bmPtr->nOutlinePts = n;
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
    BitmapMarker *bmPtr = (BitmapMarker *)markerPtr;

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
    BitmapMarker *bmPtr = (BitmapMarker *)markerPtr;

    if (bmPtr->nWorldPts < 1) {
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
    BitmapMarker *bmPtr = (BitmapMarker *)markerPtr;
    double theta;

    if ((bmPtr->destBitmap == None) || (bmPtr->destWidth < 1) || (bmPtr->destHeight < 1)) {
        return;
    }
    theta = FMOD(bmPtr->theta, (double)90.0);
    if ((bmPtr->fillColor == NULL) || (theta != 0.0)) {

        /*
         * If the bitmap is rotated and a filled background is
         * required, then a filled polygon is drawn before the
         * bitmap.
         */

        if (bmPtr->fillColor != NULL) {
            int i;
            XPoint polygon[MAX_OUTLINE_POINTS];

            for (i = 0; i < bmPtr->nOutlinePts; i++) {
                polygon[i].x = (short int)bmPtr->outline[i].x;
                polygon[i].y = (short int)bmPtr->outline[i].y;
            }
            XFillPolygon(graphPtr->display, drawable, bmPtr->fillGC, polygon, bmPtr->nOutlinePts, Convex,
                         CoordModeOrigin);
        }
        XSetClipMask(graphPtr->display, bmPtr->gc, bmPtr->destBitmap);
        XSetClipOrigin(graphPtr->display, bmPtr->gc, (int)bmPtr->anchorPos.x, (int)bmPtr->anchorPos.y);
    } else {
        XSetClipMask(graphPtr->display, bmPtr->gc, None);
        XSetClipOrigin(graphPtr->display, bmPtr->gc, 0, 0);
    }
    XCopyPlane(graphPtr->display, bmPtr->destBitmap, drawable, bmPtr->gc, 0, 0, bmPtr->destWidth, bmPtr->destHeight,
               (int)bmPtr->anchorPos.x, (int)bmPtr->anchorPos.y, 1);
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
    Graph *graphPtr = markerPtr->graphPtr;
    BitmapMarker *bmPtr = (BitmapMarker *)markerPtr;

    if (bmPtr->destBitmap == None) {
        return;
    }
    if (bmPtr->fillColor != NULL) {
        Rbc_BackgroundToPostScript(psToken, bmPtr->fillColor);
        Rbc_PolygonToPostScript(psToken, bmPtr->outline, 4);
    }
    Rbc_ForegroundToPostScript(psToken, bmPtr->outlineColor);

    Rbc_FormatToPostScript(psToken, "  gsave\n    %g %g translate\n    %d %d scale\n", bmPtr->anchorPos.x,
                           bmPtr->anchorPos.y + bmPtr->destHeight, bmPtr->destWidth, -bmPtr->destHeight);
    Rbc_FormatToPostScript(psToken, "    %d %d true [%d 0 0 %d 0 %d] {", bmPtr->destWidth, bmPtr->destHeight,
                           bmPtr->destWidth, -bmPtr->destHeight, bmPtr->destHeight);
    Rbc_BitmapDataToPostScript(psToken, graphPtr->display, bmPtr->destBitmap, bmPtr->destWidth, bmPtr->destHeight);
    Rbc_AppendToPostScript(psToken, "    } imagemask\n", "grestore\n", (char *)NULL);
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
    BitmapMarker *bmPtr = (BitmapMarker *)markerPtr;

    if (bmPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->gc);
    }
    if (bmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, bmPtr->fillGC);
    }
    if (bmPtr->destBitmap != bmPtr->srcBitmap) {
        Tk_FreePixmap(graphPtr->display, bmPtr->destBitmap);
    }
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
static Marker *CreateBitmapMarker() {
    BitmapMarker *bmPtr;

    bmPtr = RbcCalloc(1, sizeof(BitmapMarker));
    if (bmPtr != NULL) {
        bmPtr->classPtr = &bitmapMarkerClass;
    }
    return (Marker *)bmPtr;
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
    ImageMarker *imPtr = clientData;
    Tk_PhotoHandle photo;

    photo = Tk_FindPhoto(imPtr->graphPtr->interp, imPtr->imageName);
    if (photo != NULL) {
        if (imPtr->srcImage != NULL) {
            Rbc_FreeColorImage(imPtr->srcImage);
        }
        /* Convert the latest incarnation of the photo image back to a
         * color image that we can scale. */
        imPtr->srcImage = Rbc_PhotoToColorImage(photo);
    }
    imPtr->graphPtr->flags |= REDRAW_BACKING_STORE;
    imPtr->flags |= MAP_ITEM;
    Rbc_EventuallyRedrawGraph(imPtr->graphPtr);
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
    ImageMarker *imPtr = (ImageMarker *)markerPtr;
    Graph *graphPtr = markerPtr->graphPtr;

    if (Rbc_ConfigModified(graphPtr->interp, markerPtr->classPtr->configSpecs, "-image", (char *)NULL)) {
        Tcl_Interp *interp = graphPtr->interp;

        if (imPtr->tkImage != NULL) {
            Tk_FreeImage(imPtr->tkImage);
            imPtr->tkImage = NULL;
        }
        if (imPtr->imageName != NULL && imPtr->imageName[0] != '\0') {
            GC newGC;
            Tk_PhotoHandle photo;

            imPtr->tkImage = Tk_GetImage(interp, graphPtr->tkwin, imPtr->imageName, ImageChangedProc, imPtr);
            if (imPtr->tkImage == NULL) {
                Tcl_AppendResult(interp, "can't find an image \"", imPtr->imageName, "\"", (char *)NULL);
                ckfree((char *)imPtr->imageName);
                imPtr->imageName = NULL;
                return TCL_ERROR;
            }
            photo = Tk_FindPhoto(interp, imPtr->imageName);
            if (photo != NULL) {
                if (imPtr->srcImage != NULL) {
                    Rbc_FreeColorImage(imPtr->srcImage);
                }
                /* Convert the photo into a color image */
                imPtr->srcImage = Rbc_PhotoToColorImage(photo);
            }
            newGC = Tk_GetGC(graphPtr->tkwin, 0L, (XGCValues *)NULL);
            if (imPtr->gc != NULL) {
                Tk_FreeGC(graphPtr->display, imPtr->gc);
            }
            imPtr->gc = newGC;
        }
    }
    imPtr->flags |= MAP_ITEM;
    if (imPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
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
    ImageMarker *imPtr;
    Point2D anchorPos;
    Point2D corner1, corner2;
    int scaledWidth, scaledHeight;
    int srcWidth, srcHeight;

    imPtr = (ImageMarker *)markerPtr;
    if (imPtr->tkImage == NULL) {
        return;
    }
    graphPtr = imPtr->graphPtr;
    corner1 = MapPoint(graphPtr, imPtr->worldPts, &imPtr->axes);
    if (imPtr->srcImage == NULL) {
        /*
         * Don't scale or rotate non-photo images.
         */
        Tk_SizeOfImage(imPtr->tkImage, &srcWidth, &srcHeight);
        imPtr->width = srcWidth;
        imPtr->height = srcHeight;
        imPtr->anchorPos.x = corner1.x + imPtr->xOffset;
        imPtr->anchorPos.y = corner1.y + imPtr->yOffset;
        exts.left = imPtr->anchorPos.x;
        exts.top = imPtr->anchorPos.y;
        exts.right = exts.left + srcWidth - 1;
        exts.bottom = exts.top + srcHeight - 1;
        imPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
        return;
    }

    imPtr->width = srcWidth = Rbc_ColorImageWidth(imPtr->srcImage);
    imPtr->height = srcHeight = Rbc_ColorImageHeight(imPtr->srcImage);
    if ((srcWidth == 0) && (srcHeight == 0)) {
        imPtr->clipped = TRUE;
        return; /* Empty image. */
    }
    if (imPtr->nWorldPts > 1) {
        double hold;

        corner2 = MapPoint(graphPtr, imPtr->worldPts + 1, &imPtr->axes);
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

    if (imPtr->nWorldPts == 1) {
        anchorPos = Rbc_TranslatePoint(&corner1, scaledWidth, scaledHeight, imPtr->anchor);
    } else {
        anchorPos = corner1;
    }
    anchorPos.x += imPtr->xOffset;
    anchorPos.y += imPtr->yOffset;

    /* Check if the image sits at least partially in the plot area. */
    exts.left = anchorPos.x;
    exts.top = anchorPos.y;
    exts.right = anchorPos.x + scaledWidth - 1;
    exts.bottom = anchorPos.y + scaledHeight - 1;

    imPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
    if (imPtr->clipped) {
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
    ImageMarker *imPtr = (ImageMarker *)markerPtr;

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
    ImageMarker *imPtr = (ImageMarker *)markerPtr;

    if (imPtr->nWorldPts < 1) {
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
    ImageMarker *imPtr = (ImageMarker *)markerPtr;
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
            XCopyArea(imPtr->graphPtr->display, pixmap, drawable, imPtr->gc, 0, 0, width, height,
                      (int)imPtr->anchorPos.x, (int)imPtr->anchorPos.y);
        }
    } else {
        XCopyArea(imPtr->graphPtr->display, imPtr->pixmap, drawable, imPtr->gc, 0, 0, imPtr->width, imPtr->height,
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
    ImageMarker *imPtr = (ImageMarker *)markerPtr;
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
    ImageMarker *imPtr = (ImageMarker *)markerPtr;

    if (imPtr->pixmap != None) {
        Tk_FreePixmap(graphPtr->display, imPtr->pixmap);
    }
    if (imPtr->tkImage != NULL) {
        Tk_FreeImage(imPtr->tkImage);
    }
    if (imPtr->tmpImage != NULL) {
        Rbc_DestroyTemporaryImage(graphPtr->interp, imPtr->tmpImage);
    }
    if (imPtr->srcImage != NULL) {
        Rbc_FreeColorImage(imPtr->srcImage);
    }
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
static Marker *CreateImageMarker() {
    ImageMarker *imPtr;

    imPtr = RbcCalloc(1, sizeof(ImageMarker));
    if (imPtr != NULL) {
        imPtr->classPtr = &imageMarkerClass;
    }
    return (Marker *)imPtr;
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
    Graph *graphPtr = markerPtr->graphPtr;
    TextMarker *tmPtr = (TextMarker *)markerPtr;
    GC newGC;
    XGCValues gcValues;
    unsigned long gcMask;

    tmPtr->style.theta = FMOD(tmPtr->style.theta, 360.0);
    if (tmPtr->style.theta < 0.0) {
        tmPtr->style.theta += 360.0;
    }
    newGC = NULL;
    if (tmPtr->fillColor != NULL) {
        gcMask = GCForeground;
        gcValues.foreground = tmPtr->fillColor->pixel;
        newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    }
    if (tmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, tmPtr->fillGC);
    }
    tmPtr->fillGC = newGC;
    Rbc_ResetTextStyle(graphPtr->tkwin, &tmPtr->style);

    if (Rbc_ConfigModified(graphPtr->interp, tmPtr->classPtr->configSpecs, "-text", (char *)NULL)) {
        if (tmPtr->textPtr != NULL) {
            ckfree((char *)tmPtr->textPtr);
            tmPtr->textPtr = NULL;
        }
        tmPtr->width = tmPtr->height = 0;
        if (tmPtr->string != NULL) {
            register int i;
            double rotWidth, rotHeight;

            tmPtr->textPtr = Rbc_GetTextLayout(tmPtr->string, &tmPtr->style);
            Rbc_GetBoundingBox(tmPtr->textPtr->width, tmPtr->textPtr->height, tmPtr->style.theta, &rotWidth, &rotHeight,
                               tmPtr->outline);
            tmPtr->width = ROUND(rotWidth);
            tmPtr->height = ROUND(rotHeight);
            for (i = 0; i < 4; i++) {
                tmPtr->outline[i].x += ROUND(rotWidth * 0.5);
                tmPtr->outline[i].y += ROUND(rotHeight * 0.5);
            }
            tmPtr->outline[4].x = tmPtr->outline[0].x;
            tmPtr->outline[4].y = tmPtr->outline[0].y;
        }
    }
    tmPtr->flags |= MAP_ITEM;
    if (tmPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
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
    TextMarker *tmPtr = (TextMarker *)markerPtr;
    Extents2D exts;
    Point2D anchorPos;

    if (tmPtr->string == NULL) {
        return;
    }
    anchorPos = MapPoint(graphPtr, tmPtr->worldPts, &tmPtr->axes);
    anchorPos = Rbc_TranslatePoint(&anchorPos, tmPtr->width, tmPtr->height, tmPtr->anchor);
    anchorPos.x += tmPtr->xOffset;
    anchorPos.y += tmPtr->yOffset;
    /*
     * Determine the bounding box of the text and test to see if it
     * is at least partially contained within the plotting area.
     */
    exts.left = anchorPos.x;
    exts.top = anchorPos.y;
    exts.right = anchorPos.x + tmPtr->width - 1;
    exts.bottom = anchorPos.y + tmPtr->height - 1;
    tmPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
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
    TextMarker *tmPtr = (TextMarker *)markerPtr;

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
    TextMarker *tmPtr = (TextMarker *)markerPtr;

    if (tmPtr->nWorldPts < 1) {
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
    TextMarker *tmPtr = (TextMarker *)markerPtr;
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
    TextMarker *tmPtr = (TextMarker *)markerPtr;

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
    TextMarker *tmPtr = (TextMarker *)markerPtr;

    Rbc_FreeTextStyle(graphPtr->display, &tmPtr->style);
    if (tmPtr->textPtr != NULL) {
        ckfree((char *)tmPtr->textPtr);
    }
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
static Marker *CreateTextMarker() {
    TextMarker *tmPtr;

    tmPtr = RbcCalloc(1, sizeof(TextMarker));
    assert(tmPtr);

    tmPtr->classPtr = &textMarkerClass;
    Rbc_InitTextStyle(&tmPtr->style);
    tmPtr->style.anchor = TK_ANCHOR_NW;
    tmPtr->style.padLeft = tmPtr->style.padRight = 4;
    tmPtr->style.padTop = tmPtr->style.padBottom = 4;

    return (Marker *)tmPtr;
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
    Graph *graphPtr = markerPtr->graphPtr;
    WindowMarker *wmPtr = (WindowMarker *)markerPtr;
    Tk_Window tkwin;

    if (wmPtr->pathName == NULL) {
        return TCL_OK;
    }
    tkwin = Tk_NameToWindow(graphPtr->interp, wmPtr->pathName, graphPtr->tkwin);
    if (tkwin == NULL) {
        return TCL_ERROR;
    }
    if (Tk_Parent(tkwin) != graphPtr->tkwin) {
        Tcl_AppendResult(graphPtr->interp, "\"", wmPtr->pathName, "\" is not a child of \"",
                         Tk_PathName(graphPtr->tkwin), "\"", (char *)NULL);
        return TCL_ERROR;
    }
    if (tkwin != wmPtr->tkwin) {
        if (wmPtr->tkwin != NULL) {
            Tk_DeleteEventHandler(wmPtr->tkwin, StructureNotifyMask, ChildEventProc, wmPtr);
            Tk_ManageGeometry(wmPtr->tkwin, (Tk_GeomMgr *)0, (ClientData)0);
            Tk_UnmapWindow(wmPtr->tkwin);
        }
        Tk_CreateEventHandler(tkwin, StructureNotifyMask, ChildEventProc, wmPtr);
        Tk_ManageGeometry(tkwin, &winMarkerMgrInfo, wmPtr);
    }
    wmPtr->tkwin = tkwin;

    wmPtr->flags |= MAP_ITEM;
    if (wmPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
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
    WindowMarker *wmPtr = (WindowMarker *)markerPtr;
    Graph *graphPtr = markerPtr->graphPtr;
    Extents2D exts;
    int width, height;

    if (wmPtr->tkwin == (Tk_Window)NULL) {
        return;
    }
    wmPtr->anchorPos = MapPoint(graphPtr, wmPtr->worldPts, &wmPtr->axes);

    width = Tk_ReqWidth(wmPtr->tkwin);
    height = Tk_ReqHeight(wmPtr->tkwin);
    if (wmPtr->reqWidth > 0) {
        width = wmPtr->reqWidth;
    }
    if (wmPtr->reqHeight > 0) {
        height = wmPtr->reqHeight;
    }
    wmPtr->anchorPos = Rbc_TranslatePoint(&wmPtr->anchorPos, width, height, wmPtr->anchor);
    wmPtr->anchorPos.x += wmPtr->xOffset;
    wmPtr->anchorPos.y += wmPtr->yOffset;
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
    wmPtr->clipped = BoxesDontOverlap(graphPtr, &exts);
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
    WindowMarker *wmPtr = (WindowMarker *)markerPtr;

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
    WindowMarker *wmPtr = (WindowMarker *)markerPtr;

    if (wmPtr->nWorldPts < 1) {
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
    WindowMarker *wmPtr = (WindowMarker *)markerPtr;

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
    WindowMarker *wmPtr = (WindowMarker *)markerPtr;

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
    WindowMarker *wmPtr = (WindowMarker *)markerPtr;

    if (wmPtr->tkwin != NULL) {
        Tk_DeleteEventHandler(wmPtr->tkwin, StructureNotifyMask, ChildEventProc, wmPtr);
        Tk_ManageGeometry(wmPtr->tkwin, (Tk_GeomMgr *)0, (ClientData)0);
        Tk_DestroyWindow(wmPtr->tkwin);
    }
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
static Marker *CreateWindowMarker() {
    WindowMarker *wmPtr;

    wmPtr = RbcCalloc(1, sizeof(WindowMarker));
    if (wmPtr != NULL) {
        wmPtr->classPtr = &windowMarkerClass;
    }
    return (Marker *)wmPtr;
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
    WindowMarker *wmPtr = clientData;

    if (eventPtr->type == DestroyNotify) {
        wmPtr->tkwin = NULL;
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
    Marker *markerPtr = clientData;
    Graph *graphPtr;

    graphPtr = markerPtr->graphPtr;
    DestroyMarker(markerPtr);
    /*
     * Not really needed. We should get an Expose event when the
     * child window is unmapped.
     */
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
    Graph *graphPtr = markerPtr->graphPtr;
    LineMarker *lmPtr = (LineMarker *)markerPtr;
    Point2D *srcPtr, *endPtr;
    Segment2D *segments, *segPtr;
    Point2D p, q, next;
    Extents2D exts;

    lmPtr->nSegments = 0;
    if (lmPtr->segments != NULL) {
        ckfree((char *)lmPtr->segments);
    }
    if (lmPtr->nWorldPts < 2) {
        return; /* Too few points */
    }
    Rbc_GraphExtents(graphPtr, &exts);

    /*
     * Allow twice the number of world coordinates. The line will
     * represented as series of line segments, not one continous
     * polyline.  This is because clipping against the plot area may
     * chop the line into several disconnected segments.
     */
    segments = (Segment2D *)ckalloc(lmPtr->nWorldPts * sizeof(Segment2D));
    srcPtr = lmPtr->worldPts;
    p = MapPoint(graphPtr, srcPtr, &lmPtr->axes);
    p.x += lmPtr->xOffset;
    p.y += lmPtr->yOffset;

    segPtr = segments;
    for (srcPtr++, endPtr = lmPtr->worldPts + lmPtr->nWorldPts; srcPtr < endPtr; srcPtr++) {
        next = MapPoint(graphPtr, srcPtr, &lmPtr->axes);
        next.x += lmPtr->xOffset;
        next.y += lmPtr->yOffset;
        q = next;
        if (Rbc_LineRectClip(&exts, &p, &q)) {
            segPtr->p = p;
            segPtr->q = q;
            segPtr++;
        }
        p = next;
    }
    lmPtr->nSegments = segPtr - segments;
    lmPtr->segments = segments;
    lmPtr->clipped = (lmPtr->nSegments == 0);
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
    LineMarker *lmPtr = (LineMarker *)markerPtr;

    return Rbc_PointInSegments(samplePtr, lmPtr->segments, lmPtr->nSegments, (double)lmPtr->graphPtr->halo);
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
    LineMarker *lmPtr = (LineMarker *)markerPtr;

    if (lmPtr->nWorldPts < 2) {
        return FALSE;
    }
    if (enclosed) {
        Point2D p;
        Point2D *pointPtr, *endPtr;

        for (pointPtr = lmPtr->worldPts, endPtr = lmPtr->worldPts + lmPtr->nWorldPts; pointPtr < endPtr; pointPtr++) {
            p = MapPoint(lmPtr->graphPtr, pointPtr, &lmPtr->axes);
            if ((p.x < extsPtr->left) && (p.x > extsPtr->right) && (p.y < extsPtr->top) && (p.y > extsPtr->bottom)) {
                return FALSE;
            }
        }
        return TRUE; /* All points inside bounding box. */
    } else {
        Point2D p, q;
        int count;
        Point2D *pointPtr, *endPtr;

        count = 0;
        for (pointPtr = lmPtr->worldPts, endPtr = lmPtr->worldPts + (lmPtr->nWorldPts - 1); pointPtr < endPtr;
             pointPtr++) {
            p = MapPoint(lmPtr->graphPtr, pointPtr, &lmPtr->axes);
            q = MapPoint(lmPtr->graphPtr, pointPtr + 1, &lmPtr->axes);
            if (Rbc_LineRectClip(extsPtr, &p, &q)) {
                count++;
            }
        }
        return (count > 0); /* At least 1 segment passes through region. */
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
    LineMarker *lmPtr = (LineMarker *)markerPtr;

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
    Graph *graphPtr = markerPtr->graphPtr;
    LineMarker *lmPtr = (LineMarker *)markerPtr;
    GC newGC;
    XGCValues gcValues;
    unsigned long gcMask;
    Drawable drawable;

    drawable = Tk_WindowId(graphPtr->tkwin);
    gcMask = (GCLineWidth | GCLineStyle | GCCapStyle | GCJoinStyle);
    if (lmPtr->outlineColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = lmPtr->outlineColor->pixel;
    }
    if (lmPtr->fillColor != NULL) {
        gcMask |= GCBackground;
        gcValues.background = lmPtr->fillColor->pixel;
    }
    gcValues.cap_style = lmPtr->capStyle;
    gcValues.join_style = lmPtr->joinStyle;
    gcValues.line_width = LineWidth(lmPtr->lineWidth);
    gcValues.line_style = LineSolid;
    if (LineIsDashed(lmPtr->dashes)) {
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
        gcValues.foreground ^= pixel;
        if (drawable != None) {
            DrawLineMarker(markerPtr, drawable);
        }
    }
    newGC = Rbc_GetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (lmPtr->gc != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, lmPtr->gc);
    }
    if (LineIsDashed(lmPtr->dashes)) {
        Rbc_SetDashes(graphPtr->display, newGC, &lmPtr->dashes);
    }
    lmPtr->gc = newGC;
    if (lmPtr->xor) {
        if (drawable != None) {
            MapLineMarker(markerPtr);
            DrawLineMarker(markerPtr, drawable);
        }
        return TCL_OK;
    }
    lmPtr->flags |= MAP_ITEM;
    if (lmPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
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
    LineMarker *lmPtr = (LineMarker *)markerPtr;

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
    LineMarker *lmPtr = (LineMarker *)markerPtr;

    if (lmPtr->gc != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, lmPtr->gc);
    }
    if (lmPtr->segments != NULL) {
        ckfree((char *)lmPtr->segments);
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
        lmPtr->classPtr = &lineMarkerClass;
        lmPtr->xor = FALSE;
        lmPtr->capStyle = CapButt;
        lmPtr->joinStyle = JoinMiter;
    }
    return (Marker *)lmPtr;
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
    Graph *graphPtr = markerPtr->graphPtr;
    PolygonMarker *pmPtr = (PolygonMarker *)markerPtr;
    Point2D *srcPtr, *destPtr, *endPtr;
    Point2D *screenPts;
    Extents2D exts;
    int nScreenPts;

    if (pmPtr->outlinePts != NULL) {
        ckfree((char *)pmPtr->outlinePts);
        pmPtr->outlinePts = NULL;
        pmPtr->nOutlinePts = 0;
    }
    if (pmPtr->fillPts != NULL) {
        ckfree((char *)pmPtr->fillPts);
        pmPtr->fillPts = NULL;
        pmPtr->nFillPts = 0;
    }
    if (pmPtr->screenPts != NULL) {
        ckfree((char *)pmPtr->screenPts);
        pmPtr->screenPts = NULL;
    }
    if (pmPtr->nWorldPts < 3) {
        return; /* Too few points */
    }

    /*
     * Allocate and fill a temporary array to hold the screen
     * coordinates of the polygon.
     */
    nScreenPts = pmPtr->nWorldPts + 1;
    screenPts = (Point2D *)ckalloc((nScreenPts + 1) * sizeof(Point2D));
    endPtr = pmPtr->worldPts + pmPtr->nWorldPts;
    destPtr = screenPts;
    for (srcPtr = pmPtr->worldPts; srcPtr < endPtr; srcPtr++) {
        *destPtr = MapPoint(graphPtr, srcPtr, &pmPtr->axes);
        destPtr->x += pmPtr->xOffset;
        destPtr->y += pmPtr->yOffset;
        destPtr++;
    }
    *destPtr = screenPts[0];

    Rbc_GraphExtents(graphPtr, &exts);
    pmPtr->clipped = TRUE;
    if (pmPtr->fill.fgColor != NULL) { /* Polygon fill required. */
        Point2D *fillPts;
        int n;

        fillPts = (Point2D *)ckalloc(sizeof(Point2D) * nScreenPts * 3);
        assert(fillPts);
        n = Rbc_PolyRectClip(&exts, screenPts, pmPtr->nWorldPts, fillPts);
        if (n < 3) {
            ckfree((char *)fillPts);
        } else {
            pmPtr->nFillPts = n;
            pmPtr->fillPts = fillPts;
            pmPtr->clipped = FALSE;
        }
    }
    if ((pmPtr->outline.fgColor != NULL) && (pmPtr->lineWidth > 0)) {
        Segment2D *outlinePts;
        register Segment2D *segPtr;
        /*
         * Generate line segments representing the polygon outline.
         * The resulting outline may or may not be closed from
         * viewport clipping.
         */
        outlinePts = (Segment2D *)ckalloc(nScreenPts * sizeof(Segment2D));
        if (outlinePts == NULL) {
            return; /* Can't allocate point array */
        }
        /*
         * Note that this assumes that the point array contains an
         * extra point that closes the polygon.
         */
        segPtr = outlinePts;
        for (srcPtr = screenPts, endPtr = screenPts + (nScreenPts - 1); srcPtr < endPtr; srcPtr++) {
            segPtr->p = srcPtr[0];
            segPtr->q = srcPtr[1];
            if (Rbc_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
                segPtr++;
            }
        }
        pmPtr->nOutlinePts = segPtr - outlinePts;
        pmPtr->outlinePts = outlinePts;
        if (pmPtr->nOutlinePts > 0) {
            pmPtr->clipped = FALSE;
        }
    }
    pmPtr->screenPts = screenPts;
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
    PolygonMarker *pmPtr = (PolygonMarker *)markerPtr;

    if (pmPtr->nWorldPts < 2) {
        return FALSE;
    }
    return Rbc_PointInPolygon(samplePtr, pmPtr->screenPts, pmPtr->nWorldPts + 1);
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
    PolygonMarker *pmPtr = (PolygonMarker *)markerPtr;

    if (pmPtr->nWorldPts >= 3) {
        return Rbc_RegionInPolygon(extsPtr, pmPtr->screenPts, pmPtr->nWorldPts, enclosed);
    }
    return FALSE;
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
static void DrawPolygonMarker(Marker *markerPtr, Drawable drawable) {
    Graph *graphPtr = markerPtr->graphPtr;
    PolygonMarker *pmPtr = (PolygonMarker *)markerPtr;

    /* Draw polygon fill region */
    if ((pmPtr->nFillPts > 0) && (pmPtr->fill.fgColor != NULL)) {
        XPoint *destPtr, *pointArr;
        Point2D *srcPtr, *endPtr;

        pointArr = (XPoint *)ckalloc(pmPtr->nFillPts * sizeof(XPoint));
        if (pointArr == NULL) {
            return;
        }
        destPtr = pointArr;
        for (srcPtr = pmPtr->fillPts, endPtr = pmPtr->fillPts + pmPtr->nFillPts; srcPtr < endPtr; srcPtr++) {
            destPtr->x = (short int)srcPtr->x;
            destPtr->y = (short int)srcPtr->y;
            destPtr++;
        }
        XFillPolygon(graphPtr->display, drawable, pmPtr->fillGC, pointArr, pmPtr->nFillPts, Complex, CoordModeOrigin);
        ckfree((char *)pointArr);
    }
    /* and then the outline */
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
    PolygonMarker *pmPtr = (PolygonMarker *)markerPtr;

    if (pmPtr->fill.fgColor != NULL) {

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
    if ((pmPtr->lineWidth > 0) && (pmPtr->outline.fgColor != NULL)) {

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
    Graph *graphPtr = markerPtr->graphPtr;
    PolygonMarker *pmPtr = (PolygonMarker *)markerPtr;
    GC newGC;
    XGCValues gcValues;
    unsigned long gcMask;
    Drawable drawable;

    drawable = Tk_WindowId(graphPtr->tkwin);
    gcMask = (GCLineWidth | GCLineStyle);
    if (pmPtr->outline.fgColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = pmPtr->outline.fgColor->pixel;
    }
    if (pmPtr->outline.bgColor != NULL) {
        gcMask |= GCBackground;
        gcValues.background = pmPtr->outline.bgColor->pixel;
    }
    gcMask |= (GCCapStyle | GCJoinStyle);
    gcValues.cap_style = pmPtr->capStyle;
    gcValues.join_style = pmPtr->joinStyle;
    gcValues.line_style = LineSolid;
    gcValues.dash_offset = 0;
    gcValues.line_width = LineWidth(pmPtr->lineWidth);
    if (LineIsDashed(pmPtr->dashes)) {
        gcValues.line_style = (pmPtr->outline.bgColor == NULL) ? LineOnOffDash : LineDoubleDash;
    }
    if (pmPtr->xor) {
        unsigned long pixel;
        gcValues.function = GXxor;

        gcMask |= GCFunction;
        if (graphPtr->plotBg == NULL) {
            /* The graph's color option may not have been set yet */
            pixel = WhitePixelOfScreen(Tk_Screen(graphPtr->tkwin));
        } else {
            pixel = graphPtr->plotBg->pixel;
        }
        if (gcMask & GCBackground) {
            gcValues.background ^= pixel;
        }
        gcValues.foreground ^= pixel;
        if (drawable != None) {
            DrawPolygonMarker(markerPtr, drawable);
        }
    }
    newGC = Rbc_GetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (LineIsDashed(pmPtr->dashes)) {
        Rbc_SetDashes(graphPtr->display, newGC, &pmPtr->dashes);
    }
    if (pmPtr->outlineGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, pmPtr->outlineGC);
    }
    pmPtr->outlineGC = newGC;

    gcMask = 0;
    if (pmPtr->fill.fgColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = pmPtr->fill.fgColor->pixel;
    }
    if (pmPtr->fill.bgColor != NULL) {
        gcMask |= GCBackground;
        gcValues.background = pmPtr->fill.bgColor->pixel;
    }
    if (pmPtr->stipple != None) {
        gcValues.stipple = pmPtr->stipple;
        gcValues.fill_style = (pmPtr->fill.bgColor != NULL) ? FillOpaqueStippled : FillStippled;
        gcMask |= (GCStipple | GCFillStyle);
    }
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (pmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, pmPtr->fillGC);
    }
    pmPtr->fillGC = newGC;

    if ((gcMask == 0) && !(graphPtr->flags & RESET_AXES) && (pmPtr->xor)) {
        if (drawable != None) {
            MapPolygonMarker(markerPtr);
            DrawPolygonMarker(markerPtr, drawable);
        }
        return TCL_OK;
    }
    pmPtr->flags |= MAP_ITEM;
    if (pmPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
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
    PolygonMarker *pmPtr = (PolygonMarker *)markerPtr;

    if (pmPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, pmPtr->fillGC);
    }
    if (pmPtr->outlineGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, pmPtr->outlineGC);
    }
    if (pmPtr->fillPts != NULL) {
        ckfree((char *)pmPtr->fillPts);
    }
    if (pmPtr->outlinePts != NULL) {
        ckfree((char *)pmPtr->outlinePts);
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
static Marker *CreatePolygonMarker() {
    PolygonMarker *pmPtr;

    pmPtr = RbcCalloc(1, sizeof(PolygonMarker));
    if (pmPtr != NULL) {
        pmPtr->classPtr = &polygonMarkerClass;
        pmPtr->capStyle = CapButt;
        pmPtr->joinStyle = JoinMiter;
    }
    return (Marker *)pmPtr;
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
 *      char *name
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
static int NameToMarker(Graph *graphPtr, char *name, Marker **markerPtrPtr) {
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->markers.table, name);
    if (hPtr != NULL) {
        *markerPtrPtr = (Marker *)Tcl_GetHashValue(hPtr);
        return TCL_OK;
    }
    Tcl_AppendResult(graphPtr->interp, "can't find marker \"", name, "\" in \"", Tk_PathName(graphPtr->tkwin),
                     (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * RenameMarker --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Marker *markerPtr
 *      char *oldName
 *      char *newName
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int RenameMarker(Graph *graphPtr, Marker *markerPtr, char *oldName, char *newName) {
    int isNew;
    Tcl_HashEntry *hPtr;

    /* Rename the marker only if no marker already exists by that name */
    hPtr = Tcl_CreateHashEntry(&graphPtr->markers.table, newName, &isNew);
    if (!isNew) {
        Tcl_AppendResult(graphPtr->interp, "can't rename marker: \"", newName, "\" already exists", (char *)NULL);
        return TCL_ERROR;
    }
    markerPtr->name = RbcStrdup(newName);
    markerPtr->hashPtr = hPtr;
    Tcl_SetHashValue(hPtr, (char *)markerPtr);

    /* Delete the old hash entry */
    hPtr = Tcl_FindHashEntry(&graphPtr->markers.table, oldName);
    Tcl_DeleteHashEntry(hPtr);
    if (oldName != NULL) {
        ckfree((char *)oldName);
    }
    return TCL_OK;
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
static int NamesOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;
    Rbc_ChainLink *linkPtr;
    register int i;
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
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;

    if (NameToMarker(graphPtr, Tcl_GetString(objv[3]), &markerPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tk_ConfigureValue(interp, graphPtr->tkwin, markerPtr->classPtr->configSpecs, (char *)markerPtr,
                          Tcl_GetString(objv[4]), 0) != TCL_OK) {
        return TCL_ERROR;
    }
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
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;
    int flags = TK_CONFIG_ARGV_ONLY;
    char *oldName;
    int nNames, nOpts;
    Tcl_Obj *const *options;
    register int i;
    char *str;

    /* Figure out where the option value pairs begin */
    objc -= 3;
    objv += 3;
    for (i = 0; i < objc; i++) {
        str = Tcl_GetString(objv[i]);
        if (str[0] == '-') {
            break;
        }
        if (NameToMarker(graphPtr, str, &markerPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    nNames = i;              /* Number of element names specified */
    nOpts = objc - i;        /* Number of options specified */
    options = objv + nNames; /* Start of options in argv  */

    for (i = 0; i < nNames; i++) {
        str = Tcl_GetString(objv[i]);
        NameToMarker(graphPtr, str, &markerPtr);
        if (nOpts == 0) {
            return Tk_ConfigureInfo(interp, graphPtr->tkwin, markerPtr->classPtr->configSpecs, (char *)markerPtr,
                                    (char *)NULL, flags);
        } else if (nOpts == 1) {
            return Tk_ConfigureInfo(interp, graphPtr->tkwin, markerPtr->classPtr->configSpecs, (char *)markerPtr,
                                    Tcl_GetString(options[0]), flags);
        }
        /* Save the old marker. */
        oldName = markerPtr->name;
        if (Tk_ConfigureWidget(interp, graphPtr->tkwin, markerPtr->classPtr->configSpecs, nOpts, options,
                               (char *)markerPtr, flags) != TCL_OK) {
            return TCL_ERROR;
        }
        if (oldName != markerPtr->name) {
            if (RenameMarker(graphPtr, markerPtr, oldName, markerPtr->name) != TCL_OK) {
                markerPtr->name = oldName;
                return TCL_ERROR;
            }
        }
        if ((*markerPtr->classPtr->configProc)(markerPtr) != TCL_OK) {
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
static int CreateOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;
    Tcl_HashEntry *hPtr;
    int isNew;
    Rbc_Uid classUid;
    register int i;
    char *name;
    char string[200];
    Tcl_Size length;
    //    char c;

    static const struct markerTypeMap {
        const char *name;
        Rbc_Uid *uid;
    } typeMap[] = {{"text", &rbcTextMarkerUid},   {"line", &rbcLineMarkerUid},     {"polygon", &rbcPolygonMarkerUid},
                   {"image", &rbcImageMarkerUid}, {"bitmap", &rbcBitmapMarkerUid}, {"window", &rbcWindowMarkerUid},
                   {NULL, (Rbc_Uid *)0}};
    int index;

    if (Tcl_GetIndexFromObjStruct(interp, objv[3], typeMap, sizeof(struct markerTypeMap), "marker type", 0, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    classUid = *typeMap[index].uid;

    /*
        c = argv[3][0];
    //    Create the new marker based upon the given type
        if ((c == 't') && (strcmp(argv[3], "text") == 0)) {
        classUid = rbcTextMarkerUid;
        } else if ((c == 'l') && (strcmp(argv[3], "line") == 0)) {
        classUid = rbcLineMarkerUid;
        } else if ((c == 'p') && (strcmp(argv[3], "polygon") == 0)) {
        classUid = rbcPolygonMarkerUid;
        } else if ((c == 'i') && (strcmp(argv[3], "image") == 0)) {
        classUid = rbcImageMarkerUid;
        } else if ((c == 'b') && (strcmp(argv[3], "bitmap") == 0)) {
        classUid = rbcBitmapMarkerUid;
        } else if ((c == 'w') && (strcmp(argv[3], "window") == 0)) {
        classUid = rbcWindowMarkerUid;
        } else {
        Tcl_AppendResult(interp, "unknown marker type \"", argv[3],
                 "\": should be \"text\", \"line\", \"polygon\", \"bitmap\", \"image\", or \
    \"window\"", (char *)NULL);
        return TCL_ERROR;
        }
    */
    /* Scan for "-name" option. We need it for the component name */
    name = NULL;
    for (i = 4; i < objc; i += 2) {
        const char *str = Tcl_GetStringFromObj(objv[i], &length);
        //        length = strlen(argv[i]);
        if ((length > 1) && (strncmp(str, "-name", length) == 0)) {
            name = Tcl_GetString(objv[i + 1]);
            break;
        }
    }
    /* If no name was given for the marker, make up one. */
    if (name == NULL) {
        sprintf(string, "marker%d", graphPtr->nextMarkerId++);
        name = string;
    } else if (name[0] == '-') {
        Tcl_AppendResult(interp, "name of marker \"", name, "\" can't start with a '-'", (char *)NULL);
        return TCL_ERROR;
    }
    markerPtr = CreateMarker(graphPtr, name, classUid);
    if (Rbc_ConfigureWidgetComponent(interp, graphPtr->tkwin, name, markerPtr->classUid,
                                     markerPtr->classPtr->configSpecs, objc - 4, objv + 4, (char *)markerPtr,
                                     0) != TCL_OK) {
        DestroyMarker(markerPtr);
        return TCL_ERROR;
    }
    if ((*markerPtr->classPtr->configProc)(markerPtr) != TCL_OK) {
        DestroyMarker(markerPtr);
        return TCL_ERROR;
    }
    hPtr = Tcl_CreateHashEntry(&graphPtr->markers.table, name, &isNew);
    if (!isNew) {
        Marker *oldMarkerPtr;
        /*
         * Marker by the same name already exists.  Delete the old
         * marker and it's list entry.  But save the hash entry.
         */
        oldMarkerPtr = (Marker *)Tcl_GetHashValue(hPtr);
        oldMarkerPtr->hashPtr = NULL;
        DestroyMarker(oldMarkerPtr);
    }
    Tcl_SetHashValue(hPtr, markerPtr);
    markerPtr->hashPtr = hPtr;
    markerPtr->linkPtr = Rbc_ChainAppend(graphPtr->markers.displayList, markerPtr);
    if (markerPtr->drawUnder) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    Tcl_SetResult(interp, name, TCL_VOLATILE);
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
static int DeleteOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;
    register int i;

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
static int GetOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
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
static int RelinkOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
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
static int FindOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
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
        Tcl_AppendResult(interp, "bad search type \"", argv[3],
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
static int ExistsOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
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
static int TypeOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;

    if (NameToMarker(graphPtr, Tcl_GetString(objv[3]), &markerPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->classUid, -1));
    return TCL_OK;
}

/* Public routines */

static Rbc_OpSpec markerOps[] = {{"after", (Rbc_Op)RelinkOp, 4, 5, "marker ?afterMarker?"},
                                 {"before", (Rbc_Op)RelinkOp, 4, 5, "marker ?beforeMarker?"},
                                 {"bind", (Rbc_Op)BindOp, 3, 6, "marker sequence command"},
                                 {"cget", (Rbc_Op)CgetOp, 5, 5, "marker option"},
                                 {"configure", (Rbc_Op)ConfigureOp, 4, 0, "marker ?marker?... ?option value?..."},
                                 {"create", (Rbc_Op)CreateOp, 4, 0, "type ?option value?..."},
                                 {"delete", (Rbc_Op)DeleteOp, 3, 0, "?marker?..."},
                                 {"exists", (Rbc_Op)ExistsOp, 4, 4, "marker"},
                                 {"find", (Rbc_Op)FindOp, 8, 8, "enclosed|overlapping x1 y1 x2 y2"},
                                 {"get", (Rbc_Op)GetOp, 4, 4, "name"},
                                 {"names", (Rbc_Op)NamesOp, 3, 0, "?pattern?..."},
                                 {"type", (Rbc_Op)TypeOp, 4, 4, "marker"},
                                 RBC_OPSPEC_END};

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
int Rbc_MarkerOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Rbc_Op proc;
    int result;

    proc = Rbc_GetOpFromObj(interp, markerOps, RBC_OP_ARG2, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    result = (*proc)(graphPtr, interp, objc, objv);
    return result;
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
