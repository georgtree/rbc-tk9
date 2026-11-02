/*
 * rbcWinDraw.c --
 *
 *      This module contains WIN32 routines not included in the Tcl/Tk
 *      libraries.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @file rbcWinDraw.c
 * Private Tk Windows compatibility services used below the native renderer.
 */
/** @defgroup windows_drawing_resources Windows drawing resource helpers
 * Drawable wrappers, acquisition records, raster fast paths and GC conversion.
 */
/** @defgroup windows_drawing_primitives Windows compatibility primitives
 * Xlib-shaped drawing entry points implemented with native Windows operations.
 */
#include "rbcInt.h"
#include <X11/Xutil.h>
#include <X11/Xlib.h>

/*
 * Private Tk Windows interfaces are confined to this translation unit.
 * Do not expose these types or functions through RBC headers.
 */
#if defined(_WIN32) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

#include "tkWinInt.h"

#if defined(_WIN32) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

/** @brief Owned release record for a borrowed Tk drawable DC.
 * Retains the drawable and private Tk state until release. A NULL dc still
 * leaves this allocated record owned by the acquiring caller.
 */
struct Rbc_WinDrawableDC {
    TkWinDCState state;
    Drawable drawable;
    HDC dc;
};

#define WINDEBUG 0
#define XAngleToRadians(a) ((double)(a) / 64.0 * M_PI / 180.0)

static const int rbcWinRopModes[] = {
    R2_BLACK,       /* GXclear */
    R2_MASKPEN,     /* GXand */
    R2_MASKPENNOT,  /* GXandReverse */
    R2_COPYPEN,     /* GXcopy */
    R2_MASKNOTPEN,  /* GXandInverted */
    R2_NOT,         /* GXnoop */
    R2_XORPEN,      /* GXxor */
    R2_MERGEPEN,    /* GXor */
    R2_NOTMERGEPEN, /* GXnor */
    R2_NOTXORPEN,   /* GXequiv */
    R2_NOT,         /* GXinvert */
    R2_MERGEPENNOT, /* GXorReverse */
    R2_NOTCOPYPEN,  /* GXcopyInverted */
    R2_MERGENOTPEN, /* GXorInverted */
    R2_NOTMASKPEN,  /* GXnand */
    R2_WHITE        /* GXset */
};

/**
 * @brief Wrap a borrowed HDC as a Tk-compatible drawable.
 * @ingroup windows_drawing_resources
 *
 * Returns None for NULL DC; otherwise allocates a TkWinDC wrapper. Caller
 * releases only this wrapper with Rbc_WinFreeDrawableFromDC. Underlying DC
 * ownership and restoration stay with the caller.
 */
Drawable Rbc_WinCreateDrawableFromDC(HDC dc) {
    TkWinDC *dcPtr;

    if (dc == NULL) {
        return None;
    }
    dcPtr = (TkWinDC *)ckalloc(sizeof(*dcPtr));
    dcPtr->type = TWD_WINDC;
    dcPtr->hdc = dc;
    return (Drawable)dcPtr;
}

/**
 * @brief Free an owned HDC drawable wrapper.
 * @ingroup windows_drawing_resources
 *
 * Accepts None. The argument must come from Rbc_WinCreateDrawableFromDC;
 * this does not release or destroy the underlying HDC.
 */
void Rbc_WinFreeDrawableFromDC(Drawable drawable) {
    if (drawable != None) {
        ckfree((char *)drawable);
    }
}

/**
 * @brief Wrap an HWND in static Tk window storage.
 * @ingroup windows_drawing_resources
 *
 * Returns a borrowed Window valid only until the next call. Does not create
 * a Tk window, own the HWND or allocate a persistent wrapper. The static
 * record is overwritten on each call and is not independent per thread.
 */
Window Rbc_WinWindowFromHWND(HWND hWnd) {
    static TkWinWindow window;

    window.type = TWD_WINDOW;
    window.handle = hWnd;
    window.winPtr = NULL;
    return (Window)&window;
}

/**
 * @brief Acquire a drawable DC and allocate its release record.
 * @ingroup windows_drawing_resources
 *
 * Requires valid output storage. Always returns an owned state record through
 * statePtrPtr, including when the returned DC is NULL. Release that record
 * with Rbc_WinReleaseDrawableDC. The record contains private Tk DC state;
 * callers separately restore GDI objects/state they explicitly change.
 */
HDC Rbc_WinAcquireDrawableDC(Display *display, Drawable drawable, Rbc_WinDrawableDC **statePtrPtr) {
    Rbc_WinDrawableDC *statePtr;

    statePtr = (Rbc_WinDrawableDC *)ckalloc(sizeof(*statePtr));
    statePtr->drawable = drawable;
    statePtr->dc = TkWinGetDrawableDC(display, drawable, &statePtr->state);
    *statePtrPtr = statePtr;
    return statePtr->dc;
}

/**
 * @brief Release Tk drawable state and its RBC acquisition record.
 * @ingroup windows_drawing_resources
 *
 * Accepts NULL. Calls TkWinReleaseDrawableDC only for a non-NULL acquired DC,
 * then frees the record even after failed acquisition. Must be called once
 * for each acquisition record; does not destroy the drawable.
 */
void Rbc_WinReleaseDrawableDC(Rbc_WinDrawableDC *statePtr) {
    if (statePtr == NULL) {
        return;
    }
    if (statePtr->dc != NULL) {
        TkWinReleaseDrawableDC(statePtr->drawable, statePtr->dc, &statePtr->state);
    }
    ckfree((char *)statePtr);
}

