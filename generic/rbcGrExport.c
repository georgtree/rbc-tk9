/* Shared graph export commands and traversal. See license.terms for details. */
#include "rbcFont.h"
#include "rbcGrTrack.h"
#include <stdarg.h>

/*
 * SVG/PDF command services. Specifications store native integer fields only;
 * the format owns defaults, encoding, serialization and backend cleanup.
 */
static int *ExportOptionField(Graph *graphPtr, int offset) {
    return (int *)((char *)graphPtr + offset);
}

static Tcl_Obj *ExportOptionValue(Graph *graphPtr, const Tk_OptionSpec *spec) {
    return Tcl_NewIntObj(*ExportOptionField(graphPtr, spec->internalOffset));
}

static Tcl_Obj *ExportOptionInfo(Graph *graphPtr, const Tk_OptionSpec *spec) {
    return Rbc_GetComponentOptionInfo(spec, ExportOptionValue(graphPtr, spec));
}

/* Initialize named option-database components without retaining Tk resources. */
int Rbc_InitGraphExportOptions(Graph *graphPtr, const Rbc_GraphExportFormat *format) {
    Tk_OptionTable table = Tk_CreateOptionTable(graphPtr->interp, format->options);
    int result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy,
                                          format->name, format->className, (char *)graphPtr, table);
    if (result == TCL_OK && (*ExportOptionField(graphPtr, format->widthOffset) < 0 ||
                            *ExportOptionField(graphPtr, format->heightOffset) < 0)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("%s dimensions must be zero or positive", format->label));
        result = TCL_ERROR;
    }
    Tk_FreeConfigOptions((char *)graphPtr, table, graphPtr->tkwin);
    Tk_DeleteOptionTable(table);
    return result;
}

/* Stage every value before committing. Repeated options retain last-value wins. */
static int SetExportOptions(Graph *graphPtr, Tcl_Interp *interp, const Rbc_GraphExportFormat *format,
                            Tcl_Size objc, Tcl_Obj *const objv[]) {
    const Tk_OptionSpec *specs = format->options;
    int count, j, *settings, result = TCL_ERROR;
    Tcl_Size i;

    if (objc % 2) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s options must be option/value pairs", format->label));
        return TCL_ERROR;
    }
    for (count = 0; specs[count].type != TK_OPTION_END; count++) {
        /* Count the format's concrete integer options. */
    }
    settings = ckalloc(sizeof(*settings) * (size_t)count);
    for (j = 0; j < count; j++) {
        settings[j] = *ExportOptionField(graphPtr, specs[j].internalOffset);
    }
    for (i = 0; i < objc; i += 2) {
        int index, value;
        if (Rbc_GetComponentOptionIndex(interp, objv[i], specs, format->optionLabel, &index) != TCL_OK) {
            goto done;
        }
        switch (specs[index].type) {
        case TK_OPTION_BOOLEAN:
            if (Tcl_GetBooleanFromObj(interp, objv[i + 1], &value) != TCL_OK) {
                goto done;
            }
            break;
        case TK_OPTION_PIXELS:
            if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[i + 1], &value) != TCL_OK) {
                goto done;
            }
            if (value < 0) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s dimensions must be zero or positive", format->label));
                goto done;
            }
            break;
        default:
            Tcl_Panic("unsupported graph export option type");
            goto done;
        }
        settings[index] = value;
    }
    for (j = 0; j < count; j++) {
        *ExportOptionField(graphPtr, specs[j].internalOffset) = settings[j];
    }
    result = TCL_OK;
done:
    ckfree(settings);
    return result;
}

/* The complete document is available before a destination can be truncated. */
static int WriteExportDocument(Tcl_Interp *interp, const Rbc_GraphExportFormat *format, Tcl_Obj *fileName,
                               Tcl_Obj *document) {
    Tcl_Channel channel;
    int result = TCL_OK;

    channel = Tcl_FSOpenFileChannel(interp, fileName, "w", 0666);
    if (channel == NULL) {
        return TCL_ERROR;
    }
    if (Tcl_SetChannelOption(interp, channel, "-encoding", format->encoding) != TCL_OK ||
        Tcl_SetChannelOption(interp, channel, "-translation", format->translation) != TCL_OK) {
        result = TCL_ERROR;
    } else if (Tcl_WriteObj(channel, document) < 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("error writing %s: %s", format->label, Tcl_PosixError(interp)));
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
    return result;
}

