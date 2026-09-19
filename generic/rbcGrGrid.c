/*
 * rbcGrGrid.c --
 *
 *      This module implements grid lines for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"
#include "rbcRender.h"

#define DEF_GRID_DASHES "dot"
#define DEF_GRID_FOREGROUND RGB_GREY64
#define DEF_GRID_FG_MONO RGB_BLACK
#define DEF_GRID_LINE_WIDTH "0"
#define DEF_GRID_HIDE_BARCHART "no"
#define DEF_GRID_HIDE_GRAPH "yes"
#define DEF_GRID_MINOR "yes"
#define DEF_GRID_MAP_X_GRAPH "x"
#define DEF_GRID_MAP_X_BARCHART (char *)NULL
#define DEF_GRID_MAP_Y "y"
#define GRID_REDRAW (1U << 0)
#define GRID_GC_CHANGED (1U << 1)
#define GRID_AXES_CHANGED (1U << 2)
#define GRID_POLAR_CHANGED (1U << 3)
#define GRID_INITIALIZE_MASK (GRID_GC_CHANGED | GRID_AXES_CHANGED | GRID_POLAR_CHANGED)

#define DEF_GRID_RADIAL_LABEL_ANCHOR "se"
#define DEF_GRID_ANGLE_LABEL_ANCHOR "center"
#define DEF_GRID_SMITH_GRID "impedance"
#define DEF_GRID_ANGLE_MAJOR_TICKS "0 30 60 90 120 150 180 210 240 270 300 330"
#define DEF_GRID_ANGLE_MINOR_TICKS "15 45 75 105 135 165 195 225 255 285 315 345"
#define DEF_GRID_ANGLE_COMMAND (char *)NULL
#define DEF_GRID_SMITH_REAL_MAJOR_TICKS "0 0.2 0.5 1 2 5"
#define DEF_GRID_SMITH_REAL_MINOR_TICKS "0.1 0.3 0.7 1.5 3 10"
#define DEF_GRID_SMITH_IMAG_MAJOR_TICKS "0.2 0.5 1 2 5"
#define DEF_GRID_SMITH_IMAG_MINOR_TICKS "0.1 0.3 0.7 1.5 3 10"
#define DEF_GRID_SMITH_REAL_COMMAND (char *)NULL
#define DEF_GRID_SMITH_IMAG_COMMAND (char *)NULL

typedef struct {
    int majorStaged;
    double *majorTicks;
    Tcl_Size nMajorTicks;
    int minorStaged;
    double *minorTicks;
    Tcl_Size nMinorTicks;
} GridPolarAngleTicksTransaction;

typedef struct {
    int realMajorStaged;
    double *realMajorTicks;
    Tcl_Size nRealMajorTicks;
    int realMinorStaged;
    double *realMinorTicks;
    Tcl_Size nRealMinorTicks;
    int imagMajorStaged;
    double *imagMajorTicks;
    Tcl_Size nImagMajorTicks;
    int imagMinorStaged;
    double *imagMinorTicks;
    Tcl_Size nImagMinorTicks;
} GridSmithTicksTransaction;

static int SetPolarLabelAnchor(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **valuePtrPtr,
                               char *widgRec, Tcl_Size offset, char *saveInternalPtr, int flags);
static Tcl_Obj *GetPolarLabelAnchor(void *clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset);
static void RestorePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr, char *saveInternalPtr);
static void FreePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr);
static const Tk_ObjCustomOption polarLabelAnchorOption = {
    "polarLabelAnchor", SetPolarLabelAnchor, GetPolarLabelAnchor, RestorePolarLabelAnchor, FreePolarLabelAnchor, NULL};
static const char *const smithGridNames[] = {"impedance", "admittance", "both", NULL};

static const Tk_OptionSpec graphGridOptionSpecs[] = {
    {TK_OPTION_CUSTOM, "-anglelabelanchor", "angleLabelAnchor", "AngleLabelAnchor", DEF_GRID_ANGLE_LABEL_ANCHOR, -1,
     offsetof(Grid, angleLabelAnchor), 0, &polarLabelAnchorOption, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-anglecommand", "angleCommand", "AngleCommand", DEF_GRID_ANGLE_COMMAND,
     offsetof(Grid, angleCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-anglemajorticks", "angleMajorTicks", "AngleMajorTicks", DEF_GRID_ANGLE_MAJOR_TICKS,
     offsetof(Grid, angleMajorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-angleminorticks", "angleMinorTicks", "AngleMinorTicks", DEF_GRID_ANGLE_MINOR_TICKS,
     offsetof(Grid, angleMinorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_CUSTOM, "-radiallabelanchor", "radialLabelAnchor", "RadialLabelAnchor", DEF_GRID_RADIAL_LABEL_ANCHOR,
     -1, offsetof(Grid, radialLabelAnchor), 0, &polarLabelAnchorOption, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING_TABLE, "-smithgrid", "smithGrid", "SmithGrid", DEF_GRID_SMITH_GRID, -1,
     offsetof(Grid, smithGrid), 0, (ClientData)smithGridNames, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealcommand", "smithRealCommand", "SmithRealCommand", DEF_GRID_SMITH_REAL_COMMAND,
     offsetof(Grid, smithRealCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagcommand", "smithImagCommand", "SmithImagCommand", DEF_GRID_SMITH_IMAG_COMMAND,
     offsetof(Grid, smithImagCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealmajorticks", "smithRealMajorTicks", "SmithRealMajorTicks",
     DEF_GRID_SMITH_REAL_MAJOR_TICKS, offsetof(Grid, smithRealMajorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealminorticks", "smithRealMinorTicks", "SmithRealMinorTicks",
     DEF_GRID_SMITH_REAL_MINOR_TICKS, offsetof(Grid, smithRealMinorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagmajorticks", "smithImagMajorTicks", "SmithImagMajorTicks",
     DEF_GRID_SMITH_IMAG_MAJOR_TICKS, offsetof(Grid, smithImagMajorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagminorticks", "smithImagMinorTicks", "SmithImagMinorTicks",
     DEF_GRID_SMITH_IMAG_MINOR_TICKS, offsetof(Grid, smithImagMinorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},

    {TK_OPTION_COLOR, "-color", "color", "Color", DEF_GRID_FOREGROUND, -1, offsetof(Grid, colorPtr), 0,
     DEF_GRID_FG_MONO, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", DEF_GRID_DASHES, offsetof(Grid, dashesObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_GRID_HIDE_GRAPH, -1, offsetof(Grid, hidden), 0, NULL, GRID_REDRAW},
    {TK_OPTION_PIXELS, "-linewidth", "lineWidth", "Linewidth", DEF_GRID_LINE_WIDTH, offsetof(Grid, lineWidthObjPtr), -1,
     0, NULL, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_GRID_MAP_X_GRAPH, offsetof(Grid, mapXObjPtr), -1, TK_OPTION_NULL_OK,
     NULL, GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_GRID_MAP_Y, offsetof(Grid, mapYObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_BOOLEAN, "-minor", "minor", "Minor", DEF_GRID_MINOR, -1, offsetof(Grid, minorGrid), 0, NULL,
     GRID_REDRAW},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/* Polar grids share graph options, but are visible by default. */
