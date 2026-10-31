/*
 * rbcDefaults.h --
 *
 *      Shared resource defaults, including platform-specific selections.
 *      Include order must not change the selected colors or fonts.
 *
 * See "license.terms" for details.
 */

#ifndef RBC_DEFAULTS_H
#define RBC_DEFAULTS_H

#define RGB_ANTIQUEWHITE1 "#ffefdb"
#define RGB_BISQUE1 "#ffe4c4"
#define RGB_BISQUE2 "#eed5b7"
#define RGB_BISQUE3 "#cdb79e"
#define RGB_BLACK "#000000"
#define RGB_BLUE "#0000ff"
#define RGB_GREEN "#00ff00"
#define RGB_GREY "#b0b0b0"
#define RGB_GREY15 "#262626"
#define RGB_GREY50 "#7f7f7f"
#define RGB_GREY64 "#a3a3a3"
#define RGB_GREY70 "#b3b3b3"
#define RGB_GREY75 "#bfbfbf"
#define RGB_GREY77 "#c3c3c3"
#define RGB_GREY82 "#d1d1d1"
#define RGB_GREY85 "#d9d9d9"
#define RGB_GREY90 "#e5e5e5"
#define RGB_GREY95 "#f2f2f2"
#define RGB_LIGHTBLUE0 "#e4f7ff"
#define RGB_LIGHTBLUE1 "#bfefff"
#define RGB_LIGHTBLUE2 "#b2dfee"
#define RGB_LIGHTSKYBLUE1 "#b0e2ff"
#define RGB_MAROON "#b03060"
#define RGB_NAVYBLUE "#000080"
#define RGB_PINK "#ffc0cb"
#define RGB_BISQUE1 "#ffe4c4"
#define RGB_RED "#ff0000"
#define RGB_WHITE "#ffffff"
#define RGB_YELLOW "#ffff00"

#ifdef OLD_TK_COLORS
#define STD_NORMAL_BACKGROUND RGB_BISQUE1
#define STD_ACTIVE_BACKGROUND RGB_BISQUE2
#define STD_SELECT_BACKGROUND RGB_LIGHTBLUE1
#define STD_DISABLE_FOREGROUND RGB_GREY64
#else
#define STD_NORMAL_BACKGROUND RGB_GREY85
#define STD_ACTIVE_BACKGROUND RGB_GREY64
#define STD_SELECT_BACKGROUND RGB_LIGHTBLUE1
#define STD_DISABLE_FOREGROUND RGB_GREY64
#endif /* OLD_TK_COLORS */

#define STD_ACTIVE_BG_MONO RGB_BLACK
#define STD_ACTIVE_FOREGROUND RGB_BLACK
#define STD_ACTIVE_FG_MONO RGB_WHITE
#define STD_BORDERWIDTH "1"

#define STD_FONT "TkDefaultFont"
#define STD_FONT_HUGE "TkCaptionFont"
#define STD_FONT_LARGE "TkHeadingFont"
#define STD_FONT_SMALL "TkSmallCaptionFont"

#define STD_INDICATOR_COLOR RGB_MAROON
#define STD_NORMAL_BG_MONO RGB_WHITE
#define STD_NORMAL_FOREGROUND RGB_BLACK
#define STD_NORMAL_FG_MONO RGB_BLACK
#define STD_SELECT_BG_MONO RGB_BLACK
#define STD_SELECT_BORDERWIDTH "1"
#define STD_SELECT_FOREGROUND RGB_BLACK
#define STD_SELECT_FG_MONO RGB_WHITE
#define STD_SHADOW_COLOR RGB_GREY64
#define STD_SHADOW_MONO RGB_BLACK

#ifdef WIN32
#undef STD_NORMAL_BACKGROUND
#undef STD_NORMAL_FOREGROUND
#undef STD_SELECT_BACKGROUND
#undef STD_SELECT_FOREGROUND
#undef STD_TEXT_FOREGROUND
#undef STD_FONT
#undef STD_FONT_LARGE
#undef STD_FONT_SMALL

#define STD_NORMAL_BACKGROUND "SystemButtonFace"
#define STD_NORMAL_FOREGROUND "SystemButtonText"
#define STD_SELECT_BACKGROUND "SystemHighlight"
#define STD_SELECT_FOREGROUND "SystemHighlightText"
#define STD_TEXT_FOREGROUND "SystemWindowText"
#define STD_FONT "Arial 8"
#define STD_FONT_LARGE "Arial 12"
#define STD_FONT_SMALL "Arial 6"

#endif /* WIN32 */

#endif /* RBC_DEFAULTS_H */
