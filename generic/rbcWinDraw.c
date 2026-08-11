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

#include "rbcInt.h"
#include <X11/Xutil.h>
#include <X11/Xlib.h>

#define WINDEBUG 0
#define XAngleToRadians(a) ((double)(a) / 64.0 * M_PI / 180.0)

const int tkpWinRopModes[] = {
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


/*
 *--------------------------------------------------------------
 *
 * Rbc_GetSystemPalette --
 *
 *      TODO: Description
 *
 * Parameters:
 *      None
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * Rbc_GetBitmapData --
 *
 *      Returns the DIB bits from a bitmap.
 *
 * Parameters:
 *      Display *display - Display of bitmap
 *      Pixmap bitmap - Bitmap to query
 *      int width - Width of bitmap
 *      int height - Height of bitmap
 *      int *pitchPtr - (out) Number of bytes per row
 *
 * Results:
 *      Returns a byte array of bitmap data or NULL if an error
 *      occurred.  The parameter pitchPtr returns the number
 *      of bytes per row.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
unsigned char *Rbc_GetBitmapData(Display *display, Pixmap bitmap, int width, int height, int *pitchPtr) {
    TkWinDCState state;
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
    hBitmap = ((TkWinDrawable *)bitmap)->bitmap.handle;
    dc = TkWinGetDrawableDC(display, bitmap, &state);
    result = GetDIBits(dc, hBitmap, 0, (UINT)height, NULL, (BITMAPINFO *)bmiPtr, DIB_RGB_COLORS);
    TkWinReleaseDrawableDC(bitmap, dc, &state);
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
    dc = TkWinGetDrawableDC(display, bitmap, &state);
    result = GetDIBits(dc, hBitmap, 0, (UINT)height, (unsigned char *)bmiPtr + headerSize, (BITMAPINFO *)bmiPtr,
                       DIB_RGB_COLORS);
    TkWinReleaseDrawableDC(bitmap, dc, &state);
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

/*
 *--------------------------------------------------------------
 *
 * Rbc_EmulateXMaxRequestSize --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
long Rbc_EmulateXMaxRequestSize(Display *display) { return (SHRT_MAX / 4); }

/*
 *--------------------------------------------------------------
 *
 * Rbc_EmulateXLowerWindow --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Window window
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_EmulateXLowerWindow(Display *display, Window window) {
    HWND hWnd;

    hWnd = Tk_GetHWND(window);
    /*
        display->request++;
    */
    SetWindowPos(hWnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_SetROP2 --
 *
 *      TODO: Description
 *
 * Parameters:
 *      HDC dc
 *      int function
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_SetROP2(HDC dc, int function) { SetROP2(dc, tkpWinRopModes[function]); }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GCToPen --
 *
 *      Set up the graphics port from the given GC.
 *
 * Parameters:
 *      HDC dc
 *      GC gc
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The current port is adjusted.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_EmulateXDrawRectangles --
 *
 *      Draws the outlines of the specified rectangles as if a
 *      five-point PolyLine protocol request were specified for each
 *      rectangle:
 *
 *        [x,y] [x+width,y] [x+width,y+height] [x,y+height]
 *        [x,y]
 *
 *      For the specified rectangles, these functions do not draw a
 *      pixel more than once.  XDrawRectangles draws the rectangles in
 *      the order listed in the array.  If rectangles intersect, the
 *      intersecting pixels are drawn multiple times.  Draws a
 *      rectangle.
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      GC gc
 *      XRectangle *rectArr
 *      int nRects
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws rectangles on the specified drawable.
 *
 *----------------------------------------------------------------------
 */
void Rbc_EmulateXDrawRectangles(Display *display, Drawable drawable, GC gc, XRectangle *rectArr, int nRects) {
    HPEN pen, oldPen;
    TkWinDCState state;
    HBRUSH brush, oldBrush;
    HDC dc;
    XRectangle *rectPtr;
    int i;

    if ((drawable == None) || (rectArr == NULL) || (nRects <= 0)) {
        return;
    }
    dc = TkWinGetDrawableDC(display, drawable, &state);
    pen = Rbc_GCToPen(dc, gc);
    brush = GetStockObject(NULL_BRUSH);
    oldPen = SelectPen(dc, pen);
    oldBrush = SelectBrush(dc, brush);
    SetROP2(dc, tkpWinRopModes[gc->function]);
    rectPtr = rectArr;
    for (i = 0; i < nRects; i++, rectPtr++) {
        Rectangle(dc, (int)rectPtr->x, (int)rectPtr->y, (int)(rectPtr->x + rectPtr->width + 1),
                  (int)(rectPtr->y + rectPtr->height + 1));
    }
    DeletePen(SelectPen(dc, oldPen));
    DeleteBrush(SelectBrush(dc, oldBrush));
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawArc --
 *
 *      This procedure handles the rendering of drawn or filled
 *      arcs and chords.
 *
 * Parameters:
 *      HDC dc
 *      int arcMode - Mode: either ArcChord or ArcPieSlice
 *      XArc *arcPtr
 *      HPEN pen
 *      HBRUSH brush
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Renders the requested arcs.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_EmulateXDrawArcs --
 *
 *      Draws multiple circular or elliptical arcs.  Each arc is
 *      specified by a rectangle and two angles.  The center of the
 *      circle or ellipse is the center of the rect- angle, and the
 *      major and minor axes are specified by the width and height.
 *      Positive angles indicate counterclock- wise motion, and
 *      negative angles indicate clockwise motion.  If the magnitude
 *      of angle2 is greater than 360 degrees, XDrawArcs truncates it
 *      to 360 degrees.
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      GC gc
 *      XArc *arcArr
 *      int nArcs
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws an arc for each array element on the specified drawable.
 *
 *----------------------------------------------------------------------
 */
void Rbc_EmulateXDrawArcs(Display *display, Drawable drawable, GC gc, XArc *arcArr, int nArcs) {
    HPEN pen, oldPen;
    HBRUSH brush, oldBrush;
    HDC dc;
    TkWinDCState state;
    XArc *arcPtr, *endPtr;

    //    display->request++;
    if ((drawable == None) || (arcArr == NULL) || (nArcs <= 0)) {
        return;
    }
    dc = TkWinGetDrawableDC(display, drawable, &state);
    SetROP2(dc, tkpWinRopModes[gc->function]);
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
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_EmulateXFillArcs --
 *
 *      Draw a filled arc.
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      GC gc
 *      XArc *arcArr
 *      int nArcs
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws a filled arc for each array element on the specified drawable.
 *
 *----------------------------------------------------------------------
 */
void Rbc_EmulateXFillArcs(Display *display, Drawable drawable, GC gc, XArc *arcArr, int nArcs) {
    HBRUSH brush, oldBrush;
    HPEN pen, oldPen;
    HDC dc;
    XArc *arcPtr, *endPtr;
    TkWinDCState state;

    //    display->request++;
    if ((drawable == None) || (arcArr == NULL) || (nArcs <= 0)) {
        return;
    }
    dc = TkWinGetDrawableDC(display, drawable, &state);
    SetROP2(dc, tkpWinRopModes[gc->function]);
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
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_EmultateXDrawSegments --
 *
 *      Draws multiple, unconnected lines. For each segment, draws a
 *      line between (x1, y1) and (x2, y2).  It draws the lines in the
 *      order listed in the array of XSegment structures and does not
 *      perform joining at coincident endpoints.  For any given line,
 *      does not draw a pixel more than once. If lines intersect, the
 *      intersecting pixels are drawn multiple times.
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      GC gc
 *      XSegment *segArr
 *      int nSegments
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws unconnected line segments on the specified drawable.
 *
 *----------------------------------------------------------------------
 */
void Rbc_EmulateXDrawSegments(Display *display, Drawable drawable, GC gc, XSegment *segArr, int nSegments) {
    HDC dc;
    HPEN pen, oldPen;
    XSegment *segPtr, *endPtr;
    TkWinDCState state;

    if ((drawable == None) || (segArr == NULL) || (nSegments <= 0)) {
        return;
    }
    dc = TkWinGetDrawableDC(display, drawable, &state);
    SetROP2(dc, tkpWinRopModes[gc->function]);
    pen = Rbc_GCToPen(dc, gc);
    oldPen = SelectPen(dc, pen);
    endPtr = segArr + nSegments;
    for (segPtr = segArr; segPtr < endPtr; segPtr++) {
        MoveToEx(dc, segPtr->x1, segPtr->y1, NULL);
        LineTo(dc, segPtr->x2, segPtr->y2);
    }
    DeletePen(SelectPen(dc, oldPen));
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_EmulateXDrawPoints --
 *
 *      Uses the foreground pixel and function components of the GC to
 *      draw a multiple points into the specified drawable.
 *      CoordModeOrigin treats all coordinates as relative to the
 *      origin, and CoordModePrevious treats all coordinates after
 *      the first as relative to the previous point.
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      GC gc
 *      XPoint *pointArr
 *      int nPoints
 *      int mode
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Draws points on the specified drawable.
 *
 *----------------------------------------------------------------------
 */
void Rbc_EmulateXDrawPoints(Display *display, Drawable drawable, GC gc, XPoint *pointArr, int nPoints, int mode) {
    HDC dc;
    TkWinDCState state;
    int i;

    if ((drawable == None) || (pointArr == NULL) || (nPoints <= 0)) {
        return;
    }
    dc = TkWinGetDrawableDC(display, drawable, &state);
    SetROP2(dc, tkpWinRopModes[gc->function]);
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
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_EmultateXDrawString --
 *
 *      Draw a single string in the current font.
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      GC gc
 *      int x
 *      int y
 *      _Xconst char *string
 *      int length
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Renders the specified string in the drawable.
 *
 *----------------------------------------------------------------------
 */
void Rbc_EmulateXDrawString(Display *display, Drawable drawable, GC gc, int x, int y, _Xconst char *string,
                            int length) {
    if ((drawable == None) || (string == NULL) || (length <= 0)) {
        return;
    }
    Tk_DrawChars(display, drawable, gc, (Tk_Font)gc->font, string, length, x, y);
}
