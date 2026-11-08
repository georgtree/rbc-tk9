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

/** @brief Four-channel pixel with packed and byte-array views. */
typedef union {
    unsigned int value; /**< Packed pixel word; byte interpretation is shared with rgba. */
    /** @brief Pixel channel bytes; alpha represents opacity. */
    struct RGBA {
        unsigned char red; /**< Red intensity 0..255 */
        unsigned char green; /**< Green intensity 0..255 */
        unsigned char blue; /**< Blue intensity 0..255 */
        unsigned char alpha; /**< Alpha-channel for compositing. 0..255 */
    } rgba; /**< Named channel view of this pixel. */
    unsigned char channel[4]; /**< Channel-byte array sharing storage with rgba and value; byte order follows RGBA member order. */
} Pix32;



/** @brief Owned packed pixel array and its dimensions. */
typedef struct ColorImage {
    int width; /**< Dimensions of the image */
    int height; /**< Dimensions of the image */
    Pix32 *bits; /**< Array of pixels representing the image. */
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

/** @brief Named separable reconstruction filter and support radius. */
typedef struct {
    const char *name; /**< Name of the filter */
    ResampleFilterProc *proc; /**< 1-D filter procedure. */
    double support; /**< Width of 1-D filter */
} ResampleFilter;

extern ResampleFilter *rbcBoxFilterPtr; /* The ubiquitous box filter */

/** @brief Two-dimensional convolution kernel and normalization state. */
typedef struct {
    double support; /**< Radius of filter */
    double sum; /**< Sum of kernel */
    double scale; /**< Sum of kernel */
    double *kernel; /**< Array of values (allocated) representing the discrete 2-D filter. */
} Filter2D;

/* Prototypes of image routines */

void Rbc_ColorImageToGreyscale(Rbc_ColorImage image);
void Rbc_ColorImageToPhoto(Tcl_Interp *interp, Rbc_ColorImage src, Tk_PhotoHandle photo);
Rbc_ColorImage Rbc_ConvolveColorImage(Rbc_ColorImage src, Filter2D *filterPtr);
Rbc_ColorImage Rbc_CreateColorImage(int width, int height);
Rbc_ColorImage Rbc_DrawableToColorImage(Tk_Window tkwin, Drawable drawable, int x, int y, int width, int height,
                                        double inputGamma);
int Rbc_GetResampleFilterFromObj(Tcl_Interp *interp, Tcl_Obj *filterObj, ResampleFilter **filterPtrPtr);
void Rbc_FreeColorImage(Rbc_ColorImage image);
#if HAVE_JPEG
Rbc_ColorImage Rbc_JPEGToColorImage(Tcl_Interp *interp, const char *fileName);
#endif
Rbc_ColorImage Rbc_PhotoToColorImage(Tk_PhotoHandle photo);
Rbc_ColorImage Rbc_PhotoRegionToColorImage(Tk_PhotoHandle photo, int x, int y, int width, int height);
int Rbc_QuantizeColorImage(Rbc_ColorImage src, Rbc_ColorImage dest, int reduceColors);
Rbc_ColorImage Rbc_ResampleColorImage(Rbc_ColorImage src, int width, int height,
                                      ResampleFilter *horzFilterPtr, ResampleFilter *vertFilterPtr);
void Rbc_ResamplePhoto(Tcl_Interp *interp, Tk_PhotoHandle srcPhoto, int x, int y, int width, int height,
                       Tk_PhotoHandle destPhoto, ResampleFilter *horzFilterPtr, ResampleFilter *vertFilterPtr);
Rbc_ColorImage Rbc_ResizeColorImage(Rbc_ColorImage src, int x, int y, int width, int height, int destWidth,
                                    int destHeight);
Rbc_ColorImage Rbc_ResizeColorSubimage(Rbc_ColorImage src, int regionX, int regionY, int regionWidth, int regionHeight, int destWidth,
                                       int destHeight);
Rbc_ColorImage Rbc_RotateColorImage(Rbc_ColorImage src, double angle);
void Rbc_ResizePhoto(Tcl_Interp *interp, Tk_PhotoHandle srcPhoto, int x, int y, int width, int height,
                     Tk_PhotoHandle destPhoto);
int Rbc_SnapPhoto(Tcl_Interp *interp, Tk_Window tkwin, Drawable drawable, int x, int y, int width, int height,
                  int destWidth, int destHeight, const char *photoName, double inputGamma);
Region2D *Rbc_SetRegion(int x, int y, int width, int height, Region2D *regionPtr);
/* Missing routines from the Tk photo C API */
/**
 * @brief Inspect whether a retained image instance has lost its image manager.
 *
 * Require a non-NULL, unfreed Tk_Image. Return true for a NULL master pointer
 * or NULL master type. This RBC-defined helper reads copied private Tk layout;
 * it cannot validate an arbitrary or already-freed pointer.
 *
 * @param[in] tkImage Borrowed retained image instance; master validity requirements are described above.
 */
int Rbc_ImageIsDeleted(Tk_Image tkImage);
/**
 * @brief Return the borrowed master pointer from a retained, non-NULL Tk image.
 * No reference is acquired. The result may be NULL; callers must establish
 * validity before passing it to further Tk or RBC operations. This helper is
 * implemented by RBC using copied private Tk structure layout.
 *
 * @param[in] tkImage Borrowed retained image instance; master validity requirements are described above.
 */
Tk_ImageMaster Rbc_ImageGetMaster(Tk_Image tkImage);
/**
 * @brief Return a borrowed image-type pointer using RBC's private-layout adapter.
 * Require a retained non-NULL instance with a non-NULL master. A deleted
 * master's type may be NULL. Do not free or modify the returned descriptor.
 *
 * @param[in] tkImage Borrowed retained image instance; master validity requirements are described above.
 */
Tk_ImageType *Rbc_ImageGetType(Tk_Image tkImage);
/**
 * @brief Borrow the native display pixmap of a live photo instance, or return None
 * for a different live image type. Require non-NULL instance, master and type.
 * This is RBC's private-layout accessor, not a retained pixel snapshot. Do not
 * free the pixmap or retain it across image changes.
 *
 * @param[in] photoImage Borrowed live instance with a valid master and image type.
 */
Pixmap Rbc_ImageGetPhotoPixmap(Tk_Image photoImage);
/**
 * @brief Borrow the display GC of a live photo instance, or return NULL for another
 * live type. Require non-NULL instance, master and type. Do not free or mutate
 * the returned Tk-owned GC. This RBC helper relies on private photo layout.
 *
 * @param[in] photoImage Borrowed live instance with a valid master and image type.
 */
GC Rbc_ImageGetPhotoGC(Tk_Image photoImage);
/**
 * @brief Return the borrowed Tk name of an image with a valid master.
 * No name copy or lifetime reference is acquired. Do not free the result;
 * copy it before an operation that can invalidate the image or master.
 *
 * @param[in] tkImage Borrowed retained image instance; master validity requirements are described above.
 */
const char *Rbc_NameOfImage(Tk_Image tkImage);
/**
 * @brief Create a named photo through Tcl and acquire an image instance for tkwin.
 *
 * Borrow interp, tkwin and clientData; the installed change callback is a no-op.
 * Return a caller-owned token paired with Rbc_DestroyTemporaryImage(), or NULL
 * on failure. The Tcl result object is retained while Tk_GetImage consumes its
 * name. If instance acquisition fails, the named image is deleted through Tk's
 * C API while preserving the acquisition error.
 * Evaluates the image command in the current context and does not preserve
 * caller lifetime or interpreter result across callbacks.
 *
 * @param[in,out] interp Interpreter in which the image command is evaluated; receives the result or error.
 * @param[in] tkwin Borrowed live window providing the native display and drawable context.
 * @param[in] clientData Borrowed opaque data supplied to the no-op image-change callback.
 */
Tk_Image Rbc_CreateTemporaryImage(Tcl_Interp *interp, Tk_Window tkwin, ClientData clientData);
/**
 * @brief Delete a temporary image through Tk's C API and consume its instance.
 *
 * NULL is a successful no-op. Otherwise require a retained token. Already
 * deleted images still have their instance released. Always returns TCL_OK;
 * cleanup preserves interpreter state and does not evaluate the image command.
 * Tk deletion may notify other image clients. Callers must detach the token
 * from owner fields before calling and provide their own owner lifetime guard.
 *
 * @param[in,out] interp Interpreter owning the temporary image; result state is preserved.
 * @param[in] tkImage Owned temporary image instance consumed by this call, or NULL.
 */
int Rbc_DestroyTemporaryImage(Tcl_Interp *interp, Tk_Image tkImage);
GC Rbc_GetBitmapGC(Tk_Window tkwin);
Pixmap Rbc_PhotoImageMask(Tk_Window tkwin, Tk_PhotoImageBlock src);
/**
 * @brief Create an owned one-bit pixmap containing the rotated source bitmap.
 *
 * Borrow the Tk window and source bitmap, whose positive dimensions must match
 * the supplied sizes. Require a finite angle in degrees and non-NULL dimension
 * output pointers. On success assign output sizes and return a new pixmap for
 * Tk_FreePixmap(). None reports a detected failure; do not rely on output
 * dimensions after failure, since Windows and X11 initialize them differently.
 * Rotation uses discrete sampling, not the color-image shear filter.
 *
 * @param[in] tkwin Borrowed live window providing the native display and drawable context.
 * @param[in] bitmap Borrowed one-bit source pixmap matching width and height.
 * @param[in] width Positive source width in pixels.
 * @param[in] height Positive source height in pixels.
 * @param[in] theta Finite rotation angle in degrees.
 * @param[out] widthPtr Required output for the new width; unspecified on failure.
 * @param[out] heightPtr Required output for the new height; unspecified on failure.
 */
Pixmap Rbc_RotateBitmap(Tk_Window tkwin, Pixmap bitmap, int width, int height, double theta, int *widthPtr,
                        int *heightPtr);
/**
 * @brief Create an owned one-bit pixmap scaled to the requested dimensions.
 *
 * Borrow the window and source bitmap. Require positive source and destination
 * sizes and a source bitmap matching its declared dimensions. Release a returned
 * pixmap with Tk_FreePixmap(). X11 copies nearest-neighbour samples; Windows
 * uses StretchBlt and does not propagate its status. A non-None return is not
 * a portable guarantee that every native drawing operation succeeded.
 *
 * @param[in] tkwin Borrowed live window providing the native display and drawable context.
 * @param[in] srcBitmap Borrowed one-bit source pixmap matching its declared dimensions.
 * @param[in] srcWidth Positive source horizontal dimension in pixels.
 * @param[in] srcHeight Positive source vertical dimension in pixels.
 * @param[in] scaledWidth Positive destination horizontal dimension in pixels.
 * @param[in] scaledHeight Positive destination vertical dimension in pixels.
 */
Pixmap Rbc_ScaleBitmap(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight, int scaledWidth,
                       int scaledHeight);
/**
 * @brief Create only a region of a virtually scaled, rotated one-bit bitmap.
 *
 * Borrow window and source bitmap. The region coordinates refer to the virtual
 * rotated destination, whose dimensions are virtWidth and virtHeight. Require
 * positive dimensions, a finite degree angle and a region contained in that
 * virtual image, with representable coordinate sums. Right-angle paths assume
 * these bounds rather than checking each source sample. Return an owned pixmap
 * of regionWidth by regionHeight, or None for a detected failure. Free with
 * Tk_FreePixmap(); no source or window ownership is transferred.
 *
 * @param[in] tkwin Borrowed live window providing the native display and drawable context.
 * @param[in] srcBitmap Borrowed one-bit source pixmap matching its declared dimensions.
 * @param[in] srcWidth Positive source width in pixels.
 * @param[in] srcHeight Positive source height in pixels.
 * @param[in] regionX Left coordinate of the requested region in the virtual rotated image.
 * @param[in] regionY Top coordinate of the requested region in the virtual rotated image.
 * @param[in] regionWidth Positive output width; region must fit within the virtual image.
 * @param[in] regionHeight Positive output height; region must fit within the virtual image.
 * @param[in] virtWidth Positive width of the complete virtual rotated destination.
 * @param[in] virtHeight Positive height of the complete virtual rotated destination.
 * @param[in] theta Finite rotation angle in degrees.
 */
Pixmap Rbc_ScaleRotateBitmapRegion(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight, int regionX,
                                   int regionY, int regionWidth, int regionHeight, int virtWidth, int virtHeight,
                                   double theta);
#endif /* _RBCIMAGE */