/*
 * Render once at export dimensions, then restore screen layout before delivery.
 * The document callback returns a new, unreferenced Tcl object on success.
 * It does not restore geometry or free the export context. Backend cleanup runs
 * even when initial layout fails, so it must accept an empty context.
 */
static int OutputGraphExport(Graph *graphPtr, Tcl_Interp *interp, const Rbc_GraphExportFormat *format,
                             Tcl_Obj *fileName) {
    Rbc_ExportContext token;
    Tcl_Obj *document = NULL;
    int screenWidth = graphPtr->width, screenHeight = graphPtr->height;
    int width = *ExportOptionField(graphPtr, format->widthOffset);
    int height = *ExportOptionField(graphPtr, format->heightOffset);
    int result;

    Rbc_ExportInit(&token, format->backend, interp, graphPtr->tkwin,
                   *ExportOptionField(graphPtr, format->decorationsOffset));
    token.embedFonts = *ExportOptionField(graphPtr, format->embedFontsOffset);
    Rbc_ExportBeginGraph(graphPtr);
    if (width > 0) {
        graphPtr->width = width;
    }
    if (height > 0) {
        graphPtr->height = height;
    }
    graphPtr->flags |= LAYOUT_NEEDED | MAP_WORLD;
    result = Rbc_LayoutGraph(graphPtr);
    if (result == TCL_OK) {
        result = format->documentProc(graphPtr, &token, &document);
        if (document != NULL) {
            Tcl_IncrRefCount(document);
        }
    }
    Rbc_ExportEndGraph(graphPtr);
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
    if (result == TCL_OK) {
        assert(document != NULL);
        if (fileName == NULL) {
            Tcl_SetObjResult(interp, document);
        } else {
            result = WriteExportDocument(interp, format, fileName, document);
        }
    }
    if (document != NULL) {
        Tcl_DecrRefCount(document);
    }
    if (format->freeProc != NULL) {
        format->freeProc(&token);
    }
    Rbc_ExportFree(&token);
    return result;
}

/* Shared SVG/PDF dispatch; successful output options remain configured. */
int Rbc_GraphExportOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[],
                      const Rbc_GraphExportFormat *format) {
    static const char *const commands[] = {"cget", "configure", "output", NULL};
    const Tk_OptionSpec *specs = format->options;
    int command, index;
    Tcl_Size optionIndex;
    Tcl_Obj *fileName = NULL;

    if (Tcl_GetIndexFromObj(interp, objv[2], commands, format->operationLabel, 0, &command) != TCL_OK) {
        return TCL_ERROR;
    }
    if (command == 0 || (command == 1 && objc == 4)) {
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 3, objv, "option");
            return TCL_ERROR;
        }
        if (Rbc_GetComponentOptionIndex(interp, objv[3], specs, format->optionLabel, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, command == 0 ? ExportOptionValue(graphPtr, specs + index)
                                             : ExportOptionInfo(graphPtr, specs + index));
        return TCL_OK;
    }
    if (command == 1) {
        if (objc == 3) {
            Tcl_Obj *list = Tcl_NewListObj(0, NULL);
            for (index = 0; specs[index].type != TK_OPTION_END; index++) {
                Tcl_ListObjAppendElement(interp, list, ExportOptionInfo(graphPtr, specs + index));
            }
            Tcl_SetObjResult(interp, list);
            return TCL_OK;
        }
        return SetExportOptions(graphPtr, interp, format, objc - 3, objv + 3);
    }
    optionIndex = 3;
    if (objc > 3 && Tcl_GetString(objv[3])[0] != '-') {
        fileName = objv[3];
        optionIndex = 4;
    }
    if (SetExportOptions(graphPtr, interp, format, objc - optionIndex, objv + optionIndex) != TCL_OK) {
        return TCL_ERROR;
    }
    return OutputGraphExport(graphPtr, interp, format, fileName);
}

