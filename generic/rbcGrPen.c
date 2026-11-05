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

/** @file rbcGrPen.c
 * Pen and palette implementation contracts.
 */
/** @defgroup pen_internals Named pen internals
 * Private helpers; no new public API.
 */

#include "rbcGraph.h"
#include <X11/Xutil.h>

static Pen *NameToPen(Graph *graphPtr, Tcl_Obj *nameObj);

typedef int RbcGrPenOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

/** @brief Typed command dispatch entry with shared argument validation metadata. */
typedef struct {
    Rbc_OpSpecHeader header; /**< Shared operation name, abbreviation and argument-count metadata. */
    RbcGrPenOp *proc; /**< Typed command handler for this operation. */
} PenOpSpec;

static RbcGrPenOp CgetOp;
static RbcGrPenOp ConfigureOp;
static RbcGrPenOp CreateOp;
static RbcGrPenOp DeleteOp;
static RbcGrPenOp NamesOp;
static RbcGrPenOp TypeOp;

/**
 * @ingroup pen_internals
 * @brief Initialize a named pen from its Pen option-database component.
 * @param graphPtr Live owning graph with window and component proxy.
 * @param penPtr Concrete pen with name and optionSpecs installed.
 * @return TCL_OK when initialized (including an already initialized record),
 * TCL_ERROR after releasing partial Tk options and the new table on failure.
 * Normalizes the component resource name, releases that temporary name, and sets
 * optionsInitialized only on success. Does not construct derived drawing GCs;
 * the caller must run the concrete configurator afterward.
 */
static int InitPenOptions(Graph *graphPtr, Pen *penPtr) {
    char *componentName;
    int result;

    if (penPtr->optionsInitialized) {
        return TCL_OK;
    }
    assert(penPtr->optionSpecs != NULL);
    penPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, penPtr->optionSpecs);
    /*
     * Preserve the historical component resource hierarchy for pen names
     * beginning with an uppercase letter. Temporary Tk child names cannot
     * begin with an uppercase character.
     */
    componentName = Rbc_GetComponentResourceName(penPtr->name);
    result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, componentName, "Pen",
                                      (char *)penPtr, penPtr->optionTable);
    ckfree(componentName);
    if (result != TCL_OK) {
        /* Tk_InitOptions leaves earlier options initialized on failure. */
        Tk_FreeConfigOptions((char *)penPtr, penPtr->optionTable, graphPtr->tkwin);
        Tk_DeleteOptionTable(penPtr->optionTable);
        penPtr->optionTable = NULL;
        return TCL_ERROR;
    }
    penPtr->optionsInitialized = TRUE;
    return TCL_OK;
}

/**
 * @ingroup pen_internals
 * @brief Apply one named pen's explicit Tk options and derived configuration.
 * @param graphPtr Live owning graph and interpreter.
 * @param penPtr Initialized concrete pen.
 * @param objc Even number of option/value objects.
 * @param objv Borrowed option/value array, valid throughout configuration.
 * @return TCL_OK on commit, TCL_ERROR with a diagnostic on failure.
 * Tk_SetOptions handles option conversion errors. If configProc fails, restore
 * saved Tk options while preserving its diagnostic. Concrete configProc must
 * stage its own derived resources before commit. On success release saved old
 * options. This helper neither schedules redraw nor preserves objects across
 * arbitrary callbacks; its caller owns their lifetime.
 */
