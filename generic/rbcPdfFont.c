/* PDF font resources. Native font loading is shared with SVG in rbcFont.c. */
#include "rbcFont.h"

/* The compact, non-embedded mode intentionally retains the standard-font
 * WinAnsi repertoire. Do not emit native glyph IDs without their font program. */
static int PdfStandardCharacter(Rbc_ExportContext *token, Tk_Font font, Tcl_UniChar ch,
                                int *idPtr, int *codePtr, double *sizePtr) {
    static const unsigned short extra[32] = {
        0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017d, 0,
        0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0, 0x017e, 0x0178
    };
    Tcl_DString name;
    const char *native, *mapped;
    char dictionary[180];
    int code = -1, i, bold, italic;

    if ((ch >= 32 && ch <= 126) || (ch >= 160 && ch <= 255)) {
        code = ch;
    } else {
        for (i = 0; i < 32; i++) {
            if (extra[i] != 0 && ch == extra[i]) {
                code = 128 + i;
                break;
            }
        }
    }
    if (code < 0) {
        token->error = "PDF text contains a character outside Windows-1252; enable -embedfonts";
        return TCL_ERROR;
    }
    Tcl_DStringInit(&name);
    *sizePtr = Tk_PostscriptFontName(font, &name) / Rbc_PdfScale(token);
    native = Tcl_DStringValue(&name);
    bold = strstr(native, "Bold") != NULL;
    italic = strstr(native, "Italic") != NULL || strstr(native, "Oblique") != NULL;
    if (strncmp(native, "Courier", 7) == 0) {
        mapped = bold ? (italic ? "Courier-BoldOblique" : "Courier-Bold") : (italic ? "Courier-Oblique" : "Courier");
    } else if (strncmp(native, "Times", 5) == 0) {
        mapped = bold ? (italic ? "Times-BoldItalic" : "Times-Bold") : (italic ? "Times-Italic" : "Times-Roman");
    } else {
        mapped = bold ? (italic ? "Helvetica-BoldOblique" : "Helvetica-Bold") : (italic ? "Helvetica-Oblique" : "Helvetica");
    }
    snprintf(dictionary, sizeof(dictionary), "<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding >>", mapped);
    *idPtr = Rbc_PdfResource(token, 'F', dictionary, -1);
    *codePtr = code;
    Tcl_DStringFree(&name);
    return TCL_OK;
}

