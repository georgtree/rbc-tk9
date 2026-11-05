/**
 * @file rbcText.c
 * @brief Shared multiline layout, anchor geometry and native Tk text drawing.
 * @defgroup text_drawing Text geometry and native drawing
 * @brief Private helpers; layouts borrow strings and styles borrow Tk resources.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include <X11/Xutil.h>
#include "rbcImage.h"
/*
 * TkDrawAngledChars is part of Tk's unsupported internal API.
 *
 * On MinGW, Tk's internal declarations redeclare several Xlib
 * compatibility functions with different dllimport attributes,
 * producing -Wattributes warnings inside Tk headers.
 */
#if defined(_WIN32) && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
#endif

#include <tkInt.h>

#if defined(_WIN32) && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <stddef.h>
#include <stdint.h>

#define WINDEBUG 0

static Tcl_HashTable bitmapGCTable;
static int initialized;

static void DrawTextLayout(Display *display, Drawable drawable, GC gc, Tk_Font font, register int x, register int y,
                           TextLayout *textPtr, double theta);


/**
 * @brief Saturate a wide signed coordinate to the native int range.
 * @ingroup text_drawing
 *
 * No allocation or interpreter error is involved; negative coordinates remain valid.
 *
 * @param[in] value Wide signed pixel coordinate, saturated to the native int range.
 */
static int TextLayoutInt(Tcl_WideInt value) {
    if (value > INT_MAX) {
        return INT_MAX;
    }
    if (value < INT_MIN) {
        return INT_MIN;
    }
    return (int)value;
}

/**
 * @brief Clamp a wide size to the nonnegative int range.
 * @ingroup text_drawing
 *
 * Nonpositive inputs become zero; values above INT_MAX become INT_MAX.
 *
 * @param[in] value Wide pixel extent, clamped to zero through INT_MAX.
 */
static int TextLayoutSize(Tcl_WideInt value) {
    if (value <= 0) {
        return 0;
    }
    if (value > INT_MAX) {
        return INT_MAX;
    }
    return (int)value;
}

/**
 * @brief Compute allocation bytes for a variable-length fragment array.
 * @ingroup text_drawing
 *
 * Uses offsetof(TextLayout, fragArr), including for zero fragments. Negative
 * counts or size_t overflow cause Tcl_Panic rather than a recoverable error.
 *
 * @param[in] nFrags Nonnegative number of TextFragment records; zero requires only the layout header.
 */
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

/**
 * @brief Round a rotated extent into bounded nonnegative integer geometry.
 * @ingroup text_drawing
 *
 * Nonfinite or nonpositive values become zero; large finite values saturate
 * at INT_MAX. Remaining values use the existing ROUND convention.
 *
 * @param[in] value Floating pixel extent; nonfinite or nonpositive values become zero.
 */
static int TextRotatedSize(double value) {
    if ((!FINITE(value)) || (value <= 0.0)) {
        return 0;
    }
    if (value >= (double)INT_MAX) {
        return INT_MAX;
    }
    return ROUND(value);
}

/**
 * @brief Round a floating coordinate with finite checks and int saturation.
 * @ingroup text_drawing
 *
 * Nonfinite values become zero; finite out-of-range values saturate at the
 * corresponding int endpoint. Remaining values use ROUND.
 *
 * @param[in] value Floating signed pixel coordinate; nonfinite values become zero.
 */
static int TextLayoutCoordinate(double value) {
    if (!FINITE(value)) {
        return 0;
    }
    if (value >= (double)INT_MAX) {
        return INT_MAX;
    }
    if (value <= (double)INT_MIN) {
        return INT_MIN;
    }
    return ROUND(value);
}

/**
 * @brief Draw borrowed fragments relative to an already anchored box origin.
 * @ingroup text_drawing
 *
 * For zero theta, add fragment baselines with saturated integer arithmetic
 * and call Tk_DrawChars. Otherwise rotate each baseline about the unrotated
 * box center, translate to the rounded rotated box, and call TkDrawAngledChars.
 * theta is in degrees and must satisfy the bounding-box helper preconditions.
 * The caller supplies a live drawable, compatible GC and font. This helper
 * neither selects foreground colors nor retains or frees layout resources.
 *
 * @param[in] display Display owning the destination and drawing resources.
 * @param[in] drawable Live destination drawable compatible with the supplied font and GC.
 * @param[in] gc Borrowed compatible GC with foreground already selected.
 * @param[in] font Borrowed Tk font used to draw the measured fragments.
 * @param[in] x Upper-left X coordinate of the rotated bounding box, already anchored.
 * @param[in] y Upper-left Y coordinate of the rotated bounding box, already anchored.
 * @param[in] textPtr Borrowed unrotated layout whose fragment strings remain alive and unchanged.
 * @param[in] theta Finite normalized rotation in degrees, in [0, 360).
 */