static int ConfigurePenOptions(Graph *graphPtr, Pen *penPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_SavedOptions savedOptions;
    int mask;

    assert(penPtr->optionsInitialized);
    assert((objc & 1) == 0);
    if (Tk_SetOptions(graphPtr->interp, (char *)penPtr, penPtr->optionTable, objc, objv, graphPtr->tkwin, &savedOptions,
                      &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Concrete modern configuration functions must parse additional
     * object-backed fields transactionally: they must not modify the
     * active derived state until every fallible conversion succeeds.
     */
    if ((*penPtr->configProc)(graphPtr, penPtr) != TCL_OK) {
        return Rbc_RestoreOptionsOnError(graphPtr->interp, &savedOptions);
    }
    Tk_FreeSavedOptions(&savedOptions);
    return TCL_OK;
}

/**
 * @ingroup pen_internals
 * @brief Release a named pen's drawing and Tk resources once.
 * @param graphPtr Owning graph; a live Tk window is required for actual release.
 * @param penPtr Concrete pen record retained after this call.
 * Does nothing before successful option initialization or after prior release.
 * Calls destroyProc before freeing Tk options, then deletes and clears the option
 * table and marks tkResourcesReleased. Does not free the name, registry entry or
 * record, nor change reference counts.
 */
static void ReleasePenResources(Graph *graphPtr, Pen *penPtr) {
    if ((!penPtr->optionsInitialized) || (penPtr->tkResourcesReleased)) {
        return;
    }
    assert(penPtr->optionSpecs != NULL);
    assert(graphPtr->tkwin != NULL);
    /*
     * Release GCs and manually managed derived resources before the
     * Tk option table releases fonts, colours, borders, and bitmaps.
     */
    (*penPtr->destroyProc)(graphPtr, penPtr);
    Tk_FreeConfigOptions((char *)penPtr, penPtr->optionTable, graphPtr->tkwin);
    Tk_DeleteOptionTable(penPtr->optionTable);
    penPtr->optionTable = NULL;
    penPtr->tkResourcesReleased = TRUE;
}

void Rbc_ReleasePenTkResources(Graph *graphPtr) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->penTable, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        Pen *penPtr;
        penPtr = Tcl_GetHashValue(hPtr);
        ReleasePenResources(graphPtr, penPtr);
    }
}

/**
 * @ingroup pen_internals
 * @brief Find a visible named pen without acquiring a reference.
 * @param graphPtr Live graph with initialized pen table and Tk window.
 * @param nameObj Borrowed pen-name object.
 * @return Borrowed Pen pointer, or NULL with a graph-interpreter diagnostic.
 * Delete-pending records are hidden even when retained in the table. Does not
 * check concrete type or increment refCount; use Rbc_GetPen for owned references.
 */
static Pen *NameToPen(Graph *graphPtr, Tcl_Obj *nameObj) {
    Tcl_HashEntry *hPtr;
    Pen *penPtr;
    const char *name;

    name = Tcl_GetString(nameObj);
    hPtr = Tcl_FindHashEntry(&graphPtr->penTable, name);
    if (hPtr != NULL) {
        penPtr = Tcl_GetHashValue(hPtr);
        if (!(penPtr->flags & PEN_DELETE_PENDING)) {
            return penPtr;
        }
    }
    Tcl_SetObjResult(graphPtr->interp,
                     Tcl_ObjPrintf("can't find pen \"%s\" in \"%s\"", name, Tk_PathName(graphPtr->tkwin)));
    return NULL;
}

/**
 * @ingroup pen_internals
 * @brief Remove a pen record and its registry entry without a reference check.
 * @param graphPtr Owning graph in normal operation or ordered teardown.
 * @param penPtr Pen whose users have already released their references.
 * Releases Tk resources if still needed and the window remains live, then frees
 * the nonempty owned name, deletes a non-NULL hash entry and frees the record.
 * After window destruction, the caller must already have run resource release.
 * Graph-wide teardown clears hashPtr first and deletes the table separately.
 */
static void DestroyPen(Graph *graphPtr, Pen *penPtr) {
    if ((!penPtr->tkResourcesReleased) && (graphPtr->tkwin != NULL)) {
        ReleasePenResources(graphPtr, penPtr);
    }
    if ((penPtr->name != NULL) && (penPtr->name[0] != '\0')) {
        ckfree(penPtr->name);
    }
    if (penPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(penPtr->hashPtr);
    }
    ckfree(penPtr);
}

void Rbc_FreePen(Graph *graphPtr, Pen *penPtr) {
    penPtr->refCount--;
    if ((penPtr->refCount == 0) && (penPtr->flags & PEN_DELETE_PENDING)) {
        DestroyPen(graphPtr, penPtr);
    }
}

/**
 * @ingroup pen_internals
 * @brief Create a named concrete pen or revive a matching retained record.
 * @param graphPtr Live owning graph.
 * @param penName Borrowed name to register.
 * @param classUid Default pen class; strip and polar are normalized to line.
 * @param nOpts Number of explicit option/value objects, expected to be even.
 * @param options Borrowed options, valid throughout initialization/configuration.
 * @return Graph-owned Pen pointer without a new counted reference, or NULL on error.
 * Scans -type prefixes of length three through five before allocation; the last
 * recognized occurrence selects bar or line (strip selects line). Existing live
 * names and incompatible pending types fail. New records are removed on failed
 * initialization or configuration. A matching pending record is reused, but its
 * PEN_DELETE_PENDING flag is cleared only after configuration succeeds, so
 * failed revival keeps the retained record hidden. No redraw is
 * scheduled here; acquiring a long-lived reference requires Rbc_GetPen.
 */
