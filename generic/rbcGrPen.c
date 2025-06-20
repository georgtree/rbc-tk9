/*
 * rbcGrPen.c --
 *
 *      This module implements pens for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"
#include <X11/Xutil.h>

static Tk_OptionParseProc StringToColor;
static Tk_OptionPrintProc ColorToString;
static Tk_OptionParseProc StringToPen;
static Tk_OptionPrintProc PenToString;
Tk_CustomOption rbcColorOption = {StringToColor, ColorToString, (ClientData)0};
Tk_CustomOption rbcPenOption = {StringToPen, PenToString, (ClientData)0};
Tk_CustomOption rbcBarPenOption = {StringToPen, PenToString, (ClientData)&rbcBarElementUid};
Tk_CustomOption rbcLinePenOption = {StringToPen, PenToString, (ClientData)&rbcLineElementUid};

static const char *NameOfColor(XColor *colorPtr);
static Pen *NameToPen(Graph *graphPtr, Tcl_Obj *nameObj);

typedef int(RbcGrPenOp)(Graph *, Tcl_Interp *, int, Tcl_Obj *const[]);
static RbcGrPenOp CgetOp;
static RbcGrPenOp ConfigureOp;
static RbcGrPenOp CreateOp;
static RbcGrPenOp DeleteOp;
static RbcGrPenOp NamesOp;
static RbcGrPenOp TypeOp;

/*
 *----------------------------------------------------------------------

 * StringToColor --
 *
 *      Convert the string representation of a color into a XColor pointer.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing color
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of color field in record
 *
 * Results:
 *      The return value is a standard Tcl result.  The color pointer is
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static int StringToColor(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                         Tcl_Size offset) {
    XColor **colorPtrPtr = (XColor **)(widgRec + offset);
    XColor *colorPtr;
    unsigned int length;
    char c;

    if ((string == NULL) || (*string == '\0')) {
        *colorPtrPtr = NULL;
        return TCL_OK;
    }
    c = string[0];
    length = strlen(string);

    if ((c == 'd') && (strncmp(string, "defcolor", length) == 0)) {
        colorPtr = COLOR_DEFAULT;
    } else {
        colorPtr = Tk_GetColor(interp, tkwin, Tk_GetUid(string));
        if (colorPtr == NULL) {
            return TCL_ERROR;
        }
    }
    *colorPtrPtr = colorPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NameOfColor --
 *
 *      Convert the color option value into a string.
 *
 * Parameters:
 *      XColor *colorPtr
 *
 * Results:
 *      The static string representing the color option is returned.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static const char *NameOfColor(XColor *colorPtr) {
    if (colorPtr == NULL) {
        return "";
    } else if (colorPtr == COLOR_DEFAULT) {
        return "defcolor";
    } else {
        return Tk_NameOfColor(colorPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ColorToString --
 *
 *      Convert the color value into a string.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget information record
 *      Tcl_Size offset - Offset of symbol type in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string representing the symbol color is returned.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static const char *ColorToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                 Tcl_FreeProc **freeProcPtr) {
    XColor *colorPtr = *(XColor **)(widgRec + offset);

    return NameOfColor(colorPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * StringToPen --
 *
 *      Convert the color value into a string.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing pen
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of pen field in record
 *
 * Results:
 *      The string representing the symbol color is returned.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static int StringToPen(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                       Tcl_Size offset) {
    Rbc_Uid classUid = *(Rbc_Uid *)clientData; /* Element type. */
    Pen **penPtrPtr = (Pen **)(widgRec + offset);
    Pen *penPtr;
    Graph *graphPtr;

    penPtr = NULL;
    graphPtr = Rbc_GetGraphFromWindowData(tkwin);

    if (classUid == NULL) {
        classUid = graphPtr->classUid;
    }
    if ((string != NULL) && (string[0] != '\0')) {
        if (Rbc_GetPen(graphPtr, string, classUid, &penPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /* Release the old pen */
    if (*penPtrPtr != NULL) {
        Rbc_FreePen(graphPtr, *penPtrPtr);
    }
    *penPtrPtr = penPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PenToString --
 *
 *      Parse the name of the name.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget information record
 *      Tcl_Size offset - Offset of pen in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static const char *PenToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                               Tcl_FreeProc **freeProcPtr) {
    Pen *penPtr = *(Pen **)(widgRec + offset);

    return penPtr->name;
}

/*
 *----------------------------------------------------------------------
 *
 * NameToPen --
 *
 *      Find and return the pen style from a given name.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Obj *nameObj
 *
 * Results:
 *      A standard TCL result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static Pen *NameToPen(Graph *graphPtr, Tcl_Obj *nameObj) {
    Tcl_HashEntry *hPtr;
    Pen *penPtr;
    char *name = Tcl_GetString(nameObj);

    hPtr = Tcl_FindHashEntry(&(graphPtr->penTable), name);
    if (hPtr == NULL) {
        goto notFound;
    }
    penPtr = (Pen *)Tcl_GetHashValue(hPtr);
    if (penPtr->flags & PEN_DELETE_PENDING) {
        goto notFound;
    }
    return penPtr;

notFound:
    Tcl_AppendResult(graphPtr->interp, "can't find pen \"", name, "\" in \"", Tk_PathName(graphPtr->tkwin), "\"",
                     (char *)NULL);
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyPen --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Pen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DestroyPen(Graph *graphPtr, Pen *penPtr) {
    Tk_FreeOptions(penPtr->configSpecs, (char *)penPtr, graphPtr->display, 0);
    (*penPtr->destroyProc)(graphPtr, penPtr);
    if ((penPtr->name != NULL) && (penPtr->name[0] != '\0')) {
        ckfree((char *)penPtr->name);
    }
    if (penPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(penPtr->hashPtr);
    }
    ckfree((char *)penPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreePen --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Pen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreePen(Graph *graphPtr, Pen *penPtr) {
    penPtr->refCount--;
    if ((penPtr->refCount == 0) && (penPtr->flags & PEN_DELETE_PENDING)) {
        DestroyPen(graphPtr, penPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CreatePen --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *penName
 *      Rbc_Uid classUid
 *      int nOpts
 *      Tcl_Obj *const *options
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Pen *Rbc_CreatePen(Graph *graphPtr, char *penName, Rbc_Uid classUid, int nOpts, Tcl_Obj *const *options) {
    Pen *penPtr;
    Tcl_HashEntry *hPtr;
    unsigned int configFlags;
    int isNew;
    register int i;
    char *option;
    Tcl_Size length;

    /*
     * Scan the option list for a "-type" entry.  This will indicate
     * what type of pen we are creating. Otherwise we'll default to the
     * suggested type.  Last -type option wins.
     */
    for (i = 0; i < nOpts; i += 2) {
        option = Tcl_GetStringFromObj(options[i], &length);
        if ((length > 2) && (strncmp(option, "-type", length) == 0)) {
            char *arg = Tcl_GetString(options[i + 1]);
            if (strcmp(arg, "bar") == 0) {
                classUid = rbcBarElementUid;
            } else if (strcmp(arg, "line") != 0) {
                classUid = rbcLineElementUid;
            } else if (strcmp(arg, "strip") != 0) {
                classUid = rbcLineElementUid;
            } else {
                Tcl_AppendResult(graphPtr->interp, "unknown pen type \"", arg, "\" specified", (char *)NULL);
                return NULL;
            }
        }
    }
    if (classUid == rbcStripElementUid) {
        classUid = rbcLineElementUid;
    }
    hPtr = Tcl_CreateHashEntry(&(graphPtr->penTable), penName, &isNew);
    if (!isNew) {
        penPtr = (Pen *)Tcl_GetHashValue(hPtr);
        if (!(penPtr->flags & PEN_DELETE_PENDING)) {
            Tcl_AppendResult(graphPtr->interp, "pen \"", penName, "\" already exists in \"",
                             Tk_PathName(graphPtr->tkwin), "\"", (char *)NULL);
            return NULL;
        }
        if (penPtr->classUid != classUid) {
            Tcl_AppendResult(graphPtr->interp, "pen \"", penName, "\" in-use: can't change pen type from \"",
                             penPtr->classUid, "\" to \"", classUid, "\"", (char *)NULL);
            return NULL;
        }
        penPtr->flags &= ~PEN_DELETE_PENDING;
    } else {
        if (classUid == rbcBarElementUid) {
            penPtr = Rbc_BarPen(penName);
        } else {
            penPtr = Rbc_LinePen(penName);
        }
        penPtr->classUid = classUid;
        penPtr->hashPtr = hPtr;
        Tcl_SetHashValue(hPtr, penPtr);
    }

    configFlags = (penPtr->flags & (ACTIVE_PEN | NORMAL_PEN));
    if (Rbc_ConfigureWidgetComponent(graphPtr->interp, graphPtr->tkwin, penPtr->name, "Pen", penPtr->configSpecs, nOpts,
                                     options, (char *)penPtr, configFlags) != TCL_OK) {
        if (isNew) {
            DestroyPen(graphPtr, penPtr);
        }
        return NULL;
    }
    (*penPtr->configProc)(graphPtr, penPtr);
    return penPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetPen --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      const char *name
 *      Rbc_Uid classUid
 *      Pen **penPtrPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetPen(Graph *graphPtr, const char *name, Rbc_Uid classUid, Pen **penPtrPtr) {
    Pen *penPtr;
    Tcl_Obj *nameObj = Tcl_NewStringObj(name, -1);

    penPtr = NameToPen(graphPtr, nameObj);
    Tcl_BounceRefCount(nameObj);

    if (penPtr == NULL) {
        return TCL_ERROR;
    }
    if (classUid == rbcStripElementUid) {
        classUid = rbcLineElementUid;
    }
    if (penPtr->classUid != classUid) {
        Tcl_AppendResult(graphPtr->interp, "pen \"", name, "\" is the wrong type (is \"", penPtr->classUid, "\"",
                         ", wanted \"", classUid, "\")", (char *)NULL);
        return TCL_ERROR;
    }
    penPtr->refCount++;
    *penPtrPtr = penPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DestroyPens --
 *
 *      Release memory and resources allocated for the style.
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the pen style is freed up.
 *
 *----------------------------------------------------------------------
 */
void Rbc_DestroyPens(Graph *graphPtr) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Pen *penPtr;

    for (hPtr = Tcl_FirstHashEntry(&(graphPtr->penTable), &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        penPtr = (Pen *)Tcl_GetHashValue(hPtr);
        penPtr->hashPtr = NULL;
        DestroyPen(graphPtr, penPtr);
    }
    Tcl_DeleteHashTable(&(graphPtr->penTable));
}

/*
 * ----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries axis attributes (font, line width, label, etc).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.  If querying configuration values,
 *      the interpreter result will contain the results.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 * ----------------------------------------------------------------------
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Pen *penPtr;
    unsigned int configFlags;

    penPtr = NameToPen(graphPtr, objv[3]);
    if (penPtr == NULL) {
        return TCL_ERROR;
    }
    configFlags = (penPtr->flags & (ACTIVE_PEN | NORMAL_PEN));
    return Tk_ConfigureValue(interp, graphPtr->tkwin, penPtr->configSpecs, (char *)penPtr, Tcl_GetString(objv[4]),
                             configFlags);
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Queries or resets pen attributes (font, line width, color, etc).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.  If querying configuration values,
 *      the interpreter result will contain the results.
 *
 * Side Effects:
 *      Pen resources are possibly allocated (GC, font).
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    int flags;
    Pen *penPtr;
    int nNames, nOpts;
    int redraw;
    Tcl_Obj *const *options;
    register int i;

    /* Figure out where the option value pairs begin */
    objc -= 3;
    objv += 3;
    for (i = 0; i < objc; i++) {
        char *arg = Tcl_GetString(objv[i]);
        if (arg[0] == '-') {
            break;
        }
        if (NameToPen(graphPtr, objv[i]) == NULL) {
            return TCL_ERROR;
        }
    }
    nNames = i;         /* Number of pen names specified */
    nOpts = objc - i;   /* Number of options specified */
    options = objv + i; /* Start of options in argv  */

    redraw = 0;
    for (i = 0; i < nNames; i++) {
        penPtr = NameToPen(graphPtr, objv[i]);
        flags = TK_CONFIG_ARGV_ONLY | (penPtr->flags & (ACTIVE_PEN | NORMAL_PEN));
        if (nOpts == 0) {
            return Tk_ConfigureInfo(interp, graphPtr->tkwin, penPtr->configSpecs, (char *)penPtr, (char *)NULL, flags);
        } else if (nOpts == 1) {
            return Tk_ConfigureInfo(interp, graphPtr->tkwin, penPtr->configSpecs, (char *)penPtr,
                                    Tcl_GetString(options[0]), flags);
        }
        if (Tk_ConfigureWidget(interp, graphPtr->tkwin, penPtr->configSpecs, nOpts, options, (char *)penPtr, flags) !=
            TCL_OK) {
            break;
        }
        (*penPtr->configProc)(graphPtr, penPtr);
        if (penPtr->refCount > 0) {
            redraw++;
        }
    }
    if (redraw) {
        graphPtr->flags |= REDRAW_BACKING_STORE | DRAW_MARGINS;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    if (i < nNames) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateOp --
 *
 *      Adds a new penstyle to the graph.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
 */
static int CreateOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Pen *penPtr;

    penPtr = Rbc_CreatePen(graphPtr, Tcl_GetString(objv[3]), graphPtr->classUid, objc - 4, objv + 4);

    if (penPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, objv[3]); /* pen name is objv[3] */
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * DeleteOp --
 *
 *      Delete the given pen.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns TCL_OK. If there is an error, returns TCL_ERROR and an
 *      error message is left in the interpreter result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *--------------------------------------------------------------
 */
static int DeleteOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Pen *penPtr;
    int i;

    for (i = 3; i < objc; i++) {
        penPtr = NameToPen(graphPtr, objv[i]);
        if (penPtr == NULL) {
            return TCL_ERROR;
        }
        if (penPtr->flags & PEN_DELETE_PENDING) {
            Tcl_AppendResult(graphPtr->interp, "can't find pen \"", Tcl_GetString(objv[i]), "\" in \"",
                             Tk_PathName(graphPtr->tkwin), "\"", (char *)NULL);
            return TCL_ERROR;
        }
        penPtr->flags |= PEN_DELETE_PENDING;
        if (penPtr->refCount == 0) {
            DestroyPen(graphPtr, penPtr);
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * NamesOp --
 *
 *      Return a list of the names of all the axes.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 * ----------------------------------------------------------------------
 */
static int NamesOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Tcl_HashSearch cursor;
    Pen *penPtr;
    register int i;
    register Tcl_HashEntry *hPtr;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    for (hPtr = Tcl_FirstHashEntry(&(graphPtr->penTable), &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        penPtr = (Pen *)Tcl_GetHashValue(hPtr);
        if (penPtr->flags & PEN_DELETE_PENDING) {
            continue;
        }
        if (objc == 3) {
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(penPtr->name, -1));
            continue;
        }
        for (i = 3; i < objc; i++) {
            if (Tcl_StringMatch(penPtr->name, Tcl_GetString(objv[i]))) {
                Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(penPtr->name, -1));
                break;
            }
        }
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TypeOp --
 *
 *      Return the type of pen.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 * ----------------------------------------------------------------------
 */
static int TypeOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Pen *penPtr;

    penPtr = NameToPen(graphPtr, objv[3]);
    if (penPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(penPtr->classUid, -1));
    return TCL_OK;
}

static Rbc_OpSpec penOps[] = {{"cget", (Rbc_Op)CgetOp, 5, 5, "penName option"},
                              {"configure", (Rbc_Op)ConfigureOp, 4, 0, "penName ?penName?... ?option value?..."},
                              {"create", (Rbc_Op)CreateOp, 4, 0, "penName ?option value?..."},
                              {"delete", (Rbc_Op)DeleteOp, 3, 0, "?penName?..."},
                              {"names", (Rbc_Op)NamesOp, 3, 0, "?pattern?..."},
                              {"type", (Rbc_Op)TypeOp, 4, 4, "penName"},
                              RBC_OPSPEC_END};

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PenOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_PenOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    Rbc_Op proc;

    proc = Rbc_GetOpFromObj(interp, penOps, RBC_OP_ARG2, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    return (*proc)(graphPtr, interp, objc, objv);
}