static void DrawTextLayout(Display *display, Drawable drawable, GC gc, Tk_Font font, int x, int y, TextLayout *textPtr,
                           double theta) {
    TextFragment *fragPtr;
    Tcl_Size i;
    int width;
    int height;
    int rW;
    int rH;
    double sinA;
    double cosA;
    double rotWidth;
    double rotHeight;

    width = 0;
    height = 0;
    rW = 0;
    rH = 0;
    if (theta != 0.0) {
        width = textPtr->width;
        height = textPtr->height;
        sinA = sin(theta * M_PI / 180.0);
        cosA = cos(theta * M_PI / 180.0);
        Rbc_GetBoundingBox(width, height, theta, &rotWidth, &rotHeight, (Point2D *)NULL);
        rW = TextRotatedSize(rotWidth);
        rH = TextRotatedSize(rotHeight);
    }
    fragPtr = textPtr->fragArr;
    for (i = 0; i < textPtr->nFrags; i++, fragPtr++) {
        if (theta == 0.0) {
            int drawX;
            int drawY;

            drawX = TextLayoutInt((Tcl_WideInt)x + (Tcl_WideInt)fragPtr->x);
            drawY = TextLayoutInt((Tcl_WideInt)y + (Tcl_WideInt)fragPtr->y);
            Tk_DrawChars(display, drawable, gc, font, fragPtr->text, fragPtr->count, drawX, drawY);
        } else {
            double realX;
            double realY;
            int newX;
            int newY;
            /*
             * Convert to double before subtracting half the layout
             * dimensions.  Doing the subtraction in int can overflow
             * for saturated fragment coordinates.
             */
            realX = (((double)fragPtr->x - ((double)width * 0.5)) * cosA) +
                    (((double)fragPtr->y - ((double)height * 0.5)) * sinA);
            realY = (((double)fragPtr->y - ((double)height * 0.5)) * cosA) -
                    (((double)fragPtr->x - ((double)width * 0.5)) * sinA);
            newX = TextLayoutCoordinate(realX + (double)x + ((double)rW * 0.5));
            newY = TextLayoutCoordinate(realY + (double)y + ((double)rH * 0.5));
            TkDrawAngledChars(display, drawable, gc, font, fragPtr->text, fragPtr->count, newX, newY, theta);
        }
    }
}

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

/**
 * @brief Compute the centered bounding box of a rotated rectangle.
 * @ingroup text_drawing
 *
 * width and height describe the unrotated rectangle. theta is in degrees;
 * callers supply a finite, normalized angle in [0, 360). Both dimension
 * outputs are required; bbox may be NULL or point to four Point2D entries.
 * Corners retain the original NW, NE, SE, SW indexing around the origin.
 * Positive angles rotate visually counterclockwise in screen coordinates.
 * Exact quadrants avoid trigonometric rounding. Other angles rotate each
 * corner and double the maximum positive coordinates. No resources are owned.
 *
 * @param[in] width Nonnegative unrotated width in pixels.
 * @param[in] height Nonnegative unrotated height in pixels.
 * @param[in] theta Finite normalized rotation in degrees, in [0, 360).
 * @param[out] rotWidthPtr Required destination for the floating axis-aligned rotated width.
 * @param[out] rotHeightPtr Required destination for the floating axis-aligned rotated height.
 * @param[out] bbox Optional array of four rotated corners centered at the origin, in original NW, NE, SE, SW
 *    order.
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

/**
 * @brief Translate an integer anchor position to the box upper-left corner.
 * @ingroup text_drawing
 *
 * Both output pointers are required. NULL and NW anchors leave the position
 * unchanged. Center offsets use integer division; subtraction is performed
 * in Tcl_WideInt and saturated to int. No drawing or allocation occurs.
 *
 * @param[in] x Horizontal anchor position in drawable pixels.
 * @param[in] y Vertical anchor position in drawable pixels.
 * @param[in] width Nonnegative box width in pixels.
 * @param[in] height Nonnegative box height in pixels.
 * @param[in] anchor Tk anchor locating the rotated box relative to the requested position.
 * @param[out] transXPtr Required destination for the saturated upper-left X coordinate.
 * @param[out] transYPtr Required destination for the saturated upper-left Y coordinate.
 */
