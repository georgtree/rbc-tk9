/**
 * @file rbcWinImage.c
 * @brief Windows drawable capture, transparency masks and monochrome transforms.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup windows_bitmap_images Windows bitmap and capture services
 * Platform implementation, extracted separately from shared image declarations.
 */
#include "rbcInt.h"
#include "rbcImage.h"
#include <stdint.h>
#include <X11/Xutil.h>

#define CLAMP(c) ((((c) < 0.0) ? 0.0 : ((c) > 255.0) ? 255.0 : (c)))

#define GetBit(x, y)                                                                                                   \
    (srcBits[((size_t)srcBytesPerRow * (size_t)(srcHeight - (y) - 1)) + ((size_t)(x) >> 3)] & (0x80 >> ((x) & 7)))

#define SetBit(x, y)                                                                                                   \
    (destBits[((size_t)destBytesPerRow * (size_t)(destHeight - (y) - 1)) + ((size_t)(x) >> 3)] |= (0x80 >> ((x) & 7)))

/**
 * @brief Calculate a DWORD-aligned monochrome row stride.
 * @ingroup windows_bitmap_images
 *
 * Returns zero for nonpositive width or width too large for the alignment
 * addition. Otherwise returns ((width + 31) & ~31) / 8 bytes. Height and total
 * allocation-size validation remain with the caller.
 *
 * @param[in] width Requested monochrome width in pixels; nonpositive or alignment-overflowing values return
 *   zero.
 */
static int GetMonoBitmapStride(int width) {
    if ((width <= 0) || (width > INT_MAX - 31)) {
        return 0;
    }
    return ((width + 31) & ~31) / 8;
}

/**
 * @brief Capture a Windows drawable rectangle into an owned color image.
 * @ingroup windows_bitmap_images
 *
 * Rejects nonpositive dimensions and nonfinite/nonpositive gamma. Borrows
 * window/target, creates a 32-bit DIB and memory DC, then copies the rectangle
 * with SRCCOPY. Converts bottom-up BGR rows using the bitmap row stride and
 * a pow(channel/255, inputGamma) lookup; output alpha is fully opaque.
 * Releases temporary bitmap/DC resources and any copied system palette.
 * Returns NULL on checked capture failures; caller frees a successful image
 * with Rbc_FreeColorImage. Not every GDI acquisition/selection is validated.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] drawable Borrowed source drawable compatible with tkwin; copied without ownership transfer.
 * @param[in] x Source rectangle left coordinate in pixels.
 * @param[in] y Source rectangle top coordinate in pixels.
 * @param[in] width Positive rectangle width in pixels; endpoint arithmetic must remain representable.
 * @param[in] height Positive rectangle height in pixels; endpoint arithmetic must remain representable.
 * @param[in] inputGamma Finite positive exponent applied to normalized RGB channels; invalid values return
 *   NULL.
 */
Rbc_ColorImage Rbc_DrawableToColorImage(Tk_Window tkwin, Drawable drawable, int x, int y, int width, int height,
                                        double inputGamma) {
    void *data;
    BITMAPINFO info;
    DIBSECTION ds;
    HBITMAP hBitmap, oldBitmap;
    HPALETTE hPalette;
    HDC memDC;
    unsigned char *srcArr;
    register unsigned char *srcPtr;
    HDC hDC;
    Rbc_WinDrawableDC *dcStatePtr;
    register Pix32 *destPtr;
    Rbc_ColorImage image;
    unsigned char lut[256];

    hBitmap = NULL;
    oldBitmap = NULL;
    memDC = NULL;
    hPalette = NULL;
    image = NULL;
    if ((width <= 0) || (height <= 0) || (!FINITE(inputGamma)) || (inputGamma <= 0.0)) {
        return NULL;
    }
    hDC = Rbc_WinAcquireDrawableDC(Tk_Display(tkwin), drawable, &dcStatePtr);
    /* Create the intermediate drawing surface at window resolution. */
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    hBitmap = CreateDIBSection(hDC, &info, DIB_RGB_COLORS, &data, NULL, 0);
    if ((hBitmap == NULL) || (data == NULL)) {
        goto done;
    }
    memDC = CreateCompatibleDC(hDC);
    if (memDC == NULL) {
        goto done;
    }
    oldBitmap = SelectBitmap(memDC, hBitmap);
    hPalette = Rbc_GetSystemPalette();
    if (hPalette != NULL) {
        SelectPalette(hDC, hPalette, FALSE);
        RealizePalette(hDC);
        SelectPalette(memDC, hPalette, FALSE);
        RealizePalette(memDC);
    }
    image = NULL;
    /* Copy the window contents to the memory surface. */
    if (!BitBlt(memDC, 0, 0, width, height, hDC, x, y, SRCCOPY)) {
        goto done;
    }
    if (GetObject(hBitmap, sizeof(DIBSECTION), &ds) == 0) {
        goto done;
    }
    srcArr = (unsigned char *)ds.dsBm.bmBits;
    image = Rbc_CreateColorImage(width, height);
    destPtr = Rbc_ColorImageBits(image);
    {
        register int i;
        double value;

        for (i = 0; i < 256; i++) {
            value = pow(i / 255.0, inputGamma) * 255.0 + 0.5;
            lut[i] = (unsigned char)CLAMP(value);
        }
    }
    /*
     * Copy the DIB RGB data into the color image. The DIB scanlines
     * are stored bottom-to-top and the order of the RGB color
     * components is BGR. Who says Win32 GDI programming isn't
     * backwards?
     */
    for (y = height - 1; y >= 0; y--) {
        srcPtr = srcArr + ((size_t)y * (size_t)ds.dsBm.bmWidthBytes);
        for (x = 0; x < width; x++) {
            destPtr->rgba.blue = lut[*srcPtr++];
            destPtr->rgba.green = lut[*srcPtr++];
            destPtr->rgba.red = lut[*srcPtr++];
            destPtr->rgba.alpha = (unsigned char)-1;
            destPtr++;
            srcPtr++;
        }
    }
    
done:
    if (memDC != NULL) {
        if (oldBitmap != NULL) {
            DeleteBitmap(SelectBitmap(memDC, oldBitmap));
        } else if (hBitmap != NULL) {
            DeleteObject(hBitmap);
        }
        DeleteDC(memDC);
    } else if (hBitmap != NULL) {
        DeleteObject(hBitmap);
    }
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    if (hPalette != NULL) {
        DeletePalette(hPalette);
    }
    return image;
}