/**
 * @brief Select the DC raster operation from the compatibility table.
 * @ingroup windows_drawing_resources
 *
 * Requires an X graphics-function index in the table range. Changes the
 * borrowed DC without saving its prior ROP2 value; no range check or GDI
 * error result is provided.
 */
void Rbc_WinSetROP2(HDC dc, int function) { SetROP2(dc, rbcWinRopModes[function]); }

/**
 * @brief Wrap a native bitmap in Tk pixmap representation.
 * @ingroup windows_drawing_resources
 *
 * Returns None for NULL HBITMAP; otherwise allocates a TkWinBitmap record
 * with the supplied depth and colormap. The result participates in normal
 * Tk pixmap destruction, including bitmap deletion. Do not independently
 * delete the bitmap while the wrapper remains live.
 */
Pixmap Rbc_WinCreatePixmapFromBitmap(HBITMAP hBitmap, int depth, Colormap colormap) {
    TkWinBitmap *bitmapPtr;

    if (hBitmap == NULL) {
        return None;
    }
    bitmapPtr = (TkWinBitmap *)ckalloc(sizeof(*bitmapPtr));
    bitmapPtr->type = TWD_BITMAP;
    bitmapPtr->handle = hBitmap;
    bitmapPtr->colormap = colormap;
    bitmapPtr->depth = depth;
    return (Pixmap)bitmapPtr;
}

/**
 * @brief Return the borrowed native bitmap handle of a Tk pixmap.
 * @ingroup windows_drawing_resources
 *
 * Returns NULL for None or a non-bitmap drawable. The returned handle remains
 * owned by the pixmap; no reference or copy is acquired.
 */
HBITMAP Rbc_WinGetPixmapHandle(Pixmap pixmap) {
    TkWinDrawable *twdPtr;

    if (pixmap == None) {
        return NULL;
    }
    twdPtr = (TkWinDrawable *)pixmap;
    if (twdPtr->type != TWD_BITMAP) {
        return NULL;
    }
    return twdPtr->bitmap.handle;
}

/**
 * @brief Copy the system palette when the display is palette-based.
 * @ingroup windows_drawing_resources
 *
 * Acquires and releases a screen DC. For RC_PALETTE devices, allocates a
 * temporary 256-entry LOGPALETTE and creates an owned HPALETTE. Caller deletes
 * the returned palette with DeleteObject. Returns NULL on unsupported devices
 * or acquisition/allocation/creation failure; temporary storage is released.
 */
HPALETTE Rbc_GetSystemPalette(void) {
    HDC hDC;
    HPALETTE hPalette;
    DWORD flags;

    hPalette = NULL;
    hDC = GetDC(NULL);
    if (hDC == NULL) {
        return NULL;
    }
    flags = GetDeviceCaps(hDC, RASTERCAPS);
    if (flags & RC_PALETTE) {
        LOGPALETTE *palettePtr;
        size_t size;

        size = sizeof(LOGPALETTE) + (256 * sizeof(PALETTEENTRY));
        palettePtr = (LOGPALETTE *)GlobalAlloc(GPTR, size);
        if (palettePtr != NULL) {
            palettePtr->palVersion = 0x300;
            palettePtr->palNumEntries = 256;
            GetSystemPaletteEntries(hDC, 0, 256, palettePtr->palPalEntry);
            hPalette = CreatePalette(palettePtr);
            GlobalFree(palettePtr);
        }
    }
    ReleaseDC(NULL, hDC);
    return hPalette;
}

/**
 * @brief Read monochrome DIB data into a caller-owned byte buffer.
 * @ingroup windows_drawing_resources
 *
 * Requires pitchPtr, initializes it to zero and rejects invalid dimensions
 * or overflowing storage sizes. Requests a positive-height one-bit BI_RGB
 * DIB with DWORD-aligned rows; returned rows retain native bottom-up order.
 * Uses at least the calculated stride times height even if biSizeImage is
 * zero. Releases temporary global memory and each DC acquisition. Returns
 * NULL on failure, or ckalloc storage to free with ckfree and the row pitch.
 * A nonzero GetDIBits result is accepted; exact scanline count is not checked.
 */