void Rbc_TranslateAnchor(int x, int y, int width, int height, Tk_Anchor anchor, int *transXPtr, int *transYPtr) {
    Tcl_WideInt tx;
    Tcl_WideInt ty;

    tx = x;
    ty = y;

    switch (anchor) {
    case TK_ANCHOR_NULL:
    case TK_ANCHOR_NW:
        break;
    case TK_ANCHOR_W:
        ty -= (Tcl_WideInt)height / 2;
        break;
    case TK_ANCHOR_SW:
        ty -= (Tcl_WideInt)height;
        break;
    case TK_ANCHOR_N:
        tx -= (Tcl_WideInt)width / 2;
        break;
    case TK_ANCHOR_CENTER:
        tx -= (Tcl_WideInt)width / 2;
        ty -= (Tcl_WideInt)height / 2;
        break;
    case TK_ANCHOR_S:
        tx -= (Tcl_WideInt)width / 2;
        ty -= (Tcl_WideInt)height;
        break;
    case TK_ANCHOR_NE:
        tx -= (Tcl_WideInt)width;
        break;
    case TK_ANCHOR_E:
        tx -= (Tcl_WideInt)width;
        ty -= (Tcl_WideInt)height / 2;
        break;
    case TK_ANCHOR_SE:
        tx -= (Tcl_WideInt)width;
        ty -= (Tcl_WideInt)height;
        break;
    }
    *transXPtr = TextLayoutInt(tx);
    *transYPtr = TextLayoutInt(ty);
}

