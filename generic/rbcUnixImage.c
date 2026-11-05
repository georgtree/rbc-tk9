
/*
 * rbcUnixImage.c --
 *
 *	This module implements image processing procedures for the rbc
 *	toolkit.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/**
 * @file rbcUnixImage.c
 * @brief X11 drawable capture and native bitmap conversion.
 */
/**
 * @defgroup x11_image_helpers X11 image sizing and capture helpers
 * @brief Checked dimensions, visual channel masks and asynchronous capture errors.
 */

#include "rbcInt.h"
#include "rbcImage.h"
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @ingroup x11_image_helpers
 * @brief Compute a nonnegative image pixel count without size_t overflow.
 * Returns width times height, including zero for an empty dimension. Negative dimensions or overflow
 * panic rather than returning a recoverable Tcl error. Does not check a later pixel-byte
 * multiplication or allocate storage.
 *
 * @param[in] width Nonnegative image width; a negative value causes Tcl_Panic.
 * @param[in] height Nonnegative image height; a negative value or product overflow causes Tcl_Panic.
 */
static size_t GetUnixImagePixelCount(int width, int height) {
    if ((width < 0) || (height < 0)) {
        Tcl_Panic("negative image dimension");
    }
    if ((width != 0) && ((size_t)height > SIZE_MAX / (size_t)width)) {
        Tcl_Panic("image size overflow");
    }
    return (size_t)width * (size_t)height;
}

/**
 * @ingroup x11_image_helpers
 * @brief Compute bytes per tightly packed one-bit bitmap row.
 * Returns ceiling(width/8) using size_t arithmetic, including zero for width zero. Negative width
 * panics. This is byte packing, not the Windows DWORD-aligned bitmap stride.
 *
 * @param[in] width Nonnegative width in bits/pixels; zero returns zero bytes and a negative width panics.
 */
static size_t GetPackedBitmapRowBytes(int width) {
    if (width < 0) {
        Tcl_Panic("negative bitmap width");
    }
    return ((size_t)width + 7u) / 8u;
}

/**
 * @ingroup x11_image_helpers
 * @brief Round a geometric extent upward to a usable native dimension.
 * Applies ceil and accepts only finite results from 1 through INT_MAX. Returns zero for invalid,
 * nonpositive or unrepresentable rounded dimensions. Does not allocate or set an interpreter
 * diagnostic.
 *
 * @param[in] value Geometric pixel extent to round upward; invalid or unrepresentable rounded dimensions return
 *   zero.
 */
static int GetBitmapDimension(double value) {
    value = ceil(value);
    if ((!FINITE(value)) || (value < 1.0) || (value > (double)INT_MAX)) {
        return 0;
    }
    return (int)value;
}

#define CLAMP(c) ((((c) < 0.0) ? 0.0 : ((c) > 255.0) ? 255.0 : (c)))

/* Per-capture shifts for converting TrueColor pixels to RGB bytes. */
typedef struct {
    int redAdjust, greenAdjust, blueAdjust;
    int redMaskShift, greenMaskShift, blueMaskShift;
} TrueColorMasks;

/**
 * @ingroup x11_image_helpers
 * @brief Find the lowest set bit in a 32-bit visual component mask.
 * Examines at most 32 positions and returns 32 when none is set. Caller supplies a nonzero valid
 * TrueColor mask before using the result as a shift. Does not allocate or inspect a display.
 *
 * @param[in] mask Visual component mask within the low 32 bits; no display resource ownership is involved.
 */
static int ShiftCount(register unsigned int mask) {
    int count;

    for (count = 0; count < 32; count++) {
        if (mask & 0x01) {
            break;
        }
        mask >>= 1;
    }
    return count;
}

/**
 * @ingroup x11_image_helpers
 * @brief Count set bits in a 32-bit visual mask.
 * Uses parallel masked tallies over the low 32 bits, returning zero for a zero mask. The caller
 * supplies a visual component mask within that range. Runs during mask preparation, not once
 * per captured pixel.
 *
 * @param[in] mask Visual component mask within the low 32 bits; no display resource ownership is involved.
 */
static int CountBits(register unsigned long mask) {
    /* 16  2-bit tallies */
    mask = (mask & 0x55555555) + ((mask >> 1) & (0x55555555));
    /* 8  4-bit tallies */
    mask = (mask & 0x33333333) + ((mask >> 2) & (0x33333333));
    /* 4  8-bit tallies */
    mask = (mask & 0x07070707) + ((mask >> 4) & (0x07070707));
    /* 2 16-bit tallies */
    mask = (mask & 0x000F000F) + ((mask >> 8) & (0x000F000F));
    /* 1 32-bit tally */
    mask = (mask & 0x0000001F) + ((mask >> 16) & (0x0000001F));
    return mask;
}

