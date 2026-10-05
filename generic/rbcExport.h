/* Private export document state. See license.terms for details. */
#ifndef RBC_EXPORT_H
#define RBC_EXPORT_H

typedef enum { RBC_EXPORT_POSTSCRIPT, RBC_EXPORT_SVG, RBC_EXPORT_PDF } Rbc_ExportBackend;

typedef struct {
    Rbc_ExportBackend backend;
    Tcl_Interp *interp;
    Tk_Window tkwin;
    Tcl_DString storage;
    Tcl_DString *buffer;
    void *backendData;           /* State owned by the backend command (PostScript token or PDF document). */
    unsigned int nextResourceId; /* Document-local SVG definition identifiers. */
    int decorations;
    const char *error; /* First export error, static message. */
} Rbc_ExportContext;

void Rbc_ExportInit(Rbc_ExportContext *exportPtr, Rbc_ExportBackend backend, Tcl_Interp *interp, Tk_Window tkwin,
                    int decorations);
void Rbc_ExportFree(Rbc_ExportContext *exportPtr);
void Rbc_ExportAppend(Rbc_ExportContext *exportPtr, ...);
void Rbc_ExportFormat(Rbc_ExportContext *exportPtr, const char *format, ...) TCL_FORMAT_PRINTF(2, 3);
/* Private PDF document/resource writer. No Cairo dependency. */
void Rbc_PdfBegin(Rbc_ExportContext *token, double scale, int height);
void Rbc_PdfFree(Rbc_ExportContext *token);
Tcl_Obj *Rbc_PdfDocument(Rbc_ExportContext *token, int width, int height);
int Rbc_PdfResource(Rbc_ExportContext *token, char kind, const char *body, Tcl_Size length);
int Rbc_PdfStream(Rbc_ExportContext *token, char kind, const char *dictionary,
                  const char *bytes, Tcl_Size length);
double Rbc_PdfScale(Rbc_ExportContext *token);
int Rbc_PdfHeight(Rbc_ExportContext *token);
#endif
