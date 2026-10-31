/** @file rbcFont.h
 * @brief Private native font programs shared by document exporters.
 * See license.terms for details.
 */
#ifndef RBC_FONT_H
#define RBC_FONT_H
#include "rbcRender.h"

typedef struct {
    const unsigned char *bytes;
    size_t length;
} Rbc_FontTable;

typedef struct Rbc_ExportFont {
    struct Rbc_ExportFont *next;
    char *key;
    unsigned char *bytes;
    size_t length;
    Rbc_FontTable head, hhea, hmtx, cmap, maxp;
    int id, units, glyphCount, metricsCount, count, capacity;
    int isCff;
    int fontIds[258]; /* Backend resource IDs; zero until the PDF writer reserves them. */
    Rbc_FontTable cff, cffStrings;
    unsigned short *sids;
    int *glyphs;
    Tcl_UniChar *characters;
    Tcl_HashTable codes;
    Tcl_Encoding symbolEncoding;
    char name[128];
} Rbc_ExportFont;

unsigned int Rbc_FontU16(const unsigned char *p);
uint32_t Rbc_FontU32(const unsigned char *p);
int Rbc_FontS16(const unsigned char *p);
void Rbc_FontPut16(unsigned char *p, unsigned int n);
Rbc_FontTable Rbc_FontGetTable(Rbc_ExportFont *font, const char *tag);
Rbc_FontTable Rbc_FontGlyphName(Rbc_ExportFont *font, int glyph);
/**
 * @brief Return the borrowed head of the document font list, or NULL.
 * Follow next pointers only while the document font cache remains alive.
 * This does not initialize a cache or transfer ownership.
 */
Rbc_ExportFont *Rbc_FirstExportFont(Rbc_ExportContext *token);
/**
 * @brief Resolve a Unicode scalar into a document-local font and character ID.
 * Borrow token and tkfont. Requires a live Tk window and interpreter. May call
 * the global Tcl font command to resolve Tk's per-character fallback face;
 * this helper does not itself preserve graph lifetime.
 * On TCL_OK, return a borrowed font, a one-based character ID, pixel size and
 * horizontal advance. The ID is not a native glyph index. Outputs are valid
 * only on success; the font remains owned by token until ExportFontsFree.
 * On TCL_ERROR, retain the first static export diagnostic. Partial cache
 * allocations remain valid for cleanup; do not continue serialization.
 */
int Rbc_ExportFontCharacter(Rbc_ExportContext *token, Tk_Font tkfont, Tcl_UniChar ch,
                            Rbc_ExportFont **fontPtr, int *cidPtr, double *sizePtr, double *advancePtr);
/**
 * @brief Release all document-local font programs and character cache entries.
 * Clears token->fontData; an empty cache is harmless. Invalidates all borrowed
 * font records and table views. Does not free backend PDF resources, the
 * document buffer, the export context or any borrowed Tk font.
 */
void Rbc_ExportFontsFree(Rbc_ExportContext *token);
/**
 * @brief Append an SVG font program with Unicode mappings for used characters.
 * Borrow token and a populated export font. The caller initializes program and
 * owns its DString storage. Rebuilds a temporary sfnt with replacement cmap;
 * retains outlines rather than subsetting them, and leaves font unchanged.
 * Returns TCL_OK after appending, or TCL_ERROR with an export diagnostic.
 * Temporary program allocations are released on both paths.
 */
int Rbc_FontUnicodeProgram(Rbc_ExportContext *token, Rbc_ExportFont *font, Tcl_DString *program);
#endif