unsigned char *Rbc_GetBitmapData(Display *display, Pixmap bitmap, int width, int height, int *pitchPtr) {
    Rbc_WinDrawableDC *dcStatePtr;
    HDC dc;
    int result;
    unsigned char *bits;
    size_t headerSize;
    size_t imageSize;
    size_t minImageSize;
    size_t totalSize;
    HBITMAP hBitmap;
    BITMAPINFOHEADER *bmiPtr;
    HANDLE hMem, hMem2;
    int bytesPerRow;

    if (pitchPtr == NULL) {
        return NULL;
    }
    *pitchPtr = 0;
    if ((bitmap == None) || (width <= 0) || (height <= 0) || (width > INT_MAX - 31)) {
        return NULL;
    }
    /*
     * Windows monochrome DIB scanlines are DWORD aligned.
     */
    bytesPerRow = ((width + 31) & ~31) / 8;
    if (bytesPerRow <= 0) {
        return NULL;
    }
    if ((size_t)height > SIZE_MAX / (size_t)bytesPerRow) {
        return NULL;
    }
    minImageSize = (size_t)height * (size_t)bytesPerRow;
    headerSize = sizeof(BITMAPINFOHEADER) + (2 * sizeof(RGBQUAD));
    hMem = GlobalAlloc(GHND, headerSize);
    if (hMem == NULL) {
        return NULL;
    }
    bmiPtr = (BITMAPINFOHEADER *)GlobalLock(hMem);
    if (bmiPtr == NULL) {
        GlobalFree(hMem);
        return NULL;
    }
    bmiPtr->biSize = sizeof(BITMAPINFOHEADER);
    bmiPtr->biPlanes = 1;
    bmiPtr->biBitCount = 1;
    bmiPtr->biCompression = BI_RGB;
    bmiPtr->biWidth = width;
    bmiPtr->biHeight = height;
    hBitmap = Rbc_WinGetPixmapHandle(bitmap);
    if (hBitmap == NULL) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
        return NULL;
    }
    dc = Rbc_WinAcquireDrawableDC(display, bitmap, &dcStatePtr);
    result = GetDIBits(dc, hBitmap, 0, (UINT)height, NULL, (BITMAPINFO *)bmiPtr, DIB_RGB_COLORS);
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    if (!result) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
        return NULL;
    }
    imageSize = (size_t)bmiPtr->biSizeImage;
    GlobalUnlock(hMem);
    /*
     * BI_RGB is allowed to report biSizeImage == 0.  In that case,
     * derive the required size from the DWORD-aligned row stride.
     */
    if (imageSize < minImageSize) {
        imageSize = minImageSize;
    }
    if (imageSize > SIZE_MAX - headerSize) {
        GlobalFree(hMem);
        return NULL;
    }
    totalSize = headerSize + imageSize;
    hMem2 = GlobalReAlloc(hMem, totalSize, 0);
    if (hMem2 == NULL) {
        GlobalFree(hMem);
        return NULL;
    }
    hMem = hMem2;
    bmiPtr = (BITMAPINFOHEADER *)GlobalLock(hMem);
    if (bmiPtr == NULL) {
        GlobalFree(hMem);
        return NULL;
    }
    dc = Rbc_WinAcquireDrawableDC(display, bitmap, &dcStatePtr);
    result = GetDIBits(dc, hBitmap, 0, (UINT)height, (unsigned char *)bmiPtr + headerSize, (BITMAPINFO *)bmiPtr,
                       DIB_RGB_COLORS);
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    bits = NULL;
    if (!result) {
        OutputDebugStringA("GetDIBits failed\n");
    } else {
        bits = ckalloc(imageSize);
        memcpy(bits, (unsigned char *)bmiPtr + headerSize, imageSize);
        *pitchPtr = bytesPerRow;
    }
    GlobalUnlock(hMem);
    GlobalFree(hMem);
    return bits;
}

/**
 * @brief Read Tk Windows bevel pixels into caller storage.
 * @ingroup windows_drawing_resources
 *
 * Borrows window and border; fills flat, light, dark, light2 and dark2 through
 * TkWinGetBorderPixels. Does not allocate or transfer color resources.
 */
void Rbc_WinGet3DBorderColors(Tk_Window tkwin, Tk_3DBorder border, Rbc_Win3DBorderColors *colorsPtr) {
    colorsPtr->flat = TkWinGetBorderPixels(tkwin, border, TK_3D_FLAT_GC);
    colorsPtr->light = TkWinGetBorderPixels(tkwin, border, TK_3D_LIGHT_GC);
    colorsPtr->dark = TkWinGetBorderPixels(tkwin, border, TK_3D_DARK_GC);
    colorsPtr->light2 = TkWinGetBorderPixels(tkwin, border, TK_3D_LIGHT2);
    colorsPtr->dark2 = TkWinGetBorderPixels(tkwin, border, TK_3D_DARK2);
}

/**
 * @brief Fill a positive rectangle through Tk Windows drawing.
 * @ingroup windows_drawing_resources
 *
 * Borrows DC and ignores nonpositive dimensions. Uses TkWinFillRect so bevel
 * strips match Tk geometry. Does not acquire a DC or report drawing failure.
 */
void Rbc_WinFillRect(HDC dc, int x, int y, int width, int height, COLORREF color) {
    if ((width <= 0) || (height <= 0)) {
        return;
    }
    /*
     * Use Tk's own Windows rectangle primitive so this reproduces
     * Tk_3DVerticalBevel/Tk_3DHorizontalBevel exactly.
     */
    TkWinFillRect(dc, x, y, width, height, (int)color);
}

/**
 * @brief Try a pattern-brush fast path for opaque stipple rectangles.
 * @ingroup windows_drawing_resources
 *
 * Requires GXcopy, FillOpaqueStippled and a bitmap stipple. Borrows GC/rectangles
 * and owns the pattern brush and DC acquisition. Sets text/background colors
 * in reverse GC order for monochrome stipple expansion and uses the GC origin.
 * Restores colors, brush and origin after painting. FALSE requests ordinary
 * fallback and can follow partial output; copy semantics permit replay.
 * Failed DC acquisition releases its allocated record and the pattern brush
 * before returning FALSE.
 */