/**
 * @ingroup x11_image_helpers
 * @brief Prepare per-capture TrueColor shifts for eight-bit gamma indices.
 * Borrows a Visual with valid nonzero contiguous RGB masks in its 32-bit pixel layout and fills
 * caller-owned shift metadata. Components narrower than eight bits retain the legacy left shift
 * without low-bit replication. Wider components discard low bits by increasing the extraction shift,
 * so ten-bit channels cannot index beyond the 256-entry gamma table. Eight-bit extraction is
 * unchanged. No display calls, shared mutable state or allocation occur.
 *
 * @param[in] visualPtr Borrowed TrueColor visual with valid nonzero contiguous RGB masks in a 32-bit pixel
 *   layout.
 * @param[out] masks Required caller-owned shift metadata; all extraction shifts and eight-bit adjustments are
 *   assigned.
 */
static void ComputeMasks(const Visual *visualPtr, TrueColorMasks *masks) {
    int count;

    masks->redMaskShift = ShiftCount((unsigned int)visualPtr->red_mask);
    masks->greenMaskShift = ShiftCount((unsigned int)visualPtr->green_mask);
    masks->blueMaskShift = ShiftCount((unsigned int)visualPtr->blue_mask);
    masks->redAdjust = masks->greenAdjust = masks->blueAdjust = 0;
    count = CountBits((unsigned long)visualPtr->red_mask);
    if (count < 8) {
        masks->redAdjust = 8 - count;
    } else if (count > 8) {
        /* Discard low component bits before indexing the 256-entry LUT. */
        masks->redMaskShift += count - 8;
    }
    count = CountBits((unsigned long)visualPtr->green_mask);
    if (count < 8) {
        masks->greenAdjust = 8 - count;
    } else if (count > 8) {
        /* Discard low component bits before indexing the 256-entry LUT. */
        masks->greenMaskShift += count - 8;
    }
    count = CountBits((unsigned long)visualPtr->blue_mask);
    if (count < 8) {
        masks->blueAdjust = 8 - count;
    } else if (count > 8) {
        /* Discard low component bits before indexing the 256-entry LUT. */
        masks->blueMaskShift += count - 8;
    }
}

/**
 * @ingroup x11_image_helpers
 * @brief Record an X capture error in a caller-owned status slot.
 * Sets the retained integer slot to TCL_ERROR and returns zero to mark the error handled. Does not
 * inspect error fields, report Tcl diagnostics or release resources. The caller keeps the slot alive
 * until XSync has delivered errors and the temporary Tk handler is removed.
 *
 * @param[in,out] clientData Borrowed int status slot kept alive until XSync and handler removal; set to
 *   TCL_ERROR.
 * @param[in] errEventPtr Unused borrowed X error event; its fields are not inspected.
 */
