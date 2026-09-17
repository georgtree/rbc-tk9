/* Private export document state. See license.terms for details. */
#ifndef RBC_EXPORT_H
#define RBC_EXPORT_H

typedef enum { RBC_EXPORT_POSTSCRIPT, RBC_EXPORT_SVG } Rbc_ExportBackend;

typedef struct {
    Rbc_ExportBackend backend;
    Tcl_Interp *interp;
    Tk_Window tkwin;
    Tcl_DString storage;
    Tcl_DString *buffer;
    void *backendData; /* Borrowed backend state; PostScript uses its legacy token. */
    int decorations;
    const char *error; /* First export error, static message. */
} Rbc_ExportContext;

void Rbc_ExportInit(Rbc_ExportContext *exportPtr, Rbc_ExportBackend backend, Tcl_Interp *interp,
                      Tk_Window tkwin, int decorations);
void Rbc_ExportFree(Rbc_ExportContext *exportPtr);
void Rbc_ExportAppend(Rbc_ExportContext *exportPtr, ...);
void Rbc_ExportFormat(Rbc_ExportContext *exportPtr, const char *format, ...);
#endif
