/*
 * rbcScroll.c --
 *
 *      Scroll request parsing and viewport arithmetic.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 * See "license.terms" for details.
 */

#include "rbcInt.h"

static int ClampScrollValue(long double value) {
    if (value > (long double)INT_MAX) {
        return INT_MAX;
    }
    if (value < (long double)INT_MIN) {
        return INT_MIN;
    }
    return (int)value;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_AdjustViewport --
 *
 *      Adjusts the offsets of the viewport according to the scroll mode.
 *      This is to accommodate both "listbox" and "canvas" style scrolling.
 *
 *      "canvas"    The viewport scrolls within the range of world
 *              coordinates.  This way the viewport always displays
 *              a full page of the world.  If the world is smaller
 *              than the viewport, then (bizarrely) the world and
 *              viewport are inverted so that the world moves up
 *              and down within the viewport.
 *
 *      "listbox"    The viewport can scroll beyond the range of world
 *              coordinates.  Every entry can be displayed at the
 *              top of the viewport.  This also means that the
 *              scrollbar thumb weirdly shrinks as the last entry
 *              is scrolled upward.
 *
 * Parameters:
 *      int offset
 *      int worldSize
 *      int windowSize
 *      int scrollUnits
 *      int scrollMode
 *
 * Results:
 *      The corrected offset is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_AdjustViewport(int offset, int worldSize, int windowSize, int scrollUnits, int scrollMode) {
    Tcl_WideInt newOffset;
    Tcl_WideInt world;
    Tcl_WideInt window;
    Tcl_WideInt units;

    newOffset = offset;
    world = worldSize;
    window = windowSize;
    units = scrollUnits;
    switch (scrollMode) {
    case RBC_SCROLL_MODE_CANVAS:
        /*
         * Canvas-style scrolling allows the world to be scrolled
         * within the window.
         */
        if (world < window) {
            if ((world - newOffset) > window) {
                newOffset = world - window;
            }
            if (newOffset > 0) {
                newOffset = 0;
            }
        } else {
            if ((newOffset + window) > world) {
                newOffset = world - window;
            }
            if (newOffset < 0) {
                newOffset = 0;
            }
        }
        break;
    case RBC_SCROLL_MODE_LISTBOX:
        if (newOffset >= world) {
            newOffset = world - units;
        }
        if (newOffset < 0) {
            newOffset = 0;
        }
        break;
    case RBC_SCROLL_MODE_HIERBOX:
        /*
         * Hierbox-style scrolling allows the world to be scrolled
         * within the window.
         */
        if ((newOffset + window) > world) {
            newOffset = world - window;
        }
        if (newOffset < 0) {
            newOffset = 0;
        }
        break;
    }
    if (newOffset > INT_MAX) {
        return INT_MAX;
    }
    if (newOffset < INT_MIN) {
        return INT_MIN;
    }
    return (int)newOffset;
}

/* Validate the complete request before inspecting optional arguments. Queries
 * are handled by callers. Axis commands retain abbreviations and their legacy
 * numeric form; track commands use the exact Tk-style words. */
int Rbc_ParseScrollRequest(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Tcl_Size first,
    int legacy, Rbc_ScrollRequest *request) {
    Tcl_Size count = objc - first, valueIndex;
    const char *word;
    size_t length;
    Rbc_ScrollRequest parsed;
    if (count <= 0) {
        goto usage;
    }
    word = Tcl_GetString(objv[first]);
    length = strlen(word);
    if (length > 0 && (legacy ? strncmp(word, "moveto", length) == 0 : strcmp(word, "moveto") == 0)) {
        if (count != 2) {
            goto usage;
        }
        parsed.kind = RBC_SCROLL_MOVETO;
        valueIndex = first + 1;
    } else if (length > 0 && (legacy ? strncmp(word, "scroll", length) == 0 : strcmp(word, "scroll") == 0)) {
        const char *units;
        size_t unitLength;
        if (count != 3) {
            goto usage;
        }
        units = Tcl_GetString(objv[first + 2]);
        unitLength = strlen(units);
        if (unitLength > 0 && (legacy ? strncmp(units, "units", unitLength) == 0 : strcmp(units, "units") == 0)) {
            parsed.kind = RBC_SCROLL_UNITS;
        } else if (unitLength > 0 && (legacy ? strncmp(units, "pages", unitLength) == 0 : strcmp(units, "pages") == 0)) {
            parsed.kind = RBC_SCROLL_PAGES;
        } else {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown \"scroll\" units \"%s\"", units));
            return TCL_ERROR;
        }
        valueIndex = first + 1;
    } else {
        if (!legacy || count != 1) {
            goto usage;
        }
        parsed.kind = RBC_SCROLL_UNITS;
        valueIndex = first;
    }
    if (Tcl_GetDoubleFromObj(interp, objv[valueIndex], &parsed.value) != TCL_OK) {
        return TCL_ERROR;
    }
    if (!FINITE(parsed.value)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("scroll request must be finite", -1));
        return TCL_ERROR;
    }
    *request = parsed;
    return TCL_OK;
