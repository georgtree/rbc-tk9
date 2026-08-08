/*
 * rbcText.c --
 *
 *      This module implements multi-line, rotate-able text for the rbc toolkit.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include <X11/Xutil.h>
#include "rbcImage.h"
#include <tkInt.h> /* needed for TkDrawAngledChars */
#include <stddef.h>
#include <stdint.h>

#define WINDEBUG 0

static Tcl_HashTable bitmapGCTable;
static int initialized;

static void DrawTextLayout(Display *display, Drawable drawable, GC gc, Tk_Font font, register int x, register int y,
                           TextLayout *textPtr, double theta);


static int TextLayoutInt(Tcl_WideInt value) {
    if (value > INT_MAX) {
        return INT_MAX;
    }
    if (value < INT_MIN) {
        return INT_MIN;
    }
    return (int)value;
}

static int TextLayoutSize(Tcl_WideInt value) {
    if (value <= 0) {
        return 0;
    }
    if (value > INT_MAX) {
        return INT_MAX;
    }
    return (int)value;
}

static size_t GetTextLayoutByteCount(Tcl_Size nFrags) {
    size_t headerSize;

    if (nFrags < 0) {
        Tcl_Panic("GetTextLayoutByteCount: negative fragment count");
    }
    headerSize = offsetof(TextLayout, fragArr);
    if ((size_t)nFrags > (SIZE_MAX - headerSize) / sizeof(TextFragment)) {
        Tcl_Panic("GetTextLayoutByteCount: layout size overflow");
    }
    return headerSize + ((size_t)nFrags * sizeof(TextFragment));
}

static int TextRotatedSize(double value) {
    if ((!FINITE(value)) || (value <= 0.0)) {
        return 0;
    }
    if (value >= (double)INT_MAX) {
        return INT_MAX;
    }
    return ROUND(value);
}

