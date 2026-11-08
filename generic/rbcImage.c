/**
 * @file rbcImage.c
 * @brief CPU image processing and Tk photo conversion.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup image_resample_internals Image resampling kernels and contributions
 */
/** @defgroup image_quantize_internals RGB quantization internals
 */
/** @defgroup image_rotation_internals Color-image rotation internals
 */
/** @defgroup image_storage_internals Image storage and coordinate helpers
 */

#include "rbcInt.h"
#include "rbcImage.h"
#include <X11/Xutil.h>
#ifndef WIN32
#include <X11/Xproto.h>
#endif
#include <stdint.h>
#include <stddef.h>

#define CLAMP(c) ((((c) < 0.0) ? 0.0 : ((c) > 255.0) ? 255.0 : (c)))

#define NC 256
/** @ingroup selector_image_color
 * @brief Channel indices for the red, green and blue components of image operations. */
enum ColorIndices { RED, GREEN, BLUE };

#define R0 (cubePtr->r0)
#define R1 (cubePtr->r1)
#define G0 (cubePtr->g0)
#define G1 (cubePtr->g1)
#define B0 (cubePtr->b0)
#define B1 (cubePtr->b1)

/**
 * @brief Lower-exclusive, upper-inclusive box in the five-bit RGB grid.
 * @ingroup image_quantize_internals
 * Bounds lie in 0..32. vol is geometric cell count, not pixel population;
 * Cut updates it for the two resulting boxes. The owner allocates the array.
 */
typedef struct {
    int r0; /**< min, max values: min exclusive max inclusive */
    int r1; /**< min, max values: min exclusive max inclusive */
    int g0; /**< Lower green boundary, excluded from the box sum. */
    int g1; /**< Upper green boundary, included in the box sum. */
    int b0; /**< Lower blue boundary, excluded from the box sum. */
    int b1; /**< Upper blue boundary, included in the box sum. */
    int vol; /**< Box volume in histogram cells. */
} Cube;

/*
 *----------------------------------------------------------------------
 *
 * Histogram is in elements 1..HISTSIZE along each axis,
 * element 0 is for base or marginal value
 * NB: these must start out 0!
 *----------------------------------------------------------------------
 */

/** Signed cumulative count or color sum; arithmetic must remain representable. */
typedef int64_t Moment;
/**
 * @brief Owned raw RGB histograms, transformed in place into cumulative moments.
 * @ingroup image_quantize_internals
 * Arrays use bins 1..32 with zero boundary planes. gm2 stores squared-color
 * sums, not a precomputed variance. Alpha is absent. GetColorImageStatistics
 * allocates the record; M3d changes its representation; the owner frees it.
 */
typedef struct {
    Moment wt[33][33][33]; /**< # pixels in voxel */
    Moment mR[33][33][33]; /**< Sum over voxel of red pixel values */
    Moment mG[33][33][33]; /**< Sum over voxel of green pixel values */
    Moment mB[33][33][33]; /**< Sum over voxel of blue pixel values */
    Moment gm2[33][33][33]; /**< Sum of squared RGB channel values */
} ColorImageStatistics;

static void ZoomImageVertically(Rbc_ColorImage src, Rbc_ColorImage dest, ResampleFilter *filterPtr);
static void ZoomImageHorizontally(Rbc_ColorImage src, Rbc_ColorImage dest, ResampleFilter *filterPtr);
static void ShearY(Rbc_ColorImage src, Rbc_ColorImage dest, int y, int offset, double frac, Pix32 bgColor);
static void ShearX(Rbc_ColorImage src, Rbc_ColorImage dest, int x, int offset, double frac, Pix32 bgColor);
static Rbc_ColorImage Rotate45(Rbc_ColorImage src, double theta, Pix32 bgColor);
static Rbc_ColorImage CopyColorImage(Rbc_ColorImage src);
static Rbc_ColorImage Rotate90(Rbc_ColorImage src);
static Rbc_ColorImage Rotate180(Rbc_ColorImage src);
static Rbc_ColorImage Rotate270(Rbc_ColorImage src);
static ColorImageStatistics *GetColorImageStatistics(Rbc_ColorImage image);
static void M3d(ColorImageStatistics *s);
static INLINE Moment Volume(Cube *cubePtr, Moment m[33][33][33]);
static Moment Bottom(Cube *cubePtr, unsigned char dir, Moment m[33][33][33]);
static Moment Top(Cube *cubePtr, unsigned char dir, int pos, Moment m[33][33][33]);
static double Variance(Cube *cubePtr, ColorImageStatistics *s);
static double Maximize(Cube *cubePtr, unsigned char dir, int first, int last, int *cut, Moment rWhole, Moment gWhole,
                       Moment bWhole, Moment wWhole, ColorImageStatistics *s);
static int Cut(Cube *set1, Cube *set2, ColorImageStatistics *s);
static int SplitColorSpace(ColorImageStatistics *s, Cube *cubes, int nColors);
static void Mark(Cube *cubePtr, int label, unsigned int tag[33][33][33]);
static unsigned int *CreateColorLookupTable(ColorImageStatistics *s, Cube *cubes, int nColors);
static void MapColors(Rbc_ColorImage src, Rbc_ColorImage dest, unsigned int lut[33][33][33]);

ResampleFilter *rbcBoxFilterPtr; /**
 * @brief Validate dimensions and calculate their size_t pixel product.
 * @ingroup image_storage_internals
 *
 * Requires nonnegative dimensions; zero is valid. Panics for negative input
 * or multiplication overflow. Returns a pixel count, not a byte count.
 * Does not allocate storage; the allocator separately checks element size.
 *
 * @param[in] width Nonnegative horizontal pixel count; source or destination role is specified above.
 * @param[in] height Nonnegative vertical pixel count; source or destination role is specified above.
 */
static size_t GetColorImagePixelCount(int width, int height) {
    if ((width < 0) || (height < 0)) {
        Tcl_Panic("GetColorImagePixelCount: negative image dimension");
    }
    if ((width != 0) && ((size_t)height > SIZE_MAX / (size_t)width)) {
        Tcl_Panic("GetColorImagePixelCount: image size overflow");
    }
    return (size_t)width * (size_t)height;
}

/**
 * @brief Calculate a photo-block byte displacement with wide arithmetic.
 * @ingroup image_storage_internals
 *
 * Borrows a valid Tk block. Computes y*pitch + x*pixelSize in Tcl_WideInt
 * and checks the ptrdiff_t range before conversion. Does not validate x/y
 * against image bounds, channel offsets, or accessible storage; callers
 * establish those conditions before adding the result to pixelPtr.
 *
 * @param[in] blockPtr Borrowed valid photo block; pitch and pixelSize determine byte displacement.
 * @param[in] x In-bounds column index; not validated here.
 * @param[in] y In-bounds row index; not validated here.
 */
static ptrdiff_t GetPhotoByteOffset(const Tk_PhotoImageBlock *blockPtr, int x, int y) {
    Tcl_WideInt offset;

    offset = ((Tcl_WideInt)y * (Tcl_WideInt)blockPtr->pitch) + ((Tcl_WideInt)x * (Tcl_WideInt)blockPtr->pixelSize);
    if ((offset < (Tcl_WideInt)PTRDIFF_MIN) || (offset > (Tcl_WideInt)PTRDIFF_MAX)) {
        Tcl_Panic("GetPhotoByteOffset: photo offset overflow");
    }
    return (ptrdiff_t)offset;
}

/**
 * @brief Clamp a source origin and limit its spans to the remaining image.
 * @ingroup image_storage_internals
 *
 * Reads and rewrites all four coordinate outputs. Negative image dimensions
 * panic. Origins clamp to inclusive bounds [0,imageSize]; negative or
 * excessive spans become the remaining distance to the edge. A negative
 * origin does not subtract from a positive span: this is not geometric
 * rectangle intersection. Returns true only when both resulting spans are
 * positive. Performs no allocation or image access.
 *
 * @param[in] imageWidth Nonnegative source width in pixels.
 * @param[in] imageHeight Nonnegative source height in pixels.
 * @param[in,out] xPtr Required input/output for horizontal origin; rewritten to the effective region.
 * @param[in,out] yPtr Required input/output for vertical origin; rewritten to the effective region.
 * @param[in,out] widthPtr Required input/output for horizontal span; rewritten to the effective region.
 * @param[in,out] heightPtr Required input/output for vertical span; rewritten to the effective region.
 */
static int ClipImageRegion(int imageWidth, int imageHeight, int *xPtr, int *yPtr, int *widthPtr, int *heightPtr) {
    int x, y;
    int width, height;

    if ((imageWidth < 0) || (imageHeight < 0)) {
        Tcl_Panic("ClipImageRegion: negative image dimension");
    }
    x = *xPtr;
    y = *yPtr;
    width = *widthPtr;
    height = *heightPtr;
    if (x < 0) {
        x = 0;
    } else if (x > imageWidth) {
        x = imageWidth;
    }
    if (y < 0) {
        y = 0;
    } else if (y > imageHeight) {
        y = imageHeight;
    }
    if ((width < 0) || (width > imageWidth - x)) {
        width = imageWidth - x;
    }
    if ((height < 0) || (height > imageHeight - y)) {
        height = imageHeight - y;
    }
    *xPtr = x;
    *yPtr = y;
    *widthPtr = width;
    *heightPtr = height;
    return ((width > 0) && (height > 0));
}

/**
 * @brief Map an in-range destination position into a source interval.
 * @ingroup image_storage_internals
 *
 * Requires positive span/destSpan and position in [0,destSpan). Computes
 * position*span/destSpan in double, truncates, caps at span-1 and adds
 * origin. Caller ensures that sum lies within the source and int range.
 * Has no half-pixel offset, interpolation or allocation.
 *
 * @param[in] origin Source interval origin; origin plus span must be representable.
 * @param[in] span Positive source interval length.
 * @param[in] position Destination index in [0,destSpan).
 * @param[in] destSpan Positive destination interval length.
 */
static int MapNearestCoordinate(int origin, int span, int position, int destSpan) {
    int offset;
    double value;

    assert(span > 0);
    assert(destSpan > 0);
    assert(position >= 0);
    assert(position < destSpan);
    value = ((double)position * (double)span) / (double)destSpan;
    offset = (int)value;
    if (offset >= span) {
        offset = span - 1;
    }
    return origin + offset;
}

/**
 * @brief Map a virtual destination position to a clamped source index.
 * @ingroup image_storage_internals
 *
 * Requires positive source and virtual sizes and a finite virtualPosition.
 * Scales in double, clamps at either source edge, and truncates interior
 * values. Allows positions outside the virtual image; those repeat edge
 * pixels rather than producing transparent padding. Does not allocate.
 *
 * @param[in] srcSize Positive source dimension.
 * @param[in] virtualPosition Finite coordinate in the virtual destination; outlying values repeat edge samples.
 * @param[in] virtualSize Positive virtual destination dimension.
 */
static int MapVirtualCoordinate(int srcSize, double virtualPosition, int virtualSize) {
    double value;

    assert(srcSize > 0);
    assert(virtualSize > 0);
    value = virtualPosition * (double)srcSize / (double)virtualSize;
    if (value <= 0.0) {
        return 0;
    }
    if (value >= (double)srcSize) {
        return srcSize - 1;
    }
    return (int)value;
}

/**
 * @brief Allocate an owned row-major RGBA pixel buffer and image record.
 * Dimensions must be nonnegative; invalid or overflowing sizes panic. Pixels
 * start zeroed, including alpha. Release with Rbc_FreeColorImage. The returned
 * image has no Tk image identity, reference count or change notification.
 *
 * @param[in] width Nonnegative horizontal pixel count; source or destination role is specified above.
 * @param[in] height Nonnegative vertical pixel count; source or destination role is specified above.
 */
Rbc_ColorImage Rbc_CreateColorImage(int width, int height) {
    struct ColorImage *imagePtr;
    size_t nPixels;

    nPixels = GetColorImagePixelCount(width, height);
    imagePtr = Rbc_Calloc(1, sizeof(*imagePtr));
    imagePtr->bits = Rbc_Calloc(nPixels, sizeof(*imagePtr->bits));
    imagePtr->width = width;
    imagePtr->height = height;
    return imagePtr;
}

/**
 * @defgroup image_storage_release Image storage release
 * @{
 */
/**
 * @ingroup image_storage_release
 * @brief Release both the pixel array and its owning image record.
 * Requires a valid non-NULL owned image. All borrowed pixel pointers become
 * invalid. This does not delete or modify any Tk photo used to create it.
 *
 * @param[in] imagePtr Owned non-NULL image consumed here, including its pixel allocation.
 */
void Rbc_FreeColorImage(struct ColorImage *imagePtr) {
    ckfree((char *)imagePtr->bits);
    ckfree((char *)imagePtr);
}
/** @} */

/**
 * @brief Apply a gamma lookup table to RGB bytes in place, preserving alpha.
 * @ingroup image_storage_internals
 *
 * Borrows a writable valid image. newGamma must be finite and positive;
 * invalid gamma panics. Builds a stack table with 255*pow(i/255,1/gamma),
 * clamping and truncating each entry to a byte. Replaces RGB independently
 * for every pixel without changing alpha, dimensions or ownership. No Tk
 * change notification is issued for this CPU-side mutation.
 *
 * @param[in,out] src Borrowed writable image modified in place.
 * @param[in] newGamma Finite positive gamma used as the reciprocal exponent.
 */
void Rbc_GammaCorrectColorImage(Rbc_ColorImage src, double newGamma) {
    size_t nPixels;
    Pix32 *srcPtr, *endPtr;
    unsigned int i;
    double value;
    unsigned char lut[256];
    double invGamma;

    if ((!FINITE(newGamma)) || (newGamma <= 0.0)) {
        Tcl_Panic("Rbc_GammaCorrectColorImage: invalid gamma");
    }
    invGamma = 1.0 / newGamma;
    for (i = 0; i < 256; i++) {
        value = 255.0 * pow((double)i / 255.0, invGamma);
        lut[i] = (unsigned char)CLAMP(value);
    }
    nPixels = GetColorImagePixelCount(Rbc_ColorImageWidth(src), Rbc_ColorImageHeight(src));
    srcPtr = Rbc_ColorImageBits(src);
    for (endPtr = srcPtr + nPixels; srcPtr < endPtr; srcPtr++) {
        srcPtr->rgba.red = lut[srcPtr->rgba.red];
        srcPtr->rgba.green = lut[srcPtr->rgba.green];
        srcPtr->rgba.blue = lut[srcPtr->rgba.blue];
    }
}

/**
 * @brief Replace RGB channels in place with their weighted greyscale value.
 *
 * Borrow image and retain its dimensions, storage and alpha values. Apply
 * coefficients 0.212671, 0.715160 and 0.072169 to stored RGB bytes, clamp and
 * truncate to a byte. No gamma conversion or temporary image is involved.
 *
 * @param[in,out] image Borrowed writable image; RGB is replaced and alpha retained.
 */
void Rbc_ColorImageToGreyscale(Rbc_ColorImage image) {
    Pix32 *srcPtr;
    Pix32 *endPtr;
    double y;
    size_t nPixels;

    nPixels = GetColorImagePixelCount(Rbc_ColorImageWidth(image), Rbc_ColorImageHeight(image));
    srcPtr = Rbc_ColorImageBits(image);
    endPtr = srcPtr + nPixels;
    for (; srcPtr < endPtr; srcPtr++) {
        y = (0.212671 * (double)srcPtr->rgba.red) + (0.715160 * (double)srcPtr->rgba.green) + (0.072169 * (double)srcPtr->rgba.blue);
        srcPtr->rgba.red = srcPtr->rgba.green = srcPtr->rgba.blue = (unsigned char)CLAMP(y);
    }
}

