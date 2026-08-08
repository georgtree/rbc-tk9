/*
 * rbcConfig.c --
 *
 *      This module implements custom configuration options for the rbc
 *      toolkit.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"

static Tk_OptionParseProc StringToDistance;
static Tk_OptionPrintProc DistanceToString;
Tk_CustomOption rbcDistanceOption = {StringToDistance, DistanceToString, (ClientData)PIXELS_NONNEGATIVE};

static Tk_OptionParseProc StringToShadow;
static Tk_OptionPrintProc ShadowToString;
Tk_CustomOption rbcShadowOption = {StringToShadow, ShadowToString, (ClientData)0};

static int IsFillPrefix(const char *string, Tcl_Size length, const char *fullName) {
    Tcl_Size fullLength;

    fullLength = (Tcl_Size)strlen(fullName);
    return ((length > 0) && (length <= fullLength) && (strncmp(string, fullName, (size_t)length) == 0));
}

int Rbc_GetFillFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *fillPtr) {
    const char *string;
    Tcl_Size length;
    int fill;

    string = Tcl_GetStringFromObj(objPtr, &length);
    /*
     * "no" is retained as a compatibility alias for "none".
     */
    if (IsFillPrefix(string, length, "none") || IsFillPrefix(string, length, "no")) {
        fill = FILL_NONE;
    } else if (IsFillPrefix(string, length, "x")) {
        fill = FILL_X;
    } else if (IsFillPrefix(string, length, "y")) {
        fill = FILL_Y;
    } else if (IsFillPrefix(string, length, "both")) {
        fill = FILL_BOTH;
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad argument \"%s\": should be "
                                               "\"none\", \"x\", \"y\", or \"both\"",
                                               string));
        return TCL_ERROR;
    }
    *fillPtr = fill;
    return TCL_OK;
}