static int XGetImageErrorProc(ClientData clientData, XErrorEvent *errEventPtr) {
    int *errorPtr = clientData;
    *errorPtr = TCL_ERROR;
    return 0;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DrawableToColorImage --
 *
 *      Takes a snapshot of an X drawable (pixmap or window) and
 *    converts it to a color image.
 *
 *    The trick here is to efficiently convert the pixel values
 *    (indices into the color table) into RGB color values.  In the
 *    days of 8-bit displays, it was simpler to get RGB values for
 *    all 256 indices into the colormap.  Instead we'll build a
 *    hashtable of unique pixels and from that an array of pixels to
 *    pass to XQueryColors.  For TrueColor visuals, we'll simple
 *    compute the colors from the pixel.
 *
 *    [I don't know how much faster it would be to take advantage
 *    of all the different visual types.  This pretty much depends
 *    on the size of the image and the number of colors it uses.]
 *
 * Parameters:
 *      Tk_Window tkwin
 *      Drawable drawable
 *      register int x - -
 *      register int y - Offset of image from the drawable's origin.
 *      int width - -
 *      int height - Dimension of the image.  Image must be completely contained by the drawable.
 *      double inputGamma
 *
 * Results:
 *      Returns a color image of the drawable.  If an error occurred,
 *    NULL is returned.
 *
 *----------------------------------------------------------------------
 */
Rbc_ColorImage Rbc_DrawableToColorImage(Tk_Window tkwin, Drawable drawable, register int x, register int y, int width,
                                        int height, double inputGamma) {
    Display *display;
    XImage *imagePtr;
    Rbc_ColorImage image;
    register Pix32 *destPtr;
    unsigned long pixel;
    int result = TCL_OK;
    Tk_ErrorHandler errHandler;
    Visual *visualPtr;
    unsigned char lut[256];

    if ((width <= 0) || (height <= 0) || (!FINITE(inputGamma)) || (inputGamma <= 0.0)) {
        return NULL;
    }
    display = Tk_Display(tkwin);
    errHandler = Tk_CreateErrorHandler(display, BadMatch, X_GetImage, -1, XGetImageErrorProc, &result);
    imagePtr = XGetImage(display, drawable, x, y, width, height, AllPlanes, ZPixmap);
    /*
     * Force delivery of any asynchronous X error while our handler is
     * still installed.
     */
    XSync(display, False);
    Tk_DeleteErrorHandler(errHandler);
    if ((result != TCL_OK) || (imagePtr == NULL)) {
        if (imagePtr != NULL) {
            XDestroyImage(imagePtr);
        }
        return NULL;
    }
    {
        int i;
        double value;

        for (i = 0; i < 256; i++) {
            value = pow(i / 255.0, inputGamma) * 255.0 + 0.5;
            lut[i] = (unsigned char)CLAMP(value);
        }
    }
    /*
     * First allocate a color image to hold the screen snapshot.
     */
    image = Rbc_CreateColorImage(width, height);
    visualPtr = Tk_Visual(tkwin);
    if (visualPtr->class == TrueColor) {
        unsigned int red, green, blue;
        TrueColorMasks masks;

        /*
         * Directly compute the RGB color values from the pixel index
         * rather than going through XQueryColors.
         */
        ComputeMasks(visualPtr, &masks);
        destPtr = Rbc_ColorImageBits(image);
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                pixel = XGetPixel(imagePtr, x, y);
                red = ((pixel & visualPtr->red_mask) >> masks.redMaskShift) << masks.redAdjust;
                green = ((pixel & visualPtr->green_mask) >> masks.greenMaskShift) << masks.greenAdjust;
                blue = ((pixel & visualPtr->blue_mask) >> masks.blueMaskShift) << masks.blueAdjust;
                destPtr->rgba.red = lut[red];
                destPtr->rgba.green = lut[green];
                destPtr->rgba.blue = lut[blue];
                destPtr->rgba.alpha = (unsigned char)-1;
                destPtr++;
            }
        }
        XDestroyImage(imagePtr);
    } else {
        Tcl_HashEntry *hPtr;
        Tcl_HashSearch cursor;
        Tcl_HashTable pixelTable;
        XColor *colorPtr, *colorArr;
        Pix32 *endPtr;
        size_t nPixels;
        Tcl_Size nColors;
        int isNew;

        /*
         * Fill the image with each pixel value and build a table of
         * the unique pixels used.
         */
        nPixels = GetUnixImagePixelCount(width, height);
        Tcl_InitHashTable(&pixelTable, TCL_ONE_WORD_KEYS);
        destPtr = Rbc_ColorImageBits(image);
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                pixel = XGetPixel(imagePtr, x, y);
                hPtr = Tcl_CreateHashEntry(&pixelTable, (char *)pixel, &isNew);
                if (isNew) {
                    Tcl_SetHashValue(hPtr, (char *)pixel);
                }
                destPtr->value = pixel;
                destPtr++;
            }
        }
        XDestroyImage(imagePtr);
        /*
         * XQueryColors takes the number of colors as an int.
         */
        nColors = pixelTable.numEntries;
        if (nColors > (Tcl_Size)INT_MAX) {
            Tcl_DeleteHashTable(&pixelTable);
            Rbc_FreeColorImage(image);
            return NULL;
        }
        /*
         * Build one XColor array.  The hash-table values point directly
         * into this array, which XQueryColors then fills in place.
         */
        colorArr = RbcCalloc((size_t)nColors, sizeof(*colorArr));
        colorPtr = colorArr;
        for (hPtr = Tcl_FirstHashEntry(&pixelTable, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
            colorPtr->pixel = (unsigned long)Tcl_GetHashValue(hPtr);
            Tcl_SetHashValue(hPtr, (char *)colorPtr);
            colorPtr++;
        }
        XQueryColors(display, Tk_Colormap(tkwin), colorArr, (int)nColors);
        /*
         * Replace each stored pixel value with its RGB value.
         */
        destPtr = Rbc_ColorImageBits(image);
        endPtr = destPtr + nPixels;
        for (/* empty */; destPtr < endPtr; destPtr++) {
            hPtr = Tcl_FindHashEntry(&pixelTable, INT2PTR(destPtr->value));
            colorPtr = (XColor *)Tcl_GetHashValue(hPtr);
            destPtr->rgba.red = lut[colorPtr->red >> 8];
            destPtr->rgba.green = lut[colorPtr->green >> 8];
            destPtr->rgba.blue = lut[colorPtr->blue >> 8];
            destPtr->rgba.alpha = (unsigned char)-1;
        }
        ckfree((char *)colorArr);
        Tcl_DeleteHashTable(&pixelTable);
    }
    return image;
}