usage:
    Tcl_WrongNumArgs(interp, first, objv, "?moveto fraction|scroll number units|pages?");
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetScrollInfoFromObj --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const *objv
 *      int *offsetPtr
 *      int worldSize
 *      int windowSize
 *      int scrollUnits
 *      int scrollMode
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetScrollInfoFromObj(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], int *offsetPtr, int worldSize,
                             int windowSize, int scrollUnits, int scrollMode) {
    const char *string;
    size_t length;
    char c;
    int offset;
    int count;
    int delta;
    double fract;

    if ((objc < 1) || (objv == NULL)) {
        return TCL_ERROR;
    }
    offset = *offsetPtr;
    string = Tcl_GetString(objv[0]);
    c = string[0];
    length = strlen(string);
    if ((c == 's') && (strncmp(string, "scroll", length) == 0)) {
        if (objc != 3) {
            return TCL_ERROR;
        }
        /*
         * scroll number units|pages
         */
        if (Tcl_GetIntFromObj(interp, objv[1], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        string = Tcl_GetString(objv[2]);
        c = string[0];
        length = strlen(string);
        if ((c == 'u') && (strncmp(string, "units", length) == 0)) {
            fract = (double)count * (double)scrollUnits;
        } else if ((c == 'p') && (strncmp(string, "pages", length) == 0)) {
            /*
             * A page is 90% of the visible window.
             */
            fract = (double)count * (double)windowSize * 0.9;
        } else {
            Rbc_AppendResultStrings(interp, "unknown \"scroll\" units \"", Tcl_GetString(objv[2]), "\"", (char *)NULL);
            return TCL_ERROR;
        }
        /*
         * Preserve the historical truncation of the scroll delta
         * before adding it to the current integer offset.
         */
        delta = ClampScrollValue((long double)fract);
        offset = ClampScrollValue((long double)offset + (long double)delta);

    } else if ((c == 'm') && (strncmp(string, "moveto", length) == 0)) {
        long double position;

        if (objc != 2) {
            return TCL_ERROR;
        }
        if (Tcl_GetDoubleFromObj(interp, objv[1], &fract) != TCL_OK) {
            return TCL_ERROR;
        }
        if (!isfinite(fract)) {
            Rbc_AppendResultStrings(interp, "bad scroll fraction \"", Tcl_GetString(objv[1]), "\": must be finite",
                                    (char *)NULL);
            return TCL_ERROR;
        }
        position = (long double)worldSize * (long double)fract;
        offset = ClampScrollValue(position);
    } else {
        /*
         * Treat like "scroll units".
         */
        if (Tcl_GetIntFromObj(interp, objv[0], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        fract = (double)count * (double)scrollUnits;
        delta = ClampScrollValue((long double)fract);
        offset = ClampScrollValue((long double)offset + (long double)delta);
    }
    *offsetPtr = Rbc_AdjustViewport(offset, worldSize, windowSize, scrollUnits, scrollMode);
    return TCL_OK;
}
