/**
 * @file rbcGrPdf.c
 * @brief PDF graph command policy and document framing.
 * See license.terms for details.
 */
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

/**
 * @brief Initialize the PDF builder, traverse components and serialize bytes.
 * @ingroup graph_export_commands
 *
 * Requires mapped export geometry and an initialized PDF token. Computes
 * points per pixel from horizontal screen pixel/mm dimensions, and supplies
 * the graph height to the builder. Borrows graph and token; on TCL_OK stores
 * a new byte-array object at reference count zero in documentPtr. Traversal
 * failure skips serialization; serializer failure stores NULL and reports
 * the recorded token error. The shared caller releases backend resources
 * even after failure, restores geometry and owns result delivery.
 */
static int PdfDocument(Graph *graphPtr, Rbc_ExportContext *token, Tcl_Obj **documentPtr) {
    int result;

    /* Tk screen distances become physical PDF points (1/72 inch). */
    Rbc_PdfBegin(
        token, 72.0 * WidthMMOfScreen(Tk_Screen(graphPtr->tkwin)) / (25.4 * WidthOfScreen(Tk_Screen(graphPtr->tkwin))),
        graphPtr->height);
    result = Rbc_ExportGraph(graphPtr, token);
    if (result == TCL_OK) {
        *documentPtr = Rbc_PdfDocument(token, graphPtr->width, graphPtr->height);
        if (*documentPtr == NULL) {
            Tcl_SetObjResult(token->interp, Tcl_NewStringObj(token->error, -1));
            result = TCL_ERROR;
        }
    }
    return result;
}

static const Rbc_GraphExportFormat pdfFormat = {
    "pdf", "Pdf", "PDF", "PDF option", "PDF operation", pdfOptionSpecs, RBC_EXPORT_PDF,
    offsetof(Graph, pdfWidth), offsetof(Graph, pdfHeight), offsetof(Graph, pdfDecorations),
    offsetof(Graph, pdfEmbedFonts), "iso8859-1", "binary", PdfDocument, Rbc_PdfFree
};

/**
 * @brief Initialize the graph's named PDF option component.
 * @ingroup graph_export_commands
 *
 * Borrows the graph and delegates to Rbc_InitGraphExportOptions using the
 * static format policy. Returns TCL_OK or TCL_ERROR. Intended for graph
 * initialization; failure is not a rollback transaction on an existing
 * configuration. No Tk option-table resources remain retained afterward.
 */
int Rbc_InitPdfOptions(Graph *graphPtr) {
    return Rbc_InitGraphExportOptions(graphPtr, &pdfFormat);
}

/**
 * @brief Dispatch the graph pdf command through shared export services.
 * @ingroup graph_export_commands
 *
 * Requires objv[2] to name the operation and a live graph protected by the
 * enclosing widget command. Borrows all arguments and delegates cget,
 * configure and output to Rbc_GraphExportOp. Returns its Tcl status and
 * result; this adapter neither retains the graph nor owns backend cleanup.
 */
int Rbc_PdfOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_GraphExportOp(graphPtr, interp, objc, objv, &pdfFormat);
}
