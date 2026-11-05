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

/** @file rbcGrMarker.c
 * Shared marker services and concrete marker classes.
 */
/** @defgroup marker_configuration Marker common option transactions
 * Internal services shared by concrete marker classes.
 */
/** @defgroup marker_mapping_core Marker coordinate mapping and picking
 * Internal services shared by concrete marker classes.
 */
/** @defgroup marker_lifecycle_core Marker record and resource lifecycle
 * Internal services shared by concrete marker classes.
 */
/** @defgroup marker_commands Marker registry and command dispatch
 * Internal services shared by concrete marker classes.
 */

/** @defgroup marker_line_geometry Line marker geometry and lifetime
 * Internal geometric marker implementation.
 */

/** @defgroup marker_shape_output Geometric marker renderer dispatch
 * Internal geometric marker implementation.
 */

/** @defgroup marker_polygon_geometry Polygon and arc geometry and ownership
 * Internal geometric marker implementation.
 */

/** @defgroup marker_bitmap Bitmap marker resources and presentation
 * Internal concrete marker implementation.
 */

/** @defgroup marker_image Image marker resources and presentation
 * Internal concrete marker implementation.
 */

/** @defgroup marker_text Text marker layout and presentation
 * Internal concrete marker implementation.
 */

/** @defgroup marker_window Window marker geometry management and callbacks
 * Internal concrete marker implementation.
 */

/** @defgroup marker_batches Marker pass batching and renderer dispatch
 * Internal style predicates, synchronous runs and clipping.
 */

#include <float.h>
#include "rbcGraph.h"
#include "rbcGrTrack.h"
#include "rbcChain.h"
#include "rbcGrAxis.h"
#include "rbcGrElem.h"
#include "rbcRender.h"

#define MAX_OUTLINE_POINTS RBC_RENDER_BITMAP_OUTLINE_POINTS

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
#define DEF_MARKER_ARROW "none"
#define DEF_MARKER_ARROW_SHAPE "8 10 3"
#define PTS_IN_ARROW RBC_RENDER_ARROW_POINTS
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
typedef void(MarkerExportProc)(Marker *markerPtr, Rbc_ExportContext *exportPtr);
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
    MarkerExportProc *exportProc;
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
    Point2D *worldPts;  /* Coordinate array to position marker */
    Tcl_Size nWorldPts; /* Number of points in above array */
    char *elemName;     /* Element associated with marker */
    Axis2D axes;
    RbcTrack *track;       /* Optional owner; prevents track/axis detachment. */
    Tcl_Obj *trackObjPtr;
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

/**
 * @ingroup marker_text
 * @brief Text options, derived layout and anchor-local rotated outline.
 *
 * Tk options own font/string/foreground/background; the class owns layout,
 * GCs and shadow color. The marker anchor is distinct from the internal
 * north-west text-style anchor. outline is translated by anchorPos at use.
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
    {TK_OPTION_STRING, "-track", "track", "Track", "", offsetof(Marker, trackObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},
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
     offsetof(Marker, xOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/**
 * @ingroup marker_window
 * @brief Embedded child token and requested/mapped geometry.
 *
 * Geometry management and structure notifications retain this record as
 * client data. Zero requested dimensions mean automatic size. Deleting the
 * marker destroys its current child; replacing the child only detaches it.
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
    {TK_OPTION_STRING, "-track", "track", "Track", "", offsetof(Marker, trackObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME, -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},
    {TK_OPTION_STRING, "-width", "width", "Width", DEF_MARKER_WIDTH, offsetof(WindowMarker, reqWidthObjPtr), -1, 0,
     NULL, WINDOW_WIDTH_CHANGED},
    {TK_OPTION_STRING, "-window", "window", "Window", DEF_MARKER_WINDOW, -1, offsetof(WindowMarker, pathName),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffsetObjPtr),
     offsetof(Marker, xOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/**
 * @ingroup marker_bitmap
 * @brief Bitmap source references and explicitly owned mapped pixmaps.
 *
 * Tk options own srcBitmap/srcMask and configured colors. Destination handles
 * may alias source handles or each other; ownership flags govern release.
 * outline is an absolute mapped background polygon, not anchor-local geometry.
 */