static int DoCheck(Tcl_Interp *interp, int length, int check) {
    switch (check) {
    case PIXELS_NONNEGATIVE:
        if (length < 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad distance \"%d\": "
                                                   "can't be negative",
                                                   length));
            return TCL_ERROR;
        }
        break;
    case PIXELS_POSITIVE:
        if (length <= 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad distance \"%d\": "
                                                   "must be positive",
                                                   length));
            return TCL_ERROR;
        }
        break;
    case PIXELS_ANY:
        break;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetPixels --
 *
 *      Like Tk_GetPixels, but checks for negative, zero.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      const char *string
 *      int check - Can be PIXELS_POSITIVE, PIXELS_NONNEGATIVE, or PIXELS_ANY
 *      int *valuePtr
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetPixels(Tcl_Interp *interp, Tk_Window tkwin, const char *string, int check, int *valuePtr) {
    int length;

    if (Tk_GetPixels(interp, tkwin, string, &length) != TCL_OK) {
        return TCL_ERROR;
    }
    if (DoCheck(interp, length, check) != TCL_OK) {
        return TCL_ERROR;
    }
    *valuePtr = length;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetPixelsFromObj --
 *
 *      Like Tk_GetPixelsFromObj, but checks for negative, zero.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      Tcl_Obj *pixelsObj
 *      int check - Can be PIXELS_POSITIVE, PIXELS_NONNEGATIVE, or PIXELS_ANY
 *      int *valuePtr
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetPixelsFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *pixelsObj, int check, int *valuePtr) {
    int length;

    if (Tk_GetPixelsFromObj(interp, tkwin, pixelsObj, &length) != TCL_OK) {
        return TCL_ERROR;
    }
    if (DoCheck(interp, length, check) != TCL_OK) {
        return TCL_ERROR;
    }
    *valuePtr = length;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetPadFromObj --
 *
 *      Converts a Tcl list containing one or two screen distances
 *      into an Rbc_Pad structure.
 *
 *      A single value applies to both sides. Two values specify the
 *      two sides independently.
 *
 * Results:
 *      Returns TCL_OK on success and TCL_ERROR on failure.
 *
 * Side Effects:
 *      Updates *padPtr only after the complete value has been
 *      successfully parsed.
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetPadFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, Rbc_Pad *padPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    Rbc_Pad newPad;
    int value;

    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((objc < 1) || (objc > 2)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("wrong # elements in padding list: "
                                                  "should be one or two screen distances",
                                                  -1));
        return TCL_ERROR;
    }
    if (Rbc_GetPixelsFromObj(interp, tkwin, objv[0], PIXELS_NONNEGATIVE, &value) != TCL_OK) {
        return TCL_ERROR;
    }
    newPad.side1 = value;
    newPad.side2 = value;
    if (objc == 2) {
        if (Rbc_GetPixelsFromObj(interp, tkwin, objv[1], PIXELS_NONNEGATIVE, &value) != TCL_OK) {
            return TCL_ERROR;
        }
        newPad.side2 = value;
    }
    *padPtr = newPad;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToDistance --
 *
 *      Like TK_CONFIG_PIXELS, but adds an extra check for negative
 *      values.
 *
 * Parameters:
 *      void *clientData - Indicated how to check distance
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Window
 *      const char *string - Pixel value string
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of pixel size in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToDistance(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                            Tcl_Size offset) {
    int *valuePtr = (int *)(widgRec + offset);
    Tcl_Size data = (Tcl_Size)clientData;

    return Rbc_GetPixels(interp, tkwin, string, data, valuePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * DistanceToString --
 *
 *      Returns the string representing the positive pixel size.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget structure record
 *      Tcl_Size offset - Offset in widget record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The pixel size string is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *DistanceToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                    Tcl_FreeProc **freeProcPtr) {
    int value = *(int *)(widgRec + offset);
    char *result;

    result = RbcStrdup(Rbc_Itoa(value));
    assert(result);
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
}


int Rbc_GetShadowFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, Shadow *shadowPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    Shadow newShadow;

    newShadow.color = NULL;
    newShadow.offset = 0;
    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        *shadowPtr = newShadow;
        return TCL_OK;
    }
    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((objc < 1) || (objc > 2)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("wrong # elements in drop shadow value: "
                                                  "should be \"color ?offset?\"",
                                                  -1));
        return TCL_ERROR;
    }
    newShadow.color = Tk_GetColor(interp, tkwin, Tk_GetUid(Tcl_GetString(objv[0])));
    if (newShadow.color == NULL) {
        return TCL_ERROR;
    }
    newShadow.offset = 1;
    if (objc == 2) {
        if (Rbc_GetPixelsFromObj(interp, tkwin, objv[1], PIXELS_NONNEGATIVE, &newShadow.offset) != TCL_OK) {
            Tk_FreeColor(newShadow.color);
            return TCL_ERROR;
        }
    }
    *shadowPtr = newShadow;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToShadow --
 *
 *      Convert a string into two pad values.  The string may be in one of
 *      the following forms:
 *
 *          n      - n is a non-negative integer. This sets both
 *                   pad values to n.
 *          {n m}  - both n and m are non-negative integers. side1
 *                   is set to n, side2 is set to m.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Window
 *      const char *string - Pixel value string
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of pad in widget
 *
 * Results:
 *      If the string is successfully converted, TCL_OK is returned.
 *      Otherwise, TCL_ERROR is returned and an error message is left in
 *      the interpreter result.
 *
 * Side Effects:
 *      The padding structure passed is updated with the new values.
 *
 *----------------------------------------------------------------------
 */
