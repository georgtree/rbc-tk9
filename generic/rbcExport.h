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

/**
 * @brief Synchronous document export owner with backend and font caches. Window and interpreter are borrowed;
 * cleanup releases backend state and font resources.
 */
typedef struct {
    Rbc_ExportBackend backend; /**< Selected document backend. */
    Tcl_Interp *interp; /**< Borrowed result interpreter. */
    Tk_Window tkwin; /**< Borrowed live Tk window used to resolve display resources. */
    Tcl_DString storage; /**< Inline DString storage used when no external buffer is supplied. */
    Tcl_DString *buffer; /**< Active document buffer; may point to storage or caller-owned storage. */
    void *backendData; /**< State owned by the backend command (PostScript token or PDF document). */
    void *fontData; /**< Document-local font cache shared by PDF and SVG. */
    unsigned int nextResourceId; /**< Document-local SVG definition identifiers. */
    int decorations; /**< Nonzero includes plot decorations. */
    int embedFonts; /**< Nonzero requests embedded font programs. */
    const char *error; /**< First export error, static message. */
} Rbc_ExportContext;

void Rbc_ExportInit(Rbc_ExportContext *exportPtr, Rbc_ExportBackend backend, Tcl_Interp *interp, Tk_Window tkwin,
                    int decorations);
int Rbc_ExportError(Rbc_ExportContext *exportPtr, const char *message);
void Rbc_ExportFree(Rbc_ExportContext *exportPtr);
void Rbc_ExportAppend(Rbc_ExportContext *exportPtr, ...);
void Rbc_ExportFormat(Rbc_ExportContext *exportPtr, const char *format, ...) TCL_FORMAT_PRINTF(2, 3);
/* Private PDF document/resource writer. No Cairo dependency. */
/**
 * @brief Attach a fresh single-page PDF builder and begin its content transform.
 * @param token Initialized export context with no existing backendData.
 * @param scale Caller-validated conversion from screen pixels to PDF points.
 * @param height Target height in screen pixels, used for the Y-axis flip.
 * @details Allocates backendData and appends a graphics-state save and transform
 * to the current buffer. Does not clear earlier content or free an earlier
 * backend. The owner must call Rbc_PdfFree, including after rendering failure.
 */
void Rbc_PdfBegin(Rbc_ExportContext *token, double scale, int height);
/**
 * @brief Release PDF resources and clear backendData; an absent builder is safe.
 * @param token Borrowed export context with PDF or NULL backendData.
 * @details Frees copied resource bodies, the resource array and builder. Does
 * not free fontData, content storage, an alternate buffer or the token itself.
 * Finish all renderer/font operations before releasing their builder.
 */
void Rbc_PdfFree(Rbc_ExportContext *token);
/**
 * @brief Finalize fonts and serialize one PDF document into a binary Tcl object.
 * @param token Live PDF export context with completed drawing in its buffer.
 * @param width Page width in screen pixels.
 * @param height Page height in screen pixels.
 * @return New byte-array object at reference count zero, or NULL on failure.
 * @details Appends the final graphics-state restore to content and serializes
 * PDF 1.4 objects, classic xref and trailer. Restores the original buffer pointer
 * and frees temporary serialization storage. Resources/content remain owned by
 * the context. This is a single-use finalizer, not an idempotent query or retry
 * API. No file is opened. The caller checks export diagnostics and owns result
 * delivery/reference management.
 */
Tcl_Obj *Rbc_PdfDocument(Rbc_ExportContext *token, int width, int height);
/**
 * @brief Intern a copied PDF object body within the current document.
 * @param token Live PDF builder.
 * @param kind Resource category; part of the interning key.
 * @param body Borrowed object-body bytes, copied for a new entry.
 * @param length Explicit byte count, or negative for a NUL-terminated string.
 * @return Stable object number, starting at six; identical kind/bytes reuse it.
 * @details Does not parse or validate PDF syntax. Binary bodies require explicit
 * lengths. Resource numbers are meaningful only within this builder. Allocation
 * uses Tcl's non-recoverable allocation functions rather than an error return.
 */
int Rbc_PdfResource(Rbc_ExportContext *token, char kind, const char *body, Tcl_Size length);
/**
 * @brief Wrap binary payload bytes in a PDF stream and intern the object.
 * @param token Live PDF builder with a valid current buffer.
 * @param kind Category passed to Rbc_PdfResource.
 * @param dictionary Borrowed NUL-terminated dictionary entries without delimiters.
 * @param bytes Borrowed readable payload; embedded NULs are allowed.
 * @param length Nonnegative payload byte count supplied by the caller.
 * @return Resource object number for the constructed stream body.
 * @details Temporarily redirects buffer, emits Length, copies the payload,
 * restores buffer and frees temporary storage. Adds no compression or encoding;
 * supplied dictionary filters must match the supplied bytes. The returned ID
 * does not imply that the context has no previously recorded export error.
 */
int Rbc_PdfStream(Rbc_ExportContext *token, char kind, const char *dictionary,
                  const char *bytes, Tcl_Size length);
/**
 * @brief Replace a reserved object's body without changing its ID or category.
 * @param token Live PDF builder.
 * @param id Existing object number returned by this builder, at least six.
 * @param body Borrowed replacement bytes, not aliased with the destination body.
 * @param length Byte count, or negative for a NUL-terminated string.
 * @details Used to resolve font placeholders after character collection. Copies
 * the new body and discards the previous one; does not validate id or re-intern
 * the result. An ID shared through earlier interning names the same object for
 * every reference, so placeholders intended to differ must be distinct.
 */
void Rbc_PdfSetResource(Rbc_ExportContext *token, int id, const char *body, Tcl_Size length);
int Rbc_PdfFontCharacter(Rbc_ExportContext *token, Tk_Font font, Tcl_UniChar ch,
                         int *idPtr, int *cidPtr, double *sizePtr, int *digitsPtr, double *advancePtr);
int Rbc_PdfFontsFinish(Rbc_ExportContext *token);
int Rbc_SvgFontsFinish(Rbc_ExportContext *token);
/**
 * @brief Read a stored PDF page metric without changing builder state.
 * @param[in] token Live export context with an initialized PDF builder; backendData must not be NULL.
 * @return Pixel-to-point scale recorded by Rbc_PdfBegin.
 * Does not query Tk, infer current graph dimensions or validate the stored metric.
 */
double Rbc_PdfScale(Rbc_ExportContext *token);
/**
 * @brief Read a stored PDF page metric without changing builder state.
 * @param[in] token Live export context with an initialized PDF builder; backendData must not be NULL.
 * @return Page height in display pixels recorded by Rbc_PdfBegin.
 * Does not query Tk, infer current graph dimensions or validate the stored metric.
 */
int Rbc_PdfHeight(Rbc_ExportContext *token);
#endif