void Rbc_ExportInit(Rbc_ExportContext *exportPtr, Rbc_ExportBackend backend, Tcl_Interp *interp, Tk_Window tkwin,
                    int decorations) {
    memset(exportPtr, 0, sizeof(*exportPtr));
    exportPtr->backend = backend;
    exportPtr->interp = interp;
    exportPtr->tkwin = tkwin;
    exportPtr->decorations = decorations;
    Tcl_DStringInit(&exportPtr->storage);
    exportPtr->buffer = &exportPtr->storage;
}

/* Error messages are borrowed static strings. Preserve the original failure
 * even when a later drawing or document-finalization step also fails. */
int Rbc_ExportError(Rbc_ExportContext *exportPtr, const char *message) {
    if (exportPtr->error == NULL) {
        exportPtr->error = message;
    }
    return TCL_ERROR;
}

void Rbc_ExportFree(Rbc_ExportContext *exportPtr) {
    Rbc_ExportFontsFree(exportPtr);
    Tcl_DStringFree(&exportPtr->storage);
}

void Rbc_ExportAppend(Rbc_ExportContext *exportPtr, ...) {
    va_list args;
    const char *string;

    va_start(args, exportPtr);
    while ((string = va_arg(args, const char *)) != NULL) {
        Tcl_DStringAppend(exportPtr->buffer, string, -1);
    }
    va_end(args);
}

void Rbc_ExportFormat(Rbc_ExportContext *exportPtr, const char *format, ...) {
    va_list args, copy;
    char local[512], *buffer = local;
    int length;

    va_start(args, format);
    va_copy(copy, args);
    length = vsnprintf(local, sizeof(local), format, args);
    va_end(args);
    if (length >= (int)sizeof(local)) {
        buffer = (char *)ckalloc((size_t)length + 1);
        vsnprintf(buffer, (size_t)length + 1, format, copy);
    }
    va_end(copy);
    if (length < 0) {
        Rbc_ExportError(exportPtr, "cannot format export document");
    } else {
        Tcl_DStringAppend(exportPtr->buffer, buffer, length);
    }
    if (buffer != local) {
        ckfree(buffer);
    }
}

void Rbc_ExportBeginGraph(Graph *graphPtr) {
    if (graphPtr->height <= 1) {
        graphPtr->height = Tk_ReqHeight(graphPtr->tkwin);
    }
    if (graphPtr->width <= 1) {
        graphPtr->width = Tk_ReqWidth(graphPtr->tkwin);
    }
    graphPtr->flags |= GRAPH_EXPORT;
}

void Rbc_ExportEndGraph(Graph *graphPtr) {
    graphPtr->flags &= ~GRAPH_EXPORT;
    if (graphPtr->tkwin == NULL) {
        return;
    }
    graphPtr->width = Tk_Width(graphPtr->tkwin);
    graphPtr->height = Tk_Height(graphPtr->tkwin);
    graphPtr->flags = MAP_WORLD;
    Rbc_EventuallyRedrawGraph(graphPtr);
}