static int StringToShadow(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                          Tcl_Size offset) {
    Shadow *shadowPtr = (Shadow *)(widgRec + offset);
    XColor *colorPtr;
    int dropOffset;

    colorPtr = NULL;
    dropOffset = 0;
    if ((string != NULL) && (string[0] != '\0')) {
        Tcl_Size nElem;
        const char **elemArr;

        if (Tcl_SplitList(interp, string, &nElem, &elemArr) != TCL_OK) {
            return TCL_ERROR;
        }
        if ((nElem < 1) || (nElem > 2)) {
            Tcl_AppendResult(interp, "wrong # elements in drop shadow value", (char *)NULL);
            ckfree((char *)elemArr);
            return TCL_ERROR;
        }
        colorPtr = Tk_GetColor(interp, tkwin, Tk_GetUid(elemArr[0]));
        if (colorPtr == NULL) {
            ckfree((char *)elemArr);
            return TCL_ERROR;
        }
        dropOffset = 1;
        if (nElem == 2) {
            if (Rbc_GetPixels(interp, tkwin, elemArr[1], PIXELS_NONNEGATIVE, &dropOffset) != TCL_OK) {
                Tk_FreeColor(colorPtr);
                ckfree((char *)elemArr);
                return TCL_ERROR;
            }
        }
        ckfree((char *)elemArr);
    }
    if (shadowPtr->color != NULL) {
        Tk_FreeColor(shadowPtr->color);
    }
    shadowPtr->color = colorPtr;
    shadowPtr->offset = dropOffset;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ShadowToString --
 *
 *      Converts the two pad values into a Tcl list.  Each pad has two
 *      pixel values.  For vertical pads, they represent the top and bottom
 *      margins.  For horizontal pads, they're the left and right margins.
 *      All pad values are non-negative integers.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Structure record
 *      Tcl_Size offset - Offset of pad in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The padding list is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *ShadowToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                  Tcl_FreeProc **freeProcPtr) {
    Shadow *shadowPtr = (Shadow *)(widgRec + offset);
    char *result;

    result = "";
    if (shadowPtr->color != NULL) {
        char string[200];

        sprintf(string, "%s %d", Tk_NameOfColor(shadowPtr->color), shadowPtr->offset);
        result = RbcStrdup(string);
        *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    }
    return result;
}

int Rbc_GetDashesFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Rbc_Dashes *dashesPtr) {
    Rbc_Dashes newDashes;
    const char *string;
    Tcl_Obj **valueObjv;
    Tcl_Size nValues;
    Tcl_Size i;

    memset(&newDashes, 0, sizeof(newDashes));
    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        *dashesPtr = newDashes;
        return TCL_OK;
    }
    string = Tcl_GetString(objPtr);
    if (strcmp(string, "dash") == 0) {
        newDashes.values[0] = 5;
        newDashes.values[1] = 2;
    } else if (strcmp(string, "dot") == 0) {
        newDashes.values[0] = 1;
    } else if (strcmp(string, "dashdot") == 0) {
        newDashes.values[0] = 2;
        newDashes.values[1] = 4;
        newDashes.values[2] = 2;
    } else if (strcmp(string, "dashdotdot") == 0) {
        newDashes.values[0] = 2;
        newDashes.values[1] = 4;
        newDashes.values[2] = 2;
        newDashes.values[3] = 2;
    } else {
        if (Tcl_ListObjGetElements(interp, objPtr, &nValues, &valueObjv) != TCL_OK) {
            return TCL_ERROR;
        }
        if (nValues > 11) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("too many values in dash list \"%s\"", string));
            return TCL_ERROR;
        }
        for (i = 0; i < nValues; i++) {
            long value;
            if (Tcl_ExprLongObj(interp, valueObjv[i], &value) != TCL_OK) {
                return TCL_ERROR;
            }
            if ((value == 0) && (nValues == 1)) {
                break;
            }
            if ((value < 1) || (value > 255)) {
                Tcl_SetObjResult(interp,
                                 Tcl_ObjPrintf("dash value \"%s\" is out of range", Tcl_GetString(valueObjv[i])));
                return TCL_ERROR;
            }
            newDashes.values[i] = (unsigned char)value;
        }
    }
    *dashesPtr = newDashes;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetState --
 *
 *      Converts a state string to its integer representation.
 *
 *      State names are matched exactly to preserve the behaviour of
 *      the legacy custom option.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter for error reporting.
 *      const char *string - State name.
 *      int *statePtr      - Receives the parsed state.
 *
 * Results:
 *      TCL_OK if the state is valid.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Sets the interpreter result on error. The destination is
 *      modified only after the value has been validated.
 *
 *----------------------------------------------------------------------
 */