/**
 * @brief Create a Windows one-bit transparency mask from a Tk photo block.
 * @ingroup windows_bitmap_images
 *
 * Borrows photo storage and honors pitch, pixelSize and alpha offset. Requires
 * a valid block with accessible alpha bytes. Sets bits only for exactly zero
 * alpha; partial alpha is treated as opaque. Uses aligned temporary rows and
 * the existing photo-specific row traversal before CreateBitmapIndirect.
 * Returns an owned Tk pixmap, or None for all-opaque input, invalid dimensions
 * or bitmap creation failure. Releases temporary bits; caller frees the pixmap.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] src Photo-block descriptor copied by value; pixel storage is borrowed and must expose valid pitch,
 *   pixelSize and alpha offset.
 */
Pixmap Rbc_PhotoImageMask(Tk_Window tkwin, Tk_PhotoImageBlock src) {
    Pixmap bitmap;
    size_t count = 0;
    int x, y;
    unsigned char *srcPtr;
    int destBytesPerRow;
    int destHeight;
    unsigned char *destBits;
    unsigned char *srcRowPtr;

    bitmap = None;    
    if (src.height <= 0) {
        return None;
    }
    destBytesPerRow = GetMonoBitmapStride(src.width);
    if (destBytesPerRow == 0) {
        return None;
    }
    destBits = RbcCalloc((size_t)src.height, (size_t)destBytesPerRow);
    destHeight = src.height;
    srcRowPtr = src.pixelPtr;
    /* FIXME: figure out why this is so! */
    for (y = src.height - 1; y >= 0; y--) {
        srcPtr = srcRowPtr;
        for (x = 0; x < src.width; x++) {
            if (srcPtr[src.offset[3]] == 0x00) {
                SetBit(x, y);
                count++;
            }
            srcPtr += src.pixelSize;
        }
        srcRowPtr += src.pitch;
    }
    if (count > 0) {
        HBITMAP hBitmap;
        BITMAP bm;

        bm.bmType = 0;
        bm.bmWidth = src.width;
        bm.bmHeight = src.height;
        bm.bmWidthBytes = destBytesPerRow;
        bm.bmPlanes = 1;
        bm.bmBitsPixel = 1;
        bm.bmBits = destBits;
        hBitmap = CreateBitmapIndirect(&bm);
        if (hBitmap == NULL) {
            ckfree(destBits);
            return None;
        }
        Colormap colormap;
        if (Tk_WindowId(tkwin) == None) {
            colormap = DefaultColormap(Tk_Display(tkwin), DefaultScreen(Tk_Display(tkwin)));
        } else {
            colormap = Tk_Colormap(tkwin);
        }
        bitmap = Rbc_WinCreatePixmapFromBitmap(hBitmap, 1, colormap);
    }
    if (destBits != NULL) {
        ckfree((char *)destBits);
    }
    return bitmap;
}

