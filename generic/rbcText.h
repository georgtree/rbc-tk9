/**
 * @file rbcText.h
 * @brief Private multiline text layout and style resources.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCTEXT
#define _RBCTEXT

#include <tk.h>
#include "rbcTypes.h"

#define DEF_TEXT_FLAGS (TK_PARTIAL_OK | TK_IGNORE_NEWLINES)

/*
 * ----------------------------------------------------------------------
 *
 * TextFragment --
 *
 * ----------------------------------------------------------------------
 */
typedef struct {
    const char *text; /* Text to be displayed. */
    int x, y; /* X-Y offset of the baseline from the
               * upper-left corner of the bounding box. */
    int sx, sy; /* Reserved screen-coordinate fields. */
    Tcl_Size count; /* Number of bytes in text. The actual
                     * character count may differ because of
                     * multi-byte UTF encodings. */
    int width; /* Width of segment in pixels. This is used
                * to draw PostScript strings at the same
                * width as the screen representation. */
} TextFragment;

/*
 * ----------------------------------------------------------------------
 *
 * TextLayout --
 *
 * ----------------------------------------------------------------------
 */
typedef struct {
    Tcl_Size nFrags;         /* Number of text fragments. */
    int width, height;       /* Dimensions of text bounding box. */
    TextFragment fragArr[1]; /* Information about each fragment. */
} TextLayout;

typedef struct {
    XColor *color;
    int offset;
} Shadow;

/*
 * ----------------------------------------------------------------------
 *
 * TextStyle --
 *
 *     Represents a convenient structure to hold text attributes
 *    which determine how a text string is to be displayed on the
 *    window, or drawn with PostScript commands.  The alternative
 *    is to pass lots of parameters to the drawing and printing
 *    routines. This seems like a more efficient and less cumbersome
 *    way of passing parameters.
 *
 * ----------------------------------------------------------------------
 */
typedef struct {
    unsigned int state; /* If non-zero, indicates to draw text
                         * in the active color */
    int width, height;  /* Extents of text */
    XColor *color;       /* Normal color */
    XColor *activeColor; /* Active color */
    Tk_Font font;        /* Font to use to draw text */
    Tk_3DBorder border;  /* Background color of text.  This is also
                          * used for drawing disabled text. */
    Shadow shadow;       /* Drop shadow color and offset */
    Tk_Justify justify;  /* Justification of the text string. This
                          * only matters if the text is composed
                          * of multiple lines. */
    GC gc;               /* GC used to draw the text */
    double theta;        /* Rotation of text in degrees. */
    Tk_Anchor anchor;    /* Indicates how the text is anchored around
                          * its x and y coordinates. */
    Rbc_Pad padX, padY;  /* # pixels padding of around text region */
    int leader;          /* # pixels spacing between lines of text */

} TextStyle;

/**
 * @brief Allocate an unrotated multiline layout using a borrowed style and font.
 * Fragment text pointers borrow the input string; keep it alive and unchanged
 * until the layout is no longer used. NULL is treated as an empty string.
 * The caller frees the single allocation with ckfree. Counts are UTF byte
 * counts, not character counts. Rotation and anchoring are drawing operations.
 *
 * @param[in] string NUL-terminated Tcl UTF text borrowed by fragments until the layout is freed; NULL is
 *    treated as empty.
 * @param[in] stylePtr Borrowed style with live font, padding, justification, leader and shadow offset used for
 *    measurement.
 */
TextLayout *Rbc_GetTextLayout(const char *string, TextStyle *stylePtr);
/**
 * @brief Measure unrotated text without allocating a fragment array.
 * Borrow the style and font. NULL returns zero dimensions, ignoring padding.
 * The height includes leader spacing for every line; unlike GetTextLayout it
 * does not remove the final leader. Do not substitute it blindly for layout
 * dimensions when nonzero leader spacing is used.
 *
 * @param[in] tsPtr Borrowed measurement style and live font; ignored when string is NULL.
 * @param[in] string NUL-terminated Tcl UTF text, or NULL to return zero dimensions.
 * @param[out] widthPtr Required destination for the nonnegative unrotated width in pixels.
 * @param[out] heightPtr Required destination for the nonnegative unrotated height in pixels, including final
 *    leader spacing.
 */
void Rbc_GetTextExtents(TextStyle *tsPtr, const char *string, int *widthPtr, int *heightPtr);
/**
 * @brief Initialize style defaults without acquiring or releasing resources.
 * This is not a full memset: gc, border, width and height are not initialized.
 * A caller using those fields must initialize them separately. In particular,
 * set gc to NULL or a valid owned Tk GC before ResetTextStyle or FreeTextStyle.
 *
 * @param[inout] stylePtr Caller-owned record receiving selected defaults; existing resources are not released.
 */
void Rbc_InitTextStyle(TextStyle *stylePtr);
/**
 * @brief Replace the style GC using its current font and optional foreground.
 * Requires a live window, valid font, and gc either NULL or an owned Tk GC.
 * Acquires the replacement before releasing the old GC. Font and colors are
 * borrowed; their ownership is unchanged.
 *
 * @param[in] tkwin Live Tk window supplying the display and drawing resources; borrowed for the call.
 * @param[inout] stylePtr Style with a valid borrowed font, optional color and NULL or owned Tk GC; receives
 *    the new owned GC.
 */
void Rbc_ResetTextStyle(Tk_Window tkwin, TextStyle *stylePtr);
/**
 * @brief Release a non-NULL style GC with Tk_FreeGC.
 * Does not release font, colors, border or the style record, and does not clear
 * the gc field. Do not call twice without resetting that field or installing
 * another owned GC. A borrowed GC must not be released through this helper.
 *
 * @param[in] display Display owning the style GC.
 * @param[inout] stylePtr Style whose non-NULL owned Tk GC is released; gc remains stale until the caller
 *    resets it.
 */
void Rbc_FreeTextStyle(Display *display, TextStyle *stylePtr);
void Rbc_SetDrawTextStyle(TextStyle *stylePtr, Tk_Font font, GC gc, XColor *normalColor, XColor *activeColor,
                          XColor *shadowColor, double theta, Tk_Anchor anchor, Tk_Justify justify, int leader,
                          int shadowOffset);
void Rbc_SetPrintTextStyle(TextStyle *stylePtr, Tk_Font font, XColor *fgColor, XColor *activeColor, XColor *shadowColor,
                           double theta, Tk_Anchor anchor, Tk_Justify justify, int leader, int shadowOffset);
void Rbc_DrawText(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *stylePtr, int x, int y);
void Rbc_DrawTextLayout(Tk_Window tkwin, Drawable drawable, TextLayout *textPtr, TextStyle *stylePtr, int x, int y);
void Rbc_DrawText2(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *stylePtr, int x, int y,
                   Dim2D *areaPtr);
Pixmap Rbc_CreateTextBitmap(Tk_Window tkwin, TextLayout *textPtr, TextStyle *stylePtr, int *widthPtr, int *heightPtr);
int Rbc_DrawRotatedText(Display *display, Drawable drawable, int x, int y, double theta, TextStyle *stylePtr,
                        TextLayout *textPtr);

#endif /* _RBCTEXT */
