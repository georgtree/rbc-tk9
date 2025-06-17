/*
 * rbcTkInt.h --
 *
 *      Contains copies of internal Tk structures.
 *      It is critical to keep these in sync with Tk.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCTKINT
#define _RBCTKINT

#ifdef WIN32
#include "tkWinInt.h"
#endif

#define TKP_CLIP_PIXMAP 0
#define TKP_CLIP_REGION 1

#ifdef WIN32
/*
 * The TkWinDrawable is the internal implementation of an X Drawable (either
 * a Window or a Pixmap).  The following constants define the valid Drawable
 * types.
 */

#define TWD_BITMAP    1
#define TWD_WINDOW    2
#define TWD_WINDC    3

#endif /* WIN32 */

#endif /* _RBCTKINT */
