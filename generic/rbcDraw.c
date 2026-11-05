/** @defgroup shared_draw_helpers Shared native drawing helpers
 * Private GC and dash support beneath component drawing.
 */

/*
 * rbcDraw.c --
 *
 *      Shared Tk/Xlib drawing and graphics-context utilities.
 *      Shared function contracts are declared in rbcInt.h.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include <X11/Xutil.h>
#include <math.h>

GC Rbc_GetPrivateGCFromDrawable(Display *display, Drawable drawable, unsigned long gcMask, XGCValues *valuePtr) {
    return XCreateGC(display, drawable, gcMask, valuePtr);
}

GC Rbc_GetPrivateGC(Tk_Window tkwin, unsigned long gcMask, XGCValues *valuePtr) {
    GC gc;
    Pixmap pixmap;
    Drawable drawable;
    Display *display;

    pixmap = None;
    drawable = Tk_WindowId(tkwin);
    display = Tk_Display(tkwin);
    if (drawable == None) {
        Drawable root;
        int depth;

        root = RootWindow(display, Tk_ScreenNumber(tkwin));
        depth = Tk_Depth(tkwin);
        if (depth == DefaultDepth(display, Tk_ScreenNumber(tkwin))) {
            drawable = root;
        } else {
            pixmap = Tk_GetPixmap(display, root, 1, 1, depth);
            drawable = pixmap;
        }
    }
    gc = Rbc_GetPrivateGCFromDrawable(display, drawable, gcMask, valuePtr);
    if (pixmap != None) {
        Tk_FreePixmap(display, pixmap);
    }
    return gc;
}

void Rbc_FreePrivateGC(Display *display, GC gc) {
    /* Tk 9 removed Tk_FreeXId; previously on 8.6 it was a NOOP

    Tk_FreeXId(display, (XID) XGContextFromGC(gc));

    This is the comment in the empty body of Tk_FreeXId on 8.6

     * This does nothing, because the XC-MISC extension takes care of
     * freeing XIDs for us.  It has been a standard X11 extension for
     * about 15 years as of 2008.  Keith Packard and another X.org
     * developer suggested that we remove the previous code that used:
     * #define XLIB_ILLEGAL_ACCESS.
     */
    XFreeGC(display, gc);
}

/**
 * @ingroup shared_draw_helpers
 * @brief Count dash bytes up to the first zero or the fixed descriptor capacity.
 *
 * Returns a count from zero through RBC_MAX_DASH_VALUES. Does not validate dash
 * lengths or alter the descriptor.
 * @param[in] dashesPtr Borrowed descriptor with readable values array; offset is not examined.
 */
static int GetDashCount(const Rbc_Dashes *dashesPtr) {
    int n;

    for (n = 0; n < RBC_MAX_DASH_VALUES; n++) {
        if (dashesPtr->values[n] == 0) {
            break;
        }
    }
    return n;
}

void Rbc_SetDashes(Display *display, GC gc, Rbc_Dashes *dashesPtr) {
    int nValues;

    nValues = GetDashCount(dashesPtr);
    XSetDashes(display, gc, dashesPtr->offset, (const char *)dashesPtr->values, nValues);
}

int Rbc_MaxRequestSize(Display *display, unsigned int elemSize) {
    long size;

#ifdef HAVE_XEXTENDEDMAXREQUESTSIZE
    size = XExtendedMaxRequestSize(display);
    if (size == 0) {
        size = XMaxRequestSize(display);
    }
#else
    size = XMaxRequestSize(display);
#endif
    if ((elemSize == 0) || (size <= 4)) {
        return 0;
    }
    size -= 4;
    if (size > (LONG_MAX / 4)) {
        return 0;
    }
    size *= 4;
    if ((unsigned long)size / elemSize > INT_MAX) {
        return INT_MAX;
    }
    return (int)((unsigned long)size / elemSize);
}

#undef Rbc_Draw3DRectangle
void Rbc_Draw3DRectangle(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                         int borderWidth, int relief) {
    if ((borderWidth > 1) && (width > 2) && (height > 2) &&
        ((relief == TK_RELIEF_SUNKEN) || (relief == TK_RELIEF_RAISED))) {
        GC lightGC, darkGC;
        int x2, y2;

        x2 = x + width - 1;
        y2 = y + height - 1;
        if (relief == TK_RELIEF_RAISED) {
            lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
#ifdef WIN32
            darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
#else
            darkGC = DefaultGC(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
#endif
        } else {
#ifdef WIN32
            lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC);
#else
            lightGC = DefaultGC(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
#endif
            darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
        }
        XDrawLine(Tk_Display(tkwin), drawable, darkGC, x2, y2, x2, y);
        XDrawLine(Tk_Display(tkwin), drawable, lightGC, x, y, x2, y);
        XDrawLine(Tk_Display(tkwin), drawable, darkGC, x2, y2, x, y2);
        XDrawLine(Tk_Display(tkwin), drawable, lightGC, x, y, x, y2);
        x++;
        y++;
        width -= 2;
        height -= 2;
        borderWidth--;
    }
    Tk_Draw3DRectangle(tkwin, drawable, border, x, y, width, height, borderWidth, relief);
}