Pen *Rbc_CreatePen(Graph *graphPtr, const char *penName, Rbc_Uid classUid, Tcl_Size nOpts, Tcl_Obj *const options[]) {
    Pen *penPtr;
    Tcl_HashEntry *hPtr;
    Tcl_Size i;
    int isNew;

    penPtr = NULL;
    /*
     * Scan the option list for "-type". The last occurrence wins.
     */
    for (i = 0; (i + 1) < nOpts; i += 2) {
        const char *option;
        Tcl_Size length;

        option = Tcl_GetStringFromObj(options[i], &length);
        if ((length >= 3) && (length <= 5) && (strncmp(option, "-type", (size_t)length) == 0)) {
            const char *arg;

            arg = Tcl_GetString(options[i + 1]);
            if (strcmp(arg, "bar") == 0) {
                classUid = rbcBarElementUid;
            } else if ((strcmp(arg, "line") == 0) || (strcmp(arg, "strip") == 0)) {
                classUid = rbcLineElementUid;
            } else {
                Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("unknown pen type \"%s\" specified", arg));
                return NULL;
            }
        }
    }
    /*
     * Stripchart and polar elements use the same concrete implementation
     * as line pens.
     */
    if ((classUid == rbcStripElementUid) || (classUid == rbcPolarElementUid)) {
        classUid = rbcLineElementUid;
    }
    /*
     * Create the hash-table entry or retrieve an existing delete-pending
     * pen with the same name.
     */
    hPtr = Tcl_CreateHashEntry(&graphPtr->penTable, penName, &isNew);
    if (!isNew) {
        penPtr = Tcl_GetHashValue(hPtr);
        if (!(penPtr->flags & PEN_DELETE_PENDING)) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("pen \"%s\" already exists in \"%s\"", penName,
                                                             Tk_PathName(graphPtr->tkwin)));
            return NULL;
        }
        if (penPtr->classUid != classUid) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("pen \"%s\" in-use: can't change pen type "
                                                             "from \"%s\" to \"%s\"",
                                                             penName, penPtr->classUid, classUid));
            return NULL;
        }
    } else {
        /*
         * Construct a new concrete named pen.
         */
        if (classUid == rbcBarElementUid) {
            penPtr = Rbc_BarPen(penName);
        } else {
            penPtr = Rbc_LinePen(penName);
        }
        if (penPtr == NULL) {
            Tcl_DeleteHashEntry(hPtr);
            return NULL;
        }
        penPtr->classUid = classUid;
        penPtr->hashPtr = hPtr;
        Tcl_SetHashValue(hPtr, penPtr);
    }
    assert(penPtr != NULL);
    assert(penPtr->optionSpecs != NULL);
    /*
     * A newly allocated pen needs its Tk option record initialised.
     * A revived delete-pending pen already has an initialised option
     * record.
     */
    if (isNew) {
        if (InitPenOptions(graphPtr, penPtr) != TCL_OK) {
            DestroyPen(graphPtr, penPtr);
            return NULL;
        }
    }
    if (nOpts > 0) {
        /*
         * Apply explicitly supplied options transactionally.
         */
        if (ConfigurePenOptions(graphPtr, penPtr, nOpts, options) != TCL_OK) {
            if (isNew) {
                DestroyPen(graphPtr, penPtr);
            }
            return NULL;
        }
    } else {
        /*
         * Tk_InitOptions() installed the default option values, but
         * the concrete pen still needs its derived GCs and other
         * drawing resources constructed.
         *
         * This also refreshes the derived resources of a revived
         * delete-pending pen.
         */
        if ((*penPtr->configProc)(graphPtr, penPtr) != TCL_OK) {
            if (isNew) {
                DestroyPen(graphPtr, penPtr);
            }
            return NULL;
        }
    }
    /* Publish a revived pen only after all fallible configuration succeeds. */
    penPtr->flags &= ~PEN_DELETE_PENDING;
    return penPtr;
}

