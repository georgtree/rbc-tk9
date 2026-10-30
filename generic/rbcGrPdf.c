/* PDF command and document lifecycle. See license.terms for details. */
#include "rbcRender.h"

/* PDF options intentionally exclude printer/page-specific PostScript settings. */
static const Tk_OptionSpec pdfOptionSpecs[] = {
    {TK_OPTION_PIXELS, "-width", "width", "Width", "0", -1,
     offsetof(Graph, pdfWidth), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-height", "height", "Height", "0", -1,
     offsetof(Graph, pdfHeight), 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-decorations", "decorations", "Decorations", "1", -1,
     offsetof(Graph, pdfDecorations), 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-embedfonts", "embedFonts", "EmbedFonts", "1", -1,
     offsetof(Graph, pdfEmbedFonts), 0, NULL, 0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}
};

/* Initialize the named export component once during widget creation. These
 * options contain only native integers, so no Tk-owned resources survive the
 * temporary option table. Runtime configuration keeps its existing transaction. */
int Rbc_InitPdfOptions(Graph *graphPtr) {
    Tk_OptionTable table = Tk_CreateOptionTable(graphPtr->interp, pdfOptionSpecs);
    int result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy,
                                          "pdf", "Pdf", (char *)graphPtr, table);
    if (result == TCL_OK && (graphPtr->pdfWidth < 0 || graphPtr->pdfHeight < 0)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("PDF dimensions must be zero or positive", -1));
        result = TCL_ERROR;
    }
    Tk_FreeConfigOptions((char *)graphPtr, table, graphPtr->tkwin);
    Tk_DeleteOptionTable(table);
    return result;
}

/* These specifications store native integers directly in the Graph record. */
static Tcl_Obj *PdfOptionValue(Graph *graphPtr, int index) {
    const Tk_OptionSpec *spec = pdfOptionSpecs + index;
    return Tcl_NewIntObj(*(int *)((char *)graphPtr + spec->internalOffset));
}

static Tcl_Obj *PdfOptionInfo(Graph *graphPtr, int index) {
    return Rbc_GetComponentOptionInfo(pdfOptionSpecs + index, PdfOptionValue(graphPtr, index));
}

static int SetPdfOptions(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int settings[4] = {graphPtr->pdfWidth, graphPtr->pdfHeight, graphPtr->pdfDecorations, graphPtr->pdfEmbedFonts};
    Tcl_Size i;

    if (objc % 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("PDF options must be option/value pairs", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i += 2) {
        int index, value;
        if (Rbc_GetComponentOptionIndex(interp, objv[i], pdfOptionSpecs, "PDF option", &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (index >= 2) {
            if (Tcl_GetBooleanFromObj(interp, objv[i + 1], &value) != TCL_OK) {
                return TCL_ERROR;
            }
        } else {
            if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[i + 1], &value) != TCL_OK) {
                return TCL_ERROR;
            }
            if (value < 0) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("PDF dimensions must be zero or positive", -1));
                return TCL_ERROR;
            }
        }
        settings[index] = value;
    }
    graphPtr->pdfWidth = settings[0];
    graphPtr->pdfHeight = settings[1];
    graphPtr->pdfDecorations = settings[2];
    graphPtr->pdfEmbedFonts = settings[3];
    return TCL_OK;
}