/**
 * @brief Create a one-bit transparency mask from RBC color pixels.
 * @ingroup windows_bitmap_images
 *
 * Borrows a valid image and sets bits only where alpha equals zero. Uses
 * aligned rows and the SetBit bottom-up addressing convention while traversing
 * source pixels in logical row order. Returns None for all-opaque input or
 * checked failure, otherwise an owned Tk pixmap to free with Tk_FreePixmap.
 * Temporary bit storage is released after wrapping the native bitmap.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] image Borrowed valid RBC color image; source pixels and alpha remain unchanged.
 */
Pixmap Rbc_ColorImageMask(Tk_Window tkwin, Rbc_ColorImage image) {
    Pixmap bitmap;
    size_t count;
    int x, y;
    Pix32 *srcPtr;
    int destBytesPerRow;
    int destWidth, destHeight;
    unsigned char *destBits;

    destWidth = Rbc_ColorImageWidth(image);
    destHeight = Rbc_ColorImageHeight(image);
    if (destHeight <= 0) {
        return None;
    }
    destBytesPerRow = GetMonoBitmapStride(destWidth);
    if (destBytesPerRow == 0) {
        return None;
    }
    destBits = RbcCalloc((size_t)destHeight, (size_t)destBytesPerRow);
    count = 0;
    srcPtr = Rbc_ColorImageBits(image);
    for (y = 0; y < destHeight; y++) {
        for (x = 0; x < destWidth; x++) {
            if (srcPtr->rgba.alpha == 0x00) {
                SetBit(x, y);
                count++;
            }
            srcPtr++;
        }
    }
    bitmap = None;
    if (count > 0) {
        HBITMAP hBitmap;
        BITMAP bm;

        bm.bmType = 0;
        bm.bmWidth = destWidth;
        bm.bmHeight = destHeight;
        bm.bmWidthBytes = destBytesPerRow;
        bm.bmPlanes = 1;
        bm.bmBitsPixel = 1;
        bm.bmBits = destBits;
        hBitmap = CreateBitmapIndirect(&bm);
        if (hBitmap != NULL) {
            Colormap colormap;

            if (Tk_WindowId(tkwin) == None) {
                colormap = DefaultColormap(Tk_Display(tkwin), DefaultScreen(Tk_Display(tkwin)));
            } else {
                colormap = Tk_Colormap(tkwin);
            }
            bitmap = Rbc_WinCreatePixmapFromBitmap(hBitmap, 1, colormap);
        }
    }
    ckfree(destBits);
    return bitmap;
}

/**
 * @brief Create a rotated Windows monochrome pixmap.
 * @ingroup windows_bitmap_images
 *
 * Borrows source/window. Rejects invalid sizes, nonfinite angle or missing
 * output pointers before initializing outputs. After initial validation,
 * outputs are zero until success. Normalizes angle to [0,360), rounds bounding
 * dimensions upward and checks stride, size_t and DWORD image-size limits.
 * Uses exact quadrant mappings or inverse mapping with ROUND and source bounds
 * checks. Frees readback bits and temporary storage; failed upload frees the
 * destination. Returns None on checked failure, otherwise owned pixmap and
 * dimensions. Caller releases the pixmap with Tk_FreePixmap.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] srcBitmap Borrowed monochrome source pixmap on the window display, readable at the supplied source
 *   dimensions.
 * @param[in] srcWidth Positive source bitmap width in pixels.
 * @param[in] srcHeight Positive source bitmap height in pixels.
 * @param[in] theta Finite rotation angle in degrees, normalized to a full turn internally.
 * @param[out] destWidthPtr Required width output, separate from destHeightPtr; zeroed after initial validation
 *   and assigned on success.
 * @param[out] destHeightPtr Required height output, separate from destWidthPtr; initial validation failure
 *   leaves it untouched.
 */
