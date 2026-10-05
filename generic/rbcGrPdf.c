/* PDF command and document lifecycle. See license.terms for details. */
#include "rbcRender.h"

/* PDF options intentionally exclude printer/page-specific PostScript settings. */
static const char *const pdfOptions[] = {"-width", "-height", "-decorations", NULL};

static Tcl_Obj *PdfOptionInfo(Graph *graphPtr, int index) {
    static const char *const names[] = {"width", "height", "decorations"};
    static const char *const classes[] = {"Width", "Height", "Decorations"};
    int value = index == 0 ? graphPtr->pdfWidth : (index == 1 ? graphPtr->pdfHeight : graphPtr->pdfDecorations);
    Tcl_Obj *items[5];

    items[0] = Tcl_NewStringObj(pdfOptions[index], -1);
    items[1] = Tcl_NewStringObj(names[index], -1);
    items[2] = Tcl_NewStringObj(classes[index], -1);
    items[3] = Tcl_NewIntObj(index == 2 ? 1 : 0);
    items[4] = Tcl_NewIntObj(value);
    return Tcl_NewListObj(5, items);
}

static int SetPdfOptions(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int settings[3] = {graphPtr->pdfWidth, graphPtr->pdfHeight, graphPtr->pdfDecorations};
    Tcl_Size i;

    if (objc % 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("PDF options must be option/value pairs", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i += 2) {
        int index, value;
        if (Tcl_GetIndexFromObj(interp, objv[i], pdfOptions, "PDF option", 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (index == 2) {
            if (Tcl_GetBooleanFromObj(interp, objv[i + 1], &value) != TCL_OK) {
                return TCL_ERROR;
            }
        } else {
            if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[i + 1], &value) != TCL_OK) {
                return TCL_ERROR;
            }
            if (value < 0) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("PDF dimensions must be zero or positive", -1));
                return TCL_ERROR;
            }
        }
        settings[index] = value;
    }
    graphPtr->pdfWidth = settings[0];
    graphPtr->pdfHeight = settings[1];
    graphPtr->pdfDecorations = settings[2];
    return TCL_OK;
}

int Rbc_PdfOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *const commands[] = {"cget", "configure", "output", NULL};
    int command, index, result, screenWidth, screenHeight;
    Tcl_Size optionIndex;
    Tcl_Obj *fileName = NULL, *document = NULL;
    Rbc_ExportContext storage, *token = &storage;

    if (Tcl_GetIndexFromObj(interp, objv[2], commands, "PDF operation", 0, &command) != TCL_OK) {
        return TCL_ERROR;
    }
    if (command == 0 || (command == 1 && objc == 4)) {
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 3, objv, "option");
            return TCL_ERROR;
        }
        if (Tcl_GetIndexFromObj(interp, objv[3], pdfOptions, "PDF option", 0, &index) != TCL_OK) {
            return TCL_ERROR;
        }
        if (command == 0) {
            int value = index == 0 ? graphPtr->pdfWidth : (index == 1 ? graphPtr->pdfHeight : graphPtr->pdfDecorations);
            Tcl_SetObjResult(interp, Tcl_NewIntObj(value));
        } else {
            Tcl_SetObjResult(interp, PdfOptionInfo(graphPtr, index));
        }
        return TCL_OK;
    }
    if (command == 1) {
        if (objc == 3) {
            Tcl_Obj *list = Tcl_NewListObj(0, NULL);
            for (index = 0; index < 3; index++) {
                Tcl_ListObjAppendElement(interp, list, PdfOptionInfo(graphPtr, index));
            }
            Tcl_SetObjResult(interp, list);
            return TCL_OK;
        }
        return SetPdfOptions(graphPtr, interp, objc - 3, objv + 3);
    }
    optionIndex = 3;
    if (objc > 3 && Tcl_GetString(objv[3])[0] != '-') {
        fileName = objv[3];
        optionIndex = 4;
    }
    if (SetPdfOptions(graphPtr, interp, objc - optionIndex, objv + optionIndex) != TCL_OK) {
        return TCL_ERROR;
    }
    Rbc_ExportInit(token, RBC_EXPORT_PDF, interp, graphPtr->tkwin, graphPtr->pdfDecorations);
    screenWidth = graphPtr->width;
    screenHeight = graphPtr->height;
    Rbc_ExportBeginGraph(graphPtr);
    if (graphPtr->pdfWidth > 0) {
        graphPtr->width = graphPtr->pdfWidth;
    }
    if (graphPtr->pdfHeight > 0) {
        graphPtr->height = graphPtr->pdfHeight;
    }
    graphPtr->flags |= LAYOUT_NEEDED | MAP_WORLD;
    Rbc_LayoutGraph(graphPtr);
    /* Tk screen distances become physical PDF points (1/72 inch). */
    Rbc_PdfBegin(
        token, 72.0 * WidthMMOfScreen(Tk_Screen(graphPtr->tkwin)) / (25.4 * WidthOfScreen(Tk_Screen(graphPtr->tkwin))),
        graphPtr->height);
    result = Rbc_ExportGraph(graphPtr, token);
    if (result == TCL_OK) {
        document = Rbc_PdfDocument(token, graphPtr->width, graphPtr->height);
        if (document == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(token->error, -1));
            result = TCL_ERROR;
        } else {
            Tcl_IncrRefCount(document);
        }
    }
    Rbc_ExportEndGraph(graphPtr);
    /* Restore margins as well as axis mapping before returning, including errors. */
    graphPtr->width = screenWidth;
    graphPtr->height = screenHeight;
    graphPtr->flags |= LAYOUT_NEEDED | MAP_WORLD;
    Rbc_LayoutGraph(graphPtr);
    if (result == TCL_OK && fileName == NULL) {
        Tcl_SetObjResult(interp, document);
    } else if (result == TCL_OK) {
        Tcl_Channel channel;

        /* Validate/render before opening the destination, including unsupported features. */
        channel = Tcl_FSOpenFileChannel(interp, fileName, "w", 0666);
        if (channel == NULL) {
            result = TCL_ERROR;
        } else {
            if (Tcl_SetChannelOption(interp, channel, "-encoding", "iso8859-1") != TCL_OK ||
                Tcl_SetChannelOption(interp, channel, "-translation", "binary") != TCL_OK) {
                result = TCL_ERROR;
            } else if (Tcl_WriteObj(channel, document) < 0) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("error writing PDF: %s", Tcl_PosixError(interp)));
                result = TCL_ERROR;
            }
            if (result != TCL_OK) {
                Tcl_InterpState saved = Tcl_SaveInterpState(interp, result);
                Tcl_Close(NULL, channel);
                Tcl_RestoreInterpState(interp, saved);
            } else {
                result = Tcl_Close(interp, channel);
                if (result == TCL_OK) {
                    Tcl_ResetResult(interp);
                }
            }
        }
    }
    if (document != NULL) {
        Tcl_DecrRefCount(document);
    }
    Rbc_PdfFree(token);
    Rbc_ExportFree(token);
    return result;
}

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
    int i, id, total = doc->count + 6;
    static const char kinds[] = {'F', 'G', 'I', 'P'};
    static const char *names[] = {"Font", "ExtGState", "XObject", "Pattern"};

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
        token->error = "PDF exceeds classic cross-reference size limit";
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