static const Tk_OptionSpec polarGridOptionSpecs[] = {
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", "no", -1, offsetof(Grid, hidden), 0, NULL, GRID_REDRAW},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, (ClientData)graphGridOptionSpecs, 0}};

static const Tk_OptionSpec barGridOptionSpecs[] = {
    {TK_OPTION_CUSTOM, "-anglelabelanchor", "angleLabelAnchor", "AngleLabelAnchor", DEF_GRID_ANGLE_LABEL_ANCHOR, -1,
     offsetof(Grid, angleLabelAnchor), 0, &polarLabelAnchorOption, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-anglecommand", "angleCommand", "AngleCommand", DEF_GRID_ANGLE_COMMAND,
     offsetof(Grid, angleCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-anglemajorticks", "angleMajorTicks", "AngleMajorTicks", DEF_GRID_ANGLE_MAJOR_TICKS,
     offsetof(Grid, angleMajorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-angleminorticks", "angleMinorTicks", "AngleMinorTicks", DEF_GRID_ANGLE_MINOR_TICKS,
     offsetof(Grid, angleMinorTicksObjPtr), -1, 0, NULL, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_CUSTOM, "-radiallabelanchor", "radialLabelAnchor", "RadialLabelAnchor", DEF_GRID_RADIAL_LABEL_ANCHOR,
     -1, offsetof(Grid, radialLabelAnchor), 0, &polarLabelAnchorOption, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING_TABLE, "-smithgrid", "smithGrid", "SmithGrid", DEF_GRID_SMITH_GRID, -1,
     offsetof(Grid, smithGrid), 0, (ClientData)smithGridNames, GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealcommand", "smithRealCommand", "SmithRealCommand", DEF_GRID_SMITH_REAL_COMMAND,
     offsetof(Grid, smithRealCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagcommand", "smithImagCommand", "SmithImagCommand", DEF_GRID_SMITH_IMAG_COMMAND,
     offsetof(Grid, smithImagCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealmajorticks", "smithRealMajorTicks", "SmithRealMajorTicks",
     DEF_GRID_SMITH_REAL_MAJOR_TICKS, offsetof(Grid, smithRealMajorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithrealminorticks", "smithRealMinorTicks", "SmithRealMinorTicks",
     DEF_GRID_SMITH_REAL_MINOR_TICKS, offsetof(Grid, smithRealMinorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagmajorticks", "smithImagMajorTicks", "SmithImagMajorTicks",
     DEF_GRID_SMITH_IMAG_MAJOR_TICKS, offsetof(Grid, smithImagMajorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},
    {TK_OPTION_STRING, "-smithimagminorticks", "smithImagMinorTicks", "SmithImagMinorTicks",
     DEF_GRID_SMITH_IMAG_MINOR_TICKS, offsetof(Grid, smithImagMinorTicksObjPtr), -1, 0, NULL,
     GRID_POLAR_CHANGED | GRID_REDRAW},

    {TK_OPTION_COLOR, "-color", "color", "Color", DEF_GRID_FOREGROUND, -1, offsetof(Grid, colorPtr), 0,
     DEF_GRID_FG_MONO, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-dashes", "dashes", "Dashes", DEF_GRID_DASHES, offsetof(Grid, dashesObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_GRID_HIDE_BARCHART, -1, offsetof(Grid, hidden), 0, NULL,
     GRID_REDRAW},
    {TK_OPTION_PIXELS, "-linewidth", "lineWidth", "Linewidth", DEF_GRID_LINE_WIDTH, offsetof(Grid, lineWidthObjPtr), -1,
     0, NULL, GRID_REDRAW | GRID_GC_CHANGED},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_GRID_MAP_X_BARCHART, offsetof(Grid, mapXObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_GRID_MAP_Y, offsetof(Grid, mapYObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRID_REDRAW | GRID_AXES_CHANGED},
    {TK_OPTION_BOOLEAN, "-minor", "minor", "Minor", DEF_GRID_MINOR, -1, offsetof(Grid, minorGrid), 0, NULL,
     GRID_REDRAW},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

static int IsGridOption(Tcl_Obj *objPtr, const char *optionName) {
    const char *name = Rbc_GetCanonicalOptionName(objPtr, graphGridOptionSpecs);

    return ((name != NULL) && (strcmp(name, optionName) == 0));
}

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

static void RestorePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr, char *saveInternalPtr) {
    (void)clientData;
    (void)tkwin;

    *((PolarLabelAnchor *)internalPtr) = *((PolarLabelAnchor *)saveInternalPtr);
}

static void FreePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr) {
    (void)clientData;
    (void)tkwin;
    (void)internalPtr;
}

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

static void FreeGridPolarAngleTicksTransaction(GridPolarAngleTicksTransaction *transactionPtr) {
    if (transactionPtr->majorTicks != NULL) {
        ckfree(transactionPtr->majorTicks);
    }
    if (transactionPtr->minorTicks != NULL) {
        ckfree(transactionPtr->minorTicks);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

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

typedef struct {
    Rbc_OpSpecHeader header;
    RbcGrGridOp *proc;
} GridOpSpec;

static RbcGrGridOp CgetOp;
static RbcGrGridOp ConfigureOp;
static RbcGrGridOp MapOp;
static RbcGrGridOp UnmapOp;
static RbcGrGridOp ToggleOp;

/*
 *----------------------------------------------------------------------
 *
 * ConfigureGrid --
 *
 *      Configures attributes of the grid such as line width,
 *      dashes, and position.  The grid are first turned off
 *      before any of the attributes changes.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Grid *gridPtr
 *
 * Results:
 *      TCL_OK if the grid was configured successfully.
 *      TCL_ERROR if a retained value or axis mapping is invalid.
 *
 * Side Effects:
 *      May replace the grid GC and the retained X and Y axis references.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MapGrid --
 *
 *      Determines the coordinates of the line segments corresponding
 *      to the grid lines for each axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
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
    /*
     * Generate line segments to represent the grid.  Line segments
     * are calculated from the major tick intervals of each axis mapped.
     */
    Rbc_GetAxisSegments(graphPtr, gridPtr->axes.x, &segments, &nSegments);
    if (nSegments > 0) {
        gridPtr->x.nSegments = nSegments;
        gridPtr->x.segments = segments;
    }
    Rbc_GetAxisSegments(graphPtr, gridPtr->axes.y, &segments, &nSegments);
    if (nSegments > 0) {
        gridPtr->y.nSegments = nSegments;
        gridPtr->y.segments = segments;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DrawGrid --
 *
 *      Draws the grid lines associated with each axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_DrawGrid(Graph *graphPtr, Drawable drawable) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    Rbc_RenderContext *ctx;

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
        Rbc_Draw2DSegments(graphPtr->display, drawable, gridPtr->gc, gridPtr->x.segments, gridPtr->x.nSegments);
    }
    if (gridPtr->y.nSegments > 0) {
        Rbc_Draw2DSegments(graphPtr->display, drawable, gridPtr->gc, gridPtr->y.segments, gridPtr->y.nSegments);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GridExport --
 *
 *      Prints the grid lines associated with each axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Rbc_ExportContext *exportPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_GridExport(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    Grid *gridPtr = (Grid *)graphPtr->gridPtr;
    Rbc_RenderContext *ctx;

    if (gridPtr->hidden) {
        return;
    }
    ctx = Rbc_RenderBeginExport(exportPtr, gridPtr->colorPtr, gridPtr->lineWidth, &gridPtr->dashes,
                                    CapButt, JoinMiter);
    Rbc_RenderSegments(ctx, gridPtr->x.segments, gridPtr->x.nSegments);
    Rbc_RenderSegments(ctx, gridPtr->y.segments, gridPtr->y.nSegments);
    Rbc_RenderEnd(ctx);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DestroyGrid --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Grid GC is released.
 *
 *----------------------------------------------------------------------
 */
void Rbc_DestroyGrid(Graph *graphPtr) {
    Grid *gridPtr;

    gridPtr = (Grid *)graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    /*
     * Prevent accidental re-entry and tell DestroyGraph that this
     * component has already been released.
     */
    graphPtr->gridPtr = NULL;
    Rbc_FreeAxisReference(graphPtr, gridPtr->axes.x);
    Rbc_FreeAxisReference(graphPtr, gridPtr->axes.y);
    gridPtr->axes.x = NULL;
    gridPtr->axes.y = NULL;
    /*
     * This must run while graphPtr->tkwin is still valid.
     */
    Tk_FreeConfigOptions((char *)gridPtr, gridPtr->optionTable, graphPtr->tkwin);
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CreateGrid --
 *
 *      Creates and initializes a new grid structure.
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      Returns TCL_ERROR if the configuration failed, otherwise TCL_OK.
 *
 * Side Effects:
 *      Memory for grid structure is allocated.
 *
 *----------------------------------------------------------------------
 */
int Rbc_CreateGrid(Graph *graphPtr) {
    Grid *gridPtr;
    const Tk_OptionSpec *specsPtr;

    gridPtr = RbcCalloc(1, sizeof(Grid));
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

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries configuration attributes of the grid such as line
 *      width, dashes, and position.
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
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Queries or resets configuration attributes of the grid
 *      such as line width, dashes, and position.
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
 *      Grid attributes are reset.  The graph is redrawn at the
 *      next idle point.
 *
 *----------------------------------------------------------------------
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
        Tcl_Obj *errorObjPtr;
        /*
         * Preserve the error from post-configuration validation while
         * restoring the option values.
         */
        errorObjPtr = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(errorObjPtr);
        Tk_RestoreSavedOptions(&savedOptions);
        Tcl_SetObjResult(interp, errorObjPtr);
        Tcl_DecrRefCount(errorObjPtr);
        return TCL_ERROR;
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

/*
 *----------------------------------------------------------------------
 *
 * MapOp --
 *
 *      Maps the grid.
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
 *      Grid attributes are reset and the graph is redrawn if necessary.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * MapOp --
 *
 *      Maps or unmaps the grid (off or on).
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
 *      Grid attributes are reset and the graph is redrawn if necessary.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * ToggleOp --
 *
 *      Toggles the state of the grid shown/hidden.
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
 *      Grid is hidden/displayed. The graph is redrawn at the next
 *      idle time.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GridOp --
 *
 *      User routine to configure grid lines.  Grids are drawn
 *      at major tick intervals across the graph.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Grid may be drawn in the plotting area.
 *
 *----------------------------------------------------------------------
 */
int Rbc_GridOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, gridOps, (Tcl_Size)sizeof(gridOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    return gridOps[index].proc(graphPtr, interp, objc, objv);
}