int Rbc_PdfOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *const commands[] = {"cget", "configure", "output", NULL};
    int command, index, result, screenWidth, screenHeight;
    Tcl_Size optionIndex;
    Tcl_Obj *fileName = NULL, *document = NULL;
    Rbc_ExportContext storage, *token = &storage;

    if (Tcl_GetIndexFromObj(interp, objv[2], commands, "PDF operation", 0, &command) != TCL_OK) {
        return TCL_ERROR;
    }
    if (command == 0 || (command == 1 && objc == 4)) {
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 3, objv, "option");
            return TCL_ERROR;
        }
        if (Rbc_GetComponentOptionIndex(interp, objv[3], pdfOptionSpecs, "PDF option", &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (command == 0) {
            Tcl_SetObjResult(interp, PdfOptionValue(graphPtr, index));
        } else {
            Tcl_SetObjResult(interp, PdfOptionInfo(graphPtr, index));
        }
        return TCL_OK;
    }
    if (command == 1) {
        if (objc == 3) {
            Tcl_Obj *list = Tcl_NewListObj(0, NULL);
            for (index = 0; pdfOptionSpecs[index].type != TK_OPTION_END; index++) {
                Tcl_ListObjAppendElement(interp, list, PdfOptionInfo(graphPtr, index));
            }
            Tcl_SetObjResult(interp, list);
            return TCL_OK;
        }
        return SetPdfOptions(graphPtr, interp, objc - 3, objv + 3);
    }
    optionIndex = 3;
    if (objc > 3 && Tcl_GetString(objv[3])[0] != '-') {
        fileName = objv[3];
        optionIndex = 4;
    }
    if (SetPdfOptions(graphPtr, interp, objc - optionIndex, objv + optionIndex) != TCL_OK) {
        return TCL_ERROR;
    }
    Rbc_ExportInit(token, RBC_EXPORT_PDF, interp, graphPtr->tkwin, graphPtr->pdfDecorations);
    token->embedFonts = graphPtr->pdfEmbedFonts;
    screenWidth = graphPtr->width;
    screenHeight = graphPtr->height;
    Rbc_ExportBeginGraph(graphPtr);
    if (graphPtr->pdfWidth > 0) {
        graphPtr->width = graphPtr->pdfWidth;
    }
    if (graphPtr->pdfHeight > 0) {
        graphPtr->height = graphPtr->pdfHeight;
    }
    graphPtr->flags |= LAYOUT_NEEDED | MAP_WORLD;
    result = Rbc_LayoutGraph(graphPtr);
    if (result != TCL_OK) {
        goto restore;
    }
    /* Tk screen distances become physical PDF points (1/72 inch). */
    Rbc_PdfBegin(
        token, 72.0 * WidthMMOfScreen(Tk_Screen(graphPtr->tkwin)) / (25.4 * WidthOfScreen(Tk_Screen(graphPtr->tkwin))),
        graphPtr->height);
    result = Rbc_ExportGraph(graphPtr, token);
    if (result == TCL_OK) {
        document = Rbc_PdfDocument(token, graphPtr->width, graphPtr->height);
        if (document == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(token->error, -1));
            result = TCL_ERROR;
        } else {
            Tcl_IncrRefCount(document);
        }
    }
restore:
    Rbc_ExportEndGraph(graphPtr);
    /* Restore margins as well as axis mapping before returning, including errors. */
    graphPtr->width = screenWidth;
    graphPtr->height = screenHeight;
    graphPtr->flags |= LAYOUT_NEEDED | MAP_WORLD;
    if (graphPtr->tkwin != NULL && !graphPtr->axisFormatInvalid) {
        Tcl_InterpState saved = Tcl_SaveInterpState(interp, result);
        int restored = Rbc_LayoutGraph(graphPtr);
        if (result == TCL_OK) {
            Tcl_DiscardInterpState(saved);
            result = restored;
        } else {
            Tcl_RestoreInterpState(interp, saved);
        }
    }
    if (result == TCL_OK && fileName == NULL) {
        Tcl_SetObjResult(interp, document);
    } else if (result == TCL_OK) {
        Tcl_Channel channel;

        /* Validate/render before opening the destination, including unsupported features. */
        channel = Tcl_FSOpenFileChannel(interp, fileName, "w", 0666);
        if (channel == NULL) {
            result = TCL_ERROR;
        } else {
            if (Tcl_SetChannelOption(interp, channel, "-encoding", "iso8859-1") != TCL_OK ||
                Tcl_SetChannelOption(interp, channel, "-translation", "binary") != TCL_OK) {
                result = TCL_ERROR;
            } else if (Tcl_WriteObj(channel, document) < 0) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("error writing PDF: %s", Tcl_PosixError(interp)));
                result = TCL_ERROR;
            }
            if (result != TCL_OK) {
                Tcl_InterpState saved = Tcl_SaveInterpState(interp, result);
                Tcl_Close(NULL, channel);
                Tcl_RestoreInterpState(interp, saved);
            } else {
                result = Tcl_Close(interp, channel);
                if (result == TCL_OK) {
                    Tcl_ResetResult(interp);
                }
            }
        }
    }
    if (document != NULL) {
        Tcl_DecrRefCount(document);
    }
    Rbc_PdfFree(token);
    Rbc_ExportFree(token);
    return result;
}
