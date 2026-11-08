/**
 * @file rbcGrGrid.c
 * @brief Grid configuration and output helpers.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup grid_internals Grid configuration and output helpers
 * Private component helpers; not installed C API entry points.
 */
#include "rbcGraph.h"
#include "rbcGrTrack.h"
#include "rbcRender.h"

#define GRID_REDRAW (1U << 0)
#define GRID_GC_CHANGED (1U << 1)
#define GRID_AXES_CHANGED (1U << 2)
#define GRID_POLAR_CHANGED (1U << 3)
#define GRID_INITIALIZE_MASK (GRID_GC_CHANGED | GRID_AXES_CHANGED | GRID_POLAR_CHANGED)

/**
 * @brief Owned staging buffers for the two angular tick options.
 * @ingroup grid_internals
 *
 * Zero-initialize before staging. A staged flag distinguishes an explicitly
 * empty replacement from an unchanged slot. Commit transfers array ownership
 * and clears each staged slot; cleanup releases only remaining owned arrays.
 * The record must not be copied as an independent owner while buffers exist.
 */
typedef struct {
    int majorStaged; /**< Nonzero when the corresponding tick array has a staged replacement. */
    double *majorTicks; /**< Owned staged tick values; transferred on commit or freed on abort. */
    Tcl_Size nMajorTicks; /**< Number of values in the staged tick array. */
    int minorStaged; /**< Nonzero when the corresponding tick array has a staged replacement. */
    double *minorTicks; /**< Owned staged tick values; transferred on commit or freed on abort. */
    Tcl_Size nMinorTicks; /**< Number of values in the staged tick array. */
} GridPolarAngleTicksTransaction;

/**
 * @brief Owned staging buffers for four independent Smith tick options.
 * @ingroup grid_internals
 *
 * Real/imaginary and major/minor slots each have a staged flag, owned array and
 * count. NULL with a true flag means clear that active list. Prepare initializes
 * the complete record; commit detaches ownership; cleanup resets all fields.
 * This storage is separate from Tk_SavedOptions and owns no Tcl objects.
 */
typedef struct {
    int realMajorStaged; /**< Nonzero when the corresponding tick array has a staged replacement. */
    double *realMajorTicks; /**< Owned staged tick values; transferred on commit or freed on abort. */
    Tcl_Size nRealMajorTicks; /**< Number of values in the staged tick array. */
    int realMinorStaged; /**< Nonzero when the corresponding tick array has a staged replacement. */
    double *realMinorTicks; /**< Owned staged tick values; transferred on commit or freed on abort. */
    Tcl_Size nRealMinorTicks; /**< Number of values in the staged tick array. */
    int imagMajorStaged; /**< Nonzero when the corresponding tick array has a staged replacement. */
    double *imagMajorTicks; /**< Owned staged tick values; transferred on commit or freed on abort. */
    Tcl_Size nImagMajorTicks; /**< Number of values in the staged tick array. */
    int imagMinorStaged; /**< Nonzero when the corresponding tick array has a staged replacement. */
    double *imagMinorTicks; /**< Owned staged tick values; transferred on commit or freed on abort. */
    Tcl_Size nImagMinorTicks; /**< Number of values in the staged tick array. */
} GridSmithTicksTransaction;

static int SetPolarLabelAnchor(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **valuePtrPtr,
                               char *widgRec, Tcl_Size offset, char *saveInternalPtr, int flags);
static Tcl_Obj *GetPolarLabelAnchor(void *clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset);
static void RestorePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr, char *saveInternalPtr);
static void FreePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr);
static const Tk_ObjCustomOption polarLabelAnchorOption = {
    "polarLabelAnchor", SetPolarLabelAnchor, GetPolarLabelAnchor, RestorePolarLabelAnchor, FreePolarLabelAnchor, NULL};
static const char *const smithGridNames[] = {"impedance", "admittance", "both", NULL};

/** @ingroup component_option_tables
 * @brief Graph grid settings, including Cartesian and polar presentation fields.
 * @hideinitializer
 */