Pixmap Rbc_PhotoImageMask(Tk_Window tkwin, Tk_PhotoImageBlock src) {
    Pixmap bitmap;
    size_t bytesPerLine;
    size_t count = 0;
    int value, bitMask;
    int x, y;
    unsigned char *bits;
    unsigned char *srcPtr;
    unsigned char *destPtr;
    unsigned long pixel;
    unsigned char *srcRowPtr;

    bytesPerLine = GetPackedBitmapRowBytes(src.width);
    bits = RbcCalloc((size_t)src.height, bytesPerLine);
    assert(bits);
    destPtr = bits;
    srcRowPtr = src.pixelPtr;
    for (y = 0; y < src.height; y++) {
        value = 0, bitMask = 1;
        srcPtr = srcRowPtr;
        for (x = 0; x < src.width; /*empty*/) {
            pixel = (srcPtr[src.offset[3]] != 0x00);
            if (pixel) {
                value |= bitMask;
            } else {
                count++; /* Count the number of transparent pixels. */
            }
            bitMask <<= 1;
            x++;
            if (!(x & 7)) {
                *destPtr++ = (unsigned char)value;
                value = 0, bitMask = 1;
            }
            srcPtr += src.pixelSize;
        }
        if (x & 7) {
            *destPtr++ = (unsigned char)value;
        }
        srcRowPtr += src.pitch;
    }
    if (count > 0) {
        Tk_MakeWindowExist(tkwin);
        bitmap = XCreateBitmapFromData(Tk_Display(tkwin), Tk_WindowId(tkwin), (char *)bits, (unsigned int)src.width,
                                       (unsigned int)src.height);
    } else {
        bitmap = None; /* Image is opaque. */
    }
    ckfree((char *)bits);
    return bitmap;
}

