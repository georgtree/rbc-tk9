/**
 * @file rbcGrExport.c
 * @defgroup graph_export_commands SVG/PDF graph command internals
 * @brief Shared export commands, document context and graph traversal.
 * See license.terms for details.
 */
#include "rbcFont.h"
#include "rbcGrTrack.h"
#include <stdarg.h>

/**
 * @brief Address one native integer option field within a borrowed graph.
 * @ingroup graph_export_commands
 *
 * Requires an offset for a correctly aligned int member of Graph. Returns a
 * borrowed pointer valid only while the graph is alive. Does not validate
 * the offset or allocate storage; format policies are trusted private data.
 */
static int *ExportOptionField(Graph *graphPtr, int offset) {
    return (int *)((char *)graphPtr + offset);
}

/**
 * @brief Copy an integer-backed export option into a new Tcl object.
 * @ingroup graph_export_commands
 *
 * Borrows the graph and concrete option specification. Returns a new object
 * at reference count zero, using internalOffset to locate the int field.
 * Boolean options are reported as integer zero or one, not original spelling.
 */
static Tcl_Obj *ExportOptionValue(Graph *graphPtr, const Tk_OptionSpec *spec) {
    return Tcl_NewIntObj(*ExportOptionField(graphPtr, spec->internalOffset));
}

/**
 * @brief Build the standard option-description list for one export field.
 * @ingroup graph_export_commands
 *
 * Borrows graph and spec, reads the current integer value, and passes its
 * new object to Rbc_GetComponentOptionInfo. Returns a new, unreferenced
 * description object. Does not retain the graph, specification or Tk table.
 */
static Tcl_Obj *ExportOptionInfo(Graph *graphPtr, const Tk_OptionSpec *spec) {
    return Rbc_GetComponentOptionInfo(spec, ExportOptionValue(graphPtr, spec));
}

/**
 * @brief Initialize the named SVG/PDF option-database component.
 * Borrow the graph and format policy. Specifications must store concrete pixel
 * or boolean values in native Graph integer fields, without retained Tk
 * resources. The temporary option table is freed on success and failure.
 * @return TCL_OK, or TCL_ERROR for invalid options or negative dimensions.
 * Failure is graph-initialization failure, not a rollback transaction.
 */
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

/**
 * @brief Stage all SVG/PDF option values before committing the configuration.
 * @ingroup graph_export_commands
 *
 * Requires concrete pixel/boolean options backed by native Graph ints.
 * Allocates and frees a temporary settings array; arguments and format are
 * borrowed. Rejects odd argument counts, unknown or ambiguous options,
 * invalid values and negative dimensions with TCL_ERROR. No field changes
 * before all conversions succeed. Repeated valid options use the last value,
 * but an invalid earlier occurrence still fails. Unsupported specification
 * types are programming errors and panic. Returns TCL_OK after committing.
 */
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

/**
 * @brief Write an already-built document through the Tcl filesystem API.
 * @ingroup graph_export_commands
 *
 * Borrows the format, filename and document objects. Opens with mode w,
 * applies the format encoding and translation, then writes the object.
 * Owns and closes the channel on every path after open. Preserves an earlier
 * channel/write error across cleanup; otherwise propagates close failure.
 * TCL_OK clears the interpreter result. This is not atomic replacement: an
 * open or delivery failure may leave an empty or partial destination.
 */
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

/**
 * @brief Build SVG/PDF, restore screen layout and deliver only on success.
 * @ingroup graph_export_commands
 *
 * Borrows the graph and format; NULL fileName requests a Tcl result. Owns a
 * stack export context and takes a reference to any document returned by
 * documentProc. Saves screen dimensions, applies positive configured sizes
 * and invokes the builder only after successful layout. Ends export mode,
 * restores saved dimensions and remaps if the window and formatting state
 * permit. Preserves an earlier error across restoration; a restoration
 * error replaces success. Releases the document, backend state and shared
 * storage on every path. freeProc must accept an uninitialized backend.
 * The enclosing widget command protects graph lifetime across callbacks.
 * Returns TCL_OK or TCL_ERROR; persistent export options are not restored.
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

/**
 * @brief Dispatch SVG/PDF cget, configure and output operations.
 * The enclosing widget command must preserve graph lifetime across callbacks.
 * Requires objv[2] to exist and contain the operation. Option parsing stages
 * all values before committing; successful output options stay configured even
 * if rendering or file delivery later fails. Output remaps at export size,
 * restores screen layout when callback state permits, and then delivers the
 * complete document. Backend and shared context cleanup run on every path.
 * @return TCL_OK or TCL_ERROR with an interpreter diagnostic.
 */
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