int Rbc_WinFillOpaqueStippledRectangles(Display *display, Drawable drawable, GC gc, const XRectangle *rectangles,
                                        int nRectangles) {
    Rbc_WinDrawableDC *dcStatePtr;
    TkWinDrawable *stipplePtr;
    HBRUSH patternBrush;
    HBRUSH oldBrush;
    COLORREF oldTextColor;
    COLORREF oldBkColor;
    POINT oldOrigin;
    HDC dc;
    int i;
    int result;

    if ((display == NULL) || (drawable == None) || (gc == NULL) || (rectangles == NULL) || (nRectangles <= 0)) {
        return FALSE;
    }
    /*
     * Keep this optimization deliberately narrow initially.
     */
    if ((gc->function != GXcopy) || (gc->fill_style != FillOpaqueStippled) || (gc->stipple == None)) {
        return FALSE;
    }
    stipplePtr = (TkWinDrawable *)gc->stipple;
    if (stipplePtr->type != TWD_BITMAP) {
        return FALSE;
    }
    patternBrush = CreatePatternBrush(stipplePtr->bitmap.handle);
    if (patternBrush == NULL) {
        return FALSE;
    }
    dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
    if (dc == NULL) {
        Rbc_WinReleaseDrawableDC(dcStatePtr);
        DeleteObject(patternBrush);
        return FALSE;
    }
    oldBrush = (HBRUSH)SelectObject(dc, patternBrush);
    if (oldBrush == NULL) {
        Rbc_WinReleaseDrawableDC(dcStatePtr);
        DeleteObject(patternBrush);
        return FALSE;
    }
    /*
     * CreatePatternBrush with a monochrome bitmap maps:
     *
     *      bitmap 0 -> DC text color
     *      bitmap 1 -> DC background color
     *
     * X stipple semantics are:
     *
     *      stipple 0 -> background
     *      stipple 1 -> foreground
     *
     * Hence the apparently reversed assignments below.
     */
    oldTextColor = SetTextColor(dc, (COLORREF)gc->background);
    oldBkColor = SetBkColor(dc, (COLORREF)gc->foreground);
    /*
     * Preserve Tk's existing tile/stipple origin convention.
     */
    if (!SetBrushOrgEx(dc, gc->ts_x_origin, gc->ts_y_origin, &oldOrigin)) {
        SetBkColor(dc, oldBkColor);
        SetTextColor(dc, oldTextColor);
        SelectObject(dc, oldBrush);
        Rbc_WinReleaseDrawableDC(dcStatePtr);
        DeleteObject(patternBrush);
        return FALSE;
    }
    result = TRUE;
    for (i = 0; i < nRectangles; i++) {
        const XRectangle *rectPtr;

        rectPtr = rectangles + i;
        if ((rectPtr->width == 0) || (rectPtr->height == 0)) {
            continue;
        }
        if (!PatBlt(dc, rectPtr->x, rectPtr->y, rectPtr->width, rectPtr->height, PATCOPY)) {
            result = FALSE;
            break;
        }
    }
    SetBrushOrgEx(dc, oldOrigin.x, oldOrigin.y, NULL);
    SetBkColor(dc, oldBkColor);
    SetTextColor(dc, oldTextColor);
    SelectObject(dc, oldBrush);
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    DeleteObject(patternBrush);
    return result;
}

/**
 * @brief Copy a monochrome bitmap at multiple positions using shared DCs.
 * @ingroup windows_drawing_resources
 *
 * Borrows bitmap, GC and positions. Caller restricts this to the unmasked
 * copy-compatible case; the helper does not validate raster-operation or
 * clip-mask eligibility. Saves destination background mode/colors, sets the
 * opaque expansion colors and uses SRCCOPY. Restores destination state and
 * releases both DC acquisitions. FALSE can follow partial output; shared
 * caller may replay only because this path has copy semantics.
 */
int Rbc_WinCopyOpaqueBitmapBatch(Display *display, Drawable drawable, Pixmap bitmap, GC gc, int width, int height,
                                 const int (*positions)[2], Tcl_Size nPositions) {
    Rbc_WinDrawableDC *dcStatePtr;
    TkWinDCState srcState;
    HDC srcDC;
    HDC dc;
    COLORREF oldBkColor;
    COLORREF oldTextColor;
    int oldBkMode;
    Tcl_Size i;
    int result;

    if ((display == NULL) || (drawable == None) || (bitmap == None) || (gc == NULL) || (positions == NULL) ||
        (nPositions <= 0) || (width <= 0) || (height <= 0)) {
        return FALSE;
    }
    dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
    if (dc == NULL) {
        Rbc_WinReleaseDrawableDC(dcStatePtr);
        return FALSE;
    }
    /*
     * Acquire the monochrome source bitmap DC once for the whole run.
     */
    srcDC = TkWinGetDrawableDC(display, bitmap, &srcState);
    if (srcDC == NULL) {
        Rbc_WinReleaseDrawableDC(dcStatePtr);
        return FALSE;
    }
    /*
     * Match Tk's opaque XCopyPlane conversion exactly:
     *
     *      bitmap bit 0 -> gc foreground
     *      bitmap bit 1 -> gc background
     */
    oldBkMode = SetBkMode(dc, OPAQUE);
    oldBkColor = SetBkColor(dc, (COLORREF)gc->foreground);
    oldTextColor = SetTextColor(dc, (COLORREF)gc->background);
    result = TRUE;
    for (i = 0; i < nPositions; i++) {
        if (!BitBlt(dc, positions[i][0], positions[i][1], width, height, srcDC, 0, 0, SRCCOPY)) {
            result = FALSE;
            break;
        }
    }
    SetTextColor(dc, oldTextColor);
    SetBkColor(dc, oldBkColor);
    SetBkMode(dc, oldBkMode);
    TkWinReleaseDrawableDC(bitmap, srcDC, &srcState);
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    return result;
}

/**
 * @brief Return the legacy Windows X request-size surrogate.
 * @ingroup windows_drawing_resources
 *
 * Returns SHRT_MAX divided by four, independent of display. This is a
 * compatibility sizing value, not a query of a live GDI device limit.
 */
long Rbc_EmulateXMaxRequestSize(Display *display) { return (SHRT_MAX / 4); }

/**
 * @brief Move the native window to the bottom of its stacking order.
 * @ingroup windows_drawing_resources
 *
 * Borrows the Tk-compatible Window, obtains its HWND and calls SetWindowPos
 * without moving or resizing. Does not report failure or increment an X
 * request counter; the flags do not explicitly suppress activation.
 */