static void MarginsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    Rbc_RenderRectangle margin[4];

    int decorations = exportPtr->decorations;
    margin[0].x = margin[0].y = margin[3].x = margin[1].x = 0;
    margin[0].width = margin[3].width = graphPtr->width;
    margin[0].height = graphPtr->top;
    margin[3].y = graphPtr->bottom;
    margin[3].height = graphPtr->height - graphPtr->bottom;
    margin[2].y = margin[1].y = graphPtr->top;
    margin[1].width = graphPtr->left;
    margin[2].height = margin[1].height = graphPtr->bottom - graphPtr->top;
    margin[2].x = graphPtr->right;
    margin[2].width = graphPtr->width - graphPtr->right;
    /* Clear the surrounding margins and clip the plotting surface */
    Rbc_RenderBackgroundRectangles(output, decorations ? Tk_3DBorderColor(graphPtr->border) : NULL, margin, 4);
    /* Interior 3D border */
    if ((decorations) && (graphPtr->plotBorderWidth > 0)) {
        int x, y, width, height;

        x = graphPtr->left - graphPtr->plotBorderWidth;
        y = graphPtr->top - graphPtr->plotBorderWidth;
        width = (graphPtr->right - graphPtr->left) + (2 * graphPtr->plotBorderWidth);
        height = (graphPtr->bottom - graphPtr->top) + (2 * graphPtr->plotBorderWidth);
        Rbc_RenderBorder(output, graphPtr->border, (double)x, (double)y, width, height, graphPtr->plotBorderWidth,
                         graphPtr->plotRelief, FALSE);
    }
    if (Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_MARGIN) {
        /*
         * Print the legend if we're using a site which lies in one
         * of the margins (left, right, top, or bottom) of the graph.
         */
        Rbc_LegendExport(graphPtr->legend, exportPtr);
    }
    if (graphPtr->title != NULL) {
        Rbc_RenderText(output, graphPtr->title, &graphPtr->titleTextStyle, (double)graphPtr->titleX,
                       (double)graphPtr->titleY);
    }
    Rbc_AxesExport(graphPtr, exportPtr);
    Rbc_RenderEnd(output);
}

int Rbc_ExportGraph(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    int x, y, width, height;
    Rbc_RenderContext *output;

    /*
     * Determine rectangle of the plotting area for the graph window
     */
    x = graphPtr->left - graphPtr->plotBorderWidth;
    y = graphPtr->top - graphPtr->plotBorderWidth;
    width = (graphPtr->right - graphPtr->left + 1) + (2 * graphPtr->plotBorderWidth);
    height = (graphPtr->bottom - graphPtr->top + 1) + (2 * graphPtr->plotBorderWidth);
    output = Rbc_RenderBeginExportOutput(exportPtr);
    Rbc_RenderPlotBegin(output, graphPtr->titleTextStyle.font, (double)x, (double)y, width, height,
                        exportPtr->decorations ? graphPtr->plotBg : NULL);
    if (exportPtr->decorations) {
        Rbc_DrawTrackSeparators(graphPtr, None, exportPtr);
    }
    /* Draw the grid, elements, and markers in the plotting area. */
    if (graphPtr->trackGraph || !graphPtr->gridPtr->hidden) {
        Rbc_GridExport(graphPtr, exportPtr);
    }
    if (graphPtr->classUid == rbcPolarElementUid) {
        switch (graphPtr->representation) {
        case POLAR_REPRESENTATION_POLAR:
            Rbc_PolarLabelsExport(graphPtr, exportPtr);
            break;
        case POLAR_REPRESENTATION_SMITH:
            Rbc_SmithLabelsExport(graphPtr, exportPtr);
            break;
        }
    }
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        Rbc_RenderPlotEnd(output);
        Rbc_RenderEnd(output);
        return TCL_ERROR;
    }
    Rbc_MarkersExport(graphPtr, exportPtr, TRUE);
    Rbc_ExportTrackLegends(graphPtr, exportPtr, FALSE);
    if ((Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_PLOT) && (!Rbc_LegendIsRaised(graphPtr->legend))) {
        /* Print legend underneath elements and markers */
        Rbc_LegendExport(graphPtr->legend, exportPtr);
    }
    Rbc_AxisLimitsExport(graphPtr, exportPtr);
    Rbc_ElementsExport(graphPtr, exportPtr);
    Rbc_ExportTrackLegends(graphPtr, exportPtr, TRUE);
    if ((Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_PLOT) && (Rbc_LegendIsRaised(graphPtr->legend))) {
        /* Print legend above elements (but not markers) */
        Rbc_LegendExport(graphPtr->legend, exportPtr);
    }
    Rbc_MarkersExport(graphPtr, exportPtr, FALSE);
    Rbc_ActiveElementsExport(graphPtr, exportPtr);
    Rbc_RenderPlotEnd(output);
    MarginsExport(graphPtr, exportPtr);
    Rbc_RenderEnd(output);
    if (exportPtr->error != NULL) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj(exportPtr->error, -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}
