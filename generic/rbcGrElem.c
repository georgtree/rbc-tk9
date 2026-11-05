/*
 * rbcGrElem.c --
 *
 *      This module implements generic elements for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @file rbcGrElem.c
 * Common element data, palettes, lifecycle and dispatch contracts.
 */
/** @defgroup palette_internals Element palette internals
 * Private helpers; no new public API.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
/** @defgroup element_transaction_internals Element transaction internals
 * Candidate ownership, option identity and retained representations.
 */

/** @defgroup element_data_internals Element data and value-label internals
 * Borrowed arrays, notifications and source-value presentation.
 */

/** @defgroup element_lifecycle_internals Element lifecycle and display dispatch
 * Registry ownership, option rollback, teardown and viewport-scoped rendering.
 */

/** @defgroup element_command_internals Element commands and binding tags
 * Query results, mutation boundaries, search publication and dispatch.
 */

#include "rbcGraph.h"
#include "rbcGrTrack.h"
#include "rbcChain.h"
#include <X11/Xutil.h>

#include "rbcGrElem.h"
#include "rbcRender.h"
#include "rbcVectorInt.h"

static Rbc_VectorChangedProc VectorChangedProc;

static void FormatValueLabel(char *buffer, size_t bufferSize, const char *format, int show, double x, double y);
static void DestroyPalette(Graph *graphPtr, Rbc_Chain *palette);
static int ParseStylesObj(Graph *graphPtr, Element *elemPtr, Tcl_Obj *objPtr, size_t styleSize,
                          Rbc_Chain **palettePtrPtr);

static int GetPenStyleFromObj(Graph *graphPtr, Tcl_Obj *objPtr, Rbc_Uid type, PenStyle *stylePtr);
static void SyncElemVector(ElemVector *vPtr);
static void FindRange(ElemVector *vPtr);
static int EvalExprListObj(Tcl_Interp *interp, Tcl_Obj *listObjPtr, Tcl_Size *nElemPtr, double **arrayPtr);
static int GetIndex(Tcl_Interp *interp, Tcl_Size nPoints, const char *string, Tcl_Size *indexPtr);
static int NameToElement(Graph *graphPtr, Tcl_Obj *nameObj, Element **elemPtrPtr);
static int CreateElement(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid);
static void DestroyElement(Graph *graphPtr, Element *elemPtr);
static int RebuildDisplayList(Graph *graphPtr, Tcl_Obj *newList);
static int InitElementOptions(Graph *graphPtr, Element *elemPtr);
static int ConfigureElementOptions(Graph *graphPtr, Element *elemPtr, Tcl_Size objc, Tcl_Obj *const objv[],
                                   int *maskPtr);
static void ReleaseElementResources(Graph *graphPtr, Element *elemPtr);

typedef int RbcGrElementOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcGrElementOp *proc;
} ElementOpSpec;

static RbcGrElementOp ActivateOp;
static RbcGrElementOp BindOp;
static RbcGrElementOp CreateOp;
static RbcGrElementOp ConfigureOp;
static RbcGrElementOp DeactivateOp;
static RbcGrElementOp DeleteOp;
static RbcGrElementOp ExistsOp;
static RbcGrElementOp GetOp;
static RbcGrElementOp NamesOp;
static RbcGrElementOp ShowOp;
static RbcGrElementOp TypeOp;

typedef enum {
    ELEM_DATA_OPTION_NONE,
    ELEM_DATA_OPTION_PAIRS,
    ELEM_DATA_OPTION_WEIGHTS,
    ELEM_DATA_OPTION_X,
    ELEM_DATA_OPTION_Y,
    ELEM_DATA_OPTION_X_ERROR,
    ELEM_DATA_OPTION_X_HIGH,
    ELEM_DATA_OPTION_X_LOW,
    ELEM_DATA_OPTION_Y_ERROR,
    ELEM_DATA_OPTION_Y_HIGH,
    ELEM_DATA_OPTION_Y_LOW
} ElemDataOption;

#define ELEM_DATA_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

typedef enum { ELEM_PEN_OPTION_NONE, ELEM_PEN_OPTION_ACTIVE, ELEM_PEN_OPTION_NORMAL } ElemPenOption;

#define ELEM_PEN_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

typedef enum { ELEM_AXIS_OPTION_NONE, ELEM_AXIS_OPTION_X, ELEM_AXIS_OPTION_Y } ElemAxisOption;

#define ELEM_AXIS_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

/**
 * @ingroup element_data_internals
 * @brief Validate element-array multiplication before allocation.
 * @param interp Interpreter receiving an overflow/size diagnostic.
 * @param count Nonnegative logical element count.
 * @param elementSize Positive byte size of one element.
 * @param[out] byteCountPtr Receives the product only on success.
 * @return TCL_OK on representable multiplication, TCL_ERROR otherwise.
 * Zero count is valid and yields zero bytes. Does not allocate memory or validate
 * arithmetic used by the caller to compute count.
 */
static int GetElemArrayByteCount(Tcl_Interp *interp, Tcl_Size count, size_t elementSize, size_t *byteCountPtr) {
    if ((count < 0) || (elementSize == 0) || ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / elementSize))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("element data array is too large", -1));
        return TCL_ERROR;
    }
    *byteCountPtr = (size_t)count * elementSize;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */
/**
 * @ingroup palette_internals
 * @brief Parse one palette style and acquire its named pen reference.
 * @param graphPtr Live graph and interpreter for lookup/errors.
 * @param objPtr Borrowed list containing penName or penName/min/max.
 * @param type Required element pen class, normalized by Rbc_GetPen.
 * @param stylePtr Candidate style with default weight already initialized.
 * @return TCL_OK transferring one pen reference, or TCL_ERROR without changing
 * stylePtr. Numeric endpoints are converted before acquiring the reference.
 * The one-name form preserves the candidate weight. Three items replace it via
 * SetWeight; no ascending-range check is performed here. This helper does not
 * release an existing destination pen and does not evaluate Tcl expressions.
 */
static int GetPenStyleFromObj(Graph *graphPtr, Tcl_Obj *objPtr, Rbc_Uid type, PenStyle *stylePtr) {
    Tcl_Interp *interp;
    Tcl_Obj **objv;
    Tcl_Size objc;
    Pen *penPtr;
    double min;
    double max;

    interp = graphPtr->interp;
    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((objc != 1) && (objc != 3)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad style \"%s\": should be "
                                               "\"penName\" or \"penName min max\"",
                                               Tcl_GetString(objPtr)));
        return TCL_ERROR;
    }
    /*
     * Validate the numeric range before acquiring a pen reference.
     */
    if (objc == 3) {
        if (Tcl_GetDoubleFromObj(interp, objv[1], &min) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Tcl_GetDoubleFromObj(interp, objv[2], &max) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    penPtr = NULL;
    if (Rbc_GetPen(graphPtr, Tcl_GetString(objv[0]), type, &penPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc == 3) {
        SetWeight(stylePtr->weight, min, max);
    }
    stylePtr->penPtr = penPtr;
    return TCL_OK;
}

/**
 * @ingroup element_data_internals
 * @brief Validate the list structure of an optional value-label command prefix.
 * @param interp Interpreter for list-syntax errors.
 * @param commandObjPtr Borrowed prefix object; NULL and empty lists are accepted.
 * @return TCL_OK for valid structure, TCL_ERROR otherwise.
 * Does not resolve the command name, execute it, or guarantee that later evaluation
 * succeeds. An empty prefix selects ordinary format-based labels.
 */
int Rbc_ValidateValueCommand(Tcl_Interp *interp, Tcl_Obj *commandObjPtr) {
    Tcl_Size length;

    if (commandObjPtr == NULL) {
        return TCL_OK;
    }
    return Tcl_ListObjLength(interp, commandObjPtr, &length);
}

/**
 * @ingroup element_data_internals
 * @brief Parse exactly two signed integer pixel offsets transactionally.
 * @param interp Interpreter for list/number diagnostics.
 * @param objPtr Borrowed two-element list.
 * @param[out] offsetPtr Receives both coordinates only after both integers parse.
 * @return TCL_OK on success; TCL_ERROR leaves the entire point unchanged.
 * Uses Tcl_GetIntFromObj, not screen-unit conversion or expression evaluation.
 * Negative values are permitted; the resulting ints are stored as doubles.
 */
int Rbc_GetValueOffset(Tcl_Interp *interp, Tcl_Obj *objPtr, Point2D *offsetPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    int dx, dy;

    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc != 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("-valueoffset must contain two integer pixel offsets", -1));
        return TCL_ERROR;
    }
    if ((Tcl_GetIntFromObj(interp, objv[0], &dx) != TCL_OK) ||
        (Tcl_GetIntFromObj(interp, objv[1], &dy) != TCL_OK)) {
        return TCL_ERROR;
    }
    offsetPtr->x = dx;
    offsetPtr->y = dy;
    return TCL_OK;
}

/**
 * @ingroup element_data_internals
 * @brief Obtain a referenced label object from fallback formatting or a callback.
 * @param elemPtr Live element with graph, interpreter, window and name.
 * @param commandObjPtr Optional borrowed command-prefix list.
 * @param format Validated fallback printf format or NULL.
 * @param show Coordinate selection; SHOW_NONE suppresses callback evaluation.
 * @param index Original source index passed to the command.
 * @param x Source X value.
 * @param y Source Y value.
 * @return Tcl object with one owned reference; caller must Tcl_DecrRefCount it.
 * Copies bounded fallback text first. A nonempty prefix is duplicated and invoked
 * globally with graph path, element name, index, X and Y appended. Successful text
 * is retained without the fallback buffer limit; errors go to the background and
 * retain fallback text. Saves/restores the enclosing interpreter state. Does not
 * preserve drawing records against callback mutation: callbacks must not mutate
 * drawing state or enter nested event loops.
 */
Tcl_Obj *Rbc_GetElementValueLabel(Element *elemPtr, Tcl_Obj *commandObjPtr, const char *format,
                                 int show, Tcl_Size index, double x, double y) {
    Tcl_Interp *interp = elemPtr->graphPtr->interp;
    Tcl_Obj *labelObjPtr;
    Tcl_Obj *cmdObjPtr;
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_InterpState state;
    int result;
    char fallback[RBC_VALUE_LABEL_SIZE];

    /* Copy fallback text before evaluating any application code. */
    FormatValueLabel(fallback, sizeof(fallback), format, show, x, y);
    labelObjPtr = Tcl_NewStringObj(fallback, -1);
    Tcl_IncrRefCount(labelObjPtr);
    if ((commandObjPtr == NULL) || (show == SHOW_NONE)) {
        return labelObjPtr;
    }
    state = Tcl_SaveInterpState(interp, TCL_OK);
    result = Tcl_ListObjLength(interp, commandObjPtr, &objc);
    if ((result == TCL_OK) && (objc == 0)) {
        Tcl_RestoreInterpState(interp, state);
        return labelObjPtr;
    }
    if (result == TCL_OK) {
        /* The option object is Tk-owned: append only to a duplicate. */
        cmdObjPtr = Tcl_DuplicateObj(commandObjPtr);
        Tcl_IncrRefCount(cmdObjPtr);
        Tcl_ListObjAppendElement(interp, cmdObjPtr,
            Tcl_NewStringObj(Tk_PathName(elemPtr->graphPtr->tkwin), -1));
        Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewStringObj(elemPtr->name, -1));
        Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewWideIntObj((Tcl_WideInt)index));
        Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewDoubleObj(x));
        Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewDoubleObj(y));
        result = Tcl_ListObjGetElements(interp, cmdObjPtr, &objc, &objv);
        if (result == TCL_OK) {
            Tcl_ResetResult(interp);
            result = Tcl_EvalObjv(interp, objc, objv, TCL_EVAL_GLOBAL);
        }
        if (result == TCL_OK) {
            Tcl_DecrRefCount(labelObjPtr);
            labelObjPtr = Tcl_GetObjResult(interp);
            Tcl_IncrRefCount(labelObjPtr);
        }
        Tcl_DecrRefCount(cmdObjPtr);
    }
    if (result != TCL_OK) {
        Tcl_AddErrorInfo(interp, "\n    (element value-label command)");
        Tcl_BackgroundException(interp, result);
    }
    Tcl_RestoreInterpState(interp, state);
    return labelObjPtr;
}

#define VALUE_FORMAT_LIMIT 200

/**
 * @ingroup element_data_internals
 * @brief Validate a printf format that receives one double argument.
 * @param interp Interpreter receiving a format diagnostic on error.
 * @param format Borrowed NUL-terminated format; NULL selects the default later.
 * @return TCL_OK when accepted, TCL_ERROR otherwise.
 * Permits literal-only text, escaped percent and at most one a/A/e/E/f/F/g/G
 * conversion with optional lowercase l. Limits format length, width and precision
 * to VALUE_FORMAT_LIMIT. Rejects dynamic width/precision and incompatible argument
 * types. Does not format a value or guarantee untruncated output in every buffer.
 */
int Rbc_ValidateValueFormat(Tcl_Interp *interp, const char *format) {
    const char *p;
    int nConversions;

    if (format == NULL) {
        return TCL_OK;
    }
    if (strlen(format) > VALUE_FORMAT_LIMIT) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("value format is too long: \"%s\"", format));
        return TCL_ERROR;
    }
    nConversions = 0;
    for (p = format; *p != '\0'; p++) {
        unsigned long value;

        if (*p != '%') {
            continue;
        }
        p++;
        /*
         * Literal percent.
         */
        if (*p == '%') {
            continue;
        }
        if (*p == '\0') {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("incomplete value format \"%s\"", format));
            return TCL_ERROR;
        }
        /*
         * Only one double is passed to snprintf().
         */
        if (nConversions != 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("value format \"%s\" contains more than one conversion", format));
            return TCL_ERROR;
        }
        /*
         * Standard printf flags.
         */
        while ((*p == '-') || (*p == '+') || (*p == ' ') || (*p == '#') || (*p == '0')) {
            p++;
        }
        /*
         * A dynamic width consumes another argument.
         */
        if (*p == '*') {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("value format \"%s\" cannot use a dynamic field width", format));
            return TCL_ERROR;
        }
        value = 0;
        while ((*p >= '0') && (*p <= '9')) {
            if (value <= (unsigned long)VALUE_FORMAT_LIMIT) {
                value = value * 10u + (unsigned long)(*p - '0');
            }
            p++;
        }
        if (value > (unsigned long)VALUE_FORMAT_LIMIT) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("value format \"%s\" has an excessive field width", format));
            return TCL_ERROR;
        }
        /*
         * Precision.
         */
        if (*p == '.') {
            p++;
            if (*p == '*') {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("value format \"%s\" cannot use a dynamic precision", format));
                return TCL_ERROR;
            }
            value = 0;
            while ((*p >= '0') && (*p <= '9')) {
                if (value <= (unsigned long)VALUE_FORMAT_LIMIT) {
                    value = value * 10u + (unsigned long)(*p - '0');
                }
                p++;
            }
            if (value > (unsigned long)VALUE_FORMAT_LIMIT) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("value format \"%s\" has an excessive precision", format));
                return TCL_ERROR;
            }
        }
        /*
         * 'l' is valid with floating conversions for printf.
         * 'L' is deliberately rejected because it requires a
         * long double rather than the double we supply.
         */
        if (*p == 'l') {
            p++;
        }
        if ((*p != 'a') && (*p != 'A') && (*p != 'e') && (*p != 'E') && (*p != 'f') && (*p != 'F') && (*p != 'g') &&
            (*p != 'G')) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("invalid floating-point conversion in value format \"%s\"", format));
            return TCL_ERROR;
        }
        nConversions++;
    }
    return TCL_OK;
}

/**
 * @ingroup element_data_internals
 * @brief Format selected source coordinates into a bounded fallback buffer.
 * @param buffer Writable storage of bufferSize bytes.
 * @param bufferSize Capacity including terminator; zero performs no writes.
 * @param format Previously validated format, or NULL for percent-g formatting.
 * @param show SHOW_X, SHOW_Y, SHOW_BOTH or a value producing empty text.
 * @param x Source X value.
 * @param y Source Y value.
 * Formats X/Y independently with one double each; BOTH separates results by a
 * comma. Truncation remains terminated and a truncated X prevents Y append.
 * Negative snprintf results clear the affected output. Performs no Tcl callback
 * and does not validate the format; validation must precede this helper.
 */
static void FormatValueLabel(char *buffer, size_t bufferSize, const char *format, int show, double x, double y) {
    const char *fmt;
    int n;
    size_t used;

    if (bufferSize == 0) {
        return;
    }
    buffer[0] = '\0';
    fmt = (format != NULL) ? format : "%g";
    switch (show) {
    case SHOW_X:
        n = snprintf(buffer, bufferSize, fmt, x);
        if (n < 0) {
            buffer[0] = '\0';
        }
        break;
    case SHOW_Y:
        n = snprintf(buffer, bufferSize, fmt, y);
        if (n < 0) {
            buffer[0] = '\0';
        }
        break;
    case SHOW_BOTH:
        n = snprintf(buffer, bufferSize, fmt, x);
        if (n < 0) {
            buffer[0] = '\0';
            break;
        }
        used = (size_t)n;
        /*
         * snprintf() reports the number of characters that would have
         * been written.  Do not append if the first value was
         * truncated.
         */
        if (used >= bufferSize) {
            buffer[bufferSize - 1] = '\0';
            break;
        }
        if ((used + 1) >= bufferSize) {
            break;
        }
        buffer[used++] = ',';
        buffer[used] = '\0';
        n = snprintf(buffer + used, bufferSize - used, fmt, y);
        if (n < 0) {
            buffer[used] = '\0';
        }
        break;
    default:
        break;
    }
}

/**
 * @ingroup element_data_internals
 * @brief Refresh a data slot's borrowed vector storage and cached extrema.
 * @param vPtr Slot whose vecPtr identifies a live real vector.
 * Copies length, capacity, numeric-array pointer and range from the source.
 * Does not acquire a reference, install callbacks, record a changed interval or
 * schedule redraw. The source vector retains ownership of its numeric array.
 */
static void SyncElemVector(ElemVector *vPtr) {
    vPtr->nValues = Rbc_VectorLength(vPtr->vecPtr);
    vPtr->arraySize = Rbc_VectorSize(vPtr->vecPtr);
    vPtr->valueArr = Rbc_VectorData(vPtr->vecPtr);
    Rbc_VectorGetRange(vPtr->vecPtr, &vPtr->min, &vPtr->max);
}

/**
 * @ingroup element_data_internals
 * @brief Compute finite extrema for a literal element data slot.
 * @param vPtr Initialized slot with storage for its nonnegative nValues.
 * Returns without changing cached extrema for empty or NULL storage. Otherwise
 * ignores nonfinite values; an all-nonfinite array yields DBL_MAX and -DBL_MAX.
 * Does not compute a positive minimum, despite the historical comment. Does not
 * change the array, allocate memory or notify observers.
 */
static void FindRange(ElemVector *vPtr) {
    Tcl_Size i;
    double *x;
    double min;
    double max;

    if ((vPtr->nValues < 1) || (vPtr->valueArr == NULL)) {
        return;
    }
    x = vPtr->valueArr;
    min = DBL_MAX;
    max = -DBL_MAX;
    for (i = 0; i < vPtr->nValues; i++) {
        if (FINITE(x[i])) {
            min = x[i];
            max = x[i];
            break;
        }
    }
    for (; i < vPtr->nValues; i++) {
        if (FINITE(x[i])) {
            if (x[i] < min) {
                min = x[i];
            } else if (x[i] > max) {
                max = x[i];
            }
        }
    }
    vPtr->min = min;
    vPtr->max = max;
}

/**
 * @ingroup element_data_internals
 * @brief Find a finite sample at or above a supplied lower bound.
 * @param vPtr Slot with valid storage for every value counted by nValues.
 * @param minLimit Lower bound and fallback value when no usable minimum is found.
 * @return Smallest qualifying value, or minLimit if the accumulator remains
 * DBL_MAX. A sole qualifying DBL_MAX value is therefore indistinguishable from
 * no match. Does not modify cached extrema or filter minLimit itself.
 */
double Rbc_FindElemVectorMinimum(ElemVector *vPtr, double minLimit) {
    const double *arr;
    double min;
    Tcl_Size i;

    min = DBL_MAX;
    arr = vPtr->valueArr;
    for (i = 0; i < vPtr->nValues; i++) {
        double value;

        value = arr[i];
        if ((!FINITE(value)) || (value < minLimit)) {
            continue;
        }
        if (value < min) {
            min = value;
        }
    }
    if (min == DBL_MAX) {
        min = minLimit;
    }
    return min;
}

void Rbc_FreeElemVector(ElemVector *vPtr) {
    if (vPtr->clientId != NULL) {
        Rbc_FreeVectorId(vPtr->clientId);
    } else if (vPtr->valueArr != NULL) {
        ckfree((char *)vPtr->valueArr);
    }
    memset(vPtr, 0, sizeof(*vPtr));
}

