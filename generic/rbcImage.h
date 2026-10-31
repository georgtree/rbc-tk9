/**
 * @file rbcImage.h
 * @brief Private image storage and tile resource services.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCIMAGE
#define _RBCIMAGE

#include <tk.h>
#include "rbcTypes.h"
#ifdef WIN32
#include "rbcWinConfig.h"
#else
#include "rbcConfig.h"
#endif
#include <X11/Xutil.h>
#ifndef WIN32
#include <X11/Xproto.h>
#endif

#define DIV255(i) ((((i) + 1) + (((i) + 1) >> 8)) >> 8)

#define GAMMA (1.0)

#define ROTATE_0 0
#define ROTATE_90 1
#define ROTATE_180 2
#define ROTATE_270 3

/*
 *----------------------------------------------------------------------
 *
 * Pix32 --
 *
 *      A union representing either a pixel as a RGB triplet or a
 *    single word value.
 *
 *----------------------------------------------------------------------
 */
typedef union {
    unsigned int value; /* Lookup table address */
    struct RGBA {
        unsigned char red;   /* Red intensity 0..255 */
        unsigned char green; /* Green intensity 0.255 */
        unsigned char blue;  /* Blue intensity 0..255 */
        unsigned char alpha; /* Alpha-channel for compositing. 0..255 */
    } rgba;
    unsigned char channel[4];
} Pix32;



/*
 *----------------------------------------------------------------------
 *
 * Rbc_ColorImage --
 *
 *      The structure below represents a color image.  Each pixel
 *    occupies a 32-bit word of memory: one byte for each of the
 *    red, green, and blue color intensities, and another for
 *    alpha-channel image compositing (e.g. transparency).
 *
 *----------------------------------------------------------------------
 */
typedef struct ColorImage {
    int width, height; /* Dimensions of the image */
    Pix32 *bits;       /* Array of pixels representing the image. */
} *Rbc_ColorImage;

/*
 * Rbc_ColorImage is supposed to be an opaque type.
 * Use the macros below to access its members.
 */
#define Rbc_ColorImageHeight(c) ((c)->height)
#define Rbc_ColorImageWidth(c) ((c)->width)
#define Rbc_ColorImageBits(c) ((c)->bits)
#define Rbc_ColorImagePixel(c, x, y) ((c)->bits + ((size_t)(c)->width * (size_t)(y)) + (size_t)(x))

/*
 *----------------------------------------------------------------------
 *
 * ResampleFilterProc --
 *
 *      A function implementing a 1-D filter.
 *
 *----------------------------------------------------------------------
 */
typedef double(ResampleFilterProc)(double value);

/*
 *----------------------------------------------------------------------
 *
 * ResampleFilter --
 *
 *      Contains information about a 1-D filter (its support and
 *    the procedure implementing the filter).
 *
 *----------------------------------------------------------------------
 */
typedef struct {
    const char *name;         /* Name of the filter */
    ResampleFilterProc *proc; /* 1-D filter procedure. */
    double support;           /* Width of 1-D filter */
} ResampleFilter;

extern ResampleFilter *rbcBoxFilterPtr; /* The ubiquitous box filter */

/*
 *----------------------------------------------------------------------
 *
 * Filter2D --
 *
 *      Defines a convolution mask for a 2-D filter.  Used to smooth or
 *    enhance images.
 *
 *----------------------------------------------------------------------
 */
typedef struct {
    double support;    /* Radius of filter */
    double sum, scale; /* Sum of kernel */
    double *kernel;    /* Array of values (malloc-ed) representing
                        * the discrete 2-D filter. */
} Filter2D;

/* Prototypes of image routines */

void Rbc_ColorImageToGreyscale(Rbc_ColorImage image);
/**
 * @brief Replace a borrowed Tk photo's size and pixels from an RBC image.
 * Source ownership stays with the caller; Tk copies the block synchronously.
 * Uses TK_PHOTO_COMPOSITE_SET, including alpha. This legacy void helper ignores
 * return codes from Tk_PhotoSetSize and Tk_PhotoPutBlock; it does not provide a
 * reliable success/failure status to its caller.
 */