int Rbc_GetPen(Graph *graphPtr, const char *name, Rbc_Uid classUid, Pen **penPtrPtr) {
    Pen *penPtr;

    Tcl_Obj *nameObj = Tcl_NewStringObj(name, -1);
    penPtr = NameToPen(graphPtr, nameObj);
    Tcl_BounceRefCount(nameObj);
    if (penPtr == NULL) {
        return TCL_ERROR;
    }
    if ((classUid == rbcStripElementUid) || (classUid == rbcPolarElementUid)) {
        classUid = rbcLineElementUid;
    }
    if (penPtr->classUid != classUid) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("pen \"%s\" is the wrong type "
                                                         "(is \"%s\", wanted \"%s\")",
                                                         name, penPtr->classUid, classUid));
        return TCL_ERROR;
    }
    penPtr->refCount++;
    *penPtrPtr = penPtr;
    return TCL_OK;
}

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

/**
 * @ingroup pen_internals
 * @brief Return the current value of one named pen option.
 * @param graphPtr Live owning graph.
 * @param interp Command interpreter.
 * @param objc Full widget-command argument count, already checked by dispatch.
 * @param objv Borrowed full argument vector, with pen subcommand at index two.
 * Looks up objv[3] without acquiring a reference, then uses Tk_GetOptionValue
 * for objv[4]. Returns TCL_ERROR on lookup/conversion failure; otherwise installs
 * the returned Tcl object as the result. No configuration or redraw occurs.
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Pen *penPtr;
    Tcl_Obj *resultObjPtr;

    penPtr = NameToPen(graphPtr, objv[3]);
    if (penPtr == NULL) {
        return TCL_ERROR;
    }
    resultObjPtr = Tk_GetOptionValue(interp, (char *)penPtr, penPtr->optionTable, objv[4], graphPtr->tkwin);
    if (resultObjPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, resultObjPtr);
    return TCL_OK;
}

/**
 * @ingroup pen_internals
 * @brief Query or configure one or more named pens.
 * @param graphPtr Live owning graph.
 * @param interp Command interpreter.
 * @param objc Full widget-command argument count, already checked by dispatch.
 * @param objv Borrowed full argument vector, with pen subcommand at index two.
 * Treats the first argument beginning with a dash as the option boundary and
 * checks preceding names before applying changes. Zero or one option arguments
 * query only the first pen. Updates run in name order and stop at the first
 * failure; earlier commits remain. Successfully changed referenced pens cause
 * backing-store and margin redraw, including when a later pen fails. Returns
 * TCL_OK or TCL_ERROR; does not provide a transaction across all named pens.
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Pen *penPtr;
    Tcl_Size nNames;
    Tcl_Size nOpts;
    Tcl_Size i;
    Tcl_Obj *const *options;
    int redraw;

    /* Figure out where the option value pairs begin */
    objc -= 3;
    objv += 3;
    for (i = 0; i < objc; i++) {
        const char *arg = Tcl_GetString(objv[i]);
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
        Tcl_Obj *resultObjPtr;

        penPtr = NameToPen(graphPtr, objv[i]);
        if (nOpts == 0) {
            resultObjPtr = Tk_GetOptionInfo(interp, (char *)penPtr, penPtr->optionTable, NULL, graphPtr->tkwin);
            if (resultObjPtr == NULL) {
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, resultObjPtr);
            return TCL_OK;
        }
        if (nOpts == 1) {
            resultObjPtr = Tk_GetOptionInfo(interp, (char *)penPtr, penPtr->optionTable, options[0], graphPtr->tkwin);
            if (resultObjPtr == NULL) {
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, resultObjPtr);
            return TCL_OK;
        }
        if (ConfigurePenOptions(graphPtr, penPtr, nOpts, options) != TCL_OK) {
            break;
        }
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

/**
 * @ingroup pen_internals
 * @brief Create a pen using the graph class as the default.
 * @param graphPtr Live owning graph.
 * @param interp Command interpreter.
 * @param objc Full widget-command argument count, already checked by dispatch.
 * @param objv Borrowed full argument vector, with pen subcommand at index two.
 * Passes objv[3] and trailing options to Rbc_CreatePen. Returns TCL_ERROR on
 * failure, otherwise sets the original name object as the result and returns
 * TCL_OK. Does not acquire a counted reference to the returned pen.
 */
static int CreateOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Pen *penPtr;

    penPtr = Rbc_CreatePen(graphPtr, Tcl_GetString(objv[3]), graphPtr->classUid, objc - 4, objv + 4);
    if (penPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, objv[3]); /* pen name is objv[3] */
    return TCL_OK;
}