static int GetState(Tcl_Interp *interp, const char *string, int *statePtr) {
    int state;

    if (strcmp(string, "normal") == 0) {
        state = STATE_NORMAL;
    } else if (strcmp(string, "disabled") == 0) {
        state = STATE_DISABLED;
    } else if (strcmp(string, "active") == 0) {
        state = STATE_ACTIVE;
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad state \"%s\": should be normal, active, or disabled", string));
        return TCL_ERROR;
    }

    *statePtr = state;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetStateFromObj --
 *
 *      Converts a Tcl object containing an element state into its
 *      integer representation.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter for error reporting.
 *      Tcl_Obj *objPtr    - Object containing the state name.
 *      int *statePtr      - Receives the parsed state.
 *
 * Results:
 *      TCL_OK if the state is valid.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Sets the interpreter result on error. The destination is
 *      modified only on success.
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetStateFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *statePtr) {
    return GetState(interp, Tcl_GetString(objPtr), statePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * GetStringList --
 *
 *      Converts a Tcl list string into a newly allocated,
 *      NULL-terminated array of strings.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter for list parsing and errors.
 *      const char *string  - Tcl list representation.
 *      char ***listPtrPtr  - Receives the allocated string array.
 *
 * Results:
 *      TCL_OK if the list was parsed successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Allocates one string-list block on success. The caller owns the
 *      result and must release it with ckfree(). An empty string is
 *      represented by NULL.
 *
 *----------------------------------------------------------------------
 */
static int GetStringList(Tcl_Interp *interp, const char *string, char ***listPtrPtr) {
    const char **elemArr;
    Tcl_Size nElem;

    *listPtrPtr = NULL;

    if ((string == NULL) || (string[0] == '\0')) {
        return TCL_OK;
    }

    elemArr = NULL;

    if (Tcl_SplitList(interp, string, &nElem, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }

    if (nElem == 0) {
        if (elemArr != NULL) {
            ckfree((char *)elemArr);
        }
        return TCL_OK;
    }

    /*
     * Tcl_SplitList returns one allocation containing both the pointer
     * array and the strings. Existing RBC users release it with one
     * ckfree().
     */
    *listPtrPtr = (char **)elemArr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetStringListFromObj --
 *
 *      Converts a Tcl object containing a list into a newly allocated,
 *      NULL-terminated string array.
 *
 * Parameters:
 *      Tcl_Interp *interp  - Interpreter for list parsing and errors.
 *      Tcl_Obj *objPtr     - Object containing the Tcl list.
 *      char ***listPtrPtr  - Receives the allocated string array.
 *
 * Results:
 *      TCL_OK if the list was parsed successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Allocates a string-list block on success. The destination is
 *      modified only after the value has been validated.
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetStringListFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, char ***listPtrPtr) {
    const char *string;

    string = (objPtr == NULL) ? "" : Tcl_GetString(objPtr);

    return GetStringList(interp, string, listPtrPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * FindChild --
 *
 *      Search for the named child window in a given parent window.
 *
 * Parameters:
 *      Tk_Window parent
 *      const char *name
 *
 * Results:
 *      The child Tk_Window. If the named child can't be found, NULL
 *      is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Tk_Window Rbc_FindChild(Tk_Window parent, const char *name) {
    Tcl_Obj *pathNamePtr;
    Tk_Window result;

    if (!parent) {
        return NULL;
    }

    pathNamePtr = Tcl_ObjPrintf("%s.%s", Tk_PathName(parent), name);
    result = Tk_NameToWindow(NULL, Tcl_GetString(pathNamePtr), parent);
    Tcl_BounceRefCount(pathNamePtr);
    return result;
}

int Rbc_InitComponentOptions(Tcl_Interp *interp, Tk_Window parent, const char *name, const char *className,
                             char *recordPtr, Tk_OptionTable optionTable) {
    Tk_Window tkwin;
    int isTemporary;
    int result;

    tkwin = Rbc_FindChild(parent, name);
    isTemporary = FALSE;
    if (tkwin == NULL) {
        tkwin = Tk_CreateWindow(interp, parent, name, (char *)NULL);
        if (tkwin == NULL) {
            return TCL_ERROR;
        }
        Tk_SetClass(tkwin, className);
        isTemporary = TRUE;
    }
    result = Tk_InitOptions(interp, recordPtr, optionTable, tkwin);
    if (isTemporary) {
        Tk_DestroyWindow(tkwin);
    }
    return result;
}