/**
 * @brief Replace a borrowed Tk photo's size and pixels from an RBC image.
 * Source ownership stays with the caller; Tk copies the block synchronously.
 * Uses TK_PHOTO_COMPOSITE_SET, including alpha. This legacy void helper ignores
 * return codes from Tk_PhotoSetSize and Tk_PhotoPutBlock; it does not provide a
 * reliable success/failure status to its caller.
 *
 * @param[in,out] interp Interpreter receiving Tcl diagnostics; no ownership is transferred.
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in] photo Borrowed live Tk photo handle.
 */
void Rbc_ColorImageToPhoto(Tcl_Interp *interp, Rbc_ColorImage src, Tk_PhotoHandle photo) {
    Tk_PhotoImageBlock dest;
    int width, height;

    width = Rbc_ColorImageWidth(src);
    height = Rbc_ColorImageHeight(src);

    Tk_PhotoGetImage(photo, &dest);
    if ((size_t)width > (size_t)INT_MAX / sizeof(Pix32)) {
        Tcl_Panic("Rbc_ColorImageToPhoto: photo pitch overflow");
    }
    dest.pixelSize = (int)sizeof(Pix32);
    dest.pitch = (int)((size_t)width * sizeof(Pix32));
    dest.width = width;
    dest.height = height;
    dest.offset[0] = offsetof(Pix32, rgba.red);
    dest.offset[1] = offsetof(Pix32, rgba.green);
    dest.offset[2] = offsetof(Pix32, rgba.blue);
    dest.offset[3] = offsetof(Pix32, rgba.alpha);
    dest.pixelPtr = (unsigned char *)Rbc_ColorImageBits(src);
    Tk_PhotoSetSize(interp, photo, width, height);
    Tk_PhotoPutBlock(interp, photo, &dest, 0, 0, width, height, TK_PHOTO_COMPOSITE_SET);
}

/**
 * @brief Copy a clamped photo origin and limited spans into an owned image.
 * Requires a valid photo handle. An empty effective region returns an empty image,
 * not NULL. Negative origins clamp without reducing positive spans.
 * Non-RGB/RGBA layouts replicate offset[0] into RGB and supply opaque alpha,
 * matching whole-photo conversion.
 * The result owns its pixels and must be freed with Rbc_FreeColorImage.
 *
 * @param[in] photo Borrowed live Tk photo handle.
 * @param[in] x Horizontal source origin in pixels; clamped as described above.
 * @param[in] y Vertical source origin in pixels; clamped as described above.
 * @param[in] width Requested source horizontal span; negative or excessive spans extend to the edge.
 * @param[in] height Requested source vertical span; negative or excessive spans extend to the edge.
 */
Rbc_ColorImage Rbc_PhotoRegionToColorImage(Tk_PhotoHandle photo, int x, int y, int width, int height) {
    Tk_PhotoImageBlock src;
    Rbc_ColorImage image;
    Pix32 *destPtr;
    unsigned char *srcRowPtr;
    unsigned char *srcData;
    int offR, offG, offB, offA;
    int row, column;

    Tk_PhotoGetImage(photo, &src);
    (void)ClipImageRegion(src.width, src.height, &x, &y, &width, &height);
    image = Rbc_CreateColorImage(width, height);
    if ((width == 0) || (height == 0)) {
        return image;
    }
    destPtr = Rbc_ColorImageBits(image);
    offR = src.offset[0];
    offG = src.offset[1];
    offB = src.offset[2];
    offA = src.offset[3];
    srcRowPtr = src.pixelPtr + GetPhotoByteOffset(&src, x, y);
    if (src.pixelSize == 4) {
        for (row = 0; row < height; row++) {
            srcData = srcRowPtr;
            for (column = 0; column < width; column++) {
                destPtr->rgba.red = srcData[offR];
                destPtr->rgba.green = srcData[offG];
                destPtr->rgba.blue = srcData[offB];
                destPtr->rgba.alpha = srcData[offA];
                srcData += src.pixelSize;
                destPtr++;
            }
            srcRowPtr += src.pitch;
        }
    } else if (src.pixelSize == 3) {
        for (row = 0; row < height; row++) {
            srcData = srcRowPtr;
            for (column = 0; column < width; column++) {
                destPtr->rgba.red = srcData[offR];
                destPtr->rgba.green = srcData[offG];
                destPtr->rgba.blue = srcData[offB];
                destPtr->rgba.alpha = (unsigned char)-1;
                srcData += src.pixelSize;
                destPtr++;
            }
            srcRowPtr += src.pitch;
        }
    } else {
        for (row = 0; row < height; row++) {
            srcData = srcRowPtr;
            for (column = 0; column < width; column++) {
                destPtr->rgba.red = destPtr->rgba.green = destPtr->rgba.blue = srcData[offR];
                destPtr->rgba.alpha = (unsigned char)-1;
                srcData += src.pixelSize;
                destPtr++;
            }
            srcRowPtr += src.pitch;
        }
    }
    return image;
}

/**
 * @brief Copy a valid Tk photo into a newly owned RBC color image.
 * Copies pixels using the photo block's pitch and channel offsets. The result
 * is independent of later photo changes; release it with Rbc_FreeColorImage.
 *
 * @param[in] photo Borrowed live Tk photo handle.
 */
Rbc_ColorImage Rbc_PhotoToColorImage(Tk_PhotoHandle photo) {
    Rbc_ColorImage image;
    Tk_PhotoImageBlock src;
    int width, height;
    Pix32 *destPtr;
    unsigned char *srcRowPtr;
    unsigned char *srcData;
    int x, y;

    Tk_PhotoGetImage(photo, &src);
    width = src.width;
    height = src.height;
    image = Rbc_CreateColorImage(width, height);
    destPtr = Rbc_ColorImageBits(image);
    srcRowPtr = src.pixelPtr;
    if (src.pixelSize == 4) {
        for (y = 0; y < height; y++) {
            srcData = srcRowPtr;
            for (x = 0; x < width; x++) {
                destPtr->rgba.red = srcData[src.offset[0]];
                destPtr->rgba.green = srcData[src.offset[1]];
                destPtr->rgba.blue = srcData[src.offset[2]];
                destPtr->rgba.alpha = srcData[src.offset[3]];
                srcData += src.pixelSize;
                destPtr++;
            }
            srcRowPtr += src.pitch;
        }
    } else if (src.pixelSize == 3) {
        for (y = 0; y < height; y++) {
            srcData = srcRowPtr;
            for (x = 0; x < width; x++) {
                destPtr->rgba.red = srcData[src.offset[0]];
                destPtr->rgba.green = srcData[src.offset[1]];
                destPtr->rgba.blue = srcData[src.offset[2]];
                /* No transparency information */
                destPtr->rgba.alpha = (unsigned char)-1;
                srcData += src.pixelSize;
                destPtr++;
            }
            srcRowPtr += src.pitch;
        }
    } else {
        for (y = 0; y < height; y++) {
            srcData = srcRowPtr;
            for (x = 0; x < width; x++) {
                destPtr->rgba.red = destPtr->rgba.green = destPtr->rgba.blue = srcData[src.offset[0]];
                /* No transparency information */
                destPtr->rgba.alpha = (unsigned char)-1;
                srcData += src.pixelSize;
                destPtr++;
            }
            srcRowPtr += src.pitch;
        }
    }
    return image;
}

/*
 *    filter function definitions
 */

static ResampleFilterProc DefaultFilter;
static ResampleFilterProc BellFilter;
static ResampleFilterProc BesselFilter;
static ResampleFilterProc BoxFilter;
static ResampleFilterProc BSplineFilter;
static ResampleFilterProc CatRomFilter;
static ResampleFilterProc DummyFilter;
static ResampleFilterProc GaussianFilter;
static ResampleFilterProc GiFilter;
static ResampleFilterProc Lanczos3Filter;
static ResampleFilterProc MitchellFilter;
static ResampleFilterProc SincFilter;
static ResampleFilterProc TriangleFilter;
static Tk_ImageChangedProc TempImageChangedProc;

/**
 * @brief Evaluate the compact cubic default kernel.
 * @ingroup image_resample_internals
 *
 * For t = abs(x) below one returns (2t - 3)t squared + 1, otherwise zero.
 * Uses no mutable state or allocation. The table support is one pixel.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double DefaultFilter(double x) {
    if (x < 0.0) {
        x = -x;
    }
    if (x < 1.0) {
        /* f(x) = 2x^3 - 3x^2 + 1, -1 <= x <= 1 */
        return (2.0 * x - 3.0) * x * x + 1.0;
    }
    return 0.0;
}

/**
 * @brief Evaluate the historical diagnostic kernel abs(x).
 * @ingroup image_resample_internals
 *
 * Has no intrinsic cutoff, normalization or image access. Its table support
 * is 0.5; contribution construction supplies the sampling interval. This
 * test kernel is not an identity or nearest-neighbour operation.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double DummyFilter(double x) { return FABS(x); }

/*
 *
 * Finite filters in increasing order:
 *
 *      Box (constant)
 *      Triangle (linear)
 *      Bell
 *      BSpline (cubic)
 *
 */

/**
 * @brief Return unit weight inside the closed interval [-0.5, 0.5].
 * @ingroup image_resample_internals
 *
 * Returns zero outside the interval, including neither endpoint in that
 * rejection. Stateless; the table support is 0.5 pixels.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double BoxFilter(double x) {
    if ((x < -0.5) || (x > 0.5)) {
        return 0.0;
    }
    return 1.0;
}

/**
 * @brief Evaluate the linear tent kernel.
 * @ingroup image_resample_internals
 *
 * Returns 1 - abs(x) for abs(x) below one, otherwise zero. Stateless with
 * table support one pixel; performs no contribution normalization itself.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double TriangleFilter(double x) {
    if (x < 0.0) {
        x = -x;
    }
    if (x < 1.0) {
        return (1.0 - x);
    }
    return 0.0;
}

/**
 * @brief Evaluate the piecewise quadratic bell kernel.
 * @ingroup image_resample_internals
 *
 * For t = abs(x), returns 0.75 - t squared below 0.5 and half of
 * (t - 1.5) squared below 1.5; otherwise zero. Stateless; support is 1.5.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double BellFilter(double x) {
    if (x < 0.0) {
        x = -x;
    }
    if (x < 0.5) {
        return (0.75 - (x * x));
    }
    if (x < 1.5) {
        x = (x - 1.5);
        return (0.5 * (x * x));
    }
    return 0.0;
}

/**
 * @brief Evaluate the compact cubic B-spline kernel.
 * @ingroup image_resample_internals
 *
 * Uses even cubic pieces on abs(x) below one and below two, and returns
 * zero outside. Stateless; table support is two pixels. The kernel value
 * at zero is 2/3, so this is not an interpolating cardinal kernel.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double BSplineFilter(double x) {
    double x2;

    if (x < 0.0) {
        x = -x;
    }
    if (x < 1) {
        x2 = x * x;
        return ((.5 * x2 * x) - x2 + (2.0 / 3.0));
    } else if (x < 2) {
        x = 2 - x;
        return ((x * x * x) / 6.0);
    }
    return 0.0;
}

/*
 *
 * Infinite Filters:
 *      Sinc        perfect lowpass filter
 *      Bessel        circularly symmetric 2-D filter
 *      Gaussian
 *      Lanczos3
 *      Mitchell
 */

/**
 * @brief Evaluate sin(pi*x)/(pi*x), with its removable singularity filled.
 * @ingroup image_resample_internals
 *
 * Returns one at zero. No intrinsic cutoff; the table limits sampling to
 * support four. Stateless; signed lobes are preserved for normalization.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double SincFilter(double x) {
    x *= M_PI;
    if (x == 0.0) {
        return 1.0;
    }
    return (sin(x) / x);
}

/**
 * @brief Evaluate the legacy Bessel reconstruction kernel.
 * @ingroup image_resample_internals
 *
 * Returns pi/4 at zero and j1(pi*x)/(2*x) otherwise. The table support is
 * 3.2383; there is no cutoff in this function. The legacy Borland branch
 * returns zero for all inputs. Requires the platform j1 implementation
 * on other builds. Owns no state and performs no normalization.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double BesselFilter(double x) {
#ifdef NEED_DECL_J1
    double j1(double value);
#endif
    /*
     * See Pratt "Digital Image Processing" p. 97 for Bessel functions
     * zeros are at approx x=1.2197, 2.2331, 3.2383, 4.2411, 5.2428, 6.2439,
     * 7.2448, 8.2454
     */
#ifdef __BORLANDC__
    return 0.0;
#else
    return (x == 0.0) ? M_PI / 4.0 : j1(M_PI * x) / (x + x);
#endif
}

#define SQRT_2PI 0.79788456080286541 /* sqrt(2.0 / M_PI) */

/**
 * @brief Evaluate exp(-2*x*x) times sqrt(2/pi).
 * @ingroup image_resample_internals
 *
 * Stateless with no intrinsic cutoff. The gaussian and gauss8 table entries
 * share this procedure but use support radii 1.25 and 8 respectively.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double GaussianFilter(double x) { return exp(-2.0 * x * x) * SQRT_2PI; }

/**
 * @brief Evaluate a sinc kernel with a three-lobe sinc window.
 * @ingroup image_resample_internals
 *
 * For abs(x) below three returns SincFilter(x) times SincFilter(x/3),
 * otherwise zero. Stateless; the table support is three pixels.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double Lanczos3Filter(double x) {
    if (x < 0) {
        x = -x;
    }
    if (x < 3.0) {
        return (SincFilter(x) * SincFilter(x / 3.0));
    }
    return 0.0;
}

#define B 0.3333333333333333 /* (1.0 / 3.0) */
#define C 0.3333333333333333 /* (1.0 / 3.0) */

/**
 * @brief Evaluate the compact Mitchell cubic with B = C = 1/3.
 * @ingroup image_resample_internals
 *
 * Uses even polynomial pieces below absolute distances one and two, then
 * returns zero. Stateless; support is two. Negative lobes are retained
 * here and may produce values later clamped by the image passes.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double MitchellFilter(double x) {
    double x2;

    x2 = x * x;
    if (x < 0) {
        x = -x;
    }
    if (x < 1.0) {
        x = (((12.0 - 9.0 * B - 6.0 * C) * (x * x2)) + ((-18.0 + 12.0 * B + 6.0 * C) * x2) + (6.0 - 2 * B));
        return (x / 6.0);
    } else if (x < 2.0) {
        x = (((-1.0 * B - 6.0 * C) * (x * x2)) + ((6.0 * B + 30.0 * C) * x2) + ((-12.0 * B - 48.0 * C) * x) +
             (8.0 * B + 24 * C));
        return (x / 6.0);
    }
    return 0.0;
}

/**
 * @brief Evaluate the piecewise Catmull-Rom cubic kernel.
 * @ingroup image_resample_internals
 *
 * Uses four polynomial intervals spanning [-2, 2] and is zero outside.
 * Returns one at zero. Stateless; signed lobes are not clipped here and
 * the table support is two pixels.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double CatRomFilter(double x) {
    if (x < -2.) {
        return 0.0;
    }
    if (x < -1.0) {
        return 0.5 * (4.0 + x * (8.0 + x * (5.0 + x)));
    }
    if (x < 0.0) {
        return 0.5 * (2.0 + x * x * (-5.0 + x * -3.0));
    }
    if (x < 1.0) {
        return 0.5 * (2.0 + x * x * (-5.0 + x * 3.0));
    }
    if (x < 2.0) {
        return 0.5 * (4.0 + x * (-8.0 + x * (5.0 - x)));
    }
    return 0.0;
}

/**
 * @brief Evaluate the historical Gaussian-tail integral approximation.
 * @ingroup image_resample_internals
 *
 * Returns one below -1.5 and zero above 1.5, with three polynomial pieces
 * between. This is an asymmetric cumulative function, not the even
 * Gaussian kernel. The table support is 1.25. Owns no mutable state.
 *
 * @param[in] x Signed distance from the reconstruction center in sample units.
 */