typedef struct {
    Marker core;
    /* Bitmap specific attributes */
    Pixmap srcBitmap;     /* Original bitmap. May be further
                           * scaled or rotated. */
    Pixmap srcMask;       /* User-specified transparency mask */
    double rotate;        /* Requested rotation of the bitmap */
    double theta;         /* Normalized rotation (0..360
                           * degrees) */
    Tk_Anchor anchor;     /* If only one X-Y coordinate is
                           * given, indicates how to translate
                           * the given marker position.  Otherwise,
                           * if there are two X-Y coordinates, then
                           * this value is ignored. */
    Point2D anchorPos;    /* Translated anchor point. */
    XColor *outlineColor; /* Foreground color */
    XColor *fillColor;    /* Background color */
    GC gc;                /* Private graphic context */
    GC fillGC;            /* Shared graphic context */
    Pixmap destBitmap;    /* Bitmap to be drawn. */
    Pixmap destMask;      /* Final scaled/rotated clip mask */
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
    {TK_OPTION_STRING, "-track", "track", "Track", "", offsetof(Marker, trackObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_BITMAP, "-mask", "mask", "Mask", DEF_MARKER_MASK, -1, offsetof(BitmapMarker, srcMask), TK_OPTION_NULL_OK,
     NULL, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME, -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_SYNONYM, "-outline", "foreground", NULL, NULL, 0, -1, 0, (ClientData) "-foreground", 0},
    {TK_OPTION_DOUBLE, "-rotate", "rotate", "Rotate", DEF_MARKER_ROTATE, -1, offsetof(BitmapMarker, rotate), 0, NULL,
     0},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffsetObjPtr),
     offsetof(Marker, xOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/**
 * @ingroup marker_image
 * @brief Source image instance and owned photo/scaled-image caches.
 *
 * The application owns the named source image. The marker owns its Tk
 * instance, source color-image copy, temporary scaled image and drawing
 * resources. Cached dimensions describe current mapped presentation.
 */
typedef struct {
    Marker core;
    /* Image specific attributes */
    Tcl_Obj *imageObjPtr; /* Original Tcl representation of -image. */
    Tk_Image tkImage;     /* Tk image to be displayed. */
    Tk_Anchor anchor;     /* Indicates how to translate the given
                           * marker position. */
    Point2D anchorPos;    /* Translated anchor point. */
    int width, height;    /* Dimensions of the image */
    Tk_Image tmpImage;
    Pixmap pixmap;         /* Pixmap containing the scaled image */
    Rbc_ColorImage srcImage;
    GC gc;

} ImageMarker;

typedef struct {
    char **tags;
    Point2D *worldPts;
    Tcl_Size nWorldPts;
    Axis *xAxis;
    Axis *yAxis;
    RbcTrack *track;
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
    {TK_OPTION_STRING, "-track", "track", "Track", "", offsetof(Marker, trackObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME, -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffsetObjPtr),
     offsetof(Marker, xOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/*
 * -------------------------------------------------------------------
 *
 * LineMarker --
 *
 * -------------------------------------------------------------------
 */
typedef enum { LINE_ARROW_NONE, LINE_ARROW_FIRST, LINE_ARROW_LAST, LINE_ARROW_BOTH } LineArrow;

static const char *const arrowStrings[] = {"none", "first", "last", "both", NULL};

/**
 * @ingroup marker_line_geometry
 * @brief Line-specific option state, owned segment cache and fixed arrow arrays.
 *
 * Common Marker is embedded first. Tk options own Tcl representations and
 * colors; class cleanup owns the private GC and segment allocation. Arrow
 * arrays have five unique vertices plus a repeated tip. xorState records an
 * immediate graph-wide image, not the current requested xor option alone.
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
    Tcl_Obj *arrowShapeObjPtr;
    /* Line specific attributes */
    XColor *fillColor;
    XColor *outlineColor; /* Foreground and background colors */
    int lineWidth;        /* Line width. */
    int capStyle;         /* Cap style. */
    int joinStyle;        /* Join style.*/
    Rbc_Dashes dashes;    /* Dash list values (max 11) */
    LineArrow arrow;
    double arrowShape[3];
    /*
     * Mapped arrowhead geometry.  The first and last point are
     * identical so the polygon is explicitly closed.
     */
    Point2D firstArrow[PTS_IN_ARROW];
    Point2D lastArrow[PTS_IN_ARROW];
    Point2D firstShaftPoint;
    Point2D lastShaftPoint;
    int hasFirstArrow;
    int hasLastArrow;
    GC gc;               /* Private graphic context */
    Segment2D *segments; /* Malloc'ed array of points.
                          * Represents individual line segments
                          * (2 points per segment) comprising
                          * the mapped line.  The segments may
                          * not necessarily be connected after
                          * clipping. */
    Tcl_Size nSegments;  /* # segments in the above array. */
    int xor ;
    int xorState; /* State of the XOR drawing. Indicates
                   * if the marker is currently drawn. */
} LineMarker;

static const Tk_OptionSpec lineMarkerOptionSpecs[] = {
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_MARKER_LINE_TAGS, offsetof(Marker, bindTagsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING_TABLE, "-arrow", "arrow", "Arrow", DEF_MARKER_ARROW, -1, offsetof(LineMarker, arrow), 0,
     (ClientData)arrowStrings, 0},
    {TK_OPTION_STRING, "-arrowshape", "arrowShape", "ArrowShape", DEF_MARKER_ARROW_SHAPE,
     offsetof(LineMarker, arrowShapeObjPtr), -1, 0, NULL, 0},
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
    {TK_OPTION_STRING, "-track", "track", "Track", "", offsetof(Marker, trackObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-name", NULL, NULL, DEF_MARKER_NAME, -1, offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_COLOR, "-outline", "outline", "Outline", DEF_MARKER_OUTLINE_COLOR, -1,
     offsetof(LineMarker, outlineColor), TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-xoffset", "xOffset", "XOffset", DEF_MARKER_X_OFFSET, offsetof(Marker, xOffsetObjPtr),
     offsetof(Marker, xOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_BOOLEAN, "-xor", "xor", "Xor", DEF_MARKER_XOR, -1, offsetof(LineMarker, xor), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-yoffset", "yOffset", "YOffset", DEF_MARKER_Y_OFFSET, offsetof(Marker, yOffsetObjPtr),
     offsetof(Marker, yOffset), TK_OPTION_NEG_OK, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/**
 * @ingroup marker_polygon_geometry
 * @brief Shared polygon/arc storage with separately owned mapped caches.
 *
 * screenPts retains full mapped geometry; fillPts and outlinePts are clipped
 * output caches. Polygon vertex count is core.nWorldPts; arc count is nArcPts
 * while its world input remains two corners. arc retains exact curve geometry
 * for curved renderers. Color pairs and GCs are class-owned; Tk options own
 * their Tcl representations and stipple reference.
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
    Rbc_Dashes dashes;     /* List of dash values.  Indicates how
                            * draw the dashed line.  If no dash
                            * values are provided, or the first value
                            * is zero, then the line is drawn solid. */
    GC outlineGC;          /* Graphics context to draw the outline of
                            * the polygon. */
    GC fillGC;             /* Graphics context to draw the filled
                            * polygon. */
    Point2D *fillPts;      /* Malloc'ed array of points used to draw
                            * the filled polygon. These points may
                            * form a degenerate polygon after clipping.
                            */
    Tcl_Size nFillPts;     /* # points in the above array. */
    Segment2D *outlinePts; /* Malloc'ed array of points.
                            * Represents individual line segments
                            * (2 points per segment) comprising
                            * the outline of the polygon.  The
                            * segments may not necessarily be
                            * closed or connected after clipping. */
    Tcl_Size nOutlinePts;  /* # points in the above array. */
    int xor ;
    int xorState; /* State of the XOR drawing. Indicates
                   * if the marker is visible. We have
                   * to drawn it again to erase it. */
    /* Arc markers share polygon appearance/resources, but have their own
     * mapped vertex count: their world coordinates remain a two-point box. */
    double start, extent;
    int arcStyle;
    Tcl_Size nArcPts;
    Rbc_RenderArcGeometry arc; /* Unclipped ellipse for curved renderers. */
} PolygonMarker;

enum { ARC_OPEN = RBC_RENDER_ARC_OPEN, ARC_CHORD = RBC_RENDER_ARC_CHORD, ARC_PIESLICE = RBC_RENDER_ARC_PIESLICE };
static const char *const arcStyles[] = {"arc", "chord", "pieslice", NULL};

#define POLYGON_MARKER_OPTION_ENTRIES(FILL_DEFAULT, OUTLINE_DEFAULT, TAGS_DEFAULT)                                       \
    {TK_OPTION_STRING,  "-bindtags", "bindTags", "BindTags", TAGS_DEFAULT, offsetof(Marker, bindTagsObjPtr), -1,         \
     TK_OPTION_NULL_OK, NULL,        0},                                                                                 \
        {TK_OPTION_STRING,                                                                                               \
         "-cap",                                                                                                         \
         "cap",                                                                                                          \
         "Cap",                                                                                                          \
         DEF_MARKER_CAP_STYLE,                                                                                           \
         offsetof(PolygonMarker, capObjPtr),                                                                             \
         -1,                                                                                                             \
         0,                                                                                                              \
         NULL,                                                                                                           \
         0},                                                                                                             \
        {TK_OPTION_STRING,  "-coords", "coords", "Coords", DEF_MARKER_COORDS, offsetof(Marker, coordsObjPtr), -1,        \
         TK_OPTION_NULL_OK, NULL,      0},                                                                               \
        {TK_OPTION_STRING,  "-dashes", "dashes", "Dashes", DEF_MARKER_DASHES, offsetof(PolygonMarker, dashesObjPtr), -1, \
         TK_OPTION_NULL_OK, NULL,      0},                                                                               \
        {TK_OPTION_STRING,           "-element",        "element", "Element", DEF_MARKER_ELEMENT, -1,                    \
         offsetof(Marker, elemName), TK_OPTION_NULL_OK, NULL,      0},                                                   \
        {TK_OPTION_STRING,  "-fill", "fill", "Fill", FILL_DEFAULT, offsetof(PolygonMarker, fillObjPtr), -1,              \
         TK_OPTION_NULL_OK, NULL,    0},                                                                                 \
        {TK_OPTION_STRING,                                                                                               \
         "-join",                                                                                                        \
         "join",                                                                                                         \
         "Join",                                                                                                         \
         DEF_MARKER_JOIN_STYLE,                                                                                          \
         offsetof(PolygonMarker, joinObjPtr),                                                                            \
         -1,                                                                                                             \
         0,                                                                                                              \
         NULL,                                                                                                           \
         0},                                                                                                             \
        {TK_OPTION_STRING,                                                                                               \
         "-linewidth",                                                                                                   \
         "lineWidth",                                                                                                    \
         "LineWidth",                                                                                                    \
         DEF_MARKER_LINE_WIDTH,                                                                                          \
         offsetof(PolygonMarker, lineWidthObjPtr),                                                                       \
         -1,                                                                                                             \
         0,                                                                                                              \
         NULL,                                                                                                           \
         0},                                                                                                             \
        {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_MARKER_HIDE, -1, offsetof(Marker, hidden), 0, NULL, 0},         \
        {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_MARKER_MAP_X, offsetof(Marker, mapXObjPtr), -1, 0, NULL, 0},     \
        {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_MARKER_MAP_Y, offsetof(Marker, mapYObjPtr), -1, 0, NULL, 0},     \
        {TK_OPTION_STRING,  "-track", "track", "Track", "", offsetof(Marker, trackObjPtr), -1,                           \
         TK_OPTION_NULL_OK, NULL,     0},                                                                                \
        {TK_OPTION_STRING,       "-name",           NULL, NULL, DEF_MARKER_NAME, -1,                                     \
         offsetof(Marker, name), TK_OPTION_NULL_OK, NULL, 0},                                                            \
        {TK_OPTION_STRING,                                                                                               \
         "-outline",                                                                                                     \
         "outline",                                                                                                      \
         "Outline",                                                                                                      \
         OUTLINE_DEFAULT,                                                                                                \
         offsetof(PolygonMarker, outlineObjPtr),                                                                         \
         -1,                                                                                                             \
         TK_OPTION_NULL_OK,                                                                                              \
         NULL,                                                                                                           \
         0},                                                                                                             \
        {TK_OPTION_STRING, "-state", "state", "State", DEF_MARKER_STATE, offsetof(Marker, stateObjPtr), -1, 0, NULL, 0}, \
        {TK_OPTION_BITMAP,  "-stipple", "stipple", "Stipple", DEF_MARKER_STIPPLE, -1, offsetof(PolygonMarker, stipple),  \
         TK_OPTION_NULL_OK, NULL,       0},                                                                              \
        {TK_OPTION_BOOLEAN, "-under", "under", "Under", DEF_MARKER_UNDER, -1, offsetof(Marker, drawUnder), 0, NULL, 0},  \
        {TK_OPTION_PIXELS,                                                                                               \
         "-xoffset",                                                                                                     \
         "xOffset",                                                                                                      \
         "XOffset",                                                                                                      \
         DEF_MARKER_X_OFFSET,                                                                                            \
         offsetof(Marker, xOffsetObjPtr),                                                                                \
         offsetof(Marker, xOffset),                                                                                      \
         TK_OPTION_NEG_OK,                                                                                               \
         NULL,                                                                                                           \
         0},                                                                                                             \
        {TK_OPTION_BOOLEAN, "-xor", "xor", "Xor", DEF_MARKER_XOR, -1, offsetof(PolygonMarker, xor), 0, NULL, 0},         \
        {TK_OPTION_PIXELS,                                                                                               \
         "-yoffset",                                                                                                     \
         "yOffset",                                                                                                      \
         "YOffset",                                                                                                      \
         DEF_MARKER_Y_OFFSET,                                                                                            \
         offsetof(Marker, yOffsetObjPtr),                                                                                \
         offsetof(Marker, yOffset),                                                                                      \
         TK_OPTION_NEG_OK,                                                                                               \
         NULL,                                                                                                           \
         0}

static const Tk_OptionSpec polygonMarkerOptionSpecs[] = {
    POLYGON_MARKER_OPTION_ENTRIES(DEF_MARKER_FILL_COLOR, DEF_MARKER_OUTLINE_COLOR, DEF_MARKER_POLYGON_TAGS),
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

static const Tk_OptionSpec polygonMarkerMonoOptionSpecs[] = {
    POLYGON_MARKER_OPTION_ENTRIES(DEF_MARKER_FILL_MONO, DEF_MARKER_OUTLINE_MONO, DEF_MARKER_POLYGON_TAGS),
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

#define ARC_MARKER_OPTION_ENTRIES                                                                                      \
    {TK_OPTION_DOUBLE, "-start", "start", "Start", "0", -1, offsetof(PolygonMarker, start), 0, NULL, 0},               \
        {TK_OPTION_DOUBLE, "-extent", "extent", "Extent", "360", -1, offsetof(PolygonMarker, extent), 0, NULL, 0},     \
        {TK_OPTION_STRING_TABLE, "-style", "style", "Style", "pieslice", -1, offsetof(PolygonMarker, arcStyle), 0,     \
         (ClientData)arcStyles,  0},                                                                                   \
        {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}

static const Tk_OptionSpec arcMarkerOptionSpecs[] = {
    POLYGON_MARKER_OPTION_ENTRIES(DEF_MARKER_FILL_COLOR, DEF_MARKER_OUTLINE_COLOR, "Arc all"),
    ARC_MARKER_OPTION_ENTRIES};
static const Tk_OptionSpec arcMarkerMonoOptionSpecs[] = {
    POLYGON_MARKER_OPTION_ENTRIES(DEF_MARKER_FILL_MONO, DEF_MARKER_OUTLINE_MONO, "Arc all"), ARC_MARKER_OPTION_ENTRIES};
#undef ARC_MARKER_OPTION_ENTRIES
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

static void StrokeArcMarker(PolygonMarker *pmPtr, Drawable drawable, Rbc_ExportContext *exportPtr);
static void DrawImmediatePolygonOutline(PolygonMarker *pmPtr, Drawable drawable);
static MarkerCreateProc CreateArcMarker;
static MarkerMapProc MapArcMarker;
static MarkerPointProc PointInArcMarker;
static MarkerRegionProc RegionInArcMarker;
static MarkerDrawProc DrawArcMarker;
static MarkerExportProc ArcMarkerExport;

static MarkerCreateProc CreateBitmapMarker, CreateLineMarker, CreateImageMarker, CreatePolygonMarker, CreateTextMarker,
    CreateWindowMarker;
static MarkerDrawProc DrawBitmapMarker, DrawLineMarker, DrawImageMarker, DrawPolygonMarker, DrawTextMarker,
    DrawWindowMarker;
static MarkerFreeProc FreeBitmapMarker, FreeLineMarker, FreeImageMarker, FreePolygonMarker, FreeTextMarker,
    FreeWindowMarker;
static MarkerConfigProc ConfigureBitmapMarker, ConfigureLineMarker, ConfigureImageMarker, ConfigurePolygonMarker,
    ConfigureTextMarker, ConfigureWindowMarker;
static MarkerMapProc MapBitmapMarker, MapLineMarker, MapImageMarker, MapPolygonMarker, MapTextMarker, MapWindowMarker;
static MarkerExportProc BitmapMarkerExport, LineMarkerExport, ImageMarkerExport,
    PolygonMarkerExport, TextMarkerExport, WindowMarkerExport;
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
    .exportProc = BitmapMarkerExport,
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
    .exportProc = ImageMarkerExport,
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
    .exportProc = LineMarkerExport,
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
    .exportProc = PolygonMarkerExport,
};

static MarkerClass arcMarkerClass = {
    .optionSpecs = arcMarkerOptionSpecs,
    .monoOptionSpecs = arcMarkerMonoOptionSpecs,
    .configProc = ConfigurePolygonMarker,
    .drawProc = DrawArcMarker,
    .freeProc = FreePolygonMarker,
    .mapProc = MapArcMarker,
    .pointProc = PointInArcMarker,
    .regionProc = RegionInArcMarker,
    .exportProc = ArcMarkerExport,
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
    .exportProc = TextMarkerExport,
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
    .exportProc = WindowMarkerExport,
};

/**
 * @ingroup marker_configuration
 * @brief Initialize the concrete Tk option record while preserving the chosen name.
 *
 * Chooses monochrome specifications when supplied on a one-bit display. Uses
 * a normalized component resource name and class identity. Temporarily detaches
 * the owned preliminary name so partial Tk cleanup cannot free it. Returns
 * TCL_OK with initialized options, or TCL_ERROR after releasing partial Tk
 * resources and restoring the original name for caller cleanup.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 */
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
    componentName = Rbc_GetComponentResourceName(initialName);
    result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, componentName,
                                      markerPtr->classUid, (char *)markerPtr, markerPtr->optionTable);
    ckfree(componentName);
    if (result != TCL_OK) {
        /* The partial record owns any -name default as well as its other
         * Tk options. Restore the command's original name only after cleanup. */
        Tk_FreeConfigOptions((char *)markerPtr, markerPtr->optionTable, graphPtr->tkwin);
        Tk_DeleteOptionTable(markerPtr->optionTable);
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

/**
 * @ingroup marker_configuration
 * @brief Apply one marker option transaction and commit a rename last.
 *
 * Requires initialized options. Installs candidate Tk fields, validates a
 * nonempty name not starting with a dash, and rejects rename collisions when
 * not creating. Passes optionMask to the class configuration callback and
 * clears it afterwards. On error restores saved Tk fields and diagnostic;
 * the class owns rollback of derived state. On success commits registry rename
 * and frees saved options. Creation permits replacement, handled by CreateOp.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in] objc Even number of option/value entries in objv.
 * @param[in] objv Borrowed even-length option/value vector, excluding widget and operation names.
 * @param[in] creating Nonzero permits creation-time name replacement; zero enforces rename collision checks.
 */
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
        return Rbc_RestoreOptionsOnError(graphPtr->interp, &savedOptions);
    }
    if (newName[0] == '-') {
        Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("name of marker \"%s\" can't start with a '-'", newName));
        return Rbc_RestoreOptionsOnError(graphPtr->interp, &savedOptions);
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
            return Rbc_RestoreOptionsOnError(graphPtr->interp, &savedOptions);
        }
    }
    markerPtr->optionMask = mask;
    if ((*markerPtr->classPtr->configProc)(markerPtr) != TCL_OK) {
        markerPtr->optionMask = 0;
        return Rbc_RestoreOptionsOnError(graphPtr->interp, &savedOptions);
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

/**
 * @ingroup marker_lifecycle_core
 * @brief Release one initialized marker before the graph window is lost.
 *
 * Guards uninitialized/already released records. Calls concrete cleanup before
 * Tk frees option resources, then frees common coordinates/tags, drops axes,
 * frees options/table and sets the release guard. Requires a live graph window.
 * Registry links and record remain for final destruction. Class cleanup can
 * release images or destroy a managed child; this is not just scalar cleanup.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 */
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
    Tk_DeleteOptionTable(markerPtr->optionTable);
    markerPtr->optionTable = NULL;
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

/**
 * @ingroup marker_mapping_core
 * @brief Reject disjoint inclusive marker and current plot rectangles.
 *
 * Requires ordered extents and graph plot bounds, checked with assertions.
 * Touching edges count as overlap. Returns true only for disjoint rectangles;
 * does not clip partial overlaps or map coordinates.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] extsPtr Borrowed ordered rectangle in screen coordinates.
 */
static int BoxesDontOverlap(Graph *graphPtr, Extents2D *extsPtr) {
    assert(extsPtr->right >= extsPtr->left);
    assert(extsPtr->bottom >= extsPtr->top);
    assert(graphPtr->right >= graphPtr->left);
    assert(graphPtr->bottom >= graphPtr->top);
    return (((double)graphPtr->right < extsPtr->left) || ((double)graphPtr->bottom < extsPtr->top) ||
            (extsPtr->right < (double)graphPtr->left) || (extsPtr->bottom < (double)graphPtr->top));
}

/**
 * @ingroup marker_configuration
 * @brief Parse an optional tag list into one owned allocation.
 *
 * Initializes output to NULL. NULL/empty input succeeds without storage.
 * Tcl_SplitList owns both the returned pointer array and its strings in one
 * allocation; the caller frees it with ckfree. Returns TCL_ERROR on list errors.
 *
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed Tcl option value; accepted empty/NULL cases are described above.
 * @param[out] tagsPtrPtr Required output initialized to NULL; success transfers one ckfree-compatible tag
 *   allocation.
 */
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

/**
 * @ingroup marker_configuration
 * @brief Parse an exact normal, active or disabled marker state.
 *
 * Borrows the non-NULL object. Writes statePtr only on success; otherwise
 * returns TCL_ERROR with a diagnostic. Does not change drawing or binding state.
 *
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed Tcl option value; accepted empty/NULL cases are described above.
 * @param[out] statePtr Required output receiving the state selector only on success.
 */
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

/**
 * @ingroup marker_configuration
 * @brief Stage an owned coordinate array with class-specific point counts.
 *
 * Initializes outputs to NULL/zero and accepts empty coordinates for every
 * class. Nonempty lists must contain pairs and satisfy the class count limits.
 * Checks allocation size and uses Tcl_AttemptAlloc. Each component is parsed
 * through GetCoordinate, which can evaluate Tcl. Returns TCL_OK and transfers
 * the array, or TCL_ERROR after freeing it. Borrows marker and list storage
 * throughout; does not protect against callback mutation of owners/arguments.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in] objPtr Borrowed Tcl option value; accepted empty/NULL cases are described above.
 * @param[out] worldPtsPtr Required output initialized to NULL; success transfers an owned coordinate array.
 * @param[out] nWorldPtsPtr Required output initialized to zero; receives the number of coordinate pairs.
 */
static int GetMarkerCoordinatesFromObj(Marker *markerPtr, Tcl_Obj *objPtr, Point2D **worldPtsPtr,
                                       Tcl_Size *nWorldPtsPtr) {
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
    } else if (markerPtr->classUid == rbcArcMarkerUid) {
        minArgs = maxArgs = 4;
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

/**
 * @ingroup marker_configuration
 * @brief Release candidate common resources that have not been transferred.
 *
 * Frees tag/coordinate allocations and drops candidate axis references,
 * clearing those pointers/counts. Track is borrowed and not released. Accepts
 * a zeroed or partially populated candidate; does not free the candidate record.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] optionsPtr Caller-owned common-option candidate; resource transfer and cleanup rules are
 *   described above.
 */
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

/**
 * @ingroup marker_configuration
 * @brief Prepare common tags, coordinates, state, axes and track association.
 *
 * Zeros the caller-owned candidate, then parses all common fields and
 * acquires axis references. A nonempty track must name an existing trackgraph
 * track, and Y-axis ownership must equal the selected track (including NULL).
 * Returns TCL_OK with staged ownership or TCL_ERROR after candidate cleanup.
 * Coordinate expressions can execute Tcl; live records are not preserved here.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[out] optionsPtr Fresh caller-owned candidate, zeroed here; success requires commit or cleanup.
 */
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
    if (markerPtr->trackObjPtr != NULL && Tcl_GetCharLength(markerPtr->trackObjPtr) > 0) {
        optionsPtr->track = Rbc_FindTrack(graphPtr, Tcl_GetString(markerPtr->trackObjPtr));
        if (!graphPtr->trackGraph || optionsPtr->track == NULL) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-track requires an existing trackgraph track", -1));
            goto error;
        }
    }
    if (optionsPtr->yAxis->track != optionsPtr->track) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("marker -mapy axis must belong to its -track", -1));
        goto error;
    }
    return TCL_OK;

error:
    FreeParsedMarkerOptions(graphPtr, optionsPtr);
    return TCL_ERROR;
}

/**
 * @ingroup marker_configuration
 * @brief Transfer common candidate allocations and axis references to the marker.
 *
 * Releases old tags, coordinates and axes, moves candidate storage/references
 * and copies borrowed track and state. Clears transferred pointers so candidate
 * cleanup is safe. Does not register a name, map geometry or schedule redraw;
 * the concrete configuration routine owns those remaining steps.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in,out] optionsPtr Caller-owned common-option candidate; resource transfer and cleanup rules are
 *   described above.
 */
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
    markerPtr->track = optionsPtr->track;
    optionsPtr->xAxis = NULL;
    optionsPtr->yAxis = NULL;
    markerPtr->state = optionsPtr->state;
}

/**
 * @ingroup marker_configuration
 * @brief Resolve an optional color or an allowed default-color sentinel.
 *
 * Initializes output to NULL. Empty input succeeds; when allowDefault is true,
 * a nonempty prefix of defcolor yields COLOR_DEFAULT. Other spellings acquire
 * a Tk color reference. Returns TCL_OK/TCL_ERROR; callers must distinguish the
 * sentinel from an owned color when releasing the result.
 *
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed live Tk window supplying the native resource context.
 * @param[in] objPtr Borrowed Tcl option value; accepted empty/NULL cases are described above.
 * @param[in] allowDefault Nonzero permits the COLOR_DEFAULT sentinel instead of an owned Tk color.
 * @param[out] colorPtrPtr Required output initialized to NULL; receives an owned Tk color or the allowed
 *   sentinel.
 */
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

/**
 * @ingroup marker_configuration
 * @brief Stage foreground and background from a list of at most two colors.
 *
 * Empty input yields a zeroed pair. Parses colors into temporary ownership;
 * failure releases any first color and leaves the output pair unchanged.
 * Success transfers the pair for eventual Rbc_FreeColorPair cleanup. Does not
 * release an existing output pair, so callers must manage replacement explicitly.
 *
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed live Tk window supplying the native resource context.
 * @param[in] objPtr Borrowed Tcl option value; accepted empty/NULL cases are described above.
 * @param[in] allowDefault Nonzero permits the COLOR_DEFAULT sentinel instead of an owned Tk color.
 * @param[out] pairPtr Required output receiving candidate colors on success; existing contents are not
 *   released.
 */
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

/**
 * @ingroup marker_configuration
 * @brief Evaluate a coordinate with special elastic endpoint spellings.
 *
 * Exact Inf, +Inf and -Inf become positive/negative DBL_MAX. Other strings
 * are passed to Tcl_ExprDouble and may execute Tcl. Returns Tcl status and
 * writes valuePtr on success. No general finite-value check is performed here.
 *
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] expr Borrowed NUL-terminated coordinate expression, potentially executing Tcl.
 * @param[out] valuePtr Required output assigned only on successful conversion.
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

/**
 * @ingroup marker_mapping_core
 * @brief Map a horizontal marker coordinate, preserving elastic endpoints.
 *
 * Borrows current axis range and graph geometry. DBL_MAX sentinels select
 * normalized endpoints; descending axes reverse them. Legacy log mapping uses
 * log10 for positives and zero for negatives, then ordinary normalization.
 * Track-owned axes use full world width, otherwise graph hRange/hOffset.
 * Does not clamp normalized values or update layout.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] axisPtr Borrowed mapped axis providing range, scale, direction and track ownership.
 * @param[in] x Horizontal world coordinate, including supported elastic sentinels.
 */
static double HMap(Graph *graphPtr, Axis *axisPtr, double x) {
    double norm;

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
    if (axisPtr->track != NULL) {
        /* Map against the full track; visible bounds are only for clipping. */
        return norm * MAX(1, axisPtr->track->worldRight - axisPtr->track->worldLeft) + axisPtr->track->worldLeft;
    }
    /* Horizontal transformation */
    return ((norm * graphPtr->hRange) + graphPtr->hOffset);
}

/**
 * @ingroup marker_mapping_core
 * @brief Map a vertical marker coordinate against full track or graph geometry.
 *
 * Uses the same sentinel, descending and legacy log rules as HMap, with
 * screen-Y reversal. Track ownership selects world bounds even outside a
 * temporary track drawing scope. Visible clipping must not replace this span.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] axisPtr Borrowed mapped axis providing range, scale, direction and track ownership.
 * @param[in] y Vertical world coordinate, including supported elastic sentinels.
 */
static double VMap(Graph *graphPtr, Axis *axisPtr, double y) {
    double norm;

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
    /* Region searches also map original points outside the drawing scope. */
    if (axisPtr->track != NULL) {
        /* Scrolling must not rescale markers into the remaining visible area. */
        return (1.0 - norm) * MAX(1, axisPtr->track->worldBottom - axisPtr->track->worldTop) + axisPtr->track->worldTop;
    }
    /* Vertical transformation */
    return (((1.0 - norm) * graphPtr->vRange) + graphPtr->vOffset);
}

/**
 * @ingroup marker_mapping_core
 * @brief Map one borrowed world point through its axes and graph inversion.
 *
 * Uses marker-specific HMap/VMap, swapping logical axes when inverted.
 * Returns screen coordinates without rounding or remapping graph layout.
 * These helpers intentionally differ from ordinary element mapping.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] pointPtr Borrowed world-coordinate point to map.
 * @param[in] axesPtr Borrowed logical X/Y axis pair.
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

/**
 * @ingroup marker_lifecycle_core
 * @brief Allocate an unregistered concrete marker and initialize common state.
 *
 * Dispatches by interned class identity and returns NULL for an unknown class.
 * Concrete allocation is asserted. Copies the name, borrows graph/class, sets
 * MAP_ITEM and initializes option lifecycle flags. Caller must initialize and
 * configure options, then register the marker or destroy the partial record.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] name Borrowed NUL-terminated exact marker name.
 * @param[in] classUid Interned concrete marker class identifier.
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
    } else if (classUid == rbcArcMarkerUid) {
        markerPtr = CreateArcMarker();
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

/**
 * @ingroup marker_lifecycle_core
 * @brief Remove binding and registry/list state and free a concrete marker.
 *
 * Requires a live marker. Marks backing store dirty for under markers, removes
 * object bindings, and releases initialized resources while the window exists.
 * Handles pre-initialization failure with explicit cleanup instead. Deletes
 * non-NULL hash/list links and frees the record. Bulk teardown or replacement
 * may clear hashPtr first to retain control of the hash entry. Does not itself
 * schedule a graph redraw or make callback-driven registry mutation safe.
 *
 * @param[in,out] markerPtr Owned marker consumed by destruction; record and resources are invalid afterward.
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

/**
 * @ingroup marker_bitmap
 * @brief Release only owned mapped bitmap and mask pixmaps.
 *
 * Source Tk bitmaps are borrowed here. A destination mask may alias the
 * destination bitmap but must not also own that same pixmap. Frees owned
 * handles and clears both handles/ownership flags; dimensions and outline
 * count are left to the caller.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] bmPtr Borrowed bitmap marker whose owned mapped caches are released.
 */
static void FreeMappedBitmapResources(Graph *graphPtr, BitmapMarker *bmPtr) {
    /*
     * destMask may alias destBitmap, but in that case only
     * destBitmapOwned may be true.
     */
    assert(!((bmPtr->destMaskOwned) && (bmPtr->destBitmapOwned) && (bmPtr->destMask == bmPtr->destBitmap)));
    if ((bmPtr->destMaskOwned) && (bmPtr->destMask != None)) {
        Tk_FreePixmap(graphPtr->display, bmPtr->destMask);
    }
    if ((bmPtr->destBitmapOwned) && (bmPtr->destBitmap != None)) {
        Tk_FreePixmap(graphPtr->display, bmPtr->destBitmap);
    }
    bmPtr->destMask = None;
    bmPtr->destBitmap = None;
    bmPtr->destMaskOwned = FALSE;
    bmPtr->destBitmapOwned = FALSE;
}

/**
 * @ingroup marker_bitmap
 * @brief Allocate a depth-one intersection of two equally sized masks.
 *
 * Borrows both masks and window/display. Copies mask1 and applies GXand with
 * mask2 through a temporary GC. Returns None on pixmap/GC allocation failure;
 * otherwise the caller must Tk_FreePixmap the result. Does not validate input
 * dimensions or alter the source masks.
 *
 * @param[in] tkwin Borrowed live Tk window supplying the native resource context.
 * @param[in] mask1 Borrowed depth-one mask matching width and height.
 * @param[in] mask2 Borrowed second depth-one mask with identical dimensions.
 * @param[in] width Positive mask width in pixels; input storage must match.
 * @param[in] height Positive mask height in pixels; input storage must match.
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
/**
 * @ingroup marker_bitmap
 * @brief Stage bitmap options and GCs before invalidating mapped resources.
 *
 * Parses common resources and checks explicit mask dimensions against the
 * source bitmap. Normalizes rotation separately from the configured value.
 * After validation, commits common state, releases old mapped pixmaps and
 * shared GCs, installs replacements and schedules remapping/redraw. Error
 * paths release staged resources; the outer Tk transaction owns rollback.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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

/**
 * @ingroup marker_bitmap
 * @brief Build visible bitmap/mask transformations and background geometry.
 *
 * Requires a source bitmap and caller-supplied coordinates. One point uses
 * source dimensions and anchor; two corners define an inclusive scaled box.
 * Applies offsets, rejects offscreen boxes, and transforms only the visible
 * subregion. With transparent background, combines an explicit mask with
 * source bits or aliases the source bitmap. Tracks generated pixmap ownership
 * separately. Stores the background outline in absolute display coordinates for drawing
 * and picking without further anchor translation.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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
    int i;

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
    if (bmPtr->core.nWorldPts == 1) {
        /* A single anchor retains the source bitmap dimensions. */
        destWidth = srcWidth;
        destHeight = srcHeight;
        if ((bmPtr->anchor == TK_ANCHOR_CENTER) && (bmPtr->theta == 0.0)) {
            /* Align the center pixel before applying integer offsets. */
            anchorPos.x = floor(corner1.x + 0.5) - destWidth / 2;
            anchorPos.y = floor(corner1.y + 0.5) - destHeight / 2;
        } else {
            anchorPos = Rbc_TranslatePoint(&corner1, destWidth, destHeight, bmPtr->anchor);
        }
    } else {
        destWidth = (int)(corner2.x - corner1.x) + 1;
        destHeight = (int)(corner2.y - corner1.y) + 1;
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

/**
 * @ingroup marker_bitmap
 * @brief Test a mapped bitmap rectangle or rotated bounding polygon.
 *
 * Returns false without a source bitmap. Unrotated bounds are half-open.
 * Rotated tests use the absolute mapped outline directly. Does not test
 * individual bitmap bits or transparency mask pixels.
 *
 * @param[in] markerPtr Borrowed concrete marker with current mapped geometry.
 * @param[in] samplePtr Borrowed query point in screen coordinates; cached geometry must be current.
 */
static int PointInBitmapMarker(Marker *markerPtr, Point2D *samplePtr) {
    BitmapMarker *bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);

    if (bmPtr->srcBitmap == None) {
        return 0;
    }
    if (bmPtr->theta != 0.0) {
        return Rbc_PointInPolygon(samplePtr, bmPtr->outline, bmPtr->nOutlinePts);
    }
    return ((samplePtr->x >= bmPtr->anchorPos.x) && (samplePtr->x < (bmPtr->anchorPos.x + bmPtr->destWidth)) &&
            (samplePtr->y >= bmPtr->anchorPos.y) && (samplePtr->y < (bmPtr->anchorPos.y + bmPtr->destHeight)));
}

/**
 * @ingroup marker_bitmap
 * @brief Test bitmap bounds against an enclosed/overlapping region.
 *
 * Requires world coordinates; uses cached dimensions for unrotated tests.
 * Rotated tests use the absolute mapped outline directly. No pixel-mask
 * or alpha test is made.
 *
 * @param[in] markerPtr Borrowed concrete marker with current mapped geometry.
 * @param[in] extsPtr Borrowed ordered rectangle in screen coordinates.
 * @param[in] enclosed Nonzero requests full enclosure; zero requests overlap.
 */
static int RegionInBitmapMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    BitmapMarker *bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);

    if (bmPtr->core.nWorldPts < 1) {
        return FALSE;
    }
    if (bmPtr->theta != 0.0) {
        return Rbc_RegionInPolygon(extsPtr, bmPtr->outline, bmPtr->nOutlinePts, enclosed);
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

/**
 * @ingroup marker_bitmap
 * @brief Present the mapped bitmap through screen raster services.
 *
 * Skips missing/empty destination bitmaps. Borrows mapped handles and GCs.
 * Arbitrary rotations with opaque background and no explicit mask use the
 * background polygon plus bitmap-as-mask workaround. Integer placement is
 * performed here; renderer services own native clip/origin handling.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in] drawable Borrowed compatible screen destination drawable.
 */
static void DrawBitmapMarker(Marker *markerPtr, Drawable drawable) {
    BitmapMarker *bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);
    Rbc_RenderContext *ctx;
    Rbc_RenderRectangle r;
    Rbc_RenderBitmapStyle style;
    int rotatedBackground;

    if ((bmPtr->destBitmap == None) || (bmPtr->destWidth < 1) || (bmPtr->destHeight < 1)) {
        return;
    }
    /* The mapper supplies the clipped rotated background; presentation keeps
     * the bitmap-as-mask workaround for arbitrary rotations without a mask. */
    rotatedBackground = (bmPtr->srcMask == None) && (bmPtr->fillColor != NULL) &&
                        (FMOD(bmPtr->theta, 90.0) != 0.0);
    r.x = (int)bmPtr->anchorPos.x;
    r.y = (int)bmPtr->anchorPos.y;
    r.width = bmPtr->destWidth;
    r.height = bmPtr->destHeight;
    style.bitmap = bmPtr->destBitmap;
    style.mask = rotatedBackground ? bmPtr->destBitmap : bmPtr->destMask;
    style.gc = bmPtr->gc;
    style.fillGC = bmPtr->fillGC;
    style.foreground = bmPtr->outlineColor;
    style.background = bmPtr->fillColor;
    style.backgroundPoints = rotatedBackground ? bmPtr->outline : NULL;
    style.backgroundCount = rotatedBackground ? bmPtr->nOutlinePts : 0;
    ctx = Rbc_RenderBeginScreenRaster(markerPtr->graphPtr, drawable);
    Rbc_RenderMappedBitmap(ctx, &r, &style);
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup marker_bitmap
 * @brief Export mapped foreground and background masks or polygon.
 *
 * Borrows export context and mapped resources. Explicit masks restrict the
 * background and intersect foreground bits; transparent backgrounds use the
 * mapper-selected effective mask. An unmasked opaque background uses the
 * mapped polygon. Frees any temporary foreground intersection and ends the
 * output context on both empty and normal paths.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in,out] exportPtr Borrowed export context receiving output and backend error state.
 */