void Rbc_ColorImageToPhoto(Tcl_Interp *interp, Rbc_ColorImage image, Tk_PhotoHandle photo);
Rbc_ColorImage Rbc_ConvolveColorImage(Rbc_ColorImage srcImage, Filter2D *filter);
/**
 * @brief Allocate an owned row-major RGBA pixel buffer and image record.
 * Dimensions must be nonnegative; invalid or overflowing sizes panic. Pixels
 * start zeroed, including alpha. Release with Rbc_FreeColorImage. The returned
 * image has no Tk image identity, reference count or change notification.
 */
Rbc_ColorImage Rbc_CreateColorImage(int width, int height);
Rbc_ColorImage Rbc_DrawableToColorImage(Tk_Window tkwin, Drawable drawable, int x, int y, int width, int height,
                                        double inputGamma);
/**
 * @brief Resolve a filter name or unambiguous abbreviation to borrowed static state.
 *
 * Return TCL_OK and a filter-table pointer, or NULL for the name none. On
 * lookup failure return TCL_ERROR without assigning the output pointer.
 * Do not free or modify a returned filter. NULL selects policy in callers;
 * it is not a valid filter argument to Rbc_ResampleColorImage().
 */
int Rbc_GetResampleFilterFromObj(Tcl_Interp *interp, Tcl_Obj *filterObj, ResampleFilter **filterPtrPtr);
/**
 * @brief Release both the pixel array and its owning image record.
 * Requires a valid non-NULL owned image. All borrowed pixel pointers become
 * invalid. This does not delete or modify any Tk photo used to create it.
 */
void Rbc_FreeColorImage(Rbc_ColorImage image);
#if HAVE_JPEG
Rbc_ColorImage Rbc_JPEGToColorImage(Tcl_Interp *interp, const char *fileName);
#endif
/**
 * @brief Copy a valid Tk photo into a newly owned RBC color image.
 * Copies pixels using the photo block's pitch and channel offsets. The result
 * is independent of later photo changes; release it with Rbc_FreeColorImage.
 */
Rbc_ColorImage Rbc_PhotoToColorImage(Tk_PhotoHandle photo);
/**
 * @brief Copy the clipped intersection of a photo region into an owned image.
 * Requires a valid photo handle. An empty intersection returns an empty image,
 * not NULL. The result owns its pixels and must be freed with Rbc_FreeColorImage.
 */
Rbc_ColorImage Rbc_PhotoRegionToColorImage(Tk_PhotoHandle photo, int x, int y, int width, int height);
int Rbc_QuantizeColorImage(Rbc_ColorImage src, Rbc_ColorImage dest, int nColors);
/**
 * @brief Return a newly owned image filtered horizontally and then vertically.
 *
 * Borrow the source and two valid, non-NULL filters; leave the source intact.
 * Release the result with Rbc_FreeColorImage(). Negative destination dimensions
 * or missing filters panic. Zero dimensions are allowed; empty input leaves
 * zero-initialized output. Each pass filters straight RGBA channels separately,
 * with fixed-point weights and clamping, without gamma conversion.
 */
Rbc_ColorImage Rbc_ResampleColorImage(Rbc_ColorImage image, int destWidth, int destHeight,
                                      ResampleFilter *horzFilterPtr, ResampleFilter *vertFilterPtr);
/**
 * @brief Filter a source photo region into the destination photo's existing dimensions.
 *
 * Snapshot the source region before writing, allowing identical photo handles.
 * Borrow both handles and valid non-NULL filters. Temporary RBC images are
 * released internally. The void photo-write wrapper does not propagate Tk's
 * write status; callers must not interpret return as a success code.
 */
