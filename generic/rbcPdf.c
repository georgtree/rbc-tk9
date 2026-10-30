/*
 * rbcPdf.c --
 *
 *      PDF resources, streams and document serialization.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include "rbcExport.h"

/* PDF resources own their byte strings; IDs 1..5 are the document skeleton. */
typedef struct {
    char kind;
    Tcl_DString body;
} PdfResource;

typedef struct {
    PdfResource **resources;
    int count, capacity, height;
    double scale;
} PdfDocument;

void Rbc_PdfBegin(Rbc_ExportContext *token, double scale, int height) {
    PdfDocument *doc = (PdfDocument *)ckalloc(sizeof(*doc));
    memset(doc, 0, sizeof(*doc));
    doc->scale = scale;
    doc->height = height;
    token->backendData = doc;
    Rbc_ExportFormat(token, "q\n%.8f 0 0 %.8f 0 %.8f cm\n", scale, -scale, height * scale);
}

double Rbc_PdfScale(Rbc_ExportContext *token) { return ((PdfDocument *)token->backendData)->scale; }
int Rbc_PdfHeight(Rbc_ExportContext *token) { return ((PdfDocument *)token->backendData)->height; }

void Rbc_PdfFree(Rbc_ExportContext *token) {
    PdfDocument *doc = (PdfDocument *)token->backendData;
    int i;
    if (doc == NULL) {
        return;
    }
    for (i = 0; i < doc->count; i++) {
        Tcl_DStringFree(&doc->resources[i]->body);
        ckfree(doc->resources[i]);
    }
    if (doc->resources != NULL) {
        ckfree(doc->resources);
    }
    ckfree(doc);
    token->backendData = NULL;
}

/* Intern identical fonts, opacity states and images within one document. */
int Rbc_PdfResource(Rbc_ExportContext *token, char kind, const char *body, Tcl_Size length) {
    PdfDocument *doc = (PdfDocument *)token->backendData;
    PdfResource *resource;
    int i;

    if (length < 0) {
        length = (Tcl_Size)strlen(body);
    }
    for (i = 0; i < doc->count; i++) {
        resource = doc->resources[i];
        if (resource->kind == kind && Tcl_DStringLength(&resource->body) == length &&
            memcmp(Tcl_DStringValue(&resource->body), body, (size_t)length) == 0) {
            return i + 6;
        }
    }
    if (doc->count == doc->capacity) {
        int capacity = doc->capacity + 32;
        doc->resources = (PdfResource **)ckrealloc(doc->resources, (size_t)capacity * sizeof(*doc->resources));
        doc->capacity = capacity;
    }
    resource = (PdfResource *)ckalloc(sizeof(*resource));
    doc->resources[doc->count] = resource;
    resource->kind = kind;
    Tcl_DStringInit(&resource->body);
    Tcl_DStringAppend(&resource->body, body, length);
    return doc->count++ + 6;
}

/* Resolve a reserved font object after all of its character codes are known. */
void Rbc_PdfSetResource(Rbc_ExportContext *token, int id, const char *body, Tcl_Size length) {
    PdfDocument *doc = (PdfDocument *)token->backendData;
    Tcl_DString *target = &doc->resources[id - 6]->body;
    Tcl_DStringSetLength(target, 0);
    Tcl_DStringAppend(target, body, length);
}

/* Stream lengths always count bytes, including embedded NULs. */
int Rbc_PdfStream(Rbc_ExportContext *token, char kind, const char *dictionary, const char *bytes, Tcl_Size length) {
    Tcl_DString body;
    Tcl_DString *saved = token->buffer;
    int id;

    Tcl_DStringInit(&body);
    token->buffer = &body;
    Rbc_ExportFormat(token, "<< %s /Length %" TCL_SIZE_MODIFIER "d >>\nstream\n", dictionary, length);
    Tcl_DStringAppend(&body, bytes, length);
    Rbc_ExportAppend(token, "\nendstream", (char *)NULL);
    token->buffer = saved;
    id = Rbc_PdfResource(token, kind, Tcl_DStringValue(&body), Tcl_DStringLength(&body));
    Tcl_DStringFree(&body);
    return id;
}