/**
 * @ingroup element_data_internals
 * @brief Accumulate a pending source-change interval conservatively.
 * @param vPtr Initialized element data slot.
 * @param haveRange Nonzero when first/last describe a precise changed range.
 * @param first Inclusive first source index, valid when haveRange is nonzero.
 * @param last Inclusive last source index, valid when haveRange is nonzero.
 * First notification initializes the pending state. Precise intervals are merged
 * by their outer bounds; unknown/full changes set changeAll and clear endpoints.
 * Later precise updates cannot narrow a pending full change. Does not validate
 * indices, fetch data, consume the pending state or request redraw.
 */
static void RecordElemVectorChange(ElemVector *vPtr, int haveRange, Tcl_Size first, Tcl_Size last) {
    if (!vPtr->changePending) {
        vPtr->changePending = TRUE;
        if (haveRange) {
            vPtr->changeAll = FALSE;
            vPtr->changedFirst = first;
            vPtr->changedLast = last;
        } else {
            vPtr->changeAll = TRUE;
            vPtr->changedFirst = 0;
            vPtr->changedLast = 0;
        }
        return;
    }
    /*
     * Once an unknown/full update has been observed, later ranged
     * updates cannot make the combined pending change precise again.
     */
    if (vPtr->changeAll) {
        return;
    }
    if (!haveRange) {
        vPtr->changeAll = TRUE;
        vPtr->changedFirst = 0;
        vPtr->changedLast = 0;
        return;
    }
    if (first < vPtr->changedFirst) {
        vPtr->changedFirst = first;
    }
    if (last > vPtr->changedLast) {
        vPtr->changedLast = last;
    }
}

/**
 * @ingroup element_data_internals
 * @brief Refresh or invalidate a stable element slot during vector notification.
 * @param interp Interpreter supplied by the vector notification service.
 * @param clientData Stable ElemVector field whose owning element remains live.
 * @param notify UPDATE or DESTROY; other values take the update path.
 * During update, reads the client-specific changed range inside its valid callback
 * scope, records it and refreshes source storage. Destruction records a full
 * change and clears clientId, valueArr and nValues, but does not zero every cached
 * field. Marks axes and mapping dirty; requests backing-store redraw only when
 * neither hidden nor plotHidden. Never draws synchronously. The update path
 * assumes its registered client ID still resolves successfully.
 */
static void VectorChangedProc(Tcl_Interp *interp, ClientData clientData, Rbc_VectorNotify notify) {
    ElemVector *vPtr = clientData;
    Element *elemPtr = vPtr->elemPtr;
    Graph *graphPtr = elemPtr->graphPtr;
    switch (notify) {
    case RBC_VECTOR_NOTIFY_DESTROY:
        /*
         * Destruction invalidates the complete source vector.
         */
        RecordElemVectorChange(vPtr, FALSE, 0, 0);
        vPtr->clientId = NULL;
        vPtr->valueArr = NULL;
        vPtr->nValues = 0;
        break;
    case RBC_VECTOR_NOTIFY_UPDATE:
    default: {
        Tcl_Size first = 0;
        Tcl_Size last = 0;
        int haveRange;

        /*
         * Query the range while the vector callback is active.  The
         * client-specific snapshot is intentionally valid only for the
         * duration of this notification.
         */
        haveRange = Rbc_VectorGetChangedRange(vPtr->clientId, &first, &last);
        RecordElemVectorChange(vPtr, haveRange, first, last);
        Rbc_GetVectorById(interp, vPtr->clientId, &vPtr->vecPtr);
        SyncElemVector(vPtr);
        break;
    }
    }
    graphPtr->flags |= RESET_AXES;
    elemPtr->flags |= MAP_ITEM;
    if ((!elemPtr->hidden) && (!elemPtr->plotHidden)) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
}

/**
 * @ingroup element_data_internals
 * @brief Refresh pending named-vector inputs before synchronous layout/export.
 * @param graphPtr Live graph with initialized element registry.
 * Visits registered elements, including hidden ones, and checks common X/Y,
 * weight and six error slots. Pending clients refresh borrowed storage and record
 * a full change, marking mapping, axes and backing store dirty. Does not consume
 * queued notifications, run their callbacks, process events or independently
 * schedule an idle redraw. Literal inputs and nonpending clients are untouched.
 */
void Rbc_SyncElementVectors(Graph *graphPtr) {
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &search); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&search)) {
        Element *elemPtr = Tcl_GetHashValue(hPtr);
        ElemVector *vectors[] = {&elemPtr->x, &elemPtr->y, &elemPtr->w,
            &elemPtr->xError, &elemPtr->xLow, &elemPtr->xHigh,
            &elemPtr->yError, &elemPtr->yLow, &elemPtr->yHigh};
        size_t i;

        for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
            ElemVector *vPtr = vectors[i];
            if (vPtr->clientId != NULL && Rbc_VectorNotifyPending(vPtr->clientId)) {
                SyncElemVector(vPtr);
                RecordElemVectorChange(vPtr, FALSE, 0, 0);
                elemPtr->flags |= MAP_ITEM;
                graphPtr->flags |= RESET_AXES | REDRAW_BACKING_STORE;
            }
        }
    }
}

/**
 * @ingroup element_data_internals
 * @brief Evaluate a Tcl list into a newly owned double array.
 * @param interp Interpreter for list parsing, expression evaluation and errors.
 * @param listObjPtr Borrowed list object valid throughout evaluation.
 * @param[out] nElemPtr Cleared first; receives the list length on success.
 * @param[out] arrayPtr Cleared first; receives storage to release with ckfree.
 * @return TCL_OK on success, TCL_ERROR after releasing partial allocation.
 * Empty input yields zero/NULL. Uses checked byte counts and Tcl_AttemptAlloc.
 * Each entry is a Tcl expression, not merely a literal double: commands and
 * traces can run. The borrowed list-element array is not snapshotted here; callers
 * must not assume staging alone protects it or owner lifetimes across reentry.
 */
static int EvalExprListObj(Tcl_Interp *interp, Tcl_Obj *listObjPtr, Tcl_Size *nElemPtr, double **arrayPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_Size i;
    double *array;
    size_t byteCount;

    *nElemPtr = 0;
    *arrayPtr = NULL;
    if (Tcl_ListObjGetElements(interp, listObjPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    array = NULL;
    if (objc > 0) {
        if (GetElemArrayByteCount(interp, objc, sizeof(double), &byteCount) != TCL_OK) {
            return TCL_ERROR;
        }
        array = Tcl_AttemptAlloc(byteCount);
        if (array == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate element data array", -1));
            return TCL_ERROR;
        }
        for (i = 0; i < objc; i++) {
            /*
             * Preserve the existing behaviour: list members may be Tcl
             * numeric expressions rather than only literal doubles.
             */
            if (Tcl_ExprDouble(interp, Tcl_GetString(objv[i]), array + i) != TCL_OK) {
                ckfree(array);
                return TCL_ERROR;
            }
        }
    }
    *nElemPtr = objc;
    *arrayPtr = array;
    return TCL_OK;
}

int Rbc_ParseElemVectorObj(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr, ElemVector *candidatePtr) {
    const char *string;

    memset(candidatePtr, 0, sizeof(*candidatePtr));
    candidatePtr->elemPtr = elemPtr;
    string = Tcl_GetString(objPtr);
    if (Rbc_VectorExists2(interp, string)) {
        Rbc_VectorId clientId;

        clientId = Rbc_AllocVectorId(interp, string);
        if (clientId == NULL) {
            return TCL_ERROR;
        }
        if (Rbc_GetVectorById(interp, clientId, &candidatePtr->vecPtr) != TCL_OK) {
            Rbc_FreeVectorId(clientId);
            memset(candidatePtr, 0, sizeof(*candidatePtr));
            return TCL_ERROR;
        }
        if (Rbc_VectorGetType(candidatePtr->vecPtr) != RBC_VECTOR_REAL) {
            Tcl_SetObjResult(interp,
                             Tcl_ObjPrintf("vector \"%s\" is complex; graph elements require real vectors", string));
            Rbc_FreeVectorId(clientId);
            memset(candidatePtr, 0, sizeof(*candidatePtr));
            return TCL_ERROR;
        }
        candidatePtr->clientId = clientId;
        SyncElemVector(candidatePtr);
        /*
         * Do not install VectorChangedProc yet. Its clientData must
         * point to the final, stable ElemVector member, not this
         * temporary candidate.
         */
        return TCL_OK;
    }
    if (EvalExprListObj(interp, objPtr, &candidatePtr->nValues, &candidatePtr->valueArr) != TCL_OK) {
        memset(candidatePtr, 0, sizeof(*candidatePtr));
        return TCL_ERROR;
    }
    candidatePtr->arraySize = candidatePtr->nValues;
    FindRange(candidatePtr);
    return TCL_OK;
}

/**
 * @ingroup element_data_internals
 * @brief Split evaluated alternating coordinates into two owned candidate slots.
 * @param interp Interpreter for parsing and allocation diagnostics.
 * @param elemPtr Live owner recorded in both candidates.
 * @param objPtr Borrowed list of alternating X/Y expressions.
 * @param[out] xCandidatePtr Fresh slot, zeroed before parsing.
 * @param[out] yCandidatePtr Distinct fresh slot, zeroed before parsing.
 * @return TCL_OK with both arrays and extrema prepared; TCL_ERROR with temporary
 * arrays released. Caller may free both slots after either result.
 * Evaluates the entire list before rejecting odd length. Thus evaluation side
 * effects may already have occurred when odd input fails. Allocates a temporary
 * flat array then separate X/Y arrays; empty input leaves both arrays NULL.
 * Never treats this paired list as a named-vector reference.
 */
static int ParseElemVectorPairsObj(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr, ElemVector *xCandidatePtr,
                                ElemVector *yCandidatePtr) {
    double *pairArr;
    Tcl_Size nElem;
    Tcl_Size nValues;
    Tcl_Size i;
    size_t byteCount;

    memset(xCandidatePtr, 0, sizeof(*xCandidatePtr));
    memset(yCandidatePtr, 0, sizeof(*yCandidatePtr));
    xCandidatePtr->elemPtr = elemPtr;
    yCandidatePtr->elemPtr = elemPtr;
    if (EvalExprListObj(interp, objPtr, &nElem, &pairArr) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((nElem & 1) != 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("odd number of data points", -1));
        if (pairArr != NULL) {
            ckfree(pairArr);
        }
        return TCL_ERROR;
    }
    nValues = nElem / 2;
    if (nValues > 0) {
        if (GetElemArrayByteCount(interp, nValues, sizeof(double), &byteCount) != TCL_OK) {
            ckfree(pairArr);
            return TCL_ERROR;
        }
        xCandidatePtr->valueArr = Tcl_AttemptAlloc(byteCount);
        if (xCandidatePtr->valueArr == NULL) {
            ckfree(pairArr);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate element X data", -1));
            return TCL_ERROR;
        }
        yCandidatePtr->valueArr = Tcl_AttemptAlloc(byteCount);
        if (yCandidatePtr->valueArr == NULL) {
            ckfree(xCandidatePtr->valueArr);
            xCandidatePtr->valueArr = NULL;
            ckfree(pairArr);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate element Y data", -1));
            return TCL_ERROR;
        }
        for (i = 0; i < nValues; i++) {
            xCandidatePtr->valueArr[i] = pairArr[i * 2];
            yCandidatePtr->valueArr[i] = pairArr[i * 2 + 1];
        }
        ckfree(pairArr);
    }
    xCandidatePtr->nValues = nValues;
    xCandidatePtr->arraySize = nValues;
    yCandidatePtr->nValues = nValues;
    yCandidatePtr->arraySize = nValues;
    FindRange(xCandidatePtr);
    FindRange(yCandidatePtr);
    return TCL_OK;
}

/**
 * @ingroup element_transaction_internals
 * @brief Move a parsed data slot into its stable element destination.
 * @param elemPtr Live owner of the destination field.
 * @param destPtr Initialized live slot to replace; distinct from candidatePtr.
 * @param candidatePtr Successfully parsed slot whose ownership is transferred.
 * Releases the old slot, copies the candidate and zeroes it. Installs the owning
 * element and, for named-vector data, a change callback whose client data points
 * to destPtr rather than temporary storage. Does not destroy the source vector,
 * copy its numeric storage, or schedule redraw independently.
 */
static void CommitElemVector(Element *elemPtr, ElemVector *destPtr, ElemVector *candidatePtr) {
    Rbc_FreeElemVector(destPtr);

    *destPtr = *candidatePtr;
    memset(candidatePtr, 0, sizeof(*candidatePtr));
    destPtr->elemPtr = elemPtr;
    if (destPtr->clientId != NULL) {
        /*
         * The callback must refer to the stable destination member,
         * never to the temporary candidate.
         */
        Rbc_SetVectorChangedProc(destPtr->clientId, VectorChangedProc, destPtr);
    }
}

/**
 * @ingroup element_transaction_internals
 * @brief Identify an element's common data option after Tk validation.
 * @param elemPtr Element with concrete optionSpecs.
 * @param objPtr Borrowed original option-name object.
 * @return The corresponding ElemDataOption, or NONE for unrelated/unresolved names.
 * Uses Rbc_GetCanonicalOptionName so synonyms and accepted abbreviations resolve
 * through the concrete table. Does not parse values, acquire resources or change
 * state. Exact -x and -y names must retain precedence over longer data names.
 */
static ElemDataOption GetElemDataOption(Element *elemPtr, Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, elemPtr->optionSpecs);
    if (name == NULL) {
        return ELEM_DATA_OPTION_NONE;
    }
    if (strcmp(name, "-data") == 0) {
        return ELEM_DATA_OPTION_PAIRS;
    }
    if (strcmp(name, "-weights") == 0) {
        return ELEM_DATA_OPTION_WEIGHTS;
    }
    if (strcmp(name, "-x") == 0) {
        return ELEM_DATA_OPTION_X;
    }
    if (strcmp(name, "-y") == 0) {
        return ELEM_DATA_OPTION_Y;
    }
    if (strcmp(name, "-xerror") == 0) {
        return ELEM_DATA_OPTION_X_ERROR;
    }
    if (strcmp(name, "-xhigh") == 0) {
        return ELEM_DATA_OPTION_X_HIGH;
    }
    if (strcmp(name, "-xlow") == 0) {
        return ELEM_DATA_OPTION_X_LOW;
    }
    if (strcmp(name, "-yerror") == 0) {
        return ELEM_DATA_OPTION_Y_ERROR;
    }
    if (strcmp(name, "-yhigh") == 0) {
        return ELEM_DATA_OPTION_Y_HIGH;
    }
    if (strcmp(name, "-ylow") == 0) {
        return ELEM_DATA_OPTION_Y_LOW;
    }
    return ELEM_DATA_OPTION_NONE;
}

/**
 * @ingroup palette_internals
 * @brief Recover the canonical active or normal pen option identity.
 * @param elemPtr Element with its concrete optionSpecs.
 * @param objPtr Borrowed original option-name object.
 * @return ELEM_PEN_OPTION_ACTIVE, ELEM_PEN_OPTION_NORMAL or NONE.
 * Uses Rbc_GetCanonicalOptionName after Tk option validation, recognizing aliases
 * and accepted abbreviations. Does not acquire references or alter the element.
 */
static ElemPenOption GetElemPenOption(Element *elemPtr, Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, elemPtr->optionSpecs);
    if (name == NULL) {
        return ELEM_PEN_OPTION_NONE;
    }
    if (strcmp(name, "-activepen") == 0) {
        return ELEM_PEN_OPTION_ACTIVE;
    }
    if (strcmp(name, "-pen") == 0) {
        return ELEM_PEN_OPTION_NORMAL;
    }
    return ELEM_PEN_OPTION_NONE;
}

/**
 * @ingroup element_transaction_internals
 * @brief Identify an element's axis mapping option after Tk validation.
 * @param elemPtr Element with concrete optionSpecs.
 * @param objPtr Borrowed original option-name object.
 * @return X or Y axis identity, or NONE for unrelated/unresolved names.
 * Uses Rbc_GetCanonicalOptionName so synonyms and accepted abbreviations resolve
 * through the concrete table. Does not parse values, acquire resources or change
 * state. Exact -x and -y names must retain precedence over longer data names.
 */
static ElemAxisOption GetElemAxisOption(Element *elemPtr, Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, elemPtr->optionSpecs);
    if (name == NULL) {
        return ELEM_AXIS_OPTION_NONE;
    }
    if (strcmp(name, "-mapx") == 0) {
        return ELEM_AXIS_OPTION_X;
    }
    if (strcmp(name, "-mapy") == 0) {
        return ELEM_AXIS_OPTION_Y;
    }
    return ELEM_AXIS_OPTION_NONE;
}

/**
 * @ingroup element_transaction_internals
 * @brief Identify an element's state option after Tk validation.
 * @param elemPtr Element with concrete optionSpecs.
 * @param objPtr Borrowed original option-name object.
 * @return Nonzero only for the canonical -state option.
 * Uses Rbc_GetCanonicalOptionName so synonyms and accepted abbreviations resolve
 * through the concrete table. Does not parse values, acquire resources or change
 * state. Exact -x and -y names must retain precedence over longer data names.
 */
static int IsElemStateOption(Element *elemPtr, Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, elemPtr->optionSpecs);
    return ((name != NULL) && (strcmp(name, "-state") == 0));
}

/**
 * @ingroup element_transaction_internals
 * @brief Identify an element's binding tags option after Tk validation.
 * @param elemPtr Element with concrete optionSpecs.
 * @param objPtr Borrowed original option-name object.
 * @return Nonzero only for the canonical -bindtags option.
 * Uses Rbc_GetCanonicalOptionName so synonyms and accepted abbreviations resolve
 * through the concrete table. Does not parse values, acquire resources or change
 * state. Exact -x and -y names must retain precedence over longer data names.
 */
static int IsElemBindTagsOption(Element *elemPtr, Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, elemPtr->optionSpecs);
    return ((name != NULL) && (strcmp(name, "-bindtags") == 0));
}

/**
 * @ingroup element_transaction_internals
 * @brief Replace a candidate tag list only after its successor parses.
 * @param interp Interpreter for list syntax diagnostics.
 * @param objPtr Borrowed tag-list object; NULL represents an empty list.
 * @param transactionPtr Initialized transaction owning any earlier candidate.
 * @return TCL_OK on staging; TCL_ERROR preserves the earlier candidate.
 * Success frees the old candidate block, transfers the new single-block string
 * list and sets staged even for an empty list. Does not modify live element tags
 * or execute a binding script.
 */
