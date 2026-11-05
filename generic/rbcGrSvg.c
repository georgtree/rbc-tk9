/**
 * @file rbcGrSvg.c
 * @brief SVG graph command policy and document framing.
 * See license.terms for details.
 */
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

/**
 * @brief Frame SVG, traverse components and finalize font definitions.
 * @ingroup graph_export_commands
 *
 * Requires mapped export geometry and an initialized SVG token. Borrows graph
 * and token; documentPtr is written only on TCL_OK with a new string object
 * at reference count zero. Emits pixel dimensions and matching viewBox.
 * Finalizes fonts only after successful traversal; appends the closing tag
 * even on failure, but publishes no partial document. Reports font errors
 * through the token interpreter. The shared caller owns geometry restoration,
 * document reference management and context cleanup.
 *
 * @param[inout] graphPtr Retained graph mapped at the requested export size, with a live window at entry.
 * @param[inout] token Initialized SVG context receiving content, font resources and errors.
 * @param[out] documentPtr Receives a new string object at reference count zero only on success; unchanged on failure.
 */
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

/**
 * @brief Initialize the graph's named SVG option component.
 * @ingroup graph_export_commands
 *
 * Borrows the graph and delegates to Rbc_InitGraphExportOptions using the
 * static format policy. Returns TCL_OK or TCL_ERROR. Intended for graph
 * initialization; failure is not a rollback transaction on an existing
 * configuration. No Tk option-table resources remain retained afterward.
 *
 * @param[inout] graphPtr Borrowed graph; caller keeps the record alive throughout the operation.
 */
int Rbc_InitSvgOptions(Graph *graphPtr) {
    return Rbc_InitGraphExportOptions(graphPtr, &svgFormat);
}

/**
 * @brief Dispatch the graph svg command through shared export services.
 * @ingroup graph_export_commands
 *
 * Requires objv[2] to name the operation and a live graph protected by the
 * enclosing widget command. Borrows all arguments and delegates cget,
 * configure and output to Rbc_GraphExportOp. Returns its Tcl status and
 * result; this adapter neither retains the graph nor owns backend cleanup.
 *
 * @param[inout] graphPtr Borrowed graph; caller keeps the record alive throughout the operation.
 * @param[inout] interp Result and diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Full widget command argument count, at least three.
 * @param[in] objv Borrowed command vector with the export operation at index two; see @ref rbc_command_arguments.
 */
int Rbc_SvgOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_GraphExportOp(graphPtr, interp, objc, objv, &svgFormat);
}