/* Build a deterministic PDF 1.4 document after all drawing has succeeded. */
Tcl_Obj *Rbc_PdfDocument(Rbc_ExportContext *token, int width, int height) {
    PdfDocument *doc = (PdfDocument *)token->backendData;
    Tcl_DString document;
    Tcl_DString *content = token->buffer;
    Tcl_WideInt *offsets, xref;
    Tcl_Obj *result = NULL;
    int i, id, total;
    static const char kinds[] = {'F', 'G', 'I', 'P'};
    static const char *names[] = {"Font", "ExtGState", "XObject", "Pattern"};

    if (Rbc_PdfFontsFinish(token) != TCL_OK) {
        return NULL;
    }
    total = doc->count + 6;
    Rbc_ExportAppend(token, "Q\n", (char *)NULL);
    offsets = (Tcl_WideInt *)ckalloc((size_t)total * sizeof(*offsets));
    Tcl_DStringInit(&document);
    token->buffer = &document;
    Rbc_ExportAppend(token, "%PDF-1.4\n%\342\343\317\323\n", (char *)NULL);
    for (id = 1; id < total; id++) {
        offsets[id] = Tcl_DStringLength(&document);
        Rbc_ExportFormat(token, "%d 0 obj\n", id);
        switch (id) {
        case 1:
            Rbc_ExportAppend(token, "<< /Type /Catalog /Pages 2 0 R >>", (char *)NULL);
            break;
        case 2:
            Rbc_ExportAppend(token, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>", (char *)NULL);
            break;
        case 3:
            Rbc_ExportFormat(token,
                             "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %.8f %.8f] "
                             "/Resources 4 0 R /Contents 5 0 R >>",
                             width * doc->scale, height * doc->scale);
            break;
        case 4:
            Rbc_ExportAppend(token, "<<", (char *)NULL);
            for (i = 0; i < 4; i++) {
                int j;
                Rbc_ExportFormat(token, " /%s <<", names[i]);
                for (j = 0; j < doc->count; j++) {
                    if (doc->resources[j]->kind == kinds[i]) {
                        Rbc_ExportFormat(token, " /%c%d %d 0 R", kinds[i], j + 6, j + 6);
                    }
                }
                Rbc_ExportAppend(token, " >>", (char *)NULL);
            }
            Rbc_ExportAppend(token, " >>", (char *)NULL);
            break;
        case 5:
            Rbc_ExportFormat(token, "<< /Length %" TCL_SIZE_MODIFIER "d >>\nstream\n", Tcl_DStringLength(content));
            Tcl_DStringAppend(&document, Tcl_DStringValue(content), Tcl_DStringLength(content));
            Rbc_ExportAppend(token, "\nendstream", (char *)NULL);
            break;
        default:
            Tcl_DStringAppend(&document, Tcl_DStringValue(&doc->resources[id - 6]->body),
                              Tcl_DStringLength(&doc->resources[id - 6]->body));
        }
        Rbc_ExportAppend(token, "\nendobj\n", (char *)NULL);
    }
    xref = Tcl_DStringLength(&document);
    if (xref > 9999999999LL) {
        Rbc_ExportError(token, "PDF exceeds classic cross-reference size limit");
    } else {
        Rbc_ExportFormat(token, "xref\n0 %d\n0000000000 65535 f \n", total);
        for (id = 1; id < total; id++) {
            Rbc_ExportFormat(token, "%010" TCL_LL_MODIFIER "d 00000 n \n", offsets[id]);
        }
        Rbc_ExportFormat(token, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%" TCL_LL_MODIFIER "d\n%%%%EOF\n",
                         total, xref);
        if (token->error == NULL) {
            result =
                Tcl_NewByteArrayObj((const unsigned char *)Tcl_DStringValue(&document), Tcl_DStringLength(&document));
        }
    }
    token->buffer = content;
    ckfree(offsets);
    Tcl_DStringFree(&document);
    return result;
}