static int StageElemTags(Tcl_Interp *interp, Tcl_Obj *objPtr, ElemTagsTransaction *transactionPtr) {
    char **newTags;

    newTags = NULL;
    if (Rbc_GetStringListFromObj(interp, objPtr, &newTags) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Do not discard the previous candidate until the replacement has
     * parsed successfully.
     */
    if (transactionPtr->tags != NULL) {
        ckfree((char *)transactionPtr->tags);
    }
    transactionPtr->tags = newTags;
    transactionPtr->staged = TRUE;
    return TCL_OK;
}

/**
 * @ingroup element_transaction_internals
 * @brief Release candidate binding tags and reset their transaction.
 * @param transactionPtr Initialized transaction, possibly empty or already committed.
 * Frees the owned string-list block if present and zeroes the complete record.
 * Does not alter the element or free tags already transferred to it.
 */
void Rbc_FreeElemTagsTransaction(ElemTagsTransaction *transactionPtr) {
    if (transactionPtr->tags != NULL) {
        ckfree((char *)transactionPtr->tags);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup element_transaction_internals
 * @brief Validate every relevant binding-tag occurrence before element commit.
 * @param graphPtr Live owning graph and interpreter.
 * @param elemPtr Element with a validated, even optionObjv/optionObjc sequence.
 * @param[out] transactionPtr Fresh transaction; any earlier owned candidate must
 * already have been released before this function zeroes the record.
 * @return TCL_OK on preparation; TCL_ERROR after freeing all staged tag storage.
 * On initial configuration, processes the retained default only if not explicitly
 * overridden. Then parses explicit occurrences in caller order, so a later valid
 * value cannot hide an earlier invalid list. Live tags remain unchanged.
 */
int Rbc_PrepareElemTagsTransaction(Graph *graphPtr, Element *elemPtr, ElemTagsTransaction *transactionPtr) {
    int explicitlySpecified;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitlySpecified = FALSE;
    assert((elemPtr->optionObjc & 1) == 0);
    /*
     * Determine whether -bindtags was supplied explicitly.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemBindTagsOption(elemPtr, elemPtr->optionObjv[i])) {
            explicitlySpecified = TRUE;
        }
    }
    /*
     * On the first modern configuration, process the effective default
     * or option-database value unless it was explicitly overridden.
     */
    if (!elemPtr->optionsConfigured && !explicitlySpecified && (elemPtr->bindTagsObjPtr != NULL)) {
        if (StageElemTags(graphPtr->interp, elemPtr->bindTagsObjPtr, transactionPtr) != TCL_OK) {
            goto error;
        }
    }
    /*
     * Process every explicit occurrence in caller order.
     *
     * Do not read only bindTagsObjPtr here. That field contains the
     * final value and would conceal an invalid earlier occurrence.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemBindTagsOption(elemPtr, elemPtr->optionObjv[i])) {
            if (StageElemTags(graphPtr->interp, elemPtr->optionObjv[i + 1], transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }
    return TCL_OK;

error:
    Rbc_FreeElemTagsTransaction(transactionPtr);
    return TCL_ERROR;
}

/**
 * @ingroup element_transaction_internals
 * @brief Transfer a prepared tag-list block into the live element.
 * @param elemPtr Live destination element owning its previous tags.
 * @param transactionPtr Successfully prepared tags transaction.
 * Does nothing unless staged. Otherwise moves tags (including NULL), clears the
 * candidate and flag, then frees the old live block. No parsing or binding script
 * execution occurs; subsequent transaction cleanup is safe.
 */
void Rbc_CommitElemTagsTransaction(Element *elemPtr, ElemTagsTransaction *transactionPtr) {
    char **oldTags;

    if (!transactionPtr->staged) {
        return;
    }
    oldTags = elemPtr->tags;
    elemPtr->tags = transactionPtr->tags;
    transactionPtr->tags = NULL;
    transactionPtr->staged = FALSE;
    if (oldTags != NULL) {
        ckfree((char *)oldTags);
    }
}

/**
 * @ingroup element_transaction_internals
 * @brief Validate state values in their original configuration order.
 * @param graphPtr Live graph supplying the error interpreter.
 * @param elemPtr Element with an even validated option sequence.
 * @param[out] transactionPtr Transaction reset before preparation.
 * @return TCL_OK when all relevant values pass; TCL_ERROR on the first bad state.
 * Uses the initial retained state only when not explicitly overridden, then checks
 * every explicit occurrence. Does not change live state. Unlike owning resource
 * transactions, failure may leave an earlier scalar candidate marked staged;
 * there is nothing to free, but callers must not commit after TCL_ERROR.
 */
int Rbc_PrepareElemStateTransaction(Graph *graphPtr, Element *elemPtr, ElemStateTransaction *transactionPtr) {
    int explicitlySpecified;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitlySpecified = FALSE;
    assert((elemPtr->optionObjc & 1) == 0);
    /*
     * Determine whether -state was supplied explicitly.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemStateOption(elemPtr, elemPtr->optionObjv[i])) {
            explicitlySpecified = TRUE;
        }
    }
    /*
     * During the first modern configuration, process the effective
     * default or option-database value unless the caller explicitly
     * overrides it.
     *
     * Elements whose option tables do not contain -state leave
     * stateObjPtr as NULL, so nothing is staged for them.
     */
    if (!elemPtr->optionsConfigured && !explicitlySpecified && (elemPtr->stateObjPtr != NULL)) {
        if (Rbc_GetStateFromObj(graphPtr->interp, elemPtr->stateObjPtr, &transactionPtr->state) != TCL_OK) {
            return TCL_ERROR;
        }
        transactionPtr->staged = TRUE;
    }
    /*
     * Process every explicit occurrence in caller order.
     *
     * Reading only stateObjPtr would incorrectly hide an invalid
     * earlier repeated occurrence because stateObjPtr contains only
     * the final retained value.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemStateOption(elemPtr, elemPtr->optionObjv[i])) {
            if (Rbc_GetStateFromObj(graphPtr->interp, elemPtr->optionObjv[i + 1], &transactionPtr->state) != TCL_OK) {
                return TCL_ERROR;
            }
            transactionPtr->staged = TRUE;
        }
    }
    return TCL_OK;
}

/**
 * @ingroup element_transaction_internals
 * @brief Install a successfully prepared scalar state.
 * @param elemPtr Live destination element.
 * @param transactionPtr Transaction whose preparation returned TCL_OK.
 * Copies state only when staged, then clears staged. Performs no resource cleanup,
 * redraw scheduling or binding repick; the configuration caller handles those.
 */
void Rbc_CommitElemStateTransaction(Element *elemPtr, ElemStateTransaction *transactionPtr) {
    if (transactionPtr->staged) {
        elemPtr->state = transactionPtr->state;
        transactionPtr->staged = FALSE;
    }
}

/**
 * @ingroup element_transaction_internals
 * @brief Acquire a replacement axis before releasing the staged predecessor.
 * @param graphPtr Live graph containing the axis registry.
 * @param objPtr Borrowed axis-name object; an empty name is not accepted.
 * @param transactionPtr Initialized axis transaction.
 * @param candidatePtrPtr Address of the candidate matching option.
 * @param option X or Y; any other identity is an internal fatal error.
 * @return TCL_OK after staging; TCL_ERROR retains the previous candidate.
 * Rbc_GetAxisFromObj checks in-use orientation (or assigns the class of an unused
 * axis) and acquires a reference without creating an absent axis. Success
 * releases the earlier staged reference, stores the new
 * one and sets its mask bit. Live element axes are unchanged.
 */
static int StageElemAxis(Graph *graphPtr, Tcl_Obj *objPtr, ElemAxisTransaction *transactionPtr, Axis **candidatePtrPtr,
                         ElemAxisOption option) {
    Axis *newAxisPtr;
    Rbc_Uid classUid;
    unsigned int mask;

    newAxisPtr = NULL;
    switch (option) {
    case ELEM_AXIS_OPTION_X:
        classUid = rbcXAxisUid;
        break;
    case ELEM_AXIS_OPTION_Y:
        classUid = rbcYAxisUid;
        break;
    case ELEM_AXIS_OPTION_NONE:
    default:
        Tcl_Panic("StageElemAxis called with invalid option");
        return TCL_ERROR;
    }
    /*
     * Element -mapx and -mapy values must identify an existing axis;
     * an empty axis name is not accepted.
     */
    if (Rbc_GetAxisFromObj(graphPtr, objPtr, classUid, FALSE, &newAxisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    mask = ELEM_AXIS_OPTION_MASK(option);
    /*
     * Resolve the replacement before releasing a previous candidate.
     */
    if (transactionPtr->stagedMask & mask) {
        Rbc_FreeAxisReference(graphPtr, *candidatePtrPtr);
    }
    *candidatePtrPtr = newAxisPtr;
    transactionPtr->stagedMask |= mask;
    return TCL_OK;
}

/**
 * @ingroup element_transaction_internals
 * @brief Release owned candidate axes and clear their transaction.
 * @param graphPtr Owning graph used for reference release.
 * @param transactionPtr Initialized, partially prepared or committed transaction.
 * Releases each non-NULL candidate independently, then zeroes the record. It does
 * not release references transferred to live element fields by commit.
 */
void Rbc_FreeElemAxisTransaction(Graph *graphPtr, ElemAxisTransaction *transactionPtr) {
    if (transactionPtr->xAxisPtr != NULL) {
        Rbc_FreeAxisReference(graphPtr, transactionPtr->xAxisPtr);
    }
    if (transactionPtr->yAxisPtr != NULL) {
        Rbc_FreeAxisReference(graphPtr, transactionPtr->yAxisPtr);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup element_transaction_internals
 * @brief Resolve ordered element axis options into owned candidates.
 * @param graphPtr Live owning graph.
 * @param elemPtr Element with even validated configuration arguments.
 * @param[out] transactionPtr Fresh transaction, zeroed before use.
 * @return TCL_OK on preparation; TCL_ERROR after releasing all staged references.
 * Initial mapx/mapy defaults are processed only when not explicitly overridden.
 * Every explicit occurrence is then resolved in caller order. Failure does not
 * change live mappings; successful output must be committed or freed.
 */
int Rbc_PrepareElemAxisTransaction(Graph *graphPtr, Element *elemPtr, ElemAxisTransaction *transactionPtr) {
    unsigned int explicitMask;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitMask = 0;
    assert((elemPtr->optionObjc & 1) == 0);
    /*
     * Determine which axis options were supplied explicitly.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemAxisOption option;

        option = GetElemAxisOption(elemPtr, elemPtr->optionObjv[i]);
        if (option != ELEM_AXIS_OPTION_NONE) {
            explicitMask |= ELEM_AXIS_OPTION_MASK(option);
        }
    }
    /*
     * During the first modern configuration, process effective
     * default or option-database values that were not overridden by
     * the caller.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & ELEM_AXIS_OPTION_MASK(ELEM_AXIS_OPTION_X)) && (elemPtr->mapXObjPtr != NULL)) {
            if (StageElemAxis(graphPtr, elemPtr->mapXObjPtr, transactionPtr, &transactionPtr->xAxisPtr,
                              ELEM_AXIS_OPTION_X) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_AXIS_OPTION_MASK(ELEM_AXIS_OPTION_Y)) && (elemPtr->mapYObjPtr != NULL)) {
            if (StageElemAxis(graphPtr, elemPtr->mapYObjPtr, transactionPtr, &transactionPtr->yAxisPtr,
                              ELEM_AXIS_OPTION_Y) != TCL_OK) {
                goto error;
            }
        }
    }
    /*
     * Process explicit options in their original order.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemAxisOption option;
        Tcl_Obj *valueObjPtr;

        option = GetElemAxisOption(elemPtr, elemPtr->optionObjv[i]);
        valueObjPtr = elemPtr->optionObjv[i + 1];
        switch (option) {
        case ELEM_AXIS_OPTION_X:
            if (StageElemAxis(graphPtr, valueObjPtr, transactionPtr, &transactionPtr->xAxisPtr, option) != TCL_OK) {
                goto error;
            }
            break;
        case ELEM_AXIS_OPTION_Y:
            if (StageElemAxis(graphPtr, valueObjPtr, transactionPtr, &transactionPtr->yAxisPtr, option) != TCL_OK) {
                goto error;
            }
            break;
        case ELEM_AXIS_OPTION_NONE:
            break;
        }
    }
    return TCL_OK;

error:
    Rbc_FreeElemAxisTransaction(graphPtr, transactionPtr);
    return TCL_ERROR;
}

/**
 * @ingroup element_transaction_internals
 * @brief Move prepared axis references to the element and release old mappings.
 * @param graphPtr Owning graph for release of replaced references.
 * @param elemPtr Live destination element.
 * @param transactionPtr Successfully prepared axis transaction.
 * Only staged X/Y slots are replaced. Clears transferred pointers and then the
 * mask, allowing later transaction cleanup. No lookup or parsing occurs here;
 * reference release may destroy a delete-pending axis whose count reaches zero.
 */
void Rbc_CommitElemAxisTransaction(Graph *graphPtr, Element *elemPtr, ElemAxisTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & ELEM_AXIS_OPTION_MASK(ELEM_AXIS_OPTION_X)) {
        Axis *oldAxisPtr;

        oldAxisPtr = elemPtr->axes.x;
        elemPtr->axes.x = transactionPtr->xAxisPtr;
        transactionPtr->xAxisPtr = NULL;
        if (oldAxisPtr != NULL) {
            Rbc_FreeAxisReference(graphPtr, oldAxisPtr);
        }
    }
    if (transactionPtr->stagedMask & ELEM_AXIS_OPTION_MASK(ELEM_AXIS_OPTION_Y)) {
        Axis *oldAxisPtr;

        oldAxisPtr = elemPtr->axes.y;
        elemPtr->axes.y = transactionPtr->yAxisPtr;
        transactionPtr->yAxisPtr = NULL;
        if (oldAxisPtr != NULL) {
            Rbc_FreeAxisReference(graphPtr, oldAxisPtr);
        }
    }
    transactionPtr->stagedMask = 0;
}

/**
 * @ingroup palette_internals
 * @brief Acquire a candidate pen before releasing an earlier staged reference.
 * @param graphPtr Live owning graph.
 * @param objPtr Borrowed pen name; NULL/empty stages no named pen.
 * @param penType Required concrete pen class.
 * @param transactionPtr Initialized pen transaction owning previous candidates.
 * @param candidatePtrPtr Address of the corresponding candidate pointer.
 * @param option ACTIVE or NORMAL, matching candidatePtrPtr.
 * @return TCL_OK on staging, TCL_ERROR retaining the prior candidate on lookup
 * failure. Marks the option staged even for NULL. The later normal-pen commit
 * substitutes the built-in pen; active NULL disables the active pen. The live
 * element is untouched, and any acquired reference belongs to the transaction.
 */
static int StageElemPen(Graph *graphPtr, Tcl_Obj *objPtr, Rbc_Uid penType, ElemPenTransaction *transactionPtr,
                        Pen **candidatePtrPtr, ElemPenOption option) {
    const char *name;
    Pen *newPenPtr;
    unsigned int mask;

    newPenPtr = NULL;
    if (objPtr != NULL) {
        name = Tcl_GetString(objPtr);
        if (name[0] != '\0') {
            if (Rbc_GetPen(graphPtr, name, penType, &newPenPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    mask = ELEM_PEN_OPTION_MASK(option);
    /*
     * Resolve the replacement before releasing an earlier staged
     * candidate.
     */
    if ((transactionPtr->stagedMask & mask) && (*candidatePtrPtr != NULL)) {
        Rbc_FreePen(graphPtr, *candidatePtrPtr);
    }
    *candidatePtrPtr = newPenPtr;
    transactionPtr->stagedMask |= mask;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeElemPenTransaction --
 *
 *      Releases all named-pen references owned by a staged element
 *      pen transaction.
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeElemPenTransaction(Graph *graphPtr, ElemPenTransaction *transactionPtr) {
    if (transactionPtr->activePenPtr != NULL) {
        Rbc_FreePen(graphPtr, transactionPtr->activePenPtr);
    }
    if (transactionPtr->normalPenPtr != NULL) {
        Rbc_FreePen(graphPtr, transactionPtr->normalPenPtr);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PrepareElemPenTransaction --
 *
 *      Resolves all active and normal pen options involved in the
 *      current modern configuration without modifying the live
 *      element.
 *
 *      Explicit occurrences are processed in caller order, so an
 *      invalid earlier repeated value still causes the complete
 *      configuration to fail.
 *
 *----------------------------------------------------------------------
 */
int Rbc_PrepareElemPenTransaction(Graph *graphPtr, Element *elemPtr, Rbc_Uid penType,
                                  ElemPenTransaction *transactionPtr) {
    unsigned int explicitMask;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitMask = 0;
    assert((elemPtr->optionObjc & 1) == 0);
    /*
     * Determine which pen options were supplied explicitly.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemPenOption option;

        option = GetElemPenOption(elemPtr, elemPtr->optionObjv[i]);
        if (option != ELEM_PEN_OPTION_NONE) {
            explicitMask |= ELEM_PEN_OPTION_MASK(option);
        }
    }
    /*
     * On the first modern configuration, process effective option
     * database/default values not explicitly overridden.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & ELEM_PEN_OPTION_MASK(ELEM_PEN_OPTION_ACTIVE)) && (elemPtr->activePenObjPtr != NULL)) {
            if (StageElemPen(graphPtr, elemPtr->activePenObjPtr, penType, transactionPtr, &transactionPtr->activePenPtr,
                             ELEM_PEN_OPTION_ACTIVE) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_PEN_OPTION_MASK(ELEM_PEN_OPTION_NORMAL)) && (elemPtr->normalPenObjPtr != NULL)) {
            if (StageElemPen(graphPtr, elemPtr->normalPenObjPtr, penType, transactionPtr, &transactionPtr->normalPenPtr,
                             ELEM_PEN_OPTION_NORMAL) != TCL_OK) {
                goto error;
            }
        }
    }
    /*
     * Process explicit options in their original order.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemPenOption option;
        Tcl_Obj *valueObjPtr;

        option = GetElemPenOption(elemPtr, elemPtr->optionObjv[i]);
        valueObjPtr = elemPtr->optionObjv[i + 1];
        switch (option) {
        case ELEM_PEN_OPTION_ACTIVE:
            if (StageElemPen(graphPtr, valueObjPtr, penType, transactionPtr, &transactionPtr->activePenPtr, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_PEN_OPTION_NORMAL:
            if (StageElemPen(graphPtr, valueObjPtr, penType, transactionPtr, &transactionPtr->normalPenPtr, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_PEN_OPTION_NONE:
            break;
        }
    }
    return TCL_OK;

error:
    Rbc_FreeElemPenTransaction(graphPtr, transactionPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CommitElemPenTransaction --
 *
 *      Replaces the live element pens with successfully staged
 *      candidates.
 *
 *      An empty normal-pen value selects the element's embedded
 *      built-in pen.
 *
 *----------------------------------------------------------------------
 */
void Rbc_CommitElemPenTransaction(Graph *graphPtr, Element *elemPtr, Pen *builtinPenPtr,
                                  ElemPenTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & ELEM_PEN_OPTION_MASK(ELEM_PEN_OPTION_ACTIVE)) {
        Pen *oldPenPtr;

        oldPenPtr = elemPtr->activePenPtr;
        elemPtr->activePenPtr = transactionPtr->activePenPtr;
        transactionPtr->activePenPtr = NULL;
        if (oldPenPtr != NULL) {
            Rbc_FreePen(graphPtr, oldPenPtr);
        }
    }
    if (transactionPtr->stagedMask & ELEM_PEN_OPTION_MASK(ELEM_PEN_OPTION_NORMAL)) {
        Pen *oldPenPtr;
        Pen *newPenPtr;

        oldPenPtr = elemPtr->normalPenPtr;
        newPenPtr = transactionPtr->normalPenPtr;
        if (newPenPtr == NULL) {
            newPenPtr = builtinPenPtr;
        }
        elemPtr->normalPenPtr = newPenPtr;
        transactionPtr->normalPenPtr = NULL;
        /*
         * The embedded pen is owned by the concrete element and has no
         * named-pen reference to release.
         */
        if ((oldPenPtr != NULL) && (oldPenPtr != builtinPenPtr)) {
            Rbc_FreePen(graphPtr, oldPenPtr);
        }
    }
    transactionPtr->stagedMask = 0;
}

void Rbc_FreeElemDataTransaction(ElemDataTransaction *transactionPtr) {
    Rbc_FreeElemVector(&transactionPtr->x);
    Rbc_FreeElemVector(&transactionPtr->y);
    Rbc_FreeElemVector(&transactionPtr->w);
    Rbc_FreeElemVector(&transactionPtr->xError);
    Rbc_FreeElemVector(&transactionPtr->xHigh);
    Rbc_FreeElemVector(&transactionPtr->xLow);
    Rbc_FreeElemVector(&transactionPtr->yError);
    Rbc_FreeElemVector(&transactionPtr->yHigh);
    Rbc_FreeElemVector(&transactionPtr->yLow);
    transactionPtr->stagedMask = 0;
}

Tcl_Size Rbc_ElemDataTransactionPointCount(Element *elemPtr, const ElemDataTransaction *transactionPtr) {
    const ElemVector *xPtr = &elemPtr->x;
    const ElemVector *yPtr = &elemPtr->y;
    Tcl_Size nX;
    Tcl_Size nY;

    if (transactionPtr != NULL) {
        if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X)) {
            xPtr = &transactionPtr->x;
        }
        if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y)) {
            yPtr = &transactionPtr->y;
        }
    }
    /* Count current sources without refreshing arrays or delivering callbacks. */
    nX = xPtr->clientId != NULL ? Rbc_VectorLength(xPtr->vecPtr) : xPtr->nValues;
    nY = yPtr->clientId != NULL ? Rbc_VectorLength(yPtr->vecPtr) : yPtr->nValues;
    return MIN(nX, nY);
}

/**
 * @ingroup element_transaction_internals
 * @brief Parse a data slot before discarding its previous candidate.
 * @param interp Interpreter used by the data parser.
 * @param elemPtr Live owning element.
 * @param objPtr Borrowed vector-name or expression-list object.
 * @param transactionPtr Initialized common-data transaction.
 * @param candidatePtr Candidate slot corresponding to option.
 * @param option Data identity whose mask bit tracks this slot.
 * @return TCL_OK after transferring the parsed candidate, TCL_ERROR with the
 * previous candidate intact. Frees a failed temporary candidate before returning.
 * Named data owns a vector client ID; literal data owns an array. The parser can
 * evaluate Tcl expressions, so caller-owned element and argument lifetimes must
 * survive callbacks. This staging helper adds no preservation boundary.
 */
static int StageElemDataVector(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr,
                               ElemDataTransaction *transactionPtr, ElemVector *candidatePtr, ElemDataOption option) {
    ElemVector newCandidate;
    unsigned int mask;

    memset(&newCandidate, 0, sizeof(newCandidate));
    if (Rbc_ParseElemVectorObj(interp, elemPtr, objPtr, &newCandidate) != TCL_OK) {
        Rbc_FreeElemVector(&newCandidate);
        return TCL_ERROR;
    }
    mask = ELEM_DATA_OPTION_MASK(option);
    if (transactionPtr->stagedMask & mask) {
        Rbc_FreeElemVector(candidatePtr);
    }
    *candidatePtr = newCandidate;
    transactionPtr->stagedMask |= mask;
    return TCL_OK;
}

/**
 * @ingroup element_transaction_internals
 * @brief Stage a paired data value as an atomic X/Y candidate replacement.
 * @param interp Interpreter for pair parsing and expression errors.
 * @param elemPtr Live owning element.
 * @param objPtr Borrowed alternating coordinate-expression list.
 * @param transactionPtr Initialized common-data transaction.
 * @return TCL_OK after replacing both staged slots; TCL_ERROR preserves both
 * previous candidates. Failure frees both temporary slots. Success frees earlier
 * staged X/Y records only after pair parsing succeeds, moves the new records and
 * sets both mask bits. Live coordinates remain unchanged. Parsing can run Tcl.
 */
static int StageElemDataPairs(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr,
                              ElemDataTransaction *transactionPtr) {
    ElemVector newX;
    ElemVector newY;
    unsigned int xMask;
    unsigned int yMask;

    memset(&newX, 0, sizeof(newX));
    memset(&newY, 0, sizeof(newY));
    if (ParseElemVectorPairsObj(interp, elemPtr, objPtr, &newX, &newY) != TCL_OK) {
        Rbc_FreeElemVector(&newX);
        Rbc_FreeElemVector(&newY);
        return TCL_ERROR;
    }
    xMask = ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X);
    yMask = ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y);
    if (transactionPtr->stagedMask & xMask) {
        Rbc_FreeElemVector(&transactionPtr->x);
    }
    if (transactionPtr->stagedMask & yMask) {
        Rbc_FreeElemVector(&transactionPtr->y);
    }
    transactionPtr->x = newX;
    transactionPtr->y = newY;
    transactionPtr->stagedMask |= xMask | yMask;
    return TCL_OK;
}

int Rbc_PrepareElemDataTransaction(Graph *graphPtr, Element *elemPtr, ElemDataTransaction *transactionPtr) {
    Tcl_Interp *interp;
    unsigned int explicitMask;
    Tcl_Size i;

    interp = graphPtr->interp;
    explicitMask = 0;
    memset(transactionPtr, 0, sizeof(*transactionPtr));
    assert((elemPtr->optionObjc & 1) == 0);
    /*
     * First identify all explicitly supplied data options. This lets
     * the first configuration retain option-database values for options
     * that the caller did not override.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemDataOption option;

        option = GetElemDataOption(elemPtr, elemPtr->optionObjv[i]);
        if (option != ELEM_DATA_OPTION_NONE) {
            explicitMask |= ELEM_DATA_OPTION_MASK(option);
        }
    }
    /*
     * Tk_InitOptions may have installed values from the option database
     * before the first Tk_SetOptions call. Process those effective values
     * only when that option was not explicitly supplied.
     *
     * The order follows the legacy bar-element table: "-data" first,
     * followed by weights and individual vectors.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_PAIRS)) && (elemPtr->dataObjPtr != NULL)) {
            if (StageElemDataPairs(interp, elemPtr, elemPtr->dataObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_WEIGHTS)) && (elemPtr->weightsObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->weightsObjPtr, transactionPtr, &transactionPtr->w,
                                    ELEM_DATA_OPTION_WEIGHTS) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X)) && (elemPtr->xObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->xObjPtr, transactionPtr, &transactionPtr->x,
                                    ELEM_DATA_OPTION_X) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y)) && (elemPtr->yObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->yObjPtr, transactionPtr, &transactionPtr->y,
                                    ELEM_DATA_OPTION_Y) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_ERROR)) && (elemPtr->xErrorObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->xErrorObjPtr, transactionPtr, &transactionPtr->xError,
                                    ELEM_DATA_OPTION_X_ERROR) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_HIGH)) && (elemPtr->xHighObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->xHighObjPtr, transactionPtr, &transactionPtr->xHigh,
                                    ELEM_DATA_OPTION_X_HIGH) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_LOW)) && (elemPtr->xLowObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->xLowObjPtr, transactionPtr, &transactionPtr->xLow,
                                    ELEM_DATA_OPTION_X_LOW) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_ERROR)) && (elemPtr->yErrorObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->yErrorObjPtr, transactionPtr, &transactionPtr->yError,
                                    ELEM_DATA_OPTION_Y_ERROR) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_HIGH)) && (elemPtr->yHighObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->yHighObjPtr, transactionPtr, &transactionPtr->yHigh,
                                    ELEM_DATA_OPTION_Y_HIGH) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_LOW)) && (elemPtr->yLowObjPtr != NULL)) {
            if (StageElemDataVector(interp, elemPtr, elemPtr->yLowObjPtr, transactionPtr, &transactionPtr->yLow,
                                    ELEM_DATA_OPTION_Y_LOW) != TCL_OK) {
                goto error;
            }
        }
    }
    /*
     * Process the original option/value vector rather than the retained
     * object fields. The retained fields contain only each option's final
     * value and would incorrectly hide an invalid earlier occurrence.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemDataOption option;
        Tcl_Obj *valueObjPtr;

        option = GetElemDataOption(elemPtr, elemPtr->optionObjv[i]);
        valueObjPtr = elemPtr->optionObjv[i + 1];
        switch (option) {
        case ELEM_DATA_OPTION_PAIRS:
            if (StageElemDataPairs(interp, elemPtr, valueObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_WEIGHTS:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->w, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_X:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->x, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_Y:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->y, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_X_ERROR:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->xError, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_X_HIGH:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->xHigh, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_X_LOW:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->xLow, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_Y_ERROR:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->yError, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_Y_HIGH:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->yHigh, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_Y_LOW:
            if (StageElemDataVector(interp, elemPtr, valueObjPtr, transactionPtr, &transactionPtr->yLow, option) !=
                TCL_OK) {
                goto error;
            }
            break;
        case ELEM_DATA_OPTION_NONE:
            break;
        }
    }
    return TCL_OK;

error:
    Rbc_FreeElemDataTransaction(transactionPtr);
    return TCL_ERROR;
}

void Rbc_CommitElemDataTransaction(Element *elemPtr, ElemDataTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X)) {
        CommitElemVector(elemPtr, &elemPtr->x, &transactionPtr->x);
    }
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y)) {
        CommitElemVector(elemPtr, &elemPtr->y, &transactionPtr->y);
    }
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_WEIGHTS)) {
        CommitElemVector(elemPtr, &elemPtr->w, &transactionPtr->w);
    }
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_ERROR)) {
        CommitElemVector(elemPtr, &elemPtr->xError, &transactionPtr->xError);
    }
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_HIGH)) {
        CommitElemVector(elemPtr, &elemPtr->xHigh, &transactionPtr->xHigh);
    }
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X_LOW)) {
        CommitElemVector(elemPtr, &elemPtr->xLow, &transactionPtr->xLow);
    }
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_ERROR)) {
        CommitElemVector(elemPtr, &elemPtr->yError, &transactionPtr->yError);
    }
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_HIGH)) {
        CommitElemVector(elemPtr, &elemPtr->yHigh, &transactionPtr->yHigh);
    }
    if (transactionPtr->stagedMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y_LOW)) {
        CommitElemVector(elemPtr, &elemPtr->yLow, &transactionPtr->yLow);
    }
    transactionPtr->stagedMask = 0;
}

/**
 * @ingroup element_transaction_internals
 * @brief Replace one owned Tcl option-object reference.
 * @param objPtrPtr Address of a retained object pointer, possibly NULL.
 * @param newObjPtr Borrowed replacement object, or NULL to clear the field.
 * Identical pointers are a no-op. Otherwise retains the new object before storing
 * it and releasing the old reference. Does not copy contents, evaluate scripts or
 * change internal numeric data.
 */
static void ReplaceElemOptionObject(Tcl_Obj **objPtrPtr, Tcl_Obj *newObjPtr) {
    Tcl_Obj *oldObjPtr;

    oldObjPtr = *objPtrPtr;
    if (oldObjPtr == newObjPtr) {
        return;
    }
    if (newObjPtr != NULL) {
        Tcl_IncrRefCount(newObjPtr);
    }
    *objPtrPtr = newObjPtr;
    if (oldObjPtr != NULL) {
        Tcl_DecrRefCount(oldObjPtr);
    }
}

/**
 * @ingroup element_transaction_internals
 * @brief Retain paired data and derive coherent X/Y option representations.
 * @param elemPtr Element whose dataObjPtr, xObjPtr and yObjPtr are replaced.
 * @param dataObjPtr Borrowed, already validated even-length coordinate list.
 * Builds new X/Y lists using the original element objects, preserving expression
 * spellings instead of reconstructing them from doubles. Retains all three
 * results through ReplaceElemOptionObject. Invalid list structure or failure to
 * assemble output is an internal Tcl_Panic, not a recoverable user error. Must
 * run only after successful data validation; does not evaluate expressions.
 */
static void SetElemDataPairOptionObjects(Element *elemPtr, Tcl_Obj *dataObjPtr) {
    Tcl_Obj **objv;
    Tcl_Obj *xObjPtr;
    Tcl_Obj *yObjPtr;
    Tcl_Size objc;
    Tcl_Size i;

    if (Tcl_ListObjGetElements(elemPtr->graphPtr->interp, dataObjPtr, &objc, &objv) != TCL_OK) {
        Tcl_Panic("validated bar -data value is no longer a Tcl list");
    }
    if ((objc & 1) != 0) {
        Tcl_Panic("validated bar -data value has an odd length");
    }
    xObjPtr = Tcl_NewListObj(0, NULL);
    yObjPtr = Tcl_NewListObj(0, NULL);
    for (i = 0; i < objc; i += 2) {
        if (Tcl_ListObjAppendElement(NULL, xObjPtr, objv[i]) != TCL_OK) {
            Tcl_Panic("can't construct bar -x option value");
        }
        if (Tcl_ListObjAppendElement(NULL, yObjPtr, objv[i + 1]) != TCL_OK) {
            Tcl_Panic("can't construct bar -y option value");
        }
    }
    ReplaceElemOptionObject(&elemPtr->dataObjPtr, dataObjPtr);
    ReplaceElemOptionObject(&elemPtr->xObjPtr, xObjPtr);
    ReplaceElemOptionObject(&elemPtr->yObjPtr, yObjPtr);
}

/**
 * @ingroup element_transaction_internals
 * @brief Synchronize retained coordinate options after successful data commit.
 * @param elemPtr Configured element with even, still-valid option arguments.
 * Processes non-overridden initial defaults in data/X/Y order, then explicit
 * arguments in caller order. A data occurrence sets coherent data/X/Y objects;
 * an independent X or Y clears data and replaces that coordinate representation.
 * Pins initial objects while replacing fields, then releases those temporary
 * references. Does not rebuild numeric arrays or evaluate expressions. Call while
 * optionsConfigured still identifies first configuration when applicable.
 */
void Rbc_SyncElemDataOptionObjects(Element *elemPtr) {
    Tcl_Obj *initialDataObjPtr;
    Tcl_Obj *initialXObjPtr;
    Tcl_Obj *initialYObjPtr;
    unsigned int explicitMask;
    Tcl_Size i;

    assert((elemPtr->optionObjc & 1) == 0);
    explicitMask = 0;
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemDataOption option;

        option = GetElemDataOption(elemPtr, elemPtr->optionObjv[i]);
        if (option != ELEM_DATA_OPTION_NONE) {
            explicitMask |= ELEM_DATA_OPTION_MASK(option);
        }
    }
    initialDataObjPtr = NULL;
    initialXObjPtr = NULL;
    initialYObjPtr = NULL;
    /*
     * Preserve references to initial option-database values while the
     * retained fields are being replaced below.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_PAIRS))) {
            initialDataObjPtr = elemPtr->dataObjPtr;
            if (initialDataObjPtr != NULL) {
                Tcl_IncrRefCount(initialDataObjPtr);
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_X))) {
            initialXObjPtr = elemPtr->xObjPtr;

            if (initialXObjPtr != NULL) {
                Tcl_IncrRefCount(initialXObjPtr);
            }
        }
        if (!(explicitMask & ELEM_DATA_OPTION_MASK(ELEM_DATA_OPTION_Y))) {
            initialYObjPtr = elemPtr->yObjPtr;
            if (initialYObjPtr != NULL) {
                Tcl_IncrRefCount(initialYObjPtr);
            }
        }
        /*
         * Apply initial values in the same order used by the data
         * transaction: -data first, followed by -x and -y.
         */
        if (initialDataObjPtr != NULL) {
            SetElemDataPairOptionObjects(elemPtr, initialDataObjPtr);
        }
        if (initialXObjPtr != NULL) {
            ReplaceElemOptionObject(&elemPtr->dataObjPtr, NULL);
            ReplaceElemOptionObject(&elemPtr->xObjPtr, initialXObjPtr);
        }
        if (initialYObjPtr != NULL) {
            ReplaceElemOptionObject(&elemPtr->dataObjPtr, NULL);
            ReplaceElemOptionObject(&elemPtr->yObjPtr, initialYObjPtr);
        }
        if (initialDataObjPtr != NULL) {
            Tcl_DecrRefCount(initialDataObjPtr);
        }
        if (initialXObjPtr != NULL) {
            Tcl_DecrRefCount(initialXObjPtr);
        }
        if (initialYObjPtr != NULL) {
            Tcl_DecrRefCount(initialYObjPtr);
        }
    }
    /*
     * Apply explicit options in their original order. This preserves
     * the same last-option-wins rule used when staging the vectors.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        ElemDataOption option;
        Tcl_Obj *valueObjPtr;

        option = GetElemDataOption(elemPtr, elemPtr->optionObjv[i]);
        valueObjPtr = elemPtr->optionObjv[i + 1];
        switch (option) {
        case ELEM_DATA_OPTION_PAIRS:
            SetElemDataPairOptionObjects(elemPtr, valueObjPtr);
            break;
        case ELEM_DATA_OPTION_X:
            ReplaceElemOptionObject(&elemPtr->dataObjPtr, NULL);
            ReplaceElemOptionObject(&elemPtr->xObjPtr, valueObjPtr);
            break;
        case ELEM_DATA_OPTION_Y:
            ReplaceElemOptionObject(&elemPtr->dataObjPtr, NULL);
            ReplaceElemOptionObject(&elemPtr->yObjPtr, valueObjPtr);
            break;
        case ELEM_DATA_OPTION_NONE:
        case ELEM_DATA_OPTION_WEIGHTS:
        case ELEM_DATA_OPTION_X_ERROR:
        case ELEM_DATA_OPTION_X_HIGH:
        case ELEM_DATA_OPTION_X_LOW:
        case ELEM_DATA_OPTION_Y_ERROR:
        case ELEM_DATA_OPTION_Y_HIGH:
        case ELEM_DATA_OPTION_Y_LOW:
            break;
        }
    }
}

/**
 * @ingroup palette_internals
 * @brief Recognize the canonical styles option after Tk validation.
 * @param elemPtr Element with concrete optionSpecs.
 * @param objPtr Borrowed original option-name object.
 * @return Nonzero for -styles, zero for unrelated or unresolved names.
 * Does not parse the option value, acquire pens or modify the element.
 */
static int IsElemStylesOption(Element *elemPtr, Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, elemPtr->optionSpecs);
    return ((name != NULL) && (strcmp(name, "-styles") == 0));
}

/**
 * @ingroup palette_internals
 * @brief Replace a staged palette only after parsing its successor.
 * @param graphPtr Live owning graph.
 * @param elemPtr Element supplying class and borrowed normal pen.
 * @param objPtr Borrowed style-list object; NULL means no additional styles.
 * @param penType Retained interface argument; currently unused by this helper.
 * @param styleSize Concrete style-record size, at least sizeof(PenStyle).
 * @param transactionPtr Initialized transaction owning any previous palette.
 * @return TCL_OK on staging; TCL_ERROR retains the previous candidate.
 * ParseStylesObj checks pens against elemPtr's class. Success destroys the prior
 * candidate, stores the new chain and marks it staged, without replacing the
 * live element palette.
 */
static int StageElemStyles(Graph *graphPtr, Element *elemPtr, Tcl_Obj *objPtr, Rbc_Uid penType, size_t styleSize,
                           ElemStylesTransaction *transactionPtr) {
    Rbc_Chain *newPalette;

    newPalette = NULL;
    if (ParseStylesObj(graphPtr, elemPtr, objPtr, styleSize, &newPalette) != TCL_OK) {
        return TCL_ERROR;
    }
    if (transactionPtr->palette != NULL) {
        DestroyPalette(graphPtr, transactionPtr->palette);
    }
    transactionPtr->palette = newPalette;
    transactionPtr->staged = TRUE;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeElemStylesTransaction --
 *
 *      Releases a staged candidate palette.
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeElemStylesTransaction(Graph *graphPtr, ElemStylesTransaction *transactionPtr) {
    DestroyPalette(graphPtr, transactionPtr->palette);
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PrepareElemStylesTransaction --
 *
 *      Parses all -styles values involved in the current modern
 *      configuration without modifying the live palette.
 *
 *      Explicit occurrences are processed in caller order. Therefore,
 *      an invalid earlier repeated value still causes the complete
 *      configuration to fail.
 *
 *----------------------------------------------------------------------
 */
int Rbc_PrepareElemStylesTransaction(Graph *graphPtr, Element *elemPtr, Rbc_Uid penType, size_t styleSize,
                                     ElemStylesTransaction *transactionPtr) {
    int explicitlySpecified;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitlySpecified = FALSE;
    assert((elemPtr->optionObjc & 1) == 0);
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemStylesOption(elemPtr, elemPtr->optionObjv[i])) {
            explicitlySpecified = TRUE;
        }
    }
    /*
     * Always construct the initial palette, even when stylesObjPtr is
     * NULL. NULL represents an empty style list, but the palette still
     * requires its reserved normal-pen entry.
     */
    if (!elemPtr->optionsConfigured && !explicitlySpecified) {
        if (StageElemStyles(graphPtr, elemPtr, elemPtr->stylesObjPtr, penType, styleSize, transactionPtr) != TCL_OK) {
            goto error;
        }
    }
    /*
     * Process every explicit occurrence in original caller order.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        if (IsElemStylesOption(elemPtr, elemPtr->optionObjv[i])) {
            if (StageElemStyles(graphPtr, elemPtr, elemPtr->optionObjv[i + 1], penType, styleSize, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
    }
    return TCL_OK;

error:
    Rbc_FreeElemStylesTransaction(graphPtr, transactionPtr);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CommitElemStylesTransaction --
 *
 *      Replaces the live palette with a successfully staged candidate.
 *
 *----------------------------------------------------------------------
 */
void Rbc_CommitElemStylesTransaction(Graph *graphPtr, Element *elemPtr, ElemStylesTransaction *transactionPtr) {
    Rbc_Chain *oldPalette;

    if (!transactionPtr->staged) {
        return;
    }
    oldPalette = elemPtr->palette;
    elemPtr->palette = transactionPtr->palette;
    transactionPtr->palette = NULL;
    transactionPtr->staged = FALSE;
    DestroyPalette(graphPtr, oldPalette);
}

/**
 * @ingroup element_command_internals
 * @brief Parse the exact closest-search direction x, y or both.
 *
 * Writes alongPtr only on success. Returns TCL_ERROR with a diagnostic for
 * other spellings, including abbreviations; does not evaluate Tcl expressions.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed Tcl value to parse without retaining a reference.
 * @param[out] alongPtr Required output receiving the direction selector only on success.
 */
static int GetAlongFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *alongPtr) {
    const char *string;
    int along;

    string = Tcl_GetString(objPtr);
    if ((string[0] == 'x') && (string[1] == '\0')) {
        along = SEARCH_X;
    } else if ((string[0] == 'y') && (string[1] == '\0')) {
        along = SEARCH_Y;
    } else if (strcmp(string, "both") == 0) {
        along = SEARCH_BOTH;
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad along value \"%s\": "
                                               "must be x, y, or both",
                                               string));
        return TCL_ERROR;
    }
    *alongPtr = along;
    return TCL_OK;
}

typedef enum { CLOSEST_OPTION_ALONG, CLOSEST_OPTION_HALO, CLOSEST_OPTION_INTERPOLATE } ClosestOption;

static const char *const closestOptionNames[] = {"-along", "-halo", "-interpolate", NULL};

/**
 * @ingroup element_command_internals
 * @brief Parse closest-search switches beginning at argument six.
 *
 * Mutates the caller-initialized search options as each pair succeeds; failure
 * does not roll back earlier pairs. Accepts unambiguous switch abbreviations,
 * nonnegative pixel halos and Tcl booleans for interpolation. Stops at the first
 * non-option or after --, writing nextPtr only on success. Does not resolve
 * trailing element names or execute a search.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 * @param[in,out] searchPtr Caller-initialized closest-search record; successful pairs update it without
 *   rollback.
 * @param[out] nextPtr Required output receiving the first trailing argument index only on success.
 */
static int ParseClosestOptions(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[],
                               ClosestSearch *searchPtr, Tcl_Size *nextPtr) {
    Tcl_Size i;

    for (i = 6; i < objc;) {
        const char *string;
        int optionIndex;

        string = Tcl_GetString(objv[i]);
        /*
         * The first non-option starts the optional element-name list.
         */
        if (string[0] != '-') {
            break;
        }
        /*
         * "--" explicitly terminates option parsing.  This allows an
         * element whose name begins with '-' to be specified.
         */
        if ((string[1] == '-') && (string[2] == '\0')) {
            i++;
            break;
        }
        if (Tcl_GetIndexFromObj(interp, objv[i], closestOptionNames, "option", 0, &optionIndex) != TCL_OK) {
            return TCL_ERROR;
        }
        if ((i + 1) >= objc) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("value for \"%s\" missing", string));
            return TCL_ERROR;
        }
        switch ((ClosestOption)optionIndex) {
        case CLOSEST_OPTION_ALONG:
            if (GetAlongFromObj(interp, objv[i + 1], &searchPtr->along) != TCL_OK) {
                return TCL_ERROR;
            }
            break;
        case CLOSEST_OPTION_HALO:
            if (Rbc_GetPixelsFromObj(interp, graphPtr->tkwin, objv[i + 1], PIXELS_NONNEGATIVE, &searchPtr->halo) !=
                TCL_OK) {
                return TCL_ERROR;
            }
            break;
        case CLOSEST_OPTION_INTERPOLATE: {
            int interpolate;

            if (Tcl_GetBooleanFromObj(interp, objv[i + 1], &interpolate) != TCL_OK) {
                return TCL_ERROR;
            }
            searchPtr->mode = interpolate ? SEARCH_TRACES : SEARCH_POINTS;
            break;
        }
        }
        i += 2;
    }
    *nextPtr = i;
    return TCL_OK;
}

void Rbc_FreePalette(Graph *graphPtr, Rbc_Chain *palette) {
    Rbc_ChainLink *linkPtr;

    /* Skip the first slot. It contains the built-in "normal" pen of
     * the element.  */
    linkPtr = Rbc_ChainFirstLink(palette);
    if (linkPtr != NULL) {
        register PenStyle *stylePtr;
        Rbc_ChainLink *nextPtr;

        for (linkPtr = Rbc_ChainNextLink(linkPtr); linkPtr != NULL; linkPtr = nextPtr) {
            nextPtr = Rbc_ChainNextLink(linkPtr);
            stylePtr = Rbc_ChainGetValue(linkPtr);
            Rbc_FreePen(graphPtr, stylePtr->penPtr);
            Rbc_ChainDeleteLink(palette, linkPtr);
        }
    }
}

/**
 * @ingroup palette_internals
 * @brief Destroy an entire candidate or retired palette chain.
 * @param graphPtr Owning graph used to release counted named-pen references.
 * @param palette Owned chain or NULL.
 * Releases entries after the reserved first entry through Rbc_FreePalette, then
 * destroys the chain including that first entry. Its normal pen is borrowed and
 * is not released. NULL is accepted; no live element pointer is changed.
 */
static void DestroyPalette(Graph *graphPtr, Rbc_Chain *palette) {
    if (palette == NULL) {
        return;
    }
    Rbc_FreePalette(graphPtr, palette);
    Rbc_ChainDestroy(palette);
}

/**
 * @ingroup palette_internals
 * @brief Build a candidate palette with a reserved normal-style entry.
 * @param graphPtr Live graph and interpreter.
 * @param elemPtr Element supplying class and normal-pen pointer.
 * @param objPtr Borrowed list of styles; NULL represents an empty list.
 * @param styleSize Concrete record size, at least sizeof(PenStyle).
 * @param[out] palettePtrPtr Cleared first; receives an owned chain on success.
 * @return TCL_OK on success, TCL_ERROR after releasing partial storage and pen
 * references. Does not dispose of a prior output pointer or change the element.
 * The first entry always borrows the normal pen. Additional entries acquire pens
 * and default to weight intervals i through i+1 (zero-based). Explicit endpoints
 * replace those defaults. An invalid unattached entry is freed separately before
 * destroying the partial chain. Caller must eventually commit or destroy output.
 */
static int ParseStylesObj(Graph *graphPtr, Element *elemPtr, Tcl_Obj *objPtr, size_t styleSize,
                       Rbc_Chain **palettePtrPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_Size i;
    Rbc_Chain *palette;
    Rbc_ChainLink *linkPtr;
    PenStyle *stylePtr;

    *palettePtrPtr = NULL;
    objv = NULL;
    objc = 0;
    if (objPtr != NULL) {
        if (Tcl_ListObjGetElements(graphPtr->interp, objPtr, &objc, &objv) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    palette = Rbc_ChainCreate();
    if (palette == NULL) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("can't allocate element palette", -1));
        return TCL_ERROR;
    }
    /*
     * The first entry is always reserved for the element's normal pen.
     * This entry does not own a pen reference.
     */
    linkPtr = Rbc_ChainAllocLink(styleSize);
    Rbc_ChainLinkBefore(palette, linkPtr, NULL);
    stylePtr = Rbc_ChainGetValue(linkPtr);
    stylePtr->penPtr = elemPtr->normalPenPtr;
    for (i = 0; i < objc; i++) {
        linkPtr = Rbc_ChainAllocLink(styleSize);
        stylePtr = Rbc_ChainGetValue(linkPtr);
        /*
         * Preserve the legacy default ranges. A three-item style
         * specification may replace these values.
         */
        stylePtr->weight.min = (double)i;
        stylePtr->weight.max = (double)i + 1.0;
        stylePtr->weight.range = 1.0;
        if (GetPenStyleFromObj(graphPtr, objv[i], elemPtr->classUid, stylePtr) != TCL_OK) {
            /*
             * This link has not yet been attached to the chain.
             */
            ckfree((char *)linkPtr);
            DestroyPalette(graphPtr, palette);
            return TCL_ERROR;
        }
        Rbc_ChainLinkBefore(palette, linkPtr, NULL);
    }
    *palettePtrPtr = palette;
    return TCL_OK;
}

PenStyle **Rbc_StyleMap(Element *elemPtr) {
    Tcl_Size i;
    Tcl_Size nWeights;
    PenStyle **dataToStyle;
    Rbc_ChainLink *linkPtr;
    PenStyle *stylePtr;
    const double *w;
    Tcl_Size nPoints;
    size_t byteCount;

    nPoints = NumberOfPoints(elemPtr);
    nWeights = MIN(elemPtr->w.nValues, nPoints);
    w = elemPtr->w.valueArr;
    linkPtr = Rbc_ChainFirstLink(elemPtr->palette);
    stylePtr = Rbc_ChainGetValue(linkPtr);
    /*
     * A style-map entry is required for every data point.  Preserve
     * the allocator's fatal-overflow semantics, but validate the
     * Tcl_Size value before converting it to size_t.
     */
    if ((Tcl_WideUInt)nPoints > (Tcl_WideUInt)(SIZE_MAX / sizeof(*dataToStyle))) {
        Tcl_Panic("Rbc_StyleMap: allocation size overflow");
    }
    byteCount = (size_t)nPoints * sizeof(*dataToStyle);
    /*
     * Keep an allocation for the empty case because callers
     * unconditionally release the returned pointer.
     */
    if (byteCount == 0) {
        byteCount = 1;
    }
    dataToStyle = ckalloc(byteCount);
    /*
     * Initialize every data point to the normal style.
     */
    for (i = 0; i < nPoints; i++) {
        dataToStyle[i] = stylePtr;
    }
    /*
     * Replace the normal style for points whose weights fall inside
     * one of the configured style ranges.
     */
    for (i = 0; i < nWeights; i++) {
        for (linkPtr = Rbc_ChainLastLink(elemPtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainPrevLink(linkPtr)) {
            double norm;

            stylePtr = Rbc_ChainGetValue(linkPtr);
            if (stylePtr->weight.range <= 0.0) {
                continue;
            }
            norm = (w[i] - stylePtr->weight.min) / stylePtr->weight.range;
            if (((norm - 1.0) <= DBL_EPSILON) && (((1.0 - norm) - 1.0) <= DBL_EPSILON)) {
                dataToStyle[i] = stylePtr;
                break;
            }
        }
    }
    return dataToStyle;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ExpandErrorBarExtents --
 *
 *      Expands element extents to include error-bar coordinates.
 *      Only error values that correspond to actual element data points
 *      and can therefore be mapped by Rbc_MapErrorBars are considered.
 *
 * Parameters:
 *      Element *elemPtr    - Element containing data and error vectors.
 *      Extents2D *extsPtr  - Extents to expand.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      May expand the supplied extents.
 *
 *----------------------------------------------------------------------
 */
void Rbc_ExpandErrorBarExtents(Element *elemPtr, Extents2D *extsPtr) {
    Rbc_ExpandErrorBarExtentsOffset(elemPtr, extsPtr, 0.0);
}

/* Apply a data-coordinate X translation without modifying source vectors. */
void Rbc_ExpandErrorBarExtentsOffset(Element *elemPtr, Extents2D *extsPtr, double xOffset) {
    Rbc_ExpandErrorBarExtentsTranslated(elemPtr, extsPtr, xOffset, NULL);
}

/*
 * Use per-point data-space translations when supplied. Non-finite translations
 * suppress errors for omitted bars. Explicit endpoints and symmetric errors
 * receive the same translation, without modifying any source vectors.
 */
void Rbc_ExpandErrorBarExtentsTranslated(Element *elemPtr, Extents2D *extsPtr, double xOffset,
                                       const Point2D *offsets) {
    Tcl_Size nPoints;
    Tcl_Size n;
    Tcl_Size i;

    if ((elemPtr == NULL) || (extsPtr == NULL)) {
        return;
    }
    nPoints = NumberOfPoints(elemPtr);
    if (nPoints <= 0) {
        return;
    }
    /*
     * X error bars.
     */
    if (elemPtr->xError.nValues > 0) {
        n = MIN(elemPtr->xError.nValues, nPoints);
        for (i = 0; i < n; i++) {
            double dx = (offsets != NULL) ? offsets[i].x : xOffset;
            double dy = (offsets != NULL) ? offsets[i].y : 0.0;
            double error;
            double high;
            double low;
            double x;

            if (!FINITE(dx) || !FINITE(dy)) {
                continue;
            }

            x = elemPtr->x.valueArr[i] + dx;
            error = elemPtr->xError.valueArr[i];
            if ((!FINITE(x)) || (!FINITE(error))) {
                continue;
            }
            high = x + error;
            low = x - error;
            if ((!FINITE(high)) || (!FINITE(low))) {
                continue;
            }
            if (high > extsPtr->right) {
                extsPtr->right = high;
            }
            if (elemPtr->axes.x->logScale) {
                if ((high >= DBL_MIN) && (high > extsPtr->right)) {
                    extsPtr->right = high;
                }
                if ((low >= DBL_MIN) && (low < extsPtr->left)) {
                    extsPtr->left = low;
                }
            } else if (low < extsPtr->left) {
                extsPtr->left = low;
            }
        }
    } else {
        n = MIN3(elemPtr->xHigh.nValues, elemPtr->xLow.nValues, nPoints);
        for (i = 0; i < n; i++) {
            double dx = (offsets != NULL) ? offsets[i].x : xOffset;
            double dy = (offsets != NULL) ? offsets[i].y : 0.0;
            double high;
            double low;

            if (!FINITE(dx) || !FINITE(dy)) {
                continue;
            }

            high = elemPtr->xHigh.valueArr[i] + dx;
            low = elemPtr->xLow.valueArr[i] + dx;
            if ((!FINITE(high)) || (!FINITE(low))) {
                continue;
            }
            if (high > extsPtr->right) {
                extsPtr->right = high;
            }
            if (elemPtr->axes.x->logScale) {
                if ((high >= DBL_MIN) && (high > extsPtr->right)) {
                    extsPtr->right = high;
                }
                if ((low >= DBL_MIN) && (low < extsPtr->left)) {
                    extsPtr->left = low;
                }
            } else if (low < extsPtr->left) {
                extsPtr->left = low;
            }
        }
    }
    /*
     * Y error bars.
     *
     * Start again from the original number of data points.  X and Y
     * error-vector lengths are independent.
     */
    if (elemPtr->yError.nValues > 0) {
        n = MIN(elemPtr->yError.nValues, nPoints);
        for (i = 0; i < n; i++) {
            double dx = (offsets != NULL) ? offsets[i].x : xOffset;
            double dy = (offsets != NULL) ? offsets[i].y : 0.0;
            double error;
            double high;
            double low;
            double y;

            if (!FINITE(dx) || !FINITE(dy)) {
                continue;
            }

            y = elemPtr->y.valueArr[i] + dy;
            error = elemPtr->yError.valueArr[i];
            if ((!FINITE(y)) || (!FINITE(error))) {
                continue;
            }
            high = y + error;
            low = y - error;
            if ((!FINITE(high)) || (!FINITE(low))) {
                continue;
            }
            if (high > extsPtr->bottom) {
                extsPtr->bottom = high;
            }
            if (elemPtr->axes.y->logScale) {
                if ((high >= DBL_MIN) && (high > extsPtr->bottom)) {
                    extsPtr->bottom = high;
                }
                if ((low >= DBL_MIN) && (low < extsPtr->top)) {
                    extsPtr->top = low;
                }
            } else if (low < extsPtr->top) {
                extsPtr->top = low;
            }
        }
    } else {
        n = MIN3(elemPtr->yHigh.nValues, elemPtr->yLow.nValues, nPoints);
        for (i = 0; i < n; i++) {
            double dx = (offsets != NULL) ? offsets[i].x : xOffset;
            double dy = (offsets != NULL) ? offsets[i].y : 0.0;
            double high;
            double low;

            if (!FINITE(dx) || !FINITE(dy)) {
                continue;
            }

            high = elemPtr->yHigh.valueArr[i] + dy;
            low = elemPtr->yLow.valueArr[i] + dy;
            if ((!FINITE(high)) || (!FINITE(low))) {
                continue;
            }
            if (high > extsPtr->bottom) {
                extsPtr->bottom = high;
            }
            if (elemPtr->axes.y->logScale) {
                if ((high >= DBL_MIN) && (high > extsPtr->bottom)) {
                    extsPtr->bottom = high;
                }
                if ((low >= DBL_MIN) && (low < extsPtr->top)) {
                    extsPtr->top = low;
                }
            } else if (low < extsPtr->top) {
                extsPtr->top = low;
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * AllocateErrorBarArrays --
 *
 *      Allocates storage for mapped error-bar segments and their
 *      data-index map.  Each source point may generate a main
 *      segment and two caps.
 *
 * Parameters:
 *      Tcl_Size nPoints       - Number of source error-bar points.
 *      Segment2D **segmentsPtr
 *                             - Receives the segment array.
 *      Tcl_Size **mapPtr      - Receives the data-index array.
 *
 * Results:
 *      TCL_OK if the requested allocation sizes are representable.
 *      TCL_ERROR if the required count or byte size would overflow.
 *
 * Side Effects:
 *      Allocates both result arrays with ckalloc() on success.
 *
 *----------------------------------------------------------------------
 */
static int AllocateErrorBarArrays(Tcl_Size nPoints, Segment2D **segmentsPtr, Tcl_Size **mapPtr) {
    Tcl_Size capacity;
    size_t segmentBytes;
    size_t mapBytes;

    *segmentsPtr = NULL;
    *mapPtr = NULL;
    /*
     * Each data point can generate one main error-bar segment
     * and two caps.
     */
    if ((nPoints < 0) || (nPoints > (TCL_SIZE_MAX / 3))) {
        return TCL_ERROR;
    }
    capacity = nPoints * 3;
    if (((Tcl_WideUInt)capacity > (Tcl_WideUInt)(SIZE_MAX / sizeof(**segmentsPtr))) ||
        ((Tcl_WideUInt)capacity > (Tcl_WideUInt)(SIZE_MAX / sizeof(**mapPtr)))) {
        return TCL_ERROR;
    }
    segmentBytes = (size_t)capacity * sizeof(**segmentsPtr);
    mapBytes = (size_t)capacity * sizeof(**mapPtr);
    *segmentsPtr = ckalloc(segmentBytes);
    *mapPtr = ckalloc(mapBytes);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MapErrorBars --
 *
 *      Creates two arrays of points and pen indices, filled with
 *      the screen coordinates of the visible
 *
 * Parameters:
 *      Graph *graphPtr
 *      Element *elemPtr
 *      PenStyle **dataToStyle
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed and allocated for the index array.
 *
 *----------------------------------------------------------------------
 */
void Rbc_MapErrorBars(Graph *graphPtr, Element *elemPtr, PenStyle **dataToStyle) {
    Rbc_MapErrorBarsOffset(graphPtr, elemPtr, dataToStyle, 0.0);
}

/* Apply a data-coordinate X translation without modifying source vectors. */
void Rbc_MapErrorBarsOffset(Graph *graphPtr, Element *elemPtr, PenStyle **dataToStyle, double xOffset) {
    Rbc_MapErrorBarsTranslated(graphPtr, elemPtr, dataToStyle, xOffset, NULL);
}

/*
 * Use per-point data-space translations when supplied. Non-finite translations
 * suppress errors for omitted bars. Explicit endpoints and symmetric errors
 * receive the same translation, without modifying any source vectors.
 */
void Rbc_MapErrorBarsTranslated(Graph *graphPtr, Element *elemPtr, PenStyle **dataToStyle, double xOffset,
                                const Point2D *offsets) {
    Extents2D exts;
    Tcl_Size n;
    Tcl_Size nPoints;

    Rbc_GraphExtents(graphPtr, &exts);
    nPoints = NumberOfPoints(elemPtr);
    /*
     * Map X error bars.
     */
    if (elemPtr->xError.nValues > 0) {
        n = MIN(elemPtr->xError.nValues, nPoints);
    } else {
        n = MIN3(elemPtr->xHigh.nValues, elemPtr->xLow.nValues, nPoints);
    }
    if (n > 0) {
        Segment2D *errorBars;
        Segment2D *segPtr;
        Tcl_Size *errorToData;
        Tcl_Size *indexPtr;
        Tcl_Size i;

        if (AllocateErrorBarArrays(n, &errorBars, &errorToData) != TCL_OK) {
            return;
        }
        segPtr = errorBars;
        indexPtr = errorToData;
        for (i = 0; i < n; i++) {
            double dx = (offsets != NULL) ? offsets[i].x : xOffset;
            double dy = (offsets != NULL) ? offsets[i].y : 0.0;
            PenStyle *stylePtr;
            double high;
            double low;
            double x;
            double y;

            if (!FINITE(dx) || !FINITE(dy)) {
                continue;
            }

            x = elemPtr->x.valueArr[i] + dx;
            y = elemPtr->y.valueArr[i] + dy;
            stylePtr = dataToStyle[i];
            if (!FINITE(x) || !FINITE(y)) {
                continue;
            }
            if (elemPtr->xError.nValues > 0) {
                high = x + elemPtr->xError.valueArr[i];
                low = x - elemPtr->xError.valueArr[i];
            } else {
                high = elemPtr->xHigh.valueArr[i] + dx;
                low = elemPtr->xLow.valueArr[i] + dx;
            }
            if (!FINITE(high) || !FINITE(low)) {
                continue;
            }
            {
                Point2D p;
                Point2D q;

                p = Rbc_Map2D(graphPtr, high, y, &elemPtr->axes);
                q = Rbc_Map2D(graphPtr, low, y, &elemPtr->axes);
                if ((!FINITE(p.x)) || (!FINITE(p.y)) || (!FINITE(q.x)) || (!FINITE(q.y))) {
                    continue;
                }
                /*
                 * Main X-error segment.
                 */
                segPtr->p = p;
                segPtr->q = q;
                if (Rbc_LineRectClip(&exts, &p, &q, segPtr)) {
                    segPtr++;
                    *indexPtr++ = i;
                }
                /*
                 * First cap.
                 */
                Point2D capP;
                Point2D capQ;

                /* Caps remain perpendicular when the data axes swap screen directions. */
                double capDx = (graphPtr->inverted) ? stylePtr->errorBarCapWidth : 0.0;
                double capDy = (graphPtr->inverted) ? 0.0 : stylePtr->errorBarCapWidth;

                capP.x = p.x - capDx;
                capP.y = p.y - capDy;
                capQ.x = p.x + capDx;
                capQ.y = p.y + capDy;
                if (Rbc_LineRectClip(&exts, &capP, &capQ, segPtr)) {
                    segPtr++;
                    *indexPtr++ = i;
                }
                /*
                 * Second cap.
                 */
                capP.x = q.x - capDx;
                capP.y = q.y - capDy;
                capQ.x = q.x + capDx;
                capQ.y = q.y + capDy;
                if (Rbc_LineRectClip(&exts, &capP, &capQ, segPtr)) {
                    segPtr++;
                    *indexPtr++ = i;
                }
            }
        }
        elemPtr->xErrorBars = errorBars;
        elemPtr->xErrorBarCnt = (Tcl_Size)(segPtr - errorBars);
        elemPtr->xErrorToData = errorToData;
    }
    /*
     * Map Y error bars.
     */
    if (elemPtr->yError.nValues > 0) {
        n = MIN(elemPtr->yError.nValues, nPoints);
    } else {
        n = MIN3(elemPtr->yHigh.nValues, elemPtr->yLow.nValues, nPoints);
    }
    if (n > 0) {
        Segment2D *errorBars;
        Segment2D *segPtr;
        Tcl_Size *errorToData;
        Tcl_Size *indexPtr;
        Tcl_Size i;

        if (AllocateErrorBarArrays(n, &errorBars, &errorToData) != TCL_OK) {
            return;
        }
        segPtr = errorBars;
        indexPtr = errorToData;
        for (i = 0; i < n; i++) {
            double dx = (offsets != NULL) ? offsets[i].x : xOffset;
            double dy = (offsets != NULL) ? offsets[i].y : 0.0;
            PenStyle *stylePtr;
            double high;
            double low;
            double x;
            double y;

            if (!FINITE(dx) || !FINITE(dy)) {
                continue;
            }

            x = elemPtr->x.valueArr[i] + dx;
            y = elemPtr->y.valueArr[i] + dy;
            stylePtr = dataToStyle[i];
            if (!FINITE(x) || !FINITE(y)) {
                continue;
            }
            if (elemPtr->yError.nValues > 0) {
                high = y + elemPtr->yError.valueArr[i];
                low = y - elemPtr->yError.valueArr[i];
            } else {
                high = elemPtr->yHigh.valueArr[i] + dy;
                low = elemPtr->yLow.valueArr[i] + dy;
            }
            if (!FINITE(high) || !FINITE(low)) {
                continue;
            }
            {
                Point2D p;
                Point2D q;
                Point2D capP;
                Point2D capQ;

                p = Rbc_Map2D(graphPtr, x, high, &elemPtr->axes);
                q = Rbc_Map2D(graphPtr, x, low, &elemPtr->axes);
                if ((!FINITE(p.x)) || (!FINITE(p.y)) || (!FINITE(q.x)) || (!FINITE(q.y))) {
                    continue;
                }
                /*
                 * Main Y-error segment.
                 */
                segPtr->p = p;
                segPtr->q = q;
                if (Rbc_LineRectClip(&exts, &p, &q, segPtr)) {
                    segPtr++;
                    *indexPtr++ = i;
                }
                /*
                 * First cap.
                 */
                /* Caps remain perpendicular when the data axes swap screen directions. */
                double capDx = (!graphPtr->inverted) ? stylePtr->errorBarCapWidth : 0.0;
                double capDy = (!graphPtr->inverted) ? 0.0 : stylePtr->errorBarCapWidth;

                capP.x = p.x - capDx;
                capP.y = p.y - capDy;
                capQ.x = p.x + capDx;
                capQ.y = p.y + capDy;
                if (Rbc_LineRectClip(&exts, &capP, &capQ, segPtr)) {
                    segPtr++;
                    *indexPtr++ = i;
                }
                /*
                 * Second cap.
                 */
                capP.x = q.x - capDx;
                capP.y = q.y - capDy;
                capQ.x = q.x + capDx;
                capQ.y = q.y + capDy;
                if (Rbc_LineRectClip(&exts, &capP, &capQ, segPtr)) {
                    segPtr++;
                    *indexPtr++ = i;
                }
            }
        }
        elemPtr->yErrorBars = errorBars;
        elemPtr->yErrorBarCnt = (Tcl_Size)(segPtr - errorBars);
        elemPtr->yErrorToData = errorToData;
    }
}

/**
 * @ingroup element_data_internals
 * @brief Resolve end or an integer expression against the element point count.
 * @param interp Interpreter for expression and range diagnostics.
 * @param nPoints Point count captured by the caller before expression evaluation.
 * @param string Borrowed NUL-terminated index text.
 * @param[out] indexPtr Receives the validated Tcl_Size index only on success.
 * @return TCL_OK on success, TCL_ERROR for empty data, evaluation/conversion
 * failure or an out-of-range index. Only exact end is special. Other input uses
 * Tcl_ExprObj followed by Tcl_GetSizeIntFromObj and can execute commands/traces.
 * Bounds are captured before evaluation; caller owns callback lifetime and any
 * needed revalidation if data changes during evaluation.
 */
static int GetIndex(Tcl_Interp *interp, Tcl_Size nPoints, const char *string, Tcl_Size *indexPtr) {
    Tcl_Size last;
    Tcl_Size ielem;

    if (nPoints == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't select an index from an empty element", -1));
        return TCL_ERROR;
    }
    last = nPoints - 1;
    if ((string[0] == 'e') && (strcmp(string, "end") == 0)) {
        ielem = last;
    } else {
        Tcl_Obj *exprObjPtr;
        Tcl_Obj *valueObjPtr;
        int result;

        exprObjPtr = Tcl_NewStringObj(string, -1);
        Tcl_IncrRefCount(exprObjPtr);
        valueObjPtr = NULL;
        result = Tcl_ExprObj(interp, exprObjPtr, &valueObjPtr);
        Tcl_DecrRefCount(exprObjPtr);
        if (result != TCL_OK) {
            return TCL_ERROR;
        }
        result = Tcl_GetSizeIntFromObj(interp, valueObjPtr, &ielem);
        Tcl_DecrRefCount(valueObjPtr);
        if (result != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if ((ielem < 0) || (ielem > last)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("element index \"%s\" is out of range", string));
        return TCL_ERROR;
    }
    *indexPtr = ielem;
    return TCL_OK;
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Look up an element by its exact registry name.
 * Returns TCL_OK and a borrowed record in elemPtrPtr, or TCL_ERROR with a
 * graph-interpreter diagnostic. Failure leaves the output pointer unchanged.
 * Hidden and non-displayed elements remain eligible; no reference is retained.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in] nameObj Borrowed exact element-name object.
 * @param[out] elemPtrPtr Required output receiving a borrowed registry element only on success.
 */
static int NameToElement(Graph *graphPtr, Tcl_Obj *nameObj, Element **elemPtrPtr) {
    Tcl_HashEntry *hPtr;

    const char *name = Tcl_GetString(nameObj);
    hPtr = Tcl_FindHashEntry(&graphPtr->elements.table, name);
    if (hPtr == NULL) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_ObjPrintf("can't find element \"%s\" in \"%s\"", name, Tk_PathName(graphPtr->tkwin)));
        return TCL_ERROR;
    }
    *elemPtrPtr = (Element *)Tcl_GetHashValue(hPtr);
    return TCL_OK;
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Initialize Tk-managed element options from component resources.
 * Repeated calls after success are no-ops. Uses the normalized resource name
 * and class Element. On failure frees partially initialized options and the
 * option table; constructor-owned resources remain for the concrete destructor.
 * Returns TCL_OK or TCL_ERROR. Does not construct derived drawing resources.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] elemPtr Borrowed live element belonging to graphPtr; resource requirements are described
 *   above.
 */
static int InitElementOptions(Graph *graphPtr, Element *elemPtr) {
    char *componentName;
    int result;

    if (elemPtr->optionsInitialized) {
        return TCL_OK;
    }
    assert(elemPtr->optionSpecs != NULL);
    elemPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, elemPtr->optionSpecs);
    componentName = Rbc_GetComponentResourceName(elemPtr->name);
    result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, componentName,
                                      "Element", (char *)elemPtr, elemPtr->optionTable);
    ckfree(componentName);
    if (result != TCL_OK) {
        /* Tk_InitOptions leaves earlier options initialized on failure. */
        Tk_FreeConfigOptions((char *)elemPtr, elemPtr->optionTable, graphPtr->tkwin);
        Tk_DeleteOptionTable(elemPtr->optionTable);
        elemPtr->optionTable = NULL;
        return TCL_ERROR;
    }
    elemPtr->optionsInitialized = TRUE;
    return TCL_OK;
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Apply Tk options and validate concrete derived state transactionally.
 * Requires initialized options. Exposes the option mask and borrowed original
 * arguments only during concrete configuration. Trackgraphs require a valid
 * track and a Y axis owned by it; ordinary graphs reject nonempty track names.
 * Concrete configuration must stage all fallible work before live-state commit.
 * Failure restores Tk options while preserving the error diagnostic. Success
 * commits the track, sets optionsConfigured and optionally writes maskPtr.
 * Clears transient argument pointers on both post-conversion exit paths.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] elemPtr Borrowed live element belonging to graphPtr; resource requirements are described
 *   above.
 * @param[in] objc Even number of option/value entries in objv.
 * @param[in] objv Borrowed even-length option/value vector; exposed only during concrete configuration.
 * @param[out] maskPtr Optional output receiving the Tk change mask on success.
 */
static int ConfigureElementOptions(Graph *graphPtr, Element *elemPtr, Tcl_Size objc, Tcl_Obj *const objv[],
                                   int *maskPtr) {
    Tk_SavedOptions savedOptions;
    int mask;
    RbcTrack *newTrack = NULL;
    int trackValid = TRUE;

    assert(elemPtr->optionsInitialized);
    assert(elemPtr->optionTable != NULL);
    /*
     * Clear any stale transaction context before invoking Tk.
     */
    elemPtr->optionMask = 0;
    elemPtr->optionObjc = 0;
    elemPtr->optionObjv = NULL;
    if (Tk_SetOptions(graphPtr->interp, (char *)elemPtr, elemPtr->optionTable, objc, objv, graphPtr->tkwin,
                      &savedOptions, &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Make the changed-option mask and the original argument order
     * available to the concrete configuration procedure.
     */
    elemPtr->optionMask = mask;
    elemPtr->optionObjc = objc;
    elemPtr->optionObjv = objv;
    /*
     * Concrete configuration procedures must be transactional:
     * they must not replace active derived resources until every
     * fallible conversion has succeeded.
     */
    if (graphPtr->trackGraph) {
        const char *name = (elemPtr->trackObjPtr != NULL) ? Tcl_GetString(elemPtr->trackObjPtr) : "";
        Tcl_HashEntry *entry;
        Axis *axis;
        newTrack = Rbc_FindTrack(graphPtr, name);
        entry = (elemPtr->mapYObjPtr != NULL)
                    ? Tcl_FindHashEntry(&graphPtr->axes.table, Tcl_GetString(elemPtr->mapYObjPtr))
                    : NULL;
        axis = (entry != NULL) ? Tcl_GetHashValue(entry) : NULL;
        if (newTrack == NULL || axis == NULL || axis->track != newTrack) {
            Tcl_SetObjResult(graphPtr->interp,
                             Tcl_NewStringObj("element requires -track and a -mapy axis belonging to that track", -1));
            trackValid = FALSE;
        }
    } else if (elemPtr->trackObjPtr != NULL && Tcl_GetCharLength(elemPtr->trackObjPtr) > 0) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-track requires a trackgraph widget", -1));
        trackValid = FALSE;
    }
    if (!trackValid || (*elemPtr->procsPtr->configProc)(graphPtr, elemPtr) != TCL_OK) {
        /*
         * The argument vector belongs to the caller and must not
         * remain stored while restoring or destroying the element.
         */
        elemPtr->optionMask = 0;
        elemPtr->optionObjc = 0;
        elemPtr->optionObjv = NULL;
        return Rbc_RestoreOptionsOnError(graphPtr->interp, &savedOptions);
    }
    elemPtr->optionMask = 0;
    elemPtr->optionObjc = 0;
    elemPtr->optionObjv = NULL;
    elemPtr->track = newTrack;
    elemPtr->optionsConfigured = TRUE;
    Tk_FreeSavedOptions(&savedOptions);
    if (maskPtr != NULL) {
        *maskPtr = mask;
    }
    return TCL_OK;
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Release derived and Tk-owned resources once, retaining the record.
 * Clears borrowed configuration context, then invokes the concrete destructor
 * even if option initialization failed. Frees initialized Tk options afterward,
 * so derived GCs are released before their colors, fonts and bitmaps.
 * Requires the owning graph's resource-release context, including a usable Tk
 * window for bitmap options. Does not remove registry or display-list entries.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] elemPtr Borrowed live element belonging to graphPtr; resource requirements are described
 *   above.
 */
static void ReleaseElementResources(Graph *graphPtr, Element *elemPtr) {
    if (elemPtr->tkResourcesReleased) {
        return;
    }
    /*
     * Never retain references to a caller-owned configuration argument
     * vector during destruction.
     */
    elemPtr->optionMask = 0;
    elemPtr->optionObjc = 0;
    elemPtr->optionObjv = NULL;
    /*
     * Release derived GCs and manually managed resources before Tk
     * releases the colours, fonts, borders, and bitmaps referenced by
     * those resources.
     *
     * Call the concrete destructor even if option initialization failed:
     * the element constructor may already have allocated chains, arrays,
     * or an embedded pen.
     */
    (*elemPtr->procsPtr->destroyProc)(graphPtr, elemPtr);
    if (elemPtr->optionsInitialized) {
        Tk_FreeConfigOptions((char *)elemPtr, elemPtr->optionTable, graphPtr->tkwin);
        Tk_DeleteOptionTable(elemPtr->optionTable);
        elemPtr->optionTable = NULL;
        elemPtr->optionsInitialized = FALSE;
    }
    elemPtr->tkResourcesReleased = TRUE;
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Release resources for every registered element before window teardown.
 * Includes hidden and non-displayed elements. Each release is idempotent through
 * tkResourcesReleased; element records, names and registry entries survive this
 * pass. The graph and its Tk window must remain valid throughout traversal.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 */
void Rbc_ReleaseElementTkResources(Graph *graphPtr) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        Element *elemPtr = Tcl_GetHashValue(hPtr);
        ReleaseElementResources(graphPtr, elemPtr);
    }
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Remove one element and defer its record release while preserved.
 * Removes bindings and global-legend references, releases concrete and Tk
 * resources, then removes all matching display-list links and the optional
 * registry entry. A displayed non-hidden element requests RESET_WORLD/redraw.
 * Frees the owned name; record storage survives only existing Tcl_Preserve scopes.
 * Released resources and registry identity are no longer usable.
 * Callers destroying the whole registry clear hashPtr before this call.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] elemPtr Owned element to remove; identity and resources become invalid even if record storage
 *   is preserved.
 */
static void DestroyElement(Graph *graphPtr, Element *elemPtr) {
    Rbc_ChainLink *linkPtr, *nextPtr;
    int wasDisplayed = FALSE;

    Rbc_DeleteBindings(graphPtr->bindTable, elemPtr);
    Rbc_LegendRemoveElement(graphPtr->legend, elemPtr);
    ReleaseElementResources(graphPtr, elemPtr);
    /* Remove every borrowed display reference before freeing the record. */
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL; linkPtr = nextPtr) {
        nextPtr = Rbc_ChainNextLink(linkPtr);
        if (elemPtr == Rbc_ChainGetValue(linkPtr)) {
            Rbc_ChainDeleteLink(graphPtr->elements.displayList, linkPtr);
            wasDisplayed = TRUE;
        }
    }
    if (wasDisplayed && !elemPtr->hidden) {
        graphPtr->flags |= RESET_WORLD;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    /* Remove the element for the graph's hash table of elements */
    if (elemPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(elemPtr->hashPtr);
    }
    if (elemPtr->name != NULL) {
        ckfree((char *)elemPtr->name);
    }
    Tcl_EventuallyFree(elemPtr, TCL_DYNAMIC);
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Construct, register and configure an element from command arguments.
 * Requires a dispatcher-validated argument vector with the name at objv[3].
 * Rejects duplicate names, leading hyphens and unsupported trackgraph types.
 * Registers the concrete record before option initialization; any initialization
 * or configuration failure destroys it and returns TCL_ERROR.
 * Success prepends it to the display list, marks mapping and axes dirty, queues
 * redraw if visible, and returns its name as the Tcl result. Defaults pass
 * through concrete configuration even when no explicit options are supplied.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 * @param[in] classUid Interned concrete element class selecting the constructor.
 */
static int CreateElement(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid) {
    Element *elemPtr;
    Tcl_HashEntry *hPtr;
    int isNew;

    const char *elemName = Tcl_GetString(objv[3]);
    if (elemName[0] == '-') {
        Rbc_AppendResultStrings(graphPtr->interp, "name of element \"", elemName, "\" can't start with a '-'",
                                (char *)NULL);
        return TCL_ERROR;
    }
    if (graphPtr->trackGraph && classUid != rbcLineElementUid && classUid != rbcStripElementUid &&
        classUid != rbcBarElementUid) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("trackgraph supports line, strip and bar elements", -1));
        return TCL_ERROR;
    }
    hPtr = Tcl_CreateHashEntry(&graphPtr->elements.table, elemName, &isNew);
    if (!isNew) {
        Rbc_AppendResultStrings(interp, "element \"", elemName, "\" already exists in \"", Tcl_GetString(objv[0]), "\"",
                                (char *)NULL);
        return TCL_ERROR;
    }
    if (classUid == rbcBarElementUid) {
        elemPtr = Rbc_BarElement(graphPtr, elemName, classUid);
    } else {
        /* Stripcharts are line graphs with some options enabled. */
        elemPtr = Rbc_LineElement(graphPtr, elemName, classUid);
    }
    elemPtr->hashPtr = hPtr;
    Tcl_SetHashValue(hPtr, elemPtr);
    assert(elemPtr->optionSpecs != NULL);
    if (InitElementOptions(graphPtr, elemPtr) != TCL_OK) {
        DestroyElement(graphPtr, elemPtr);
        return TCL_ERROR;
    }
    /*
     * Always use the transactional path, including when no explicit
     * option/value pairs were supplied. Tk_InitOptions installed the
     * defaults, and configProc must construct the derived state.
     */
    if (ConfigureElementOptions(graphPtr, elemPtr, objc - 4, objv + 4, NULL) != TCL_OK) {
        DestroyElement(graphPtr, elemPtr);
        return TCL_ERROR;
    }
    Rbc_ChainPrepend(graphPtr->elements.displayList, elemPtr);
    if (!elemPtr->hidden) {
        /* If the new element isn't hidden then redraw the graph.  */
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    elemPtr->flags |= MAP_ITEM;
    graphPtr->flags |= RESET_AXES;
    Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->name, -1));
    return TCL_OK;
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Replace display membership and order from a Tcl list of names.
 * Malformed list syntax returns TCL_ERROR before changing the display list.
 * Otherwise hides all registered elements, appends each recognized name in input
 * order and unhides it. Unknown names are ignored and their diagnostics cleared.
 * Repeated names retain only their first occurrence. Returns TCL_OK, sets RESET_WORLD and
 * schedules redraw; registry ownership is unchanged.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in] newListObj Borrowed Tcl list of desired display names in drawing order.
 */
static int RebuildDisplayList(Graph *graphPtr, Tcl_Obj *newListObj) {
    Tcl_Size nNames;      /* Number of names found in Tcl name list */
                          //    const char **nameArr;        /* Broken out array of element names */
    Tcl_Obj **nameObjArr; /* Broken out array of element names */
    Tcl_HashSearch cursor;
    Tcl_Size i;
    register Tcl_HashEntry *hPtr;
    Element *elemPtr; /* Element information record */

    if (Tcl_ListObjGetElements(graphPtr->interp, newListObj, &nNames, &nameObjArr) != TCL_OK) {
        return TCL_ERROR;
    }
    /* Clear the display list and mark all elements as hidden.  */
    Rbc_ChainReset(graphPtr->elements.displayList);
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = (Element *)Tcl_GetHashValue(hPtr);
        elemPtr->hidden = TRUE;
    }
    /* Rebuild the display list, checking that each name it exists
     * (currently ignoring invalid element names).  */
    for (i = 0; i < nNames; i++) {
        if (NameToElement(graphPtr, nameObjArr[i], &elemPtr) == TCL_OK && elemPtr->hidden) {
            /* All elements started hidden; an unhidden one is already listed. */
            elemPtr->hidden = FALSE;
            Rbc_ChainAppend(graphPtr->elements.displayList, elemPtr);
        }
    }
    //    ckfree((char *)nameArr);
    graphPtr->flags |= RESET_WORLD;
    Rbc_EventuallyRedrawGraph(graphPtr);
    Tcl_ResetResult(graphPtr->interp);
    return TCL_OK;
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Destroy all registered elements and their container structures.
 * Clears each record's hashPtr before destruction so the registry traversal
 * remains intact. Then deletes the element and tag tables and display chain.
 * This is final teardown, not an idempotent reset or reusable initialization.
 * Resource release must occur in the graph's valid Tk teardown sequence.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 */
void Rbc_DestroyElements(Graph *graphPtr) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Element *elemPtr;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = (Element *)Tcl_GetHashValue(hPtr);
        elemPtr->hashPtr = NULL;
        DestroyElement(graphPtr, elemPtr);
    }
    Tcl_DeleteHashTable(&graphPtr->elements.table);
    Tcl_DeleteHashTable(&graphPtr->elements.tagTable);
    Rbc_ChainDestroy(graphPtr->elements.displayList);
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Map visible display-list elements whose geometry is dirty.
 * Resets bar stacks for trackgraphs or modes other than MODE_INFRONT. Skips
 * hidden and plotHidden records; MAP_ALL or MAP_ITEM selects each remaining
 * mapping call. Enters and restores the owning track viewport around dispatch,
 * then clears MAP_ITEM. Screen mapping sets GRAPH_CHANGED; export mapping does
 * not. The caller owns layout sequencing and graph-wide flag cleanup.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 */
void Rbc_MapElements(Graph *graphPtr) {
    Element *elemPtr;
    Rbc_ChainLink *linkPtr;

    if (graphPtr->trackGraph || graphPtr->mode != MODE_INFRONT) {
        Rbc_ResetStacks(graphPtr);
    }
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->hidden || elemPtr->plotHidden) {
            continue;
        }
        if ((graphPtr->flags & MAP_ALL) || (elemPtr->flags & MAP_ITEM)) {
            /*
             * Closest-point annotations can change even when the axes
             * remain fixed. Record native element remapping separately
             * from marker-only redraws.
             */
            if (!(graphPtr->flags & GRAPH_EXPORT)) {
                graphPtr->flags |= GRAPH_CHANGED;
            }
            {
                RbcTrackViewport saved;
                Rbc_EnterTrack(graphPtr, elemPtr->track, &saved);
                (*elemPtr->procsPtr->mapProc)(graphPtr, elemPtr);
                Rbc_LeaveTrack(graphPtr, &saved);
            }
            elemPtr->flags &= ~MAP_ITEM;
        }
    }
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Draw a complete normal or active element within a screen track clip.
 * Requires a non-NULL track and an already entered track viewport. Intersects
 * inclusive track bounds with drawable graph dimensions and skips empty bounds.
 * Dispatches only when the renderer supplies a drawable, then balances clip
 * and renderer lifetime. Clipping includes native raised bar borders.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in] drawable Borrowed compatible screen drawable receiving mapped geometry.
 * @param[in,out] elemPtr Borrowed live element belonging to graphPtr; resource requirements are described
 *   above.
 * @param[in] active Nonzero dispatches the active overlay; zero dispatches normal drawing.
 */
static void DrawClippedTrackElement(Graph *graphPtr, Drawable drawable, Element *elemPtr, int active) {
    RbcTrack *track = elemPtr->track;
    Rbc_RenderContext *clip;
    Drawable target;
    int left = MAX(0, track->left), top = MAX(0, track->top);
    int width = MIN(graphPtr->width - 1, track->right) - left + 1;
    int height = MIN(graphPtr->height - 1, track->bottom) - top + 1;
    if (width <= 0 || height <= 0) {
        return;
    }
    clip = Rbc_RenderBeginScreenClip(graphPtr, drawable, NULL, 0);
    Rbc_RenderClipBegin(clip, left, top, width, height);
    target = Rbc_RenderClipDrawable(clip);
    if (target != None) {
        if (active) {
            (*elemPtr->procsPtr->drawActiveProc)(graphPtr, target, elemPtr);
        } else {
            (*elemPtr->procsPtr->drawNormalProc)(graphPtr, target, elemPtr);
        }
    }
    Rbc_RenderClipEnd(clip);
    Rbc_RenderEnd(clip);
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Draw visible elements in display-list order using mapped geometry.
 * Skips hidden, plotHidden and collapsed track regions. Enters and restores each
 * track viewport; scrolled tracks and track bars additionally clip the complete
 * callback. Does not remap data or manage element lifetime across callbacks.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in] drawable Borrowed compatible screen drawable receiving mapped geometry.
 */
void Rbc_DrawElements(Graph *graphPtr, Drawable drawable) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if ((!elemPtr->hidden) && (!elemPtr->plotHidden)) {
            if (elemPtr->track == NULL || (elemPtr->track->right - elemPtr->track->left > 1 &&
                                           elemPtr->track->bottom - elemPtr->track->top > 1)) {
                RbcTrackViewport saved;
                Rbc_EnterTrack(graphPtr, elemPtr->track, &saved);
                if (elemPtr->track != NULL && (graphPtr->trackScroll || elemPtr->classUid == rbcBarElementUid)) {
                    DrawClippedTrackElement(graphPtr, drawable, elemPtr, 0);
                } else {
                    (*elemPtr->procsPtr->drawNormalProc)(graphPtr, drawable, elemPtr);
                }
                Rbc_LeaveTrack(graphPtr, &saved);
            }
        }
    }
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Draw active overlays in display-list order using mapped geometry.
 * Requires ELEM_ACTIVE in addition to normal visibility and track-size checks.
 * Uses the same viewport and full-callback clipping policy as Rbc_DrawElements,
 * but dispatches drawActiveProc. The caller controls ordering relative to the
 * normal pass and keeps graph, element and mapped resources alive.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in] drawable Borrowed compatible screen drawable receiving mapped geometry.
 */
void Rbc_DrawActiveElements(Graph *graphPtr, Drawable drawable) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if ((!elemPtr->hidden) && (!elemPtr->plotHidden) && (elemPtr->flags & ELEM_ACTIVE)) {
            if (elemPtr->track == NULL || (elemPtr->track->right - elemPtr->track->left > 1 &&
                                           elemPtr->track->bottom - elemPtr->track->top > 1)) {
                RbcTrackViewport saved;
                Rbc_EnterTrack(graphPtr, elemPtr->track, &saved);
                if (elemPtr->track != NULL && (graphPtr->trackScroll || elemPtr->classUid == rbcBarElementUid)) {
                    DrawClippedTrackElement(graphPtr, drawable, elemPtr, 1);
                } else {
                    (*elemPtr->procsPtr->drawActiveProc)(graphPtr, drawable, elemPtr);
                }
                Rbc_LeaveTrack(graphPtr, &saved);
            }
        }
    }
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Export normal visible elements through the selected export backend.
 * Dispatches mapped geometry in display-list order, entering and restoring each
 * track viewport. Scrolled tracks and track bars wrap the callback in an export
 * renderer clip. Collapsed tracks are skipped. Only PostScript receives element
 * comments; the geometry dispatch also supports SVG and PDF.
 * Does not own exportPtr or perform layout; errors use the export context.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] exportPtr Borrowed export context receiving output and backend error state.
 */