int Rbc_PdfFontCharacter(Rbc_ExportContext *token, Tk_Font tkfont, Tcl_UniChar ch, int *idPtr, int *cidPtr,
                         double *sizePtr, int *digitsPtr, double *advancePtr) {
    Rbc_ExportFont *font;
    int cid, block;
    if (token->error != NULL) {
        return TCL_ERROR;
    }
    if (!token->embedFonts) {
        *digitsPtr = 2;
        *advancePtr = 0;
        return PdfStandardCharacter(token, tkfont, ch, idPtr, cidPtr, sizePtr);
    }
    if (Rbc_ExportFontCharacter(token, tkfont, ch, &font, &cid, sizePtr, advancePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    block = font->isCff ? (cid - 1) / 255 : 0;
    if (font->fontIds[block] == 0) {
        Tcl_Obj *placeholder = Tcl_ObjPrintf("font:%d:block:%d", font->id, block);
        Tcl_IncrRefCount(placeholder);
        font->fontIds[block] = Rbc_PdfResource(token, 'F', Tcl_GetString(placeholder), -1);
        Tcl_DecrRefCount(placeholder);
    }
    *idPtr = font->fontIds[block];
    *cidPtr = font->isCff ? (cid - 1) % 255 + 1 : cid;
    *digitsPtr = font->isCff ? 2 : 4;
    return TCL_OK;
}

/* Simple Type1C fonts have 8-bit codes. Split their encodings into groups of
 * 255 characters while sharing a single embedded CFF program and descriptor. */
static void FontCffResources(Rbc_ExportContext *token, Rbc_ExportFont *font, int descriptor) {
    Tcl_DString body;
    Tcl_DString *saved = token->buffer;
    int first;
    double scale = 1000.0 / font->units;
    Tcl_DStringInit(&body);
    token->buffer = &body;
    for (first = 0; first < font->count; first += 255) {
        int end = MIN(first + 255, font->count), i, unicode;
        Tcl_DStringSetLength(&body, 0);
        Rbc_ExportAppend(
            token,
            "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
            "/CMapName /RBCUnicode def\n/CMapType 2 def\n1 begincodespacerange\n<00> <FF>\nendcodespacerange\n",
            (char *)NULL);
        for (i = first; i < end;) {
            int stop = MIN(i + 100, end);
            Rbc_ExportFormat(token, "%d beginbfchar\n", stop - i);
            while (i < stop) {
                uint32_t ch = (uint32_t)font->characters[i];
                if (ch <= 0xffff) {
                    Rbc_ExportFormat(token, "<%02X> <%04X>\n", i - first + 1, ch);
                } else {
                    ch -= 0x10000;
                    Rbc_ExportFormat(token, "<%02X> <%04X%04X>\n", i - first + 1, 0xd800 + (ch >> 10),
                                     0xdc00 + (ch & 1023));
                }
                i++;
            }
            Rbc_ExportAppend(token, "endbfchar\n", (char *)NULL);
        }
        Rbc_ExportAppend(token, "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n", (char *)NULL);
        unicode = Rbc_PdfStream(token, 'D', "", Tcl_DStringValue(&body), Tcl_DStringLength(&body));
        Tcl_DStringSetLength(&body, 0);
        Rbc_ExportFormat(token,
                         "<< /Type /Font /Subtype /Type1 /BaseFont /%s /FontDescriptor %d 0 R "
                         "/ToUnicode %d 0 R /FirstChar 1 /LastChar %d /Widths [",
                         font->name, descriptor, unicode, end - first);
        for (i = first; i < end; i++) {
            int metric = MIN(font->glyphs[i], font->metricsCount - 1);
            Rbc_ExportFormat(token, " %.4f", Rbc_FontU16(font->hmtx.bytes + 4 * metric) * scale);
        }
        Rbc_ExportAppend(token, " ] /Encoding << /Type /Encoding /Differences [1", (char *)NULL);
        for (i = first; i < end; i++) {
            Rbc_FontTable name = Rbc_FontGlyphName(font, font->glyphs[i]);
            size_t j;
            Rbc_ExportAppend(token, " /", (char *)NULL);
            for (j = 0; j < name.length; j++) {
                unsigned char b = name.bytes[j];
                if (b < 33 || b > 126 || strchr("()<>[]{}/%#", b) != NULL) {
                    Rbc_ExportFormat(token, "#%02X", b);
                } else {
                    Tcl_DStringAppend(&body, (const char *)name.bytes + j, 1);
                }
            }
        }
        Rbc_ExportAppend(token, " ] >> >>", (char *)NULL);
        Rbc_PdfSetResource(token, font->fontIds[first / 255], Tcl_DStringValue(&body), Tcl_DStringLength(&body));
    }
    token->buffer = saved;
    Tcl_DStringFree(&body);
}

/* Emit the font program, descriptor, CID-to-glyph map, widths and ToUnicode.
 * PDF glyph codes and Unicode text extraction have deliberately separate maps. */
int Rbc_PdfFontsFinish(Rbc_ExportContext *token) {
    Rbc_ExportFont *font;
    Tcl_DString body;
    Tcl_DString *saved = token->buffer;
    if (token->error != NULL) {
        return TCL_ERROR;
    }
    Tcl_DStringInit(&body);
    for (font = Rbc_FirstExportFont(token); font != NULL; font = font->next) {
        char dictionary[100];
        int program, descriptor, mapping, unicode, descendant, i;
        unsigned char *map;
        double scale = 1000.0 / font->units;
        Rbc_FontTable post = Rbc_FontGetTable(font, "post"), os2 = Rbc_FontGetTable(font, "OS/2");
        double italic = post.length >= 8 ? (double)(int32_t)Rbc_FontU32(post.bytes + 4) / 65536.0 : 0;
        int flags = (font->isCff ? 4 : 32) | (italic != 0 ? 64 : 0);
        if (post.length >= 16 && Rbc_FontU32(post.bytes + 12) != 0) {
            flags |= 1;
        }
        snprintf(dictionary, sizeof(dictionary), "/Length1 %u", (unsigned int)font->length);
        program = font->isCff ? Rbc_PdfStream(token, 'D', "/Subtype /Type1C", (const char *)font->cff.bytes,
                                              (Tcl_Size)font->cff.length)
                              : Rbc_PdfStream(token, 'D', dictionary, (char *)font->bytes, (Tcl_Size)font->length);
        token->buffer = &body;
        Tcl_DStringSetLength(&body, 0);
        Rbc_ExportFormat(
            token,
            "<< /Type /FontDescriptor /FontName /%s /Flags %d /FontBBox [%.4f %.4f %.4f %.4f] "
            "/ItalicAngle %.4f /Ascent %.4f /Descent %.4f /CapHeight %.4f /StemV 80 /%s %d 0 R >>",
            font->name, flags, Rbc_FontS16(font->head.bytes + 36) * scale, Rbc_FontS16(font->head.bytes + 38) * scale,
            Rbc_FontS16(font->head.bytes + 40) * scale, Rbc_FontS16(font->head.bytes + 42) * scale, italic,
            Rbc_FontS16(font->hhea.bytes + 4) * scale, Rbc_FontS16(font->hhea.bytes + 6) * scale,
            (os2.length >= 90 && Rbc_FontU16(os2.bytes) >= 2 ? Rbc_FontS16(os2.bytes + 88) : Rbc_FontS16(font->hhea.bytes + 4)) *
                scale,
            font->isCff ? "FontFile3" : "FontFile2", program);
        descriptor = Rbc_PdfResource(token, 'D', Tcl_DStringValue(&body), Tcl_DStringLength(&body));
        if (font->isCff) {
            FontCffResources(token, font, descriptor);
            continue;
        }
        map = (unsigned char *)ckalloc(2 * (font->count + 1));
        Rbc_FontPut16(map, 0);
        for (i = 0; i < font->count; i++) {
            Rbc_FontPut16(map + 2 * (i + 1), font->glyphs[i]);
        }
        mapping = Rbc_PdfStream(token, 'D', "", (char *)map, 2 * (font->count + 1));
        ckfree(map);
        Tcl_DStringSetLength(&body, 0);
        Rbc_ExportAppend(
            token,
            "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
            "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
            "/CMapName /RBCUnicode def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n",
            (char *)NULL);
        for (i = 0; i < font->count;) {
            int end = MIN(i + 100, font->count);
            Rbc_ExportFormat(token, "%d beginbfchar\n", end - i);
            while (i < end) {
                uint32_t ch = (uint32_t)font->characters[i];
                if (ch <= 0xffff) {
                    Rbc_ExportFormat(token, "<%04X> <%04X>\n", i + 1, ch);
                } else {
                    ch -= 0x10000;
                    Rbc_ExportFormat(token, "<%04X> <%04X%04X>\n", i + 1, 0xd800 + (ch >> 10), 0xdc00 + (ch & 1023));
                }
                i++;
            }
            Rbc_ExportAppend(token, "endbfchar\n", (char *)NULL);
        }
        Rbc_ExportAppend(token, "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n", (char *)NULL);
        unicode = Rbc_PdfStream(token, 'D', "", Tcl_DStringValue(&body), Tcl_DStringLength(&body));
        Tcl_DStringSetLength(&body, 0);
        Rbc_ExportFormat(token,
                         "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /%s "
                         "/CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> "
                         "/FontDescriptor %d 0 R /CIDToGIDMap %d 0 R /W [1 [",
                         font->name, descriptor, mapping);
        for (i = 0; i < font->count; i++) {
            int metric = MIN(font->glyphs[i], font->metricsCount - 1);
            Rbc_ExportFormat(token, " %.4f", Rbc_FontU16(font->hmtx.bytes + 4 * metric) * scale);
        }
        Rbc_ExportAppend(token, " ]] >>", (char *)NULL);
        descendant = Rbc_PdfResource(token, 'D', Tcl_DStringValue(&body), Tcl_DStringLength(&body));
        Tcl_DStringSetLength(&body, 0);
        Rbc_ExportFormat(token,
                         "<< /Type /Font /Subtype /Type0 /BaseFont /%s /Encoding /Identity-H "
                         "/DescendantFonts [%d 0 R] /ToUnicode %d 0 R >>",
                         font->name, descendant, unicode);
        Rbc_PdfSetResource(token, font->fontIds[0], Tcl_DStringValue(&body), Tcl_DStringLength(&body));
    }
    token->buffer = saved;
    Tcl_DStringFree(&body);
    return TCL_OK;
}
