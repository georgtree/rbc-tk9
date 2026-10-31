/**
 * @file rbcExport.h
 * @brief Private document state and serialization services.
 *
 * Contexts belong to one synchronous export on the owning Tk thread. They do
 * not preserve the interpreter, window or graph and are not public vector API.
 * See license.terms for details.
 */
#ifndef RBC_EXPORT_H
#define RBC_EXPORT_H

#include <tcl.h>
#include <tk.h>

typedef enum { RBC_EXPORT_POSTSCRIPT, RBC_EXPORT_SVG, RBC_EXPORT_PDF } Rbc_ExportBackend;

typedef struct {
    Rbc_ExportBackend backend;
    Tcl_Interp *interp;
    Tk_Window tkwin;
    Tcl_DString storage;
    Tcl_DString *buffer;
    void *backendData;           /* State owned by the backend command (PostScript token or PDF document). */
    void *fontData;              /* Document-local font cache shared by PDF and SVG. */
    unsigned int nextResourceId; /* Document-local SVG definition identifiers. */
    int decorations, embedFonts;
    const char *error; /* First export error, static message. */
} Rbc_ExportContext;

/**
 * @brief Initialize fresh caller-owned export storage.
 * Zero the record, borrow interp and tkwin, and select the embedded DString as
 * the append destination. Call only on an uninitialized or already freed record;
 * this does not release an earlier document. Font embedding starts disabled.
 * The caller supplies backend state and any alternate buffer after this call.
 */
void Rbc_ExportInit(Rbc_ExportContext *exportPtr, Rbc_ExportBackend backend, Tcl_Interp *interp, Tk_Window tkwin,
                    int decorations);
/**
 * @brief Retain the first export failure without replacing later diagnostics.
 * @param exportPtr Initialized, borrowed context.
 * @param message Borrowed static string; it is neither copied nor freed.
 * @return TCL_ERROR. This helper does not set the interpreter result.
 */
int Rbc_ExportError(Rbc_ExportContext *exportPtr, const char *message);
/**
 * @brief Release document-local font data and the embedded DString.
 * Does not free the caller's context, backendData, or an alternate buffer.
 * The format owner must release backend resources separately. No renderer
 * context may still be using these resources.
 */
void Rbc_ExportFree(Rbc_ExportContext *exportPtr);
/**
 * @brief Append a NULL-terminated sequence of C strings to the current buffer.
 * The initialized context borrows buffer; callers must keep that DString alive.
 * This helper neither checks nor clears an existing export error.
 */
void Rbc_ExportAppend(Rbc_ExportContext *exportPtr, ...);
/**
 * @brief Append printf-formatted text to the current document buffer.
 * Uses temporary heap storage when the local formatting buffer is too small.
 * A formatting failure records a static export error; it does not set the Tcl
 * result. The caller must check the context's error before delivering output.
 */
void Rbc_ExportFormat(Rbc_ExportContext *exportPtr, const char *format, ...) TCL_FORMAT_PRINTF(2, 3);
/* Private PDF document/resource writer. No Cairo dependency. */
void Rbc_PdfBegin(Rbc_ExportContext *token, double scale, int height);
void Rbc_PdfFree(Rbc_ExportContext *token);
Tcl_Obj *Rbc_PdfDocument(Rbc_ExportContext *token, int width, int height);
int Rbc_PdfResource(Rbc_ExportContext *token, char kind, const char *body, Tcl_Size length);
int Rbc_PdfStream(Rbc_ExportContext *token, char kind, const char *dictionary,
                  const char *bytes, Tcl_Size length);
void Rbc_PdfSetResource(Rbc_ExportContext *token, int id, const char *body, Tcl_Size length);
int Rbc_PdfFontCharacter(Rbc_ExportContext *token, Tk_Font font, Tcl_UniChar ch,
                         int *idPtr, int *cidPtr, double *sizePtr, int *digitsPtr, double *advancePtr);
int Rbc_PdfFontsFinish(Rbc_ExportContext *token);
int Rbc_SvgFontsFinish(Rbc_ExportContext *token);
double Rbc_PdfScale(Rbc_ExportContext *token);
int Rbc_PdfHeight(Rbc_ExportContext *token);
#endif
