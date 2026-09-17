/* SVG command and document lifecycle. See license.terms for details. */
#include "rbcRender.h"

/* SVG options intentionally exclude printer/page-specific PostScript settings. */
static const char *const svgOptions[] = {"-width", "-height", "-decorations", NULL};

static Tcl_Obj *SvgOptionInfo(Graph *graphPtr, int index) {
    static const char *const names[] = {"width", "height", "decorations"};
    static const char *const classes[] = {"Width", "Height", "Decorations"};
    int value = index == 0 ? graphPtr->svgWidth : (index == 1 ? graphPtr->svgHeight : graphPtr->svgDecorations);
    Tcl_Obj *items[5];

    items[0] = Tcl_NewStringObj(svgOptions[index], -1);
    items[1] = Tcl_NewStringObj(names[index], -1);
    items[2] = Tcl_NewStringObj(classes[index], -1);
    items[3] = Tcl_NewIntObj(index == 2 ? 1 : 0);
    items[4] = Tcl_NewIntObj(value);
    return Tcl_NewListObj(5, items);
}

static int SetSvgOptions(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int settings[3] = {graphPtr->svgWidth, graphPtr->svgHeight, graphPtr->svgDecorations};
    Tcl_Size i;

    if (objc % 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("SVG options must be option/value pairs", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i += 2) {
        int index, value;
        if (Tcl_GetIndexFromObj(interp, objv[i], svgOptions, "SVG option", 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (index == 2) {
            if (Tcl_GetBooleanFromObj(interp, objv[i + 1], &value) != TCL_OK) { return TCL_ERROR; }
        } else {
            if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[i + 1], &value) != TCL_OK) { return TCL_ERROR; }
            if (value < 0) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("SVG dimensions must be zero or positive", -1));
                return TCL_ERROR;
            }
        }
        settings[index] = value;
    }
    graphPtr->svgWidth = settings[0];
    graphPtr->svgHeight = settings[1];
    graphPtr->svgDecorations = settings[2];
    return TCL_OK;
}

int Rbc_SvgOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *const commands[] = {"cget", "configure", "output", NULL};
    int command, index, result;
    Tcl_Size optionIndex;
    Tcl_Obj *fileName = NULL;
    Rbc_ExportContext storage, *token = &storage;

    if (Tcl_GetIndexFromObj(interp, objv[2], commands, "SVG operation", 0, &command) != TCL_OK) {
        return TCL_ERROR;
    }
    if (command == 0 || (command == 1 && objc == 4)) {
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 3, objv, "option");
            return TCL_ERROR;
        }
        if (Tcl_GetIndexFromObj(interp, objv[3], svgOptions, "SVG option", 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (command == 0) {
            int value = index == 0 ? graphPtr->svgWidth : (index == 1 ? graphPtr->svgHeight : graphPtr->svgDecorations);
            Tcl_SetObjResult(interp, Tcl_NewIntObj(value));
        } else {
            Tcl_SetObjResult(interp, SvgOptionInfo(graphPtr, index));
        }
        return TCL_OK;
    }
    if (command == 1) {
        if (objc == 3) {
            Tcl_Obj *list = Tcl_NewListObj(0, NULL);
            for (index = 0; index < 3; index++) { Tcl_ListObjAppendElement(interp, list, SvgOptionInfo(graphPtr, index)); }
            Tcl_SetObjResult(interp, list);
            return TCL_OK;
        }
        return SetSvgOptions(graphPtr, interp, objc - 3, objv + 3);
    }
    optionIndex = 3;
    if (objc > 3 && Tcl_GetString(objv[3])[0] != '-') {
        fileName = objv[3];
        optionIndex = 4;
    }
    if (SetSvgOptions(graphPtr, interp, objc - optionIndex, objv + optionIndex) != TCL_OK) {
        return TCL_ERROR;
    }
    Rbc_ExportInit(token, RBC_EXPORT_SVG, interp, graphPtr->tkwin, graphPtr->svgDecorations);
    Rbc_ExportBeginGraph(graphPtr);
    if (graphPtr->svgWidth > 0) { graphPtr->width = graphPtr->svgWidth; }
    if (graphPtr->svgHeight > 0) { graphPtr->height = graphPtr->svgHeight; }
    graphPtr->flags |= LAYOUT_NEEDED | MAP_WORLD;
    Rbc_LayoutGraph(graphPtr);
    Rbc_ExportFormat(token,
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n",
        graphPtr->width, graphPtr->height, graphPtr->width, graphPtr->height);
    result = Rbc_ExportGraph(graphPtr, token);
    Rbc_ExportAppend(token, "</svg>\n", (char *)NULL);
    Rbc_ExportEndGraph(graphPtr);
    if (result == TCL_OK && fileName == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_DStringValue(token->buffer), Tcl_DStringLength(token->buffer)));
    } else if (result == TCL_OK) {
        Tcl_Channel channel;
        Tcl_Obj *document;

        /* Validate/render before opening the destination, including unsupported features. */
        channel = Tcl_FSOpenFileChannel(interp, fileName, "w", 0666);
        if (channel == NULL) {
            result = TCL_ERROR;
        } else {
            document = Tcl_NewStringObj(Tcl_DStringValue(token->buffer), Tcl_DStringLength(token->buffer));
            Tcl_IncrRefCount(document);
            if (Tcl_SetChannelOption(interp, channel, "-encoding", "utf-8") != TCL_OK ||
                Tcl_SetChannelOption(interp, channel, "-translation", "lf") != TCL_OK) {
                result = TCL_ERROR;
            } else if (Tcl_WriteObj(channel, document) < 0) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("error writing SVG: %s", Tcl_PosixError(interp)));
                result = TCL_ERROR;
            }
            Tcl_DecrRefCount(document);
            if (result != TCL_OK) {
                Tcl_InterpState saved = Tcl_SaveInterpState(interp, result);
                Tcl_Close(NULL, channel);
                Tcl_RestoreInterpState(interp, saved);
            } else {
                result = Tcl_Close(interp, channel);
                if (result == TCL_OK) { Tcl_ResetResult(interp); }
            }
        }
    }
    Rbc_ExportFree(token);
    return result;
}