void Rbc_ElementsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if ((!elemPtr->hidden) && (!elemPtr->plotHidden)) {
            /* Comment the PostScript to indicate the start of the element */
            if (exportPtr->backend == RBC_EXPORT_POSTSCRIPT) {
                Rbc_ExportFormat(exportPtr, "\n%% Element \"%s\"\n\n", elemPtr->name);
            }
            if (elemPtr->track == NULL || (elemPtr->track->right - elemPtr->track->left > 1 &&
                                           elemPtr->track->bottom - elemPtr->track->top > 1)) {
                RbcTrackViewport saved;
                Rbc_EnterTrack(graphPtr, elemPtr->track, &saved);
                if (elemPtr->track != NULL && (graphPtr->trackScroll || elemPtr->classUid == rbcBarElementUid)) {
                    Rbc_RenderContext *ctx = Rbc_RenderBeginExportOutput(exportPtr);
                    Rbc_RenderClipBegin(ctx, elemPtr->track->left, elemPtr->track->top,
                                        elemPtr->track->right - elemPtr->track->left + 1,
                                        elemPtr->track->bottom - elemPtr->track->top + 1);
                    (*elemPtr->procsPtr->exportNormalProc)(graphPtr, exportPtr, elemPtr);
                    Rbc_RenderClipEnd(ctx);
                    Rbc_RenderEnd(ctx);
                } else {
                    (*elemPtr->procsPtr->exportNormalProc)(graphPtr, exportPtr, elemPtr);
                }
                Rbc_LeaveTrack(graphPtr, &saved);
            }
        }
    }
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Export active overlays through the selected export backend.
 * Uses the visibility, track viewport and clipping policy of Rbc_ElementsExport
 * with an additional ELEM_ACTIVE check and exportActiveProc dispatch.
 * Emits active-element comments only for PostScript. The caller owns the export
 * context and ordering of normal and active passes.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] exportPtr Borrowed export context receiving output and backend error state.
 */
