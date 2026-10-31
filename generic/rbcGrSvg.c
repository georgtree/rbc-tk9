/* SVG command and document lifecycle. See license.terms for details. */
#include "rbcRender.h"

/* SVG options intentionally exclude printer/page-specific PostScript settings. */
static const Tk_OptionSpec svgOptionSpecs[] = {
    {TK_OPTION_PIXELS, "-width", "width", "Width", "0", -1, offsetof(Graph, svgWidth), 0, NULL, 0},
    {TK_OPTION_PIXELS, "-height", "height", "Height", "0", -1, offsetof(Graph, svgHeight), 0, NULL, 0},
    {TK_OPTION_BOOLEAN, "-decorations", "decorations", "Decorations", "1", -1, offsetof(Graph, svgDecorations), 0,
     NULL, 0},
    {TK_OPTION_BOOLEAN, "-embedfonts", "embedFonts", "EmbedFonts", "0", -1, offsetof(Graph, svgEmbedFonts), 0, NULL,
     0},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/* Build only the format-specific document; the shared command owns layout,
 * delivery and cleanup, including failures caused by user callbacks. */
static int SvgDocument(Graph *graphPtr, Rbc_ExportContext *token, Tcl_Obj **documentPtr) {
    int result;

    Rbc_ExportFormat(token,
                     "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                     "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" viewBox=\"0 0 %d %d\">\n",
                     graphPtr->width, graphPtr->height, graphPtr->width, graphPtr->height);
    result = Rbc_ExportGraph(graphPtr, token);
    if (result == TCL_OK && Rbc_SvgFontsFinish(token) != TCL_OK) {
        Tcl_SetObjResult(token->interp, Tcl_NewStringObj(token->error, -1));
        result = TCL_ERROR;
    }
    Rbc_ExportAppend(token, "</svg>\n", (char *)NULL);
    if (result == TCL_OK) {
        *documentPtr = Tcl_NewStringObj(Tcl_DStringValue(token->buffer), Tcl_DStringLength(token->buffer));
    }
    return result;
}

static const Rbc_GraphExportFormat svgFormat = {
    "svg", "Svg", "SVG", "SVG option", "SVG operation", svgOptionSpecs, RBC_EXPORT_SVG,
    offsetof(Graph, svgWidth), offsetof(Graph, svgHeight), offsetof(Graph, svgDecorations),
    offsetof(Graph, svgEmbedFonts), "utf-8", "lf", SvgDocument, NULL
};

int Rbc_InitSvgOptions(Graph *graphPtr) {
    return Rbc_InitGraphExportOptions(graphPtr, &svgFormat);
}

int Rbc_SvgOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_GraphExportOp(graphPtr, interp, objc, objv, &svgFormat);
}