/**
 * @ingroup pen_internals
 * @brief Hide named pens and destroy those with no remaining references.
 * @param graphPtr Live owning graph.
 * @param interp Command interpreter.
 * @param objc Full widget-command argument count, already checked by dispatch.
 * @param objv Borrowed full argument vector, with pen subcommand at index two.
 * Processes names in order, sets PEN_DELETE_PENDING and immediately destroys
 * zero-reference records. Returns TCL_ERROR at the first unavailable name;
 * earlier deletions remain. An empty name sequence succeeds. Repeated names
 * therefore fail after the first deletion rather than being silently ignored.
 */
static int DeleteOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Pen *penPtr;
    Tcl_Size i;

    for (i = 3; i < objc; i++) {
        penPtr = NameToPen(graphPtr, objv[i]);
        if (penPtr == NULL) {
            return TCL_ERROR;
        }
        if (penPtr->flags & PEN_DELETE_PENDING) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't find pen \"%s\" in \"%s\"", Tcl_GetString(objv[i]),
                                                   Tk_PathName(graphPtr->tkwin)));
            return TCL_ERROR;
        }
        penPtr->flags |= PEN_DELETE_PENDING;
        if (penPtr->refCount == 0) {
            DestroyPen(graphPtr, penPtr);
        }
    }
    return TCL_OK;
}

/**
 * @ingroup pen_internals
 * @brief List visible pen names matching any supplied glob pattern.
 * @param graphPtr Live owning graph.
 * @param interp Command interpreter.
 * @param objc Full widget-command argument count, already checked by dispatch.
 * @param objv Borrowed full argument vector, with pen subcommand at index two.
 * Walks the hash table, skips pending records and appends each matching name
 * once, even if multiple patterns match. With no patterns, returns all visible
 * names. Result order is hash traversal order, not sorting or creation order.
 * Returns TCL_OK without acquiring references to listed pens.
 */
static int NamesOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_HashSearch cursor;
    Pen *penPtr;
    Tcl_Size i;
    Tcl_HashEntry *hPtr;

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

/**
 * @ingroup pen_internals
 * @brief Return the concrete class identifier of a visible pen.
 * @param graphPtr Live owning graph.
 * @param interp Command interpreter.
 * @param objc Full widget-command argument count, already checked by dispatch.
 * @param objv Borrowed full argument vector, with pen subcommand at index two.
 * Looks up objv[3] and returns its classUid as a Tcl string. Returns TCL_ERROR
 * for unknown or delete-pending names. Strip/polar uses report the normalized
 * line implementation rather than a distinct pen class.
 */
static int TypeOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Pen *penPtr;

    penPtr = NameToPen(graphPtr, objv[3]);
    if (penPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(penPtr->classUid, -1));
    return TCL_OK;
}

static const PenOpSpec penOps[] = {{{"cget", 5, 5, "penName option"}, CgetOp},
                                   {{"configure", 4, 0, "penName ?penName?... ?option value?..."}, ConfigureOp},
                                   {{"create", 4, 0, "penName ?option value?..."}, CreateOp},
                                   {{"delete", 3, 0, "?penName?..."}, DeleteOp},
                                   {{"names", 3, 0, "?pattern?..."}, NamesOp},
                                   {{"type", 4, 4, "penName"}, TypeOp},
                                   {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup pen_internals
 * @brief Dispatch a typed named-pen subcommand.
 * @param graphPtr Live owning graph.
 * @param interp Command interpreter.
 * @param objc Full widget-command argument count, already checked by dispatch.
 * @param objv Borrowed full argument vector, with pen subcommand at index two.
 * Uses Rbc_GetOpIndexFromObj to validate the subcommand and argument bounds,
 * then invokes its typed handler. Returns the handler result or TCL_ERROR from
 * dispatch. Handler-specific parsing still validates option/value structure.
 * This dispatcher supplies no independent graph or pen lifetime protection.
 */
int Rbc_PenOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, penOps, (Tcl_Size)sizeof(penOps[0]), RBC_OP_ARG2, objc, objv, &index) != TCL_OK) {
        return TCL_ERROR;
    }
    return penOps[index].proc(graphPtr, interp, objc, objv);
}