void Rbc_ActiveElementsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if ((!elemPtr->hidden) && (!elemPtr->plotHidden) && (elemPtr->flags & ELEM_ACTIVE)) {
            if (exportPtr->backend == RBC_EXPORT_POSTSCRIPT) {
                Rbc_ExportFormat(exportPtr, "\n%% Active Element \"%s\"\n\n", elemPtr->name);
            }
            if (elemPtr->track == NULL || (elemPtr->track->right - elemPtr->track->left > 1 &&
                                           elemPtr->track->bottom - elemPtr->track->top > 1)) {
                RbcTrackViewport saved;
                Rbc_EnterTrack(graphPtr, elemPtr->track, &saved);
                if (elemPtr->track != NULL && (graphPtr->trackScroll || elemPtr->classUid == rbcBarElementUid)) {
                    Rbc_RenderContext *ctx = Rbc_RenderBeginExportOutput(exportPtr);
                    Rbc_RenderClipBegin(ctx, elemPtr->track->left, elemPtr->track->top,
                                        elemPtr->track->right - elemPtr->track->left + 1,
                                        elemPtr->track->bottom - elemPtr->track->top + 1);
                    (*elemPtr->procsPtr->exportActiveProc)(graphPtr, exportPtr, elemPtr);
                    Rbc_RenderClipEnd(ctx);
                    Rbc_RenderEnd(ctx);
                } else {
                    (*elemPtr->procsPtr->exportActiveProc)(graphPtr, exportPtr, elemPtr);
                }
                Rbc_LeaveTrack(graphPtr, &saved);
            }
        }
    }
}