void Rbc_ResamplePhoto(Tcl_Interp *interp, Tk_PhotoHandle srcPhoto, int x, int y, int width, int height,
                       Tk_PhotoHandle destPhoto, ResampleFilter *horzFilterPtr, ResampleFilter *vertFilterPtr);
/**
 * @brief Copy a source region into a newly owned image using nearest-neighbour mapping.
 *
 * Borrow src. Clamp the origin to source bounds; negative or excessive spans
 * extend to the remaining edge. Scale that effective region to destWidth by
 * destHeight. Empty regions leave zero-initialized output. Negative destination
 * dimensions panic; zero dimensions are allowed. Free the independent result
 * with Rbc_FreeColorImage(). All four channels are copied unchanged.
 */
Rbc_ColorImage Rbc_ResizeColorImage(Rbc_ColorImage src, int x, int y, int width, int height, int destWidth,
                                    int destHeight);
/**
 * @brief Sample a region of a virtually resized source without allocating the full resize.
 *
 * Here x and y are virtual destination coordinates, width and height are the
 * returned region size, and destWidth/destHeight are the virtual full size.
 * Nearest-neighbour coordinates clamp to source edges, including regions beyond
 * the virtual image. Borrow src and free the result with Rbc_FreeColorImage().
 * Negative region dimensions panic; empty regions return immediately. A
 * nonempty region requires positive virtual dimensions. Empty source images
 * leave zero-initialized output.
 */
Rbc_ColorImage Rbc_ResizeColorSubimage(Rbc_ColorImage src, int x, int y, int width, int height, int destWidth,
                                       int destHeight);
Rbc_ColorImage Rbc_RotateColorImage(Rbc_ColorImage image, double theta);
/**
 * @brief Resize a source photo region to the destination's existing size without filtering.
 *
 * Borrow both handles. Snapshot before writing so source and destination may
 * be the same photo. Release temporary RBC images internally. As with
 * Rbc_ResamplePhoto(), this void wrapper does not return the Tk write status.
 */
void Rbc_ResizePhoto(Tcl_Interp *interp, Tk_PhotoHandle srcPhoto, int x, int y, int width, int height,
                     Tk_PhotoHandle destPhoto);
int Rbc_SnapPhoto(Tcl_Interp *interp, Tk_Window tkwin, Drawable drawable, int x, int y, int width, int height,
                  int destWidth, int destHeight, const char *photoName, double inputGamma);
Region2D *Rbc_SetRegion(int x, int y, int width, int height, Region2D *regionPtr);
/* Missing routines from the Tk photo C API */
int Tk_ImageIsDeleted(Tk_Image tkImage);
Tk_ImageMaster Tk_ImageGetMaster(Tk_Image tkImage);
Tk_ImageType *Tk_ImageGetType(Tk_Image tkImage);
Pixmap Tk_ImageGetPhotoPixmap(Tk_Image photoImage);
GC Tk_ImageGetPhotoGC(Tk_Image photoImage);
const char *Rbc_NameOfImage(Tk_Image tkImage);
Tk_Image Rbc_CreateTemporaryImage(Tcl_Interp *interp, Tk_Window tkwin, ClientData clientData);
int Rbc_DestroyTemporaryImage(Tcl_Interp *interp, Tk_Image tkImage);
GC Rbc_GetBitmapGC(Tk_Window tkwin);
Pixmap Rbc_PhotoImageMask(Tk_Window tkwin, Tk_PhotoImageBlock src);
Pixmap Rbc_RotateBitmap(Tk_Window tkwin, Pixmap bitmap, int width, int height, double theta, int *widthPtr,
                        int *heightPtr);
Pixmap Rbc_ScaleBitmap(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight, int scaledWidth,
                       int scaledHeight);
Pixmap Rbc_ScaleRotateBitmapRegion(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight, int regionX,
                                   int regionY, int regionWidth, int regionHeight, int virtWidth, int virtHeight,
                                   double theta);
#endif /* _RBCIMAGE */