static double GiFilter(double x) {
    if (x > 1.5) {
        return 0.0;
    } else if (x < -1.5) {
        return 1.0;
    } else {
#define I6 0.166666666666667
#define I4 0.25
#define I3 0.333333333333333
        double x2 = x * x;
        double x3 = x2 * x;
        if (x > 0.5) {
            return .5625 - (x3 * I6 - 3 * x2 * I4 + 1.125 * x);
        } else if (x > -0.5) {
            return 0.5 - (0.75 * x - x3 * I3);
        } else {
            return 0.4375 + (-x3 * I6 - 3 * x2 * I4 - 1.125 * x);
        }
    }
}

static ResampleFilter filterTable[] = {
    /* name,     function,        support */
    {"bell", BellFilter, 1.5},
    {"bessel", BesselFilter, 3.2383},
    {"box", BoxFilter, 0.5},
    {"bspline", BSplineFilter, 2.0},
    {"catrom", CatRomFilter, 2.0},
    {"default", DefaultFilter, 1.0},
    {"dummy", DummyFilter, 0.5},
    {"gauss8", GaussianFilter, 8.0},
    {"gaussian", GaussianFilter, 1.25},
    {"gi", GiFilter, 1.25},
    {"lanczos3", Lanczos3Filter, 3.0},
    {"mitchell", MitchellFilter, 2.0},
    {"none", (ResampleFilterProc *)NULL, 0.0},
    {"sinc", SincFilter, 4.0},
    {"triangle", TriangleFilter, 1.0},
    {NULL, NULL, 0.0}};

ResampleFilter *rbcBoxFilterPtr = &(filterTable[2]);

/**
 * @brief Resolve a filter name or unambiguous abbreviation to borrowed static state.
 *
 * Return TCL_OK and a filter-table pointer, or NULL for the name none. On
 * lookup failure return TCL_ERROR without assigning the output pointer.
 * Do not free or modify a returned filter. NULL selects policy in callers;
 * it is not a valid filter argument to Rbc_ResampleColorImage().
 *
 * @param[in,out] interp Interpreter receiving Tcl diagnostics; no ownership is transferred.
 * @param[in] filterObj Borrowed Tcl object naming a filter or unambiguous abbreviation.
 * @param[out] filterPtrPtr Required output; receives borrowed static filter or NULL for none only on success.
 */