/**
 * @ingroup element_lifecycle_internals
 * @brief Test visible display-list X/Y vector clients for pending notification.
 * Returns 1 on the first pending X or Y client, otherwise 0. Does not deliver
 * notifications, synchronize arrays, mark geometry or schedule redraw.
 * This is not an exhaustive dirty-state test: it does not inspect weights,
 * error inputs, concrete polar inputs or elements outside the visible list.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 */
int Rbc_GraphUpdateNeeded(Graph *graphPtr) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->hidden || elemPtr->plotHidden) {
            continue;
        }
        /* Check if the x or y vectors have notifications pending */
        if ((Rbc_VectorNotifyPending(elemPtr->x.clientId)) || (Rbc_VectorNotifyPending(elemPtr->y.clientId))) {
            return 1;
        }
    }
    return 0;
}

/**
 * @ingroup element_command_internals
 * @brief Read current activation bounds without consuming vector notifications.
 * Requires a live element whose resources have not been released. Literal slots
 * use cached lengths; named slots query their live vectors. Polar elements use
 * their concrete point-count callback to include complex input.
 *
 * @param[in] elemPtr Borrowed live element with unreleased literal or vector input resources.
 */
static Tcl_Size ActivationPointCount(Element *elemPtr) {
    Tcl_Size nx, ny;

    if (elemPtr->classUid == rbcPolarElementUid) {
        return NumberOfPoints(elemPtr);
    }
    nx = elemPtr->x.clientId != NULL ? Rbc_VectorLength(elemPtr->x.vecPtr) : elemPtr->x.nValues;
    ny = elemPtr->y.clientId != NULL ? Rbc_VectorLength(elemPtr->y.vecPtr) : elemPtr->y.nValues;
    return MIN(nx, ny);
}

