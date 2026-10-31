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

/* Build only the format-specific document; the shared command owns layout,
 * delivery and cleanup, including failures caused by user callbacks. */
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

int Rbc_InitPdfOptions(Graph *graphPtr) {
    return Rbc_InitGraphExportOptions(graphPtr, &pdfFormat);
}

int Rbc_PdfOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_GraphExportOp(graphPtr, interp, objc, objv, &pdfFormat);
}
