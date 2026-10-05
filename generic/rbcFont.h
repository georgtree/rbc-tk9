/* Private native font programs shared by document exporters. See license.terms. */
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
void Rbc_FontPut32(unsigned char *p, uint32_t n);
Rbc_FontTable Rbc_FontGetTable(Rbc_ExportFont *font, const char *tag);
Rbc_FontTable Rbc_FontGlyphName(Rbc_ExportFont *font, int glyph);
Rbc_ExportFont *Rbc_FirstExportFont(Rbc_ExportContext *token);
int Rbc_ExportFontCharacter(Rbc_ExportContext *token, Tk_Font tkfont, Tcl_UniChar ch,
                            Rbc_ExportFont **fontPtr, int *cidPtr, double *sizePtr, double *advancePtr);
void Rbc_ExportFontsFree(Rbc_ExportContext *token);
int Rbc_FontUnicodeProgram(Rbc_ExportContext *token, Rbc_ExportFont *font, Tcl_DString *program);
#endif