static void BitmapMarkerExport(Marker *markerPtr, Rbc_ExportContext *exportPtr) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    Graph *graphPtr;
    BitmapMarker *bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);
    Pixmap foregroundMask;
    int foregroundMaskOwned;

    graphPtr = markerPtr->graphPtr;
    if ((bmPtr->destBitmap == None) || (bmPtr->destWidth < 1) || (bmPtr->destHeight < 1)) {
        Rbc_RenderEnd(output);
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
            Rbc_RenderBitmapMask(output, graphPtr->display, bmPtr->destMask, bmPtr->anchorPos.x, bmPtr->anchorPos.y,
                                   bmPtr->destWidth, bmPtr->destHeight, bmPtr->fillColor, TRUE);
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
        Rbc_RenderBackgroundPolygon(output, bmPtr->fillColor, bmPtr->outline, bmPtr->nOutlinePts);
    }
    if ((bmPtr->outlineColor != NULL) && (foregroundMask != None)) {
        Rbc_RenderBitmapMask(output, graphPtr->display, foregroundMask, bmPtr->anchorPos.x, bmPtr->anchorPos.y,
                               bmPtr->destWidth, bmPtr->destHeight, bmPtr->outlineColor, FALSE);
    }
    if (foregroundMaskOwned) {
        Tk_FreePixmap(graphPtr->display, foregroundMask);
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup marker_bitmap
 * @brief Release bitmap-derived caches and shared drawing GCs.
 *
 * Clears mapped handles/ownership, both GCs, destination dimensions and
 * outline count. Tk option cleanup owns source bitmap/mask and colors; common
 * cleanup owns the record. Does not itself schedule redraw.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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

/**
 * @ingroup marker_bitmap
 * @brief Allocate a zeroed bitmap record and select its callback class.
 *
 * Returns the embedded common marker. Common creation supplies options,
 * registry membership and later destruction; no bitmap is acquired here.
 */
static Marker *CreateBitmapMarker(void) {
    BitmapMarker *bmPtr;

    bmPtr = RbcCalloc(1, sizeof(BitmapMarker));
    if (bmPtr != NULL) {
        bmPtr->core.classPtr = &bitmapMarkerClass;
    }
    return (bmPtr != NULL) ? &bmPtr->core : NULL;
}

/**
 * @ingroup marker_image
 * @brief Refresh the owned photo copy after a Tk image change.
 *
 * Borrows the marker callback data and ignores the changed rectangle/size
 * arguments. Looks up the configured name and replaces srcImage with a fresh
 * photo copy, or NULL if no photo remains. Frees the old copy and marks mapping
 * and backing storage dirty before scheduling redraw; no script evaluation is
 * performed directly here.
 *
 * @param[in,out] clientData Borrowed marker registered as callback data; concrete type is specified above.
 * @param[in] x Changed image rectangle metadata supplied by Tk; ignored by this callback.
 * @param[in] y Changed image rectangle metadata supplied by Tk; ignored by this callback.
 * @param[in] width Changed image rectangle metadata supplied by Tk; ignored by this callback.
 * @param[in] height Changed image rectangle metadata supplied by Tk; ignored by this callback.
 * @param[in] imageWidth Image width supplied by Tk; ignored by this callback.
 * @param[in] imageHeight Image height supplied by Tk; ignored by this callback.
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

/**
 * @ingroup marker_image
 * @brief Acquire replacement Tk image resources before releasing the old ones.
 *
 * Stages common options, Tk image instance/change callback, optional photo
 * copy and GC. After validation, commits common resources and releases old
 * pixmap, temporary image, photo copy, Tk instance and GC. Installs new state,
 * clears dimensions and schedules remap/redraw. Error paths free staged
 * resources; the named source image itself is not owned by the marker.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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
        Tk_Image tmpImage = imPtr->tmpImage;

        imPtr->tmpImage = NULL;
        Rbc_DestroyTemporaryImage(graphPtr->interp, tmpImage);
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

/**
 * @ingroup marker_image
 * @brief Map image placement and maintain a temporary scaled photo.
 *
 * Skips absent/deleted source instances. Non-photo images use the first
 * mapped point plus offsets as top-left, without anchor adjustment or
 * scaling. Photos use anchor/natural size or a two-corner inclusive box; only
 * the visible scaled subregion is generated. Returning to natural size
 * destroys the temporary photo after clearing its field. Creation failure
 * returns without a Tcl status; callers must not assume mapping is atomic.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 */
static void MapImageMarker(Marker *markerPtr) {
    Extents2D exts;
    Graph *graphPtr;
    ImageMarker *imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);
    Point2D anchorPos;
    Point2D corner1, corner2;
    int scaledWidth, scaledHeight;
    int srcWidth, srcHeight;

    if ((imPtr->tkImage == NULL) || Tk_ImageIsDeleted(imPtr->tkImage)) {
        return;
    }
    graphPtr = imPtr->core.graphPtr;
    corner1 = MapPoint(graphPtr, imPtr->core.worldPts, &imPtr->core.axes);
    if (imPtr->srcImage == NULL) {
        if (imPtr->tmpImage != NULL) {
            Tk_Image tmpImage = imPtr->tmpImage;

            imPtr->tmpImage = NULL;
            Rbc_DestroyTemporaryImage(graphPtr->interp, tmpImage);
        }
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
        imPtr->pixmap = None;
        if (imPtr->tmpImage == NULL) {
            imPtr->tmpImage = Rbc_CreateTemporaryImage(graphPtr->interp, graphPtr->tkwin, imPtr);
            if (imPtr->tmpImage == NULL) {
                Rbc_FreeColorImage(destImage);
                return;
            }
        }
        /* Put the scaled colorimage into the photo. */
        photo = Tk_FindPhoto(graphPtr->interp, Rbc_NameOfImage(imPtr->tmpImage));
        Rbc_ColorImageToPhoto(graphPtr->interp, destImage, photo);
        Rbc_FreeColorImage(destImage);
        imPtr->width = width;
        imPtr->height = height;
    } else if (imPtr->tmpImage != NULL) {
        /* Returning to natural size must not reuse the last scaled photo. */
        Tk_Image tmpImage = imPtr->tmpImage;

        imPtr->tmpImage = NULL;
        Rbc_DestroyTemporaryImage(graphPtr->interp, tmpImage);
    }
    imPtr->anchorPos = anchorPos;
}

/**
 * @ingroup marker_window
 * @brief Parse a child dimension while distinguishing defaults from explicit zero.
 *
 * Writes zero for a NULL object. Otherwise parses pixels and rejects
 * negative values or explicitly changed zero, leaving output unchanged on
 * error. Internal zero means use the child requested size. Returns Tcl status
 * and diagnostic; caller supplies the option-change flag.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in] objPtr Borrowed Tcl option value; accepted empty/NULL cases are described above.
 * @param[in] explicitlyChanged Nonzero rejects an explicitly supplied zero dimension.
 * @param[out] valuePtr Required output assigned only on successful conversion.
 */
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

/**
 * @ingroup marker_window
 * @brief Detach the embedded child and optionally destroy it.
 *
 * Returns if no child. Clears the marker token before removing its event
 * handler and geometry management. Destroys the child when requested, else
 * unmaps it for reuse; resets dimensions afterward. The marker record must
 * remain alive across the Tk operations. Does not free the record.
 *
 * @param[in,out] wmPtr Borrowed window marker kept alive while its child is detached.
 * @param[in] destroyWindow Nonzero destroys the detached child; zero unmaps it for reuse.
 */
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

/**
 * @ingroup marker_image
 * @brief Test the half-open cached image rectangle.
 *
 * Borrows placement/dimensions and does not refresh mapping or inspect
 * source existence, transparency or individual pixels. Caller filtering
 * and current mapped state remain prerequisites.
 *
 * @param[in] markerPtr Borrowed concrete marker with current mapped geometry.
 * @param[in] samplePtr Borrowed query point in screen coordinates; cached geometry must be current.
 */
static int PointInImageMarker(Marker *markerPtr, Point2D *samplePtr) {
    ImageMarker *imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);
    return ((samplePtr->x >= imPtr->anchorPos.x) && (samplePtr->x < (imPtr->anchorPos.x + imPtr->width)) &&
            (samplePtr->y >= imPtr->anchorPos.y) && (samplePtr->y < (imPtr->anchorPos.y + imPtr->height)));
}

/**
 * @ingroup marker_image
 * @brief Test enclosure or overlap of cached image bounds.
 *
 * Requires at least one world point. Uses anchor plus width/height, with
 * strict overlap excluding edge-only contact. Does not inspect alpha or
 * source-image availability and does not remap.
 *
 * @param[in] markerPtr Borrowed concrete marker with current mapped geometry.
 * @param[in] extsPtr Borrowed ordered rectangle in screen coordinates.
 * @param[in] enclosed Nonzero requests full enclosure; zero requests overlap.
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

/**
 * @ingroup marker_image
 * @brief Draw the temporary/source Tk image or cached pixmap.
 *
 * Skips absent/deleted source instances. Prefers tmpImage when there is no
 * pixmap, otherwise submits the pixmap with its GC and rectangle. Borrows all
 * resources and ends the screen raster context after dispatch.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in] drawable Borrowed compatible screen destination drawable.
 */
static void DrawImageMarker(Marker *markerPtr, Drawable drawable) {
    ImageMarker *imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);
    Rbc_RenderContext *ctx;

    if ((imPtr->tkImage == NULL) || (Tk_ImageIsDeleted(imPtr->tkImage))) {
        return;
    }
    ctx = Rbc_RenderBeginScreenRaster(markerPtr->graphPtr, drawable);
    if (imPtr->pixmap == None) {
        Tk_Image image = (imPtr->tmpImage != NULL) ? imPtr->tmpImage : imPtr->tkImage;
        Rbc_RenderTkImage(ctx, image, (int)imPtr->anchorPos.x, (int)imPtr->anchorPos.y);
    } else {
        Rbc_RenderRectangle r = {(int)imPtr->anchorPos.x, (int)imPtr->anchorPos.y, imPtr->width, imPtr->height};
        Rbc_RenderPixmap(ctx, imPtr->pixmap, imPtr->gc, &r);
    }
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup marker_image
 * @brief Export the current temporary/source image through output services.
 *
 * Skips absent/deleted source instances. Resolves the selected image name as
 * a photo for photo output, otherwise delegates to generic Tk image output.
 * Borrows resources and ends the context on all paths; format-specific
 * capture/conversion remains in the renderer.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in,out] exportPtr Borrowed export context receiving output and backend error state.
 */
static void ImageMarkerExport(Marker *markerPtr, Rbc_ExportContext *exportPtr) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    ImageMarker *imPtr = IMAGE_MARKER_FROM_CORE(markerPtr);
    const char *imageName;
    Tk_PhotoHandle photo;

    if ((imPtr->tkImage == NULL) || (Tk_ImageIsDeleted(imPtr->tkImage))) {
        Rbc_RenderEnd(output);
        return; /* Image doesn't exist anymore */
    }
    imageName = (imPtr->tmpImage == NULL) ? Rbc_NameOfImage(imPtr->tkImage) : Rbc_NameOfImage(imPtr->tmpImage);
    photo = Tk_FindPhoto(markerPtr->graphPtr->interp, imageName);
    if (photo != NULL) {
        Rbc_RenderPhotoImage(output, photo, imPtr->anchorPos.x, imPtr->anchorPos.y);
    } else {
        Rbc_RenderTkImage(output, (imPtr->tmpImage == NULL) ? imPtr->tkImage : imPtr->tmpImage,
                             imPtr->anchorPos.x, imPtr->anchorPos.y);
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup marker_image
 * @brief Release owned image caches and the source image instance.
 *
 * Frees pixmap, GC, Tk instance, temporary image and photo copy, then clears
 * dimensions. Clears tmpImage before temporary-image destruction. Freeing the
 * source instance does not delete the user-owned named image; common cleanup
 * releases option objects and the record.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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
        Tk_Image tmpImage = imPtr->tmpImage;

        imPtr->tmpImage = NULL;
        Rbc_DestroyTemporaryImage(graphPtr->interp, tmpImage);
    }
    if (imPtr->srcImage != NULL) {
        Rbc_FreeColorImage(imPtr->srcImage);
        imPtr->srcImage = NULL;
    }
    imPtr->width = 0;
    imPtr->height = 0;
}

/**
 * @ingroup marker_image
 * @brief Allocate a zeroed image record and set its class.
 *
 * Returns the embedded common marker for common option initialization and
 * registry insertion. Acquires no image resources itself.
 */
static Marker *CreateImageMarker(void) {
    ImageMarker *imPtr;

    imPtr = RbcCalloc(1, sizeof(ImageMarker));
    if (imPtr != NULL) {
        imPtr->core.classPtr = &imageMarkerClass;
    }
    return (imPtr != NULL) ? &imPtr->core : NULL;
}

/**
 * @ingroup marker_text
 * @brief Stage text layout, padding, shadow and drawing resources.
 *
 * Parses common resources, padding and shadow, normalizes rotation, and
 * builds a new layout/rotated outline using the proposed Tk-managed style.
 * After validation commits the layout, GCs and manually owned shadow color
 * while saved Tk options retain old resources. Error paths free staged
 * resources. Marks mapping and under-layer backing dirty and schedules redraw.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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

/**
 * @ingroup marker_text
 * @brief Place the cached rotated text rectangle using its marker anchor.
 *
 * Requires a non-NULL string and caller-supplied coordinates. Maps the first
 * point, applies marker anchor and pixel offsets, tests plot overlap, and
 * stores anchorPos. Does not rebuild text layout or allocate a bitmap.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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

/**
 * @ingroup marker_text
 * @brief Pick the text bounding rectangle or rotated outline.
 *
 * Returns false for a NULL string. Rotated outline coordinates are local
 * to anchorPos and are translated for polygon testing. Unrotated bounds are
 * half-open. Tests bounds, not glyph coverage or shadow pixels independently.
 *
 * @param[in] markerPtr Borrowed concrete marker with current mapped geometry.
 * @param[in] samplePtr Borrowed query point in screen coordinates; cached geometry must be current.
 */
static int PointInTextMarker(Marker *markerPtr, Point2D *samplePtr) {
    TextMarker *tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);

    if (tmPtr->string == NULL) {
        return 0;
    }
    if (tmPtr->style.theta != 0.0) {
        Point2D points[5];
        int i;

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

/**
 * @ingroup marker_text
 * @brief Test cached text bounds for enclosure or overlap.
 *
 * Requires coordinates. Uses four translated rotated-outline vertices or
 * the unrotated rectangle; does not refresh layout or inspect glyph coverage.
 * Unrotated overlap excludes edge-only contact.
 *
 * @param[in] markerPtr Borrowed concrete marker with current mapped geometry.
 * @param[in] extsPtr Borrowed ordered rectangle in screen coordinates.
 * @param[in] enclosed Nonzero requests full enclosure; zero requests overlap.
 */
static int RegionInTextMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    TextMarker *tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);
    if (tmPtr->core.nWorldPts < 1) {
        return FALSE;
    }
    if (tmPtr->style.theta != 0.0) {
        Point2D points[5];
        int i;

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

/**
 * @ingroup marker_text
 * @brief Try Cairo background fill with native-sized integer vertices.
 *
 * Returns FALSE for other renderers. Translates the four local outline
 * vertices by anchorPos and truncates to integer coordinates before area
 * submission. Returns the render helper status for native fallback; does not
 * modify cached geometry.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] drawable Borrowed compatible screen destination drawable.
 * @param[in] tmPtr Borrowed text marker with current layout and mapped background outline.
 */
static int DrawRenderedTextBackground(Graph *graphPtr, Drawable drawable, TextMarker *tmPtr) {
    Point2D points[4];
    int i;

    if (graphPtr->renderer != RBC_RENDERER_CAIRO)
        return FALSE;
    for (i = 0; i < 4; i++) {
        points[i].x = (int)(tmPtr->outline[i].x + tmPtr->anchorPos.x);
        points[i].y = (int)(tmPtr->outline[i].y + tmPtr->anchorPos.y);
    }
    return Rbc_RenderArea(graphPtr, drawable, points, 4, tmPtr->fillColor, NULL, None);
}

/**
 * @ingroup marker_text
 * @brief Draw optional background and cached text layout.
 *
 * Returns for a NULL string. Background tries Cairo then native services;
 * text uses the screen-text renderer when foreground exists, with integer
 * placement. Borrows layout/style and does not alter their ownership.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in] drawable Borrowed compatible screen destination drawable.
 */
static void DrawTextMarker(Marker *markerPtr, Drawable drawable) {
    TextMarker *tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);
    Graph *graphPtr = markerPtr->graphPtr;

    if (tmPtr->string == NULL) {
        return;
    }
    if ((tmPtr->fillGC != NULL) && !DrawRenderedTextBackground(graphPtr, drawable, tmPtr)) {
        Rbc_RenderScreenTextBackground(graphPtr, drawable, tmPtr->fillGC, tmPtr->outline, &tmPtr->anchorPos);
    }
    if (tmPtr->style.color != NULL) {
        Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);
        Rbc_RenderTextLayout(text, tmPtr->textPtr, &tmPtr->style, (int)tmPtr->anchorPos.x,
                             (int)tmPtr->anchorPos.y);
        Rbc_RenderEnd(text);
    }
}

/**
 * @ingroup marker_text
 * @brief Export the text string/style and optional rotated background.
 *
 * Returns through an ended context for a NULL string. Translates background
 * vertices without screen integer truncation, then delegates text to document
 * output using the string and style rather than the cached screen layout.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in,out] exportPtr Borrowed export context receiving output and backend error state.
 */
static void TextMarkerExport(Marker *markerPtr, Rbc_ExportContext *exportPtr) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    TextMarker *tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);

    if (tmPtr->string == NULL) {
        Rbc_RenderEnd(output);
        return;
    }
    if (tmPtr->fillGC != NULL) {
        Point2D polygon[4];
        int i;

        /*
         * Simulate the rotated background of the bitmap by
         * filling a bounding polygon with the background color.
         */
        for (i = 0; i < 4; i++) {
            polygon[i].x = tmPtr->outline[i].x + tmPtr->anchorPos.x;
            polygon[i].y = tmPtr->outline[i].y + tmPtr->anchorPos.y;
        }
        Rbc_RenderBackgroundPolygon(output, tmPtr->fillColor, polygon, 4);
    }
    Rbc_RenderText(output, tmPtr->string, &tmPtr->style, tmPtr->anchorPos.x, tmPtr->anchorPos.y);
    Rbc_RenderEnd(output);
}

/**
 * @ingroup marker_text
 * @brief Release derived text resources without freeing Tk option values.
 *
 * Frees shared text/background GCs, manually owned shadow color and layout.
 * Clears their fields, dimensions and outline. Tk option cleanup owns font,
 * foreground/background colors and string; common cleanup owns the record.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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

/**
 * @ingroup marker_text
 * @brief Allocate text storage with an internally north-west text style.
 *
 * Initializes the text style, class and four-pixel padding. The internal
 * style anchor differs from the marker anchor applied during mapping.
 * Common creation installs configured options and takes record ownership.
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

/**
 * @ingroup marker_window
 * @brief Validate dimensions and direct-child ownership before transferring geometry.
 *
 * Stages common options and dimensions and resolves the requested window.
 * Only direct children of the graph are accepted. On success commits common
 * state, detaches/unmaps the previous child without destroying it, and sets
 * the new token before registering events and geometry management. Schedules
 * remap/redraw; errors release staged common state. Geometry transfer invokes
 * Tk services, so successful validation is not a blanket callback-safety guarantee.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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

/**
 * @ingroup marker_window
 * @brief Map child requested/explicit dimensions, anchor and pixel offsets.
 *
 * Returns without a child. Uses positive explicit dimensions in preference
 * to Tk requested size, stores rectangle and tests plot overlap. Does not
 * move/map the actual child or introduce plot/track clipping containers.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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

/**
 * @ingroup marker_window
 * @brief Test the half-open cached child rectangle.
 *
 * Borrows geometry only; does not check the child token, mapping state or
 * refresh its requested dimensions. Outer marker filtering remains separate.
 *
 * @param[in] markerPtr Borrowed concrete marker with current mapped geometry.
 * @param[in] samplePtr Borrowed query point in screen coordinates; cached geometry must be current.
 */
static int PointInWindowMarker(Marker *markerPtr, Point2D *samplePtr) {
    WindowMarker *wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);
    return ((samplePtr->x >= wmPtr->anchorPos.x) && (samplePtr->x < (wmPtr->anchorPos.x + wmPtr->width)) &&
            (samplePtr->y >= wmPtr->anchorPos.y) && (samplePtr->y < (wmPtr->anchorPos.y + wmPtr->height)));
}

/**
 * @ingroup marker_window
 * @brief Test enclosure or strict overlap of cached child bounds.
 *
 * Requires coordinates and uses anchor plus width/height. Does not inspect
 * child visibility, update geometry or test actual window contents.
 *
 * @param[in] markerPtr Borrowed concrete marker with current mapped geometry.
 * @param[in] extsPtr Borrowed ordered rectangle in screen coordinates.
 * @param[in] enclosed Nonzero requests full enclosure; zero requests overlap.
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

/**
 * @ingroup marker_window
 * @brief Synchronize the real child position/size and map it if necessary.
 *
 * Returns without a child. Moves/resizes only when cached geometry differs,
 * then maps if needed. Ignores drawable: this changes a real Tk child, not a
 * pixmap. Caller must have checked visibility and prepared mapped geometry.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in] drawable Unused drawable; this callback moves/maps the real Tk child.
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

/**
 * @ingroup marker_window
 * @brief Synchronize visibility of all real child markers across both layers.
 *
 * Walks display order and filters missing coordinates, hidden/clipped state,
 * collapsed tracks and associated hidden elements. Maps/moves visible children
 * and unmaps others. Partial overlap keeps the full child, clipped by its
 * graph parent only. Required even when under-layer backing is reused.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 */
