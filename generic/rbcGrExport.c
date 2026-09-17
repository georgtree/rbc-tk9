/* Shared graph export traversal. See license.terms for details. */
#include "rbcRender.h"
#include <stdarg.h>

void Rbc_ExportInit(Rbc_ExportContext *exportPtr, Rbc_ExportBackend backend, Tcl_Interp *interp,
                      Tk_Window tkwin, int decorations) {
    memset(exportPtr, 0, sizeof(*exportPtr));
    exportPtr->backend = backend;
    exportPtr->interp = interp;
    exportPtr->tkwin = tkwin;
    exportPtr->decorations = decorations;
    Tcl_DStringInit(&exportPtr->storage);
    exportPtr->buffer = &exportPtr->storage;
}

void Rbc_ExportFree(Rbc_ExportContext *exportPtr) {
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
        if (exportPtr->error == NULL) { exportPtr->error = "cannot format export document"; }
    } else {
        Tcl_DStringAppend(exportPtr->buffer, buffer, length);
    }
    if (buffer != local) { ckfree(buffer); }
}

void Rbc_ExportBeginGraph(Graph *graphPtr) {
    if (graphPtr->height <= 1) { graphPtr->height = Tk_ReqHeight(graphPtr->tkwin); }
    if (graphPtr->width <= 1) { graphPtr->width = Tk_ReqWidth(graphPtr->tkwin); }
    graphPtr->flags |= GRAPH_EXPORT;
}

void Rbc_ExportEndGraph(Graph *graphPtr) {
    graphPtr->flags &= ~GRAPH_EXPORT;
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
    Rbc_RenderBackgroundRectangles(output,
        decorations ? Tk_3DBorderColor(graphPtr->border) : NULL, margin, 4);
    /* Interior 3D border */
    if ((decorations) && (graphPtr->plotBorderWidth > 0)) {
        int x, y, width, height;

        x = graphPtr->left - graphPtr->plotBorderWidth;
        y = graphPtr->top - graphPtr->plotBorderWidth;
        width = (graphPtr->right - graphPtr->left) + (2 * graphPtr->plotBorderWidth);
        height = (graphPtr->bottom - graphPtr->top) + (2 * graphPtr->plotBorderWidth);
        Rbc_RenderBorder(output, graphPtr->border, (double)x, (double)y, width, height,
                                        graphPtr->plotBorderWidth, graphPtr->plotRelief, FALSE);
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
    /* Draw the grid, elements, and markers in the plotting area. */
    if (!graphPtr->gridPtr->hidden) {
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
    Rbc_MarkersExport(graphPtr, exportPtr, TRUE);
    if ((Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_PLOT) && (!Rbc_LegendIsRaised(graphPtr->legend))) {
        /* Print legend underneath elements and markers */
        Rbc_LegendExport(graphPtr->legend, exportPtr);
    }
    Rbc_AxisLimitsExport(graphPtr, exportPtr);
    Rbc_ElementsExport(graphPtr, exportPtr);
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
