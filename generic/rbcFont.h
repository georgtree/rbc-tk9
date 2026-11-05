/** @file rbcFont.h
 * @brief Private native font programs shared by document exporters.
 * See license.terms for details.
 */
#ifndef RBC_FONT_H
#define RBC_FONT_H
#include "rbcRender.h"

/** @brief Borrowed byte view into a font program; invalidated when its owner releases the program. */
typedef struct {
    const unsigned char *bytes; /**< First table byte, or NULL for an absent table. */
    size_t length; /**< Number of available bytes. */
} Rbc_FontTable;

/**
 * @brief Document-owned font program, parsed table views and character mapping. Table views borrow bytes;
 * export-font cleanup releases the program and auxiliary arrays.
 */
typedef struct Rbc_ExportFont {
    struct Rbc_ExportFont *next; /**< Next font in the document cache, or NULL. */
    char *key; /**< Owned native-face cache key. */
    unsigned char *bytes; /**< Owned sfnt font program. */
    size_t length; /**< Font program length in bytes. */
    Rbc_FontTable head; /**< Borrowed font-header table. */
    Rbc_FontTable hhea; /**< Borrowed horizontal-header table. */
    Rbc_FontTable hmtx; /**< Borrowed horizontal-metrics table. */
    Rbc_FontTable cmap; /**< Borrowed character-map table. */
    Rbc_FontTable maxp; /**< Borrowed maximum-profile table. */
    int id; /**< Document-local font identifier. */
    int units; /**< Font design units per em. */
    int glyphCount; /**< Number of glyphs in the source program. */
    int metricsCount; /**< Number of long horizontal metrics. */
    int count; /**< Number of assigned document character IDs. */
    int capacity; /**< Allocated capacity of character/glyph mapping arrays. */
    int isCff; /**< Nonzero for CFF outlines instead of TrueType outlines. */
    int fontIds[258]; /**< Backend resource IDs; zero until the PDF writer reserves them. */
    Rbc_FontTable cff; /**< Borrowed CFF table. */
    Rbc_FontTable cffStrings; /**< Borrowed CFF string INDEX. */
    unsigned short *sids; /**< Owned CFF glyph-name string-ID array. */
    int *glyphs; /**< Owned native-glyph array indexed by document character ID minus one. */
    Tcl_UniChar *characters; /**< Owned Unicode array indexed by document character ID minus one. */
    Tcl_HashTable codes; /**< Unicode-to-document-character-ID cache. */
    Tcl_Encoding symbolEncoding; /**< Retained Tcl encoding for symbol-font mappings, or NULL. */
    char name[128]; /**< NUL-terminated export font name. */
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
 *
 * @param[in] token Live export context; an absent font cache yields NULL.
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
 *
 * @param[inout] token Live export context receiving the first error diagnostic and any document-local cache
 *    changes.
 * @param[in] tkfont Borrowed Tk font, kept valid throughout the export cache lifetime.
 * @param[in] ch Unicode scalar to resolve, not a native glyph index or PDF character code.
 * @param[out] fontPtr Receives a borrowed document-owned font only on success; invalidated by
 *    Rbc_ExportFontsFree.
 * @param[out] cidPtr Receives a one-based character ID within that font only on success.
 * @param[out] sizePtr Receives resolved font size in display pixels only on success.
 * @param[out] advancePtr Receives horizontal advance in display pixels only on success.
 */
int Rbc_ExportFontCharacter(Rbc_ExportContext *token, Tk_Font tkfont, Tcl_UniChar ch,
                            Rbc_ExportFont **fontPtr, int *cidPtr, double *sizePtr, double *advancePtr);
/**
 * @brief Release all document-local font programs and character cache entries.
 * Clears token->fontData; an empty cache is harmless. Invalidates all borrowed
 * font records and table views. Does not free backend PDF resources, the
 * document buffer, the export context or any borrowed Tk font.
 *
 * @param[inout] token Context whose entire font cache is released and fontData cleared; backend resources are
 *    separate.
 */
void Rbc_ExportFontsFree(Rbc_ExportContext *token);
/**
 * @brief Append an SVG font program with Unicode mappings for used characters.
 * Borrow token and a populated export font. The caller initializes program and
 * owns its DString storage. Rebuilds a temporary sfnt with replacement cmap;
 * retains outlines rather than subsetting them, and leaves font unchanged.
 * Returns TCL_OK after appending, or TCL_ERROR with an export diagnostic.
 * Temporary program allocations are released on both paths.
 *
 * @param[inout] token Live export context receiving the first error diagnostic and any document-local cache
 *    changes.
 * @param[in] font Borrowed font record with validated program bytes and table metadata.
 * @param[inout] program Caller-initialized DString receiving appended binary sfnt bytes on success; unchanged
 *    on failure.
 */
int Rbc_FontUnicodeProgram(Rbc_ExportContext *token, Rbc_ExportFont *font, Tcl_DString *program);
#endif