Pixmap Rbc_ColorImageMask(Tk_Window tkwin, Rbc_ColorImage image) {
    Pixmap bitmap;
    size_t bytesPerLine;
    size_t count;
    int value, bitMask;
    register int x, y;
    unsigned char *bits;
    Pix32 *srcPtr;
    unsigned char *destPtr;
    unsigned long pixel;
    int width, height;

    width = Rbc_ColorImageWidth(image);
    height = Rbc_ColorImageHeight(image);
    bytesPerLine = GetPackedBitmapRowBytes(width);
    bits = RbcCalloc((size_t)height, bytesPerLine);
    assert(bits);
    destPtr = bits;
    count = 0;
    srcPtr = Rbc_ColorImageBits(image);
    for (y = 0; y < height; y++) {
        value = 0, bitMask = 1;
        for (x = 0; x < width; /*empty*/) {
            pixel = (srcPtr->rgba.alpha != 0x00);
            if (pixel) {
                value |= bitMask;
            } else {
                count++; /* Count the number of transparent pixels. */
            }
            bitMask <<= 1;
            x++;
            if (!(x & 7)) {
                *destPtr++ = (unsigned char)value;
                value = 0, bitMask = 1;
            }
            srcPtr++;
        }
        if (x & 7) {
            *destPtr++ = (unsigned char)value;
        }
    }
    if (count > 0) {
        Tk_MakeWindowExist(tkwin);
        bitmap = XCreateBitmapFromData(Tk_Display(tkwin), Tk_WindowId(tkwin), (char *)bits, (unsigned int)width,
                                       (unsigned int)height);
    } else {
        bitmap = None; /* Image is opaque. */
    }
    ckfree((char *)bits);
    return bitmap;
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_RotateBitmap --
 *
 *    Creates a new bitmap containing the rotated image of the given
 *    bitmap.  We also need a special GC of depth 1, so that we do
 *    not need to rotate more than one plane of the bitmap.
 *
 * Parameters:
 *      Tk_Window tkwin - Window for which border was allocated.
 *      Pixmap srcBitmap - Source bitmap to be rotated
 *      int srcWidth - Width of the source bitmap
 *      int srcHeight - Height of the source bitmap
 *      double theta - Right angle rotation to perform
 *      int *destWidthPtr
 *      int *destHeightPtr
 *
 * Results:
 *    Returns a new bitmap containing the rotated image.
 *
 * -----------------------------------------------------------------
 */
Pixmap Rbc_RotateBitmap(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight, double theta, int *destWidthPtr,
                        int *destHeightPtr) {
    Display *display; /* X display */
    Window root;      /* Root window drawable */
    Pixmap destBitmap;
    int destWidth, destHeight;
    XImage *src, *dest;
    register int x, y;   /* Destination bitmap coordinates */
    register int sx, sy; /* Source bitmap coordinates */
    unsigned long pixel;
    GC bitmapGC;
    double rotWidth, rotHeight;

    if ((srcWidth <= 0) || (srcHeight <= 0) || (!FINITE(theta))) {
        return None;
    }
    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    /* Create a bitmap and image big enough to contain the rotated text */
    Rbc_GetBoundingBox(srcWidth, srcHeight, theta, &rotWidth, &rotHeight, (Point2D *)NULL);
    destWidth = GetBitmapDimension(rotWidth);
    destHeight = GetBitmapDimension(rotHeight);
    if ((destWidth == 0) || (destHeight == 0)) {
        return None;
    }
    destBitmap = Tk_GetPixmap(display, root, destWidth, destHeight, 1);
    bitmapGC = Rbc_GetBitmapGC(tkwin);
    XSetForeground(display, bitmapGC, 0x0);
    XFillRectangle(display, destBitmap, bitmapGC, 0, 0, destWidth, destHeight);
    src = XGetImage(display, srcBitmap, 0, 0, srcWidth, srcHeight, 1, ZPixmap);
    if (src == NULL) {
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    dest = XGetImage(display, destBitmap, 0, 0, destWidth, destHeight, 1, ZPixmap);
    if (dest == NULL) {
        XDestroyImage(src);
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    theta = FMOD(theta, 360.0);
    if (theta < 0.0) {
        theta += 360.0;
    }
    if (FMOD(theta, (double)90.0) == 0.0) {
        int quadrant;

        /* Handle right-angle rotations specifically */
        quadrant = (int)(theta / 90.0);
        switch (quadrant) {
        case ROTATE_270: /* 270 degrees */
            for (y = 0; y < destHeight; y++) {
                sx = y;
                for (x = 0; x < destWidth; x++) {
                    sy = destWidth - x - 1;
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;
        case ROTATE_180: /* 180 degrees */
            for (y = 0; y < destHeight; y++) {
                sy = destHeight - y - 1;
                for (x = 0; x < destWidth; x++) {
                    sx = destWidth - x - 1, pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;
        case ROTATE_90: /* 90 degrees */
            for (y = 0; y < destHeight; y++) {
                sx = destHeight - y - 1;
                for (x = 0; x < destWidth; x++) {
                    sy = x;
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;
        case ROTATE_0: /* 0 degrees */
            for (y = 0; y < destHeight; y++) {
                for (x = 0; x < destWidth; x++) {
                    pixel = XGetPixel(src, x, y);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;
        default:
            /* The calling routine should never let this happen. */
            break;
        }
    } else {
        double radians, sinTheta, cosTheta;
        double sox, soy;       /* Offset from the center of
                                * the source rectangle. */
        double destCX, destCY; /* Offset to the center of the destination
                                * rectangle. */
        double tx, ty;         /* Translated coordinates from center */
        double rx, ry;         /* Angle of rotation for x and y coordinates */

        radians = (theta / 180.0) * M_PI;
        sinTheta = sin(radians), cosTheta = cos(radians);
        /*
         * Coordinates of the centers of the source and destination rectangles
         */
        sox = srcWidth * 0.5;
        soy = srcHeight * 0.5;
        destCX = destWidth * 0.5;
        destCY = destHeight * 0.5;
        /* For each pixel of the destination image, transform back to the
         * associated pixel in the source image. */
        for (y = 0; y < destHeight; y++) {
            ty = y - destCY;
            for (x = 0; x < destWidth; x++) {
                /* Translate origin to center of destination image. */
                tx = x - destCX;
                /* Rotate the coordinates about the origin. */
                rx = (tx * cosTheta) - (ty * sinTheta);
                ry = (tx * sinTheta) + (ty * cosTheta);
                /* Translate back to the center of the source image. */
                rx += sox;
                ry += soy;
                sx = ROUND(rx);
                sy = ROUND(ry);
                /*
                 * Verify the coordinates, since the destination image can be
                 * bigger than the source.
                 */
                if ((sx >= srcWidth) || (sx < 0) || (sy >= srcHeight) || (sy < 0)) {
                    continue;
                }
                pixel = XGetPixel(src, sx, sy);
                if (pixel) {
                    XPutPixel(dest, x, y, pixel);
                }
            }
        }
    }
    /* Write the rotated image into the destination bitmap. */
    XPutImage(display, destBitmap, bitmapGC, dest, 0, 0, 0, 0, destWidth, destHeight);
    /* Clean up the temporary resources used. */
    XDestroyImage(src);
    XDestroyImage(dest);
    *destWidthPtr = destWidth;
    *destHeightPtr = destHeight;
    return destBitmap;
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_ScaleBitmap --
 *
 *    Creates a new scaled bitmap from another bitmap. The new bitmap
 *    is bounded by a specified region. Only this portion of the bitmap
 *    is scaled from the original bitmap.
 *
 *    By bounding scaling to a region we can generate a new bitmap
 *    which is no bigger than the specified viewport.
 *
 * Parameters:
 *      Tk_Window tkwin
 *      Pixmap srcBitmap
 *      int srcWidth
 *      int srcHeight
 *      int destWidth
 *      int destHeight
 *
 * Results:
 *    The new scaled bitmap is returned.
 *
 * Side Effects:
 *    A new pixmap is allocated. The caller must release this.
 *
 * -----------------------------------------------------------------------
 */
Pixmap Rbc_ScaleBitmap(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight, int destWidth, int destHeight) {
    Display *display;
    GC bitmapGC;
    Pixmap destBitmap;
    Window root;
    XImage *src, *dest;
    double xScale, yScale;
    register int sx, sy; /* Source bitmap coordinates */
    register int x, y;   /* Destination bitmap coordinates */
    unsigned long pixel;

    if ((srcWidth <= 0) || (srcHeight <= 0) || (destWidth <= 0) || (destHeight <= 0)) {
        return None;
    }
    /* Create a new bitmap the size of the region and clear it */
    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    destBitmap = Tk_GetPixmap(display, root, destWidth, destHeight, 1);
    bitmapGC = Rbc_GetBitmapGC(tkwin);
    XSetForeground(display, bitmapGC, 0x0);
    XFillRectangle(display, destBitmap, bitmapGC, 0, 0, destWidth, destHeight);
    src = XGetImage(display, srcBitmap, 0, 0, srcWidth, srcHeight, 1, ZPixmap);
    if (src == NULL) {
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    dest = XGetImage(display, destBitmap, 0, 0, destWidth, destHeight, 1, ZPixmap);
    if (dest == NULL) {
        XDestroyImage(src);
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    /*
     * Scale each pixel of destination image from results of source
     * image. Verify the coordinates, since the destination image can
     * be bigger than the source
     */
    xScale = (double)srcWidth / (double)destWidth;
    yScale = (double)srcHeight / (double)destHeight;
    /* Map each pixel in the destination image back to the source. */
    for (y = 0; y < destHeight; y++) {
        sy = (int)(yScale * (double)y);
        for (x = 0; x < destWidth; x++) {
            sx = (int)(xScale * (double)x);
            pixel = XGetPixel(src, sx, sy);
            if (pixel) {
                XPutPixel(dest, x, y, pixel);
            }
        }
    }
    /* Write the scaled image into the destination bitmap */
    XPutImage(display, destBitmap, bitmapGC, dest, 0, 0, 0, 0, destWidth, destHeight);
    XDestroyImage(src);
    XDestroyImage(dest);
    return destBitmap;
}

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_RotateScaleBitmapRegion --
 *
 *    Creates a scaled and rotated bitmap from a given bitmap.  The
 *    caller also provides (offsets and dimensions) the region of
 *    interest in the destination bitmap.  This saves having to
 *    process the entire destination bitmap is only part of it is
 *    showing in the viewport.
 *
 *    This uses a simple rotation/scaling of each pixel in the
 *    destination image.  For each pixel, the corresponding
 *    pixel in the source bitmap is used.  This means that
 *    destination coordinates are first scaled to the size of
 *    the rotated source bitmap.  These coordinates are then
 *    rotated back to their original orientation in the source.
 *
 * Parameters:
 *      Tk_Window tkwin
 *      Pixmap srcBitmap - Source bitmap.
 *      int srcWidth - -
 *      int srcHeight - Size of source bitmap
 *      int regionX - -
 *      int regionY - - Offset of region in virtual destination bitmap.
 *      int regionWidth - -
 *      int regionHeight - Desire size of bitmap region.
 *      int virtWidth - -
 *      int virtHeight - Virtual size of destination bitmap.
 *      double theta - Angle to rotate bitmap.
 *
 * Results:
 *    The new rotated and scaled bitmap is returned.
 *
 * Side Effects:
 *    A new pixmap is allocated. The caller must release this.
 *
 * -----------------------------------------------------------------------
 */
Pixmap Rbc_ScaleRotateBitmapRegion(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight, int regionX,
                                   int regionY, int regionWidth, int regionHeight, int virtWidth, int virtHeight,
                                   double theta) {
    Display *display;
    Window root;
    Pixmap destBitmap;
    XImage *src, *dest;
    int x, y;
    int sx, sy;
    unsigned long pixel;
    double xScale, yScale;
    double rotWidth, rotHeight;
    GC bitmapGC;
    
    if ((srcWidth <= 0) || (srcHeight <= 0) || (regionWidth <= 0) || (regionHeight <= 0) || (virtWidth <= 0) ||
        (virtHeight <= 0) || (!FINITE(theta))) {
        return None;
    }
    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    /* Create a bitmap and image big enough to contain the rotated text */
    bitmapGC = Rbc_GetBitmapGC(tkwin);
    destBitmap = Tk_GetPixmap(display, root, regionWidth, regionHeight, 1);
    XSetForeground(display, bitmapGC, 0x0);
    XFillRectangle(display, destBitmap, bitmapGC, 0, 0, regionWidth, regionHeight);
    src = XGetImage(display, srcBitmap, 0, 0, srcWidth, srcHeight, 1, ZPixmap);
    if (src == NULL) {
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    dest = XGetImage(display, destBitmap, 0, 0, regionWidth, regionHeight, 1, ZPixmap);
    if (dest == NULL) {
        XDestroyImage(src);
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    theta = FMOD(theta, 360.0);
    if (theta < 0.0) {
        theta += 360.0;
    }
    Rbc_GetBoundingBox(srcWidth, srcHeight, theta, &rotWidth, &rotHeight, (Point2D *)NULL);
    xScale = rotWidth / (double)virtWidth;
    yScale = rotHeight / (double)virtHeight;
    if (FMOD(theta, (double)90.0) == 0.0) {
        int quadrant;

        /* Handle right-angle rotations specifically */
        quadrant = (int)(theta / 90.0);
        switch (quadrant) {
        case ROTATE_270: /* 270 degrees */
            for (y = 0; y < regionHeight; y++) {
                sx = (int)(yScale * (double)(y + regionY));
                for (x = 0; x < regionWidth; x++) {
                    sy = (int)(xScale * ((double)virtWidth - ((double)x + (double)regionX) - 1.0));
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;
        case ROTATE_180: /* 180 degrees */
            for (y = 0; y < regionHeight; y++) {
                sy = (int)(yScale * ((double)virtHeight - ((double)y + (double)regionY) - 1.0));
                for (x = 0; x < regionWidth; x++) {
                    sx = (int)(xScale * ((double)virtWidth - ((double)x + (double)regionX) - 1.0));
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;
        case ROTATE_90: /* 90 degrees */
            for (y = 0; y < regionHeight; y++) {
                sx = (int)(yScale * ((double)virtHeight - ((double)y + (double)regionY) - 1.0));
                for (x = 0; x < regionWidth; x++) {
                    sy = (int)(xScale * (double)(x + regionX));
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;
        case ROTATE_0: /* 0 degrees */
            for (y = 0; y < regionHeight; y++) {
                sy = (int)(yScale * ((double)y + (double)regionY));
                for (x = 0; x < regionWidth; x++) {
                    sx = (int)(xScale * ((double)x + (double)regionX));
                    pixel = XGetPixel(src, sx, sy);
                    if (pixel) {
                        XPutPixel(dest, x, y, pixel);
                    }
                }
            }
            break;
        default:
            /* The calling routine should never let this happen. */
            break;
        }
    } else {
        double radians, sinTheta, cosTheta;
        double sox, soy; /* Offset from the center of the
                          * source rectangle. */
        double rox, roy; /* Offset to the center of the
                          * rotated rectangle. */
        double tx, ty;   /* Translated coordinates from center */
        double rx, ry;   /* Angle of rotation for x and y coordinates */

        radians = (theta / 180.0) * M_PI;
        sinTheta = sin(radians), cosTheta = cos(radians);
        /*
         * Coordinates of the centers of the source and destination rectangles
         */
        sox = srcWidth * 0.5;
        soy = srcHeight * 0.5;
        rox = rotWidth * 0.5;
        roy = rotHeight * 0.5;
        /* For each pixel of the destination image, transform back to the
         * associated pixel in the source image. */
        for (y = 0; y < regionHeight; y++) {
            ty = (yScale * ((double)y + (double)regionY)) - roy;
            for (x = 0; x < regionWidth; x++) {
                /* Translate origin to center of destination image. */
                tx = (xScale * ((double)x + (double)regionX)) - rox;
                /* Rotate the coordinates about the origin. */
                rx = (tx * cosTheta) - (ty * sinTheta);
                ry = (tx * sinTheta) + (ty * cosTheta);
                /* Translate back to the center of the source image. */
                rx += sox;
                ry += soy;
                sx = ROUND(rx);
                sy = ROUND(ry);
                /*
                 * Verify the coordinates, since the destination image can be
                 * bigger than the source.
                 */
                if ((sx >= srcWidth) || (sx < 0) || (sy >= srcHeight) || (sy < 0)) {
                    continue;
                }
                pixel = XGetPixel(src, sx, sy);
                if (pixel) {
                    XPutPixel(dest, x, y, pixel);
                }
            }
        }
    }
    /* Write the rotated image into the destination bitmap. */
    XPutImage(display, destBitmap, bitmapGC, dest, 0, 0, 0, 0, regionWidth, regionHeight);
    /* Clean up the temporary resources used. */
    XDestroyImage(src);
    XDestroyImage(dest);
    return destBitmap;
}

#if HAVE_JPEGLIB_H

#undef HAVE_STDLIB_H
#undef EXTERN
#ifdef WIN32
#define XMD_H 1
#endif
#include "jpeglib.h"
#include <setjmp.h>

typedef struct {
    struct jpeg_error_mgr pub; /* "public" fields */
    jmp_buf jmpBuf;
    Tcl_DString dString;
} ReaderHandler;

static void ErrorProc(j_common_ptr jpegInfo);
static void MessageProc(j_common_ptr jpegInfo);

/*
 * Here's the routine that will replace the standard error_exit method:
 */

static void ErrorProc(j_common_ptr jpgPtr) {
    ReaderHandler *handlerPtr = (ReaderHandler *)jpgPtr->err;
    (*handlerPtr->pub.output_message)(jpgPtr);
    longjmp(handlerPtr->jmpBuf, 1);
}

static void MessageProc(j_common_ptr jpgPtr) {
    ReaderHandler *handlerPtr = (ReaderHandler *)jpgPtr->err;
    char buffer[JMSG_LENGTH_MAX];

    /* Create the message and append it into the dynamic string. */
    (*handlerPtr->pub.format_message)(jpgPtr, buffer);
    Tcl_DStringAppend(&(handlerPtr->dString), " ", -1);
    Tcl_DStringAppend(&(handlerPtr->dString), buffer, -1);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_JPEGToColorImage --
 *
 *      Reads a JPEG file and converts it into a color image.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      char *fileName
 *
 * Results:
 *      The color image is returned.  If an error occured, such
 *    as the designated file could not be opened, NULL is returned.
 *
 *----------------------------------------------------------------------
 */
Rbc_ColorImage Rbc_JPEGToColorImage(Tcl_Interp *interp, char *fileName) {
    struct jpeg_decompress_struct jpg;
    Rbc_ColorImage image;
    unsigned int imageWidth, imageHeight;
    register Pix32 *destPtr;
    ReaderHandler handler;
    FILE *f;
    JSAMPLE **readBuffer;
    int row_stride;
    register int i;
    register JSAMPLE *bufPtr;

    f = fopen(fileName, "rb");
    if (f == NULL) {
        Rbc_AppendResultStrings(interp, "can't open \"", fileName, "\":", Tcl_PosixError(interp), (char *)NULL);
        return NULL;
    }
    image = NULL;
    /* Step 1: allocate and initialize JPEG decompression object */
    /* We set up the normal JPEG error routines, then override error_exit. */
    jpg.dct_method = JDCT_IFAST;
    jpg.err = jpeg_std_error(&handler.pub);
    handler.pub.error_exit = ErrorProc;
    handler.pub.output_message = MessageProc;
    Tcl_DStringInit(&handler.dString);
    Tcl_DStringAppend(&handler.dString, "error reading \"", -1);
    Tcl_DStringAppend(&handler.dString, fileName, -1);
    Tcl_DStringAppend(&handler.dString, "\": ", -1);
    if (setjmp(handler.jmpBuf)) {
        jpeg_destroy_decompress(&jpg);
        fclose(f);
        Tcl_DStringResult(interp, &(handler.dString));
        return NULL;
    }
    jpeg_create_decompress(&jpg);
    jpeg_stdio_src(&jpg, f);
    jpeg_read_header(&jpg, TRUE); /* Step 3: read file parameters */
    jpeg_start_decompress(&jpg); /* Step 5: Start decompressor */
    imageWidth = jpg.output_width;
    imageHeight = jpg.output_height;
    if ((imageWidth < 1) || (imageHeight < 1)) {
        Rbc_AppendResultStrings(interp, "bad JPEG image size", (char *)NULL);
        fclose(f);
        return NULL;
    }
    /* JSAMPLEs per row in output buffer */
    row_stride = imageWidth * jpg.output_components;
    /* Make a one-row-high sample array that will go away when done
     * with image */
    readBuffer = (*jpg.mem->alloc_sarray)((j_common_ptr)&jpg, JPOOL_IMAGE, row_stride, 1);
    image = Rbc_CreateColorImage(imageWidth, imageHeight);
    destPtr = Rbc_ColorImageBits(image);
    if (jpg.output_components == 1) {
        while (jpg.output_scanline < imageHeight) {
            jpeg_read_scanlines(&jpg, readBuffer, 1);
            bufPtr = readBuffer[0];
            for (i = 0; i < (int)imageWidth; i++) {
                destPtr->rgba.red = destPtr->rgba.green = destPtr->rgba.blue = *bufPtr++;
                destPtr->rgba.alpha = (unsigned char)-1;
                destPtr++;
            }
        }
    } else {
        while (jpg.output_scanline < imageHeight) {
            jpeg_read_scanlines(&jpg, readBuffer, 1);
            bufPtr = readBuffer[0];
            for (i = 0; i < (int)imageWidth; i++) {
                destPtr->rgba.red = *bufPtr++;
                destPtr->rgba.green = *bufPtr++;
                destPtr->rgba.blue = *bufPtr++;
                destPtr->rgba.alpha = (unsigned char)-1;
                destPtr++;
            }
        }
    }
    jpeg_finish_decompress(&jpg); /* We can ignore the return value
                                   * since suspension is not
                                   * possible with the stdio data
                                   * source.  */
    jpeg_destroy_decompress(&jpg);
    /*
     * After finish_decompress, we can close the input file.  Here we
     * postpone it until after no more JPEG errors are possible, so as
     * to simplify the setjmp error logic above.  (Actually, I don't
     * think that jpeg_destroy can do an error exit, but why assume
     * anything...)
     */
    fclose(f);
    /*
     * At this point you may want to check to see whether any corrupt-data
     * warnings occurred (test whether jerr.pub.num_warnings is nonzero).
     */
    if (handler.pub.num_warnings > 0) {
        Tcl_SetErrorCode(interp, "IMAGE", "JPEG", Tcl_DStringValue(&(handler.dString)), (char *)NULL);
    } else {
        Tcl_SetErrorCode(interp, "NONE", (char *)NULL);
    }
    /*
     * We're ready to call the Tk_Photo routines. They'll take the RGB
     * array we've processed to build the Tk image of the JPEG.
     */
    Tcl_DStringFree(&(handler.dString));
    return image;
}

#endif /* HAVE_JPEGLIB_H */