/**
 * @brief Initialize fresh caller-owned export storage.
 * Zero the record, borrow interp and tkwin, and select the embedded DString as
 * the append destination. Call only on an uninitialized or already freed record;
 * this does not release an earlier document. Font embedding starts disabled.
 * The caller supplies backend state and any alternate buffer after this call.
 */
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

/**
 * @brief Retain the first export failure without replacing later diagnostics.
 * @param exportPtr Initialized, borrowed context.
 * @param message Borrowed static string; it is neither copied nor freed.
 * @return TCL_ERROR. This helper does not set the interpreter result.
 */
int Rbc_ExportError(Rbc_ExportContext *exportPtr, const char *message) {
    if (exportPtr->error == NULL) {
        exportPtr->error = message;
    }
    return TCL_ERROR;
}

/**
 * @brief Release document-local font data and the embedded DString.
 * Does not free the caller's context, backendData, or an alternate buffer.
 * The format owner must release backend resources separately. No renderer
 * context may still be using these resources.
 */
void Rbc_ExportFree(Rbc_ExportContext *exportPtr) {
    Rbc_ExportFontsFree(exportPtr);
    Tcl_DStringFree(&exportPtr->storage);
}

/**
 * @brief Append a NULL-terminated sequence of C strings to the current buffer.
 * The initialized context borrows buffer; callers must keep that DString alive.
 * This helper neither checks nor clears an existing export error.
 */
void Rbc_ExportAppend(Rbc_ExportContext *exportPtr, ...) {
    va_list args;
    const char *string;

    va_start(args, exportPtr);
    while ((string = va_arg(args, const char *)) != NULL) {
        Tcl_DStringAppend(exportPtr->buffer, string, -1);
    }
    va_end(args);
}

/**
 * @brief Append printf-formatted text to the current document buffer.
 * Uses temporary heap storage when the local formatting buffer is too small.
 * A formatting failure records a static export error; it does not set the Tcl
 * result. The caller must check the context's error before delivering output.
 */
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

/**
 * @brief Mark a graph as exporting, using requested size for unset dimensions.
 * Requires a live Tk window. Does not perform layout or preserve old dimensions.
 * Pair with Rbc_ExportEndGraph on all paths; this flag is not a nesting counter.
 */
void Rbc_ExportBeginGraph(Graph *graphPtr) {
    if (graphPtr->height <= 1) {
        graphPtr->height = Tk_ReqHeight(graphPtr->tkwin);
    }
    if (graphPtr->width <= 1) {
        graphPtr->width = Tk_ReqWidth(graphPtr->tkwin);
    }
    graphPtr->flags |= GRAPH_EXPORT;
}

/**
 * @brief Clear export mode and schedule screen remapping if the window survives.
 * With a live window, replace graph dimensions by actual Tk dimensions, replace
 * flags with MAP_WORLD and schedule redraw. Does not remap immediately or
 * restore a saved flag set. With a destroyed window, only clear GRAPH_EXPORT.
 */
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

/**
 * @brief Emit graph margins, optional border, margin legend, title and axes.
 * @ingroup graph_export_commands
 *
 * Borrows a graph with mapped export geometry and an initialized context.
 * Creates and ends a temporary renderer context. Decorations control margin
 * background and plot border, not legend/title/axis inclusion. Components
 * supply their own visibility rules. Does not return status; the enclosing
 * traversal inspects the accumulated export error after rendering.
 */
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

/**
 * @brief Traverse mapped graph components into an initialized document context.
 * Borrow both arguments; the caller owns layout, GRAPH_EXPORT state, graph
 * lifetime, document framing and final delivery. Each temporary renderer
 * context is ended here. Track components retain their own scope and clipping.
 * Crosshairs are not exported. No screen geometry restoration is performed.
 * @return TCL_OK, or TCL_ERROR on invalid callback state or a recorded export
 * error. Successful traversal does not imply successful format finalization.
 */
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