static void SyncWindowMarkers(Graph *graphPtr) {
    Rbc_ChainLink *link;
    for (link = Rbc_ChainFirstLink(graphPtr->markers.displayList); link != NULL; link = Rbc_ChainNextLink(link)) {
        Marker *markerPtr = Rbc_ChainGetValue(link);
        WindowMarker *wmPtr;
        int visible;
        if (markerPtr->classUid != rbcWindowMarkerUid) {
            continue;
        }
        wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);
        if (wmPtr->tkwin == NULL) {
            continue;
        }
        visible = markerPtr->nWorldPts > 0 && !markerPtr->hidden && !markerPtr->clipped;
        if (markerPtr->track != NULL && (markerPtr->track->right - markerPtr->track->left <= 1 ||
                                         markerPtr->track->bottom - markerPtr->track->top <= 1)) {
            visible = FALSE;
        }
        if (markerPtr->elemName != NULL) {
            Tcl_HashEntry *entry = Tcl_FindHashEntry(&graphPtr->elements.table, markerPtr->elemName);
            if (entry != NULL && ((Element *)Tcl_GetHashValue(entry))->hidden) {
                visible = FALSE;
            }
        }
        if (visible) {
            DrawWindowMarker(markerPtr, None);
        } else if (Tk_IsMapped(wmPtr->tkwin)) {
            Tk_UnmapWindow(wmPtr->tkwin);
        }
    }
}

/**
 * @ingroup marker_window
 * @brief Delegate capture of an existing mapped child to the exporter.
 *
 * Creates/ends an output context and emits only if the child exists and is
 * mapped. Uses marker placement; does not map a hidden child or replace it
 * with an independent widget rendering. Capture policy belongs to the backend.
 *
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in,out] exportPtr Borrowed export context receiving output and backend error state.
 */
static void WindowMarkerExport(Marker *markerPtr, Rbc_ExportContext *exportPtr) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    WindowMarker *wmPtr = WINDOW_MARKER_FROM_CORE(markerPtr);
    if (wmPtr->tkwin == NULL) {
        Rbc_RenderEnd(output);
        return;
    }
    if (Tk_IsMapped(wmPtr->tkwin)) {
        Rbc_RenderWindow(output, wmPtr->tkwin, wmPtr->anchorPos.x, wmPtr->anchorPos.y);
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup marker_window
 * @brief Destroy the currently embedded child and clear marker placement.
 *
 * Delegates detachment/destruction to ReleaseWindowMarkerChild, then clears
 * anchorPos. Unlike image markers, deleting this marker destroys its owned
 * child. Common lifecycle code frees the marker record/options.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
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

/**
 * @ingroup marker_window
 * @brief Allocate a zeroed window marker and set its class.
 *
 * Returns the embedded common record; common configuration later resolves
 * and acquires geometry management of a child. No child is created here.
 */
static Marker *CreateWindowMarker(void) {
    WindowMarker *wmPtr;

    wmPtr = RbcCalloc(1, sizeof(WindowMarker));
    if (wmPtr != NULL) {
        wmPtr->core.classPtr = &windowMarkerClass;
    }
    return (wmPtr != NULL) ? &wmPtr->core : NULL;
}

/**
 * @ingroup marker_window
 * @brief Clear a destroyed child token while retaining the marker record.
 *
 * Handles DestroyNotify only. Clears token/dimensions, marks mapping dirty
 * and schedules redraw if the graph window still exists. Other structure
 * events are ignored; this callback does not delete the marker.
 *
 * @param[in,out] clientData Borrowed marker registered as callback data; concrete type is specified above.
 * @param[in] eventPtr Borrowed child structure event; only DestroyNotify is handled.
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

/**
 * @ingroup marker_window
 * @brief Copy newly requested child dimensions into automatic-size fields.
 *
 * Updates only dimensions whose explicit request is zero. When either
 * changes, marks mapping dirty and schedules redraw for a live graph window
 * so anchor and visibility are recomputed. Invalidates under-layer backing
 * storage as needed. Fully overridden or unchanged requests need no redraw.
 *
 * @param[in,out] clientData Borrowed marker registered as callback data; concrete type is specified above.
 * @param[in] tkwin Borrowed managed child window supplied by Tk geometry management.
 */
static void ChildGeometryProc(ClientData clientData, Tk_Window tkwin) {
    WindowMarker *wmPtr = clientData;
    Graph *graphPtr = wmPtr->core.graphPtr;
    int changed = FALSE;

    if ((wmPtr->reqWidth == 0) && (wmPtr->width != Tk_ReqWidth(tkwin))) {
        wmPtr->width = Tk_ReqWidth(tkwin);
        changed = TRUE;
    }
    if ((wmPtr->reqHeight == 0) && (wmPtr->height != Tk_ReqHeight(tkwin))) {
        wmPtr->height = Tk_ReqHeight(tkwin);
        changed = TRUE;
    }
    if (changed) {
        wmPtr->core.flags |= MAP_ITEM;
        if (wmPtr->core.drawUnder) {
            graphPtr->flags |= REDRAW_BACKING_STORE;
        }
        if (graphPtr->tkwin != NULL) {
            Rbc_EventuallyRedrawGraph(graphPtr);
        }
    }
}

/**
 * @ingroup marker_window
 * @brief Delete the marker when another manager takes its child.
 *
 * Removes the event handler and clears the matching child token before
 * DestroyMarker, preserving the window for its new manager. Schedules graph
 * redraw afterward. The marker pointer is invalid after destruction; no
 * marker field may be read again by this callback.
 *
 * @param[in,out] clientData Borrowed marker registered as callback data; concrete type is specified above.
 * @param[in] tkwin Borrowed managed child window supplied by Tk geometry management.
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

/**
 * @ingroup marker_line_geometry
 * @brief Construct a closed arrowhead and shortened shaft endpoint.
 *
 * Borrows the three shape distances and writes six vertices, including the
 * repeated tip, plus shaftPtr. Uses mapped display coordinates, effective line
 * width and Tk-style neck interpolation. Returns FALSE for an adjacent point
 * within DBL_EPSILON of the tip; outputs must not be used on that path.
 * Allocates nothing and does not clip the resulting head.
 */
static int ComputeArrowHead(Point2D tip, Point2D adjacent, double lineWidth, const double shape[3],
                            Point2D arrow[PTS_IN_ARROW], Point2D *shaftPtr) {
    double shapeA, shapeB, shapeC;
    double dx, dy, length;
    double cosTheta, sinTheta;
    double fracHeight, backup;
    double vertX, vertY, temp;

    /*
     * This follows Tk canvas line-arrow geometry.
     */
    shapeA = shape[0] + 0.001;
    shapeB = shape[1] + 0.001;
    shapeC = shape[2] + lineWidth * 0.5 + 0.001;
    dx = tip.x - adjacent.x;
    dy = tip.y - adjacent.y;
    length = hypot(dx, dy);
    if (length <= DBL_EPSILON) {
        return FALSE;
    }
    cosTheta = dx / length;
    sinTheta = dy / length;
    fracHeight = (lineWidth * 0.5) / shapeC;
    backup = fracHeight * shapeB + shapeA * (1.0 - fracHeight) * 0.5;
    arrow[0] = tip;
    arrow[5] = tip;
    vertX = tip.x - shapeA * cosTheta;
    vertY = tip.y - shapeA * sinTheta;
    temp = shapeC * sinTheta;
    arrow[1].x = tip.x - shapeB * cosTheta + temp;
    arrow[4].x = arrow[1].x - 2.0 * temp;
    temp = shapeC * cosTheta;
    arrow[1].y = tip.y - shapeB * sinTheta - temp;
    arrow[4].y = arrow[1].y + 2.0 * temp;
    arrow[2].x = arrow[1].x * fracHeight + vertX * (1.0 - fracHeight);
    arrow[2].y = arrow[1].y * fracHeight + vertY * (1.0 - fracHeight);
    arrow[3].x = arrow[4].x * fracHeight + vertX * (1.0 - fracHeight);
    arrow[3].y = arrow[4].y * fracHeight + vertY * (1.0 - fracHeight);
    /*
     * Stop the shaft at the neck instead of letting it protrude
     * through the arrowhead.
     */
    shaftPtr->x = tip.x - backup * cosTheta;
    shaftPtr->y = tip.y - backup * sinTheta;
    return TRUE;
}

/**
 * @ingroup marker_line_geometry
 * @brief Test inclusive containment of a display point.
 *
 * Borrows both inputs; returns TRUE on the rectangle boundary as well as
 * inside it. No coordinate mapping, tolerance expansion or mutation.
 */
static int PointInsideExtents(const Point2D *pointPtr, const Extents2D *extsPtr) {
    return ((pointPtr->x >= extsPtr->left) && (pointPtr->x <= extsPtr->right) && (pointPtr->y >= extsPtr->top) &&
            (pointPtr->y <= extsPtr->bottom));
}

/**
 * @ingroup marker_line_geometry
 * @brief Replace clipped shaft segments and endpoint arrow geometry.
 *
 * Clears prior segments and arrow flags, then maps at least two world points
 * with pixel offsets. Heads are constructed only when their original tips lie
 * inside plot extents; shaft endpoints are shortened before edge clipping.
 * Owns the replacement segment allocation. Size/allocation failure leaves an
 * empty clipped marker without a Tcl error. A visible head alone makes the
 * marker non-clipped even when no shaft segment survives.
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
    lmPtr->hasFirstArrow = FALSE;
    lmPtr->hasLastArrow = FALSE;
    if (lmPtr->segments != NULL) {
        ckfree(lmPtr->segments);
        lmPtr->segments = NULL;
    }
    if (lmPtr->core.nWorldPts < 2) {
        return;
    }
    /*
     * Get the plotting-area extents before using them for either
     * arrowhead endpoint tests or line clipping.
     */
    Rbc_GraphExtents(graphPtr, &exts);
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
    /*
     * Compute arrowhead geometry from mapped screen coordinates.
     */
    {
        Point2D tip, adjacent;
        double width;

        width = (lmPtr->lineWidth > 0) ? (double)lmPtr->lineWidth : 1.0;
        if ((lmPtr->arrow == LINE_ARROW_FIRST) || (lmPtr->arrow == LINE_ARROW_BOTH)) {
            tip = MapPoint(graphPtr, lmPtr->core.worldPts, &lmPtr->core.axes);
            adjacent = MapPoint(graphPtr, lmPtr->core.worldPts + 1, &lmPtr->core.axes);
            tip.x += lmPtr->core.xOffset;
            tip.y += lmPtr->core.yOffset;
            adjacent.x += lmPtr->core.xOffset;
            adjacent.y += lmPtr->core.yOffset;
            if (PointInsideExtents(&tip, &exts)) {
                lmPtr->hasFirstArrow = ComputeArrowHead(tip, adjacent, width, lmPtr->arrowShape, lmPtr->firstArrow,
                                                        &lmPtr->firstShaftPoint);
            }
        }
        if ((lmPtr->arrow == LINE_ARROW_LAST) || (lmPtr->arrow == LINE_ARROW_BOTH)) {
            Point2D *lastPtr;

            lastPtr = lmPtr->core.worldPts + (lmPtr->core.nWorldPts - 1);
            tip = MapPoint(graphPtr, lastPtr, &lmPtr->core.axes);
            adjacent = MapPoint(graphPtr, lastPtr - 1, &lmPtr->core.axes);
            tip.x += lmPtr->core.xOffset;
            tip.y += lmPtr->core.yOffset;
            adjacent.x += lmPtr->core.xOffset;
            adjacent.y += lmPtr->core.yOffset;
            if (PointInsideExtents(&tip, &exts)) {
                lmPtr->hasLastArrow =
                    ComputeArrowHead(tip, adjacent, width, lmPtr->arrowShape, lmPtr->lastArrow, &lmPtr->lastShaftPoint);
            }
        }
    }
    /*
     * Map and clip the line shaft.
     */
    srcPtr = lmPtr->core.worldPts;
    p = MapPoint(graphPtr, srcPtr, &lmPtr->core.axes);
    p.x += lmPtr->core.xOffset;
    p.y += lmPtr->core.yOffset;
    if (lmPtr->hasFirstArrow) {
        p = lmPtr->firstShaftPoint;
    }
    segPtr = segments;
    for (srcPtr++, endPtr = lmPtr->core.worldPts + lmPtr->core.nWorldPts; srcPtr < endPtr; srcPtr++) {
        next = MapPoint(graphPtr, srcPtr, &lmPtr->core.axes);
        next.x += lmPtr->core.xOffset;
        next.y += lmPtr->core.yOffset;
        if ((srcPtr == (endPtr - 1)) && lmPtr->hasLastArrow) {
            next = lmPtr->lastShaftPoint;
        }
        if (Rbc_LineRectClip(&exts, &p, &next, segPtr)) {
            segPtr++;
        }
        p = next;
    }
    lmPtr->nSegments = (Tcl_Size)(segPtr - segments);
    if ((lmPtr->nSegments == 0) && (!lmPtr->hasFirstArrow) && (!lmPtr->hasLastArrow)) {
        ckfree(segments);
        return;
    }
    lmPtr->segments = segments;
    lmPtr->core.clipped = FALSE;
}

/**
 * @ingroup marker_line_geometry
 * @brief Pick mapped arrow polygons or clipped shaft segments.
 *
 * Borrows mapped caches. Tests five unique vertices per enabled head, then
 * segment distance using the graph halo. Does not expand that halo by shaft
 * width or refresh layout.
 */
static int PointInLineMarker(Marker *markerPtr, Point2D *samplePtr) {
    LineMarker *lmPtr = LINE_MARKER_FROM_CORE(markerPtr);
    /*
     * Arrow arrays contain the tip twice: the final point closes the
     * polygon for drawing.  Polygon hit testing needs only the five
     * unique vertices.
     */
    if (lmPtr->hasFirstArrow) {
        if (Rbc_PointInPolygon(samplePtr, lmPtr->firstArrow, PTS_IN_ARROW - 1)) {
            return TRUE;
        }
    }
    if (lmPtr->hasLastArrow) {
        if (Rbc_PointInPolygon(samplePtr, lmPtr->lastArrow, PTS_IN_ARROW - 1)) {
            return TRUE;
        }
    }
    return Rbc_PointInSegments(samplePtr, lmPtr->segments, lmPtr->nSegments, (double)lmPtr->core.graphPtr->halo);
}

/**
 * @ingroup marker_line_geometry
 * @brief Test original mapped line edges and cached arrowheads against a region.
 *
 * Requires two world points. Enclosure requires every original mapped point
 * (including pixel offsets) and every enabled head inside the rectangle.
 * Overlap accepts any original edge or cached head intersection. Unlike point
 * picking, shaft tests do not use the plot-clipped segment cache or graph halo.
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

        /*
         * First require every original line point to be inside the
         * region, preserving the existing line-marker semantics.
         */
        for (pointPtr = lmPtr->core.worldPts, endPtr = lmPtr->core.worldPts + lmPtr->core.nWorldPts; pointPtr < endPtr;
             pointPtr++) {
            p = MapPoint(lmPtr->core.graphPtr, pointPtr, &lmPtr->core.axes);
            p.x += lmPtr->core.xOffset;
            p.y += lmPtr->core.yOffset;
            if ((p.x < extsPtr->left) || (p.x > extsPtr->right) || (p.y < extsPtr->top) || (p.y > extsPtr->bottom)) {
                return FALSE;
            }
        }
        /*
         * The complete arrowhead must also be enclosed.  Use only
         * the five unique vertices; arrow[5] duplicates arrow[0].
         */
        if (lmPtr->hasFirstArrow) {
            if (!Rbc_RegionInPolygon(extsPtr, lmPtr->firstArrow, PTS_IN_ARROW - 1, TRUE)) {
                return FALSE;
            }
        }
        if (lmPtr->hasLastArrow) {
            if (!Rbc_RegionInPolygon(extsPtr, lmPtr->lastArrow, PTS_IN_ARROW - 1, TRUE)) {
                return FALSE;
            }
        }
        return TRUE;
    } else {
        Point2D p;
        Point2D q;

        /*
         * Any intersection with the original line is enough.
         */
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
        /*
         * Otherwise the region may intersect only an arrowhead.
         */
        if (lmPtr->hasFirstArrow) {
            if (Rbc_RegionInPolygon(extsPtr, lmPtr->firstArrow, PTS_IN_ARROW - 1, FALSE)) {
                return TRUE;
            }
        }
        if (lmPtr->hasLastArrow) {
            if (Rbc_RegionInPolygon(extsPtr, lmPtr->lastArrow, PTS_IN_ARROW - 1, FALSE)) {
                return TRUE;
            }
        }
        return FALSE;
    }
}

/**
 * @ingroup marker_shape_output
 * @brief Submit independent clipped segments to the selected renderer.
 *
 * Borrows geometry/style. XOR requests or a missing render context return
 * FALSE so the caller can use its native GC path. Otherwise sets caps/joins,
 * submits all segments, ends the context and returns TRUE. Width is at least
 * one pixel; TRUE denotes dispatch, not a pixel-visibility assertion.
 */