/**
 * @ingroup element_command_internals
 * @brief Query active registry names or replace one element's active indices.
 *
 * No name returns all ELEM_ACTIVE registry entries in hash iteration order,
 * including hidden elements. A name without indices activates the whole element
 * using nActiveIndices = -1. Explicit indices are staged in owned storage and
 * committed only after all conversions succeed; duplicates retain input order.
 * Success replaces old indices, sets ELEM_ACTIVE and ACTIVE_PENDING and queues
 * redraw. Ordinary conversion failure frees the candidate and retains old state.
 * Preserves element storage across index expressions, rejects released records,
 * and revalidates every staged index against current data before commit. The
 * enclosing widget command preserves the graph. Script errors retain their
 * original diagnostics; callback side effects are not rolled back.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int ActivateOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Size *activeArr;
    Tcl_Size nActiveIndices;

    (void)type;
    /*
     * With no element name, return all currently active elements.
     */
    if (objc == 3) {
        Tcl_HashEntry *hPtr;
        Tcl_HashSearch cursor;
        Tcl_Obj *resultObjPtr;

        resultObjPtr = Tcl_NewListObj(0, NULL);
        for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &cursor); hPtr != NULL;
             hPtr = Tcl_NextHashEntry(&cursor)) {
            elemPtr = Tcl_GetHashValue(hPtr);
            if (elemPtr->flags & ELEM_ACTIVE) {
                Tcl_ListObjAppendElement(interp, resultObjPtr, Tcl_NewStringObj(elemPtr->name, -1));
            }
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (NameToElement(graphPtr, objv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    activeArr = NULL;
    nActiveIndices = -1;
    if (objc > 4) {
        Tcl_Size count;
        Tcl_Size *activePtr;
        size_t byteCount;

        count = objc - 4;
        if (GetElemArrayByteCount(interp, count, sizeof(Tcl_Size), &byteCount) != TCL_OK) {
            return TCL_ERROR;
        }
        activeArr = Tcl_AttemptAlloc(byteCount);
        if (activeArr == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate active element indices", -1));
            return TCL_ERROR;
        }
        activePtr = activeArr;
        nActiveIndices = count;
        Tcl_Preserve(elemPtr);
        for (Tcl_Size i = 4; i < objc; i++) {
            int result = GetIndex(interp, ActivationPointCount(elemPtr), Tcl_GetString(objv[i]), activePtr);

            if (result != TCL_OK) {
                ckfree(activeArr);
                Tcl_Release(elemPtr);
                return TCL_ERROR;
            }
            if (graphPtr->tkwin == NULL || elemPtr->tkResourcesReleased) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("element deleted during activation index evaluation", -1));
                Tcl_SetErrorCode(interp, "RBC", "ELEMENT", "CALLBACK", NULL);
                ckfree(activeArr);
                Tcl_Release(elemPtr);
                return TCL_ERROR;
            }
            activePtr++;
        }
        /* A later expression may shrink data used by an earlier index. */
        {
            Tcl_Size nPoints = ActivationPointCount(elemPtr);

            for (Tcl_Size i = 0; i < count; i++) {
                if (activeArr[i] >= nPoints) {
                    Tcl_SetObjResult(interp, Tcl_ObjPrintf("element index \"%s\" is out of range",
                                                          Tcl_GetString(objv[i + 4])));
                    ckfree(activeArr);
                    Tcl_Release(elemPtr);
                    return TCL_ERROR;
                }
            }
        }
        Tcl_Release(elemPtr);
    }
    /*
     * Commit the replacement only after every supplied index has been
     * validated.
     */
    if (elemPtr->activeIndices != NULL) {
        ckfree(elemPtr->activeIndices);
    }
    elemPtr->activeIndices = activeArr;
    elemPtr->nActiveIndices = nActiveIndices;
    elemPtr->flags |= ELEM_ACTIVE | ACTIVE_PENDING;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}
/**
 * @ingroup element_command_internals
 * @brief Intern a binding tag in the graph's element tag table.
 *
 * Copies a new string key into the table or reuses an existing entry. Returns
 * the table-owned key as a stable binding token until tag-table destruction.
 * Does not require an element with that name or create a binding script.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in] tagName Borrowed NUL-terminated tag name, copied when first interned.
 */
ClientData Rbc_MakeElementTag(Graph *graphPtr, char *tagName) {
    Tcl_HashEntry *hPtr;
    int isNew;

    hPtr = Tcl_CreateHashEntry(&graphPtr->elements.tagTable, tagName, &isNew);
    assert(hPtr);
    return Tcl_GetHashKey(&graphPtr->elements.tagTable, hPtr);
}

/**
 * @ingroup element_command_internals
 * @brief Append an element's binding tags to a caller-owned list.
 *
 * Appends interned name and class tokens first, then configured tags in order.
 * Does not clear the list or filter repeated tags. Token lifetime belongs to the
 * graph's tag table; list-node lifetime belongs to the receiving list.
 * Does not execute binding scripts.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] elemPtr Borrowed live element belonging to graphPtr; resource requirements are described
 *   above.
 * @param[in,out] list Caller-owned list to append to; existing entries are retained.
 */
void Rbc_AppendElementBindingTags(Graph *graphPtr, Element *elemPtr, Rbc_List list) {
    char **p;

    Rbc_ListAppend(list, Rbc_MakeElementTag(graphPtr, elemPtr->name), 0);
    Rbc_ListAppend(list, Rbc_MakeElementTag(graphPtr, elemPtr->classUid), 0);
    if (elemPtr->tags == NULL) {
        return;
    }
    for (p = elemPtr->tags; *p != NULL; p++) {
        Rbc_ListAppend(list, Rbc_MakeElementTag(graphPtr, *p), 0);
    }
}

/**
 * @ingroup element_command_internals
 * @brief Query interned element tags or delegate tag binding configuration.
 *
 * With no tag returns every interned tag in hash order, not just tags with a
 * binding or a live element. Otherwise interns the supplied tag and delegates
 * remaining query/set arguments to Rbc_ConfigureBindingsFromObj.
 * Returns the delegated Tcl status; interning can survive a failed binding call.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int BindOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        Tcl_HashEntry *hPtr;
        Tcl_HashSearch cursor;
        char *tagName;

        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
        for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.tagTable, &cursor); hPtr != NULL;
             hPtr = Tcl_NextHashEntry(&cursor)) {
            tagName = Tcl_GetHashKey(&graphPtr->elements.tagTable, hPtr);
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(tagName, -1));
        }
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    return Rbc_ConfigureBindingsFromObj(interp, graphPtr->bindTable,
                                        Rbc_MakeElementTag(graphPtr, Tcl_GetString(objv[3])), objc - 4, objv + 4);
}

/**
 * @ingroup element_command_internals
 * @brief Delegate element creation using the dispatcher-supplied class UID.
 *
 * Passes the complete validated command vector to CreateElement and returns
 * its status and result. This wrapper adds no ownership or rollback layer.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int CreateOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return CreateElement(graphPtr, interp, objc, objv, type);
}

/**
 * @ingroup element_command_internals
 * @brief Return one named element's Tk option value.
 *
 * Resolves the registry record, asks its option table for objv[4], and installs
 * the returned object as the interpreter result. Returns TCL_ERROR for missing
 * elements or invalid options. Does not configure or map the element.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Obj *resultObjPtr;

    if (NameToElement(graphPtr, objv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    resultObjPtr = Tk_GetOptionValue(interp, (char *)elemPtr, elemPtr->optionTable, objv[4], graphPtr->tkwin);
    if (resultObjPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, resultObjPtr);
    return TCL_OK;
}

/**
 * @ingroup element_data_internals
 * @brief Read one finite, current error-data sample without synchronizing it.
 * @param vectorPtr Borrowed error-data slot.
 * @param index Source index to inspect.
 * @param[out] valuePtr Assigned only if a valid sample is available.
 * @return Nonzero on success; zero for pending vector updates, invalid index,
 * NULL storage or nonfinite data. Does not dereference storage while a vector
 * notification is pending. No allocation, callbacks or graph changes occur.
 */
static int ClosestErrorValue(const ElemVector *vectorPtr, Tcl_Size index, double *valuePtr) {
    if ((vectorPtr->clientId != NULL) && Rbc_VectorNotifyPending(vectorPtr->clientId)) {
        return FALSE;
    }
    if ((index < 0) || (index >= vectorPtr->nValues) || (vectorPtr->valueArr == NULL) ||
        !FINITE(vectorPtr->valueArr[index])) {
        return FALSE;
    }
    *valuePtr = vectorPtr->valueArr[index];
    return TRUE;
}