Pixmap Rbc_RotateBitmap(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight, double theta, int *destWidthPtr,
                        int *destHeightPtr) {
    Display *display;
    Window root;
    Pixmap destBitmap;
    double rotWidth, rotHeight;
    double widthValue, heightValue;
    HDC hDC;
    Rbc_WinDrawableDC *dcStatePtr;
    HBITMAP hBitmap;
    unsigned char *srcBits;
    unsigned char *destBits;
    int srcBytesPerRow;
    int destBytesPerRow;
    int destWidth, destHeight;
    int x, y;
    int sx, sy;
    int result;
    size_t imageSize;
    unsigned long pixel;

    struct MonoBitmap {
        BITMAPINFOHEADER bi;
        RGBQUAD colors[2];
    } mb;
    if ((srcWidth <= 0) || (srcHeight <= 0) || (!FINITE(theta)) || (destWidthPtr == NULL) || (destHeightPtr == NULL)) {
        return None;
    }
    *destWidthPtr = 0;
    *destHeightPtr = 0;
    display = Tk_Display(tkwin);
    root = RootWindow(display, Tk_ScreenNumber(tkwin));
    /*
     * Normalize the angle to [0,360).  This is important for the
     * right-angle switch below: FMOD(-90,360) is otherwise -90.
     */
    theta = FMOD(theta, 360.0);
    if (theta < 0.0) {
        theta += 360.0;
    }
    /*
     * Determine the dimensions needed to contain the rotated bitmap.
     */
    Rbc_GetBoundingBox(srcWidth, srcHeight, theta, &rotWidth, &rotHeight, (Point2D *)NULL);
    widthValue = ceil(rotWidth);
    heightValue = ceil(rotHeight);
    if ((!FINITE(widthValue)) || (!FINITE(heightValue)) || (widthValue < 1.0) || (heightValue < 1.0) ||
        (widthValue > (double)INT_MAX) || (heightValue > (double)INT_MAX)) {
        return None;
    }
    destWidth = (int)widthValue;
    destHeight = (int)heightValue;
    /*
     * Windows monochrome bitmap scanlines are DWORD aligned.
     *
     * Calculate:
     *
     *     ((width + 31) & ~31) / 8
     *
     * without overflowing signed int.
     */
    destBytesPerRow = GetMonoBitmapStride(destWidth);
    if (destBytesPerRow == 0) {
        return None;
    }
    if ((size_t)destHeight > SIZE_MAX / (size_t)destBytesPerRow) {
        return None;
    }
    imageSize = (size_t)destHeight * (size_t)destBytesPerRow;
    /*
     * biSizeImage is a DWORD, so this is an actual Win32 boundary.
     */
    if (imageSize > (size_t)UINT32_MAX) {
        return None;
    }
    destBitmap = Tk_GetPixmap(display, root, destWidth, destHeight, 1);
    if (destBitmap == None) {
        return None;
    }
    srcBits = Rbc_GetBitmapData(display, srcBitmap, srcWidth, srcHeight, &srcBytesPerRow);
    if (srcBits == NULL) {
        OutputDebugStringA("Rbc_GetBitmapData failed");
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    if (srcBytesPerRow <= 0) {
        ckfree(srcBits);
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    destBits = RbcCalloc((size_t)destHeight, (size_t)destBytesPerRow);
    /*
     * Windows monochrome bitmaps are bottom-to-top.  The mappings
     * below intentionally preserve the orientation of the original
     * implementation.
     */
    if (FMOD(theta, 90.0) == 0.0) {
        int quadrant;

        quadrant = (int)(theta / 90.0);
        switch (quadrant) {
        case ROTATE_270:
            for (y = 0; y < destHeight; y++) {
                sx = y;
                for (x = 0; x < destWidth; x++) {
                    sy = destWidth - x - 1;
                    if ((sx < 0) || (sx >= srcWidth) || (sy < 0) || (sy >= srcHeight)) {
                        continue;
                    }
                    pixel = GetBit(sx, sy);
                    if (pixel != 0) {
                        SetBit(x, y);
                    }
                }
            }
            break;
        case ROTATE_180:
            for (y = 0; y < destHeight; y++) {
                sy = destHeight - y - 1;
                for (x = 0; x < destWidth; x++) {
                    sx = destWidth - x - 1;
                    if ((sx < 0) || (sx >= srcWidth) || (sy < 0) || (sy >= srcHeight)) {
                        continue;
                    }
                    pixel = GetBit(sx, sy);
                    if (pixel != 0) {
                        SetBit(x, y);
                    }
                }
            }
            break;
        case ROTATE_90:
            for (y = 0; y < destHeight; y++) {
                sx = destHeight - y - 1;
                for (x = 0; x < destWidth; x++) {
                    sy = x;
                    if ((sx < 0) || (sx >= srcWidth) || (sy < 0) || (sy >= srcHeight)) {
                        continue;
                    }
                    pixel = GetBit(sx, sy);
                    if (pixel != 0) {
                        SetBit(x, y);
                    }
                }
            }
            break;
        case ROTATE_0:
            for (y = 0; y < destHeight; y++) {
                for (x = 0; x < destWidth; x++) {
                    if ((x >= srcWidth) || (y >= srcHeight)) {
                        continue;
                    }
                    pixel = GetBit(x, y);
                    if (pixel != 0) {
                        SetBit(x, y);
                    }
                }
            }
            break;
        default:
            /*
             * theta is normalized to [0,360), so an exact multiple
             * of 90 must be one of the four cases above.
             */
            break;
        }
    } else {
        double radians;
        double sinTheta, cosTheta;
        double srcCX, srcCY;
        double destCX, destCY;
        double tx, ty;
        double rx, ry;

        radians = (theta / 180.0) * M_PI;
        sinTheta = sin(radians);
        cosTheta = cos(radians);
        srcCX = (double)srcWidth * 0.5;
        srcCY = (double)srcHeight * 0.5;
        destCX = (double)destWidth * 0.5;
        destCY = (double)destHeight * 0.5;
        for (y = 0; y < destHeight; y++) {
            ty = (double)y - destCY;
            for (x = 0; x < destWidth; x++) {
                tx = (double)x - destCX;
                /*
                 * Inverse-map the destination coordinate into the
                 * unrotated source bitmap.
                 */
                rx = (tx * cosTheta) - (ty * sinTheta);
                ry = (tx * sinTheta) + (ty * cosTheta);
                rx += srcCX;
                ry += srcCY;
                sx = ROUND(rx);
                sy = ROUND(ry);
                if ((sx < 0) || (sx >= srcWidth) || (sy < 0) || (sy >= srcHeight)) {
                    continue;
                }
                pixel = GetBit(sx, sy);
                if (pixel != 0) {
                    SetBit(x, y);
                }
            }
        }
    }

    /*
     * Transfer the rotated bit array into the Windows bitmap backing
     * the Tk pixmap.
     */
    hBitmap = Rbc_WinGetPixmapHandle(destBitmap);
    ZeroMemory(&mb, sizeof(mb));
    mb.bi.biSize = sizeof(BITMAPINFOHEADER);
    mb.bi.biPlanes = 1;
    mb.bi.biBitCount = 1;
    mb.bi.biCompression = BI_RGB;
    mb.bi.biWidth = destWidth;
    mb.bi.biHeight = destHeight;
    mb.bi.biSizeImage = (DWORD)imageSize;
    mb.colors[0].rgbBlue = 0x00;
    mb.colors[0].rgbGreen = 0x00;
    mb.colors[0].rgbRed = 0x00;
    mb.colors[1].rgbBlue = 0xFF;
    mb.colors[1].rgbGreen = 0xFF;
    mb.colors[1].rgbRed = 0xFF;
    hDC = Rbc_WinAcquireDrawableDC(display, destBitmap, &dcStatePtr);
    result = SetDIBits(hDC, hBitmap, 0, (UINT)destHeight, (const VOID *)destBits, (BITMAPINFO *)&mb, DIB_RGB_COLORS);
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    ckfree(destBits);
    ckfree(srcBits);
    if (result == 0) {
#if WINDEBUG
        PurifyPrintf("can't SetDIBits: %s\n", Rbc_LastError());
#endif
        /*
         * The original code merely replaced destBitmap with None,
         * leaking the pixmap.  Release it first.
         */
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    *destWidthPtr = destWidth;
    *destHeightPtr = destHeight;
    return destBitmap;
}

/**
 * @brief Scale a monochrome bitmap through Windows StretchBlt.
 * @ingroup windows_bitmap_images
 *
 * Borrows source/window and allocates an owned destination pixmap. Acquires
 * source and destination DC records and releases both after SRCCOPY scaling.
 * Returns None if pixmap allocation fails. This path does not validate sizes
 * or test DC acquisition and StretchBlt success separately; callers must
 * supply valid dimensions. A returned pixmap is not proof of successful copy.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] srcBitmap Borrowed monochrome source pixmap on the window display, readable at the supplied source
 *   dimensions.
 * @param[in] srcWidth Positive source bitmap width in pixels.
 * @param[in] srcHeight Positive source bitmap height in pixels.
 * @param[in] destWidth Positive destination bitmap width in pixels; caller validates native allocation limits.
 * @param[in] destHeight Positive destination bitmap height in pixels; caller validates native allocation
 *   limits.
 */
Pixmap Rbc_ScaleBitmap(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight, int destWidth, int destHeight) {
    Rbc_WinDrawableDC *srcStatePtr;
    Rbc_WinDrawableDC *destStatePtr;
    HDC src, dest;
    Pixmap destBitmap;
    Window root;
    Display *display;

    /* Create a new bitmap the size of the region and clear it */
    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    destBitmap = Tk_GetPixmap(display, root, destWidth, destHeight, 1);
    if (destBitmap == None) {
        return None;
    }
    src = Rbc_WinAcquireDrawableDC(display, srcBitmap, &srcStatePtr);
    dest = Rbc_WinAcquireDrawableDC(display, destBitmap, &destStatePtr);
    StretchBlt(dest, 0, 0, destWidth, destHeight, src, 0, 0, srcWidth, srcHeight, SRCCOPY);
    Rbc_WinReleaseDrawableDC(srcStatePtr);
    Rbc_WinReleaseDrawableDC(destStatePtr);
    return destBitmap;
}

/**
 * @brief Construct only a region of a virtual scaled/rotated bitmap.
 * @ingroup windows_bitmap_images
 *
 * Validates positive source, region and virtual sizes plus finite angle.
 * Borrows source/window; owns destination pixmap and readback/output buffers.
 * Normalizes rotation and inverse-maps region coordinates through virtual
 * scaling into source bits. Exact quadrant paths assume an in-bounds region
 * and directly sample; arbitrary angles bounds-check source coordinates.
 * Callers must keep region offsets and coordinate arithmetic representable
 * and within the virtual image. Checks stride/storage limits and frees the
 * destination on checked readback/upload failure. Caller owns successful pixmap.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] srcBitmap Borrowed monochrome source pixmap on the window display, readable at the supplied source
 *   dimensions.
 * @param[in] srcWidth Positive source bitmap width in pixels.
 * @param[in] srcHeight Positive source bitmap height in pixels.
 * @param[in] regionX Left offset inside the virtual scaled/rotated image; the complete region must fit and
 *   arithmetic must be representable.
 * @param[in] regionY Top offset inside the virtual scaled/rotated image; the complete region must fit and
 *   arithmetic must be representable.
 * @param[in] regionWidth Positive width of the requested destination region in pixels.
 * @param[in] regionHeight Positive height of the requested destination region in pixels.
 * @param[in] virtWidth Positive width of the complete virtual image, not the allocated region.
 * @param[in] virtHeight Positive height of the complete virtual image, not the allocated region.
 * @param[in] theta Finite rotation angle in degrees, normalized to a full turn internally.
 */
Pixmap Rbc_ScaleRotateBitmapRegion(Tk_Window tkwin, Pixmap srcBitmap, int srcWidth, int srcHeight,
                                   int regionX, int regionY, int regionWidth, int regionHeight,
                                   int virtWidth, int virtHeight, double theta) {
    Display *display; /* X display */
    HBITMAP hBitmap;
    HDC hDC;
    Pixmap destBitmap;
    Rbc_WinDrawableDC *dcStatePtr;
    Window root; /* Root window drawable */
    double rotWidth, rotHeight;
    double xScale, yScale;
    int srcBytesPerRow, destBytesPerRow;
    int destHeight;
    int result;
    register int sx, sy; /* Source bitmap coordinates */
    register int x, y;   /* Destination bitmap coordinates */
    unsigned char *srcBits, *destBits;
    unsigned long pixel;
    size_t imageSize;
    struct MonoBitmap {
        BITMAPINFOHEADER bi;
        RGBQUAD colors[2];
    } mb;

    if ((srcWidth <= 0) || (srcHeight <= 0) || (regionWidth <= 0) || (regionHeight <= 0) || (virtWidth <= 0) ||
        (virtHeight <= 0) || (!FINITE(theta))) {
        return None;
    }
    display = Tk_Display(tkwin);
    root = RootWindow(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
    /* Create a bitmap and image big enough to contain the rotated text */
    destBitmap = Tk_GetPixmap(display, root, regionWidth, regionHeight, 1);
    if (destBitmap == None) {
        return None; /* Can't allocate pixmap. */
    }
    srcBits = Rbc_GetBitmapData(display, srcBitmap, srcWidth, srcHeight, &srcBytesPerRow);
    if (srcBits == NULL) {
        OutputDebugStringA("Rbc_GetBitmapData failed");
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    destBytesPerRow = GetMonoBitmapStride(regionWidth);
    if (destBytesPerRow == 0) {
        ckfree(srcBits);
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    if ((size_t)regionHeight > SIZE_MAX / (size_t)destBytesPerRow) {
        ckfree(srcBits);
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    imageSize = (size_t)regionHeight * (size_t)destBytesPerRow;
    if (imageSize > (size_t)UINT32_MAX) {
        ckfree(srcBits);
        Tk_FreePixmap(display, destBitmap);
        return None;
    }
    destBits = RbcCalloc((size_t)regionHeight, (size_t)destBytesPerRow);
    destHeight = regionHeight;
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
            for (y = 0; y < (int)regionHeight; y++) {
                sx = (int)(yScale * (double)(y + regionY));
                for (x = 0; x < (int)regionWidth; x++) {
                    sy = (int)(xScale * (double)(virtWidth - (x + regionX) - 1));
                    pixel = GetBit(sx, sy);
                    if (pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;
        case ROTATE_180: /* 180 degrees */
            for (y = 0; y < (int)regionHeight; y++) {
                sy = (int)(yScale * (double)(virtHeight - (y + regionY) - 1));
                for (x = 0; x < (int)regionWidth; x++) {
                    sx = (int)(xScale * (double)(virtWidth - (x + regionX) - 1));
                    pixel = GetBit(sx, sy);
                    if (pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;
        case ROTATE_90: /* 90 degrees */
            for (y = 0; y < (int)regionHeight; y++) {
                sx = (int)(yScale * (double)(virtHeight - (y + regionY) - 1));
                for (x = 0; x < (int)regionWidth; x++) {
                    sy = (int)(xScale * (double)(x + regionX));
                    pixel = GetBit(sx, sy);
                    if (pixel) {
                        SetBit(x, y);
                    }
                }
            }
            break;
        case ROTATE_0: /* 0 degrees */
            for (y = 0; y < (int)regionHeight; y++) {
                sy = (int)(yScale * (double)(y + regionY));
                for (x = 0; x < (int)regionWidth; x++) {
                    sx = (int)(xScale * (double)(x + regionX));
                    pixel = GetBit(sx, sy);
                    if (pixel) {
                        SetBit(x, y);
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
        double scx, scy; /* Offset from the center of the
                          * source rectangle. */
        double rcx, rcy; /* Offset to the center of the
                          * rotated rectangle. */
        double tx, ty;   /* Translated coordinates from center */
        double rx, ry;   /* Angle of rotation for x and y coordinates */

        radians = (theta / 180.0) * M_PI;
        sinTheta = sin(radians), cosTheta = cos(radians);
        /*
         * Coordinates of the centers of the source and destination rectangles
         */
        scx = srcWidth * 0.5;
        scy = srcHeight * 0.5;
        rcx = rotWidth * 0.5;
        rcy = rotHeight * 0.5;
        /* For each pixel of the destination image, transform back to the
         * associated pixel in the source image. */
        for (y = 0; y < (int)regionHeight; y++) {
            ty = (yScale * (double)(y + regionY)) - rcy;
            for (x = 0; x < (int)regionWidth; x++) {
                /* Translate origin to center of destination image. */
                tx = (xScale * (double)(x + regionX)) - rcx;
                /* Rotate the coordinates about the origin. */
                rx = (tx * cosTheta) - (ty * sinTheta);
                ry = (tx * sinTheta) + (ty * cosTheta);
                /* Translate back to the center of the source image. */
                rx += scx;
                ry += scy;
                sx = ROUND(rx);
                sy = ROUND(ry);
                /*
                 * Verify the coordinates, since the destination image can be
                 * bigger than the source.
                 */
                if ((sx >= (int)srcWidth) || (sx < 0) || (sy >= (int)srcHeight) || (sy < 0)) {
                    continue;
                }
                pixel = GetBit(sx, sy);
                if (pixel) {
                    SetBit(x, y);
                }
            }
        }
    }
    /* Write the rotated image into the destination bitmap. */
    hBitmap = Rbc_WinGetPixmapHandle(destBitmap);
    ZeroMemory(&mb, sizeof(mb));
    mb.bi.biSize = sizeof(BITMAPINFOHEADER);
    mb.bi.biPlanes = 1;
    mb.bi.biBitCount = 1;
    mb.bi.biCompression = BI_RGB;
    mb.bi.biWidth = regionWidth;
    mb.bi.biHeight = regionHeight;
    mb.bi.biSizeImage = (DWORD)imageSize;
    mb.colors[0].rgbBlue = mb.colors[0].rgbRed = mb.colors[0].rgbGreen = 0x0;
    mb.colors[1].rgbBlue = mb.colors[1].rgbRed = mb.colors[1].rgbGreen = 0xFF;
    hDC = Rbc_WinAcquireDrawableDC(display, destBitmap, &dcStatePtr);
    result = SetDIBits(hDC, hBitmap, 0, regionHeight, (LPVOID)destBits, (BITMAPINFO *)&mb, DIB_RGB_COLORS);
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    if (!result) {
#if WINDEBUG
        PurifyPrintf("can't setDIBits: %s\n", Rbc_LastError());
#endif
        Tk_FreePixmap(display, destBitmap);
        destBitmap = None;
    }
    if (destBits != NULL) {
        ckfree((char *)destBits);
    }
    if (srcBits != NULL) {
        ckfree((char *)srcBits);
    }
    return destBitmap;
}

#ifdef HAVE_IJL_H

#include <ijl.h>

/*
 *--------------------------------------------------------------
 *
 * Rbc_JPEGToColorImage --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      char *fileName
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Rbc_ColorImage Rbc_JPEGToColorImage(Tcl_Interp *interp, const char *fileName) {
    JPEG_CORE_PROPERTIES jpgProps;
    Rbc_ColorImage image = NULL;

    ZeroMemory(&jpgProps, sizeof(JPEG_CORE_PROPERTIES));
    if (ijlInit(&jpgProps) != IJL_OK) {
        Rbc_AppendResultStrings(interp, "can't initialize Intel JPEG library", (char *)NULL);
        return NULL;
    }
    jpgProps.JPGFile = (char *)fileName;
    if (ijlRead(&jpgProps, IJL_JFILE_READPARAMS) != IJL_OK) {
        Rbc_AppendResultStrings(interp, "can't read JPEG file header from \"", fileName, "\" file.", (char *)NULL);
        goto error;
    }
    if ((jpgProps.JPGWidth <= 0) || (jpgProps.JPGHeight <= 0)) {
        Rbc_AppendResultStrings(interp, "bad JPEG image size", (char *)NULL);
        goto error;
    }
    // !dudnik: to fix bug case 584680, [OT:287A305B]
    // Set the JPG color space ... this will always be
    // somewhat of an educated guess at best because JPEG
    // is "color blind" (i.e., nothing in the bit stream
    // tells you what color space the data was encoded from).
    // However, in this example we assume that we are
    // reading JFIF files which means that 3 channel images
    // are in the YCbCr color space and 1 channel images are
    // in the Y color space.
    switch (jpgProps.JPGChannels) {
    case 1:
        jpgProps.JPGColor = IJL_G;
        jpgProps.DIBChannels = 4;
        jpgProps.DIBColor = IJL_RGBA_FPX;
        break;
    case 3:
        jpgProps.JPGColor = IJL_YCBCR;
        jpgProps.DIBChannels = 4;
        jpgProps.DIBColor = IJL_RGBA_FPX;
        break;
    case 4:
        jpgProps.JPGColor = IJL_YCBCRA_FPX;
        jpgProps.DIBChannels = 4;
        jpgProps.DIBColor = IJL_RGBA_FPX;
        break;
    default:
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unsupported JPEG channel count %d", jpgProps.JPGChannels));
        goto error;
    }
    jpgProps.DIBWidth = jpgProps.JPGWidth;
    jpgProps.DIBHeight = jpgProps.JPGHeight;
    jpgProps.DIBPadBytes = IJL_DIB_PAD_BYTES(jpgProps.DIBWidth, jpgProps.DIBChannels);
    image = Rbc_CreateColorImage(jpgProps.JPGWidth, jpgProps.JPGHeight);
    jpgProps.DIBBytes = (BYTE *)Rbc_ColorImageBits(image);
    if (ijlRead(&jpgProps, IJL_JFILE_READWHOLEIMAGE) != IJL_OK) {
        Rbc_AppendResultStrings(interp, "can't read image data from \"", fileName, "\"", (char *)NULL);
        goto error;
    }
    if (ijlFree(&jpgProps) != IJL_OK) {
        fprintf(stderr, "can't free Intel(R) JPEG library\n");
    }
    return image;

error:
    ijlFree(&jpgProps);
    if (image != NULL) {
        Rbc_FreeColorImage(image);
    }
    ijlFree(&jpgProps);
    return NULL;
}

#else

#ifdef HAVE_JPEGLIB_H

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
 *--------------------------------------------------------------
 *
 * ErrorProc --
 *
 *      Here's the routine that will replace the standard
 *      error_exit method
 *
 * Parameters:
 *      j_common_ptr jpgPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void ErrorProc(j_common_ptr jpgPtr) {
    ReaderHandler *handlerPtr = (ReaderHandler *)jpgPtr->err;
    (*handlerPtr->pub.output_message)(jpgPtr);
    longjmp(handlerPtr->jmpBuf, 1);
}

/*
 *--------------------------------------------------------------
 *
 * MessageProc --
 *
 *      TODO: Description
 *
 * Parameters:
 *      j_common_ptr jpgPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
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
 *      as the designated file could not be opened, NULL is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Rbc_ColorImage Rbc_JPEGToColorImage(Tcl_Interp *interp, const char *fileName) {
    struct jpeg_decompress_struct jpg;
    Rbc_ColorImage image;
    unsigned int imageWidth, imageHeight;
    register Pix32 *destPtr;
    ReaderHandler handler;
    FILE *f;
    JSAMPLE **readBuffer;
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
    if ((jpg.output_width < 1) || (jpg.output_height < 1) || (jpg.output_width > (JDIMENSION)INT_MAX) ||
        (jpg.output_height > (JDIMENSION)INT_MAX)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("bad JPEG image size", -1));
        jpeg_destroy_decompress(&jpg);
        fclose(f);
        return NULL;
    }
    imageWidth = jpg.output_width;
    imageHeight = jpg.output_height;
    if ((imageWidth < 1) || (imageHeight < 1)) {
        Rbc_AppendResultStrings(interp, "bad JPEG image size", (char *)NULL);
        fclose(f);
        return NULL;
    }
    /* JSAMPLEs per row in output buffer */
    if ((jpg.output_components == 0) || (jpg.output_width > (JDIMENSION_MAX / jpg.output_components))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("JPEG row is too large", -1));
        jpeg_destroy_decompress(&jpg);
        fclose(f);
        return NULL;
    }
    JDIMENSION rowStride;
    rowStride = jpg.output_width * jpg.output_components;
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
            for (JDIMENSION i = 0; i < imageWidth; i++) {
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
#endif /* HAVE_IJL_H */