int Rbc_GetResampleFilterFromObj(Tcl_Interp *interp, Tcl_Obj *filterObj, ResampleFilter **filterPtrPtr) {
    int index;

    if (Tcl_GetIndexFromObjStruct(interp, filterObj, filterTable, sizeof(ResampleFilter), "filter", 0, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    *filterPtrPtr = filterTable[index].proc == NULL ? NULL : &(filterTable[index]);
    return TCL_OK;
}

/*
 * Scaled integers are fixed point values.  The upper 18 bits is the integer
 * portion, the lower 14 bits the fractional remainder.  Must be careful
 * not to overflow the values (especially during multiplication).
 *
 * The following operations are defined:
 *
 *    S * n        Scaled integer times an integer.
 *    S1 + S2        Scaled integer plus another scaled integer.
 *
 */

#define float2si(f) (int)((f) * 16384.0 + 0.5)
#define uchar2si(b) (((int)(b)) << 14)
#define si2int(s) (((s) + 8192) >> 14)

/** @brief One image-resampling coefficient, in floating or fixed-point form.
 * The resampling phase selects the representation; the two members share storage.
 */
typedef union {
    int i; /**< Fixed-point coefficient scaled by 16384. */
    float f; /**< Floating coefficient used while constructing/normalizing weights. */
} ResampleWeight;

/** @brief Contribution record for one destination image sample.
 * Stored in an owned resampling workspace with extra trailing coefficient
 * storage; ComputeSampleSize determines the byte stride between records.
 */
typedef struct {
    int count; /**< Number of source samples contributing to this destination. */
    int start; /**< First contributing source sample index. */
    ResampleWeight weights[1]; /**< Inline first coefficient; allocated storage contains count entries. */
} Sample;

/**
 * @brief Compute storage for one variable-sized contribution record.
 * @ingroup image_resample_internals
 *
 * Requires a positive weight count. Includes the first ResampleWeight already in
 * Sample and checks extra storage against SIZE_MAX before multiplying.
 * Returns a byte stride; invalid count or representational overflow panics.
 * Does not allocate or initialize a record.
 *
 * @param[in] nWeights Positive capacity of one contribution record, including its inline first weight.
 */
static size_t GetSampleRecordSize(int nWeights) {
    size_t size;
    size_t extraWeights;

    if (nWeights <= 0) {
        Tcl_Panic("GetSampleRecordSize: invalid weight count");
    }
    size = sizeof(Sample);
    extraWeights = (size_t)nWeights - 1u;
    if (extraWeights > (SIZE_MAX - size) / sizeof(ResampleWeight)) {
        Tcl_Panic("GetSampleRecordSize: sample size overflow");
    }
    return size + (extraWeights * sizeof(ResampleWeight));
}

/**
 * @brief Bound contribution capacity by the source pixel count.
 * @ingroup image_resample_internals
 *
 * Requires positive srcWidth and finite nonnegative radius; invalid input
 * panics. Computes 2*radius + 2, caps at srcWidth before integer conversion
 * (also for an overflowed sum), and returns at least one. This capacity
 * bounds the clipped interval, not the count of nonzero weights.
 *
 * @param[in] srcWidth Positive number of source samples along the filtered axis.
 * @param[in] radius Finite nonnegative support radius in source-sample units.
 */
static int GetFilterSize(int srcWidth, double radius) {
    double value;

    if (srcWidth <= 0) {
        Tcl_Panic("GetFilterSize: invalid source width");
    }
    if ((!FINITE(radius)) || (radius < 0.0)) {
        Tcl_Panic("GetFilterSize: invalid filter radius");
    }
    value = (radius * 2.0) + 2.0;
    /*
     * A clipped filter can never contain more source samples than
     * there are source pixels.  Capping here also avoids converting
     * a value larger than INT_MAX to int.
     */
    if ((!FINITE(value)) || (value >= (double)srcWidth)) {
        return srcWidth;
    }
    if (value < 1.0) {
        return 1;
    }
    return (int)value;
}

/**
 * @brief Round and clip an inclusive source contribution interval.
 * @ingroup image_resample_internals
 *
 * Borrows output pointers. Requires positive srcWidth, finite center/radius
 * and nonnegative radius; invalid geometry panics. Rounds center +/- radius
 * using +0.5 then positive integer truncation, with clipping before casts.
 * Writes ordered bounds in [0, srcWidth-1]; outlying centers clamp to an
 * edge sample rather than yielding an empty interval.
 *
 * @param[in] srcWidth Positive number of source samples along the filtered axis.
 * @param[in] center Finite center coordinate of the source contribution interval.
 * @param[in] radius Finite nonnegative support radius in source-sample units.
 * @param[out] leftPtr Required output for inclusive first source sample.
 * @param[out] rightPtr Required output for inclusive last source sample.
 */
static void GetFilterBounds(int srcWidth, double center, double radius, int *leftPtr, int *rightPtr) {
    double value;
    int left;
    int right;

    if ((srcWidth <= 0) || (!FINITE(center)) || (!FINITE(radius)) || (radius < 0.0)) {
        Tcl_Panic("GetFilterBounds: invalid filter geometry");
    }
    value = center - radius + 0.5;
    if (value <= 0.0) {
        left = 0;
    } else if (value >= (double)srcWidth) {
        left = srcWidth - 1;
    } else {
        left = (int)value;
    }
    value = center + radius + 0.5;
    if (value <= 0.0) {
        right = 0;
    } else if (value >= (double)srcWidth) {
        right = srcWidth - 1;
    } else {
        right = (int)value;
    }
    assert(left >= 0);
    assert(right < srcWidth);
    assert(left <= right);
    *leftPtr = left;
    *rightPtr = right;
}

/**
 * @brief Allocate normalized fixed-point contributions for one output axis.
 * @ingroup image_resample_internals
 *
 * Requires positive dimensions and a non-NULL filter with a valid procedure
 * and finite nonnegative support. Invalid geometry panics. Returns the
 * byte stride between records and writes an owned zero-allocated array to
 * samplePtrPtr; caller frees it with ckfree. One record per destination
 * coordinate stores the clipped source start/count. Downsampling expands
 * support by inverse scale; weights use a half-pixel offset. Float weights
 * are summed in double, normalized unless the sum is zero, then overwritten
 * in the union by fixed-point ints using a scale of 16384 and +0.5 before
 * truncation. Custom kernels must yield finite, representable normalized
 * weights; there is no general validation of callback results.
 *
 * @param[in] srcWidth Positive number of source samples along the filtered axis.
 * @param[in] destWidth Positive number of destination samples.
 * @param[in] filterPtr Borrowed filter descriptor with a valid procedure and support.
 * @param[out] samplePtrPtr Required output receiving owned contribution storage; release with ckfree.
 */
static size_t ComputeWeights(int srcWidth, int destWidth, ResampleFilter *filterPtr, Sample **samplePtrPtr) {
    Sample *samples;
    Sample *s;
    ResampleWeight *weight;
    double scale;
    double center;
    double radius;
    double factor;
    double sum;
    double fscale;
    int filterSize;
    int x;
    int i;
    int left;
    int right;
    size_t size;

    if ((srcWidth <= 0) || (destWidth <= 0)) {
        Tcl_Panic("ComputeWeights: invalid image dimension");
    }
    if ((filterPtr == NULL) || (filterPtr->proc == NULL)) {
        Tcl_Panic("ComputeWeights: invalid resampling filter");
    }
    scale = (double)destWidth / (double)srcWidth;
    if ((!FINITE(scale)) || (scale <= 0.0)) {
        Tcl_Panic("ComputeWeights: invalid image scale");
    }
    if (scale < 1.0) {
        /*
         * Downsampling broadens the effective filter radius.
         */
        radius = filterPtr->support / scale;
        fscale = 1.0 / scale;
        filterSize = GetFilterSize(srcWidth, radius);
        size = GetSampleRecordSize(filterSize);
        samples = Rbc_Calloc((size_t)destWidth, size);
        s = samples;
        for (x = 0; x < destWidth; x++) {
            center = (double)x * fscale;
            GetFilterBounds(srcWidth, center, radius, &left, &right);
            s->start = left;
            s->count = right - left + 1;
            assert(s->count > 0);
            assert(s->count <= filterSize);
            sum = 0.0;
            for (weight = s->weights, i = left; i <= right; i++, weight++) {
                weight->f = (float)(*filterPtr->proc)(((double)i + 0.5 - center) * scale);
                sum += weight->f;
            }
            factor = (sum == 0.0) ? 1.0 : (1.0 / sum);
            for (weight = s->weights, i = left; i <= right; i++, weight++) {
                float tmp;

                weight->f = (float)(weight->f * factor);
                tmp = weight->f;
                weight->i = float2si(tmp);
            }
            s = (Sample *)((char *)s + size);
        }
    } else {
        /*
         * Upsampling uses the filter's native support radius.
         */
        radius = filterPtr->support;
        fscale = 1.0 / scale;
        filterSize = GetFilterSize(srcWidth, radius);
        size = GetSampleRecordSize(filterSize);
        samples = Rbc_Calloc((size_t)destWidth, size);
        s = samples;
        for (x = 0; x < destWidth; x++) {
            center = (double)x * fscale;
            GetFilterBounds(srcWidth, center, radius, &left, &right);
            s->start = left;
            s->count = right - left + 1;
            assert(s->count > 0);
            assert(s->count <= filterSize);
            sum = 0.0;
            for (weight = s->weights, i = left; i <= right; i++, weight++) {
                weight->f = (float)(*filterPtr->proc)((double)i - center + 0.5);
                sum += weight->f;
            }
            factor = (sum == 0.0) ? 1.0 : (1.0 / sum);
            for (weight = s->weights, i = left; i <= right; i++, weight++) {
                float tmp;

                weight->f = (float)(weight->f * factor);
                tmp = weight->f;
                weight->i = float2si(tmp);
            }
            s = (Sample *)((char *)s + size);
        }
    }
    *samplePtrPtr = samples;
    return size;
}

/*
 * The following macro converts a fixed-point scaled integer to a
 * byte, clamping the value between 0 and 255.
 */
#define SICLAMP(s) (unsigned char)(((s) < 0) ? 0 : ((s) > 4177920) ? 255 : (si2int(s)))

/**
 * @brief Filter source columns into an equally wide destination image.
 * @ingroup image_resample_internals
 *
 * Borrows distinct source/destination images and a valid non-NULL filter.
 * Requires equal widths; empty dimensions return without writing. Computes
 * one owned contribution array for all columns, accumulates straight RGBA
 * independently and clamps to bytes. Frees weights before returning.
 * Does not allocate image storage or perform gamma conversion; in-place
 * filtering is unsupported.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in,out] dest Borrowed writable destination image with the required matching dimensions.
 * @param[in] filterPtr Borrowed filter descriptor with a valid procedure and support.
 */
static void ZoomImageVertically(Rbc_ColorImage src, Rbc_ColorImage dest, ResampleFilter *filterPtr) {
    Sample *samples, *s, *endPtr;
    int destWidth, destHeight;
    int64_t red, green, blue, alpha;
    int srcWidth, srcHeight;
    register Pix32 *srcColumnPtr;
    register Pix32 *srcPtr, *destPtr;
    register ResampleWeight *weight;
    int x, i;
    size_t size; /* Size of sample. */

    srcWidth = Rbc_ColorImageWidth(src);
    srcHeight = Rbc_ColorImageHeight(src);
    destWidth = Rbc_ColorImageWidth(dest);
    destHeight = Rbc_ColorImageHeight(dest);
    if ((srcWidth <= 0) || (srcHeight <= 0) || (destWidth <= 0) || (destHeight <= 0)) {
        return;
    }
    /* Pre-calculate filter contributions for a row */
    size = ComputeWeights(srcHeight, destHeight, filterPtr, &samples);
    endPtr = (Sample *)((char *)samples + ((size_t)destHeight * size));
    /* Apply filter to zoom vertically from tmp to destination */
    for (x = 0; x < srcWidth; x++) {
        srcColumnPtr = Rbc_ColorImageBits(src) + x;
        destPtr = Rbc_ColorImageBits(dest) + x;
        for (s = samples; s < endPtr; s = (Sample *)((char *)s + size)) {
            red = green = blue = alpha = 0;
            srcPtr = srcColumnPtr + ((size_t)s->start * (size_t)srcWidth);
            for (weight = s->weights, i = 0; i < s->count; i++, weight++) {
                red += srcPtr->rgba.red * weight->i;
                green += srcPtr->rgba.green * weight->i;
                blue += srcPtr->rgba.blue * weight->i;
                alpha += srcPtr->rgba.alpha * weight->i;
                srcPtr += srcWidth;
            }
            destPtr->rgba.red = SICLAMP(red);
            destPtr->rgba.green = SICLAMP(green);
            destPtr->rgba.blue = SICLAMP(blue);
            destPtr->rgba.alpha = SICLAMP(alpha);
            destPtr += destWidth;
        }
    }
    /* Free the memory allocated for filter weights */
    ckfree((char *)samples);
}

/**
 * @brief Filter source rows into an equally tall destination image.
 * @ingroup image_resample_internals
 *
 * Borrows distinct source/destination images and a valid non-NULL filter.
 * Requires equal heights; returns without writing if either image has an
 * empty dimension. Computes one owned contribution array for all rows,
 * accumulates straight RGBA independently and clamps each channel to a
 * byte. Releases weights before returning. Does not resize either image
 * or perform gamma conversion; in-place filtering is unsupported.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in,out] dest Borrowed writable destination image with the required matching dimensions.
 * @param[in] filterPtr Borrowed filter descriptor with a valid procedure and support.
 */
static void ZoomImageHorizontally(Rbc_ColorImage src, Rbc_ColorImage dest, ResampleFilter *filterPtr) {
    Sample *samples, *s, *endPtr;
    ResampleWeight *weight;
    int destWidth;
    int64_t red, green, blue, alpha;
    int srcWidth, srcHeight;
    int y, i;
    register Pix32 *srcPtr, *destPtr;
    register Pix32 *srcRowPtr;
    size_t size; /* Size of sample. */

    srcWidth = Rbc_ColorImageWidth(src);
    srcHeight = Rbc_ColorImageHeight(src);
    destWidth = Rbc_ColorImageWidth(dest);
    if ((srcWidth <= 0) || (srcHeight <= 0) || (destWidth <= 0) || (Rbc_ColorImageHeight(dest) <= 0)) {
        return;
    }
    /* Pre-calculate filter contributions for a row */
    size = ComputeWeights(srcWidth, destWidth, filterPtr, &samples);
    endPtr = (Sample *)((char *)samples + ((size_t)destWidth * size));
    /* Apply filter to zoom horizontally from srcPtr to tmpPixels */
    srcRowPtr = Rbc_ColorImageBits(src);
    destPtr = Rbc_ColorImageBits(dest);
    for (y = 0; y < srcHeight; y++) {
        for (s = samples; s < endPtr; s = (Sample *)((char *)s + size)) {
            red = green = blue = alpha = 0;
            srcPtr = srcRowPtr + s->start;
            for (weight = s->weights, i = 0; i < s->count; i++, weight++) {
                red += srcPtr->rgba.red * weight->i;
                green += srcPtr->rgba.green * weight->i;
                blue += srcPtr->rgba.blue * weight->i;
                alpha += srcPtr->rgba.alpha * weight->i;
                srcPtr++;
            }
            destPtr->rgba.red = SICLAMP(red);
            destPtr->rgba.green = SICLAMP(green);
            destPtr->rgba.blue = SICLAMP(blue);
            destPtr->rgba.alpha = SICLAMP(alpha);
            destPtr++;
        }
        srcRowPtr += srcWidth;
    }
    /* free the memory allocated for horizontal filter weights */
    ckfree((char *)samples);
}

/**
 * @brief Return a newly owned image filtered horizontally and then vertically.
 *
 * Borrow the source and two valid, non-NULL filters; leave the source intact.
 * Release the result with Rbc_FreeColorImage(). Negative destination dimensions
 * or missing filters panic. Zero dimensions are allowed; empty input leaves
 * zero-initialized output. Each pass filters straight RGBA channels separately,
 * with fixed-point weights and clamping, without gamma conversion.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in] width Nonnegative horizontal pixel count; source or destination role is specified above.
 * @param[in] height Nonnegative vertical pixel count; source or destination role is specified above.
 * @param[in] horzFilterPtr Borrowed non-NULL horizontal reconstruction filter.
 * @param[in] vertFilterPtr Borrowed non-NULL vertical reconstruction filter.
 */
Rbc_ColorImage Rbc_ResampleColorImage(Rbc_ColorImage src, int width, int height, ResampleFilter *horzFilterPtr,
                                      ResampleFilter *vertFilterPtr) {
    Rbc_ColorImage tmp, dest;

    if ((width < 0) || (height < 0)) {
        Tcl_Panic("Rbc_ResampleColorImage: negative destination dimension");
    }
    if ((horzFilterPtr == NULL) || (vertFilterPtr == NULL)) {
        Tcl_Panic("Rbc_ResampleColorImage: missing resampling filter");
    }
    /*
     * It's usually faster to zoom vertically last.  This has to do
     * with the fact that images are stored in contiguous rows.
     */
    tmp = Rbc_CreateColorImage(width, Rbc_ColorImageHeight(src));
    ZoomImageHorizontally(src, tmp, horzFilterPtr);
    dest = Rbc_CreateColorImage(width, height);
    ZoomImageVertically(tmp, dest, vertFilterPtr);
    Rbc_FreeColorImage(tmp);
    return dest;
}

/**
 * @brief Filter a source photo region into the destination photo's existing dimensions.
 *
 * Snapshot the source region before writing, allowing identical photo handles.
 * Borrow both handles and valid non-NULL filters. Temporary RBC images are
 * released internally. The void photo-write wrapper does not propagate Tk's
 * write status; callers must not interpret return as a success code.
 *
 * @param[in,out] interp Interpreter receiving Tcl diagnostics; no ownership is transferred.
 * @param[in] srcPhoto Borrowed live source photo, snapshotted before destination writes.
 * @param[in] x Horizontal source origin in pixels; clamped as described above.
 * @param[in] y Vertical source origin in pixels; clamped as described above.
 * @param[in] width Requested source horizontal span; negative or excessive spans extend to the edge.
 * @param[in] height Requested source vertical span; negative or excessive spans extend to the edge.
 * @param[in,out] destPhoto Borrowed live destination photo whose existing dimensions determine output size.
 * @param[in] horzFilterPtr Borrowed non-NULL horizontal reconstruction filter.
 * @param[in] vertFilterPtr Borrowed non-NULL vertical reconstruction filter.
 */
void Rbc_ResamplePhoto(Tcl_Interp *interp, Tk_PhotoHandle srcPhoto, int x, int y, int width, int height,
                       Tk_PhotoHandle destPhoto, ResampleFilter *horzFilterPtr, ResampleFilter *vertFilterPtr) {
    Rbc_ColorImage srcImage, destImage;
    Tk_PhotoImageBlock dest;

    Tk_PhotoGetImage(destPhoto, &dest);
    srcImage = Rbc_PhotoRegionToColorImage(srcPhoto, x, y, width, height);
    destImage = Rbc_ResampleColorImage(srcImage, dest.width, dest.height, horzFilterPtr, vertFilterPtr);
    Rbc_FreeColorImage(srcImage);
    Rbc_ColorImageToPhoto(interp, destImage, destPhoto);
    Rbc_FreeColorImage(destImage);
}

/**
 * @brief Resize a source photo region to the destination's existing size without filtering.
 *
 * Borrow both handles. Snapshot before writing so source and destination may
 * be the same photo. Release temporary RBC images internally. As with
 * Rbc_ResamplePhoto(), this void wrapper does not return the Tk write status.
 *
 * @param[in,out] interp Interpreter receiving Tcl diagnostics; no ownership is transferred.
 * @param[in] srcPhoto Borrowed live source photo, snapshotted before destination writes.
 * @param[in] x Horizontal source origin in pixels; clamped as described above.
 * @param[in] y Vertical source origin in pixels; clamped as described above.
 * @param[in] width Requested source horizontal span; negative or excessive spans extend to the edge.
 * @param[in] height Requested source vertical span; negative or excessive spans extend to the edge.
 * @param[in,out] destPhoto Borrowed live destination photo whose existing dimensions determine output size.
 */
void Rbc_ResizePhoto(Tcl_Interp *interp, Tk_PhotoHandle srcPhoto, int x, int y, int width, int height,
                     Tk_PhotoHandle destPhoto) {
    Tk_PhotoImageBlock dest;
    Rbc_ColorImage srcImage;
    Rbc_ColorImage destImage;
    Tk_PhotoGetImage(destPhoto, &dest);
    srcImage = Rbc_PhotoRegionToColorImage(srcPhoto, x, y, width, height);
    destImage = Rbc_ResizeColorImage(srcImage, 0, 0, Rbc_ColorImageWidth(srcImage), Rbc_ColorImageHeight(srcImage),
                                     dest.width, dest.height);
    Rbc_FreeColorImage(srcImage);
    Rbc_ColorImageToPhoto(interp, destImage, destPhoto);
    Rbc_FreeColorImage(destImage);
}

/**
 * @brief Copy a source region into a newly owned image using nearest-neighbour mapping.
 *
 * Borrow src. Clamp the origin to source bounds; negative or excessive spans
 * extend to the remaining edge. Scale that effective region to destWidth by
 * destHeight. Empty regions leave zero-initialized output. Negative destination
 * dimensions panic; zero dimensions are allowed. Free the independent result
 * with Rbc_FreeColorImage(). All four channels are copied unchanged.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in] x Horizontal source origin in pixels; clamped as described above.
 * @param[in] y Vertical source origin in pixels; clamped as described above.
 * @param[in] width Requested source horizontal span; negative or excessive spans extend to the edge.
 * @param[in] height Requested source vertical span; negative or excessive spans extend to the edge.
 * @param[in] destWidth Nonnegative destination width in pixels.
 * @param[in] destHeight Nonnegative destination height in pixels.
 */
Rbc_ColorImage Rbc_ResizeColorImage(Rbc_ColorImage src, int x, int y, int width, int height, int destWidth,
                                    int destHeight) {
    Rbc_ColorImage dest;
    Pix32 *srcPtr;
    Pix32 *srcRowPtr;
    Pix32 *destPtr;
    int *mapX;
    int *mapY;
    int srcWidth;
    int srcHeight;
    int i;
    int j;

    if ((destWidth < 0) || (destHeight < 0)) {
        Tcl_Panic("Rbc_ResizeColorImage: negative destination dimension");
    }
    dest = Rbc_CreateColorImage(destWidth, destHeight);
    if ((destWidth == 0) || (destHeight == 0)) {
        return dest;
    }
    srcWidth = Rbc_ColorImageWidth(src);
    srcHeight = Rbc_ColorImageHeight(src);
    if (!ClipImageRegion(srcWidth, srcHeight, &x, &y, &width, &height)) {
        return dest;
    }
    mapX = Rbc_Calloc((size_t)destWidth, sizeof(*mapX));
    mapY = Rbc_Calloc((size_t)destHeight, sizeof(*mapY));
    for (i = 0; i < destWidth; i++) {
        mapX[i] = MapNearestCoordinate(x, width, i, destWidth);
    }
    for (i = 0; i < destHeight; i++) {
        mapY[i] = MapNearestCoordinate(y, height, i, destHeight);
    }
    destPtr = Rbc_ColorImageBits(dest);
    for (i = 0; i < destHeight; i++) {
        srcRowPtr = Rbc_ColorImagePixel(src, 0, mapY[i]);
        for (j = 0; j < destWidth; j++) {
            srcPtr = srcRowPtr + mapX[j];
            destPtr->value = srcPtr->value;
            destPtr++;
        }
    }
    ckfree(mapX);
    ckfree(mapY);
    return dest;
}

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
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in] regionX Horizontal origin in the virtual destination, not the source.
 * @param[in] regionY Vertical origin in the virtual destination, not the source.
 * @param[in] regionWidth Nonnegative width of the returned region.
 * @param[in] regionHeight Nonnegative height of the returned region.
 * @param[in] destWidth Virtual full-image width; positive for a nonempty region.
 * @param[in] destHeight Virtual full-image height; positive for a nonempty region.
 */
Rbc_ColorImage Rbc_ResizeColorSubimage(Rbc_ColorImage src, int regionX, int regionY, int regionWidth, int regionHeight,
                                       int destWidth, int destHeight) {
    Rbc_ColorImage dest;
    Pix32 *srcPtr;
    Pix32 *srcRowPtr;
    Pix32 *destPtr;
    int *mapX;
    int *mapY;
    int srcWidth;
    int srcHeight;
    int x, y;

    if ((regionWidth < 0) || (regionHeight < 0)) {
        Tcl_Panic("Rbc_ResizeColorSubimage: negative region dimension");
    }
    dest = Rbc_CreateColorImage(regionWidth, regionHeight);
    if ((regionWidth == 0) || (regionHeight == 0)) {
        return dest;
    }
    if ((destWidth <= 0) || (destHeight <= 0)) {
        Tcl_Panic("Rbc_ResizeColorSubimage: invalid virtual image dimension");
    }
    srcWidth = Rbc_ColorImageWidth(src);
    srcHeight = Rbc_ColorImageHeight(src);
    if ((srcWidth <= 0) || (srcHeight <= 0)) {
        return dest;
    }
    mapX = Rbc_Calloc((size_t)regionWidth, sizeof(*mapX));
    mapY = Rbc_Calloc((size_t)regionHeight, sizeof(*mapY));
    for (x = 0; x < regionWidth; x++) {
        mapX[x] = MapVirtualCoordinate(srcWidth, (double)regionX + (double)x, destWidth);
    }
    for (y = 0; y < regionHeight; y++) {
        mapY[y] = MapVirtualCoordinate(srcHeight, (double)regionY + (double)y, destHeight);
    }
    destPtr = Rbc_ColorImageBits(dest);
    for (y = 0; y < regionHeight; y++) {
        srcRowPtr = Rbc_ColorImagePixel(src, 0, mapY[y]);
        for (x = 0; x < regionWidth; x++) {
            srcPtr = srcRowPtr + mapX[x];
            destPtr->value = srcPtr->value;
            destPtr++;
        }
    }
    ckfree(mapX);
    ckfree(mapY);
    return dest;
}

/**
 * @brief Return a newly owned, same-size image filtered over RGB with opaque alpha.
 *
 * Borrow source and filter. The effective radius is max(1, (int)support).
 * The caller supplies a row-major square kernel of (2 * radius + 1)^2 finite
 * coefficients and a finite nonzero divisor in sum. scale is unused. Samples
 * beyond the image repeat its edge pixels. The kernel is traversed as stored,
 * without reversal. Release the result with Rbc_FreeColorImage(). Invalid
 * filter metadata panics; kernel length and coefficients are not validated.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in] filterPtr Borrowed square kernel descriptor; finite coefficients and nonzero sum are required.
 */
Rbc_ColorImage Rbc_ConvolveColorImage(Rbc_ColorImage src, Filter2D *filterPtr) {
    Rbc_ColorImage dest;
    Pix32 *srcPtr;
    Pix32 *destPtr;
    double *valuePtr;
    double red, green, blue;
    int width, height;
    int radius;
    int dx, dy;
    Tcl_WideInt sx, sy;

    if ((filterPtr == NULL) || (filterPtr->kernel == NULL) || (!FINITE(filterPtr->support)) ||
        (!FINITE(filterPtr->sum)) || (filterPtr->sum == 0.0) || (filterPtr->support < 0.0)) {
        Tcl_Panic("Rbc_ConvolveColorImage: invalid filter");
    }
    if (filterPtr->support > (double)(INT_MAX / 2)) {
        Tcl_Panic("Rbc_ConvolveColorImage: filter radius too large");
    }
    width = Rbc_ColorImageWidth(src);
    height = Rbc_ColorImageHeight(src);
    dest = Rbc_CreateColorImage(width, height);
    if ((width <= 0) || (height <= 0)) {
        return dest;
    }
    radius = (int)filterPtr->support;
    if (radius < 1) {
        radius = 1;
    }
    destPtr = Rbc_ColorImageBits(dest);
    for (dy = 0; dy < height; dy++) {
        for (dx = 0; dx < width; dx++) {
            red = green = blue = 0.0;
            valuePtr = filterPtr->kernel;
            for (sy = (Tcl_WideInt)dy - radius; sy <= (Tcl_WideInt)dy + radius; sy++) {
                int y;

                if (sy < 0) {
                    y = 0;
                } else if (sy >= height) {
                    y = height - 1;
                } else {
                    y = (int)sy;
                }
                for (sx = (Tcl_WideInt)dx - radius; sx <= (Tcl_WideInt)dx + radius; sx++) {
                    int x;

                    if (sx < 0) {
                        x = 0;
                    } else if (sx >= width) {
                        x = width - 1;
                    } else {
                        x = (int)sx;
                    }
                    srcPtr = Rbc_ColorImagePixel(src, x, y);
                    red += *valuePtr * (double)srcPtr->rgba.red;
                    green += *valuePtr * (double)srcPtr->rgba.green;
                    blue += *valuePtr * (double)srcPtr->rgba.blue;
                    valuePtr++;
                }
            }
            red /= filterPtr->sum;
            green /= filterPtr->sum;
            blue /= filterPtr->sum;
            destPtr->rgba.red = (unsigned char)CLAMP(red);
            destPtr->rgba.green = (unsigned char)CLAMP(green);
            destPtr->rgba.blue = (unsigned char)CLAMP(blue);
            destPtr->rgba.alpha = (unsigned char)-1;
            destPtr++;
        }
    }
    return dest;
}

/**
 * @brief Capture a drawable and optionally box-resample it into an existing photo.
 *
 * Borrows window, drawable, interpreter and photo name. Requires positive
 * capture dimensions, nonnegative destination dimensions and finite positive
 * gamma; reports TCL_ERROR for invalid arguments, missing photo or failed
 * native capture. Owns and releases captured/resampled RBC images. If sizes
 * differ, uses the box filter on both axes. Zero destination sizes are not
 * a request to retain the source size. Replaces the photo through the legacy
 * void writer, whose Tk errors are not propagated; TCL_OK therefore covers
 * capture/processing, not a checked photo-write result. Native capture and
 * Tk photo operations must run with valid objects on their owning thread.
 *
 * @param[in,out] interp Interpreter receiving Tcl diagnostics; no ownership is transferred.
 * @param[in] tkwin Borrowed live window supplying the native display context.
 * @param[in] drawable Borrowed compatible drawable to capture.
 * @param[in] x Capture left coordinate in drawable pixels.
 * @param[in] y Capture top coordinate in drawable pixels.
 * @param[in] width Positive capture width in pixels.
 * @param[in] height Positive capture height in pixels.
 * @param[in] destWidth Nonnegative destination width in pixels.
 * @param[in] destHeight Nonnegative destination height in pixels.
 * @param[in] photoName Borrowed name of an existing destination photo.
 * @param[in] inputGamma Finite positive gamma for native capture.
 */
int Rbc_SnapPhoto(Tcl_Interp *interp, Tk_Window tkwin, Drawable drawable, int x, int y, int width, int height,
                  int destWidth, int destHeight, const char *photoName, double inputGamma) {
    Tk_PhotoHandle photo; /* The photo image to write into. */
    Rbc_ColorImage image;

    if ((width <= 0) || (height <= 0) || (destWidth < 0) || (destHeight < 0) || (!FINITE(inputGamma)) ||
        (inputGamma <= 0.0)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid snapshot dimensions or gamma", -1));
        return TCL_ERROR;
    }
    photo = Tk_FindPhoto(interp, photoName);
    if (photo == NULL) {
        Rbc_AppendResultStrings(interp, "can't find photo \"", photoName, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    image = Rbc_DrawableToColorImage(tkwin, drawable, x, y, width, height, inputGamma);
    if (image == NULL) {
        Rbc_AppendResultStrings(interp, "can't grab window or pixmap (possibly obscured?)", (char *)NULL);
        return TCL_ERROR; /* Can't grab window image */
    }
    if ((destWidth != width) || (destHeight != height)) {
        Rbc_ColorImage destImage;

        /*
         * The requested size for the destination image is different than
         * that of the source snapshot.  Resample the image as necessary.
         * We'll use a cheap box filter. I'm assuming that the destination
         * image will typically be smaller than the original.
         */
        destImage = Rbc_ResampleColorImage(image, destWidth, destHeight, rbcBoxFilterPtr, rbcBoxFilterPtr);
        Rbc_FreeColorImage(image);
        image = destImage;
    }
    Rbc_ColorImageToPhoto(interp, image, photo);
    Rbc_FreeColorImage(image);
    return TCL_OK;
}

#if HAVE_JPEG
/*
 *----------------------------------------------------------------------
 *
 * Rbc_JPEGToPhoto --
 *
 *      Reads a JPEG file and converts it into a Tk photo.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      char *fileName
 *      Tk_PhotoHandle photo - The photo image to write into.
 *
 * Results:
 *      A standard Tcl result.  If successful, TCL_OK is returned
 *      and the designated photo is re-written with the image.
 *      Otherwise, TCL_ERROR is returned and the interpreter result will
 *      contain an error message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_JPEGToPhoto(Tcl_Interp *interp, const char *fileName, Tk_PhotoHandle photo) {
    Rbc_ColorImage image;

    image = Rbc_JPEGToColorImage(interp, fileName);
    if (image == NULL) {
        return TCL_ERROR;
    }
    Rbc_ColorImageToPhoto(interp, image, photo);
    Rbc_FreeColorImage(image);
    return TCL_OK;
}
#endif /* HAVE_JPEG */

/**
 * @brief Shift and filter one source row into a destination row.
 * @ingroup image_rotation_internals
 *
 * Despite its name, this helper moves pixels horizontally at fixed row y.
 * Borrows distinct images; y must exist in both. Initializes the entire
 * destination row to bgColor, then processes all source pixels, including
 * clipped ones, to retain the preceding fractional contribution. offset
 * supplies the integer displacement and frac must lie in [0,1). Writes
 * only in-range destination pixels and a final residual pixel when visible.
 * Uses 14-bit fixed-point straight RGBA arithmetic with byte clamping.
 * Caller ensures offset plus source width remains representable as int.
 * Does not allocate, resize, or validate the row index or fraction.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in,out] dest Borrowed writable destination image with the required matching dimensions.
 * @param[in] y Row index valid in both images.
 * @param[in] offset Integer horizontal displacement; offset plus source width must fit int.
 * @param[in] frac Fractional horizontal displacement in [0,1).
 * @param[in] bgColor RGBA padding color copied by value.
 */
static void ShearY(Rbc_ColorImage src, Rbc_ColorImage dest, int y, int offset, double frac, Pix32 bgColor) {
    Pix32 *srcPtr;
    Pix32 *srcRowPtr;
    Pix32 *destPtr;
    Pix32 *destRowPtr;
    int srcWidth;
    int destWidth;
    int x, dx;
    int red, green, blue, alpha;
    int leftRed, leftGreen, leftBlue, leftAlpha;
    int oldLeftRed, oldLeftGreen, oldLeftBlue, oldLeftAlpha;
    int ifrac;

    srcWidth = Rbc_ColorImageWidth(src);
    destWidth = Rbc_ColorImageWidth(dest);
    srcRowPtr = Rbc_ColorImagePixel(src, 0, y);
    destRowPtr = Rbc_ColorImagePixel(dest, 0, y);
    /*
     * Initialize the complete destination row.  This avoids creating
     * pointers outside the destination image when the shear offset is
     * negative or larger than the destination width.
     */
    for (x = 0; x < destWidth; x++) {
        destRowPtr[x] = bgColor;
    }
    oldLeftRed = uchar2si(bgColor.rgba.red);
    oldLeftGreen = uchar2si(bgColor.rgba.green);
    oldLeftBlue = uchar2si(bgColor.rgba.blue);
    oldLeftAlpha = uchar2si(bgColor.rgba.alpha);
    ifrac = float2si(frac);
    srcPtr = srcRowPtr;
    dx = offset;
    for (x = 0; x < srcWidth; x++, dx++, srcPtr++) {
        leftRed = srcPtr->rgba.red * ifrac;
        leftGreen = srcPtr->rgba.green * ifrac;
        leftBlue = srcPtr->rgba.blue * ifrac;
        leftAlpha = srcPtr->rgba.alpha * ifrac;
        if ((dx >= 0) && (dx < destWidth)) {
            destPtr = destRowPtr + dx;
            red = uchar2si(srcPtr->rgba.red) - (leftRed - oldLeftRed);
            green = uchar2si(srcPtr->rgba.green) - (leftGreen - oldLeftGreen);
            blue = uchar2si(srcPtr->rgba.blue) - (leftBlue - oldLeftBlue);
            alpha = uchar2si(srcPtr->rgba.alpha) - (leftAlpha - oldLeftAlpha);
            destPtr->rgba.red = SICLAMP(red);
            destPtr->rgba.green = SICLAMP(green);
            destPtr->rgba.blue = SICLAMP(blue);
            destPtr->rgba.alpha = SICLAMP(alpha);
        }
        oldLeftRed = leftRed;
        oldLeftGreen = leftGreen;
        oldLeftBlue = leftBlue;
        oldLeftAlpha = leftAlpha;
    }
    /*
     * Emit the residual antialiased pixel following the final source
     * pixel, if it lies inside the destination row.
     */
    if ((dx >= 0) && (dx < destWidth)) {
        leftRed = uchar2si(bgColor.rgba.red);
        leftGreen = uchar2si(bgColor.rgba.green);
        leftBlue = uchar2si(bgColor.rgba.blue);
        leftAlpha = uchar2si(bgColor.rgba.alpha);
        destPtr = destRowPtr + dx;
        red = leftRed + oldLeftRed - (bgColor.rgba.red * ifrac);
        green = leftGreen + oldLeftGreen - (bgColor.rgba.green * ifrac);
        blue = leftBlue + oldLeftBlue - (bgColor.rgba.blue * ifrac);
        alpha = leftAlpha + oldLeftAlpha - (bgColor.rgba.alpha * ifrac);
        destPtr->rgba.red = SICLAMP(red);
        destPtr->rgba.green = SICLAMP(green);
        destPtr->rgba.blue = SICLAMP(blue);
        destPtr->rgba.alpha = SICLAMP(alpha);
    }
}

/**
 * @brief Shift and filter one source column into a destination column.
 * @ingroup image_rotation_internals
 *
 * Despite its name, this helper moves pixels vertically at fixed column x.
 * Borrows distinct images; x must exist in both. Initializes the complete
 * destination column to bgColor. offset is the integer displacement and
 * frac must lie in [0,1). Accumulates the preceding fractional contribution
 * even while destination coordinates are clipped, and emits the final
 * residual pixel if visible. Filters straight RGBA with 14-bit fixed-point
 * weights and clamps channels. Requires representable offset plus source
 * height. Does not allocate or validate the column index or fraction.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in,out] dest Borrowed writable destination image with the required matching dimensions.
 * @param[in] x Column index valid in both images.
 * @param[in] offset Integer vertical displacement; offset plus source height must fit int.
 * @param[in] frac Fractional vertical displacement in [0,1).
 * @param[in] bgColor RGBA padding color copied by value.
 */
static void ShearX(Rbc_ColorImage src, Rbc_ColorImage dest, int x, int offset, double frac, Pix32 bgColor) {
    Pix32 *srcPtr;
    Pix32 *destPtr;
    int srcHeight;
    int destHeight;
    int y, dy;
    int red, green, blue, alpha;
    int leftRed, leftGreen, leftBlue, leftAlpha;
    int oldLeftRed, oldLeftGreen, oldLeftBlue, oldLeftAlpha;
    int ifrac;

    srcHeight = Rbc_ColorImageHeight(src);
    destHeight = Rbc_ColorImageHeight(dest);
    /*
     * Initialize the complete destination column.
     */
    for (y = 0; y < destHeight; y++) {
        destPtr = Rbc_ColorImagePixel(dest, x, y);
        *destPtr = bgColor;
    }
    oldLeftRed = uchar2si(bgColor.rgba.red);
    oldLeftGreen = uchar2si(bgColor.rgba.green);
    oldLeftBlue = uchar2si(bgColor.rgba.blue);
    oldLeftAlpha = uchar2si(bgColor.rgba.alpha);
    ifrac = float2si(frac);
    dy = offset;
    for (y = 0; y < srcHeight; y++, dy++) {
        srcPtr = Rbc_ColorImagePixel(src, x, y);
        leftRed = srcPtr->rgba.red * ifrac;
        leftGreen = srcPtr->rgba.green * ifrac;
        leftBlue = srcPtr->rgba.blue * ifrac;
        leftAlpha = srcPtr->rgba.alpha * ifrac;
        if ((dy >= 0) && (dy < destHeight)) {
            destPtr = Rbc_ColorImagePixel(dest, x, dy);
            red = uchar2si(srcPtr->rgba.red) - (leftRed - oldLeftRed);
            green = uchar2si(srcPtr->rgba.green) - (leftGreen - oldLeftGreen);
            blue = uchar2si(srcPtr->rgba.blue) - (leftBlue - oldLeftBlue);
            alpha = uchar2si(srcPtr->rgba.alpha) - (leftAlpha - oldLeftAlpha);
            destPtr->rgba.red = SICLAMP(red);
            destPtr->rgba.green = SICLAMP(green);
            destPtr->rgba.blue = SICLAMP(blue);
            destPtr->rgba.alpha = SICLAMP(alpha);
        }
        oldLeftRed = leftRed;
        oldLeftGreen = leftGreen;
        oldLeftBlue = leftBlue;
        oldLeftAlpha = leftAlpha;
    }
    /*
     * Emit the residual antialiased pixel after the source column.
     */
    if ((dy >= 0) && (dy < destHeight)) {
        leftRed = uchar2si(bgColor.rgba.red);
        leftGreen = uchar2si(bgColor.rgba.green);
        leftBlue = uchar2si(bgColor.rgba.blue);
        leftAlpha = uchar2si(bgColor.rgba.alpha);
        destPtr = Rbc_ColorImagePixel(dest, x, dy);
        red = leftRed + oldLeftRed - (bgColor.rgba.red * ifrac);
        green = leftGreen + oldLeftGreen - (bgColor.rgba.green * ifrac);
        blue = leftBlue + oldLeftBlue - (bgColor.rgba.blue * ifrac);
        alpha = leftAlpha + oldLeftAlpha - (bgColor.rgba.alpha * ifrac);
        destPtr->rgba.red = SICLAMP(red);
        destPtr->rgba.green = SICLAMP(green);
        destPtr->rgba.blue = SICLAMP(blue);
        destPtr->rgba.alpha = SICLAMP(alpha);
    }
}

/**
 * @brief Validate and truncate a nonnegative rotation extent.
 * @ingroup image_rotation_internals
 *
 * Returns int truncation of a finite value in [0, INT_MAX]. Invalid or
 * unrepresentable values panic. Does not round upward or add padding;
 * callers include any extra space in their dimension formula.
 *
 * @param[in] value Finite nonnegative extent no greater than INT_MAX.
 */
static int GetRotationDimension(double value) {
    if ((!FINITE(value)) || (value < 0.0) || (value > (double)INT_MAX)) {
        Tcl_Panic("image rotation dimension overflow");
    }
    return (int)value;
}

/**
 * @brief Floor a fractional displacement before checked integer conversion.
 * @ingroup image_rotation_internals
 *
 * Returns floor(value) as an int, including for negative displacements.
 * Non-finite or out-of-range results panic. The caller derives the
 * fractional remainder by subtracting this result from the original value.
 *
 * @param[in] value Finite displacement whose floor must fit int.
 */
static int GetShearOffset(double value) {
    double result;

    result = floor(value);
    if ((!FINITE(result)) || (result < (double)INT_MIN) || (result > (double)INT_MAX)) {
        Tcl_Panic("image shear offset overflow");
    }
    return (int)result;
}

/**
 * @brief Rotate a borrowed image by a residual angle using three filtered shears.
 * @ingroup image_rotation_internals
 *
 * theta is in radians and must lie in [-pi/4, pi/4]; this helper does not
 * normalize or validate that interval. Uses horizontal, vertical, horizontal
 * passes based on tan(theta/2) and sin(theta). Allocates two intermediates
 * and the final image, releasing each intermediate after its last use.
 * bgColor supplies padding and boundary contributions for every pass.
 * Returns independent owned storage released with Rbc_FreeColorImage.
 * Dimension/offset conversion panics on unrepresentable geometry.
 * The construction follows Alan W. Paeth, A Fast Algorithm for General
 * Raster Rotation, Graphics Gems, pp. 179-195. It is not a single-pass
 * inverse-mapped rotation; intermediate clamping affects pixel results.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in] theta Residual angle in radians within [-pi/4,pi/4].
 * @param[in] bgColor RGBA padding color copied by value.
 */
static Rbc_ColorImage Rotate45(Rbc_ColorImage src, double theta, Pix32 bgColor) {
    int tmpWidth, tmpHeight;
    int srcWidth, srcHeight;
    double sinTheta, cosTheta, tanTheta;
    double skewf;
    int skewi;
    Rbc_ColorImage tmp1, tmp2, dest;
    register int x, y;

    sinTheta = sin(theta);
    cosTheta = cos(theta);
    tanTheta = tan(theta * 0.5);
    srcWidth = Rbc_ColorImageWidth(src);
    srcHeight = Rbc_ColorImageHeight(src);
    tmpWidth = GetRotationDimension((double)srcWidth + ((double)srcHeight * FABS(tanTheta)));
    tmpHeight = srcHeight;
    /* 1st shear */
    tmp1 = Rbc_CreateColorImage(tmpWidth, tmpHeight);
    if (tanTheta >= 0.0) { /* Positive angle */
        for (y = 0; y < tmpHeight; y++) {
            skewf = (y + 0.5) * tanTheta;
            skewi = GetShearOffset(skewf);
            ShearY(src, tmp1, y, skewi, skewf - skewi, bgColor);
        }
    } else { /* Negative angle */
        for (y = 0; y < tmpHeight; y++) {
            skewf = ((y - srcHeight) + 0.5) * tanTheta;
            skewi = GetShearOffset(skewf);
            ShearY(src, tmp1, y, skewi, skewf - skewi, bgColor);
        }
    }
    tmpHeight = GetRotationDimension(((double)srcWidth * FABS(sinTheta)) + ((double)srcHeight * cosTheta) + 1.0);
    tmp2 = Rbc_CreateColorImage(tmpWidth, tmpHeight);
    /* 2nd shear */
    if (sinTheta > 0.0) { /* Positive angle */
        skewf = (srcWidth - 1) * sinTheta;
    } else { /* Negative angle */
        skewf = (srcWidth - tmpWidth) * -sinTheta;
    }
    for (x = 0; x < tmpWidth; x++) {
        skewi = GetShearOffset(skewf);
        ShearX(tmp1, tmp2, x, skewi, skewf - skewi, bgColor);
        skewf -= sinTheta;
    }
    Rbc_FreeColorImage(tmp1);
    /* 3rd shear */
    tmpWidth = GetRotationDimension(((double)srcHeight * FABS(sinTheta)) + ((double)srcWidth * cosTheta) + 1.0);
    dest = Rbc_CreateColorImage(tmpWidth, tmpHeight);
    if (sinTheta >= 0.0) { /* Positive angle */
        skewf = (srcWidth - 1) * sinTheta * -tanTheta;
    } else { /* Negative angle */
        skewf = tanTheta * ((srcWidth - 1) * -sinTheta - (tmpHeight - 1));
    }
    for (y = 0; y < tmpHeight; y++) {
        skewi = GetShearOffset(skewf);
        ShearY(tmp2, dest, y, skewi, skewf - skewi, bgColor);
        skewf += tanTheta;
    }
    Rbc_FreeColorImage(tmp2);
    return dest;
}

/**
 * @brief Duplicate an entire RGBA image into independent storage.
 * @ingroup image_rotation_internals
 *
 * Borrows a valid source, checks its pixel count, allocates an equal-size
 * image and copies complete Pix32 values including alpha. Empty dimensions
 * are retained. Returns ownership to the caller, which frees the result
 * with Rbc_FreeColorImage. Does not modify or share source pixel storage.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 */
static Rbc_ColorImage CopyColorImage(Rbc_ColorImage src) {
    int width, height;
    size_t nPixels;
    Pix32 *srcPtr;
    Pix32 *destPtr;
    Rbc_ColorImage dest;

    width = Rbc_ColorImageWidth(src);
    height = Rbc_ColorImageHeight(src);
    nPixels = GetColorImagePixelCount(width, height);
    dest = Rbc_CreateColorImage(width, height);
    srcPtr = Rbc_ColorImageBits(src);
    destPtr = Rbc_ColorImageBits(dest);
    memcpy(destPtr, srcPtr, nPixels * sizeof(*destPtr));
    return dest;
}

/**
 * @brief Copy pixels into a counterclockwise quarter-turn image.
 * @ingroup image_rotation_internals
 *
 * Borrows source storage and allocates a destination with exchanged width
 * and height. Maps (x,y) to (y,sourceWidth-x-1), copying all RGBA bytes
 * exactly. No interpolation or background blending occurs. Caller owns
 * the result and releases it with Rbc_FreeColorImage.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 */
static Rbc_ColorImage Rotate90(Rbc_ColorImage src) {
    Rbc_ColorImage dest;
    int srcWidth;
    int srcHeight;
    int x, y;

    srcWidth = Rbc_ColorImageWidth(src);
    srcHeight = Rbc_ColorImageHeight(src);
    dest = Rbc_CreateColorImage(srcHeight, srcWidth);
    for (y = 0; y < srcHeight; y++) {
        for (x = 0; x < srcWidth; x++) {
            *Rbc_ColorImagePixel(dest, y, srcWidth - x - 1) = *Rbc_ColorImagePixel(src, x, y);
        }
    }
    return dest;
}

/**
 * @brief Copy pixels into a half-turn image with unchanged dimensions.
 * @ingroup image_rotation_internals
 *
 * Borrows source storage and maps (x,y) to (width-x-1,height-y-1) in a
 * newly allocated image. Copies complete RGBA values with no interpolation.
 * Caller owns the independent result and frees it with Rbc_FreeColorImage.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 */
static Rbc_ColorImage Rotate180(Rbc_ColorImage src) {
    Rbc_ColorImage dest;
    int width;
    int height;
    int x, y;

    width = Rbc_ColorImageWidth(src);
    height = Rbc_ColorImageHeight(src);
    dest = Rbc_CreateColorImage(width, height);
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            *Rbc_ColorImagePixel(dest, width - x - 1, height - y - 1) = *Rbc_ColorImagePixel(src, x, y);
        }
    }
    return dest;
}

/**
 * @brief Copy pixels into a clockwise quarter-turn image.
 * @ingroup image_rotation_internals
 *
 * Borrows source storage and allocates a destination with exchanged width
 * and height. Maps (x,y) to (sourceHeight-y-1,x), copying complete pixels.
 * No interpolation occurs. Caller owns the independent result and frees
 * it with Rbc_FreeColorImage.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 */
static Rbc_ColorImage Rotate270(Rbc_ColorImage src) {
    Rbc_ColorImage dest;
    int srcWidth;
    int srcHeight;
    int x, y;

    srcWidth = Rbc_ColorImageWidth(src);
    srcHeight = Rbc_ColorImageHeight(src);
    dest = Rbc_CreateColorImage(srcHeight, srcWidth);
    for (y = 0; y < srcHeight; y++) {
        for (x = 0; x < srcWidth; x++) {
            *Rbc_ColorImagePixel(dest, srcHeight - y - 1, x) = *Rbc_ColorImagePixel(src, x, y);
        }
    }
    return dest;
}

/**
 * @brief Return a newly owned color image rotated counterclockwise in display space.
 *
 * Borrow image and accept a finite angle in degrees, normalized modulo 360.
 * Non-finite angles panic. Empty input is copied without swapping dimensions.
 * Right angles copy pixels exactly; other angles use three filtered shears
 * after a quadrant rotation, with transparent white background. The result
 * never aliases image, even at zero degrees; free with Rbc_FreeColorImage().
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in] angle Finite counterclockwise angle in degrees; normalized modulo 360.
 */
Rbc_ColorImage Rbc_RotateColorImage(Rbc_ColorImage src, double angle) {
    Rbc_ColorImage dest, tmp;
    int quadrant;

    if (!FINITE(angle)) {
        Tcl_Panic("Rbc_RotateColorImage: non-finite rotation angle");
    }
    if ((Rbc_ColorImageWidth(src) <= 0) || (Rbc_ColorImageHeight(src) <= 0)) {
        return CopyColorImage(src);
    }
    tmp = src; /* Suppress compiler warning. */
    /* Make the angle positive between 0 and 360 degrees. */
    angle = FMOD(angle, 360.0);
    if (angle < 0.0) {
        angle += 360.0;
    }
    quadrant = ROTATE_0;
    if ((angle > 45.0) && (angle <= 135.0)) {
        quadrant = ROTATE_90;
        angle -= 90.0;
    } else if ((angle > 135.0) && (angle <= 225.0)) {
        quadrant = ROTATE_180;
        angle -= 180.0;
    } else if ((angle > 225.0) && (angle <= 315.0)) {
        quadrant = ROTATE_270;
        angle -= 270.0;
    } else if (angle > 315.0) {
        angle -= 360.0;
    }
    /*
     * If necessary, create a temporary image that's been rotated
     * by a right-angle.  We'll then rotate this color image between
     * -45 to 45 degrees to arrive at its final angle.
     */
    switch (quadrant) {
    case ROTATE_270: /* 270 degrees */
        tmp = Rotate270(src);
        break;

    case ROTATE_90: /* 90 degrees */
        tmp = Rotate90(src);
        break;

    case ROTATE_180: /* 180 degrees */
        tmp = Rotate180(src);
        break;

    case ROTATE_0: /* 0 degrees */
        if (angle == 0.0) {
            tmp = CopyColorImage(src); /* Make a copy of the source. */
        }
        break;
    }
    assert((angle >= -45.0) && (angle <= 45.0));
    dest = tmp;
    if (angle != 0.0) {
        double theta;
        Pix32 *srcPtr;
        Pix32 bgColor;

        /* FIXME: pick up background blending color from somewhere */
        srcPtr = Rbc_ColorImageBits(src);
        bgColor = *srcPtr;
        bgColor.rgba.red = bgColor.rgba.green = bgColor.rgba.blue = 0xFF;
        bgColor.rgba.alpha = 0x00; /* Transparent background */
        theta = (angle / 180.0) * M_PI;
        dest = Rotate45(tmp, theta, bgColor);
        if (tmp != src) {
            Rbc_FreeColorImage(tmp);
        }
    }
    return dest;
}

/**
 * @brief Build an owned five-bit RGB histogram from a borrowed image.
 * @ingroup image_quantize_internals
 *
 * Allocates zeroed 33-cubed arrays of signed 64-bit moments. For each pixel,
 * bins each channel as (value >> 3) + 1 and accumulates count, original
 * eight-bit channel values and their squared sum. Index-zero planes stay
 * zero for cumulative queries. Alpha does not affect inclusion or weight.
 * Returns an allocation freed with ckfree; does not modify the source.
 *
 * @param[in] image Borrowed valid RGBA image; mutation policy is specified above.
 */
static ColorImageStatistics *GetColorImageStatistics(Rbc_ColorImage image) {
    register int r, g, b;
#define MAX_INTENSITIES 256
    unsigned int sqr[MAX_INTENSITIES];
    size_t numPixels;
    Pix32 *srcPtr, *endPtr;
    register int i;
    ColorImageStatistics *s;

    s = Rbc_Calloc(1, sizeof(ColorImageStatistics));
    /* Precompute table of squares. */
    for (i = 0; i < MAX_INTENSITIES; i++) {
        sqr[i] = i * i;
    }
    numPixels = GetColorImagePixelCount(Rbc_ColorImageWidth(image), Rbc_ColorImageHeight(image));
    for (srcPtr = Rbc_ColorImageBits(image), endPtr = srcPtr + numPixels; srcPtr < endPtr; srcPtr++) {
        /*
         * Reduce the number of bits (5) per color component. This
         * will keep the table size (2^15) reasonable without perceptually
         * affecting the final image.
         */
        r = (srcPtr->rgba.red >> 3) + 1;
        g = (srcPtr->rgba.green >> 3) + 1;
        b = (srcPtr->rgba.blue >> 3) + 1;
        s->wt[r][g][b] += 1;
        s->mR[r][g][b] += srcPtr->rgba.red;
        s->mG[r][g][b] += srcPtr->rgba.green;
        s->mB[r][g][b] += srcPtr->rgba.blue;
        s->gm2[r][g][b] += sqr[srcPtr->rgba.red] + sqr[srcPtr->rgba.green] + sqr[srcPtr->rgba.blue];
    }
    return s;
}

/*
 *----------------------------------------------------------------------
 * At conclusion of the histogram step, we can interpret
 *   wt[r][g][b] = sum over voxel of P(c)
 *   mR[r][g][b] = sum over voxel of r*P(c)  ,  similarly for mG, mB
 *   m2[r][g][b] = sum over voxel of c^2*P(c)
 * Actually each of these should be divided by 'size' to give the usual
 * interpretation of P() as ranging from 0 to 1, but we needn't do that here.
 *----------------------------------------------------------------------
 */

/**
 * @brief Convert raw color histograms into three-dimensional prefix sums.
 * @ingroup image_quantize_internals
 *
 * Mutates all five arrays in the borrowed statistics record. Requires the
 * raw histogram and zero boundary planes produced by GetColorImageStatistics.
 * Uses stack row/area accumulators, with no new owned allocation. This is
 * a one-time transformation, not an idempotent query. Subsequent box helpers
 * require the cumulative representation, not raw bins.
 *
 * @param[in,out] s Writable raw histogram, converted once into cumulative moments.
 */
static void M3d(ColorImageStatistics *s) {
    register unsigned char i, r, g, b, r0;
    Moment line, rLine, gLine, bLine;
    Moment area[33], rArea[33], gArea[33], bArea[33];
    Moment line2, area2[33];

    for (r = 1, r0 = 0; r <= 32; r++, r0++) {
        for (i = 0; i <= 32; ++i) {
            area2[i] = area[i] = rArea[i] = gArea[i] = bArea[i] = 0;
        }
        for (g = 1; g <= 32; g++) {
            line2 = line = rLine = gLine = bLine = 0;
            for (b = 1; b <= 32; b++) {
                line += s->wt[r][g][b];
                rLine += s->mR[r][g][b];
                gLine += s->mG[r][g][b];
                bLine += s->mB[r][g][b];
                line2 += s->gm2[r][g][b];
                area[b] += line;
                rArea[b] += rLine;
                gArea[b] += gLine;
                bArea[b] += bLine;
                area2[b] += line2;
                /* ind2 = ind1 - 1089; [r0][g][b] */
                s->wt[r][g][b] = s->wt[r0][g][b] + area[b];
                s->mR[r][g][b] = s->mR[r0][g][b] + rArea[b];
                s->mG[r][g][b] = s->mG[r0][g][b] + gArea[b];
                s->mB[r][g][b] = s->mB[r0][g][b] + bArea[b];
                s->gm2[r][g][b] = s->gm2[r0][g][b] + area2[b];
            }
        }
    }
}

/**
 * @brief Query a cumulative statistic over one half-open RGB box.
 * @ingroup image_quantize_internals
 *
 * Borrows the cube and prefix-sum array. Bounds are lower-exclusive and
 * upper-inclusive, within 0..32. Returns the eight-corner inclusion-exclusion
 * sum without modifying either argument. Requires valid ordered bounds;
 * performs no bounds checks or allocation.
 *
 * @param[in] cubePtr Borrowed box with valid lower-exclusive and upper-inclusive RGB bounds.
 * @param[in] m Borrowed 33 by 33 by 33 cumulative moment array.
 */
static INLINE Moment Volume(Cube *cubePtr, Moment m[33][33][33]) {
    return (m[R1][G1][B1] - m[R1][G1][B0] - m[R1][G0][B1] + m[R1][G0][B0] - m[R0][G1][B1] + m[R0][G1][B0] +
            m[R0][G0][B1] - m[R0][G0][B0]);
}

/*
 *----------------------------------------------------------------------
 *
 *      The next two routines allow a slightly more efficient
 *      calculation of Volume() for a proposed subbox of a given box.
 *      The sum of Top() and Bottom() is the Volume() of a subbox split
 *      in the given direction and with the specified new upper
 *      bound.
 *
 *----------------------------------------------------------------------
 */

/**
 * @brief Compute the cut-independent part of a lower subbox sum.
 * @ingroup image_quantize_internals
 *
 * Borrows the cube and cumulative array. RED, GREEN or BLUE selects the
 * coordinate whose upper bound will be replaced. Add Top at a candidate
 * position to obtain the lower subbox volume. Unknown directions return
 * zero; callers must still supply valid bounds and a supported direction.
 *
 * @param[in] cubePtr Borrowed box with valid lower-exclusive and upper-inclusive RGB bounds.
 * @param[in] dir Color axis selector: RED, GREEN or BLUE.
 * @param[in] m Borrowed 33 by 33 by 33 cumulative moment array.
 */
static Moment Bottom(Cube *cubePtr, unsigned char dir, Moment m[33][33][33]) {
    switch (dir) {
    case RED:
        return -m[R0][G1][B1] + m[R0][G1][B0] + m[R0][G0][B1] - m[R0][G0][B0];
    case GREEN:
        return -m[R1][G0][B1] + m[R1][G0][B0] + m[R0][G0][B1] - m[R0][G0][B0];
    case BLUE:
        return -m[R1][G1][B0] + m[R1][G0][B0] + m[R0][G1][B0] - m[R0][G0][B0];
    }
    return 0;
}

/**
 * @brief Compute the cut-dependent part of a lower subbox sum.
 * @ingroup image_quantize_internals
 *
 * Borrows the cube and cumulative array. Replaces the selected upper bound
 * with pos; Bottom plus this result is the proposed lower subbox volume.
 * Requires pos in the valid grid range and a RED, GREEN or BLUE direction.
 * Unknown directions return zero; no bounds checking or mutation occurs.
 *
 * @param[in] cubePtr Borrowed box with valid lower-exclusive and upper-inclusive RGB bounds.
 * @param[in] dir Color axis selector: RED, GREEN or BLUE.
 * @param[in] pos Candidate upper bound along dir, within the valid moment grid.
 * @param[in] m Borrowed 33 by 33 by 33 cumulative moment array.
 */
static Moment Top(Cube *cubePtr, unsigned char dir, int pos, Moment m[33][33][33]) {
    switch (dir) {
    case RED:
        return (m[pos][G1][B1] - m[pos][G1][B0] - m[pos][G0][B1] + m[pos][G0][B0]);
    case GREEN:
        return (m[R1][pos][B1] - m[R1][pos][B0] - m[R0][pos][B1] + m[R0][pos][B0]);
    case BLUE:
        return (m[R1][G1][pos] - m[R1][G0][pos] - m[R0][G1][pos] + m[R0][G0][pos]);
    }
    return 0;
}

/**
 * @brief Return the population-weighted squared RGB error of a box.
 * @ingroup image_quantize_internals
 *
 * Uses cumulative sums from a borrowed statistics record and cube. Returns
 * zero for nonpositive population, otherwise the squared-color sum minus
 * the sum of squared channel totals divided by population. Converts totals
 * to double before squaring. This is variance times population, not mean
 * variance; floating-point cancellation is not explicitly clamped.
 *
 * @param[in] cubePtr Borrowed box with valid lower-exclusive and upper-inclusive RGB bounds.
 * @param[in] s Borrowed cumulative color statistics; not a raw histogram.
 */
static double Variance(Cube *cubePtr, ColorImageStatistics *s) {
    Moment weight;
    double dR, dG, dB;
    double xx;

    dR = (double)Volume(cubePtr, s->mR);
    dG = (double)Volume(cubePtr, s->mG);
    dB = (double)Volume(cubePtr, s->mB);
    weight = Volume(cubePtr, s->wt);
    if (weight <= 0) {
        return 0.0;
    }
    xx = (double)(s->gm2[R1][G1][B1] - s->gm2[R1][G1][B0] - s->gm2[R1][G0][B1] + s->gm2[R1][G0][B0] -
                  s->gm2[R0][G1][B1] + s->gm2[R0][G1][B0] + s->gm2[R0][G0][B1] - s->gm2[R0][G0][B0]);
    return xx - ((dR * dR) + (dG * dG) + (dB * dB)) / (double)weight;
}

/**
 * @brief Choose a nonempty split maximizing the two mean-color scores.
 * @ingroup image_quantize_internals
 *
 * Borrows the cube, cumulative statistics and whole-box channel/count sums.
 * Examines candidate cuts in [first, last) along dir, skipping a cut when
 * either population is zero. Writes -1 initially to cut, replacing it only
 * on a strictly greater score; ties retain the earliest winning position.
 * Returns the best score, or zero if none exceeds zero. Does not mutate
 * boxes or statistics. Inputs must describe the same valid whole box.
 *
 * @param[in] cubePtr Borrowed box with valid lower-exclusive and upper-inclusive RGB bounds.
 * @param[in] dir Color axis selector: RED, GREEN or BLUE.
 * @param[in] first Inclusive first candidate cut position.
 * @param[in] last Exclusive final candidate cut position.
 * @param[out] cut Required output initialized to -1, then set to the best winning cut.
 * @param[in] rWhole Red-channel sum for the whole box.
 * @param[in] gWhole Green-channel sum for the whole box.
 * @param[in] bWhole Blue-channel sum for the whole box.
 * @param[in] wWhole Population of the whole box.
 * @param[in] s Borrowed cumulative color statistics; not a raw histogram.
 */
static double Maximize(Cube *cubePtr, unsigned char dir, int first, int last, int *cut, Moment rWhole, Moment gWhole,
                       Moment bWhole, Moment wWhole, ColorImageStatistics *s) {
    Moment rHalf, gHalf, bHalf, wHalf;
    Moment rBase, gBase, bBase, wBase;
    register int i;
    register double temp, max;

    rBase = Bottom(cubePtr, dir, s->mR);
    gBase = Bottom(cubePtr, dir, s->mG);
    bBase = Bottom(cubePtr, dir, s->mB);
    wBase = Bottom(cubePtr, dir, s->wt);
    max = 0.0;
    *cut = -1;
    for (i = first; i < last; i++) {
        rHalf = rBase + Top(cubePtr, dir, i, s->mR);
        gHalf = gBase + Top(cubePtr, dir, i, s->mG);
        bHalf = bBase + Top(cubePtr, dir, i, s->mB);
        wHalf = wBase + Top(cubePtr, dir, i, s->wt);
        /* Now half_x is sum over lower half of box, if split at i */
        if (wHalf == 0) { /* subbox could be empty of pixels! */
            continue;     /* never split into an empty box */
        } else {
            temp =
                (((double)rHalf * (double)rHalf) + ((double)gHalf * (double)gHalf) + ((double)bHalf * (double)bHalf)) /
                (double)wHalf;
        }
        rHalf = rWhole - rHalf;
        gHalf = gWhole - gHalf;
        bHalf = bWhole - bHalf;
        wHalf = wWhole - wHalf;
        if (wHalf == 0) { /* Subbox could be empty of pixels! */
            continue;     /* never split into an empty box */
        } else {
            temp +=
                (((double)rHalf * (double)rHalf) + ((double)gHalf * (double)gHalf) + ((double)bHalf * (double)bHalf)) /
                (double)wHalf;
        }
        if (temp > max) {
            max = temp;
            *cut = i;
        }
    }
    return max;
}

/**
 * @brief Split one populated color box along the best RGB direction.
 * @ingroup image_quantize_internals
 *
 * Borrows cumulative statistics; set1 and set2 must be distinct writable
 * cube records. Evaluates candidate cuts on all axes. Ties favor red, then
 * green, then blue. Returns zero without modifying cubes when no red cut
 * is available in the winning red case. On success, narrows set1 to the
 * lower box, writes set2 as the upper box, recomputes geometric volumes
 * and returns one. Does not allocate or update the statistical arrays.
 *
 * @param[in,out] set1 Writable original box, narrowed to the lower partition on success.
 * @param[out] set2 Distinct writable output box receiving the upper partition on success.
 * @param[in] s Borrowed cumulative color statistics; not a raw histogram.
 */
static int Cut(Cube *set1, Cube *set2, ColorImageStatistics *s) {
    unsigned char dir;
    int rCut, gCut, bCut;
    double rMax, gMax, bMax;
    Moment rWhole, gWhole, bWhole, wWhole;

    rWhole = Volume(set1, s->mR);
    gWhole = Volume(set1, s->mG);
    bWhole = Volume(set1, s->mB);
    wWhole = Volume(set1, s->wt);
    rMax = Maximize(set1, RED, set1->r0 + 1, set1->r1, &rCut, rWhole, gWhole, bWhole, wWhole, s);
    gMax = Maximize(set1, GREEN, set1->g0 + 1, set1->g1, &gCut, rWhole, gWhole, bWhole, wWhole, s);
    bMax = Maximize(set1, BLUE, set1->b0 + 1, set1->b1, &bCut, rWhole, gWhole, bWhole, wWhole, s);
    if ((rMax >= gMax) && (rMax >= bMax)) {
        dir = RED;
        if (rCut < 0) {
            return 0; /* can't split the box */
        }
    } else {
        dir = ((gMax >= rMax) && (gMax >= bMax)) ? GREEN : BLUE;
    }
    set2->r1 = set1->r1;
    set2->g1 = set1->g1;
    set2->b1 = set1->b1;
    switch (dir) {
    case RED:
        set2->r0 = set1->r1 = rCut;
        set2->g0 = set1->g0;
        set2->b0 = set1->b0;
        break;
    case GREEN:
        set2->g0 = set1->g1 = gCut;
        set2->r0 = set1->r0;
        set2->b0 = set1->b0;
        break;
    case BLUE:
        set2->b0 = set1->b1 = bCut;
        set2->r0 = set1->r0;
        set2->g0 = set1->g0;
        break;
    }
    set1->vol = (set1->r1 - set1->r0) * (set1->g1 - set1->g0) * (set1->b1 - set1->b0);
    set2->vol = (set2->r1 - set2->r0) * (set2->g1 - set2->g0) * (set2->b1 - set2->b0);
    return 1;
}

/**
 * @brief Greedily partition RGB space into at most nColors boxes.
 * @ingroup image_quantize_internals
 *
 * Requires nColors at least one and a writable cube array with that capacity;
 * statistics must already contain prefix sums. Starts with the full 0..32
 * box, repeatedly splits the selected box and selects the largest remaining
 * positive variance. Failed cuts disable that box and retry the slot.
 * One-cell boxes are assigned zero variance; selection ties favor the first
 * box. Returns the number of initialized boxes, possibly below nColors.
 * Owns and frees a temporary variance array. Emits no early-stop message.
 *
 * @param[in] s Borrowed cumulative color statistics; not a raw histogram.
 * @param[out] cubes Writable array with capacity for nColors boxes.
 * @param[in] nColors Positive number of partition boxes requested or supplied, as described above.
 */
static int SplitColorSpace(ColorImageStatistics *s, Cube *cubes, int nColors) {
    double *vv, temp;
    register int i;
    register int n, k;

    vv = Rbc_Calloc((size_t)nColors, sizeof(*vv));
    assert(vv);
    cubes[0].r0 = cubes[0].g0 = cubes[0].b0 = 0;
    cubes[0].r1 = cubes[0].g1 = cubes[0].b1 = 32;
    for (i = 1, n = 0; i < nColors; i++) {
        if (Cut(cubes + n, cubes + i, s)) {
            /*
             * Volume test ensures we won't try to cut one-cell box
             */
            vv[n] = vv[i] = 0.0;
            if (cubes[n].vol > 1) {
                vv[n] = Variance(cubes + n, s);
            }
            if (cubes[i].vol > 1) {
                vv[i] = Variance(cubes + i, s);
            }
        } else {
            vv[n] = 0.0; /* don't try to split this box again */
            i--;         /* didn't create box i */
        }
        n = 0;
        temp = vv[0];
        for (k = 1; k <= i; k++) {
            if (vv[k] > temp) {
                temp = vv[k];
                n = k;
            }
        }
        if (temp <= 0.0) {
            i++;
            break;
        }
    }
    ckfree((char *)vv);
    return i;
}

/**
 * @brief Fill the lookup-table cells covered by one color box.
 * @ingroup image_quantize_internals
 *
 * Borrows a valid cube and writable 33-cubed tag table. Writes label to
 * lower-exclusive, upper-inclusive cells on each axis; boundary index zero
 * is untouched for ordinary boxes. Does not allocate or inspect image data.
 * The integer label carries a packed pixel value supplied by the caller.
 *
 * @param[in] cubePtr Borrowed box with valid lower-exclusive and upper-inclusive RGB bounds.
 * @param[in] label Packed representative pixel value to store in the selected cells.
 * @param[in,out] tag Writable 33 by 33 by 33 lookup table.
 */
static void Mark(Cube *cubePtr, int label, unsigned int tag[33][33][33]) {
    int r, g, b;

    for (r = R0 + 1; r <= R1; r++) {
        for (g = G0 + 1; g <= G1; g++) {
            for (b = B0 + 1; b <= B1; b++) {
                tag[r][g][b] = label;
            }
        }
    }
}

/**
 * @brief Allocate packed representative RGB values for the partition boxes.
 * @ingroup image_quantize_internals
 *
 * Borrows cumulative statistics and nColors initialized cubes. Returns an
 * owned zeroed 33-cubed unsigned-int table, freed with ckfree. Computes
 * integer channel means, scales by 257 then shifts by eight, and marks each
 * cube with an opaque Pix32 value. Empty boxes print a stderr diagnostic
 * and use black; ordinary nonempty splits avoid that path. The mapping
 * pass restores source alpha separately. Does not change source statistics.
 *
 * @param[in] s Borrowed cumulative color statistics; not a raw histogram.
 * @param[in] cubes Borrowed array of nColors initialized partition boxes.
 * @param[in] nColors Positive number of partition boxes requested or supplied, as described above.
 */
static unsigned int *CreateColorLookupTable(ColorImageStatistics *s, Cube *cubes, int nColors) {
    unsigned int *lut;
    Pix32 color;
    unsigned int red, green, blue;
    Moment weight;
    Cube *cubePtr;
    int i;

    lut = Rbc_Calloc(33u * 33u * 33u, sizeof(*lut));
    color.rgba.alpha = (unsigned char)-1;
    for (cubePtr = cubes, i = 0; i < nColors; i++, cubePtr++) {
        weight = Volume(cubePtr, s->wt);
        if (weight) {
            red = (Volume(cubePtr, s->mR) / weight) * (NC + 1);
            green = (Volume(cubePtr, s->mG) / weight) * (NC + 1);
            blue = (Volume(cubePtr, s->mB) / weight) * (NC + 1);
        } else {
            fprintf(stderr, "bogus box %d\n", i);
            red = green = blue = 0;
        }
        color.rgba.red = red >> 8;
        color.rgba.green = green >> 8;
        color.rgba.blue = blue >> 8;
        Mark(cubePtr, color.value, (unsigned int (*)[33][33])lut);
    }
    return lut;
}

/**
 * @brief Replace RGB using one-based histogram bins while retaining source alpha.
 * @ingroup image_quantize_internals
 *
 * Borrows a source image, matching destination storage and packed lookup
 * table. Visits the source pixel count without checking destination shape.
 * Saves each alpha byte before writing, so exact source/destination aliasing
 * is supported. Uses (channel >> 3) + 1 for every table coordinate. Does
 * not allocate, dither or search for the nearest representative color.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in,out] dest Borrowed writable destination image with the required matching dimensions.
 * @param[in] lut Borrowed 33 by 33 by 33 packed-pixel lookup table.
 */
static void MapColors(Rbc_ColorImage src, Rbc_ColorImage dest, unsigned int lut[33][33][33]) {
    /* Apply the color lookup table against the original image */
    size_t count;
    Pix32 *srcPtr, *destPtr, *endPtr;
    unsigned char alpha;

    count = GetColorImagePixelCount(Rbc_ColorImageWidth(src), Rbc_ColorImageHeight(src));
    srcPtr = Rbc_ColorImageBits(src);
    destPtr = Rbc_ColorImageBits(dest);
    for (endPtr = destPtr + count; destPtr < endPtr; srcPtr++, destPtr++) {
        alpha = srcPtr->rgba.alpha;
        /* Match the one-based histogram bins; index zero is the moment boundary. */
        destPtr->value = lut[(srcPtr->rgba.red >> 3) + 1][(srcPtr->rgba.green >> 3) + 1]
                            [(srcPtr->rgba.blue >> 3) + 1];
        destPtr->rgba.alpha = alpha;
    }
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_QuantizeColorImage --
 *
 *      C Implementation of Wu's Color Quantizer (v. 2) (see Graphics Gems
 *      vol. II, pp. 126-133)
 *
 *      Author: Xiaolin Wu
 *            Dept. of Computer Science Univ. of Western
 *            Ontario London, Ontario
 *            N6A 5B7
 *            wu@csd.uwo.ca
 *
 *      Algorithm:
 *            Greedy orthogonal bipartition of RGB space for variance
 *            minimization aided by inclusion-exclusion tricks.  For
 *            speed no nearest neighbor search is done. Slightly
 *            better performance can be expected by more
 *            sophisticated but more expensive versions.
 *
 *      The author thanks Tom Lane at Tom_Lane@G.GP.CS.CMU.EDU for much of
 *      additional documentation and a cure to a previous bug.
 *
 *      Free to distribute, comments and suggestions are appreciated.
 *
 */

/**
 * @brief Map source RGB into an existing, equal-size destination while preserving alpha.
 *
 * Borrow both images; identical source and destination are supported. Require
 * 1..256 requested colors and matching dimensions, otherwise return TCL_ERROR
 * without a Tcl diagnostic or pixel changes. Empty images return TCL_OK.
 * Temporary histogram, boxes and lookup table are released internally.
 *
 * Mapping uses the same one-based channel bins as histogram construction and
 * lookup-table population. Some split diagnostics use stderr.
 *
 * @param[in] src Borrowed valid source image; ownership remains with the caller.
 * @param[in,out] dest Borrowed writable destination image with the required matching dimensions.
 * @param[in] reduceColors Requested palette size in [1,256].
 */
int Rbc_QuantizeColorImage(Rbc_ColorImage src, Rbc_ColorImage dest, int reduceColors) {
    Cube *cubes;
    ColorImageStatistics *statistics;
    int nColors;
    unsigned int *lut;

    if ((reduceColors < 1) || (reduceColors > 256)) {
        return TCL_ERROR;
    }
    if ((Rbc_ColorImageWidth(src) != Rbc_ColorImageWidth(dest)) ||
        (Rbc_ColorImageHeight(src) != Rbc_ColorImageHeight(dest))) {
        return TCL_ERROR;
    }
    if ((Rbc_ColorImageWidth(src) == 0) || (Rbc_ColorImageHeight(src) == 0)) {
        return TCL_OK;
    }
    /*
     * Allocated a structure to hold color statistics.
     */
    statistics = GetColorImageStatistics(src);
    M3d(statistics);
    cubes = Rbc_Calloc((size_t)reduceColors, sizeof(*cubes));
    nColors = SplitColorSpace(statistics, cubes, reduceColors);
    assert(nColors <= reduceColors);
    lut = CreateColorLookupTable(statistics, cubes, nColors);
    ckfree((char *)statistics);
    ckfree((char *)cubes);
    MapColors(src, dest, (unsigned int (*)[33][33])lut);
    ckfree((char *)lut);
    return TCL_OK;
}

/**
 * @ingroup image_storage_internals
 * @brief Set inclusive integer bounds from an origin and size.
 *
 * Computes endpoints in wide arithmetic and panics if either cannot fit int.
 * Does not normalize nonpositive sizes or allocate a region.
 * @param[in] x Left coordinate in pixels.
 * @param[in] y Top coordinate in pixels.
 * @param[in] width Pixel width; right is x+width-1. Positive sizes describe a nonempty region.
 * @param[in] height Pixel height; bottom is y+height-1.
 * @param[out] regionPtr Required caller-owned region record, also returned by the function.
 */
Region2D *Rbc_SetRegion(int x, int y, int width, int height, Region2D *regionPtr) {
    regionPtr->left = x;
    regionPtr->top = y;
    {
        Tcl_WideInt right;
        Tcl_WideInt bottom;

        right = (Tcl_WideInt)x + (Tcl_WideInt)width - 1;
        bottom = (Tcl_WideInt)y + (Tcl_WideInt)height - 1;
        if ((right < INT_MIN) || (right > INT_MAX) || (bottom < INT_MIN) || (bottom > INT_MAX)) {
            Tcl_Panic("Rbc_SetRegion: region coordinate overflow");
        }
        regionPtr->right = (int)right;
        regionPtr->bottom = (int)bottom;
    }
    return regionPtr;
}

/** @brief Compatibility view of Tk image instance internals; layout must match the supported Tk version. */
typedef struct TkImageStruct {
    Tk_Window tkwin; /**< Window passed to Tk_GetImage (needed to "re-get" the image later if the manager changes). */
    Display *display; /**< Display for tkwin. Needed because when the image is eventually freed tkwin may not exist
     * anymore.
     */
    struct TkImageMasterStruct *masterPtr; /**< Borrowed Tk image master. */
    /* Master for this image (identifiers image
     * manager, for example). */
    ClientData instanceData; /**< Tk image-type instance payload. */
    /* One word argument to pass to image manager
     * when dealing with this image instance. */
    Tk_ImageChangedProc *changeProc; /**< Tk image-change callback. */
    /* Code in widget to call when image changes
     * in a way that affects redisplay. */
    ClientData widgetClientData; /**< Borrowed callback data supplied by the image consumer. */
    /* Argument to pass to changeProc. */
    struct Image *nextPtr; /**< Next in list of all image instances associated with the same name. */

} TkImage;

/** @brief Compatibility view of Tk image master internals; RBC does not own these fields. */
typedef struct TkImageMasterStruct {
    Tk_ImageType *typePtr; /**< Information about image type. NULL means that no image manager owns this image: the image was
     * deleted.
     */
    ClientData masterData; /**< One-word argument to pass to image mgr when dealing with the master, as opposed to instances. */
    int width; /**< Last known dimensions for image. */
    int height; /**< Last known dimensions for image. */
    Tcl_HashTable *tablePtr; /**< Pointer to hash table containing image (the imageTable field in some TkMainInfo structure). */
    Tcl_HashEntry *hPtr; /**< Hash entry in mainPtr->imageTable for this structure (used to delete the hash entry). */
    TkImage *instancePtr; /**< Pointer to first in list of instances derived from this name. */
} TkImageMaster;

typedef struct TkPhotoMasterStruct TkPhotoMaster;
typedef struct TkColorTableStruct TkColorTable;

/** @brief Compatibility view of Tk photo instance internals; RBC does not own these fields. */
typedef struct TkPhotoInstanceStruct {
    TkPhotoMaster *masterPtr; /**< Pointer to master for image. */
    Display *display; /**< Display for windows using this instance. */
    Colormap colormap; /**< The image may only be used in windows with this particular colormap. */
    struct TkPhotoInstanceStruct *nextPtr; /**< Next Tk-managed instance of the same photo master. */
    /* Pointer to the next instance in the list
     * of instances associated with this master. */
    int refCount; /**< Number of instances using this structure. */
    Tk_Uid palette; /**< Palette for these particular instances. */
    double outputGamma; /**< Gamma value for these instances. */
    Tk_Uid defaultPalette; /**< Default palette to use if a palette is not specified for the master. */
    TkColorTable *colorTablePtr; /**< Pointer to information about colors allocated for image display in windows like this one. */
    Pixmap pixels; /**< X pixmap containing dithered image. */
    int width; /**< Dimensions of the pixmap. */
    int height; /**< Dimensions of the pixmap. */
    char *error; /**< Error image, used in dithering. */
    XImage *imagePtr; /**< Image structure for converted pixels. */
    XVisualInfo visualInfo; /**< Information about the visual that these windows are using. */
    GC gc; /**< Graphics context for writing images to the pixmap. */
} TkPhotoInstance;

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_ImageIsDeleted --
 *
 *      Is there any other way to determine if an image has been
 *      deleted?
 *
 * Parameters:
 *      Tk_Image tkImage - Token for image.
 *
 * Results:
 *      Returns 1 if the image has been deleted, 0 otherwise.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
int Rbc_ImageIsDeleted(Tk_Image tkImage) {
    TkImage *imagePtr = (TkImage *)tkImage;
    if (imagePtr->masterPtr == NULL) {
        return TRUE;
    }
    return (imagePtr->masterPtr->typePtr == NULL);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ImageGetMaster --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Image tkImage - Token for image.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Tk_ImageMaster Rbc_ImageGetMaster(Tk_Image tkImage) {
    TkImage *imagePtr = (TkImage *)tkImage;
    return (Tk_ImageMaster)imagePtr->masterPtr;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ImageGetType --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Image tkImage - Token for image.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Tk_ImageType *Rbc_ImageGetType(Tk_Image tkImage) {
    TkImage *imagePtr = (TkImage *)tkImage;
    return imagePtr->masterPtr->typePtr;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ImageGetPhotoPixmap --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Image tkImage - Token for image.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Pixmap Rbc_ImageGetPhotoPixmap(Tk_Image tkImage) {
    TkImage *imagePtr = (TkImage *)tkImage;
    if (strcmp(imagePtr->masterPtr->typePtr->name, "photo") == 0) {
        TkPhotoInstance *instPtr = (TkPhotoInstance *)imagePtr->instanceData;
        return instPtr->pixels;
    }
    return None;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ImageGetPhotoGC --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Image photoImage - Token for image.
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
GC Rbc_ImageGetPhotoGC(Tk_Image photoImage) {
    TkImage *imagePtr = (TkImage *)photoImage;
    if (strcmp(imagePtr->masterPtr->typePtr->name, "photo") == 0) {
        TkPhotoInstance *instPtr = (TkPhotoInstance *)imagePtr->instanceData;
        return instPtr->gc;
    }
    return NULL;
}

/**
 * @ingroup image_storage_internals
 * @brief Ignore notifications from the internally managed temporary photo.
 *
 * This callback does not schedule redraw, retain data or release the image token.
 * @param[in] clientData Caller-supplied callback data; unused and not dereferenced.
 * @param[in] x Changed image rectangle X origin; unused.
 * @param[in] y Changed image rectangle Y origin; unused.
 * @param[in] width Changed rectangle width; unused.
 * @param[in] height Changed rectangle height; unused.
 * @param[in] imageWidth Current complete image width; unused.
 * @param[in] imageHeight Current complete image height; unused.
 */
static void TempImageChangedProc(ClientData clientData, int x, int y, int width, int height, int imageWidth,
                                 int imageHeight) {
#ifdef notdef
    fprintf(stderr, "should be redrawing temp image\n");
#endif
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_CreateTemporaryImage --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      ClientData clientData
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Tk_Image Rbc_CreateTemporaryImage(Tcl_Interp *interp, Tk_Window tkwin, ClientData clientData) {
    Tcl_Obj *objv[3];
    Tcl_Obj *resultObj;
    const char *name;
    Tk_Image token;
    int result;

    objv[0] = Tcl_NewStringObj("image", -1);
    objv[1] = Tcl_NewStringObj("create", -1);
    objv[2] = Tcl_NewStringObj("photo", -1);
    Tcl_IncrRefCount(objv[0]);
    Tcl_IncrRefCount(objv[1]);
    Tcl_IncrRefCount(objv[2]);
    result = Tcl_EvalObjv(interp, 3, objv, 0);
    Tcl_DecrRefCount(objv[2]);
    Tcl_DecrRefCount(objv[1]);
    Tcl_DecrRefCount(objv[0]);
    if (result != TCL_OK) {
        return NULL;
    }
    /*
     * Hold the result object while Tk_GetImage() consumes the image
     * name.  Tk_GetImage() may itself modify the interpreter result
     * on failure.
     */
    resultObj = Tcl_GetObjResult(interp);
    Tcl_IncrRefCount(resultObj);
    name = Tcl_GetString(resultObj);
    token = Tk_GetImage(interp, tkwin, name, TempImageChangedProc, clientData);
    if (token == NULL) {
        Tcl_InterpState state;

        /* Roll back the named image without losing the acquisition error. */
        Tcl_Preserve(interp);
        state = Tcl_SaveInterpState(interp, TCL_ERROR);
        Tk_DeleteImage(interp, name);
        Tcl_RestoreInterpState(interp, state);
        Tcl_DecrRefCount(resultObj);
        Tcl_Release(interp);
        return NULL;
    }
    Tcl_DecrRefCount(resultObj);
    return token;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_DestroyTemporaryImage --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Image tkImage
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_DestroyTemporaryImage(Tcl_Interp *interp, Tk_Image tkImage) {
    Tcl_InterpState state;
    Tcl_Obj *nameObj = NULL;

    if (tkImage == NULL) {
        return TCL_OK;
    }
    /* A retained instance may outlive deletion of its named image. */
    if (!Rbc_ImageIsDeleted(tkImage)) {
        nameObj = Tcl_NewStringObj(Rbc_NameOfImage(tkImage), -1);
        Tcl_IncrRefCount(nameObj);
    }
    Tcl_Preserve(interp);
    state = Tcl_SaveInterpState(interp, TCL_OK);
    /* Private resource cleanup must not depend on a replaceable Tcl command. */
    if (nameObj != NULL) {
        Tk_DeleteImage(interp, Tcl_GetString(nameObj));
        Tcl_DecrRefCount(nameObj);
    }
    Tk_FreeImage(tkImage);
    Tcl_RestoreInterpState(interp, state);
    Tcl_Release(interp);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_NameOfImage --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Image tkImage
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
const char *Rbc_NameOfImage(Tk_Image tkImage) {
    Tk_ImageMaster master;

    master = Rbc_ImageGetMaster(tkImage);
    return Tk_NameOfImage(master);
}