/**
 * @ingroup element_data_internals
 * @brief Collect available source error values in a private result dictionary.
 * @param interp Interpreter for dictionary errors.
 * @param elemPtr Live selected element with error slots.
 * @param index Selected original source index.
 * @param resultObjPtr Unshared result dictionary owned by the caller.
 * @return TCL_OK after collection; TCL_ERROR on dictionary failure.
 * For each axis, a nonempty symmetric vector takes precedence; an unusable sample
 * does not fall back to explicit bounds. Otherwise both low/high values must be
 * usable. Reports source values, not translated bar positions. Copies values
 * into owned Tcl objects without variable writes or script evaluation. The caller
 * must finish all metadata collection before publishing any array fields.
 */
static int AppendClosestErrors(Tcl_Interp *interp, Element *elemPtr, Tcl_Size index, Tcl_Obj *resultObjPtr) {
    const ElemVector *vectors[] = {&elemPtr->xError, &elemPtr->xLow, &elemPtr->xHigh,
                                  &elemPtr->yError, &elemPtr->yLow, &elemPtr->yHigh};
    static const char *names[] = {"xerror", "xlow", "xhigh", "yerror", "ylow", "yhigh"};
    int axis;

    for (axis = 0; axis < 6; axis += 3) {
        double values[3];
        int first, last, i;

        /* Symmetric error vectors take precedence, as in Rbc_MapErrorBars. */
        if (vectors[axis]->nValues > 0) {
            if (!ClosestErrorValue(vectors[axis], index, &values[0])) {
                continue;
            }
            first = last = 0;
        } else {
            if (!ClosestErrorValue(vectors[axis + 1], index, &values[1]) ||
                !ClosestErrorValue(vectors[axis + 2], index, &values[2])) {
                continue;
            }
            first = 1;
            last = 2;
        }
        for (i = first; i <= last; i++) {
            if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj(names[axis + i], -1),
                               Tcl_NewDoubleObj(values[i])) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    return TCL_OK;
}

/**
 * @ingroup element_command_internals
 * @brief Publish a retained closest-result snapshot without reading graph state.
 *
 * The caller owns resultObjPtr throughout publication. Attempts to unset the six
 * optional error fields, ignoring unset status, then writes dictionary entries
 * in collection order. Variable traces may mutate or destroy graph data.
 * A failed write closes dictionary iteration and returns TCL_ERROR with its
 * diagnostic; earlier writes remain. Success sets the integer found result.
 * Unrelated old array fields are retained; publication is not atomic.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] varNameObjPtr Borrowed destination array name; writes may invoke variable traces.
 * @param[in] resultObjPtr Caller-retained dictionary snapshot; must remain live throughout publication.
 * @param[in] found Search hit flag installed as the command result after successful publication.
 */
static int PublishClosestInfo(Tcl_Interp *interp, Tcl_Obj *varNameObjPtr, Tcl_Obj *resultObjPtr, int found) {
    static const char *names[] = {"xerror", "xlow", "xhigh", "yerror", "ylow", "yhigh"};
    Tcl_DictSearch cursor;
    Tcl_Obj *keyObjPtr, *valueObjPtr;
    int done, k;

    /* Retain the existing optional-field cleanup and publication order. */
    for (k = 0; k < 6; k++) {
        Tcl_UnsetVar2(interp, Tcl_GetString(varNameObjPtr), names[k], 0);
    }
    if (Tcl_DictObjFirst(interp, resultObjPtr, &cursor, &keyObjPtr, &valueObjPtr, &done) != TCL_OK) {
        return TCL_ERROR;
    }
    for (; !done; Tcl_DictObjNext(&cursor, &keyObjPtr, &valueObjPtr, &done)) {
        if (Tcl_ObjSetVar2(interp, varNameObjPtr, keyObjPtr, valueObjPtr, TCL_LEAVE_ERR_MSG) == NULL) {
            Tcl_DictObjDone(&cursor);
            return TCL_ERROR;
        }
    }
    Tcl_DictObjDone(&cursor);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(found));
    return TCL_OK;
}

/**
 * @ingroup element_command_internals
 * @brief Find eligible mapped geometry and publish a stable result snapshot.
 *
 * Synchronizes common vector inputs and resets axes if requested, then parses
 * pixel coordinates and closest switches. Explicit hidden or unplotted names
 * are errors; implicit search skips them and traverses the display list in
 * reverse. Dirty geometry or pending X/Y notifications are skipped, not mapped.
 * Track searches require a noncollapsed viewport containing the query point.
 * A hit requires distance strictly less than the halo. Collects common fields,
 * bar bounds, source errors and concrete metadata before any variable traces.
 * Misses publish an empty name. Returns publication status and releases the
 * retained dictionary on every collection/publication exit; no graph accesses
 * occur after publication begins.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int ClosestOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    ClosestSearch search;
    Tcl_Size i;
    int x, y;
    Tcl_Obj *resultObjPtr;
    int found, result;

    Rbc_SyncElementVectors(graphPtr);
    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[3], &x) != TCL_OK) {
        Rbc_AppendResultStrings(interp, ": bad window x-coordinate", (char *)NULL);
        return TCL_ERROR;
    }
    if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[4], &y) != TCL_OK) {
        Rbc_AppendResultStrings(interp, ": bad window y-coordinate", (char *)NULL);
        return TCL_ERROR;
    }
    search.mode = SEARCH_POINTS;
    search.halo = graphPtr->halo;
    search.index = -1;
    search.along = SEARCH_BOTH;
    search.x = x;
    search.y = y;
    if (ParseClosestOptions(graphPtr, interp, objc, objv, &search, &i) != TCL_OK) {
        return TCL_ERROR;
    }
    search.dist = (double)search.halo + 1.0;
    if (i < objc) {
        for (/* empty */; i < objc; i++) {
            if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
                return TCL_ERROR; /* Can't find named element */
            }
            if (elemPtr->hidden) {
                Rbc_AppendResultStrings(interp, "element \"", Tcl_GetString(objv[i]), "\" is hidden", (char *)NULL);
                return TCL_ERROR; /* Element isn't visible */
            }
            if (elemPtr->plotHidden) {
                Rbc_AppendResultStrings(interp, "element \"", Tcl_GetString(objv[i]), "\" is not plotted",
                                        (char *)NULL);
                return TCL_ERROR;
            }
            /* Check if the X or Y vectors have notifications pending */
            if ((elemPtr->flags & MAP_ITEM) || (Rbc_VectorNotifyPending(elemPtr->x.clientId)) ||
                (Rbc_VectorNotifyPending(elemPtr->y.clientId))) {
                continue;
            }
            if (elemPtr->track == NULL ||
                (elemPtr->track->right - elemPtr->track->left > 1 && elemPtr->track->bottom - elemPtr->track->top > 1 &&
                 search.x >= elemPtr->track->left && search.x <= elemPtr->track->right &&
                 search.y >= elemPtr->track->top && search.y <= elemPtr->track->bottom)) {
                RbcTrackViewport saved;
                Rbc_EnterTrack(graphPtr, elemPtr->track, &saved);
                (*elemPtr->procsPtr->closestProc)(graphPtr, elemPtr, &search);
                Rbc_LeaveTrack(graphPtr, &saved);
            }
        }
    } else {
        Rbc_ChainLink *linkPtr;

        /*
         * Find the closest point from the set of displayed elements,
         * searching the display list from back to front.  That way if
         * the points from two different elements overlay each other
         * exactly, the last one picked will be the topmost.
         */
        for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
             linkPtr = Rbc_ChainPrevLink(linkPtr)) {
            elemPtr = Rbc_ChainGetValue(linkPtr);
            /* Check if the X or Y vectors have notifications pending */
            if ((elemPtr->flags & MAP_ITEM) || (Rbc_VectorNotifyPending(elemPtr->x.clientId)) ||
                (Rbc_VectorNotifyPending(elemPtr->y.clientId))) {
                continue;
            }
            if ((!elemPtr->hidden) && (!elemPtr->plotHidden)) {
                if (elemPtr->track == NULL || (elemPtr->track->right - elemPtr->track->left > 1 &&
                                               elemPtr->track->bottom - elemPtr->track->top > 1 &&
                                               search.x >= elemPtr->track->left && search.x <= elemPtr->track->right &&
                                               search.y >= elemPtr->track->top && search.y <= elemPtr->track->bottom)) {
                    RbcTrackViewport saved;
                    Rbc_EnterTrack(graphPtr, elemPtr->track, &saved);
                    (*elemPtr->procsPtr->closestProc)(graphPtr, elemPtr, &search);
                    Rbc_LeaveTrack(graphPtr, &saved);
                }
            }
        }
    }
    resultObjPtr = Tcl_NewDictObj();
    Tcl_IncrRefCount(resultObjPtr);
    found = (search.dist < (double)search.halo);
    if (found) {
        /*
         * Return the common closest-element information.
         */
        if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("name", -1),
                           Tcl_NewStringObj(search.elemPtr->name, -1)) != TCL_OK) {
            goto error;
        }
        if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("index", -1),
                           Tcl_NewWideIntObj((Tcl_WideInt)search.index)) != TCL_OK) {
            goto error;
        }
        if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("x", -1), Tcl_NewDoubleObj(search.point.x)) != TCL_OK) {
            goto error;
        }
        if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("y", -1), Tcl_NewDoubleObj(search.point.y)) != TCL_OK) {
            goto error;
        }
        if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("dist", -1), Tcl_NewDoubleObj(search.dist)) != TCL_OK) {
            goto error;
        }
        /*
         * Bar elements additionally report the physical screen
         * rectangle of the selected bar segment.
         *
         * The rectangle is the final mapped rectangle, so stacked,
         * aligned, overlap, and inverted bar layouts are already
         * reflected in these coordinates.
         */
        if (search.elemPtr->classUid == rbcBarElementUid) {
            Extents2D exts;

            if (Rbc_GetBarRectangle(search.elemPtr, search.index, &exts)) {
                if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("left", -1), Tcl_NewDoubleObj(exts.left)) != TCL_OK) {
                    goto error;
                }
                if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("top", -1), Tcl_NewDoubleObj(exts.top)) != TCL_OK) {
                    goto error;
                }
                if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("right", -1), Tcl_NewDoubleObj(exts.right)) != TCL_OK) {
                    goto error;
                }
                if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("bottom", -1), Tcl_NewDoubleObj(exts.bottom)) != TCL_OK) {
                    goto error;
                }
            }
        }
        if (AppendClosestErrors(interp, search.elemPtr, search.index, resultObjPtr) != TCL_OK) {
            goto error;
        }
        /*
         * Allow the concrete element type to append representation-specific
         * closest information.
         */
        if ((search.elemPtr->procsPtr->closestInfoProc != NULL) &&
            ((*search.elemPtr->procsPtr->closestInfoProc)(graphPtr, search.elemPtr, &search, resultObjPtr) != TCL_OK)) {
            goto error;
        }
    } else {
        /* an array with values "name" -> "" */
        if (Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj("name", -1), Tcl_NewObj()) != TCL_OK) {
            goto error;
        }
    }
    /* No graph or element access is permitted after publication starts. */
    result = PublishClosestInfo(interp, objv[5], resultObjPtr, found);
    Tcl_DecrRefCount(resultObjPtr);
    return result;

error:
    Tcl_DecrRefCount(resultObjPtr);
    return TCL_ERROR;
}

/**
 * @ingroup element_command_internals
 * @brief Query options or configure named elements sequentially.
 *
 * Resolves leading element names before the first hyphen-prefixed option.
 * Zero or one option returns Tk option information for the first named element.
 * Mutation uses ConfigureElementOptions for each element, synchronizes hidden
 * state with display membership and marks axes, layout and mapping dirty.
 * Failure stops processing; earlier successfully configured elements stay
 * changed. The operation is not a transaction across all named elements.
 * Normal completion schedules backing-store and margin redraw.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Size numNames;
    Tcl_Size numOpts;
    Tcl_Obj *const *options;
    Tcl_Size i;

    /*
     * Figure out where the option/value pairs begin.
     */
    objc -= 3;
    objv += 3;
    for (i = 0; i < objc; i++) {
        const char *string;

        string = Tcl_GetString(objv[i]);
        if (string[0] == '-') {
            break;
        }
        if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    numNames = i;
    numOpts = objc - i;
    options = objv + numNames;
    for (i = 0; i < numNames; i++) {
        if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (numOpts == 0) {
            Tcl_Obj *resultObjPtr;

            resultObjPtr = Tk_GetOptionInfo(interp, (char *)elemPtr, elemPtr->optionTable, NULL, graphPtr->tkwin);
            if (resultObjPtr == NULL) {
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, resultObjPtr);
            return TCL_OK;
        }
        if (numOpts == 1) {
            Tcl_Obj *resultObjPtr;

            resultObjPtr = Tk_GetOptionInfo(interp, (char *)elemPtr, elemPtr->optionTable, options[0], graphPtr->tkwin);
            if (resultObjPtr == NULL) {
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, resultObjPtr);
            return TCL_OK;
        }
        if (ConfigureElementOptions(graphPtr, elemPtr, numOpts, options, NULL) != TCL_OK) {
            return TCL_ERROR;
        }
        /*
         * Keep the element's hidden state and display-list membership
         * synchronized.
         */
        {
            Rbc_ChainLink *linkPtr;

            for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
                 linkPtr = Rbc_ChainNextLink(linkPtr)) {
                if (elemPtr == Rbc_ChainGetValue(linkPtr)) {
                    break;
                }
            }
            if (elemPtr->hidden != (linkPtr == NULL)) {
                if (linkPtr == NULL) {
                    Rbc_ChainPrepend(graphPtr->elements.displayList, elemPtr);
                } else {
                    Rbc_ChainDeleteLink(graphPtr->elements.displayList, linkPtr);
                }
            }
        }
        graphPtr->flags |= RESET_AXES;
        graphPtr->flags |= RESET_WORLD;
        graphPtr->flags |= MAP_WORLD | REDRAW_WORLD;
        elemPtr->flags |= MAP_ITEM;
    }
    graphPtr->flags |= REDRAW_BACKING_STORE | DRAW_MARGINS;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/**
 * @ingroup element_command_internals
 * @brief Clear activation and release indices for each named element.
 *
 * Processes names sequentially, clearing ELEM_ACTIVE and setting the index
 * pointer/count to NULL/zero. A later missing name returns TCL_ERROR without
 * restoring earlier changes or reaching the final redraw request.
 * No arguments change nothing but still schedule redraw on successful completion.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int DeactivateOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Size i;

    for (i = 3; i < objc; i++) {
        if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR; /* Can't find named element */
        }
        elemPtr->flags &= ~ELEM_ACTIVE;
        if (elemPtr->activeIndices != NULL) {
            ckfree((char *)elemPtr->activeIndices);
            elemPtr->activeIndices = NULL;
        }
        elemPtr->nActiveIndices = 0;
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/**
 * @ingroup element_command_internals
 * @brief Destroy named elements in argument order.
 *
 * Missing names return TCL_ERROR immediately; earlier deletions are not undone.
 * Repeating a name therefore deletes it once and then fails lookup. Each
 * destructor handles its own invalidation; successful completion additionally
 * queues graph redraw, including for an empty name list.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int DeleteOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_Size i;

    for (i = 3; i < objc; i++) {
        if (NameToElement(graphPtr, objv[i], &elemPtr) != TCL_OK) {
            return TCL_ERROR; /* Can't find named element */
        }
        DestroyElement(graphPtr, elemPtr);
    }
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/**
 * @ingroup element_command_internals
 * @brief Return whether an exact element name is in the registry.
 *
 * Sets a Tcl boolean result and returns TCL_OK. Includes hidden and unplotted
 * elements and does not use NameToElement's missing-name diagnostic.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int ExistsOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->elements.table, Tcl_GetString(objv[3]));
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(hPtr != NULL));
    return TCL_OK;
}

/**
 * @ingroup element_command_internals
 * @brief Return the current binding element's name when available.
 *
 * Only exact current selects an item, and the binding context must be ELEMENT.
 * Recognizes line, strip, bar and polar records. Other selectors or contexts
 * return TCL_OK without assigning a result; the command caller supplies the
 * ordinary empty initial result. Does not repick or execute bindings.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int GetOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    register Element *elemPtr;

    const char *str = Tcl_GetString(objv[3]);
    if ((str[0] == 'c') && (strcmp(str, "current") == 0)) {
        if (PTR2INT(Rbc_GetCurrentContext(graphPtr->bindTable)) != GRAPH_BIND_CONTEXT_ELEMENT) {
            return TCL_OK;
        }
        elemPtr = (Element *)Rbc_GetCurrentItem(graphPtr->bindTable);
        /* Report only on elements. */
        if ((elemPtr != NULL) &&
            ((elemPtr->classUid == rbcBarElementUid) || (elemPtr->classUid == rbcLineElementUid) ||
             (elemPtr->classUid == rbcStripElementUid) || (elemPtr->classUid == rbcPolarElementUid))) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->name, -1));
        }
    }
    return TCL_OK;
}

/**
 * @ingroup element_command_internals
 * @brief List registered element names matching any optional glob pattern.
 *
 * Includes hidden elements. No patterns includes every registry entry; otherwise
 * each name is appended once on its first matching pattern. Returns TCL_OK with
 * a new Tcl list in hash iteration order, not drawing order.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int NamesOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Tcl_HashSearch cursor;
    register Tcl_HashEntry *hPtr;
    Tcl_Size i;

    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &cursor); hPtr != NULL;
         hPtr = Tcl_NextHashEntry(&cursor)) {
        elemPtr = (Element *)Tcl_GetHashValue(hPtr);
        if (objc == 3) {
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(elemPtr->name, -1));
            continue;
        }
        for (i = 3; i < objc; i++) {
            if (Tcl_StringMatch(elemPtr->name, Tcl_GetString(objv[i]))) {
                Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(elemPtr->name, -1));
                break;
            }
        }
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/**
 * @ingroup element_command_internals
 * @brief Query display order or replace it before returning the resulting list.
 *
 * A supplied list is processed by RebuildDisplayList; parse failure returns
 * immediately. Copies names from the display chain into a new Tcl result list.
 * Does not filter hideplot entries or consult registry iteration order.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int ShowOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;
    Rbc_ChainLink *linkPtr;
    Tcl_Obj *resultObj;

    if (objc == 4) {
        if (RebuildDisplayList(graphPtr, objv[3]) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    resultObj = Tcl_NewListObj(0, NULL);
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(elemPtr->name, -1));
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/**
 * @ingroup element_command_internals
 * @brief Return the registered element's concrete class UID as a Tcl string.
 *
 * Returns TCL_ERROR for an unknown name. The dispatcher type argument does not
 * filter lookup; the result describes the actual element record.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 */
static int TypeOp(Graph *graphPtr, Tcl_Interp *interp, Rbc_Uid type, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Element *elemPtr;

    if (NameToElement(graphPtr, objv[3], &elemPtr) != TCL_OK) {
        return TCL_ERROR; /* Can't find named element */
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(elemPtr->classUid, -1));
    return TCL_OK;
}

/*
 * Global routines:
 */
static const ElementOpSpec elemOps[] = {
    {{"activate", 3, 0, "?elemName? ?index...?"}, ActivateOp},
    {{"bind", 3, 6, "elemName sequence command"}, BindOp},
    {{"cget", 5, 5, "elemName option"}, CgetOp},
    {{"closest", 6, 0, "x y varName ?option value?... ?--? ?elemName?..."}, ClosestOp},
    {{"configure", 4, 0, "elemName ?elemName?... ?option value?..."}, ConfigureOp},
    {{"create", 4, 0, "elemName ?option value?..."}, CreateOp},
    {{"deactivate", 3, 0, "?elemName?..."}, DeactivateOp},
    {{"delete", 3, 0, "?elemName?..."}, DeleteOp},
    {{"exists", 4, 4, "elemName"}, ExistsOp},
    {{"get", 4, 4, "name"}, GetOp},
    {{"names", 3, 0, "?pattern?..."}, NamesOp},
    {{"show", 3, 4, "?elemList?"}, ShowOp},
    {{"type", 4, 4, "elemName"}, TypeOp},
    {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup element_command_internals
 * @brief Resolve and dispatch an element subcommand with arity validation.
 *
 * Uses the typed elemOps table and argument-two operation lookup. Returns lookup
 * errors without invoking a handler; otherwise forwards graph, interpreter,
 * complete argument vector and creation class UID to that handler.
 * Does not retain graph/element records or provide a transaction across commands.
 *
 * @param[in,out] graphPtr Borrowed owning graph; required live-window and mapped-state conditions are described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed command arguments at the positions described above; see @ref rbc_command_arguments.
 * @param[in] type Dispatcher-supplied creation class; unused by operations that only query or modify existing
 *   elements.
 */
int Rbc_ElementOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid type) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, elemOps, (Tcl_Size)sizeof(elemOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    return elemOps[index].proc(graphPtr, interp, type, objc, objv);
}