/**
 * @brief Return a floating upper-left position from a borrowed anchor point.
 * @ingroup text_drawing
 *
 * Does not modify pointPtr. Center offsets retain half-pixel fractions,
 * unlike Rbc_TranslateAnchor; no integer saturation is applied. NULL and
 * NW anchors return the original position.
 *
 * @param[in] pointPtr Required borrowed anchor point, left unchanged.
 * @param[in] width Nonnegative box width in pixels.
 * @param[in] height Nonnegative box height in pixels.
 * @param[in] anchor Tk anchor locating the rotated box relative to the requested position.
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

/**
 * @brief Set drawing attributes using borrowed font, colors and GC.
 * @ingroup text_drawing
 *
 * Applies Rbc_InitTextStyle defaults, then installs supplied attributes.
 * No resource is acquired or released, including a previously installed GC.
 * border, width and height remain untouched. A supplied borrowed GC must not
 * subsequently be released through Rbc_FreeTextStyle.
 *
 * @param[inout] tsPtr Caller-owned style receiving defaults and borrowed attributes; existing resources are
 *    not released.
 * @param[in] font Tk font borrowed for subsequent measurement and drawing.
 * @param[in] gc GC borrowed for drawing; no reference is acquired.
 * @param[in] normalColor Borrowed normal color, required by drawing paths that restore foreground.
 * @param[in] activeColor Borrowed active color, required when STATE_ACTIVE is used.
 * @param[in] shadowColor Borrowed shadow color, or NULL to disable the shadow pass.
 * @param[in] theta Rotation in degrees; supply a finite value with quotient by 360 representable as int before
 *    drawing.
 * @param[in] anchor Tk anchor locating the rotated box relative to the requested position.
 * @param[in] justify Horizontal justification of fragments within the unrotated layout.
 * @param[in] leader Extra interline spacing in display pixels.
 * @param[in] shadowOffset Shadow displacement in pixels; positive values enable a pass when a shadow color is
 *    present.
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

/**
 * @brief Set print attributes without acquiring native drawing resources.
 * @ingroup text_drawing
 *
 * Applies Rbc_InitTextStyle defaults, then borrows the supplied font and
 * colors. gc, border, width and height remain untouched; this does not
 * prepare an uninitialized style for native drawing or GC cleanup.
 *
 * @param[inout] tsPtr Caller-owned style receiving print attributes; gc, border and extents remain untouched.
 * @param[in] font Borrowed Tk font supplying measurement and export attributes.
 * @param[in] fgColor Borrowed foreground color; validity requirements belong to the chosen exporter.
 * @param[in] activeColor Borrowed active foreground color; this is not a background color.
 * @param[in] shadowColor Borrowed shadow color, or NULL for no shadow color.
 * @param[in] theta Rotation in degrees; supply a finite value with quotient by 360 representable as int before
 *    drawing.
 * @param[in] anchor Tk anchor locating the rotated box relative to the requested position.
 * @param[in] justify Horizontal justification of fragments within the unrotated layout.
 * @param[in] leader Extra interline spacing in display pixels.
 * @param[in] shadowOffset Shadow displacement in pixels; positive values enable a pass when a shadow color is
 *    present.
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

/**
 * @brief Draw a borrowed layout with anchor, rotation and style-state effects.
 * @ingroup text_drawing
 *
 * Requires a valid font and drawable-compatible GC. Normal color is needed
 * by color-restoration paths, activeColor for active text, and border for
 * disabled or emphasis text. Normalizes theta in place; callers must supply
 * a finite angle whose quotient by 360 is representable as int.
 * Disabled/emphasis rendering takes precedence over shadow and active text,
 * drawing two border-colored passes one pixel apart. Otherwise an optional
 * shadow precedes the normal or active pass. Changed foreground is reset to
 * the normal color, not the GC foreground on entry. The GC must permit this
 * mutation. Rotation uses Tk angled text, not a temporary bitmap.
 * The layout and style resources remain owned by the caller.
 *
 * @param[in] tkwin Live Tk window supplying the display and drawing resources; borrowed for the call.
 * @param[in] drawable Live destination drawable compatible with the supplied font and GC.
 * @param[in] textPtr Borrowed measured layout and live fragment strings; not freed or modified.
 * @param[inout] tsPtr Borrowed style with valid font, mutable GC and colors required by its state; theta is
 *    normalized in place.
 * @param[in] x Horizontal anchor position in drawable pixels.
 * @param[in] y Vertical anchor position in drawable pixels.
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
        DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font, TextLayoutInt((Tcl_WideInt)x + 1),
                       TextLayoutInt((Tcl_WideInt)y + 1), textPtr, theta);
        if (color2 != NULL) {
            XSetForeground(display, tsPtr->gc, color2->pixel);
        }
        DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font, x, y, textPtr, theta);
        XSetForeground(display, tsPtr->gc, tsPtr->color->pixel);
    } else {
        if ((tsPtr->shadow.offset > 0) && (tsPtr->shadow.color != NULL)) {
            XSetForeground(display, tsPtr->gc, tsPtr->shadow.color->pixel);
            DrawTextLayout(display, drawable, tsPtr->gc, tsPtr->font,
                           TextLayoutInt((Tcl_WideInt)x + (Tcl_WideInt)tsPtr->shadow.offset),
                           TextLayoutInt((Tcl_WideInt)y + (Tcl_WideInt)tsPtr->shadow.offset), textPtr, theta);
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

/**
 * @brief Draw a temporary layout and optionally return its rotated dimensions.
 * @ingroup text_drawing
 *
 * NULL or empty text draws nothing and sets a supplied areaPtr to zero.
 * Nonempty text uses Rbc_DrawTextLayout, inheriting its style requirements
 * and theta mutation. areaPtr may be NULL; dimensions use rounded rotated
 * extents and exclude anchor position. Frees only the temporary layout.
 *
 * @param[in] tkwin Live Tk window supplying the display and drawing resources; borrowed for the call.
 * @param[in] drawable Live destination drawable compatible with the supplied font and GC.
 * @param[in] string Borrowed NUL-terminated Tcl UTF text; NULL or empty text draws nothing.
 * @param[inout] tsPtr Borrowed style with valid font, mutable GC and colors required by its state; theta is
 *    normalized in place.
 * @param[in] x Horizontal anchor position in drawable pixels.
 * @param[in] y Vertical anchor position in drawable pixels.
 * @param[out] areaPtr Optional rotated bounding dimensions in pixels; initialized to zero even when no text is
 *    drawn.
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

/**
 * @brief Draw nonempty text using a temporary borrowed-string layout.
 * @ingroup text_drawing
 *
 * NULL or empty text is a no-op. Otherwise delegates to Rbc_DrawTextLayout
 * and frees the layout, retaining the caller-owned string, font and colors.
 * Inherits the drawing helper requirements and theta/GC side effects.
 *
 * @param[in] tkwin Live Tk window supplying the display and drawing resources; borrowed for the call.
 * @param[in] drawable Live destination drawable compatible with the supplied font and GC.
 * @param[in] string Borrowed NUL-terminated Tcl UTF text; NULL or empty text draws nothing.
 * @param[inout] tsPtr Borrowed style with valid font, mutable GC and colors required by its state; theta is
 *    normalized in place.
 * @param[in] x Horizontal anchor position in drawable pixels.
 * @param[in] y Vertical anchor position in drawable pixels.
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

/**
 * @brief Borrow a cached depth-one GC keyed by Display pointer.
 * @ingroup text_drawing
 *
 * Creates the GC from a temporary one-pixel bitmap on the first request for
 * a display, initially with foreground and background zero. Later calls
 * return the same mutable GC without resetting its attributes. Callers must
 * not free it and must establish all state needed for their operation.
 * The process-static table has no cleanup or synchronization here; it is
 * not a per-window, per-screen or per-interpreter ownership mechanism.
 *
 * @param[in] tkwin Window selecting the display cache key and, on first use, the screen for GC creation.
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

void Rbc_FreeTextStyle(Display *display, TextStyle *tsPtr) {
    if (tsPtr->gc != NULL) {
        Tk_FreeGC(display, tsPtr->gc);
    }
}
