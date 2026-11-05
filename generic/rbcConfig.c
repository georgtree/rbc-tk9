/** @file rbcConfig.c
 * @brief Shared option conversion and component configuration.
 */
/** @defgroup rbcConfig_internals rbcConfig helper contracts
 * Shared implementation helpers and legacy adapters.
 */

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
#include <ctype.h>

static Tk_OptionParseProc StringToDistance;
static Tk_OptionPrintProc DistanceToString;
Tk_CustomOption rbcDistanceOption = {StringToDistance, DistanceToString, (ClientData)PIXELS_NONNEGATIVE};

static Tk_OptionParseProc StringToShadow;
static Tk_OptionPrintProc ShadowToString;
Tk_CustomOption rbcShadowOption = {StringToShadow, ShadowToString, (ClientData)0};

/**
 * @ingroup rbcConfig_internals
 * @brief Test a nonempty, case-sensitive prefix of a fill keyword.
 * @param string Borrowed bytes with at least length bytes available.
 * @param length Byte count of the candidate, not a character count.
 * @param fullName Borrowed NUL-terminated keyword.
 * @return Nonzero only when the candidate fits and matches the keyword.
 * Does not modify inputs, allocate resources or set an interpreter result.
 */
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

/**
 * @ingroup rbcConfig_internals
 * @brief Apply the RBC sign policy to an already converted pixel distance.
 * @param interp Interpreter receiving a diagnostic on failure.
 * @param length Converted integer distance.
 * @param check PIXELS_ANY, PIXELS_NONNEGATIVE or PIXELS_POSITIVE.
 * @return TCL_OK when accepted; TCL_ERROR for a violated sign restriction.
 * No unit conversion or rounding occurs here. Unrecognized check values fall
 * through successfully; callers must supply an established policy constant.
 */
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

const char *Rbc_GetCanonicalOptionName(Tcl_Obj *objPtr, const Tk_OptionSpec *specs) {
    const Tk_OptionSpec *bestPtr;
    const Tk_OptionSpec *tablePtr;
    const char *name;
    Tcl_Size length;

    if ((objPtr == NULL) || (specs == NULL)) {
        return NULL;
    }
    name = Tcl_GetStringFromObj(objPtr, &length);
    if (length <= 0) {
        return NULL;
    }
    bestPtr = NULL;
    tablePtr = specs;
    /*
     * Tk option tables may be chained through the clientData member of
     * the TK_OPTION_END entry.
     */
    while (tablePtr != NULL) {
        const Tk_OptionSpec *specPtr;

        for (specPtr = tablePtr; specPtr->type != TK_OPTION_END; specPtr++) {
            size_t fullLength;

            fullLength = strlen(specPtr->optionName);
            if (length > (Tcl_Size)fullLength) {
                continue;
            }
            if (memcmp(name, specPtr->optionName, (size_t)length) != 0) {
                continue;
            }
            /*
             * Exact option names always win, even when they are also
             * prefixes of longer option names.
             */
            if (length == (Tcl_Size)fullLength) {
                if (specPtr->type == TK_OPTION_SYNONYM) {
                    return (const char *)specPtr->clientData;
                }
                return specPtr->optionName;
            }
            /*
             * This is an abbreviation.  It is usable only if every
             * match has the same complete option name.
             */
            if (bestPtr == NULL) {
                bestPtr = specPtr;
            } else if (strcmp(bestPtr->optionName, specPtr->optionName) != 0) {
                return NULL;
            }
        }
        tablePtr = (const Tk_OptionSpec *)specPtr->clientData;
    }
    if (bestPtr == NULL) {
        return NULL;
    }
    if (bestPtr->type == TK_OPTION_SYNONYM) {
        return (const char *)bestPtr->clientData;
    }
    return bestPtr->optionName;
}

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

/**
 * @ingroup rbcConfig_internals
 * @brief Parse a legacy Tk custom-option distance into an integer field.
 * @param clientData Pixel sign policy encoded as client data.
 * @param interp Interpreter for conversion errors.
 * @param tkwin Window supplying screen-unit conversion context.
 * @param string Borrowed distance string.
 * @param widgRec Writable record containing the integer destination.
 * @param offset Byte offset of that field.
 * @return TCL_OK on commit, TCL_ERROR with the old field unchanged on failure.
 * Delegates to Rbc_GetPixels; this adapter does not evaluate Tcl scripts.
 */