void Rbc_EmulateXLowerWindow(Display *display, Window window) {
    HWND hWnd;

    hWnd = Tk_GetHWND(window);
    /*
        display->request++;
    */
    SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

/**
 * @brief Copy the bounded zero-terminated GC dash sequence.
 * @ingroup windows_drawing_resources
 *
 * Borrows the GC and writes at most RBC_MAX_DASH_VALUES DWORDs into caller
 * storage. Reads unsigned bytes starting at gc->dashes using the established
 * Windows GC dash layout. Returns the number copied, without allocation.
 */
static int GetGCDashValues(GC gc, DWORD *dashArr) {
    const unsigned char *srcPtr;
    int nValues;

    srcPtr = (const unsigned char *)&gc->dashes;
    for (nValues = 0; (nValues < RBC_MAX_DASH_VALUES) && (srcPtr[nValues] != 0); nValues++) {
        dashArr[nValues] = (DWORD)srcPtr[nValues];
    }
    return nValues;
}

/**
 * @brief Create an owned GDI pen from configured GC line attributes.
 * @ingroup windows_drawing_resources
 *
 * Clamps line width to at least one, maps caps/joins and duplicates a single
 * dash value for a repeating user style. Both dash line styles use the same
 * user-style pen representation. Sets the borrowed DC background mode to
 * TRANSPARENT without restoring it. Uses a geometric pen for width above one
 * and a cosmetic pen otherwise; asserts creation success. Caller selects and
 * later deletes the pen after restoring its previous selection.
 */
HPEN Rbc_GCToPen(HDC dc, GC gc) {
    DWORD lineAttrs;
    DWORD lineStyle;
    DWORD dashArr[RBC_MAX_DASH_VALUES];
    DWORD *dashPtr;
    int nValues;
    int lineWidth;
    LOGBRUSH lBrush;
    HPEN pen;

    nValues = 0;
    lineWidth = (gc->line_width < 1) ? 1 : gc->line_width;
    if ((gc->line_style == LineOnOffDash) || (gc->line_style == LineDoubleDash)) {
        nValues = GetGCDashValues(gc, dashArr);
        /*
         * ExtCreatePen needs a repeating user style.  Preserve the
         * historical RBC behavior for a one-element dash list.
         */
        if (nValues == 1) {
            dashArr[1] = dashArr[0];
            nValues = 2;
        }
    }
    lBrush.lbStyle = BS_SOLID;
    lBrush.lbColor = gc->foreground;
    lBrush.lbHatch = 0;
    lineAttrs = 0;
    switch (gc->cap_style) {
    case CapNotLast:
    case CapButt:
        lineAttrs |= PS_ENDCAP_FLAT;
        break;
    case CapRound:
        lineAttrs |= PS_ENDCAP_ROUND;
        break;
    default:
        lineAttrs |= PS_ENDCAP_SQUARE;
        break;
    }
    switch (gc->join_style) {
    case JoinMiter:
        lineAttrs |= PS_JOIN_MITER;
        break;
    case JoinBevel:
        lineAttrs |= PS_JOIN_BEVEL;
        break;
    case JoinRound:
    default:
        lineAttrs |= PS_JOIN_ROUND;
        break;
    }
    SetBkMode(dc, TRANSPARENT);
    if (nValues > 0) {
        lineStyle = PS_USERSTYLE;
        dashPtr = dashArr;
    } else {
        lineStyle = PS_SOLID;
        dashPtr = NULL;
    }
    if (lineWidth > 1) {
        pen = ExtCreatePen(PS_GEOMETRIC | lineAttrs | lineStyle, lineWidth, &lBrush, nValues, dashPtr);
    } else {
        pen = ExtCreatePen(PS_COSMETIC | lineAttrs | lineStyle, 1, &lBrush, nValues, dashPtr);
    }
    assert(pen != NULL);
    return pen;
}

/**
 * @brief Draw rectangle outlines through one acquired Windows DC.
 * @ingroup windows_drawing_primitives
 *
 * Ignores None target, NULL array and nonpositive count. Borrows GC/rectangles;
 * selects an owned GC-derived pen and stock null brush, then draws rectangles
 * in input order. Adds one to each far edge for the Windows pixel convention.
 * Restores selections, deletes the displaced objects and releases DC state.
 * Requires valid GC and usable DC; GDI results are not propagated.
 */
void Rbc_EmulateXDrawRectangles(Display *display, Drawable drawable, GC gc, XRectangle *rectArr, int nRects) {
    HPEN pen, oldPen;
    Rbc_WinDrawableDC *dcStatePtr;
    HBRUSH brush, oldBrush;
    HDC dc;
    XRectangle *rectPtr;
    int i;

    if ((drawable == None) || (rectArr == NULL) || (nRects <= 0)) {
        return;
    }
    dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
    pen = Rbc_GCToPen(dc, gc);
    brush = GetStockObject(NULL_BRUSH);
    oldPen = SelectPen(dc, pen);
    oldBrush = SelectBrush(dc, brush);
    Rbc_WinSetROP2(dc, gc->function);
    rectPtr = rectArr;
    for (i = 0; i < nRects; i++, rectPtr++) {
        Rectangle(dc, (int)rectPtr->x, (int)rectPtr->y, (int)(rectPtr->x + rectPtr->width + 1),
                  (int)(rectPtr->y + rectPtr->height + 1));
    }
    DeletePen(SelectPen(dc, oldPen));
    DeleteBrush(SelectBrush(dc, oldBrush));
    Rbc_WinReleaseDrawableDC(dcStatePtr);
}

/**
 * @brief Translate XArc angle units and closure into a GDI arc operation.
 * @ingroup windows_drawing_primitives
 *
 * Borrows an already configured DC and XArc. Sweeps of at least one full turn
 * in magnitude use Ellipse. Other sweeps normalize angles in 1/64 degrees
 * and swap endpoints for negative extent. Converts radial endpoints with
 * the historical add-one-half then int conversion, accounting for inverted Y.
 * Adds one to bounding-box far edges. A zero brush argument selects open Arc;
 * a nonzero brush selects Chord or Pie from arcMode. Pen/brush selection is
 * the caller's responsibility; the pen argument is not otherwise used.
 * Does not explicitly reject zero sweep or change the DC arc direction.
 */
static void DrawArc(HDC dc, int arcMode, XArc *arcPtr, HPEN pen, HBRUSH brush) {
    int start, extent, clockwise;
    int xstart, ystart, xend, yend;
    double radian_start, radian_end, xr, yr;
    double dx, dy;

    if ((arcPtr->angle2 >= (64 * 360)) || (arcPtr->angle2 <= -(64 * 360))) {
        Ellipse(dc, arcPtr->x, arcPtr->y, arcPtr->x + arcPtr->width + 1, arcPtr->y + arcPtr->height + 1);
        return;
    }
    start = arcPtr->angle1, extent = arcPtr->angle2;
    clockwise = (extent < 0); /* Non-zero if clockwise */
    /*
     * Compute the absolute starting and ending angles in normalized radians.
     * Swap the start and end if drawing clockwise.
     */
    start = start % (64 * 360);
    if (start < 0) {
        start += (64 * 360);
    }
    extent = (start + extent) % (64 * 360);
    if (extent < 0) {
        extent += (64 * 360);
    }
    if (clockwise) {
        int tmp = start;
        start = extent;
        extent = tmp;
    }
    radian_start = XAngleToRadians(start);
    radian_end = XAngleToRadians(extent);
    /*
     * Now compute points on the radial lines that define the starting and
     * ending angles.  Be sure to take into account that the y-coordinate
     * system is inverted.
     */
    dx = arcPtr->width * 0.5;
    dy = arcPtr->height * 0.5;
    xr = arcPtr->x + dx;
    yr = arcPtr->y + dy;
    xstart = (int)((xr + cos(radian_start) * dx) + 0.5);
    ystart = (int)((yr + sin(-radian_start) * dy) + 0.5);
    xend = (int)((xr + cos(radian_end) * dx) + 0.5);
    yend = (int)((yr + sin(-radian_end) * dy) + 0.5);
    /*
     * Now draw a filled or open figure.  Note that we have to
     * increase the size of the bounding box by one to account for the
     * difference in pixel definitions between X and Windows.
     */
    if (brush == 0) {
        Arc(dc, arcPtr->x, arcPtr->y, arcPtr->x + arcPtr->width + 1, arcPtr->y + arcPtr->height + 1, xstart, ystart,
            xend, yend);
        /* FIXME: */
    } else {
        if (arcMode == ArcChord) {
            Chord(dc, arcPtr->x, arcPtr->y, arcPtr->x + arcPtr->width + 1, arcPtr->y + arcPtr->height + 1, xstart,
                  ystart, xend, yend);
        } else if (arcMode == ArcPieSlice) {
            Pie(dc, arcPtr->x, arcPtr->y, arcPtr->x + arcPtr->width + 1, arcPtr->y + arcPtr->height + 1, xstart, ystart,
                xend, yend);
        }
    }
}

/**
 * @brief Draw open arcs with a retained pen and null brush.
 * @ingroup windows_drawing_primitives
 *
 * Ignores None target, NULL input and nonpositive count. Acquires one DC,
 * sets the GC raster operation and selects a GC-derived pen and stock null
 * brush. Calls DrawArc for each item in order, then restores selections,
 * deletes displaced objects and releases state. GC and array remain borrowed.
 */
void Rbc_EmulateXDrawArcs(Display *display, Drawable drawable, GC gc, XArc *arcArr, int nArcs) {
    HPEN pen, oldPen;
    HBRUSH brush, oldBrush;
    HDC dc;
    Rbc_WinDrawableDC *dcStatePtr;
    XArc *arcPtr, *endPtr;

    //    display->request++;
    if ((drawable == None) || (arcArr == NULL) || (nArcs <= 0)) {
        return;
    }
    dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
    Rbc_WinSetROP2(dc, gc->function);
    pen = Rbc_GCToPen(dc, gc);
    oldPen = SelectPen(dc, pen);
    brush = GetStockBrush(NULL_BRUSH);
    oldBrush = SelectBrush(dc, brush);
    endPtr = arcArr + nArcs;
    for (arcPtr = arcArr; arcPtr < endPtr; arcPtr++) {
        DrawArc(dc, gc->arc_mode, arcPtr, pen, 0);
    }
    DeleteBrush(SelectBrush(dc, oldBrush));
    DeletePen(SelectPen(dc, oldPen));
    Rbc_WinReleaseDrawableDC(dcStatePtr);
}

/**
 * @brief Draw filled arc figures with a solid foreground brush.
 * @ingroup windows_drawing_primitives
 *
 * Acquires one DC and selects both a GC-derived pen and an owned solid brush.
 * DrawArc chooses chord/pieslice closure or a full ellipse. Thus this path
 * retains a drawing pen rather than selecting a null pen for fill-only output.
 * Ignores empty input; restores selections, deletes owned objects and releases
 * DC state. Does not report individual GDI failures.
 */
void Rbc_EmulateXFillArcs(Display *display, Drawable drawable, GC gc, XArc *arcArr, int nArcs) {
    HBRUSH brush, oldBrush;
    HPEN pen, oldPen;
    HDC dc;
    XArc *arcPtr, *endPtr;
    Rbc_WinDrawableDC *dcStatePtr;

    //    display->request++;
    if ((drawable == None) || (arcArr == NULL) || (nArcs <= 0)) {
        return;
    }
    dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
    Rbc_WinSetROP2(dc, gc->function);
    pen = Rbc_GCToPen(dc, gc);
    oldPen = SelectPen(dc, pen);
    brush = CreateSolidBrush(gc->foreground);
    oldBrush = SelectBrush(dc, brush);
    endPtr = arcArr + nArcs;
    for (arcPtr = arcArr; arcPtr < endPtr; arcPtr++) {
        DrawArc(dc, gc->arc_mode, arcPtr, pen, brush);
    }
    DeleteBrush(SelectBrush(dc, oldBrush));
    DeletePen(SelectPen(dc, oldPen));
    Rbc_WinReleaseDrawableDC(dcStatePtr);
}

/**
 * @brief Compose independent XOR segments offscreen and present once.
 * @ingroup windows_drawing_primitives
 *
 * Requires nonempty valid segments, GC and DC. Intersects expanded segment
 * bounds with GetClipBox and returns TRUE for empty coverage. Owns a compatible
 * DC, bitmap and GC-derived pen. Clears black (XOR identity), translates the
 * viewport and draws each segment as its own Polyline. Only successful
 * composition triggers one SRCINVERT copy to the destination. Restores selected
 * objects and frees temporary resources on handled failure paths. FALSE asks
 * the caller to retry directly; no destination rollback is implemented.
 */
static int DrawXorSegments(HDC dc, GC gc, const XSegment *segments, int count) {
    RECT clip;
    HDC maskDC;
    HBITMAP bitmap, oldBitmap;
    HPEN pen, oldPen;
    int i, result;
    int left, top, right, bottom, margin;

    if (GetClipBox(dc, &clip) == ERROR) {
        return FALSE;
    }
    left = right = segments[0].x1;
    top = bottom = segments[0].y1;
    for (i = 0; i < count; i++) {
        left = MIN(left, MIN(segments[i].x1, segments[i].x2));
        right = MAX(right, MAX(segments[i].x1, segments[i].x2));
        top = MIN(top, MIN(segments[i].y1, segments[i].y2));
        bottom = MAX(bottom, MAX(segments[i].y1, segments[i].y2));
    }
    margin = MAX(1, gc->line_width) + 1;
    left = MAX(clip.left, left - margin);
    top = MAX(clip.top, top - margin);
    right = MIN(clip.right, right + margin);
    bottom = MIN(clip.bottom, bottom + margin);
    if (right <= left || bottom <= top) {
        return TRUE;
    }
    maskDC = CreateCompatibleDC(dc);
    if (maskDC == NULL) {
        return FALSE;
    }
    bitmap = CreateCompatibleBitmap(dc, right - left, bottom - top);
    if (bitmap == NULL) {
        DeleteDC(maskDC);
        return FALSE;
    }
    oldBitmap = SelectBitmap(maskDC, bitmap);
    if (!PatBlt(maskDC, 0, 0, right - left, bottom - top, BLACKNESS)) {
        SelectBitmap(maskDC, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(maskDC);
        return FALSE;
    }
    SetViewportOrgEx(maskDC, -left, -top, NULL);
    Rbc_WinSetROP2(maskDC, GXxor);
    pen = Rbc_GCToPen(maskDC, gc);
    oldPen = SelectPen(maskDC, pen);
    result = TRUE;
    for (i = 0; i < count; i++) {
        POINT points[2];
        points[0].x = segments[i].x1;
        points[0].y = segments[i].y1;
        points[1].x = segments[i].x2;
        points[1].y = segments[i].y2;
        if (!Polyline(maskDC, points, 2)) {
            result = FALSE;
            break;
        }
    }
    if (result) {
        result = BitBlt(dc, left, top, right - left, bottom - top, maskDC, left, top, SRCINVERT);
    }
    DeletePen(SelectPen(maskDC, oldPen));
    SelectBitmap(maskDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(maskDC);
    return result;
}

/**
 * @brief Draw independent segments with XOR-specific presentation.
 * @ingroup windows_drawing_primitives
 *
 * Rejects missing target/GC/segments and nonpositive count. GXxor first tries
 * DrawXorSegments and stops if handled. Otherwise owns bounded POINT/count
 * arrays and an acquired DC/pen: batches contain 8192 segments, or 680 for
 * wide lines. XOR fallback uses individual Polyline calls to retain dash phase
 * and overlap cancellation. Other operations use PolyPolyline, with per-segment
 * MoveToEx/LineTo fallback on failure. Frees arrays and restores/releases pen
 * and DC state. Void return does not report partial output or GDI failures.
 */
void Rbc_EmulateXDrawSegments(Display *display, Drawable drawable, GC gc, XSegment *segArr, int nSegments) {
    enum {
        /*
         * GDI/EMF permits 16K points in one PolyPolyline operation.
         * Each XSegment is represented by one independent two-point
         * polyline.
         */
        SEGMENT_BATCH = 8192,

        /*
         * Be conservative for wide geometric pens.  This is the same
         * limit currently used by the optimized strip-element path.
         */
        SEGMENT_WIDE_BATCH = 680
    };
    Rbc_WinDrawableDC *dcStatePtr;
    HDC dc;
    HPEN pen, oldPen;
    POINT *points;
    DWORD *counts;
    int maxSegments;
    int first;
    int i;

    if ((drawable == None) || (gc == NULL) || (segArr == NULL) || (nSegments <= 0)) {
        return;
    }
    if (gc->function == GXxor) {
        int drawn;
        dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
        drawn = DrawXorSegments(dc, gc, segArr, nSegments);
        Rbc_WinReleaseDrawableDC(dcStatePtr);
        if (drawn) {return;}
    }
    maxSegments = (gc->line_width > 1) ? SEGMENT_WIDE_BATCH : SEGMENT_BATCH;
    if (maxSegments > nSegments) {
        maxSegments = nSegments;
    }
    /*
     * Each independent segment requires exactly two POINTs.
     */
    if ((size_t)maxSegments > SIZE_MAX / (2u * sizeof(*points))) {
        return;
    }
    points = Tcl_AttemptAlloc((size_t)maxSegments * 2u * sizeof(*points));
    if (points == NULL) {
        return;
    }
    if ((size_t)maxSegments > SIZE_MAX / sizeof(*counts)) {
        ckfree(points);
        return;
    }
    counts = Tcl_AttemptAlloc((size_t)maxSegments * sizeof(*counts));
    if (counts == NULL) {
        ckfree(points);
        return;
    }
    /*
     * Every PolyPolyline entry is an independent two-point polyline.
     * The count array is therefore invariant across all chunks.
     */
    for (i = 0; i < maxSegments; i++) {
        counts[i] = 2;
    }
    dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
    Rbc_WinSetROP2(dc, gc->function);
    pen = Rbc_GCToPen(dc, gc);
    oldPen = SelectPen(dc, pen);
    first = 0;
    while (first < nSegments) {
        int count;

        count = nSegments - first;
        if (count > maxSegments) {
            count = maxSegments;
        }
        for (i = 0; i < count; i++) {
            const XSegment *segPtr;

            segPtr = segArr + first + i;
            points[2 * i].x = (LONG)segPtr->x1;
            points[2 * i].y = (LONG)segPtr->y1;

            points[2 * i + 1].x = (LONG)segPtr->x2;
            points[2 * i + 1].y = (LONG)segPtr->y2;
        }
        /*
         * PolyPolyline preserves the independent-segment nature of
         * XDrawSegments because every sub-polyline has exactly two
         * points.
         *
         * Fall back to the old per-segment implementation if the
         * device rejects the batched operation.
         */
        /*
         * XOR must be independent of batch grouping. In particular, moving
         * crosshairs submits the old and new pairs together, whereas showing
         * and hiding submit one pair at a time. Draw each XOR segment with
         * its own Polyline operation so dash phase and overlapping coverage
         * are identical when the same geometry is erased later.
         */
        if (gc->function == GXxor) {
            for (i = 0; i < count; i++) {
                Polyline(dc, points + 2 * i, 2);
            }
        } else if (!PolyPolyline(dc, points, counts, (DWORD)count)) {
            for (i = 0; i < count; i++) {
                MoveToEx(dc, points[2 * i].x, points[2 * i].y, NULL);
                LineTo(dc, points[2 * i + 1].x, points[2 * i + 1].y);
            }
        }
        first += count;
    }
    DeletePen(SelectPen(dc, oldPen));
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    ckfree(counts);
    ckfree(points);
}

/**
 * @brief Write foreground pixels at absolute or accumulated positions.
 * @ingroup windows_drawing_primitives
 *
 * Ignores None target, NULL array and nonpositive count. Acquires one DC and
 * sets its raster operation, then calls SetPixelV with GC foreground for each
 * point. CoordModeOrigin uses absolute positions; any other mode accumulates
 * LONG offsets from the first point. Releases DC state. No explicit pixel
 * read/modify/write composition or overflow check is performed here.
 */
void Rbc_EmulateXDrawPoints(Display *display, Drawable drawable, GC gc, XPoint *pointArr, int nPoints, int mode) {
    HDC dc;
    Rbc_WinDrawableDC *dcStatePtr;
    int i;

    if ((drawable == None) || (pointArr == NULL) || (nPoints <= 0)) {
        return;
    }
    dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
    Rbc_WinSetROP2(dc, gc->function);
    if (mode == CoordModeOrigin) {
        for (i = 0; i < nPoints; i++) {
            SetPixelV(dc, pointArr[i].x, pointArr[i].y, gc->foreground);
        }
    } else {
        LONG x, y;

        x = pointArr[0].x;
        y = pointArr[0].y;
        SetPixelV(dc, x, y, gc->foreground);
        for (i = 1; i < nPoints; i++) {
            x += pointArr[i].x;
            y += pointArr[i].y;
            SetPixelV(dc, x, y, gc->foreground);
        }
    }
    Rbc_WinReleaseDrawableDC(dcStatePtr);
}

/**
 * @brief Draw a counted string using the Tk font in the GC.
 * @ingroup windows_drawing_primitives
 *
 * Ignores None target, NULL string and nonpositive byte length. Delegates to
 * Tk_DrawChars with gc->font cast to Tk_Font and baseline coordinates. Borrows
 * all arguments and does not create a DC, font or converted string itself.
 */
void Rbc_EmulateXDrawString(Display *display, Drawable drawable, GC gc, int x, int y, _Xconst char *string,
                            int length) {
    if ((drawable == None) || (string == NULL) || (length <= 0)) {
        return;
    }
    Tk_DrawChars(display, drawable, gc, (Tk_Font)gc->font, string, length, x, y);
}