/*
 *--------------------------------------------------------------
 *
 * DrawTextLayout --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      GC gc
 *      Tk_Font font
 *      int x
 *      int y - Origin of text
 *      TextLayout *textPtr
 *      double theta
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void DrawTextLayout(Display *display, Drawable drawable, GC gc, Tk_Font font, int x, int y, TextLayout *textPtr,
                           double theta) {
    TextFragment *fragPtr;
    Tcl_Size i;
    int newX = 0, newY = 0, width = 0, height = 0, rW = 0, rH = 0;
    double realX, realY;
    double sinA, cosA;
    double rotWidth, rotHeight;

    if (theta != 0.0) {
        sinA = sin(theta * M_PI / 180.0);
        cosA = cos(theta * M_PI / 180.0);
        width = textPtr->width;
        height = textPtr->height;
        Rbc_GetBoundingBox(width, height, theta, &rotWidth, &rotHeight, (Point2D *)NULL);
        rW = TextRotatedSize(rotWidth);
        rH = TextRotatedSize(rotHeight);
    }
    fragPtr = textPtr->fragArr;
    for (i = 0; i < textPtr->nFrags; i++, fragPtr++) {
        if (theta == 0.0) {
            Tk_DrawChars(display, drawable, gc, font, fragPtr->text, fragPtr->count, x + fragPtr->x, y + fragPtr->y);
        } else {
            realX = (fragPtr->x - width / 2) * cosA + (fragPtr->y - height / 2) * sinA;
            realY = (fragPtr->y - height / 2) * cosA - (fragPtr->x - width / 2) * sinA;
            newX = ROUND(realX) + x + rW / 2;
            newY = ROUND(realY) + y + rH / 2;
            TkDrawAngledChars(display, drawable, gc, font, fragPtr->text, fragPtr->count, newX, newY, theta);
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_GetTextLayout --
 *
 *      Get the extents of a possibly multiple-lined text string.
 *
 * Parameters:
 *      char string[]
 *      TextStyle *tsPtr
 *
 * Results:
 *      Returns via *widthPtr* and *heightPtr* the dimensions of
 *      the text string.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
TextLayout *Rbc_GetTextLayout(const char *string, TextStyle *tsPtr) {
    const char *lineStart;
    const char *p;
    TextFragment *fragPtr;
    TextLayout *textPtr;
    Tk_FontMetrics fontMetrics;
    Tcl_Size count;
    Tcl_Size i;
    Tcl_Size nFrags;
    Tcl_Size populated;
    Tcl_WideInt height;
    Tcl_WideInt value;
    int lineHeight;
    int maxWidth;
    int width;
    size_t size;

    if (string == NULL) {
        string = "";
    }
    Tk_GetFontMetrics(tsPtr->font, &fontMetrics);
    value = (Tcl_WideInt)fontMetrics.linespace + (Tcl_WideInt)tsPtr->leader + (Tcl_WideInt)tsPtr->shadow.offset;
    lineHeight = TextLayoutSize(value);
    /*
     * Each newline terminates one fragment.  If the string does not
     * end in a newline, the final sequence is another fragment.
     */
    nFrags = 0;
    for (p = string; *p != '\0'; p++) {
        if (*p == '\n') {
            if (nFrags == TCL_SIZE_MAX) {
                Tcl_Panic("Rbc_GetTextLayout: fragment count overflow");
            }
            nFrags++;
        }
    }
    if ((p != string) && (*(p - 1) != '\n')) {
        if (nFrags == TCL_SIZE_MAX) {
            Tcl_Panic("Rbc_GetTextLayout: fragment count overflow");
        }
        nFrags++;
    }
    size = GetTextLayoutByteCount(nFrags);
    textPtr = RbcCalloc(1, size);
    textPtr->nFrags = nFrags;
    maxWidth = 0;
    height = tsPtr->padTop;
    lineStart = string;
    count = 0;
    populated = 0;
    fragPtr = textPtr->fragArr;
    for (p = string; *p != '\0'; p++) {
        if (*p == '\n') {
            /*
             * Reset width explicitly.  Otherwise an empty line after
             * a non-empty line inherits the preceding line's width.
             */
            width = 0;
            if (count > 0) {
                value = (Tcl_WideInt)Tk_TextWidth(tsPtr->font, lineStart, count) + (Tcl_WideInt)tsPtr->shadow.offset;
                width = TextLayoutSize(value);
                if (width > maxWidth) {
                    maxWidth = width;
                }
            }
            assert(populated < textPtr->nFrags);
            fragPtr->text = lineStart;
            fragPtr->count = count;
            fragPtr->width = width;
            value = height + (Tcl_WideInt)fontMetrics.ascent;
            fragPtr->y = TextLayoutInt(value);
            fragPtr++;
            populated++;
            height += (Tcl_WideInt)lineHeight;
            if (height > INT_MAX) {
                height = INT_MAX;
            } else if (height < INT_MIN) {
                height = INT_MIN;
            }
            lineStart = p + 1;
            count = 0;
            continue;
        }
        if (count == TCL_SIZE_MAX) {
            ckfree(textPtr);
            Tcl_Panic("Rbc_GetTextLayout: line length overflow");
        }
        count++;
    }
    if (populated < textPtr->nFrags) {
        value = (Tcl_WideInt)Tk_TextWidth(tsPtr->font, lineStart, count) + (Tcl_WideInt)tsPtr->shadow.offset;
        width = TextLayoutSize(value);
        if (width > maxWidth) {
            maxWidth = width;
        }
        fragPtr->text = lineStart;
        fragPtr->count = count;
        fragPtr->width = width;
        value = height + (Tcl_WideInt)fontMetrics.ascent;
        fragPtr->y = TextLayoutInt(value);
        height += (Tcl_WideInt)lineHeight;
        if (height > INT_MAX) {
            height = INT_MAX;
        } else if (height < INT_MIN) {
            height = INT_MIN;
        }
        populated++;
    }
    assert(populated == textPtr->nFrags);
    value = (Tcl_WideInt)maxWidth + (Tcl_WideInt)tsPtr->padX.side1 + (Tcl_WideInt)tsPtr->padX.side2;
    textPtr->width = TextLayoutSize(value);
    height += (Tcl_WideInt)tsPtr->padBottom;
    if (textPtr->nFrags > 0) {
        height -= (Tcl_WideInt)tsPtr->leader;
    }
    textPtr->height = TextLayoutSize(height);
    fragPtr = textPtr->fragArr;
    for (i = 0; i < textPtr->nFrags; i++, fragPtr++) {
        switch (tsPtr->justify) {
        default:
        case TK_JUSTIFY_LEFT:
            fragPtr->x = tsPtr->padLeft;
            break;
        case TK_JUSTIFY_RIGHT:
            fragPtr->x =
                TextLayoutInt((Tcl_WideInt)textPtr->width - (Tcl_WideInt)fragPtr->width - (Tcl_WideInt)tsPtr->padRight);
            break;
        case TK_JUSTIFY_CENTER:
            fragPtr->x = TextLayoutInt(((Tcl_WideInt)textPtr->width - (Tcl_WideInt)fragPtr->width) / 2);
            break;
        }
    }
    return textPtr;
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_GetTextExtents --
 *
 *      Get the extents of a possibly multiple-lined text string.
 *
 * Parameters:
 *      TextStyle *tsPtr
 *      char *string
 *      int *widthPtr
 *      int *heightPtr
 *
 * Results:
 *      Returns via *widthPtr* and *heightPtr* the dimensions of
 *      the text string.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void Rbc_GetTextExtents(TextStyle *tsPtr, const char *string, int *widthPtr, int *heightPtr) {
    const char *lineStart;
    const char *p;
    Tk_FontMetrics fontMetrics;
    Tcl_Size count;
    Tcl_WideInt height;
    Tcl_WideInt lineHeight;
    Tcl_WideInt value;
    int width;
    int w;

    if (string == NULL) {
        *widthPtr = 0;
        *heightPtr = 0;
        return;
    }
    Tk_GetFontMetrics(tsPtr->font, &fontMetrics);
    lineHeight = (Tcl_WideInt)fontMetrics.linespace + (Tcl_WideInt)tsPtr->leader + (Tcl_WideInt)tsPtr->shadow.offset;
    if (lineHeight < 0) {
        lineHeight = 0;
    } else if (lineHeight > INT_MAX) {
        lineHeight = INT_MAX;
    }
    count = 0;
    width = 0;
    height = 0;
    lineStart = string;
    for (p = string; *p != '\0'; p++) {
        if (*p == '\n') {
            if (count > 0) {
                value = (Tcl_WideInt)Tk_TextWidth(tsPtr->font, lineStart, count) + (Tcl_WideInt)tsPtr->shadow.offset;
                w = TextLayoutSize(value);
                if (w > width) {
                    width = w;
                }
            }
            height += lineHeight;
            if (height > INT_MAX) {
                height = INT_MAX;
            }
            lineStart = p + 1;
            count = 0;
            continue;
        }
        if (count == TCL_SIZE_MAX) {
            Tcl_Panic("Rbc_GetTextExtents: line length overflow");
        }
        count++;
    }
    if ((count > 0) && ((p == string) || (*(p - 1) != '\n'))) {
        value = (Tcl_WideInt)Tk_TextWidth(tsPtr->font, lineStart, count) + (Tcl_WideInt)tsPtr->shadow.offset;
        w = TextLayoutSize(value);
        if (w > width) {
            width = w;
        }
        height += lineHeight;
        if (height > INT_MAX) {
            height = INT_MAX;
        }
    }
    value = (Tcl_WideInt)width + (Tcl_WideInt)tsPtr->padX.side1 + (Tcl_WideInt)tsPtr->padX.side2;
    *widthPtr = TextLayoutSize(value);
    value = height + (Tcl_WideInt)tsPtr->padY.side1 + (Tcl_WideInt)tsPtr->padY.side2;
    *heightPtr = TextLayoutSize(value);
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_GetBoundingBox
 *
 *      Computes the dimensions of the bounding box surrounding a
 *      rectangle rotated about its center.  If pointArr isn't NULL,
 *      the coordinates of the rotated rectangle are also returned.
 *
 *      The dimensions are determined by rotating the rectangle, and
 *      doubling the maximum x-coordinate and y-coordinate.
 *
 *        w = 2 * maxX,  h = 2 * maxY
 *
 *      Since the rectangle is centered at 0,0, the coordinates of
 *      the bounding box are (-w/2,-h/2 w/2,-h/2, w/2,h/2 -w/2,h/2).
 *
 *        0 ------- 1
 *        |         |
 *        |    x    |
 *        |         |
 *        3 ------- 2
 *
 * Parameters:
 *      int width - -
 *      int height - Unrotated region
 *      double theta - Rotation of box
 *      double *rotWidthPtr - (out) Bounding box region
 *      double *rotHeightPtr - (out) Bounding box region
 *      Point2D *bbox - (out) Points of the rotated box
 *
 * Results:
 *      The width and height of the bounding box containing the
 *      rotated rectangle are returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void Rbc_GetBoundingBox(int width, int height, double theta, double *rotWidthPtr, double *rotHeightPtr, Point2D *bbox) {
    register int i;
    double sinTheta, cosTheta;
    double xMax, yMax;
    register double x, y;
    Point2D corner[4];

    theta = FMOD(theta, 360.0);
    if (FMOD(theta, (double)90.0) == 0.0) {
        int ll, ur, ul, lr;
        double rotWidth, rotHeight;
        int quadrant;

        /* Handle right-angle rotations specifically */
        quadrant = (int)(theta / 90.0);
        switch (quadrant) {
        case ROTATE_270: /* 270 degrees */
            ul = 3, ur = 0, lr = 1, ll = 2;
            rotWidth = (double)height;
            rotHeight = (double)width;
            break;
        case ROTATE_90: /* 90 degrees */
            ul = 1, ur = 2, lr = 3, ll = 0;
            rotWidth = (double)height;
            rotHeight = (double)width;
            break;
        case ROTATE_180: /* 180 degrees */
            ul = 2, ur = 3, lr = 0, ll = 1;
            rotWidth = (double)width;
            rotHeight = (double)height;
            break;
        default:
        case ROTATE_0: /* 0 degrees */
            ul = 0, ur = 1, lr = 2, ll = 3;
            rotWidth = (double)width;
            rotHeight = (double)height;
            break;
        }
        if (bbox != NULL) {
            x = rotWidth * 0.5;
            y = rotHeight * 0.5;
            bbox[ll].x = bbox[ul].x = -x;
            bbox[ur].y = bbox[ul].y = -y;
            bbox[lr].x = bbox[ur].x = x;
            bbox[ll].y = bbox[lr].y = y;
        }
        *rotWidthPtr = rotWidth;
        *rotHeightPtr = rotHeight;
        return;
    }
    /* Set the four corners of the rectangle whose center is the origin */
    corner[1].x = corner[2].x = (double)width * 0.5;
    corner[0].x = corner[3].x = -corner[1].x;
    corner[2].y = corner[3].y = (double)height * 0.5;
    corner[0].y = corner[1].y = -corner[2].y;
    theta = (-theta / 180.0) * M_PI;
    sinTheta = sin(theta), cosTheta = cos(theta);
    xMax = yMax = 0.0;
    /* Rotate the four corners and find the maximum X and Y coordinates */
    for (i = 0; i < 4; i++) {
        x = (corner[i].x * cosTheta) - (corner[i].y * sinTheta);
        y = (corner[i].x * sinTheta) + (corner[i].y * cosTheta);
        if (x > xMax) {
            xMax = x;
        }
        if (y > yMax) {
            yMax = y;
        }
        if (bbox != NULL) {
            bbox[i].x = x;
            bbox[i].y = y;
        }
    }
    /*
     * By symmetry, the width and height of the bounding box are
     * twice the maximum x and y coordinates.
     */
    *rotWidthPtr = xMax + xMax;
    *rotHeightPtr = yMax + yMax;
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_TranslateAnchor --
 *
 *      Translate the coordinates of a given bounding box based
 *      upon the anchor specified.  The anchor indicates where
 *      the given xy position is in relation to the bounding box.
 *
 *        nw --- n --- ne
 *        |            |
 *        w   center   e
 *        |            |
 *        sw --- s --- se
 *
 *      The coordinates returned are translated to the origin of the
 *      bounding box (suitable for giving to XCopyArea, XCopyPlane, etc.)
 *
 * Parameters:
 *      int x - -
 *      int y - Window coordinates of anchor
 *      int width - -
 *      int height - Extents of the bounding box
 *      Tk_Anchor anchor - Direction of the anchor
 *      int *transXPtr
 *      int *transYPtr
 *
 * Results:
 *      The translated coordinates of the bounding box are returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void Rbc_TranslateAnchor(int x, int y, int width, int height, Tk_Anchor anchor, int *transXPtr, int *transYPtr) {
    switch (anchor) {
    case TK_ANCHOR_NULL:
    case TK_ANCHOR_NW: /* Upper left corner */
        break;
    case TK_ANCHOR_W: /* Left center */
        y -= (height / 2);
        break;
    case TK_ANCHOR_SW: /* Lower left corner */
        y -= height;
        break;
    case TK_ANCHOR_N: /* Top center */
        x -= (width / 2);
        break;
    case TK_ANCHOR_CENTER: /* Center */
        x -= (width / 2);
        y -= (height / 2);
        break;
    case TK_ANCHOR_S: /* Bottom center */
        x -= (width / 2);
        y -= height;
        break;
    case TK_ANCHOR_NE: /* Upper right corner */
        x -= width;
        break;
    case TK_ANCHOR_E: /* Right center */
        x -= width;
        y -= (height / 2);
        break;
    case TK_ANCHOR_SE: /* Lower right corner */
        x -= width;
        y -= height;
        break;
    }
    *transXPtr = x;
    *transYPtr = y;
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_TranslatePoint --
 *
 *      Translate the coordinates of a given bounding box based
 *      upon the anchor specified.  The anchor indicates where
 *      the given xy position is in relation to the bounding box.
 *
 *        nw --- n --- ne
 *        |            |
 *        w   center   e
 *        |            |
 *        sw --- s --- se
 *
 *      The coordinates returned are translated to the origin of the
 *      bounding box (suitable for giving to XCopyArea, XCopyPlane, etc.)
 *
 * Parameters:
 *      Point2D *pointPtr - Window coordinates of anchor
 *      int width - -
 *      int height - Extents of the bounding box
 *      Tk_Anchor anchor - Direction of the anchor
 *
 * Results:
 *      The translated coordinates of the bounding box are returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
Point2D Rbc_TranslatePoint(Point2D *pointPtr, int width, int height, Tk_Anchor anchor) {
    Point2D trans;

    trans = *pointPtr;
    switch (anchor) {
    case TK_ANCHOR_NULL:
    case TK_ANCHOR_NW: /* Upper left corner */
        break;
    case TK_ANCHOR_W: /* Left center */
        trans.y -= (height * 0.5);
        break;
    case TK_ANCHOR_SW: /* Lower left corner */
        trans.y -= height;
        break;
    case TK_ANCHOR_N: /* Top center */
        trans.x -= (width * 0.5);
        break;
    case TK_ANCHOR_CENTER: /* Center */
        trans.x -= (width * 0.5);
        trans.y -= (height * 0.5);
        break;
    case TK_ANCHOR_S: /* Bottom center */
        trans.x -= (width * 0.5);
        trans.y -= height;
        break;
    case TK_ANCHOR_NE: /* Upper right corner */
        trans.x -= width;
        break;
    case TK_ANCHOR_E: /* Right center */
        trans.x -= width;
        trans.y -= (height * 0.5);
        break;
    case TK_ANCHOR_SE: /* Lower right corner */
        trans.x -= width;
        trans.y -= height;
        break;
    }
    return trans;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_InitTextStyle --
 *
 *      TODO: Description
 *
 * Parameters:
 *      TextStyle *tsPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_InitTextStyle(TextStyle *tsPtr) {
    /* Initialize these attributes to zero */
    tsPtr->activeColor = (XColor *)NULL;
    tsPtr->anchor = TK_ANCHOR_CENTER;
    tsPtr->color = (XColor *)NULL;
    tsPtr->font = NULL;
    tsPtr->justify = TK_JUSTIFY_CENTER;
    tsPtr->leader = 0;
    tsPtr->padLeft = tsPtr->padRight = 0;
    tsPtr->padTop = tsPtr->padBottom = 0;
    tsPtr->shadow.color = (XColor *)NULL;
    tsPtr->shadow.offset = 0;
    tsPtr->state = 0;
    tsPtr->theta = 0.0;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_SetDrawTextStyle --
 *
 *      TODO: Description
 *
 * Parameters:
 *      TextStyle *tsPtr
 *      Tk_Font font
 *      GC gc
 *      XColor *normalColor
 *      XColor *activeColor
 *      XColor *shadowColor
 *      double theta
 *      Tk_Anchor anchor
 *      Tk_Justify justify
 *      int leader
 *      int shadowOffset
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_SetDrawTextStyle(TextStyle *tsPtr, Tk_Font font, GC gc, XColor *normalColor, XColor *activeColor,
                          XColor *shadowColor, double theta, Tk_Anchor anchor, Tk_Justify justify, int leader,
                          int shadowOffset) {
    Rbc_InitTextStyle(tsPtr);
    tsPtr->activeColor = activeColor;
    tsPtr->anchor = anchor;
    tsPtr->color = normalColor;
    tsPtr->font = font;
    tsPtr->gc = gc;
    tsPtr->justify = justify;
    tsPtr->leader = leader;
    tsPtr->shadow.color = shadowColor;
    tsPtr->shadow.offset = shadowOffset;
    tsPtr->theta = theta;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_SetPrintTextStyle --
 *
 *      TODO: Description
 *
 * Parameters:
 *      TextStyle *tsPtr
 *      Tk_Font font
 *      GC gc
 *      XColor *fgColor
 *      XColor *activeColor
 *      XColor *shadowColor
 *      double theta
 *      Tk_Anchor anchor
 *      Tk_Justify justify
 *      int leader
 *      int shadowOffset
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_SetPrintTextStyle(TextStyle *tsPtr, Tk_Font font, XColor *fgColor, XColor *activeColor, XColor *shadowColor,
                           double theta, Tk_Anchor anchor, Tk_Justify justify, int leader, int shadowOffset) {
    Rbc_InitTextStyle(tsPtr);
    tsPtr->color = fgColor;
    tsPtr->activeColor = activeColor;
    tsPtr->shadow.color = shadowColor;
    tsPtr->font = font;
    tsPtr->theta = theta;
    tsPtr->anchor = anchor;
    tsPtr->justify = justify;
    tsPtr->leader = leader;
    tsPtr->shadow.offset = shadowOffset;
}

/*
 * -----------------------------------------------------------------
 *
 * Rbc_DrawTextLayout --
 *
 *      Draw a text string, possibly rotated, using the the given
 *      window coordinates as an anchor for the text bounding box.
 *      If the text is not rotated, simply use the X text drawing
 *      routines. Otherwise, generate a bitmap of the rotated text.
 *
 * Parameters:
 *      Tk_Window tkwin
 *      Drawable drawable
 *      TextLayout *textPtr
 *      TextStyle *tsPtr - Text attribute information
 *      int x - -
 *      int y - Window coordinates to draw text
 *
 * Results:
 *      Returns the x-coordinate to the right of the text.
 *
 * Side Effects:
 *      Text string is drawn using the given font and GC at the
 *      the given window coordinates.
 *
 *      The Stipple, FillStyle, and TSOrigin fields of the GC are
 *      modified for rotated text.  This assumes the GC is private,
 *      *not* shared (via Tk_GetGC)
 *
 * -----------------------------------------------------------------
 */
void Rbc_DrawTextLayout(Tk_Window tkwin, Drawable drawable, TextLayout *textPtr, TextStyle *tsPtr, int x, int y) {
    double theta;
    Display *display;
    int active;
    double rotWidth, rotHeight;
    int newWidth, newHeight;

    display = Tk_Display(tkwin);
    theta = FMOD(tsPtr->theta, (double)360.0);
    if (theta < 0.0) {
        theta += 360.0;
    }
    tsPtr->theta = theta;
    active = tsPtr->state & STATE_ACTIVE;
    Rbc_GetBoundingBox(textPtr->width, textPtr->height, theta, &rotWidth, &rotHeight, (Point2D *)NULL);
    newWidth = TextRotatedSize(rotWidth);
    newHeight = TextRotatedSize(rotHeight);
    Rbc_TranslateAnchor(x, y, newWidth, newHeight, tsPtr->anchor, &x, &y);
    if (tsPtr->state & (STATE_DISABLED | STATE_EMPHASIS)) {
        XColor xcolor1, xcolor2, *color1, *color2;
        Tk_Get3DBorderColors(tsPtr->border, NULL, &xcolor2, &xcolor1);

        color1 = &xcolor1;
        color2 = &xcolor2;
        if (tsPtr->state & STATE_EMPHASIS) {
            XColor *hold;
            hold = color1, color1 = color2, color2 = hold;
        }
        if (color1 != NULL) {
            XSetForeground(display, tsPtr->gc, color1->pixel);
        }
        DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font, x + 1, y + 1, textPtr, theta);
        if (color2 != NULL) {
            XSetForeground(display, tsPtr->gc, color2->pixel);
        }
        DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font, x, y, textPtr, theta);
        XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);
    } else {
        if ((tsPtr->shadow.offset > 0) && (tsPtr->shadow.color != NULL)) {
            XSetForeground(display, tsPtr->gc, tsPtr->shadow.color->pixel);
            DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font, x + tsPtr->shadow.offset,
                           y + tsPtr->shadow.offset, textPtr, theta);
            XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);
        }
        if (active) {
            XSetForeground(display, tsPtr->gc, tsPtr->activeColor->pixel);
        }
        DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font, x, y, textPtr, theta);
        if (active) {
            XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);
        }
    }
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_DrawText2 --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *      Drawable drawable
 *      char string[]
 *      TextStyle *tsPtr - Text attribute information
 *      int x - -
 *      int y - Window coordinates to draw text
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_DrawText2(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *tsPtr, int x, int y,
                   Dim2D *areaPtr) {
    TextLayout *textPtr;
    int width, height;
    double theta;

    if (areaPtr != NULL) {
        areaPtr->width = 0;
        areaPtr->height = 0;
    }
    if ((string == NULL) || (*string == '\0')) {
        return;
    }
    textPtr = Rbc_GetTextLayout(string, tsPtr);
    Rbc_DrawTextLayout(tkwin, drawable, textPtr, tsPtr, x, y);
    theta = FMOD(tsPtr->theta, (double)360.0);
    if (theta < 0.0) {
        theta += 360.0;
    }
    width = textPtr->width;
    height = textPtr->height;
    if (theta != 0.0) {
        double rotWidth, rotHeight;

        Rbc_GetBoundingBox(width, height, theta, &rotWidth, &rotHeight, (Point2D *)NULL);
        width = TextRotatedSize(rotWidth);
        height = TextRotatedSize(rotHeight);
    }
    if (areaPtr != NULL) {
        areaPtr->width = width;
        areaPtr->height = height;
    }
    ckfree(textPtr);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_DrawText --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *      Drawable drawable
 *      char string[]
 *      TextStyle *tsPtr - Text attribute information
 *      int x - -
 *      int y - Window coordinates to draw text
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_DrawText(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *tsPtr, int x, int y) {
    TextLayout *textPtr;

    if ((string == NULL) || (*string == '\0')) {
        return;
    }
    textPtr = Rbc_GetTextLayout(string, tsPtr);
    Rbc_DrawTextLayout(tkwin, drawable, textPtr, tsPtr, x, y);
    ckfree(textPtr);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_GetBitmapGC --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
GC Rbc_GetBitmapGC(Tk_Window tkwin) {
    int isNew;
    GC gc;
    Display *display;
    Tcl_HashEntry *hPtr;

    if (!initialized) {
        Tcl_InitHashTable(&bitmapGCTable, TCL_ONE_WORD_KEYS);
        initialized = TRUE;
    }
    display = Tk_Display(tkwin);
    hPtr = Tcl_CreateHashEntry(&bitmapGCTable, (char *)display, &isNew);
    if (isNew) {
        Pixmap bitmap;
        XGCValues gcValues;
        unsigned long gcMask;
        Window root;

        root = RootWindow(display, Tk_ScreenNumber(tkwin));
        bitmap = Tk_GetPixmap(display, root, 1, 1, 1);
        gcValues.foreground = gcValues.background = 0;
        gcMask = (GCForeground | GCBackground);
        gc = Rbc_GetPrivateGCFromDrawable(display, bitmap, gcMask, &gcValues);
        Tk_FreePixmap(display, bitmap);
        Tcl_SetHashValue(hPtr, gc);
    } else {
        gc = (GC)Tcl_GetHashValue(hPtr);
    }
    return gc;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ResetTextStyle --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *      TextStyle *tsPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_ResetTextStyle(Tk_Window tkwin, TextStyle *tsPtr) {
    GC newGC;
    XGCValues gcValues;
    unsigned long gcMask;

    gcMask = GCFont;
    gcValues.font = Tk_FontId(tsPtr->font);
    if (tsPtr->color != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = tsPtr->color->pixel;
    }
    newGC = Tk_GetGC(tkwin, gcMask, &gcValues);
    if (tsPtr->gc != NULL) {
        Tk_FreeGC(Tk_Display(tkwin), tsPtr->gc);
    }
    tsPtr->gc = newGC;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_FreeTextStyle --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      TextStyle *tsPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_FreeTextStyle(Display *display, TextStyle *tsPtr) {
    if (tsPtr->gc != NULL) {
        Tk_FreeGC(display, tsPtr->gc);
    }
}