static int StringToDistance(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                            Tcl_Size offset) {
    int *valuePtr = (int *)(widgRec + offset);
    Tcl_Size data = (Tcl_Size)clientData;
    return Rbc_GetPixels(interp, tkwin, string, data, valuePtr);
}

/**
 * @ingroup rbcConfig_internals
 * @brief Format a legacy distance field as an owned decimal string.
 * @param clientData Unused.
 * @param tkwin Unused.
 * @param widgRec Borrowed record containing an integer field.
 * @param offset Byte offset of that field.
 * @param[out] freeProcPtr Receives Tcl_Free for the allocated result.
 * @return Newly allocated string; Tk releases it through freeProcPtr.
 * Copies the shared Rbc_Itoa buffer immediately. Does not change the field.
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

/**
 * @ingroup rbcConfig_internals
 * @brief Replace a legacy shadow field after parsing color and offset.
 * @param clientData Unused.
 * @param interp Interpreter for list, color and distance errors.
 * @param tkwin Window supplying color and screen-unit context.
 * @param string Borrowed Tcl list string, or NULL/empty to disable the shadow.
 * @param widgRec Writable record containing an initialized Shadow.
 * @param offset Byte offset of that field.
 * @return TCL_OK on replacement, TCL_ERROR with the old shadow unchanged.
 * Accepts one color and an optional nonnegative distance; offset defaults to
 * one. Releases temporary list storage and any staged color on error. On success
 * releases the old field's color and transfers the new reference. Unlike
 * Rbc_GetShadowFromObj, this adapter owns replacement cleanup itself.
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
            Rbc_AppendResultStrings(interp, "wrong # elements in drop shadow value", (char *)NULL);
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

/**
 * @ingroup rbcConfig_internals
 * @brief Serialize a legacy shadow as a color/offset Tcl list.
 * @param clientData Unused.
 * @param tkwin Unused.
 * @param widgRec Borrowed record containing an initialized Shadow.
 * @param offset Byte offset of that field.
 * @param[in,out] freeProcPtr Receives Tcl_Free only for an allocated result.
 * @return Static empty string if disabled; otherwise a new two-element list.
 * The disabled path leaves freeProcPtr untouched. The Tk caller must initialize
 * its free policy for static results. Color names are quoted by Tcl_Merge.
 */