static const Tk_OptionSpec graphGridOptionSpecs[] = {
    {TK_OPTION_CUSTOM, "-anglelabelanchor", "angleLabelAnchor", "AngleLabelAnchor", "center", -1,
     offsetof(Grid, angleLabelAnchor), 0, &polarLabelAnchorOption, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-anglecommand", "angleCommand", "AngleCommand", NULL, offsetof(Grid, angleCommandObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-anglemajorticks", "angleMajorTicks", "AngleMajorTicks",
     "0 30 60 90 120 150 180 210 240 270 300 330", offsetof(Grid, angleMajorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-angleminorticks", "angleMinorTicks", "AngleMinorTicks",
     "15 45 75 105 135 165 195 225 255 285 315 345", offsetof(Grid, angleMinorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_CUSTOM, "-radiallabelanchor", "radialLabelAnchor", "RadialLabelAnchor", "se", -1,
     offsetof(Grid, radialLabelAnchor), 0, &polarLabelAnchorOption, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING_TABLE, "-smithgrid", "smithGrid", "SmithGrid", "impedance", -1, offsetof(Grid, smithGrid), 0,
     (ClientData)smithGridNames, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealcommand", "smithRealCommand", "SmithRealCommand", NULL,
     offsetof(Grid, smithRealCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagcommand", "smithImagCommand", "SmithImagCommand", NULL,
     offsetof(Grid, smithImagCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealmajorticks", "smithRealMajorTicks", "SmithRealMajorTicks", "0 0.2 0.5 1 2 5",
     offsetof(Grid, smithRealMajorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealminorticks", "smithRealMinorTicks", "SmithRealMinorTicks", "0.1 0.3 0.7 1.5 3 10",
     offsetof(Grid, smithRealMinorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagmajorticks", "smithImagMajorTicks", "SmithImagMajorTicks", "0.2 0.5 1 2 5",
     offsetof(Grid, smithImagMajorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagminorticks", "smithImagMinorTicks", "SmithImagMinorTicks", "0.1 0.3 0.7 1.5 3 10",
     offsetof(Grid, smithImagMinorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},

    {TK_OPTION_COLOR, "-color", "color", "Color", RGB_GREY64, -1, offsetof(Grid, colorPtr), 0, RGB_BLACK,
     GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", "dot", offsetof(Grid, dashesObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", "yes", -1, offsetof(Grid, hidden), 0, NULL, GRID_REDRAW},
    {TK_OPTION_PIXELS, "-linewidth", "lineWidth", "Linewidth", "0", offsetof(Grid, lineWidthObjPtr), -1, 0, NULL,
     GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", "x", offsetof(Grid, mapXObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", "y", offsetof(Grid, mapYObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_BOOLEAN, "-minor", "minor", "Minor", "yes", -1, offsetof(Grid, minorGrid), 0, NULL, GRID_REDRAW},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/* Track grids use the Cartesian options only. */
/** @ingroup component_option_tables
 * @brief Cartesian options for an individual track grid; lookup uses the track view, not the global grid table.
 * @hideinitializer
 */
static const Tk_OptionSpec trackGridOptionSpecs[] = {
    {TK_OPTION_COLOR, "-color", "color", "Color", RGB_GREY64, -1, offsetof(Grid, colorPtr), 0, RGB_BLACK,
     GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", "dot", offsetof(Grid, dashesObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", "yes", -1, offsetof(Grid, hidden), 0, NULL, GRID_REDRAW},
    {TK_OPTION_PIXELS, "-linewidth", "lineWidth", "Linewidth", "0", offsetof(Grid, lineWidthObjPtr), -1, 0, NULL,
     GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", "x", offsetof(Grid, mapXObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", "y", offsetof(Grid, mapYObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_BOOLEAN, "-minor", "minor", "Minor", "yes", -1, offsetof(Grid, minorGrid), 0, NULL, GRID_REDRAW},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/* Polar grids share graph options, but are visible by default. */
/** @ingroup component_option_tables
 * @brief Polar grid visibility override; the end entry chains to graphGridOptionSpecs for the remaining options.
 * @hideinitializer
 */
static const Tk_OptionSpec polarGridOptionSpecs[] = {
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", "no", -1, offsetof(Grid, hidden), 0, NULL, GRID_REDRAW},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, (ClientData)graphGridOptionSpecs, 0}};

/** @ingroup component_option_tables
 * @brief Options selected for a bar graph grid; use the table selected at construction when querying defaults.
 * @hideinitializer
 */
static const Tk_OptionSpec barGridOptionSpecs[] = {
    {TK_OPTION_CUSTOM, "-anglelabelanchor", "angleLabelAnchor", "AngleLabelAnchor", "center", -1,
     offsetof(Grid, angleLabelAnchor), 0, &polarLabelAnchorOption, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-anglecommand", "angleCommand", "AngleCommand", NULL, offsetof(Grid, angleCommandObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-anglemajorticks", "angleMajorTicks", "AngleMajorTicks",
     "0 30 60 90 120 150 180 210 240 270 300 330", offsetof(Grid, angleMajorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-angleminorticks", "angleMinorTicks", "AngleMinorTicks",
     "15 45 75 105 135 165 195 225 255 285 315 345", offsetof(Grid, angleMinorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_CUSTOM, "-radiallabelanchor", "radialLabelAnchor", "RadialLabelAnchor", "se", -1,
     offsetof(Grid, radialLabelAnchor), 0, &polarLabelAnchorOption, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING_TABLE, "-smithgrid", "smithGrid", "SmithGrid", "impedance", -1, offsetof(Grid, smithGrid), 0,
     (ClientData)smithGridNames, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealcommand", "smithRealCommand", "SmithRealCommand", NULL,
     offsetof(Grid, smithRealCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagcommand", "smithImagCommand", "SmithImagCommand", NULL,
     offsetof(Grid, smithImagCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealmajorticks", "smithRealMajorTicks", "SmithRealMajorTicks", "0 0.2 0.5 1 2 5",
     offsetof(Grid, smithRealMajorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealminorticks", "smithRealMinorTicks", "SmithRealMinorTicks", "0.1 0.3 0.7 1.5 3 10",
     offsetof(Grid, smithRealMinorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagmajorticks", "smithImagMajorTicks", "SmithImagMajorTicks", "0.2 0.5 1 2 5",
     offsetof(Grid, smithImagMajorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagminorticks", "smithImagMinorTicks", "SmithImagMinorTicks", "0.1 0.3 0.7 1.5 3 10",
     offsetof(Grid, smithImagMinorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},

    {TK_OPTION_COLOR, "-color", "color", "Color", RGB_GREY64, -1, offsetof(Grid, colorPtr), 0, RGB_BLACK,
     GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", "dot", offsetof(Grid, dashesObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", "no", -1, offsetof(Grid, hidden), 0, NULL, GRID_REDRAW},
    {TK_OPTION_PIXELS, "-linewidth", "lineWidth", "Linewidth", "0", offsetof(Grid, lineWidthObjPtr), -1, 0, NULL,
     GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", NULL, offsetof(Grid, mapXObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", "y", offsetof(Grid, mapYObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_BOOLEAN, "-minor", "minor", "Minor", "yes", -1, offsetof(Grid, minorGrid), 0, NULL, GRID_REDRAW},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/**
 * @brief Match a canonical grid option after abbreviation resolution.
 * @ingroup grid_internals
 *
 * Borrows objPtr and compares its canonical name from graphGridOptionSpecs with
 * optionName. Returns FALSE when resolution fails; does not set an interpreter
 * diagnostic. Used to identify explicit tick options in the original argument
 * sequence, rather than inspecting only the final Tk option values.
 *
 * @param[in] objPtr Borrowed option-name object, possibly abbreviated.
 * @param[in] optionName Borrowed NUL-terminated canonical spelling to compare after resolution.
 */
static int IsGridOption(Tcl_Obj *objPtr, const char *optionName) {
    const char *name = Rbc_GetCanonicalOptionName(objPtr, graphGridOptionSpecs);

    return ((name != NULL) && (strcmp(name, optionName) == 0));
}

/**
 * @brief Parse the custom automatic-or-Tk anchor option.
 * @ingroup grid_internals
 *
 * Tk supplies the target record/offset and optional rollback storage. Saves the
 * old value when requested, then accepts the exact word auto or a Tk anchor.
 * Auto stores CENTER with isAuto true. Assigns the active record only on success;
 * returns TCL_ERROR with the Tk diagnostic on invalid input. No owned allocation
 * or Tcl callback is involved; the input object is not replaced.
 *
 * @param[in] clientData Unused Tk custom-option client data.
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] tkwin Unused Tk custom-option window argument.
 * @param[in] valuePtrPtr Required pointer to the borrowed candidate object; the object pointer is not
 *    replaced.
 * @param[in,out] widgRec Tk option record containing the inline PolarLabelAnchor.
 * @param[in] offset Byte offset of PolarLabelAnchor within widgRec.
 * @param[out] saveInternalPtr Optional storage for the previous inline value; NULL skips saving.
 * @param[in] flags Unused Tk custom-option flags.
 */
static int SetPolarLabelAnchor(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **valuePtrPtr,
                               char *widgRec, Tcl_Size offset, char *saveInternalPtr, int flags) {
    PolarLabelAnchor *anchorPtr;
    PolarLabelAnchor newAnchor;
    const char *string;

    (void)clientData;
    (void)tkwin;
    (void)flags;
    anchorPtr = (PolarLabelAnchor *)(widgRec + offset);
    /*
     * Tk needs the old internal representation for transactional
     * rollback.
     */
    if (saveInternalPtr != NULL) {
        *((PolarLabelAnchor *)saveInternalPtr) = *anchorPtr;
    }
    string = Tcl_GetString(*valuePtrPtr);
    if (strcmp(string, "auto") == 0) {
        newAnchor.anchor = TK_ANCHOR_CENTER;
        newAnchor.isAuto = TRUE;
    } else {
        if (Tk_GetAnchorFromObj(interp, *valuePtrPtr, &newAnchor.anchor) != TCL_OK) {
            return TCL_ERROR;
        }
        newAnchor.isAuto = FALSE;
    }
    *anchorPtr = newAnchor;
    return TCL_OK;
}

/**
 * @brief Return the external value of a custom label anchor.
 * @ingroup grid_internals
 *
 * Reads the record at widgRec+offset. Returns a new zero-reference Tcl string
 * containing auto or the Tk anchor name; ownership follows Tk's custom-option
 * getProc convention. Does not change the record or allocate native resources.
 *
 * @param[in] clientData Unused Tk custom-option client data.
 * @param[in] tkwin Unused Tk custom-option window argument.
 * @param[in] widgRec Borrowed record containing a valid inline PolarLabelAnchor.
 * @param[in] offset Byte offset of that representation within widgRec.
 */
static Tcl_Obj *GetPolarLabelAnchor(void *clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset) {
    PolarLabelAnchor *anchorPtr;

    (void)clientData;
    (void)tkwin;
    anchorPtr = (PolarLabelAnchor *)(widgRec + offset);
    if (anchorPtr->isAuto) {
        return Tcl_NewStringObj("auto", -1);
    }
    return Tcl_NewStringObj(Tk_NameOfAnchor(anchorPtr->anchor), -1);
}

/**
 * @brief Restore the saved inline anchor representation.
 * @ingroup grid_internals
 *
 * Copies one PolarLabelAnchor from Tk rollback storage to internalPtr. Both
 * pointers must refer to complete records. No pointer ownership transfer or
 * allocation is necessary because the representation contains only values.
 *
 * @param[in] clientData Unused Tk custom-option client data.
 * @param[in] tkwin Unused Tk custom-option window argument.
 * @param[out] internalPtr Required storage for the active PolarLabelAnchor to restore.
 * @param[in] saveInternalPtr Required saved PolarLabelAnchor; copied without taking ownership.
 */
static void RestorePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr, char *saveInternalPtr) {
    (void)clientData;
    (void)tkwin;

    *((PolarLabelAnchor *)internalPtr) = *((PolarLabelAnchor *)saveInternalPtr);
}

/**
 * @brief Complete Tk custom-option cleanup for an inline value.
 * @ingroup grid_internals
 *
 * Intentionally a no-op: PolarLabelAnchor owns no heap or Tk resources. Tk owns
 * the surrounding record/storage. All arguments are unused.
 *
 * @param[in] clientData Unused Tk custom-option client data.
 * @param[in] tkwin Unused Tk custom-option window argument.
 * @param[in] internalPtr Unused pointer to Tk-owned inline representation; no allocation is released.
 */
static void FreePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr) {
    (void)clientData;
    (void)tkwin;
    (void)internalPtr;
}

/**
 * @brief Parse a finite angular tick list into an owned double array.
 * @ingroup grid_internals
 *
 * Resets output pointer/count to NULL/zero before parsing. Accepts values in
 * [0,360) degrees, canonicalizes negative zero, and preserves order and duplicates.
 * Empty lists succeed without allocation. Checks allocation size and uses
 * Tcl_AttemptAlloc; frees scratch storage on conversion/range failure.
 * Returns TCL_OK with caller-owned storage (release with ckfree), or TCL_ERROR
 * with an interpreter diagnostic and NULL/zero outputs. Does not change a grid.
 *
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed list object containing angles in degrees.
 * @param[in] optionName Borrowed option spelling used in range and allocation-size diagnostics.
 * @param[out] ticksPtr Required output slot; receives ckfree-owned array on success, NULL for empty
 *    input or failure.
 * @param[out] nTicksPtr Required output count; zero for empty input or failure.
 */
static int ParsePolarAngleTicks(Tcl_Interp *interp, Tcl_Obj *objPtr, const char *optionName, double **ticksPtr,
                                Tcl_Size *nTicksPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_Size i;
    double *ticks;
    size_t bytes;

    *ticksPtr = NULL;
    *nTicksPtr = 0;
    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc == 0) {
        return TCL_OK;
    }
    if ((Tcl_WideUInt)objc > (Tcl_WideUInt)(SIZE_MAX / sizeof(double))) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s contains too many angles", optionName));
        return TCL_ERROR;
    }
    bytes = (size_t)objc * sizeof(double);
    ticks = Tcl_AttemptAlloc(bytes);
    if (ticks == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate polar angular tick array", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i++) {
        double value;

        if (Tcl_GetDoubleFromObj(interp, objv[i], &value) != TCL_OK) {
            ckfree(ticks);
            return TCL_ERROR;
        }
        if ((!FINITE(value)) || (value < 0.0) || (value >= 360.0)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s angle \"%s\" must be finite "
                                                   "and in the range 0 <= angle < 360",
                                                   optionName, Tcl_GetString(objv[i])));
            ckfree(ticks);
            return TCL_ERROR;
        }
        /*
         * Canonicalize negative zero.
         */
        if (value == 0.0) {
            value = 0.0;
        }
        ticks[i] = value;
    }
    *ticksPtr = ticks;
    *nTicksPtr = objc;
    return TCL_OK;
}

/**
 * @brief Parse nonnegative or positive Smith tick magnitudes.
 * @ingroup grid_internals
 *
 * Resets output pointer/count before parsing. allowZero selects nonnegative
 * values for real ticks; otherwise all values must be strictly positive.
 * Rejects non-finite values, preserves input order/duplicates, and normalizes
 * negative zero when accepted. Empty lists return NULL/zero with TCL_OK.
 * Checked Tcl allocation is owned by the caller on success; on TCL_ERROR the
 * scratch buffer is freed, outputs remain NULL/zero, and interp has diagnostics.
 *
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed list of real values or imaginary magnitudes.
 * @param[in] optionName Borrowed option spelling for diagnostics.
 * @param[in] allowZero Nonzero accepts zero; zero requires strictly positive magnitudes.
 * @param[out] ticksPtr Required output slot; receives ckfree-owned array or NULL for empty input or
 *    failure.
 * @param[out] nTicksPtr Required output count; zero for empty input or failure.
 */
static int ParseSmithTicks(Tcl_Interp *interp, Tcl_Obj *objPtr, const char *optionName, int allowZero,
                           double **ticksPtr, Tcl_Size *nTicksPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_Size i;
    double *ticks;
    size_t bytes;

    *ticksPtr = NULL;
    *nTicksPtr = 0;
    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc == 0) {
        return TCL_OK;
    }
    if ((Tcl_WideUInt)objc > (Tcl_WideUInt)(SIZE_MAX / sizeof(double))) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s contains too many values", optionName));
        return TCL_ERROR;
    }
    bytes = (size_t)objc * sizeof(double);
    ticks = Tcl_AttemptAlloc(bytes);
    if (ticks == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate Smith tick array", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i++) {
        double value;

        if (Tcl_GetDoubleFromObj(interp, objv[i], &value) != TCL_OK) {
            ckfree(ticks);
            return TCL_ERROR;
        }
        if ((!FINITE(value)) || (allowZero ? (value < 0.0) : (value <= 0.0))) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf(allowZero ? "%s value \"%s\" must be finite and non-negative"
                                                             : "%s value \"%s\" must be finite and greater than zero",
                                                   optionName, Tcl_GetString(objv[i])));
            ckfree(ticks);
            return TCL_ERROR;
        }
        /*
         * Canonicalize negative zero.
         */
        if (value == 0.0) {
            value = 0.0;
        }
        ticks[i] = value;
    }
    *ticksPtr = ticks;
    *nTicksPtr = objc;
    return TCL_OK;
}

/**
 * @brief Replace one staged angular list after complete validation.
 * @ingroup grid_internals
 *
 * transactionPtr must be initialized. Parses a major or minor list using the
 * grid interpreter. Only after success frees that slot's earlier staged array,
 * then takes ownership of the replacement and marks the slot staged, including
 * an empty replacement. TCL_ERROR leaves earlier staged/live values intact;
 * TCL_OK changes only the transaction, never active grid arrays.
 *
 * @param[in] gridPtr Borrowed grid supplying its graph interpreter; active arrays are unchanged.
 * @param[in] objPtr Borrowed replacement list.
 * @param[in] major Nonzero selects the major slot; zero selects the minor slot.
 * @param[in,out] transactionPtr Initialized staging record taking ownership only after the replacement
 *    parses successfully.
 */
static int StageGridPolarAngleTicks(Grid *gridPtr, Tcl_Obj *objPtr, int major,
                                     GridPolarAngleTicksTransaction *transactionPtr) {
    double *ticks;
    Tcl_Size nTicks;
    const char *optionName;

    ticks = NULL;
    nTicks = 0;
    optionName = major ? "-anglemajorticks" : "-angleminorticks";
    if (ParsePolarAngleTicks(gridPtr->graphPtr->interp, objPtr, optionName, &ticks, &nTicks) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Only release an earlier staged value after the new
     * value has parsed successfully.
     */
    if (major) {
        if (transactionPtr->majorStaged && (transactionPtr->majorTicks != NULL)) {
            ckfree(transactionPtr->majorTicks);
        }
        transactionPtr->majorTicks = ticks;
        transactionPtr->nMajorTicks = nTicks;
        transactionPtr->majorStaged = TRUE;
    } else {
        if (transactionPtr->minorStaged && (transactionPtr->minorTicks != NULL)) {
            ckfree(transactionPtr->minorTicks);
        }
        transactionPtr->minorTicks = ticks;
        transactionPtr->nMinorTicks = nTicks;
        transactionPtr->minorStaged = TRUE;
    }
    return TCL_OK;
}

/**
 * @brief Replace one of four staged Smith tick lists.
 * @ingroup grid_internals
 *
 * transactionPtr must be initialized. realPart chooses real versus imaginary
 * magnitudes (and whether zero is allowed); major chooses the tier. Completely
 * parses the replacement before freeing earlier staged storage for that slot.
 * Returns Tcl status; failure preserves earlier staged/live values, success
 * transfers ownership to the transaction and marks even an empty list staged.
 *
 * @param[in] gridPtr Borrowed grid supplying its graph interpreter; active arrays are unchanged.
 * @param[in] objPtr Borrowed replacement list.
 * @param[in] realPart Nonzero selects real ticks allowing zero; zero selects positive imaginary
 *    magnitudes.
 * @param[in] major Nonzero selects major ticks; zero selects minor ticks.
 * @param[in,out] transactionPtr Initialized transaction owning any previous staged arrays; only the
 *    selected slot is replaced.
 */
static int StageGridSmithTicks(Grid *gridPtr, Tcl_Obj *objPtr, int realPart, int major,
                                GridSmithTicksTransaction *transactionPtr) {
    double *ticks;
    Tcl_Size nTicks;
    const char *optionName;
    int allowZero;

    ticks = NULL;
    nTicks = 0;
    allowZero = realPart;
    if (realPart) {
        optionName = major ? "-smithrealmajorticks" : "-smithrealminorticks";
    } else {
        optionName = major ? "-smithimagmajorticks" : "-smithimagminorticks";
    }
    if (ParseSmithTicks(gridPtr->graphPtr->interp, objPtr, optionName, allowZero, &ticks, &nTicks) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Only release an earlier staged value after the new
     * value has parsed successfully.
     */
    if (realPart) {
        if (major) {
            if (transactionPtr->realMajorStaged && (transactionPtr->realMajorTicks != NULL)) {
                ckfree(transactionPtr->realMajorTicks);
            }
            transactionPtr->realMajorTicks = ticks;
            transactionPtr->nRealMajorTicks = nTicks;
            transactionPtr->realMajorStaged = TRUE;
        } else {
            if (transactionPtr->realMinorStaged && (transactionPtr->realMinorTicks != NULL)) {
                ckfree(transactionPtr->realMinorTicks);
            }
            transactionPtr->realMinorTicks = ticks;
            transactionPtr->nRealMinorTicks = nTicks;
            transactionPtr->realMinorStaged = TRUE;
        }
    } else {
        if (major) {
            if (transactionPtr->imagMajorStaged && (transactionPtr->imagMajorTicks != NULL)) {
                ckfree(transactionPtr->imagMajorTicks);
            }
            transactionPtr->imagMajorTicks = ticks;
            transactionPtr->nImagMajorTicks = nTicks;
            transactionPtr->imagMajorStaged = TRUE;
        } else {
            if (transactionPtr->imagMinorStaged && (transactionPtr->imagMinorTicks != NULL)) {
                ckfree(transactionPtr->imagMinorTicks);
            }
            transactionPtr->imagMinorTicks = ticks;
            transactionPtr->nImagMinorTicks = nTicks;
            transactionPtr->imagMinorStaged = TRUE;
        }
    }
    return TCL_OK;
}

/**
 * @brief Discard owned angular staging storage and reset the transaction.
 * @ingroup grid_internals
 *
 * Requires a non-NULL initialized transaction containing only owned arrays or
 * NULLs. Frees both non-NULL arrays regardless of flags, then zeroes the record.
 * Safe after commit and safe to repeat on the reset record. Never frees active
 * grid storage because commit detaches transferred pointers.
 *
 * @param[in,out] transactionPtr Required initialized transaction; owned arrays are freed and all fields
 *    zeroed.
 */
static void FreeGridPolarAngleTicksTransaction(GridPolarAngleTicksTransaction *transactionPtr) {
    if (transactionPtr->majorTicks != NULL) {
        ckfree(transactionPtr->majorTicks);
    }
    if (transactionPtr->minorTicks != NULL) {
        ckfree(transactionPtr->minorTicks);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @brief Discard all owned Smith staging arrays and reset the record.
 * @ingroup grid_internals
 *
 * Requires a non-NULL initialized transaction. Frees each non-NULL array and
 * zeroes all pointers, counts and flags. Safe after partial preparation, successful
 * commit or previous cleanup. No grid state or Tcl result is changed.
 *
 * @param[in,out] transactionPtr Required initialized transaction; owned arrays are freed and all fields
 *    zeroed.
 */
static void FreeGridSmithTicksTransaction(GridSmithTicksTransaction *transactionPtr) {
    if (transactionPtr->realMajorTicks != NULL) {
        ckfree(transactionPtr->realMajorTicks);
    }
    if (transactionPtr->realMinorTicks != NULL) {
        ckfree(transactionPtr->realMinorTicks);
    }
    if (transactionPtr->imagMajorTicks != NULL) {
        ckfree(transactionPtr->imagMajorTicks);
    }
    if (transactionPtr->imagMinorTicks != NULL) {
        ckfree(transactionPtr->imagMinorTicks);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @brief Stage defaults and every explicit angular option in caller order.
 * @ingroup grid_internals
 *
 * Requires an even optionObjc and a valid borrowed optionObjv array in gridPtr.
 * Zero-initializes transactionPtr, so it must not already own allocations.
 * On first configuration, stages stored option/default values only for slots
 * not explicitly supplied. Then parses each explicit occurrence in argument order:
 * an invalid earlier duplicate is not hidden by a later valid value.
 *
 * Returns TCL_OK with owned staged buffers, or TCL_ERROR after freeing/resetting
 * the transaction. Live grid arrays are unchanged; Tk's saved options are a
 * separate transaction handled by the caller.
 *
 * @param[in] gridPtr Borrowed grid with an even optionObjc and valid optionObjv slice, plus stored
 *    defaults.
 * @param[out] transactionPtr Required unowned storage; initialized here, owns staged arrays on success
 *    and is reset on error.
 */
static int PrepareGridPolarAngleTicksTransaction(Grid *gridPtr, GridPolarAngleTicksTransaction *transactionPtr) {
    int explicitMajor;
    int explicitMinor;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitMajor = FALSE;
    explicitMinor = FALSE;
    assert((gridPtr->optionObjc & 1) == 0);
    /*
     * Determine whether the caller explicitly supplied either
     * option.  On initial configuration, option-database/default
     * values must also be staged.
     */
    for (i = 0; i < gridPtr->optionObjc; i += 2) {
        if (IsGridOption(gridPtr->optionObjv[i], "-anglemajorticks")) {
            explicitMajor = TRUE;
        } else if (IsGridOption(gridPtr->optionObjv[i], "-angleminorticks")) {
            explicitMinor = TRUE;
        }
    }
    if (!gridPtr->optionsConfigured) {
        if ((!explicitMajor) && (gridPtr->angleMajorTicksObjPtr != NULL)) {
            if (StageGridPolarAngleTicks(gridPtr, gridPtr->angleMajorTicksObjPtr, TRUE, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
        if ((!explicitMinor) && (gridPtr->angleMinorTicksObjPtr != NULL)) {
            if (StageGridPolarAngleTicks(gridPtr, gridPtr->angleMinorTicksObjPtr, FALSE, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }
    /*
     * Process explicit values in caller order so that an invalid
     * earlier duplicate is not hidden by a later valid value.
     */
    for (i = 0; i < gridPtr->optionObjc; i += 2) {
        if (IsGridOption(gridPtr->optionObjv[i], "-anglemajorticks")) {
            if (StageGridPolarAngleTicks(gridPtr, gridPtr->optionObjv[i + 1], TRUE, transactionPtr) != TCL_OK) {
                goto error;
            }
        } else if (IsGridOption(gridPtr->optionObjv[i], "-angleminorticks")) {
            if (StageGridPolarAngleTicks(gridPtr, gridPtr->optionObjv[i + 1], FALSE, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }
    return TCL_OK;

error:
    FreeGridPolarAngleTicksTransaction(transactionPtr);
    return TCL_ERROR;
}

/**
 * @brief Stage defaults and explicit values for all four Smith lists.
 * @ingroup grid_internals
 *
 * Requires even optionObjc, valid borrowed optionObjv, and an empty/unowned output
 * transaction. Initializes the record, stages non-overridden defaults only on
 * initial configuration, then validates every explicit occurrence in caller order.
 * Returns TCL_OK with staged ownership, or TCL_ERROR after complete staging
 * cleanup. Does not replace active arrays or restore Tk option objects.
 *
 * @param[in] gridPtr Borrowed grid with an even optionObjc and valid optionObjv slice, plus stored
 *    defaults.
 * @param[out] transactionPtr Required unowned storage; initialized here, owns staged arrays on success
 *    and is reset on error.
 */
static int PrepareGridSmithTicksTransaction(Grid *gridPtr, GridSmithTicksTransaction *transactionPtr) {
    int explicitRealMajor;
    int explicitRealMinor;
    int explicitImagMajor;
    int explicitImagMinor;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitRealMajor = FALSE;
    explicitRealMinor = FALSE;
    explicitImagMajor = FALSE;
    explicitImagMinor = FALSE;
    assert((gridPtr->optionObjc & 1) == 0);
    /*
     * Determine whether the caller explicitly supplied any of the
     * Smith tick options.  On initial configuration,
     * option-database/default values must also be staged.
     */
    for (i = 0; i < gridPtr->optionObjc; i += 2) {
        if (IsGridOption(gridPtr->optionObjv[i], "-smithrealmajorticks")) {
            explicitRealMajor = TRUE;
        } else if (IsGridOption(gridPtr->optionObjv[i], "-smithrealminorticks")) {
            explicitRealMinor = TRUE;
        } else if (IsGridOption(gridPtr->optionObjv[i], "-smithimagmajorticks")) {
            explicitImagMajor = TRUE;
        } else if (IsGridOption(gridPtr->optionObjv[i], "-smithimagminorticks")) {
            explicitImagMinor = TRUE;
        }
    }
    if (!gridPtr->optionsConfigured) {
        if ((!explicitRealMajor) && (gridPtr->smithRealMajorTicksObjPtr != NULL)) {
            if (StageGridSmithTicks(gridPtr, gridPtr->smithRealMajorTicksObjPtr, TRUE, TRUE, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
        if ((!explicitRealMinor) && (gridPtr->smithRealMinorTicksObjPtr != NULL)) {
            if (StageGridSmithTicks(gridPtr, gridPtr->smithRealMinorTicksObjPtr, TRUE, FALSE, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
        if ((!explicitImagMajor) && (gridPtr->smithImagMajorTicksObjPtr != NULL)) {
            if (StageGridSmithTicks(gridPtr, gridPtr->smithImagMajorTicksObjPtr, FALSE, TRUE, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
        if ((!explicitImagMinor) && (gridPtr->smithImagMinorTicksObjPtr != NULL)) {
            if (StageGridSmithTicks(gridPtr, gridPtr->smithImagMinorTicksObjPtr, FALSE, FALSE, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
    }
    /*
     * Process explicit values in caller order so that an invalid
     * earlier duplicate is not hidden by a later valid value.
     */
    for (i = 0; i < gridPtr->optionObjc; i += 2) {
        if (IsGridOption(gridPtr->optionObjv[i], "-smithrealmajorticks")) {
            if (StageGridSmithTicks(gridPtr, gridPtr->optionObjv[i + 1], TRUE, TRUE, transactionPtr) != TCL_OK) {
                goto error;
            }
        } else if (IsGridOption(gridPtr->optionObjv[i], "-smithrealminorticks")) {
            if (StageGridSmithTicks(gridPtr, gridPtr->optionObjv[i + 1], TRUE, FALSE, transactionPtr) != TCL_OK) {
                goto error;
            }
        } else if (IsGridOption(gridPtr->optionObjv[i], "-smithimagmajorticks")) {
            if (StageGridSmithTicks(gridPtr, gridPtr->optionObjv[i + 1], FALSE, TRUE, transactionPtr) != TCL_OK) {
                goto error;
            }
        } else if (IsGridOption(gridPtr->optionObjv[i], "-smithimagminorticks")) {
            if (StageGridSmithTicks(gridPtr, gridPtr->optionObjv[i + 1], FALSE, FALSE, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }
    return TCL_OK;

error:
    FreeGridSmithTicksTransaction(transactionPtr);
    return TCL_ERROR;
}

/**
 * @brief Transfer staged angular arrays into active grid ownership.
 * @ingroup grid_internals
 *
 * For each staged slot, replaces pointer/count, clears its staging fields and
 * frees the old active array. Unstaged slots are retained; staged NULL/zero means
 * an explicitly empty list. Requires successful preparation. No allocation,
 * validation, callback or redraw scheduling occurs; cleanup may run afterward.
 *
 * @param[in,out] gridPtr Owning grid receiving staged arrays and releasing replaced active arrays.
 * @param[in,out] transactionPtr Successfully prepared transaction; transferred pointers and slot flags
 *    are cleared.
 */
static void CommitGridPolarAngleTicksTransaction(Grid *gridPtr, GridPolarAngleTicksTransaction *transactionPtr) {
    if (transactionPtr->majorStaged) {
        double *oldTicks;

        oldTicks = gridPtr->angleMajorTicks;
        gridPtr->angleMajorTicks = transactionPtr->majorTicks;
        gridPtr->nAngleMajorTicks = transactionPtr->nMajorTicks;
        transactionPtr->majorTicks = NULL;
        transactionPtr->nMajorTicks = 0;
        transactionPtr->majorStaged = FALSE;
        if (oldTicks != NULL) {
            ckfree(oldTicks);
        }
    }
    if (transactionPtr->minorStaged) {
        double *oldTicks;

        oldTicks = gridPtr->angleMinorTicks;
        gridPtr->angleMinorTicks = transactionPtr->minorTicks;
        gridPtr->nAngleMinorTicks = transactionPtr->nMinorTicks;
        transactionPtr->minorTicks = NULL;
        transactionPtr->nMinorTicks = 0;
        transactionPtr->minorStaged = FALSE;
        if (oldTicks != NULL) {
            ckfree(oldTicks);
        }
    }
}

/**
 * @brief Transfer staged Smith arrays into active grid ownership.
 * @ingroup grid_internals
 *
 * Commits each staged real/imaginary major/minor slot independently, detaches
 * its pointer/count/flag and frees old active storage. Empty staged lists clear
 * the slot; unstaged slots remain unchanged. Requires successful preparation;
 * no allocation, callbacks or redraw scheduling occur.
 *
 * @param[in,out] gridPtr Owning grid receiving staged arrays and releasing replaced active arrays.
 * @param[in,out] transactionPtr Successfully prepared transaction; transferred pointers and slot flags
 *    are cleared.
 */
static void CommitGridSmithTicksTransaction(Grid *gridPtr, GridSmithTicksTransaction *transactionPtr) {
    if (transactionPtr->realMajorStaged) {
        double *oldTicks;

        oldTicks = gridPtr->smithRealMajorTicks;
        gridPtr->smithRealMajorTicks = transactionPtr->realMajorTicks;
        gridPtr->nSmithRealMajorTicks = transactionPtr->nRealMajorTicks;
        transactionPtr->realMajorTicks = NULL;
        transactionPtr->nRealMajorTicks = 0;
        transactionPtr->realMajorStaged = FALSE;
        if (oldTicks != NULL) {
            ckfree(oldTicks);
        }
    }
    if (transactionPtr->realMinorStaged) {
        double *oldTicks;

        oldTicks = gridPtr->smithRealMinorTicks;
        gridPtr->smithRealMinorTicks = transactionPtr->realMinorTicks;
        gridPtr->nSmithRealMinorTicks = transactionPtr->nRealMinorTicks;
        transactionPtr->realMinorTicks = NULL;
        transactionPtr->nRealMinorTicks = 0;
        transactionPtr->realMinorStaged = FALSE;
        if (oldTicks != NULL) {
            ckfree(oldTicks);
        }
    }
    if (transactionPtr->imagMajorStaged) {
        double *oldTicks;

        oldTicks = gridPtr->smithImagMajorTicks;
        gridPtr->smithImagMajorTicks = transactionPtr->imagMajorTicks;
        gridPtr->nSmithImagMajorTicks = transactionPtr->nImagMajorTicks;
        transactionPtr->imagMajorTicks = NULL;
        transactionPtr->nImagMajorTicks = 0;
        transactionPtr->imagMajorStaged = FALSE;
        if (oldTicks != NULL) {
            ckfree(oldTicks);
        }
    }
    if (transactionPtr->imagMinorStaged) {
        double *oldTicks;

        oldTicks = gridPtr->smithImagMinorTicks;
        gridPtr->smithImagMinorTicks = transactionPtr->imagMinorTicks;
        gridPtr->nSmithImagMinorTicks = transactionPtr->nImagMinorTicks;
        transactionPtr->imagMinorTicks = NULL;
        transactionPtr->nImagMinorTicks = 0;
        transactionPtr->imagMinorStaged = FALSE;
        if (oldTicks != NULL) {
            ckfree(oldTicks);
        }
    }
}

/**
 * @brief Validate list syntax without evaluating a label command.
 * @ingroup grid_internals
 *
 * NULL means no command and succeeds. Otherwise uses Tcl_ListObjGetElements;
 * an empty list is accepted. Returns Tcl status and a diagnostic on invalid list
 * syntax. Does not check command existence, invoke the prefix or retain it.
 *
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed command-prefix object, or NULL for no prefix; list syntax only is checked.
 */
static int ValidateGridCommandPrefix(Tcl_Interp *interp, Tcl_Obj *objPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;

    if (objPtr == NULL) {
        return TCL_OK;
    }
    return Tcl_ListObjGetElements(interp, objPtr, &objc, &objv);
}

static int ConfigureGrid(Graph *graphPtr, Grid *gridPtr, int mask);

typedef int RbcGrGridOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

/** @brief Typed command dispatch entry with shared argument validation metadata. */
typedef struct {
    Rbc_OpSpecHeader header; /**< Operation name, total argument bounds and usage suffix. */
    RbcGrGridOp *proc; /**< Typed command handler for this operation. */
} GridOpSpec;

static RbcGrGridOp CgetOp;
static RbcGrGridOp ConfigureOp;
static RbcGrGridOp MapOp;
static RbcGrGridOp UnmapOp;
static RbcGrGridOp ToggleOp;

/**
 * @brief Prepare and commit derived grid resources after Tk option parsing.
 * @ingroup grid_internals
 *
 * Borrows graph/grid with a live Tk window and uses mask to select conversions.
 * Prepares dashes, width, acquired axis references and tick transactions before
 * replacing active derived state. Track-local mappings require a shared X axis
 * and a Y axis belonging to that track. Validates label-prefix list syntax without
 * evaluation. Creates a replacement private GC only after fallible parsing.
 *
 * On TCL_ERROR, frees temporary arrays/axis references and leaves active derived
 * resources intact; caller must restore Tk_SavedOptions separately. On TCL_OK,
 * commits arrays, axes and GC, releases replaced resources, and marks initial
 * configuration complete. Does not itself remap geometry or schedule redraw.
 *
 * @param[in] graphPtr Borrowed owning graph with live Tk configuration and axis lookup context.
 * @param[in,out] gridPtr Initialized owning grid with candidate Tk options and any borrowed original
 *    option slice.
 * @param[in] mask GRID_*_CHANGED bits selecting derived conversions; not a rollback or redraw request.
 */
static int ConfigureGrid(Graph *graphPtr, Grid *gridPtr, int mask) {
    GridPolarAngleTicksTransaction angleTicks = {0};
    GridSmithTicksTransaction smithTicks = {0};
    Rbc_Dashes newDashes;
    int newLineWidth;
    Axis *newXAxis;
    Axis *newYAxis;
    Axis *oldXAxis;
    Axis *oldYAxis;
    XGCValues gcValues;
    unsigned long gcMask;
    GC newGC;

    newDashes = gridPtr->dashes;
    newLineWidth = gridPtr->lineWidth;
    newXAxis = NULL;
    newYAxis = NULL;
    newGC = NULL;
    /*
     * Parse all values into temporary storage first. Nothing in the
     * active rendering state is modified until all validation succeeds.
     */
    if (mask & GRID_GC_CHANGED) {
        if (Rbc_GetDashesFromObj(graphPtr->interp, gridPtr->dashesObjPtr, &newDashes) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, gridPtr->lineWidthObjPtr, PIXELS_NONNEGATIVE,
                                 &newLineWidth) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (mask & GRID_AXES_CHANGED) {
        if (Rbc_GetAxisFromObj(graphPtr, gridPtr->mapXObjPtr, rbcXAxisUid, TRUE, &newXAxis) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_GetAxisFromObj(graphPtr, gridPtr->mapYObjPtr, rbcYAxisUid, TRUE, &newYAxis) != TCL_OK) {
            Rbc_FreeAxisReference(graphPtr, newXAxis);
            return TCL_ERROR;
        }
    }
    if ((mask & GRID_AXES_CHANGED) && gridPtr->track != NULL &&
        ((newXAxis != NULL && newXAxis->track != NULL) ||
         (newYAxis != NULL && newYAxis->track != gridPtr->track))) {
        Tcl_SetObjResult(graphPtr->interp,
                        Tcl_NewStringObj("grid axes must be a shared X axis and a Y axis owned by the track", -1));
        Rbc_FreeAxisReference(graphPtr, newXAxis);
        Rbc_FreeAxisReference(graphPtr, newYAxis);
        return TCL_ERROR;
    }
    if (mask & GRID_POLAR_CHANGED) {
        if (ValidateGridCommandPrefix(graphPtr->interp, gridPtr->angleCommandObjPtr) != TCL_OK ||
            ValidateGridCommandPrefix(graphPtr->interp, gridPtr->smithRealCommandObjPtr) != TCL_OK ||
            ValidateGridCommandPrefix(graphPtr->interp, gridPtr->smithImagCommandObjPtr) != TCL_OK ||
            PrepareGridPolarAngleTicksTransaction(gridPtr, &angleTicks) != TCL_OK ||
            PrepareGridSmithTicksTransaction(gridPtr, &smithTicks) != TCL_OK) {
            FreeGridPolarAngleTicksTransaction(&angleTicks);
            FreeGridSmithTicksTransaction(&smithTicks);
            Rbc_FreeAxisReference(graphPtr, newXAxis);
            Rbc_FreeAxisReference(graphPtr, newYAxis);
            return TCL_ERROR;
        }
    }
    /*
     * Allocate the replacement GC only after every fallible conversion
     * and axis lookup has succeeded.
     */
    if (mask & GRID_GC_CHANGED) {
        gcValues.background = gcValues.foreground = gridPtr->colorPtr->pixel;
        gcValues.line_width = LineWidth(newLineWidth);
        gcMask = GCForeground | GCBackground | GCLineWidth;
        if (LineIsDashed(newDashes)) {
            gcValues.line_style = LineOnOffDash;
            gcMask |= GCLineStyle;
        }
        newGC = Rbc_GetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
        if (LineIsDashed(newDashes)) {
            Rbc_SetDashes(graphPtr->display, newGC, &newDashes);
        }
    }
    CommitGridPolarAngleTicksTransaction(gridPtr, &angleTicks);
    CommitGridSmithTicksTransaction(gridPtr, &smithTicks);
    gridPtr->optionsConfigured = TRUE;
    /*
     * Commit the new axis references.
     */
    if (mask & GRID_AXES_CHANGED) {
        oldXAxis = gridPtr->axes.x;
        oldYAxis = gridPtr->axes.y;
        gridPtr->axes.x = newXAxis;
        gridPtr->axes.y = newYAxis;
        Rbc_FreeAxisReference(graphPtr, oldXAxis);
        Rbc_FreeAxisReference(graphPtr, oldYAxis);
    }
    /*
     * Commit the new drawing state.
     */
    if (mask & GRID_GC_CHANGED) {
        GC oldGC;
        oldGC = gridPtr->gc;
        gridPtr->dashes = newDashes;
        gridPtr->lineWidth = newLineWidth;
        gridPtr->gc = newGC;
        if (oldGC != NULL) {
            Rbc_FreePrivateGC(graphPtr->display, oldGC);
        }
    }
    return TCL_OK;
}

void Rbc_MapGrid(Graph *graphPtr) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    Tcl_Size nSegments;
    Segment2D *segments;

    if (gridPtr->x.segments != NULL) {
        ckfree((char *)gridPtr->x.segments);
        gridPtr->x.segments = NULL;
    }
    if (gridPtr->y.segments != NULL) {
        ckfree((char *)gridPtr->y.segments);
        gridPtr->y.segments = NULL;
    }
    gridPtr->x.nSegments = gridPtr->y.nSegments = 0;
    if (graphPtr->classUid == rbcPolarElementUid) {
        switch (graphPtr->representation) {
        case POLAR_REPRESENTATION_POLAR:
            Rbc_MapPolarGrid(graphPtr, gridPtr);
            break;
        case POLAR_REPRESENTATION_SMITH:
            Rbc_MapSmithGrid(graphPtr, gridPtr);
            break;
        }
        return;
    }
    if (graphPtr->trackGraph) {
        RbcTrack *track;
        for (track = graphPtr->tracks; track != NULL; track = track->next) {
            Grid *local = track->grid != NULL ? track->grid : gridPtr;
            Axis *yAxis = track->grid != NULL ? local->axes.y :
                (gridPtr->axes.y != NULL && track->nAxes > 0 ? track->axes[0] : NULL);
            RbcTrackViewport saved;
            if (track->gridX.segments != NULL) {ckfree(track->gridX.segments);}
            if (track->gridY.segments != NULL) {ckfree(track->gridY.segments);}
            memset(&track->gridX, 0, sizeof(track->gridX));
            memset(&track->gridY, 0, sizeof(track->gridY));
            if (track->right - track->left <= 1 || track->bottom - track->top <= 1) {continue;}
            Rbc_EnterTrack(graphPtr, track, &saved);
            Rbc_GetAxisSegments(graphPtr, local->axes.x, local->minorGrid,
                                &track->gridX.segments, &track->gridX.nSegments);
            Rbc_GetAxisSegments(graphPtr, yAxis, local->minorGrid,
                                &track->gridY.segments, &track->gridY.nSegments);
            Rbc_LeaveTrack(graphPtr, &saved);
        }
        return;
    }
    /*
     * Generate line segments to represent the grid.  Line segments
     * are calculated from the major tick intervals of each axis mapped.
     */
    Rbc_GetAxisSegments(graphPtr, gridPtr->axes.x, gridPtr->minorGrid, &segments, &nSegments);
    if (nSegments > 0) {
        gridPtr->x.nSegments = nSegments;
        gridPtr->x.segments = segments;
    }
    Rbc_GetAxisSegments(graphPtr, gridPtr->axes.y, gridPtr->minorGrid, &segments, &nSegments);
    if (nSegments > 0) {
        gridPtr->y.nSegments = nSegments;
        gridPtr->y.segments = segments;
    }
}

/**
 * @brief Draw cached segments with each track's effective grid and clip.
 * @ingroup grid_internals
 *
 * Requires mapped track geometry and segment caches. Selects local settings or
 * the global inherited grid, skipping hidden/collapsed tracks independently.
 * Pairs each entered track viewport with restoration. A non-NULL exportPtr selects
 * export output and explicit rectangular clipping; otherwise drawable is borrowed
 * screen storage. Renderer contexts are ended before returning.
 *
 * The native fallback reuses one scratch clip context across tracks and copies
 * only visible track rectangles, retaining original segment/dash coordinates.
 * This clips complete strokes, not only their centerlines. Does not remap,
 * finalize export documents or own the cached segment arrays.
 *
 * @param[in,out] graphPtr Borrowed trackgraph with mapped visible rectangles and segment caches;
 *    temporary viewports are restored.
 * @param[in] drawable Borrowed compatible screen destination when exportPtr is NULL; ignored during
 *    export.
 * @param[in,out] exportPtr Borrowed initialized export context, or NULL for screen output; not finalized
 *    here.
 */
static void DrawTrackGrids(Graph *graphPtr, Drawable drawable, Rbc_ExportContext *exportPtr) {
    RbcTrack *track;
    Rbc_RenderContext *clip = NULL;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        Grid *gridPtr = track->grid != NULL ? track->grid : graphPtr->gridPtr;
        Rbc_RenderContext *ctx;
        RbcTrackViewport saved;
        if (gridPtr->hidden || track->right - track->left <= 1 || track->bottom - track->top <= 1) {continue;}
        Rbc_EnterTrack(graphPtr, track, &saved);
        if (exportPtr != NULL) {
            ctx = Rbc_RenderBeginExport(exportPtr, gridPtr->colorPtr, gridPtr->lineWidth,
                                       &gridPtr->dashes, CapButt, JoinMiter);
            Rbc_RenderClipBegin(ctx, track->left, track->top,
                                track->right-track->left+1, track->bottom-track->top+1);
        } else {
            ctx = Rbc_RenderBegin(graphPtr, drawable, gridPtr->colorPtr, MAX(1, gridPtr->lineWidth),
                                  &gridPtr->dashes, NULL);
        }
        if (ctx != NULL) {
            Rbc_RenderSegments(ctx, track->gridX.segments, track->gridX.nSegments);
            Rbc_RenderSegments(ctx, track->gridY.segments, track->gridY.nSegments);
            if (exportPtr != NULL) {Rbc_RenderClipEnd(ctx);}
            Rbc_RenderEnd(ctx);
        } else {
            int left = MAX(0, track->left);
            int top = MAX(0, track->top);
            int width = MIN(graphPtr->width - 1, track->right) - left + 1;
            int height = MIN(graphPtr->height - 1, track->bottom) - top + 1;
            /*
             * The Windows segment emulation does not honor GC clip regions.
             * As with track markers, copy only the track rectangle back from
             * a reusable scratch pixmap. This clips the whole stroke, not just
             * its centerline, without changing the grid's dash coordinates.
             */
            if (width > 0 && height > 0) {
                Drawable target;
                if (clip == NULL) clip = Rbc_RenderBeginScreenClip(graphPtr, drawable, NULL, 0);
                Rbc_RenderClipBegin(clip, left, top, width, height);
                target = Rbc_RenderClipDrawable(clip);
                if (target != None) {
                    Rbc_RenderContext *stroke = Rbc_RenderBeginNativeSegments(graphPtr, target, gridPtr->gc);
                    Rbc_RenderSegments(stroke, track->gridX.segments, track->gridX.nSegments);
                    Rbc_RenderSegments(stroke, track->gridY.segments, track->gridY.nSegments);
                    Rbc_RenderEnd(stroke);
                }
                Rbc_RenderClipEnd(clip);
            }
        }
        Rbc_LeaveTrack(graphPtr, &saved);
    }
    if (clip != NULL) Rbc_RenderEnd(clip);
}

void Rbc_DrawGrid(Graph *graphPtr, Drawable drawable) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    Rbc_RenderContext *ctx;

    if (graphPtr->trackGraph) {
        DrawTrackGrids(graphPtr, drawable, NULL);
        return;
    }
    if (gridPtr->hidden) {
        return;
    }
    ctx = Rbc_RenderBegin(graphPtr, drawable, gridPtr->colorPtr, MAX(1, gridPtr->lineWidth), &gridPtr->dashes, NULL);
    if (ctx != NULL) {
        Rbc_RenderSegments(ctx, gridPtr->x.segments, gridPtr->x.nSegments);
        Rbc_RenderSegments(ctx, gridPtr->y.segments, gridPtr->y.nSegments);
        Rbc_RenderEnd(ctx);
        return;
    }
    if (gridPtr->x.nSegments > 0) {
        Rbc_RenderScreenSegments(graphPtr->tkwin, drawable, gridPtr->gc, gridPtr->x.segments, gridPtr->x.nSegments);
    }
    if (gridPtr->y.nSegments > 0) {
        Rbc_RenderScreenSegments(graphPtr->tkwin, drawable, gridPtr->gc, gridPtr->y.segments, gridPtr->y.nSegments);
    }
}

void Rbc_GridExport(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    Rbc_RenderContext *ctx;

    if (graphPtr->trackGraph) {
        DrawTrackGrids(graphPtr, None, exportPtr);
        return;
    }
    if (gridPtr->hidden) {
        return;
    }
    ctx = Rbc_RenderBeginExport(exportPtr, gridPtr->colorPtr, gridPtr->lineWidth, &gridPtr->dashes,
                                    CapButt, JoinMiter);
    Rbc_RenderSegments(ctx, gridPtr->x.segments, gridPtr->x.nSegments);
    Rbc_RenderSegments(ctx, gridPtr->y.segments, gridPtr->y.nSegments);
    Rbc_RenderEnd(ctx);
}

/**
 * @brief Release one owning grid record while its Tk window is valid.
 * @ingroup grid_internals
 *
 * Accepts NULL. Otherwise releases axis references, Tk options/table, private
 * GC, global segment arrays and all specialized tick arrays, then frees the
 * record. Must not receive the borrowed inherited-query view. Does not clear
 * its owner's pointer or free per-track segment caches; outer destruction helpers
 * perform those tasks. graphPtr->tkwin must still be valid for Tk cleanup.
 *
 * @param[in] graphPtr Owning graph with valid Tk window and display for resource release.
 * @param[in,out] gridPtr Owned grid to free, or NULL; never the shallow inherited-query view. Owner
 *    pointer is not cleared here.
 */
static void DestroyGrid(Graph *graphPtr, Grid *gridPtr) {
    if (gridPtr == NULL) {
        return;
    }
    Rbc_FreeAxisReference(graphPtr, gridPtr->axes.x);
    Rbc_FreeAxisReference(graphPtr, gridPtr->axes.y);
    gridPtr->axes.x = NULL;
    gridPtr->axes.y = NULL;
    /*
     * This must run while graphPtr->tkwin is still valid.
     */
    Tk_FreeConfigOptions((char *)gridPtr, gridPtr->optionTable, graphPtr->tkwin);
    Tk_DeleteOptionTable(gridPtr->optionTable);
    gridPtr->optionTable = NULL;
    if (gridPtr->gc != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, gridPtr->gc);
    }
    if (gridPtr->x.segments != NULL) {
        ckfree((char *)gridPtr->x.segments);
    }
    if (gridPtr->y.segments != NULL) {
        ckfree((char *)gridPtr->y.segments);
    }
    if (gridPtr->angleMajorTicks != NULL) {
        ckfree(gridPtr->angleMajorTicks);
    }
    if (gridPtr->angleMinorTicks != NULL) {
        ckfree(gridPtr->angleMinorTicks);
    }
    if (gridPtr->smithRealMajorTicks != NULL) {
        ckfree(gridPtr->smithRealMajorTicks);
    }
    if (gridPtr->smithRealMinorTicks != NULL) {
        ckfree(gridPtr->smithRealMinorTicks);
    }
    if (gridPtr->smithImagMajorTicks != NULL) {
        ckfree(gridPtr->smithImagMajorTicks);
    }
    if (gridPtr->smithImagMinorTicks != NULL) {
        ckfree(gridPtr->smithImagMinorTicks);
    }
    ckfree((char *)gridPtr);
}

/* Release local option resources before the graph window disappears. */
void Rbc_DestroyTrackGrid(Graph *graphPtr, RbcTrack *track) {
    DestroyGrid(graphPtr, track->grid);
    track->grid = NULL;
    if (track->gridX.segments != NULL) {ckfree(track->gridX.segments);}
    if (track->gridY.segments != NULL) {ckfree(track->gridY.segments);}
    memset(&track->gridX, 0, sizeof(track->gridX));
    memset(&track->gridY, 0, sizeof(track->gridY));
}

void Rbc_DestroyGrid(Graph *graphPtr) {
    RbcTrack *track;
    Grid *gridPtr = graphPtr->gridPtr;
    graphPtr->gridPtr = NULL;
    for (track = graphPtr->tracks; track != NULL; track = track->next) {
        Rbc_DestroyTrackGrid(graphPtr, track);
    }
    DestroyGrid(graphPtr, gridPtr);
}

int Rbc_CreateGrid(Graph *graphPtr) {
    Grid *gridPtr;
    const Tk_OptionSpec *specsPtr;

    gridPtr = Rbc_Calloc(1, sizeof(Grid));
    assert(gridPtr != NULL);
    graphPtr->gridPtr = gridPtr;
    gridPtr->graphPtr = graphPtr;
    if (graphPtr->classUid == rbcBarElementUid) {
        specsPtr = barGridOptionSpecs;
    } else if (graphPtr->classUid == rbcPolarElementUid) {
        specsPtr = polarGridOptionSpecs;
    } else {
        specsPtr = graphGridOptionSpecs;
    }
    /*
     * Tk creates/caches an interpreter-specific table from this
     * static template.
     */
    gridPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, specsPtr);
    if (Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, "grid", "Grid",
                                 (char *)gridPtr, gridPtr->optionTable) != TCL_OK) {
        goto error;
    }
    if (ConfigureGrid(graphPtr, gridPtr, GRID_INITIALIZE_MASK) != TCL_OK) {
        goto error;
    }
    return TCL_OK;

error:
    Rbc_DestroyGrid(graphPtr);
    return TCL_ERROR;
}

/**
 * @ingroup grid_internals
 * @brief Return one component option value.
 * The typed dispatcher has validated argument count. Borrows the graph-owned component and its option
 * table, returning Tk option errors unchanged. This query does not configure resources or schedule
 * drawing.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected grid record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Validated count of four; option name is objv[3].
 * @param[in] objv Borrowed command vector, adapted to ordinary component form for track grids; see @ref
 *    rbc_command_arguments.
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr;
    Tcl_Obj *resultObjPtr;

    gridPtr = (Grid *)graphPtr->gridPtr;
    resultObjPtr = Tk_GetOptionValue(interp, (char *)gridPtr, gridPtr->optionTable, objv[3], graphPtr->tkwin);
    if (resultObjPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, resultObjPtr);
    return TCL_OK;
}

/**
 * @ingroup grid_internals
 * @brief Query or transactionally configure the selected grid.
 * Three or four arguments return option metadata without mutation. Updates first stage Tk options,
 * then expose the borrowed original argument sequence only while ConfigureGrid validates derived
 * state. Clears that temporary sequence on both outcomes. Failure restores saved options and the
 * diagnostic; success frees saved values and schedules masked world/backing-store invalidation.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected grid record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Command count; operation is at index two, optional arguments start at index three.
 * @param[in] objv Borrowed command vector, adapted to ordinary component form for track grids; see @ref
 *    rbc_command_arguments.
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr;
    Tcl_Obj *resultObjPtr;
    Tk_SavedOptions savedOptions;
    int mask;
    int result;

    gridPtr = (Grid *)graphPtr->gridPtr;
    if (objc == 3) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)gridPtr, gridPtr->optionTable, NULL, graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (objc == 4) {
        resultObjPtr = Tk_GetOptionInfo(interp, (char *)gridPtr, gridPtr->optionTable, objv[3], graphPtr->tkwin);
        if (resultObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, resultObjPtr);
        return TCL_OK;
    }
    if (Tk_SetOptions(interp, (char *)gridPtr, gridPtr->optionTable, objc - 3, objv + 3, graphPtr->tkwin, &savedOptions,
                      &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    gridPtr->optionObjc = objc - 3;
    gridPtr->optionObjv = objv + 3;
    result = ConfigureGrid(graphPtr, gridPtr, mask);
    gridPtr->optionObjc = 0;
    gridPtr->optionObjv = NULL;
    if (result != TCL_OK) {
        return Rbc_RestoreOptionsOnError(interp, &savedOptions);
    }
    Tk_FreeSavedOptions(&savedOptions);
    if (mask & GRID_POLAR_CHANGED) {
        graphPtr->flags |= RESET_WORLD;
    }
    if ((mask & GRID_AXES_CHANGED) && (graphPtr->classUid == rbcPolarElementUid) && graphPtr->polarAutoAspect) {
        /*
         * The automatic Polar aspect is derived from the grid's mapped
         * axes.  A mapping change can therefore change the required plot
         * geometry as well as the grid itself.
         */
        graphPtr->flags |= RESET_WORLD;
    }
    if (mask & GRID_REDRAW) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/**
 * @ingroup grid_internals
 * @brief Request visibility for the selected grid.
 * Changes hidden only when necessary, invalidates the backing store and schedules idle graph redraw.
 * Does not immediately paint or remap segments. Under track dispatch, the selected grid may be a newly
 * created local candidate.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected grid record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Unused by this visibility handler.
 * @param[in] objc Validated count of three; unused by this visibility handler.
 * @param[in] objv Unused validated command vector.
 */
static int MapOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    if (gridPtr->hidden) {
        gridPtr->hidden = FALSE; /* Changes "-hide" configuration option */
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/**
 * @ingroup grid_internals
 * @brief Hide the selected grid through deferred redraw.
 * Changes hidden only when currently shown, then invalidates the backing store and schedules idle
 * redraw. Does not free segment caches or erase pixels directly.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected grid record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Unused by this visibility handler.
 * @param[in] objc Validated count of three; unused by this visibility handler.
 * @param[in] objv Unused validated command vector.
 */
static int UnmapOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    if (!gridPtr->hidden) {
        gridPtr->hidden = TRUE; /* Changes "-hide" configuration option */
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/**
 * @ingroup grid_internals
 * @brief Invert grid visibility and schedule a backing-store redraw.
 * Mutates the selected grid, which can be global or temporarily selected track-local state. Always
 * schedules redraw; does not remap or synchronously draw geometry.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected grid record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Unused by this visibility handler.
 * @param[in] objc Validated count of three; unused by this visibility handler.
 * @param[in] objv Unused validated command vector.
 */
static int ToggleOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    gridPtr->hidden = (!gridPtr->hidden);
    graphPtr->flags |= REDRAW_BACKING_STORE;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

static const GridOpSpec gridOps[] = {
    {{"cget", 4, 4, "option"}, CgetOp}, {{"configure", 3, 0, "?options...?"}, ConfigureOp},
    {{"off", 3, 3, ""}, UnmapOp},       {{"on", 3, 3, ""}, MapOp},
    {{"toggle", 3, 3, ""}, ToggleOp},   {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup grid_internals
 * @brief Validate and dispatch a graph grid subcommand.
 * Uses the typed operation table to check name and arity before accessing operation-specific
 * arguments. Borrows graph and Tcl arguments, returns the handler result, and performs no independent
 * drawing or option rollback.
 *
 * @param[in,out] graphPtr Borrowed live graph with selected grid record; enclosing dispatch owns
 *    lifetime protection.
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Command count; operation is at index two, optional arguments start at index three.
 * @param[in] objv Borrowed command vector, adapted to ordinary component form for track grids; see @ref
 *    rbc_command_arguments.
 */
int Rbc_GridOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, gridOps, (Tcl_Size)sizeof(gridOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    return gridOps[index].proc(graphPtr, interp, objc, objv);
}

/**
 * @ingroup grid_internals
 * @brief Adapt nested track grid commands without freezing query defaults.
 * Owns a temporary adapted argument vector and option table, and retains a temporary Y-axis name.
 * Inherited queries use a shallow borrowed global view with the track first Y axis when enabled. First
 * mutation prepares an owned local candidate; only success attaches it. Temporarily selects the view
 * or candidate through graphPtr->gridPtr, restoring the global pointer before cleanup. This
 * synchronous path must not evaluate Tcl callbacks while temporary selection is active.
 *
 * @param[in,out] graphPtr Owning graph; global grid selection is restored before return.
 * @param[in,out] track Borrowed live member of graphPtr; receives an owned local grid only after
 *    successful first mutation.
 * @param[in] interp Borrowed interpreter receiving diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Full nested-command count, including widget, track, grid, track name and operation.
 * @param[in] objv Borrowed full nested-command vector; operation is objv[4]. See @ref
 *    rbc_command_arguments.
 */
int Rbc_TrackGridOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Grid *global = graphPtr->gridPtr;
    Grid view, *local = track->grid;
    Tcl_Obj **args, *mapY;
    Tcl_Size count = objc - 2, i;
    int index, code, query;
    Tk_OptionTable viewTable = NULL;
    if (objc < 5) {
        Tcl_WrongNumArgs(interp, 4, objv, "operation ?arg ...?");
        return TCL_ERROR;
    }
    args = Rbc_CreateTrackComponentArgs(objc, objv);
    code = Rbc_GetOpIndexFromObj(interp, gridOps, sizeof(gridOps[0]), RBC_OP_ARG2, count, args, &index);
    if (code != TCL_OK) {
        goto done;
    }
    query = gridOps[index].proc == CgetOp || (gridOps[index].proc == ConfigureOp && count <= 4);
    /* Queries must not materialize a local grid or freeze inherited defaults. */
    view = *global;
    viewTable = view.optionTable = Tk_CreateOptionTable(interp, trackGridOptionSpecs);
    view.axes.y = global->axes.y != NULL && track->nAxes > 0 ? track->axes[0] : NULL;
    mapY = Tcl_NewStringObj(view.axes.y != NULL ? view.axes.y->name : "", -1);
    Tcl_IncrRefCount(mapY);
    view.mapYObjPtr = mapY;
    if (local == NULL && !query) {
        Tcl_Obj *values[14];
        Tk_SavedOptions saved;
        int mask;
        local = Rbc_Calloc(1, sizeof(*local));
        local->graphPtr = graphPtr;
        local->track = track;
        local->optionTable = Tk_CreateOptionTable(interp, trackGridOptionSpecs);
        code = Tk_InitOptions(interp, (char *)local, local->optionTable, graphPtr->tkwin);
        if (code == TCL_OK) {
            for (i = 0; i < 7; i++) {
                values[i * 2] = Tcl_NewStringObj(trackGridOptionSpecs[i].optionName, -1);
                Tcl_IncrRefCount(values[i * 2]);
                values[i * 2 + 1] =
                    Tk_GetOptionValue(interp, (char *)&view, view.optionTable, values[i * 2], graphPtr->tkwin);
                Tcl_IncrRefCount(values[i * 2 + 1]);
            }
            code = Tk_SetOptions(interp, (char *)local, local->optionTable, 14, values, graphPtr->tkwin, &saved, &mask);
            if (code == TCL_OK) {
                code = ConfigureGrid(graphPtr, local, GRID_GC_CHANGED | GRID_AXES_CHANGED);
                if (code == TCL_OK) {
                    Tk_FreeSavedOptions(&saved);
                } else {
                    code = Rbc_RestoreOptionsOnError(interp, &saved);
                }
            }
            for (i = 0; i < 14; i++) {
                Tcl_DecrRefCount(values[i]);
            }
        }
        if (code != TCL_OK) {
            DestroyGrid(graphPtr, local);
            Tcl_DecrRefCount(mapY);
            goto done;
        }
    }
    graphPtr->gridPtr = local != NULL ? local : &view;
    code = gridOps[index].proc(graphPtr, interp, count, args);
    graphPtr->gridPtr = global;
    if (track->grid == NULL && local != NULL) {
        if (code == TCL_OK) {
            track->grid = local;
        } else {
            DestroyGrid(graphPtr, local);
        }
    }
    Tcl_DecrRefCount(mapY);
done:
    if (viewTable != NULL) {
        Tk_DeleteOptionTable(viewTable);
    }
    Rbc_FreeTrackComponentArgs(args);
    return code;
}