static int DrawRenderedMarkerSegments(Graph *graphPtr, Drawable drawable, const Segment2D *segments, Tcl_Size count,
                                      const XColor *color, const XColor *gapColor, int width, const Rbc_Dashes *dashes,
                                      int cap, int join, int xorMode) {
    Rbc_RenderContext *ctx;

    if (xorMode) {
        return FALSE;
    }
    ctx = Rbc_RenderBegin(graphPtr, drawable, color, MAX(1, width), dashes, gapColor);
    if (ctx == NULL) {
        return FALSE;
    }
    Rbc_RenderLineStyle(ctx, cap, join);
    Rbc_RenderSegments(ctx, segments, count);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/**
 * @ingroup marker_shape_output
 * @brief Draw shaft segments with legacy native endpoint conversion.
 *
 * Borrows the line GC/cache. Without heads, delegates directly to the screen
 * segment renderer. With heads, rounds endpoints in bounded 128-segment
 * stack batches before dispatch. Never modifies mapped doubles used by
 * picking/export and never draws the heads themselves.
 */
static void DrawNativeLineSegments(LineMarker *lmPtr, Drawable drawable) {
    Graph *graphPtr = lmPtr->core.graphPtr;
    Segment2D rounded[128];
    Tcl_Size first, count, i;

    if (!lmPtr->hasFirstArrow && !lmPtr->hasLastArrow) {
        Rbc_RenderScreenSegments(graphPtr->tkwin, drawable, lmPtr->gc, lmPtr->segments, lmPtr->nSegments);
        return;
    }
    for (first = 0; first < lmPtr->nSegments; first += count) {
        count = MIN(lmPtr->nSegments - first, 128);
        for (i = 0; i < count; i++) {
            rounded[i].p.x = round(lmPtr->segments[first + i].p.x);
            rounded[i].p.y = round(lmPtr->segments[first + i].p.y);
            rounded[i].q.x = round(lmPtr->segments[first + i].q.x);
            rounded[i].q.y = round(lmPtr->segments[first + i].q.y);
        }
        Rbc_RenderScreenSegments(graphPtr->tkwin, drawable, lmPtr->gc, rounded, count);
    }
}

/**
 * @ingroup marker_shape_output
 * @brief Erase all recorded immediate line geometry with its original native GC.
 *
 * Borrows the old shaft/head caches and GC, independent of newly installed Tk
 * options such as xor, arrow and outline. Redraws shaft and enabled heads when
 * a drawable and GC remain, including head-only markers, then clears xorState.
 * Does not map geometry, select a renderer or schedule graph redraw.
 */
static void EraseLineMarkerXor(LineMarker *lmPtr, Drawable drawable) {
    Graph *graphPtr = lmPtr->core.graphPtr;

    if (!lmPtr->xorState) {
        return;
    }
    if ((drawable != None) && (lmPtr->gc != NULL)) {
        if (lmPtr->nSegments > 0) {
            DrawNativeLineSegments(lmPtr, drawable);
        }
        if (lmPtr->hasFirstArrow) {
            Rbc_RenderScreenArrowHead(graphPtr, drawable, lmPtr->gc, lmPtr->firstArrow);
        }
        if (lmPtr->hasLastArrow) {
            Rbc_RenderScreenArrowHead(graphPtr, drawable, lmPtr->gc, lmPtr->lastArrow);
        }
    }
    lmPtr->xorState = FALSE;
}

/**
 * @ingroup marker_shape_output
 * @brief Submit temporary half-pixel-shifted arrow fill vertices.
 *
 * Copies the five unique vertices and shifts both coordinates by 0.5 to
 * align area boundaries with the rendered shaft. Returns the area helper
 * status for fallback selection. Original geometry remains unchanged for
 * native drawing, picking and document export.
 */
static int DrawRenderedArrowHead(Graph *graphPtr, Drawable drawable, const Point2D *arrow, XColor *color) {
    Point2D points[PTS_IN_ARROW - 1];
    int i;

    for (i = 0; i < PTS_IN_ARROW - 1; i++) {
        points[i].x = arrow[i].x + 0.5;
        points[i].y = arrow[i].y + 0.5;
    }
    return Rbc_RenderArea(graphPtr, drawable, points, PTS_IN_ARROW - 1, color, NULL, None);
}


/**
 * @ingroup marker_shape_output
 * @brief Draw mapped shafts followed by first and last arrowheads.
 *
 * Uses renderer strokes/fills where possible, otherwise native GC services;
 * XOR always uses native paths. An absent non-XOR outline may be recovered
 * from the GC foreground for screen drawing. Toggles xorState after any
 * drawing only for graph-wide XOR markers, not track-owned markers.
 */
static void DrawLineMarker(Marker *markerPtr, Drawable drawable) {
    LineMarker *lmPtr = LINE_MARKER_FROM_CORE(markerPtr);
    Graph *graphPtr = markerPtr->graphPtr;
    int drawn;
    XColor gcColor;
    XColor *outline = lmPtr->outlineColor;

    if ((outline == NULL) && !lmPtr->xor &&Rbc_RenderGCForeground(graphPtr, lmPtr->gc, &gcColor)) {
        outline = &gcColor;
    }
    drawn = FALSE;
    if (lmPtr->nSegments > 0) {
        if (!DrawRenderedMarkerSegments(graphPtr, drawable, lmPtr->segments, lmPtr->nSegments, outline,
                                        lmPtr->fillColor, lmPtr->lineWidth, &lmPtr->dashes, lmPtr->capStyle,
                                        lmPtr->joinStyle, lmPtr->xor)) {
            DrawNativeLineSegments(lmPtr, drawable);
        }
        drawn = TRUE;
    }
    if (lmPtr->hasFirstArrow) {
        if (lmPtr->xor || !DrawRenderedArrowHead(graphPtr, drawable, lmPtr->firstArrow, outline)) {
            Rbc_RenderScreenArrowHead(graphPtr, drawable, lmPtr->gc, lmPtr->firstArrow);
        }
        drawn = TRUE;
    }
    if (lmPtr->hasLastArrow) {
        if (lmPtr->xor || !DrawRenderedArrowHead(graphPtr, drawable, lmPtr->lastArrow, outline)) {
            Rbc_RenderScreenArrowHead(graphPtr, drawable, lmPtr->gc, lmPtr->lastArrow);
        }
        drawn = TRUE;
    }
    if (drawn && lmPtr->xor &&markerPtr->track == NULL) {
        lmPtr->xorState = !lmPtr->xorState;
    }
}

/**
 * @ingroup marker_line_geometry
 * @brief Parse three screen distances into caller-owned shape storage.
 *
 * Borrows arrowShapeObjPtr and the graph window. Returns TCL_ERROR with a
 * diagnostic for a malformed list, wrong count or invalid screen distance.
 * Earlier output entries may be written before failure. No additional
 * positivity or relative-size check is performed here.
 */
static int ParseArrowShape(LineMarker *lmPtr, double shape[3]) {
    Graph *graphPtr = lmPtr->core.graphPtr;
    Tcl_Interp *interp = graphPtr->interp;
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_Size i;

    if (Tcl_ListObjGetElements(interp, lmPtr->arrowShapeObjPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc != 3) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad arrow shape \"%s\": must be a list of three screen distances",
                                               Tcl_GetString(lmPtr->arrowShapeObjPtr)));
        return TCL_ERROR;
    }
    for (i = 0; i < 3; i++) {
        if (Tk_GetDoublePixelsFromObj(interp, graphPtr->tkwin, objv[i], shape + i) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

/**
 * @ingroup marker_line_geometry
 * @brief Prepare and commit line style, common options and a private GC.
 *
 * Parses common options, arrow shape, caps, joins, dashes and nonnegative
 * width/offset before committing derived state. Error paths release staged
 * resources; the outer option transaction restores Tk-managed values.
 * Erases the recorded old XOR shaft and heads before replacing geometry/GC,
 * then maps/draws graph-wide XOR immediately or schedules redraw (including
 * track transitions). Erasure uses old caches and GC regardless of new options.
 * Under-layer scheduled drawing invalidates the backing store.
 */
static int ConfigureLineMarker(Marker *markerPtr) {
    Graph *graphPtr;
    LineMarker *lmPtr;
    ParsedMarkerOptions markerOptions;
    Rbc_Dashes newDashes;
    double newArrowShape[3];
    int newCapStyle;
    int newJoinStyle;
    int newLineWidth;
    int newDashOffset;
    XGCValues gcValues;
    unsigned long gcMask;
    GC newGC;
    Drawable drawable;
    int trackRedraw = markerPtr->track != NULL;

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
    if (ParseArrowShape(lmPtr, newArrowShape) != TCL_OK) {
        goto error;
    }
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
    EraseLineMarkerXor(lmPtr, drawable);
    /*
     * Commit the common marker options. This transfers ownership of
     * tags, coordinates, and axis references to markerPtr.
     */
    CommitMarkerOptions(markerPtr, &markerOptions);
    /*
     * Commit line-marker-specific derived values.
     */
    lmPtr->arrowShape[0] = newArrowShape[0];
    lmPtr->arrowShape[1] = newArrowShape[1];
    lmPtr->arrowShape[2] = newArrowShape[2];
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
     * Graph-wide XOR markers are mapped and drawn immediately. Track XOR
     * uses the clipped redraw pass instead. DrawLineMarker()
     * updates xorState after drawing.
     */
    if (lmPtr->xor &&!trackRedraw && markerPtr->track == NULL && (drawable != None)) {
        MapLineMarker(markerPtr);
        DrawLineMarker(markerPtr, drawable);
        return TCL_OK;
    }
    /*
     * Other markers, including track-owned XOR, use the graph redraw path.
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

/**
 * @ingroup marker_shape_output
 * @brief Export the five unique arrow vertices using the explicit outline color.
 *
 * Borrows the mapped arrow and export context. An empty outline suppresses
 * head output rather than recovering the screen GC color. Creates and ends
 * a fill-render context; does not shorten or remap the shaft.
 */
static void ArrowHeadExport(LineMarker *lmPtr, Rbc_ExportContext *exportPtr, Point2D *arrow) {
    if (lmPtr->outlineColor == NULL) {
        return;
    }
    Rbc_RenderFillStyle style = {lmPtr->outlineColor, NULL, None, 1.0, FALSE};
    Rbc_RenderContext *ctx = Rbc_RenderBeginExportFill(lmPtr->core.graphPtr, exportPtr, &style);

    Rbc_RenderFillPolygon(ctx, arrow, PTS_IN_ARROW - 1);
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup marker_shape_output
 * @brief Export clipped shaft segments and mapped arrowheads.
 *
 * Borrows caches at the current export layout. Sets cap/join/dash state and
 * dash-gap color for the shaft, then exports enabled heads. Uses the original
 * double geometry without native rounding or screen half-pixel shifts.
 * Export does not toggle XOR state.
 */
static void LineMarkerExport(Marker *markerPtr, Rbc_ExportContext *exportPtr) {
    LineMarker *lmPtr = LINE_MARKER_FROM_CORE(markerPtr);
    /*
     * Draw the line shaft.
     */
    if (lmPtr->nSegments > 0) {
        Rbc_RenderContext *ctx = Rbc_RenderBeginExport(exportPtr, lmPtr->outlineColor, lmPtr->lineWidth,
                                                          &lmPtr->dashes, lmPtr->capStyle, lmPtr->joinStyle);

        Rbc_RenderDashBackground(ctx, lmPtr->fillColor);
        Rbc_RenderSegments(ctx, lmPtr->segments, lmPtr->nSegments);
        Rbc_RenderEnd(ctx);
    }
    /*
     * Draw arrowheads after the shaft.  The shaft endpoints have
     * already been shortened by MapLineMarker so that they terminate
     * at the neck of the arrowhead.
     */
    if (lmPtr->hasFirstArrow) {
        ArrowHeadExport(lmPtr, exportPtr, lmPtr->firstArrow);
    }
    if (lmPtr->hasLastArrow) {
        ArrowHeadExport(lmPtr, exportPtr, lmPtr->lastArrow);
    }
}

/**
 * @ingroup marker_line_geometry
 * @brief Release line-owned segment storage and private graphics context.
 *
 * Borrows graph and marker; does not free the record or common options.
 * Erases recorded immediate XOR shaft and heads while a drawable and GC
 * remain, including head-only geometry. Clears released pointers and
 * segment count. Common lifecycle code owns subsequent record destruction.
 */
static void FreeLineMarker(Graph *graphPtr, Marker *markerPtr) {
    LineMarker *lmPtr;
    Drawable drawable;

    lmPtr = LINE_MARKER_FROM_CORE(markerPtr);
    /*
     * Remove an immediately drawn XOR marker before releasing its GC.
     */
    drawable = (graphPtr->tkwin != NULL) ? Tk_WindowId(graphPtr->tkwin) : None;
    EraseLineMarkerXor(lmPtr, drawable);
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

/**
 * @ingroup marker_line_geometry
 * @brief Allocate a zeroed line record and install initial class/style values.
 *
 * Uses the RBC allocation wrapper, sets the class, disables XOR and selects
 * butt caps/miter joins. Returns the embedded common marker; the caller owns
 * option initialization, registry insertion and eventual destruction.
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

/**
 * @ingroup marker_polygon_geometry
 * @brief Replace full mapped vertices and independently clipped fill/outline caches.
 *
 * Frees old geometry first and requires at least three world vertices.
 * Applies axes and pixel offsets to screenPts. Fill clipping is attempted
 * only with a fill foreground; outlines require foreground and positive
 * width, and include the closing edge. Each allocation is size-checked and
 * fallible without a Tcl error. A partial cache may survive another cache
 * allocation failure; clipped becomes FALSE when drawable geometry remains.
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

/**
 * @ingroup marker_polygon_geometry
 * @brief Pick the interior of the full mapped polygon.
 *
 * Requires screenPts and at least three world vertices. Borrows that cache
 * and tests polygon interior irrespective of fill visibility or line width.
 * Does not use clipped fill geometry, graph halo or refresh mapping.
 */
static int PointInPolygonMarker(Marker *markerPtr, Point2D *samplePtr) {
    PolygonMarker *pmPtr;

    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if ((pmPtr->screenPts == NULL) || (pmPtr->core.nWorldPts < 3)) {
        return FALSE;
    }
    return Rbc_PointInPolygon(samplePtr, pmPtr->screenPts, pmPtr->core.nWorldPts);
}

/**
 * @ingroup marker_polygon_geometry
 * @brief Test the full mapped polygon for enclosure or overlap.
 *
 * Requires screenPts and at least three world vertices, then delegates to
 * the region/polygon helper. Uses original mapped vertices rather than
 * clipped fill/outline caches and ignores stroke thickness.
 */
static int RegionInPolygonMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    PolygonMarker *pmPtr;

    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if ((pmPtr->screenPts == NULL) || (pmPtr->core.nWorldPts < 3)) {
        return FALSE;
    }
    return Rbc_RegionInPolygon(extsPtr, pmPtr->screenPts, pmPtr->core.nWorldPts, enclosed);
}

/**
 * @ingroup marker_shape_output
 * @brief Draw clipped polygon fill followed by clipped outline segments.
 *
 * Borrows fill/outline caches and colors. Tries selected-renderer area and
 * stroke operations, with native GC fallback (forced for XOR). Fill needs
 * at least three points; outline needs positive width and a foreground.
 * Does not update immediate-XOR bookkeeping.
 */
static void DrawPolygonMarker(Marker *markerPtr, Drawable drawable) {
    Graph *graphPtr;
    PolygonMarker *pmPtr;

    graphPtr = markerPtr->graphPtr;
    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    /*
     * Draw the polygon fill.  XFillPolygon requires an int count and
     * the complete polygon must fit in one native request.
     */
    if ((pmPtr->nFillPts >= 3) && (pmPtr->fill.fgColor != NULL) &&
        (pmPtr->xor || !Rbc_RenderArea(graphPtr, drawable, pmPtr->fillPts, pmPtr->nFillPts, pmPtr->fill.fgColor,
                                       pmPtr->fill.bgColor, pmPtr->stipple))) {
        Rbc_RenderContext *ctx = Rbc_RenderBeginNativeFill(graphPtr, drawable, pmPtr->fillGC, NULL,
            (Pixmap)1);
        Rbc_RenderFillPolygon(ctx, pmPtr->fillPts, pmPtr->nFillPts);
        Rbc_RenderEnd(ctx);
    }

    /*
     * Outline segments can be safely chunked by
     * the native segment renderer.
     */
    if ((pmPtr->nOutlinePts > 0) && (pmPtr->lineWidth > 0) && (pmPtr->outline.fgColor != NULL)) {
        if (!DrawRenderedMarkerSegments(graphPtr, drawable, pmPtr->outlinePts, pmPtr->nOutlinePts,
                                        pmPtr->outline.fgColor, pmPtr->outline.bgColor, pmPtr->lineWidth,
                                        &pmPtr->dashes, pmPtr->capStyle, pmPtr->joinStyle, pmPtr->xor)) {
            Rbc_RenderContext *ctx = Rbc_RenderBeginNativeSegments(graphPtr, drawable, pmPtr->outlineGC);
            Rbc_RenderSegments(ctx, pmPtr->outlinePts, pmPtr->nOutlinePts);
            Rbc_RenderEnd(ctx);
        }
    }
}

/**
 * @ingroup marker_shape_output
 * @brief Export polygon fill and outline from clipped caches.
 *
 * Borrows the export context and current mapped caches. Fill preserves
 * foreground/background/stipple; outline preserves width, cap/join and dash
 * gap color. Creates and ends each renderer context independently, without
 * remapping or altering XOR state.
 */
static void PolygonMarkerExport(Marker *markerPtr, Rbc_ExportContext *exportPtr) {
    Graph *graphPtr = markerPtr->graphPtr;
    PolygonMarker *pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if ((pmPtr->nFillPts >= 3) && (pmPtr->fill.fgColor != NULL)) {
        Rbc_RenderFillStyle style = {pmPtr->fill.fgColor, pmPtr->fill.bgColor, pmPtr->stipple, 1.0, FALSE};
        Rbc_RenderContext *ctx = Rbc_RenderBeginExportFill(graphPtr, exportPtr, &style);

        Rbc_RenderFillPolygon(ctx, pmPtr->fillPts, pmPtr->nFillPts);
        Rbc_RenderEnd(ctx);
    }
    if ((pmPtr->nOutlinePts > 0) && (pmPtr->lineWidth > 0) && (pmPtr->outline.fgColor != NULL)) {
        Rbc_RenderContext *ctx = Rbc_RenderBeginExport(exportPtr, pmPtr->outline.fgColor, pmPtr->lineWidth,
                                                          &pmPtr->dashes, pmPtr->capStyle, pmPtr->joinStyle);

        Rbc_RenderDashBackground(ctx, pmPtr->outline.bgColor);
        Rbc_RenderSegments(ctx, pmPtr->outlinePts, pmPtr->nOutlinePts);
        Rbc_RenderEnd(ctx);
    }
}

/**
 * @ingroup marker_polygon_geometry
 * @brief Prepare and commit shared polygon/arc appearance and common options.
 *
 * Arc angles must be finite. Stages common resources, styles and owned color
 * pairs, builds a private outline GC and shared fill GC, then erases a
 * recorded immediate outline before commit. Errors release staged resources;
 * the outer Tk option transaction restores option fields. Immediate XOR is
 * restricted to graph-wide outline-only cases without pending axis reset;
 * open arcs qualify as outline-only. Otherwise marks for mapping and redraw,
 * invalidating under-layer backing storage. Fill GC construction does not
 * apply the outline GC XOR function.
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
    int trackRedraw = markerPtr->track != NULL;

    graphPtr = markerPtr->graphPtr;
    pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if ((markerPtr->classUid == rbcArcMarkerUid) &&
        (!FINITE(pmPtr->start) || !FINITE(pmPtr->extent))) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("arc angles must be finite", -1));
        return TCL_ERROR;
    }
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
    if ((markerPtr->classUid == rbcArcMarkerUid) && (pmPtr->arcStyle == ARC_OPEN)) {
        outlineOnly = TRUE;
    }
    drawable = Tk_WindowId(graphPtr->tkwin);
    /*
     * Erase an old immediately drawn XOR polygon before replacing its
     * GC or mapped outline.
     */
    if (pmPtr->xorState && (drawable != None) && (pmPtr->outlineGC != NULL) && (pmPtr->outlinePts != NULL) &&
        (pmPtr->nOutlinePts > 0)) {
        DrawImmediatePolygonOutline(pmPtr, drawable);
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
    if (pmPtr->xor && !trackRedraw && markerPtr->track == NULL && outlineOnly &&
        !(graphPtr->flags & RESET_AXES) && (drawable != None)) {
        (*markerPtr->classPtr->mapProc)(markerPtr);
        if ((pmPtr->outlineGC != NULL) && (pmPtr->outlinePts != NULL) && (pmPtr->nOutlinePts > 0)) {
            DrawImmediatePolygonOutline(pmPtr, drawable);
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

/**
 * @ingroup marker_polygon_geometry
 * @brief Release polygon/arc caches, color pairs and graphics contexts.
 *
 * Erases a recorded immediate outline when its drawable/resources remain.
 * Releases the private outline GC, shared fill GC and three owned geometry
 * arrays, clearing pointers/counts as implemented. Frees manually owned color
 * pairs; common option/record cleanup retains responsibility for the stipple,
 * world points, tags and the record itself.
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
            DrawImmediatePolygonOutline(pmPtr, drawable);
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

/**
 * @ingroup marker_polygon_geometry
 * @brief Allocate polygon storage with its class and initial line style.
 *
 * Returns the embedded common marker, with butt caps/miter joins and other
 * fields zeroed. Common creation configures options and registers the record.
 * The same allocation layout is reused by arc markers.
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

/**
 * @ingroup marker_polygon_geometry
 * @brief Map an ellipse box and build bounded flattened geometry caches.
 *
 * Clears old caches, requires two corners and nonzero extent, and rejects
 * nonfinite/zero mapped radii. Normalizes start modulo 360 and clamps signed
 * extent to one turn. Stores the exact ellipse separately; flattened points
 * target 0.15-pixel chord error with at most 8192 steps. Closure follows open,
 * chord or pieslice style. Visibility is conservative from the ellipse box
 * plus outline margin, independent of successful cache allocation. Fallible
 * allocations do not set Tcl errors; picking caches can be absent even when
 * the exact curve remains eligible for renderer drawing.
 */
static void MapArcMarker(Marker *markerPtr) {
    PolygonMarker *pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    Graph *graphPtr = markerPtr->graphPtr;
    Point2D a, b, *screenPts, *srcPtr, *endPtr;
    Extents2D exts;
    double cx, cy, rx, ry, sweep, angle, radius, step;
    const double radians = 0.017453292519943295;
    Tcl_Size i, steps, nVertices;
    int full;

    if (pmPtr->screenPts != NULL) ckfree(pmPtr->screenPts);
    if (pmPtr->fillPts != NULL) ckfree(pmPtr->fillPts);
    if (pmPtr->outlinePts != NULL) ckfree(pmPtr->outlinePts);
    pmPtr->screenPts = pmPtr->fillPts = NULL;
    pmPtr->outlinePts = NULL;
    pmPtr->nArcPts = pmPtr->nFillPts = pmPtr->nOutlinePts = 0;
    memset(&pmPtr->arc, 0, sizeof(pmPtr->arc));
    markerPtr->clipped = TRUE;
    if ((markerPtr->nWorldPts != 2) || (pmPtr->extent == 0.0)) return;
    a = MapPoint(graphPtr, markerPtr->worldPts, &markerPtr->axes);
    b = MapPoint(graphPtr, markerPtr->worldPts + 1, &markerPtr->axes);
    cx = a.x * 0.5 + b.x * 0.5 + markerPtr->xOffset;
    cy = a.y * 0.5 + b.y * 0.5 + markerPtr->yOffset;
    rx = fabs(a.x * 0.5 - b.x * 0.5);
    ry = fabs(a.y * 0.5 - b.y * 0.5);
    if (!FINITE(cx) || !FINITE(cy) || !FINITE(rx) || !FINITE(ry) ||
        (rx == 0.0) || (ry == 0.0)) return;
    sweep = MAX(-360.0, MIN(360.0, pmPtr->extent));
    pmPtr->arc.cx = cx;
    pmPtr->arc.cy = cy;
    pmPtr->arc.rx = rx;
    pmPtr->arc.ry = ry;
    pmPtr->arc.start = fmod(pmPtr->start, 360.0);
    pmPtr->arc.extent = sweep;
    pmPtr->arc.style = (Rbc_RenderArcStyle)pmPtr->arcStyle;
    /* Conservative visibility must not depend on the flattened path. At high
     * zoom its chord error can exceed the usual target or omit a thin sliver.
     * The true curve is clipped by the drawing/export context itself. */
    Rbc_GraphExtents(graphPtr, &exts);
    if (((pmPtr->arcStyle != ARC_OPEN) && (pmPtr->fill.fgColor != NULL)) ||
        ((pmPtr->outline.fgColor != NULL) && (pmPtr->lineWidth > 0))) {
        double margin = pmPtr->outline.fgColor != NULL ? pmPtr->lineWidth * 0.5 : 0.0;
        markerPtr->clipped = (cx + rx + margin < exts.left || cx - rx - margin > exts.right ||
                              cy + ry + margin < exts.top || cy - ry - margin > exts.bottom);
    }
    full = (fabs(sweep) == 360.0);
    radius = MAX(rx, ry);
    /* sqrt bound is conservative for the ellipse's chord error. */
    step = MIN(0.1, sqrt(1.2 / radius));
    steps = (Tcl_Size)MIN(8192.0, MAX(1.0, ceil(fabs(sweep) * radians / step)));
    nVertices = steps + 1;
    if (!full && (pmPtr->arcStyle != ARC_OPEN)) {
        nVertices += (pmPtr->arcStyle == ARC_PIESLICE) ? 2 : 1;
    }
    screenPts = Tcl_AttemptAlloc((size_t)nVertices * sizeof(*screenPts));
    if (screenPts == NULL) return;
    for (i = 0; i <= steps; i++) {
        angle = (fmod(pmPtr->start, 360.0) + sweep * ((double)i / steps)) * radians;
        screenPts[i].x = cx + rx * cos(angle);
        screenPts[i].y = cy - ry * sin(angle);
    }
    if (full) {
        screenPts[steps] = screenPts[0];
    } else if (pmPtr->arcStyle != ARC_OPEN) {
        if (pmPtr->arcStyle == ARC_PIESLICE) {
            screenPts[steps + 1].x = cx;
            screenPts[steps + 1].y = cy;
        }
        screenPts[nVertices - 1] = screenPts[0];
    }
    pmPtr->screenPts = screenPts;
    pmPtr->nArcPts = nVertices;
    Rbc_GraphExtents(graphPtr, &exts);
    if ((pmPtr->arcStyle != ARC_OPEN) && (pmPtr->fill.fgColor != NULL)) {
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
                for (srcPtr = screenPts; srcPtr + 1 < endPtr; srcPtr++) {
                    Point2D *nextPtr;

                    nextPtr = srcPtr + 1;
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

/**
 * @ingroup marker_polygon_geometry
 * @brief Pick clipped arc fill or outline caches.
 *
 * Requires a two-corner world box. Tests the clipped fill polygon first, then
 * outline segments with tolerance max(graph halo, half line width). Borrows
 * caches; does not analytically test the ellipse or rebuild missing geometry.
 */
static int PointInArcMarker(Marker *markerPtr, Point2D *samplePtr) {
    PolygonMarker *pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if (markerPtr->nWorldPts != 2) return FALSE;
    if ((pmPtr->nFillPts >= 3) && Rbc_PointInPolygon(samplePtr, pmPtr->fillPts, pmPtr->nFillPts)) return TRUE;
    return Rbc_PointInSegments(samplePtr, pmPtr->outlinePts, pmPtr->nOutlinePts,
        MAX((double)markerPtr->graphPtr->halo, pmPtr->lineWidth * 0.5));
}

/**
 * @ingroup marker_polygon_geometry
 * @brief Test flattened arc geometry for enclosure or overlap.
 *
 * Requires mapped vertices and at least one nonempty fill/outline cache.
 * Enclosure tests every untrimmed flattened vertex; overlap tests clipped
 * fill and outline segments. Neither branch expands the rectangle by stroke
 * width or provides an exact analytic curve test.
 */
static int RegionInArcMarker(Marker *markerPtr, Extents2D *extsPtr, int enclosed) {
    PolygonMarker *pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    Tcl_Size i;
    if ((markerPtr->nWorldPts != 2) || (pmPtr->nArcPts == 0) || (pmPtr->nFillPts == 0 && pmPtr->nOutlinePts == 0)) return FALSE;
    if (enclosed) {
        for (i = 0; i < pmPtr->nArcPts; i++) {
            Point2D *p = pmPtr->screenPts + i;
            if (p->x < extsPtr->left || p->x > extsPtr->right ||
                p->y < extsPtr->top || p->y > extsPtr->bottom) return FALSE;
        }
        return TRUE;
    }
    if ((pmPtr->nFillPts >= 3) && Rbc_RegionInPolygon(extsPtr, pmPtr->fillPts, pmPtr->nFillPts, FALSE)) return TRUE;
    for (i = 0; i < pmPtr->nOutlinePts; i++) {
        if (Rbc_LineRectClip(extsPtr, &pmPtr->outlinePts[i].p, &pmPtr->outlinePts[i].q, NULL)) return TRUE;
    }
    return FALSE;
}

/**
 * @ingroup marker_shape_output
 * @brief Stroke the mapped ellipse through curved or native renderer services.
 *
 * Skips zero extent, empty outline or nonpositive width. An export context
 * selects document output; otherwise uses the screen renderer unless XOR is
 * requested. Native fallback receives clipped flattened segments as well as
 * the exact ellipse. Renderer arc handling owns continuous-path dash/join
 * behavior; this helper does not join vertices itself.
 */
static void StrokeArcMarker(PolygonMarker *pmPtr, Drawable drawable, Rbc_ExportContext *exportPtr) {
    Graph *graphPtr = pmPtr->core.graphPtr;
    Rbc_RenderContext *ctx;
    if ((pmPtr->arc.extent == 0.0) || (pmPtr->outline.fgColor == NULL) || (pmPtr->lineWidth <= 0)) return;
    ctx = exportPtr ? Rbc_RenderBeginExport(exportPtr, pmPtr->outline.fgColor, pmPtr->lineWidth,
        &pmPtr->dashes, pmPtr->capStyle, pmPtr->joinStyle) :
        (pmPtr->xor ? NULL : Rbc_RenderBegin(graphPtr, drawable, pmPtr->outline.fgColor,
            pmPtr->lineWidth, &pmPtr->dashes, pmPtr->outline.bgColor));
    if (ctx != NULL) {
        Rbc_RenderLineStyle(ctx, pmPtr->capStyle, pmPtr->joinStyle);
        Rbc_RenderDashBackground(ctx, pmPtr->outline.bgColor);
        Rbc_RenderArc(ctx, &pmPtr->arc);
        Rbc_RenderEnd(ctx);
        return;
    }
    ctx = Rbc_RenderBeginNativeArc(graphPtr, drawable, pmPtr->outlineGC,
        NULL, 0, pmPtr->outlinePts, pmPtr->nOutlinePts);
    Rbc_RenderArc(ctx, &pmPtr->arc);
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup marker_shape_output
 * @brief Draw or erase a polygon/arc outline using existing native state.
 *
 * Borrows the old outline GC and geometry. Polygon outlines submit cached
 * segments. Arcs temporarily force the XOR flag while calling StrokeArcMarker
 * to bypass Cairo, then restore it. The caller owns xorState bookkeeping
 * and must retain valid old resources until the call completes.
 */
static void DrawImmediatePolygonOutline(PolygonMarker *pmPtr, Drawable drawable) {
    if (pmPtr->core.classUid == rbcArcMarkerUid) {
        int savedXor = pmPtr->xor;
        pmPtr->xor = TRUE; /* Old XOR geometry must bypass Cairo when erased. */
        StrokeArcMarker(pmPtr, drawable, NULL);
        pmPtr->xor = savedXor;
    } else {
        Rbc_RenderContext *ctx = Rbc_RenderBeginNativeSegments(pmPtr->core.graphPtr, drawable, pmPtr->outlineGC);
        Rbc_RenderSegments(ctx, pmPtr->outlinePts, pmPtr->nOutlinePts);
        Rbc_RenderEnd(ctx);
    }
}

/**
 * @ingroup marker_shape_output
 * @brief Fill a closed mapped arc and then stroke its outline.
 *
 * Skips zero extent. Open arcs never fill. Selected-renderer fill contexts
 * consume the exact ellipse; native fallback also receives the clipped fill
 * polygon. Stroke dispatch is shared with export/immediate outline paths.
 * Does not change immediate-XOR bookkeeping.
 */
static void DrawArcMarker(Marker *markerPtr, Drawable drawable) {
    PolygonMarker *pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if (pmPtr->arc.extent == 0.0) return;
    if ((pmPtr->arc.style != RBC_RENDER_ARC_OPEN) && (pmPtr->fill.fgColor != NULL)) {
        Rbc_RenderFillStyle style = {pmPtr->fill.fgColor, pmPtr->fill.bgColor, pmPtr->stipple, 1.0, FALSE};
        Rbc_RenderContext *ctx = pmPtr->xor ? NULL : Rbc_RenderBeginFill(markerPtr->graphPtr, drawable, &style);
        if (ctx != NULL) {
            Rbc_RenderFillArc(ctx, &pmPtr->arc);
            Rbc_RenderEnd(ctx);
        } else {
            ctx = Rbc_RenderBeginNativeArc(markerPtr->graphPtr, drawable, pmPtr->fillGC,
                pmPtr->fillPts, pmPtr->nFillPts, NULL, 0);
            Rbc_RenderFillArc(ctx, &pmPtr->arc);
            Rbc_RenderEnd(ctx);
        }
    }
    StrokeArcMarker(pmPtr, drawable, NULL);
}

/**
 * @ingroup marker_shape_output
 * @brief Export the exact mapped ellipse with its closure and appearance.
 *
 * Skips zero extent and omits fill for open arcs. Borrows geometry/export
 * context, emits a fill if requested and calls the shared arc stroke path.
 * Does not export the flattened picking polygon as the primary curve or
 * modify screen XOR state.
 */
static void ArcMarkerExport(Marker *markerPtr, Rbc_ExportContext *exportPtr) {
    PolygonMarker *pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
    if (pmPtr->arc.extent == 0.0) return;
    if ((pmPtr->arc.style != RBC_RENDER_ARC_OPEN) && (pmPtr->fill.fgColor != NULL)) {
        Rbc_RenderFillStyle style = {pmPtr->fill.fgColor, pmPtr->fill.bgColor, pmPtr->stipple, 1.0, FALSE};
        Rbc_RenderContext *ctx = Rbc_RenderBeginExportFill(markerPtr->graphPtr, exportPtr, &style);
        Rbc_RenderFillArc(ctx, &pmPtr->arc);
        Rbc_RenderEnd(ctx);
    }
    StrokeArcMarker(pmPtr, None, exportPtr);
}

/**
 * @ingroup marker_polygon_geometry
 * @brief Allocate polygon storage and substitute the arc callback class.
 *
 * Delegates allocation/default style to CreatePolygonMarker, then changes
 * only classPtr when non-NULL. Common initialization installs arc options;
 * configuration and cleanup remain the polygon class implementations.
 */
static Marker *CreateArcMarker(void) {
    Marker *markerPtr = CreatePolygonMarker();
    if (markerPtr != NULL) markerPtr->classPtr = &arcMarkerClass;
    return markerPtr;
}

/**
 * @ingroup marker_commands
 * @brief Resolve an exact marker name to a borrowed registry record.
 *
 * Returns TCL_OK and writes markerPtrPtr on success. Otherwise returns
 * TCL_ERROR with graph/name diagnostic, leaving output unchanged. Does not
 * search display order, accept patterns or acquire a lifetime reference.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] name Borrowed NUL-terminated exact marker name.
 * @param[out] markerPtrPtr Required output receiving a borrowed registry marker only on success.
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

/**
 * @ingroup marker_commands
 * @brief Return display-list names matching any supplied glob pattern.
 *
 * No patterns returns all listed names. Traverses from first to last and
 * emits each marker at most once; hidden markers remain eligible. Registry
 * entries detached from the display list are not returned. Returns TCL_OK.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
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

/**
 * @ingroup marker_commands
 * @brief Intern a binding tag in the graph marker-tag table.
 *
 * Creates the string-key entry when needed and returns its borrowed key
 * pointer, valid until table destruction. Does not require a named marker
 * to exist or install a binding script.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] tagName Borrowed tag string, copied by the registry when first interned.
 */
static ClientData MakeMarkerTag(Graph *graphPtr, char *tagName) {
    Tcl_HashEntry *hPtr;
    int isNew;

    hPtr = Tcl_CreateHashEntry(&graphPtr->markers.tagTable, tagName, &isNew);
    assert(hPtr);
    return Tcl_GetHashKey(&graphPtr->markers.tagTable, hPtr);
}

/**
 * @ingroup marker_commands
 * @brief Append name, class and configured tags in binding order.
 *
 * Borrows graph, marker and destination list. Interns tag identities in the
 * marker-tag table; the list does not own their strings. Does not evaluate Tcl
 * binding scripts or alter marker state.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] markerPtr Borrowed concrete marker with the class and lifecycle state required above.
 * @param[in,out] list Caller-owned destination list; appends borrowed tag identities without clearing entries.
 */
void Rbc_AppendMarkerBindingTags(Graph *graphPtr, Marker *markerPtr, Rbc_List list) {
    char **p;

    Rbc_ListAppend(list, MakeMarkerTag(graphPtr, markerPtr->name), 0);
    Rbc_ListAppend(list, MakeMarkerTag(graphPtr, markerPtr->classUid), 0);
    if (markerPtr->tags == NULL) {
        return;
    }
    for (p = markerPtr->tags; *p != NULL; p++) {
        Rbc_ListAppend(list, MakeMarkerTag(graphPtr, *p), 0);
    }
}

/**
 * @ingroup marker_commands
 * @brief List interned marker tags or delegate a binding query/update.
 *
 * No tag returns registry tag names in hash order, not only tags with scripts.
 * Otherwise interns the tag at objv[3] and passes the remaining arguments to
 * the graph binding table service. Returns its Tcl status.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
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
                                        MakeMarkerTag(graphPtr, Tcl_GetString(objv[3])), objc - 4, objv + 4);
}

/**
 * @ingroup marker_commands
 * @brief Return a marker option through its concrete Tk option table.
 *
 * Resolves objv[3] as an exact name and objv[4] as an option. Returns TCL_OK
 * with its value or TCL_ERROR from lookup/Tk. Does not configure or redraw.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
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

/**
 * @ingroup marker_commands
 * @brief Query one marker or configure multiple named markers in sequence.
 *
 * Scans names until the first dash-prefixed argument and validates them before
 * applying options. Zero/one option arguments query the first named marker.
 * Updates re-resolve each name and call ConfigureMarkerOptions independently.
 * A later failure does not roll back earlier markers. Returns a Tcl status;
 * it is not a multi-marker atomic transaction.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
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

/**
 * @ingroup marker_commands
 * @brief Configure a new marker before replacing any same-named registry entry.
 *
 * Resolves the class and scans complete option pairs for the last -name prefix;
 * otherwise generates markerN. Creates and initializes the concrete record,
 * applies options and validates the final name before publishing. A duplicate
 * name destroys the old marker while retaining its hash entry. Appends the new
 * record to display order, invalidates under-layer backing when needed and
 * schedules redraw. Returns the configured name or TCL_ERROR with cleanup.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
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
                   {"arc", &rbcArcMarkerUid},
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

/**
 * @ingroup marker_commands
 * @brief Destroy each existing exact name and ignore missing names.
 *
 * Performs deletions sequentially, clears lookup diagnostics and schedules
 * graph redraw even for no matching names. Returns TCL_OK. Concrete cleanup
 * owns associated resources, which can include a managed child window.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
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

/**
 * @ingroup marker_commands
 * @brief Return the current marker only when the binding context is a marker.
 *
 * Recognizes only the exact selector current. Checks the common binding
 * context before casting the current item, and verifies a supported marker
 * class. Other selectors or absent/nonmarker items leave an empty result.
 * Returns TCL_OK; does not pick coordinates or resolve ordinary names.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int GetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Marker *markerPtr;

    const char *str = Tcl_GetString(objv[3]);
    if ((str[0] == 'c') && (strcmp(str, "current") == 0)) {
        if (PTR2INT(Rbc_GetCurrentContext(graphPtr->bindTable)) != GRAPH_BIND_CONTEXT_MARKER) {
            return TCL_OK;
        }
        markerPtr = (Marker *)Rbc_GetCurrentItem(graphPtr->bindTable);
        /* Report only on markers. */
        if (markerPtr == NULL) {
            return TCL_OK;
        }
        if ((markerPtr->classUid == rbcBitmapMarkerUid) || (markerPtr->classUid == rbcLineMarkerUid) ||
            (markerPtr->classUid == rbcWindowMarkerUid) || (markerPtr->classUid == rbcPolygonMarkerUid) ||
            (markerPtr->classUid == rbcTextMarkerUid) || (markerPtr->classUid == rbcImageMarkerUid) ||
            (markerPtr->classUid == rbcArcMarkerUid)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->name, -1));
        }
    }
    return TCL_OK;
}

/**
 * @ingroup marker_commands
 * @brief Move a marker link before or after an optional display-list reference.
 *
 * Borrows graph and resolves both exact names before changing display order.
 * Invalid names return TCL_ERROR without unlinking either marker. A reference
 * to the target itself is a successful no-op. Otherwise moves the existing
 * link, invalidates backing storage when the moved marker is an underlay and
 * schedules redraw. No marker ownership or registry entries change.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int RelinkOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_ChainLink *linkPtr, *placePtr;
    Marker *markerPtr, *referencePtr;
    const char *str;

    /* Find the marker to be raised or lowered. */
    if (NameToMarker(graphPtr, Tcl_GetString(objv[3]), &markerPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    /* Right now it's assumed that all markers are always in the
       display list. */
    linkPtr = markerPtr->linkPtr;
    placePtr = NULL;
    if (objc == 5) {
        if (NameToMarker(graphPtr, Tcl_GetString(objv[4]), &referencePtr) != TCL_OK) {
            return TCL_ERROR;
        }
        placePtr = referencePtr->linkPtr;
        if (placePtr == linkPtr) {
            return TCL_OK;
        }
    }
    /* Both names are valid; move the existing display-list link. */
    Rbc_ChainUnlinkLink(graphPtr->markers.displayList, linkPtr);
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

/**
 * @ingroup marker_commands
 * @brief Return the first visible marker passing a class rectangle test.
 *
 * Parses enclosed/overlapping and four integer screen coordinates, normalizes
 * corner order and scans first-to-last display order. Filters hidden markers
 * and existing hidden associated elements. Local searches require a visible
 * track intersection; overlapping queries clip the search box to that track,
 * enclosed queries retain the original box. Returns the first name or an empty
 * result. No automatic layout refresh, layer filter or marker-state filter.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
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
        Extents2D searchExts = exts;
        if (markerPtr->track != NULL) {
            if (markerPtr->clipped ||
                (markerPtr->track->right - markerPtr->track->left <= 1 ||
                 markerPtr->track->bottom - markerPtr->track->top <= 1) ||
                exts.right < markerPtr->track->left || exts.left > markerPtr->track->right ||
                exts.bottom < markerPtr->track->top || exts.top > markerPtr->track->bottom) {
                continue;
            }
            if (!enclosed) {
                searchExts.left = MAX(exts.left, markerPtr->track->left);
                searchExts.right = MIN(exts.right, markerPtr->track->right);
                searchExts.top = MAX(exts.top, markerPtr->track->top);
                searchExts.bottom = MIN(exts.bottom, markerPtr->track->bottom);
            }
        }
        if ((*markerPtr->classPtr->regionProc)(markerPtr, &searchExts, enclosed)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(markerPtr->name, -1));
            return TCL_OK;
        }
    }
    Tcl_ResetResult(interp);
    return TCL_OK;
}

/**
 * @ingroup marker_commands
 * @brief Report exact registry membership regardless of visibility or list order.
 *
 * Returns TCL_OK with integer zero/one. Does not require coordinates or
 * current geometry, and does not report an error for a missing name.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int ExistsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->markers.table, Tcl_GetString(objv[3]));
    Tcl_SetObjResult(interp, Tcl_NewIntObj(hPtr != NULL));
    return TCL_OK;
}

/**
 * @ingroup marker_commands
 * @brief Return the interned class identity for an existing marker.
 *
 * Resolves an exact name and copies classUid into the interpreter result.
 * Returns TCL_ERROR on missing name; does not configure or map the marker.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
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

/**
 * @ingroup marker_commands
 * @brief Validate operation and arity before shared marker command dispatch.
 *
 * Uses the complete widget-command argument vector with the operation at
 * index two. Returns shared-dispatch or handler Tcl status. Does not itself
 * refresh layout or preserve marker records across application callbacks.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in,out] interp Borrowed interpreter for diagnostics and results; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
int Rbc_MarkerOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, markerOps, (Tcl_Size)sizeof(markerOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    return markerOps[index].proc(graphPtr, interp, objc, objv);
}

void Rbc_MarkersExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, int under) {
    Rbc_ChainLink *linkPtr;
    Marker *markerPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->markers.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        markerPtr = Rbc_ChainGetValue(linkPtr);
        if ((markerPtr->classPtr->exportProc == NULL) || (markerPtr->nWorldPts == 0)) {
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
        if (exportPtr->backend == RBC_EXPORT_POSTSCRIPT) {
            Rbc_ExportAppend(exportPtr, "\n% Marker \"", markerPtr->name, "\" is a ", markerPtr->classUid, " marker\n",
                             (char *)NULL);
        }
        if (markerPtr->track != NULL && markerPtr->classUid != rbcWindowMarkerUid) {
            RbcTrackViewport saved;
            Rbc_RenderContext *context;
            if ((markerPtr->track->right - markerPtr->track->left <= 1 ||
                 markerPtr->track->bottom - markerPtr->track->top <= 1) ||
                markerPtr->clipped) {
                continue;
            }
            Rbc_EnterTrack(graphPtr, markerPtr->track, &saved);
            context = Rbc_RenderBeginExportOutput(exportPtr);
            Rbc_RenderClipBegin(context, graphPtr->left, graphPtr->top, graphPtr->right - graphPtr->left + 1,
                                graphPtr->bottom - graphPtr->top + 1);
            (*markerPtr->classPtr->exportProc)(markerPtr, exportPtr);
            Rbc_RenderClipEnd(context);
            Rbc_RenderEnd(context);
            Rbc_LeaveTrack(graphPtr, &saved);
        } else {
            (*markerPtr->classPtr->exportProc)(markerPtr, exportPtr);
        }
    }
}

#if defined(WIN32) || defined(RBC_DOXYGEN)

/**
 * @ingroup marker_batches
 * @brief Draw an eligible Windows text run synchronously in array order.
 *
 * Borrows the marker-pointer array for count entries and delegates each to
 * DrawTextMarker. Renderer services own the shared native target. Retains no
 * pointers and neither filters eligibility nor merges text layouts.
 */
static void DrawTextMarkerRun(void *clientData, Drawable drawable, Tcl_Size count) {
    TextMarker **markers = clientData;
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        DrawTextMarker(&markers[i]->core, drawable);
    }
}

/**
 * @ingroup marker_batches
 * @brief Test eligibility for shared-target Windows text drawing.
 *
 * Requires a string, foreground and layout, with no background GC, disabled/
 * emphasis/active text-style state or visible shadow. Does not require equal
 * fonts/colors across the run and does not reject Cairo: glyph drawing stays
 * native. Outer traversal owns layer and visibility filtering.
 */
static int CanBatchWinTextMarker(TextMarker *tmPtr) {
    if ((tmPtr == NULL) || (tmPtr->string == NULL) || (tmPtr->style.color == NULL) || (tmPtr->textPtr == NULL)) {
        return FALSE;
    }
    /*
     * Initially exclude marker backgrounds.  That keeps this test
     * focused on the text renderer.
     */
    if (tmPtr->fillGC != NULL) {
        return FALSE;
    }
    /*
     * Disabled/emphasis/shadow/active rendering may change the GC
     * foreground while drawing.  Keep the first fast path narrow.
     */
    if (tmPtr->style.state & (STATE_DISABLED | STATE_EMPHASIS | STATE_ACTIVE)) {
        return FALSE;
    }
    if ((tmPtr->style.shadow.offset > 0) && (tmPtr->style.shadow.color != NULL)) {
        return FALSE;
    }
    return TRUE;
}

/**
 * @ingroup marker_batches
 * @brief Test eligibility for the narrow Windows native line batch.
 *
 * Requires nonempty segments/GC and explicit outline, native rendering, no
 * XOR or arrows, solid dashes, exactly one-pixel width, butt caps and miter
 * joins. Dash-gap fill is irrelevant for these solid strokes. Does not test
 * layer, hidden state or associated-element visibility.
 */
static int CanBatchWinLineMarker(LineMarker *lmPtr) {
    if ((lmPtr == NULL) || (lmPtr->gc == NULL) || (lmPtr->segments == NULL) || (lmPtr->nSegments <= 0)) {
        return FALSE;
    }
    if (lmPtr->core.graphPtr->renderer == RBC_RENDERER_CAIRO) {
        return FALSE;
    }
    if (lmPtr->xor) {
        return FALSE;
    }
    if (lmPtr->arrow != LINE_ARROW_NONE) {
        return FALSE;
    }
    if (LineIsDashed(lmPtr->dashes)) {
        return FALSE;
    }
    /*
     * First optimize exactly the common/default marker geometry.
     */
    if (lmPtr->lineWidth != 1) {
        return FALSE;
    }
    if (lmPtr->capStyle != CapButt) {
        return FALSE;
    }
    if (lmPtr->joinStyle != JoinMiter) {
        return FALSE;
    }
    if (lmPtr->outlineColor == NULL) {
        return FALSE;
    }
    /*
     * -fill only matters for double-dashed lines.  Since this fast
     * path accepts only solid lines, it does not affect rendering.
     */
    return TRUE;
}

/**
 * @ingroup marker_batches
 * @brief Test native outline-only polygon batch eligibility.
 *
 * Requires mapped segments/GC, explicit foreground, one-pixel solid butt/
 * miter stroke, non-Cairo and non-XOR. Rejects a drawable fill to preserve
 * fill-before-outline ordering. Outer traversal filters marker visibility.
 */
static int CanBatchWinPolygonOutlineMarker(PolygonMarker *pmPtr) {
    if ((pmPtr == NULL) || (pmPtr->outlineGC == NULL) || (pmPtr->outlinePts == NULL) || (pmPtr->nOutlinePts <= 0)) {
        return FALSE;
    }
    if (pmPtr->core.graphPtr->renderer == RBC_RENDERER_CAIRO) {
        return FALSE;
    }
    /*
     * This fast path is strictly outline-only.  A filled polygon must
     * retain its normal fill-before-outline drawing order.
     */
    if ((pmPtr->nFillPts >= 3) && (pmPtr->fill.fgColor != NULL)) {
        return FALSE;
    }
    if (pmPtr->xor) {
        return FALSE;
    }
    if (LineIsDashed(pmPtr->dashes)) {
        return FALSE;
    }
    if (pmPtr->lineWidth != 1) {
        return FALSE;
    }
    if (pmPtr->capStyle != CapButt) {
        return FALSE;
    }
    if (pmPtr->joinStyle != JoinMiter) {
        return FALSE;
    }
    if (pmPtr->outline.fgColor == NULL) {
        return FALSE;
    }
    return TRUE;
}

/**
 * @ingroup marker_batches
 * @brief Test native solid fill-only polygon batch eligibility.
 *
 * Requires GC, foreground and 3..INT_MAX fill points; rejects Cairo, XOR,
 * stipple, background color and any drawable outline. This narrow predicate
 * makes the subsequent style comparison valid; it does not filter layers.
 */
static int CanBatchWinPolygonFillMarker(PolygonMarker *pmPtr) {
    if ((pmPtr == NULL) || (pmPtr->fillGC == NULL) || (pmPtr->fillPts == NULL) || (pmPtr->nFillPts < 3) ||
        (pmPtr->nFillPts > INT_MAX) || (pmPtr->fill.fgColor == NULL)) {
        return FALSE;
    }
    if (pmPtr->core.graphPtr->renderer == RBC_RENDERER_CAIRO) {
        return FALSE;
    }
    /*
     * Initially optimize only normal solid fills.
     */
    if (pmPtr->xor) {
        return FALSE;
    }
    if (pmPtr->stipple != None) {
        return FALSE;
    }
    /*
     * Keep this path strictly fill-only so the normal
     * fill-before-outline ordering cannot be changed.
     */
    if ((pmPtr->nOutlinePts > 0) && (pmPtr->lineWidth > 0) && (pmPtr->outline.fgColor != NULL)) {
        return FALSE;
    }
    /*
     * A background color is only meaningful for stippling, but keep
     * the initial fast path deliberately narrow.
     */
    if (pmPtr->fill.bgColor != NULL) {
        return FALSE;
    }
    return TRUE;
}

/**
 * @ingroup marker_batches
 * @brief Test opaque unmasked native bitmap batch eligibility.
 *
 * Requires destination bitmap, dimensions, GC and both colors; rejects Cairo
 * and destination masks. Accepts right-angle rotations within DBL_EPSILON
 * after modulo reduction. Arbitrary rotations retain the individual
 * background-polygon/mask path.
 */
static int CanBatchWinBitmapMarker(BitmapMarker *bmPtr) {
    double theta;

    if ((bmPtr == NULL) || (bmPtr->gc == NULL) || (bmPtr->destBitmap == None) || (bmPtr->destWidth <= 0) ||
        (bmPtr->destHeight <= 0)) {
        return FALSE;
    }
    if (bmPtr->core.graphPtr->renderer == RBC_RENDERER_CAIRO) {
        return FALSE;
    }
    /*
     * Only the simple opaque XCopyPlane case.
     */
    if (bmPtr->destMask != None) {
        return FALSE;
    }
    if ((bmPtr->outlineColor == NULL) || (bmPtr->fillColor == NULL)) {
        return FALSE;
    }
    /*
     * Arbitrary rotation with an opaque background uses the special
     * polygon-background + bitmap-mask path in DrawBitmapMarker().
     *
     * Right-angle rotations do not need that workaround.
     */
    theta = FMOD(bmPtr->theta, 90.0);
    if (theta < 0.0) {
        theta += 90.0;
    }
    if (fabs(theta) > DBL_EPSILON) {
        return FALSE;
    }
    return TRUE;
}

/**
 * @ingroup marker_batches
 * @brief Compare foreground pixels of already eligible native line markers.
 *
 * Returns FALSE for NULL records. Eligibility must have established valid
 * colors and identical width/cap/join/dash/XOR characteristics. Borrows inputs
 * and does not compare geometry or GC identity.
 */
static int SameWinLineMarkerStyle(LineMarker *aPtr, LineMarker *bPtr) {
    if ((aPtr == NULL) || (bPtr == NULL)) {
        return FALSE;
    }
    /*
     * CanBatchWinLineMarker has already established identical
     * linewidth/cap/join/dash/XOR characteristics.
     */
    return (aPtr->outlineColor->pixel == bPtr->outlineColor->pixel);
}

/**
 * @ingroup marker_batches
 * @brief Compare foreground pixels of eligible polygon outlines.
 *
 * Returns FALSE for NULL records. Caller must first establish outline-only
 * batch eligibility, including non-NULL foreground and fixed stroke style.
 */
static int SameWinPolygonOutlineStyle(PolygonMarker *aPtr, PolygonMarker *bPtr) {
    if ((aPtr == NULL) || (bPtr == NULL)) {
        return FALSE;
    }
    return (aPtr->outline.fgColor->pixel == bPtr->outline.fgColor->pixel);
}

/**
 * @ingroup marker_batches
 * @brief Compare foreground pixels of eligible solid polygon fills.
 *
 * Returns FALSE for NULL records. Requires prior fill-only eligibility;
 * stipple, background, outline and XOR differences have already been excluded.
 */
static int SameWinPolygonFillStyle(PolygonMarker *aPtr, PolygonMarker *bPtr) {
    if ((aPtr == NULL) || (bPtr == NULL)) {
        return FALSE;
    }
    return (aPtr->fill.fgColor->pixel == bPtr->fill.fgColor->pixel);
}

/**
 * @ingroup marker_batches
 * @brief Compare the shared source and GC colors of eligible bitmap markers.
 *
 * Returns FALSE for NULL records. Otherwise compares destination bitmap
 * identity, dimensions and native GC foreground/background pixels. Placement
 * is deliberately excluded and collected separately. Eligibility guarantees
 * the borrowed GC and bitmap state are valid.
 */
static int SameWinBitmapStyle(BitmapMarker *aPtr, BitmapMarker *bPtr) {
    if ((aPtr == NULL) || (bPtr == NULL)) {
        return FALSE;
    }
    return (aPtr->destBitmap == bPtr->destBitmap) && (aPtr->destWidth == bPtr->destWidth) &&
           (aPtr->destHeight == bPtr->destHeight) && (aPtr->gc->foreground == bPtr->gc->foreground) &&
           (aPtr->gc->background == bPtr->gc->background);
}

/**
 * @ingroup marker_batches
 * @brief Draw pending native line segments and reset batch bookkeeping.
 *
 * Borrows the array, representative marker and its GC. Emits only for a
 * non-NULL style and positive count, then clears style/count even when empty.
 * Does not free the pass-owned array or marker resources.
 */
static void FlushWinLineMarkerBatch(Graph *graphPtr, Drawable drawable, LineMarker **stylePtrPtr, Segment2D *segments,
                                    Tcl_Size *nSegmentsPtr) {
    if ((*stylePtrPtr != NULL) && (*nSegmentsPtr > 0)) {
        Rbc_RenderScreenSegments(graphPtr->tkwin, drawable, (*stylePtrPtr)->gc, segments, *nSegmentsPtr);
    }
    *stylePtrPtr = NULL;
    *nSegmentsPtr = 0;
}

/**
 * @ingroup marker_batches
 * @brief Draw pending polygon-outline segments and reset the batch.
 *
 * Borrows representative outline GC and segment storage. Opens/ends a native
 * segment context only for a nonempty styled batch. Clears style/count;
 * allocation ownership remains with the outer drawing pass.
 */
static void FlushWinPolygonMarkerBatch(Graph *graphPtr, Drawable drawable, PolygonMarker **stylePtrPtr,
                                       Segment2D *segments, Tcl_Size *nSegmentsPtr) {
    if ((*stylePtrPtr != NULL) && (*nSegmentsPtr > 0)) {
        Rbc_RenderContext *ctx = Rbc_RenderBeginNativeSegments(graphPtr, drawable, (*stylePtrPtr)->outlineGC);
        Rbc_RenderSegments(ctx, segments, *nSegmentsPtr);
        Rbc_RenderEnd(ctx);
    }
    *stylePtrPtr = NULL;
    *nSegmentsPtr = 0;
}

/**
 * @ingroup marker_batches
 * @brief Draw a compatible fill run with one native polygon batch context.
 *
 * Borrows markers/style and returns for missing/empty input. Computes maximum
 * point count for renderer scratch capacity, then submits each polygon in
 * array order. Does not merge polygon geometry or assume equal vertex counts.
 */
static void DrawWinPolygonFillMarkers(Graph *graphPtr, Drawable drawable, PolygonMarker *stylePtr,
                                      PolygonMarker **markers, Tcl_Size nMarkers) {
    Rbc_RenderContext *ctx;
    Tcl_Size i, maxPoints = 0;
    if (stylePtr == NULL || markers == NULL || nMarkers <= 0) return;
    for (i = 0; i < nMarkers; i++) {
        if (markers[i]->nFillPts > maxPoints) maxPoints = markers[i]->nFillPts;
    }
    ctx = Rbc_RenderBeginNativePolygonBatch(graphPtr, drawable, stylePtr->fillGC, maxPoints);
    for (i = 0; i < nMarkers; i++) {
        Rbc_RenderFillPolygon(ctx, markers[i]->fillPts, markers[i]->nFillPts);
    }
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup marker_batches
 * @brief Submit pending polygon fills and clear style/count.
 *
 * Delegates nonempty styled runs to DrawWinPolygonFillMarkers. Borrows the
 * marker-pointer array; neither destroys markers nor frees batch storage.
 */
static void FlushWinPolygonFillBatch(Graph *graphPtr, Drawable drawable, PolygonMarker **stylePtrPtr,
                                     PolygonMarker **markers, Tcl_Size *nMarkersPtr) {
    if ((*stylePtrPtr != NULL) && (*nMarkersPtr > 0)) {
        DrawWinPolygonFillMarkers(graphPtr, drawable, *stylePtrPtr, markers, *nMarkersPtr);
    }
    *stylePtrPtr = NULL;
    *nMarkersPtr = 0;
}

/**
 * @ingroup marker_batches
 * @brief Submit a synchronous shared-target text run and clear its count.
 *
 * Borrows the marker-pointer array and dispatches through Rbc_RenderScreenRun
 * and DrawTextMarkerRun. Empty batches do nothing. Array ownership remains
 * with Rbc_DrawMarkers; this helper does not free it.
 */
static void FlushWinTextMarkerBatch(Graph *graphPtr, Drawable drawable, TextMarker **markers, Tcl_Size *nMarkersPtr) {
    if (*nMarkersPtr > 0) {
        Rbc_RenderScreenRun(graphPtr, drawable, markers, *nMarkersPtr, DrawTextMarkerRun);
    }
    *nMarkersPtr = 0;
}

/**
 * @ingroup marker_batches
 * @brief Draw compatible bitmap placements and reset batch bookkeeping.
 *
 * Borrows the representative bitmap/GC/dimensions and position array. Sends
 * a nonempty run to renderer bitmap services, then clears style/count.
 * Does not acquire or release source bitmap ownership.
 */
static void FlushWinBitmapMarkerBatch(Graph *graphPtr, Drawable drawable, BitmapMarker **stylePtrPtr,
                                      Rbc_RenderPosition *positions, Tcl_Size *nPositionsPtr) {
    BitmapMarker *bmPtr = *stylePtrPtr;
    if ((bmPtr != NULL) && (*nPositionsPtr > 0)) {
        Rbc_RenderScreenBitmapRun(graphPtr, drawable, bmPtr->destBitmap, bmPtr->gc,
                                  bmPtr->destWidth, bmPtr->destHeight, positions, *nPositionsPtr);
    }
    *stylePtrPtr = NULL;
    *nPositionsPtr = 0;
}

#endif /* WIN32 */

/**
 * @ingroup marker_batches
 * @brief Borrowed Cairo stroke view valid while its marker caches remain alive.
 *
 * Holds no ownership or lifetime references. Style equality permits context
 * reuse only; each marker must still be submitted as its own stroke.
 */
typedef struct {
    const Segment2D *segments;
    Tcl_Size count;
    const XColor *color;
    int width, cap, join;
} MarkerStroke;

/**
 * @ingroup marker_batches
 * @brief Describe a Cairo-compatible line or polygon-outline stroke.
 *
 * Writes a borrowed segment/color view for non-XOR solid lines without
 * arrows, or polygons without drawable fills. Requires the appropriate GC
 * and returns TRUE only for nonempty geometry and explicit color. Width is
 * normalized to at least one. Output may be partially written on FALSE and
 * must not be used then. Arcs and non-Cairo graphs are excluded.
 */
static int GetMarkerStroke(Marker *markerPtr, MarkerStroke *stroke) {
    if (markerPtr->graphPtr->renderer != RBC_RENDERER_CAIRO) {
        return FALSE;
    }
    if (markerPtr->classUid == rbcLineMarkerUid) {
        LineMarker *line = LINE_MARKER_FROM_CORE(markerPtr);
        if (line->xor || (line->arrow != LINE_ARROW_NONE) || LineIsDashed(line->dashes) || (line->gc == NULL)) {
            return FALSE;
        }
        stroke->segments = line->segments;
        stroke->count = line->nSegments;
        stroke->color = line->outlineColor;
        stroke->width = MAX(1, line->lineWidth);
        stroke->cap = line->capStyle;
        stroke->join = line->joinStyle;
    } else if (markerPtr->classUid == rbcPolygonMarkerUid) {
        PolygonMarker *polygon = POLYGON_MARKER_FROM_CORE(markerPtr);
        if (polygon->xor || LineIsDashed(polygon->dashes) || (polygon->outlineGC == NULL) ||
            ((polygon->nFillPts >= 3) && (polygon->fill.fgColor != NULL))) {
            return FALSE;
        }
        stroke->segments = polygon->outlinePts;
        stroke->count = polygon->nOutlinePts;
        stroke->color = polygon->outline.fgColor;
        stroke->width = MAX(1, polygon->lineWidth);
        stroke->cap = polygon->capStyle;
        stroke->join = polygon->joinStyle;
    } else {
        return FALSE;
    }
    return (stroke->segments != NULL) && (stroke->count > 0) && (stroke->color != NULL);
}

/**
 * @ingroup marker_batches
 * @brief Compare renderer stroke state independently of marker identity.
 *
 * Requires two successful stroke descriptors. Compares width, cap, join and
 * RGB channels, not color-pointer identity or segment storage. A match allows
 * context reuse, not path merging or a single combined alpha operation.
 */
static int SameMarkerStroke(const MarkerStroke *a, const MarkerStroke *b) {
    return (a->width == b->width) && (a->cap == b->cap) && (a->join == b->join) && (a->color->red == b->color->red) &&
           (a->color->green == b->color->green) && (a->color->blue == b->color->blue);
}

#if defined(WIN32) || defined(RBC_DOXYGEN)
/**
 * @ingroup marker_batches
 * @brief Conservatively decide whether a Windows Cairo pass needs a target.
 *
 * Returns FALSE for non-Cairo graphs. Scans the requested layer, skipping
 * hidden/clipped/coordinate-free markers, windows, background-free text and
 * XOR line/polygon markers. Does not repeat associated-element hidden checks,
 * so TRUE can overestimate actual drawing. Owns no render target itself.
 */
static int MarkersNeedRenderTarget(Graph *graphPtr, int under) {
    Rbc_ChainLink *link;

    if (graphPtr->renderer != RBC_RENDERER_CAIRO) {
        return FALSE;
    }
    for (link = Rbc_ChainFirstLink(graphPtr->markers.displayList); link != NULL; link = Rbc_ChainNextLink(link)) {
        Marker *marker = Rbc_ChainGetValue(link);
        if (marker->hidden || marker->clipped || (marker->nWorldPts == 0) || (marker->drawUnder != under)) {
            continue;
        }
        if (marker->classUid == rbcWindowMarkerUid)
            continue;
        if ((marker->classUid == rbcTextMarkerUid) && (TEXT_MARKER_FROM_CORE(marker)->fillGC == NULL)) {
            continue;
        }
        if ((marker->classUid == rbcLineMarkerUid) && LINE_MARKER_FROM_CORE(marker)->xor) {
            continue;
        }
        if ((marker->classUid == rbcPolygonMarkerUid) && POLYGON_MARKER_FROM_CORE(marker)->xor) {
            continue;
        }
        return TRUE;
    }
    return FALSE;
}
#endif

/**
 * @ingroup marker_batches
 * @brief Draw one trackgraph marker layer in painter order with per-track clipping.
 *
 * Skips windows and ineligible visibility/layer/element associations.
 * Graph-wide markers draw directly. Track-owned markers use a lazily created
 * reusable screen clip context with bounded rectangles and paired viewport
 * entry/exit around class drawing. An unavailable clip drawable skips that
 * marker. Ends each clip scope and the reusable context; no ordinary-graph
 * batch arrays or cross-marker stroke context are used.
 */
static void DrawTrackMarkers(Graph *graphPtr, Drawable drawable, int under) {
    Rbc_ChainLink *link;
    Rbc_RenderContext *clip = NULL;
    for (link = Rbc_ChainFirstLink(graphPtr->markers.displayList); link != NULL; link = Rbc_ChainNextLink(link)) {
        Marker *markerPtr = Rbc_ChainGetValue(link);
        if (markerPtr->classUid == rbcWindowMarkerUid || markerPtr->nWorldPts == 0 || markerPtr->drawUnder != under ||
            markerPtr->hidden || markerPtr->clipped) {
            continue;
        }
        if (markerPtr->elemName != NULL) {
            Tcl_HashEntry *entry = Tcl_FindHashEntry(&graphPtr->elements.table, markerPtr->elemName);
            if (entry != NULL && ((Element *)Tcl_GetHashValue(entry))->hidden) {
                continue;
            }
        }
        if (markerPtr->track == NULL) {
            (*markerPtr->classPtr->drawProc)(markerPtr, drawable);
        } else {
            RbcTrackViewport saved;
            int left = MAX(0, markerPtr->track->left);
            int top = MAX(0, markerPtr->track->top);
            int width = MIN(graphPtr->width - 1, markerPtr->track->right) - left + 1;
            int height = MIN(graphPtr->height - 1, markerPtr->track->bottom) - top + 1;
            if (width <= 0 || height <= 2) {
                continue;
            }
            if (clip == NULL) {
                clip = Rbc_RenderBeginScreenClip(graphPtr, drawable, NULL, 0);
            }                
            Rbc_RenderClipBegin(clip, left, top, width, height);
            if (Rbc_RenderClipDrawable(clip) != None) {
                Rbc_EnterTrack(graphPtr, markerPtr->track, &saved);
                (*markerPtr->classPtr->drawProc)(markerPtr, Rbc_RenderClipDrawable(clip));
                Rbc_LeaveTrack(graphPtr, &saved);
            }
            Rbc_RenderClipEnd(clip);
        }
    }
    if (clip != NULL) {
        Rbc_RenderEnd(clip);
    }        
}

/*
 * Synchronize real children first, then select the track or ordinary pass.
 * Windows scratch arrays are sized before traversal; allocation failure falls
 * back to individual class drawing. Flush incompatible visible runs before
 * dispatch and preserve separate Cairo stroke submissions. Renderer contexts
 * and native targets must be ended after the last batch. All pass-owned arrays
 * are released only after their final synchronous flush.
 */
void Rbc_DrawMarkers(Graph *graphPtr, Drawable drawable, int under) {
    Rbc_ChainLink *linkPtr;
    Marker *markerPtr;
    Rbc_RenderContext *strokeContext = NULL;
    MarkerStroke stroke, runStyle = {0};
    int canStroke;
    SyncWindowMarkers(graphPtr);
    if (graphPtr->trackGraph) {
        DrawTrackMarkers(graphPtr, drawable, under);
        return;
    }
#if defined(WIN32) || defined(RBC_DOXYGEN)
    Rbc_RenderTarget *renderTarget = NULL;
    Segment2D *lineSegments;
    LineMarker *lineStylePtr;
    Tcl_Size nLineSegments;
    size_t lineCapacity;
    Segment2D *polygonSegments;
    PolygonMarker *polygonStylePtr;
    Tcl_Size nPolygonSegments;
    size_t polygonCapacity;
    int lineCapacityValid;
    int polygonCapacityValid;
    PolygonMarker **polygonFillMarkers;
    PolygonMarker *polygonFillStylePtr;
    Tcl_Size nPolygonFillMarkers;
    size_t polygonFillCapacity;
    int polygonFillCapacityValid;
    TextMarker **textMarkers;
    Tcl_Size nTextMarkers;
    size_t textCapacity;
    int textCapacityValid;
    Rbc_RenderPosition *bitmapPositions;
    BitmapMarker *bitmapStylePtr;
    Tcl_Size nBitmapPositions;
    size_t bitmapCapacity;
    int bitmapCapacityValid;

    if (MarkersNeedRenderTarget(graphPtr, under)) {
        renderTarget = Rbc_RenderBeginMarkerPass(graphPtr, &drawable);
    }
    polygonFillCapacityValid = TRUE;
    lineCapacityValid = TRUE;
    polygonCapacityValid = TRUE;
    lineSegments = NULL;
    lineStylePtr = NULL;
    nLineSegments = 0;
    lineCapacity = 0;
    polygonSegments = NULL;
    polygonStylePtr = NULL;
    nPolygonSegments = 0;
    polygonCapacity = 0;
    polygonFillMarkers = NULL;
    polygonFillStylePtr = NULL;
    nPolygonFillMarkers = 0;
    polygonFillCapacity = 0;
    textMarkers = NULL;
    nTextMarkers = 0;
    textCapacity = 0;
    textCapacityValid = TRUE;
    bitmapPositions = NULL;
    bitmapStylePtr = NULL;
    nBitmapPositions = 0;
    bitmapCapacity = 0;
    bitmapCapacityValid = TRUE;
    /*
     * Determine the maximum storage that could be needed by the
     * simple line-marker batches.
     *
     * This deliberately overestimates: hidden/clipped markers are
     * counted too.  That keeps the actual drawing pass simple and
     * guarantees that allocation failure cannot occur halfway through
     * rendering a batch.
     */
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->markers.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        markerPtr = Rbc_ChainGetValue(linkPtr);
        if (markerPtr->classUid == rbcLineMarkerUid) {
            LineMarker *lmPtr;

            lmPtr = LINE_MARKER_FROM_CORE(markerPtr);
            if (lineCapacityValid && CanBatchWinLineMarker(lmPtr)) {
                if ((size_t)lmPtr->nSegments > (SIZE_MAX / sizeof(*lineSegments)) - lineCapacity) {
                    lineCapacity = 0;
                    lineCapacityValid = FALSE;
                } else {
                    lineCapacity += (size_t)lmPtr->nSegments;
                }
            }
            continue;
        }
        if (markerPtr->classUid == rbcPolygonMarkerUid) {
            PolygonMarker *pmPtr;

            pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
            if (CanBatchWinPolygonOutlineMarker(pmPtr)) {
                if (polygonCapacityValid) {
                    if ((size_t)pmPtr->nOutlinePts > (SIZE_MAX / sizeof(*polygonSegments)) - polygonCapacity) {
                        polygonCapacity = 0;
                        polygonCapacityValid = FALSE;
                    } else {
                        polygonCapacity += (size_t)pmPtr->nOutlinePts;
                    }
                }
                continue;
            }
            if (CanBatchWinPolygonFillMarker(pmPtr)) {
                if (polygonFillCapacityValid) {
                    if (polygonFillCapacity == SIZE_MAX) {
                        polygonFillCapacity = 0;
                        polygonFillCapacityValid = FALSE;
                    } else {
                        polygonFillCapacity++;
                    }
                }
                continue;
            }
        }
        if (markerPtr->classUid == rbcTextMarkerUid) {
            TextMarker *tmPtr;

            tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);
            if (textCapacityValid && CanBatchWinTextMarker(tmPtr)) {
                if (textCapacity == SIZE_MAX) {
                    textCapacity = 0;
                    textCapacityValid = FALSE;
                } else {
                    textCapacity++;
                }
            }
            continue;
        }
        if (markerPtr->classUid == rbcBitmapMarkerUid) {
            BitmapMarker *bmPtr;

            bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);
            if (bitmapCapacityValid && CanBatchWinBitmapMarker(bmPtr)) {
                if (bitmapCapacity == SIZE_MAX) {
                    bitmapCapacity = 0;
                    bitmapCapacityValid = FALSE;
                } else {
                    bitmapCapacity++;
                }
            }
            continue;
        }
    }
    if (lineCapacity > 0) {
        lineSegments = Tcl_AttemptAlloc(lineCapacity * sizeof(*lineSegments));
        if (lineSegments == NULL) {
            lineCapacity = 0;
        }
    }
    if (polygonCapacity > 0) {
        polygonSegments = Tcl_AttemptAlloc(polygonCapacity * sizeof(*polygonSegments));
        if (polygonSegments == NULL) {
            polygonCapacity = 0;
        }
    }
    if (polygonFillCapacity > 0) {
        if (polygonFillCapacity > SIZE_MAX / sizeof(*polygonFillMarkers)) {
            polygonFillCapacity = 0;
        } else {
            polygonFillMarkers = Tcl_AttemptAlloc(polygonFillCapacity * sizeof(*polygonFillMarkers));
            if (polygonFillMarkers == NULL) {
                polygonFillCapacity = 0;
            }
        }
    }
    if (textCapacity > 0) {
        if (textCapacity > SIZE_MAX / sizeof(*textMarkers)) {
            textCapacity = 0;
        } else {
            textMarkers = Tcl_AttemptAlloc(textCapacity * sizeof(*textMarkers));
            if (textMarkers == NULL) {
                textCapacity = 0;
            }
        }
    }
    if (bitmapCapacity > 0) {
        if (bitmapCapacity > SIZE_MAX / sizeof(*bitmapPositions)) {
            bitmapCapacity = 0;
        } else {
            bitmapPositions = Tcl_AttemptAlloc(bitmapCapacity * sizeof(*bitmapPositions));
            if (bitmapPositions == NULL) {
                bitmapCapacity = 0;
            }
        }
    }
#endif /* WIN32 */
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->markers.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        markerPtr = Rbc_ChainGetValue(linkPtr);
        if (markerPtr->classUid == rbcWindowMarkerUid || (markerPtr->nWorldPts == 0) ||
            (markerPtr->drawUnder != under) || (markerPtr->hidden) || (markerPtr->clipped)) {
            continue;
        }
        if (markerPtr->elemName != NULL) {
            Tcl_HashEntry *hPtr;

            /*
             * Look up the named element and see if it is hidden.
             */
            hPtr = Tcl_FindHashEntry(&graphPtr->elements.table, markerPtr->elemName);
            if (hPtr != NULL) {
                Element *elemPtr;

                elemPtr = (Element *)Tcl_GetHashValue(hPtr);
                if (elemPtr->hidden) {
                    continue;
                }
            }
        }

        canStroke = GetMarkerStroke(markerPtr, &stroke);
        if ((strokeContext != NULL) && (!canStroke || !SameMarkerStroke(&runStyle, &stroke))) {
            Rbc_RenderEnd(strokeContext);
            strokeContext = NULL;
        }
#if defined(WIN32) || defined(RBC_DOXYGEN)
        /*
         * Batch consecutive compatible simple line markers.
         *
         * A non-line marker or incompatible line marker flushes the
         * batch before it is drawn, preserving display-list order.
         */
        if ((lineSegments != NULL) && (markerPtr->classUid == rbcLineMarkerUid)) {
            LineMarker *lmPtr;

            lmPtr = LINE_MARKER_FROM_CORE(markerPtr);
            if (CanBatchWinLineMarker(lmPtr)) {
                FlushWinBitmapMarkerBatch(graphPtr, drawable, &bitmapStylePtr, bitmapPositions, &nBitmapPositions);
                FlushWinTextMarkerBatch(graphPtr, drawable, textMarkers, &nTextMarkers);
                FlushWinPolygonMarkerBatch(graphPtr, drawable, &polygonStylePtr, polygonSegments, &nPolygonSegments);
                FlushWinPolygonFillBatch(graphPtr, drawable, &polygonFillStylePtr, polygonFillMarkers,
                                         &nPolygonFillMarkers);
                if ((lineStylePtr != NULL) && (!SameWinLineMarkerStyle(lineStylePtr, lmPtr))) {
                    FlushWinLineMarkerBatch(graphPtr, drawable, &lineStylePtr, lineSegments, &nLineSegments);
                }
                if (lineStylePtr == NULL) {
                    lineStylePtr = lmPtr;
                }
                /*
                 * lineCapacity was calculated over every eligible
                 * marker, so this copy cannot exceed the allocation.
                 */
                memcpy(lineSegments + nLineSegments, lmPtr->segments, (size_t)lmPtr->nSegments * sizeof(*lineSegments));
                nLineSegments += lmPtr->nSegments;
                /*
                 * Do not call the marker's individual drawProc.
                 */
                continue;
            }
        }
        if ((polygonSegments != NULL) && (markerPtr->classUid == rbcPolygonMarkerUid)) {
            PolygonMarker *pmPtr;

            pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
            if (CanBatchWinPolygonOutlineMarker(pmPtr)) {
                /*
                 * Preserve display-list ordering across marker classes.
                 */
                FlushWinBitmapMarkerBatch(graphPtr, drawable, &bitmapStylePtr, bitmapPositions, &nBitmapPositions);
                FlushWinTextMarkerBatch(graphPtr, drawable, textMarkers, &nTextMarkers);
                FlushWinLineMarkerBatch(graphPtr, drawable, &lineStylePtr, lineSegments, &nLineSegments);
                FlushWinPolygonFillBatch(graphPtr, drawable, &polygonFillStylePtr, polygonFillMarkers,
                                         &nPolygonFillMarkers);
                if ((polygonStylePtr != NULL) && (!SameWinPolygonOutlineStyle(polygonStylePtr, pmPtr))) {
                    FlushWinPolygonMarkerBatch(graphPtr, drawable, &polygonStylePtr, polygonSegments,
                                               &nPolygonSegments);
                }
                if (polygonStylePtr == NULL) {
                    polygonStylePtr = pmPtr;
                }
                memcpy(polygonSegments + nPolygonSegments, pmPtr->outlinePts,
                       (size_t)pmPtr->nOutlinePts * sizeof(*polygonSegments));
                nPolygonSegments += pmPtr->nOutlinePts;
                continue;
            }
        }
        if ((polygonFillMarkers != NULL) && (markerPtr->classUid == rbcPolygonMarkerUid)) {
            PolygonMarker *pmPtr;

            pmPtr = POLYGON_MARKER_FROM_CORE(markerPtr);
            if (CanBatchWinPolygonFillMarker(pmPtr)) {
                /*
                 * Preserve ordering with preceding line and outline markers.
                 */
                FlushWinBitmapMarkerBatch(graphPtr, drawable, &bitmapStylePtr, bitmapPositions, &nBitmapPositions);
                FlushWinTextMarkerBatch(graphPtr, drawable, textMarkers, &nTextMarkers);
                FlushWinLineMarkerBatch(graphPtr, drawable, &lineStylePtr, lineSegments, &nLineSegments);
                FlushWinPolygonMarkerBatch(graphPtr, drawable, &polygonStylePtr, polygonSegments, &nPolygonSegments);
                if ((polygonFillStylePtr != NULL) && (!SameWinPolygonFillStyle(polygonFillStylePtr, pmPtr))) {
                    FlushWinPolygonFillBatch(graphPtr, drawable, &polygonFillStylePtr, polygonFillMarkers,
                                             &nPolygonFillMarkers);
                }
                if (polygonFillStylePtr == NULL) {
                    polygonFillStylePtr = pmPtr;
                }
                polygonFillMarkers[nPolygonFillMarkers++] = pmPtr;
                continue;
            }
        }
        if ((textMarkers != NULL) && (markerPtr->classUid == rbcTextMarkerUid)) {
            TextMarker *tmPtr;

            tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);
            if (CanBatchWinTextMarker(tmPtr)) {
                /*
                 * Preserve marker display-list ordering.
                 */
                FlushWinBitmapMarkerBatch(graphPtr, drawable, &bitmapStylePtr, bitmapPositions, &nBitmapPositions);
                FlushWinLineMarkerBatch(graphPtr, drawable, &lineStylePtr, lineSegments, &nLineSegments);
                FlushWinPolygonMarkerBatch(graphPtr, drawable, &polygonStylePtr, polygonSegments, &nPolygonSegments);
                FlushWinPolygonFillBatch(graphPtr, drawable, &polygonFillStylePtr, polygonFillMarkers,
                                         &nPolygonFillMarkers);
                textMarkers[nTextMarkers++] = tmPtr;
                continue;
            }
        }
        if ((bitmapPositions != NULL) && (markerPtr->classUid == rbcBitmapMarkerUid)) {
            BitmapMarker *bmPtr;

            bmPtr = BITMAP_MARKER_FROM_CORE(markerPtr);
            if (CanBatchWinBitmapMarker(bmPtr)) {
                /*
                 * Everything preceding this bitmap marker must already
                 * have been rendered.
                 */
                FlushWinLineMarkerBatch(graphPtr, drawable, &lineStylePtr, lineSegments, &nLineSegments);
                FlushWinPolygonMarkerBatch(graphPtr, drawable, &polygonStylePtr, polygonSegments, &nPolygonSegments);
                FlushWinPolygonFillBatch(graphPtr, drawable, &polygonFillStylePtr, polygonFillMarkers,
                                         &nPolygonFillMarkers);
                FlushWinTextMarkerBatch(graphPtr, drawable, textMarkers, &nTextMarkers);
                if ((bitmapStylePtr != NULL) && (!SameWinBitmapStyle(bitmapStylePtr, bmPtr))) {
                    FlushWinBitmapMarkerBatch(graphPtr, drawable, &bitmapStylePtr, bitmapPositions, &nBitmapPositions);
                }
                if (bitmapStylePtr == NULL) {
                    bitmapStylePtr = bmPtr;
                }
                bitmapPositions[nBitmapPositions][0] = (int)bmPtr->anchorPos.x;
                bitmapPositions[nBitmapPositions][1] = (int)bmPtr->anchorPos.y;
                nBitmapPositions++;
                continue;
            }
        }
        /*
         * Anything that must be drawn individually has to appear
         * after all preceding batched line markers.
         */
        FlushWinBitmapMarkerBatch(graphPtr, drawable, &bitmapStylePtr, bitmapPositions, &nBitmapPositions);
        FlushWinTextMarkerBatch(graphPtr, drawable, textMarkers, &nTextMarkers);
        FlushWinLineMarkerBatch(graphPtr, drawable, &lineStylePtr, lineSegments, &nLineSegments);
        FlushWinPolygonMarkerBatch(graphPtr, drawable, &polygonStylePtr, polygonSegments, &nPolygonSegments);
        FlushWinPolygonFillBatch(graphPtr, drawable, &polygonFillStylePtr, polygonFillMarkers, &nPolygonFillMarkers);
#endif /* WIN32 */
        if (canStroke) {
            if (strokeContext == NULL) {
                strokeContext = Rbc_RenderBegin(graphPtr, drawable, stroke.color, stroke.width, NULL, NULL);
                if (strokeContext != NULL) {
                    Rbc_RenderLineStyle(strokeContext, stroke.cap, stroke.join);
                    runStyle = stroke;
                }
            }
            if (strokeContext != NULL) {
                /* Do not merge paths: overlapping AA markers must composite separately. */
                Rbc_RenderSegments(strokeContext, stroke.segments, stroke.count);
                continue;
            }
        }
        (*markerPtr->classPtr->drawProc)(markerPtr, drawable);
    }
    if (strokeContext != NULL)
        Rbc_RenderEnd(strokeContext);
#if defined(WIN32) || defined(RBC_DOXYGEN)
    /*
     * Flush a batch ending at the end of the display list.
     */
    FlushWinBitmapMarkerBatch(graphPtr, drawable, &bitmapStylePtr, bitmapPositions, &nBitmapPositions);
    FlushWinTextMarkerBatch(graphPtr, drawable, textMarkers, &nTextMarkers);
    FlushWinLineMarkerBatch(graphPtr, drawable, &lineStylePtr, lineSegments, &nLineSegments);
    FlushWinPolygonMarkerBatch(graphPtr, drawable, &polygonStylePtr, polygonSegments, &nPolygonSegments);
    FlushWinPolygonFillBatch(graphPtr, drawable, &polygonFillStylePtr, polygonFillMarkers, &nPolygonFillMarkers);
    if (lineSegments != NULL) {
        ckfree(lineSegments);
    }
    if (polygonSegments != NULL) {
        ckfree(polygonSegments);
    }
    if (polygonFillMarkers != NULL) {
        ckfree(polygonFillMarkers);
    }
    if (textMarkers != NULL) {
        ckfree(textMarkers);
    }
    if (bitmapPositions != NULL) {
        ckfree(bitmapPositions);
    }
    Rbc_RenderEndMarkerPass(renderTarget);
#endif /* WIN32 */
}

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
            RbcTrackViewport saved;
            Rbc_EnterTrack(graphPtr, markerPtr->track, &saved);
            (*markerPtr->classPtr->mapProc)(markerPtr);
            Rbc_LeaveTrack(graphPtr, &saved);
            markerPtr->flags &= ~MAP_ITEM;
        }
    }
}

/**
 * @ingroup marker_mapping_core
 * @brief Test marker associations independently of display visibility.
 *
 * Scans the registry, including hidden and coordinate-free markers. NULL
 * track means graph-wide markers; NULL axis matches all markers in that scope.
 * Otherwise compares the marker Y-axis pointer. Borrows all records.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] track Borrowed track selector, or NULL for graph-wide associations.
 * @param[in] axis Borrowed Y-axis selector, or NULL to match every axis in the selected scope.
 */
int Rbc_MarkersUseTrack(Graph *graphPtr, RbcTrack *track, Axis *axis) {
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    for (entry = Tcl_FirstHashEntry(&graphPtr->markers.table, &search); entry != NULL;
         entry = Tcl_NextHashEntry(&search)) {
        Marker *markerPtr = Tcl_GetHashValue(entry);
        if (markerPtr->track == track && (axis == NULL || markerPtr->axes.y == axis)) {
            return TRUE;
        }
    }
    return FALSE;
}

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

/**
 * @ingroup marker_mapping_core
 * @brief Pick the first eligible marker in reverse painter order.
 *
 * Despite its name, this returns the first successful class point test rather
 * than comparing distances. Requires mapped geometry. Filters by layer, nonempty
 * coordinates, hidden flag and STATE_NORMAL; active/disabled states are excluded.
 * Local picks enforce visible track bounds and clipping. Does not apply the
 * associated-element hidden check used by drawing/FindOp. Returns borrowed
 * marker or NULL, without layout refresh or lifetime preservation.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 * @param[in] x Query x coordinate in graph-window pixels.
 * @param[in] y Query y coordinate in graph-window pixels.
 * @param[in] under Layer selector: nonzero selects under markers, zero selects overlays.
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
            if (markerPtr->track != NULL && (markerPtr->clipped ||
                                             (markerPtr->track->right - markerPtr->track->left <= 1 ||
                                              markerPtr->track->bottom - markerPtr->track->top <= 1) ||
                                             x < markerPtr->track->left || x > markerPtr->track->right ||
                                             y < markerPtr->track->top || y > markerPtr->track->bottom)) {
                continue;
            }
            if ((*markerPtr->classPtr->pointProc)(markerPtr, &point)) {
                return markerPtr;
            }
        }
    }
    return NULL;
}

/**
 * @ingroup marker_mapping_core
 * @brief Refresh all text-marker layouts after a font change.
 *
 * Scans the registry including hidden text markers. Resets text style, replaces
 * layout and rotated bounding geometry when text exists, then marks mapping
 * dirty. Borrows graph/marker records; non-text markers are unaffected.
 *
 * @param[in,out] graphPtr Borrowed owning graph; callers keep its required Tk and mapping resources valid.
 */
void Rbc_MarkerFontsChanged(Graph *graphPtr) {
    Tcl_HashSearch iter;
    Tcl_HashEntry *entryPtr;

    for (entryPtr = Tcl_FirstHashEntry(&graphPtr->markers.table, &iter); entryPtr != NULL;
         entryPtr = Tcl_NextHashEntry(&iter)) {
        Marker *markerPtr = Tcl_GetHashValue(entryPtr);
        TextMarker *tmPtr;

        if (markerPtr->classUid != rbcTextMarkerUid) {
            continue;
        }
        tmPtr = TEXT_MARKER_FROM_CORE(markerPtr);
        if (tmPtr->style.font == NULL) {
            continue;
        }
        Rbc_ResetTextStyle(graphPtr->tkwin, &tmPtr->style);
        if (tmPtr->textPtr != NULL) {
            ckfree(tmPtr->textPtr);
            tmPtr->textPtr = NULL;
        }
        if (tmPtr->string != NULL) {
            double width, height;
            int i;

            tmPtr->textPtr = Rbc_GetTextLayout(tmPtr->string, &tmPtr->style);
            Rbc_GetBoundingBox(tmPtr->textPtr->width, tmPtr->textPtr->height, tmPtr->style.theta, &width, &height,
                               tmPtr->outline);
            tmPtr->width = ROUND(width);
            tmPtr->height = ROUND(height);
            for (i = 0; i < 4; i++) {
                tmPtr->outline[i].x += ROUND(width * 0.5);
                tmPtr->outline[i].y += ROUND(height * 0.5);
            }
            tmPtr->outline[4] = tmPtr->outline[0];
        }
        markerPtr->flags |= MAP_ITEM;
    }
}