static const char *ShadowToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                  Tcl_FreeProc **freeProcPtr) {
    Shadow *shadowPtr;
    char offsetString[32];
    const char *elements[2];
    char *result;

    (void)clientData;
    (void)tkwin;
    shadowPtr = (Shadow *)(widgRec + offset);
    if (shadowPtr->color == NULL) {
        return "";
    }
    snprintf(offsetString, sizeof(offsetString), "%d", shadowPtr->offset);
    elements[0] = Tk_NameOfColor(shadowPtr->color);
    elements[1] = offsetString;
    result = Tcl_Merge(2, elements);
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
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
        if (nValues > RBC_MAX_DASH_VALUES) {
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

/**
 * @ingroup rbcConfig_internals
 * @brief Parse an exact legacy state name without abbreviation.
 * @param interp Interpreter receiving an error diagnostic.
 * @param string Non-NULL NUL-terminated normal, active or disabled name.
 * @param[out] statePtr Receives the matching STATE constant only on success.
 * @return TCL_OK on success, TCL_ERROR with the destination unchanged otherwise.
 * Matching is case-sensitive and does not execute Tcl.
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

int Rbc_GetStateFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *statePtr) {
    return GetState(interp, Tcl_GetString(objPtr), statePtr);
}

/**
 * @ingroup rbcConfig_internals
 * @brief Split a Tcl list string into one caller-owned allocation.
 * @param interp Interpreter for list syntax errors.
 * @param string Borrowed list string; NULL and empty input are accepted.
 * @param[out] listPtrPtr Cleared first; receives the pointer array on success.
 * @return TCL_OK on success, TCL_ERROR on malformed list syntax.
 * An empty list leaves NULL. Nonempty output contains pointers and characters
 * in one Tcl_SplitList allocation; release it once with ckfree. This helper
 * neither preserves nor frees a previous destination, including on error.
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
    pathNamePtr = Tcl_ObjPrintf("%s%s%s", Tk_PathName(parent),
                                (strcmp(Tk_PathName(parent), ".") == 0) ? "" : ".", name);
    result = Tk_NameToWindow(NULL, Tcl_GetString(pathNamePtr), parent);
    Tcl_BounceRefCount(pathNamePtr);
    return result;
}

int Rbc_RestoreOptionsOnError(Tcl_Interp *interp, Tk_SavedOptions *savedOptionsPtr) {
    Tcl_InterpState state;

    state = Tcl_SaveInterpState(interp, TCL_ERROR);
    Tk_RestoreSavedOptions(savedOptionsPtr);
    return Tcl_RestoreInterpState(interp, state);
}

char *Rbc_GetComponentResourceName(const char *name) {
    char *copy = RbcStrdup(name);

    if (copy[0] != '\0') {
        copy[0] = (char)tolower((unsigned char)copy[0]);
    }
    return copy;
}

int Rbc_InitComponentOptions(Tcl_Interp *interp, Tk_Window parent, Tk_Window proxy, const char *name,
                             const char *className, char *recordPtr, Tk_OptionTable optionTable) {
    Tk_Window tkwin;

    /*
     * Preserve the old behavior when an actual child with this name
     * exists.
     */
    tkwin = Rbc_FindChild(parent, name);
    if (tkwin != NULL) {
        return Tk_InitOptions(interp, recordPtr, optionTable, tkwin);
    }
    /*
     * There is no real child corresponding to this pseudo-component.
     * Reuse the graph's anonymous option proxy instead.
     */
    if (proxy != NULL) {
        Tk_FakeWin *winPtr;
        Tcl_DString pathName;
        int result;

        /*
         * Refresh the parent stack before changing the proxy class. An option
         * database update invalidates Tk's cache, but Tk_SetClass() can replace
         * that invalidation with a cached parent still containing old values.
         * Looking up an option on the parent first rebuilds it when necessary
         * and removes the proxy level before it is reused.
         */
        (void)Tk_GetOption(parent, "background", "Background");
        winPtr = (Tk_FakeWin *)proxy;
        /*
         * Tk's option database matcher identifies this level of the
         * hierarchy by nameUid and classUid.
         *
         * Set the pseudo-component name first, then call Tk_SetClass().
         * Tk_SetClass() also invalidates Tk's cached option-stack level,
         * ensuring that the new name/class pair is used by the next
         * Tk_GetOption().
         */
        winPtr->nameUid = Tk_GetUid(name);
        Tk_SetClass(proxy, className);
        /*
         * Anonymous windows normally have no pathname.  Tk_InitOptions()
         * only needs one for error reporting, so temporarily supply the
         * pathname that the old temporary child would have had.
         *
         * Do not leave pathName set after Tk_InitOptions(): the anonymous
         * window is deliberately absent from Tk's pathname hash table.
         */
        Tcl_DStringInit(&pathName);
        Tcl_DStringAppend(&pathName, Tk_PathName(parent), TCL_INDEX_NONE);
        if (strcmp(Tk_PathName(parent), ".") != 0) {
            Tcl_DStringAppend(&pathName, ".", 1);
        }
        Tcl_DStringAppend(&pathName, name, TCL_INDEX_NONE);
        winPtr->pathName = Tcl_DStringValue(&pathName);
        result = Tk_InitOptions(interp, recordPtr, optionTable, proxy);
        winPtr->pathName = NULL;
        Tcl_DStringFree(&pathName);
        return result;
    }
    /*
     * Defensive fallback.  This should not normally occur once every
     * graph owns an option proxy.
     */
    return Tk_InitOptions(interp, recordPtr, optionTable, parent);
}

int Rbc_GetComponentOptionIndex(Tcl_Interp *interp, Tcl_Obj *objPtr, const Tk_OptionSpec *specs,
                                const char *message, int *indexPtr) {
    return Tcl_GetIndexFromObjStruct(interp, objPtr, &specs[0].optionName, sizeof(Tk_OptionSpec),
                                     message, 0, indexPtr);
}

Tcl_Obj *Rbc_GetComponentOptionInfo(const Tk_OptionSpec *spec, Tcl_Obj *valueObj) {
    Tcl_Obj *items[5];
    items[0] = Tcl_NewStringObj(spec->optionName, -1);
    items[1] = Tcl_NewStringObj(spec->dbName, -1);
    items[2] = Tcl_NewStringObj(spec->dbClass, -1);
    items[3] = Tcl_NewStringObj(spec->defValue, -1);
    items[4] = valueObj;
    return Tcl_NewListObj(5, items);
}
