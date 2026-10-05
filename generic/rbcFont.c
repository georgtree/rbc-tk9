/*
 * rbcFont.c --
 *
 *      Native TrueType and OpenType/CFF programs for PDF and SVG export. Tk 9's public "font actual"
 *      character query supplies the selected fallback family and style.
 *      Fontconfig (X11) or GDI (Windows) supplies the font program. No Tk
 *      platform-private font structures are copied or accessed here.
 *
 * See license.terms for details.
 */
#include "rbcFont.h"
#ifndef WIN32
#include <fontconfig/fontconfig.h>
#endif

/* Bound font input and directory arithmetic before allocating or indexing it. */
#define EXPORT_FONT_LIMIT (64u * 1024u * 1024u)

typedef struct {
    Rbc_ExportFont *font;
    int cid;
    double size;
} ExportCharacter;

typedef struct {
    Rbc_ExportFont *fonts;
    int serial;
    Tcl_HashTable characters;
} ExportFontCache;

typedef struct {
    unsigned char *bytes;
    size_t length, offset;
#ifdef WIN32
    HDC dc;
    HFONT font, oldFont;
#endif
} FontSource;

unsigned int Rbc_FontU16(const unsigned char *p) { return ((unsigned int)p[0] << 8) | p[1]; }

uint32_t Rbc_FontU32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

int Rbc_FontS16(const unsigned char *p) {
    unsigned int n = Rbc_FontU16(p);
    return n >= 32768 ? (int)n - 65536 : (int)n;
}

void Rbc_FontPut16(unsigned char *p, unsigned int n) {
    p[0] = (unsigned char)(n >> 8);
    p[1] = (unsigned char)n;
}

void Rbc_FontPut32(unsigned char *p, uint32_t n) {
    p[0] = (unsigned char)(n >> 24);
    p[1] = (unsigned char)(n >> 16);
    p[2] = (unsigned char)(n >> 8);
    p[3] = (unsigned char)n;
}

static int FontError(Rbc_ExportContext *token, const char *message) {
    if (token->error == NULL) {
        token->error = message;
    }
    return TCL_ERROR;
}

static void FontSourceFree(FontSource *source) {
    if (source->bytes != NULL) {
        ckfree(source->bytes);
    }
#ifdef WIN32
    if (source->dc != NULL) {
        if (source->oldFont != NULL) {
            SelectObject(source->dc, source->oldFont);
        }
        DeleteDC(source->dc);
    }
    if (source->font != NULL) {
        DeleteObject(source->font);
    }
#endif
}

/* Read a native face, including a selected face in a TrueType collection.
 * GDI table reads below avoid collection-relative offsets altogether. */
static int FontSourceOpen(Rbc_ExportContext *token, const char *family, int bold, int italic, double size,
                          Tcl_UniChar ch, FontSource *source) {
#ifdef WIN32
    LOGFONTW lf;
    DWORD length;
    (void)ch;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -(LONG)floor(size + 0.5);
    lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    lf.lfItalic = (BYTE)italic;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    MultiByteToWideChar(CP_UTF8, 0, family, -1, lf.lfFaceName, LF_FACESIZE);
    lf.lfFaceName[LF_FACESIZE - 1] = 0;
    source->font = CreateFontIndirectW(&lf);
    source->dc = CreateCompatibleDC(NULL);
    if (source->font == NULL || source->dc == NULL) {
        return FontError(token, "cannot open native font for font export");
    }
    source->oldFont = (HFONT)SelectObject(source->dc, source->font);
    /* Only the sfnt directory is read here; table data is obtained by tag. */
    source->bytes = (unsigned char *)ckalloc(12);
    source->length = 12;
    if (GetFontData(source->dc, 0, 0, source->bytes, 12) != 12) {
        return FontError(token, "font export requires an embeddable TrueType outline font");
    }
    length = 12 + 16 * Rbc_FontU16(source->bytes + 4);
    source->bytes = (unsigned char *)ckrealloc(source->bytes, length);
    source->length = length;
    if (GetFontData(source->dc, 0, 0, source->bytes, length) != length) {
        return FontError(token, "cannot read native font directory for font export");
    }
#else
    FcPattern *pattern, *match = NULL;
    FcFontSet *matches;
    FcCharSet *charset;
    FcResult result;
    FcChar8 *path;
    int index = 0, status = TCL_ERROR, candidate;
    Tcl_Obj *pathObj;
    Tcl_Channel channel;
    Tcl_DString contents;
    char block[8192];
    Tcl_Size count;
    (void)size;

    pattern = FcPatternCreate();
    charset = FcCharSetCreate();
    if (pattern == NULL || charset == NULL) {
        if (pattern != NULL) {
            FcPatternDestroy(pattern);
        }
        if (charset != NULL) {
            FcCharSetDestroy(charset);
        }
        return FontError(token, "cannot initialize font matching for font export");
    }
    FcCharSetAddChar(charset, (FcChar32)ch);
    FcPatternAddString(pattern, FC_FAMILY, (const FcChar8 *)family);
    FcPatternAddInteger(pattern, FC_WEIGHT, bold ? FC_WEIGHT_BOLD : FC_WEIGHT_REGULAR);
    FcPatternAddInteger(pattern, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddCharSet(pattern, FC_CHARSET, charset);
    FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    matches = FcFontSort(NULL, pattern, FcFalse, NULL, &result);
    FcPatternDestroy(pattern);
    FcCharSetDestroy(charset);
    /* Tk's core-X11 backend may select a legacy Type1/bitmap face. Prefer
     * its scalable sfnt equivalent, then a Fontconfig fallback with the glyph. */
    if (matches != NULL) {
        for (candidate = 0; candidate < matches->nfont; candidate++) {
            FcChar8 *format;
            FcCharSet *coverage;
            FcPattern *face = matches->fonts[candidate];
            if (FcPatternGetString(face, FC_FONTFORMAT, 0, &format) != FcResultMatch ||
                (strcmp((const char *)format, "TrueType") != 0 && strcmp((const char *)format, "CFF") != 0) ||
                FcPatternGetCharSet(face, FC_CHARSET, 0, &coverage) != FcResultMatch ||
                !FcCharSetHasChar(coverage, (FcChar32)ch)) {
                continue;
            }
            match = face;
            FcPatternReference(match);
            break;
        }
        FcFontSetDestroy(matches);
    }
    if (match == NULL) {
        return FontError(token, "no font export outline font contains a text character");
    }
    if (FcPatternGetString(match, FC_FILE, 0, &path) != FcResultMatch) {
        FcPatternDestroy(match);
        return FontError(token, "cannot locate native font file for font export");
    }
    FcPatternGetInteger(match, FC_INDEX, 0, &index);
    pathObj = Tcl_NewStringObj((const char *)path, -1);
    Tcl_IncrRefCount(pathObj);
    channel = Tcl_FSOpenFileChannel(token->interp, pathObj, "r", 0);
    Tcl_DecrRefCount(pathObj);
    FcPatternDestroy(match);
    if (channel == NULL) {
        return FontError(token, "cannot open native font file for font export");
    }
    Tcl_DStringInit(&contents);
    if (Tcl_SetChannelOption(token->interp, channel, "-translation", "binary") == TCL_OK &&
        Tcl_SetChannelOption(token->interp, channel, "-encoding", "iso8859-1") == TCL_OK) {
        while ((count = Tcl_Read(channel, block, sizeof(block))) > 0) {
            if ((size_t)Tcl_DStringLength(&contents) + count > EXPORT_FONT_LIMIT) {
                break;
            }
            Tcl_DStringAppend(&contents, block, count);
        }
        if (count == 0) {
            source->length = (size_t)Tcl_DStringLength(&contents);
            source->bytes = (unsigned char *)ckalloc(source->length);
            memcpy(source->bytes, Tcl_DStringValue(&contents), source->length);
            status = TCL_OK;
        }
    }
    if (Tcl_Close(token->interp, channel) != TCL_OK) {
        status = TCL_ERROR;
    }
    Tcl_DStringFree(&contents);
    if (status != TCL_OK) {
        return FontError(token, "cannot read native font file for font export (maximum 64 MiB)");
    }
    if (source->length >= 12 && memcmp(source->bytes, "ttcf", 4) == 0) {
        uint32_t faces = Rbc_FontU32(source->bytes + 8);
        if (index < 0 || (uint32_t)index >= faces || faces > (source->length - 12) / 4) {
            return FontError(token, "invalid TrueType collection face for font export");
        }
        source->offset = Rbc_FontU32(source->bytes + 12 + 4 * index);
    } else if (index != 0) {
        return FontError(token, "font export variable font instances are not supported");
    }
#endif
    return TCL_OK;
}

/* Rebuild a standalone sfnt, dropping stale signatures and recomputing checksums.
 * Full embedding also supports fonts whose permissions prohibit subsetting. */
static int FontProgram(Rbc_ExportContext *token, FontSource *source, Rbc_ExportFont *font) {
    const unsigned char *directory;
    unsigned char *out, *head = NULL;
    size_t length, offset;
    int count, kept = 0, i, power, selector;
    uint32_t sum = 0;

    if (source->offset > source->length || source->length - source->offset < 12) {
        return FontError(token, "invalid TrueType font directory for font export");
    }
    directory = source->bytes + source->offset;
    if (Rbc_FontU32(directory) != 0x00010000 && memcmp(directory, "true", 4) != 0 && memcmp(directory, "OTTO", 4) != 0) {
        return FontError(token, "font export requires TrueType or CFF outline fonts");
    }
    count = Rbc_FontU16(directory + 4);
    if (count == 0 || count > 4095 || (size_t)count > (source->length - source->offset - 12) / 16) {
        return FontError(token, "invalid TrueType font directory for font export");
    }
    length = 12;
    for (i = 0; i < count; i++) {
        const unsigned char *entry = directory + 12 + 16 * i;
        uint32_t n = Rbc_FontU32(entry + 12);
        if (memcmp(entry, "DSIG", 4) == 0) {
            continue;
        }
        if (memcmp(entry, "fvar", 4) == 0) {
            return FontError(token, "font export variable fonts are not supported");
        }
        if (n > EXPORT_FONT_LIMIT - 19 || length > EXPORT_FONT_LIMIT - 19 - n) {
            return FontError(token, "TrueType font is too large for embedding");
        }
        length += 16 + ((n + 3u) & ~3u);
        kept++;
    }
    out = (unsigned char *)ckalloc(length);
    memset(out, 0, length);
    font->bytes = out;
    font->length = length;
    Rbc_FontPut32(out, memcmp(directory, "OTTO", 4) == 0 ? 0x4f54544f : 0x00010000);
    Rbc_FontPut16(out + 4, kept);
    for (power = 1, selector = 0; power * 2 <= kept; power *= 2) {
        selector++;
    }
    Rbc_FontPut16(out + 6, 16 * power);
    Rbc_FontPut16(out + 8, selector);
    Rbc_FontPut16(out + 10, 16 * kept - 16 * power);
    offset = 12 + 16 * kept;
    kept = 0;
    for (i = 0; i < count; i++) {
        const unsigned char *entry = directory + 12 + 16 * i;
        unsigned char *dest = out + 12 + 16 * kept;
        uint32_t n = Rbc_FontU32(entry + 12), checksum = 0;
        size_t j;
        if (memcmp(entry, "DSIG", 4) == 0) {
            continue;
        }
#ifdef WIN32
        if (source->dc != NULL) {
            DWORD tag = (DWORD)entry[0] | ((DWORD)entry[1] << 8) | ((DWORD)entry[2] << 16) | ((DWORD)entry[3] << 24);
            if (GetFontData(source->dc, tag, 0, out + offset, n) != n) {
                return FontError(token, "cannot read native font table for font export");
            }
        } else
#endif
        {
            uint32_t start = Rbc_FontU32(entry + 8);
            if (start > source->length || n > source->length - start) {
                return FontError(token, "invalid TrueType table bounds for font export");
            }
            memcpy(out + offset, source->bytes + start, n);
        }
        if (memcmp(entry, "head", 4) == 0 && n >= 12) {
            head = out + offset;
            memset(head + 8, 0, 4);
        }
        for (j = 0; j < n; j += 4) {
            checksum += Rbc_FontU32(out + offset + j);
        }
        memcpy(dest, entry, 4);
        Rbc_FontPut32(dest + 4, checksum);
        Rbc_FontPut32(dest + 8, (uint32_t)offset);
        Rbc_FontPut32(dest + 12, n);
        offset += (n + 3u) & ~3u;
        kept++;
    }
    if (head == NULL) {
        return FontError(token, "TrueType font lacks a head table for font export");
    }
    for (offset = 0; offset < length; offset += 4) {
        sum += Rbc_FontU32(out + offset);
    }
    Rbc_FontPut32(head + 8, 0xb1b0afbaU - sum);
    return TCL_OK;
}

Rbc_FontTable Rbc_FontGetTable(Rbc_ExportFont *font, const char *tag) {
    Rbc_FontTable table = {NULL, 0};
    int i, count = Rbc_FontU16(font->bytes + 4);
    for (i = 0; i < count; i++) {
        const unsigned char *entry = font->bytes + 12 + 16 * i;
        if (memcmp(entry, tag, 4) == 0) {
            table.bytes = font->bytes + Rbc_FontU32(entry + 8);
            table.length = Rbc_FontU32(entry + 12);
            break;
        }
    }
    return table;
}

/* CFF 1 standard strings and predefined charsets (Adobe Technical Note 5176).
 * String IDs are format-defined data; custom strings follow the standard set. */
static const char *const cffStandardNames[] = {
    ".notdef",
    "space",
    "exclam",
    "quotedbl",
    "numbersign",
    "dollar",
    "percent",
    "ampersand",
    "quoteright",
    "parenleft",
    "parenright",
    "asterisk",
    "plus",
    "comma",
    "hyphen",
    "period",
    "slash",
    "zero",
    "one",
    "two",
    "three",
    "four",
    "five",
    "six",
    "seven",
    "eight",
    "nine",
    "colon",
    "semicolon",
    "less",
    "equal",
    "greater",
    "question",
    "at",
    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    "U",
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "bracketleft",
    "backslash",
    "bracketright",
    "asciicircum",
    "underscore",
    "quoteleft",
    "a",
    "b",
    "c",
    "d",
    "e",
    "f",
    "g",
    "h",
    "i",
    "j",
    "k",
    "l",
    "m",
    "n",
    "o",
    "p",
    "q",
    "r",
    "s",
    "t",
    "u",
    "v",
    "w",
    "x",
    "y",
    "z",
    "braceleft",
    "bar",
    "braceright",
    "asciitilde",
    "exclamdown",
    "cent",
    "sterling",
    "fraction",
    "yen",
    "florin",
    "section",
    "currency",
    "quotesingle",
    "quotedblleft",
    "guillemotleft",
    "guilsinglleft",
    "guilsinglright",
    "fi",
    "fl",
    "endash",
    "dagger",
    "daggerdbl",
    "periodcentered",
    "paragraph",
    "bullet",
    "quotesinglbase",
    "quotedblbase",
    "quotedblright",
    "guillemotright",
    "ellipsis",
    "perthousand",
    "questiondown",
    "grave",
    "acute",
    "circumflex",
    "tilde",
    "macron",
    "breve",
    "dotaccent",
    "dieresis",
    "ring",
    "cedilla",
    "hungarumlaut",
    "ogonek",
    "caron",
    "emdash",
    "AE",
    "ordfeminine",
    "Lslash",
    "Oslash",
    "OE",
    "ordmasculine",
    "ae",
    "dotlessi",
    "lslash",
    "oslash",
    "oe",
    "germandbls",
    "onesuperior",
    "logicalnot",
    "mu",
    "trademark",
    "Eth",
    "onehalf",
    "plusminus",
    "Thorn",
    "onequarter",
    "divide",
    "brokenbar",
    "degree",
    "thorn",
    "threequarters",
    "twosuperior",
    "registered",
    "minus",
    "eth",
    "multiply",
    "threesuperior",
    "copyright",
    "Aacute",
    "Acircumflex",
    "Adieresis",
    "Agrave",
    "Aring",
    "Atilde",
    "Ccedilla",
    "Eacute",
    "Ecircumflex",
    "Edieresis",
    "Egrave",
    "Iacute",
    "Icircumflex",
    "Idieresis",
    "Igrave",
    "Ntilde",
    "Oacute",
    "Ocircumflex",
    "Odieresis",
    "Ograve",
    "Otilde",
    "Scaron",
    "Uacute",
    "Ucircumflex",
    "Udieresis",
    "Ugrave",
    "Yacute",
    "Ydieresis",
    "Zcaron",
    "aacute",
    "acircumflex",
    "adieresis",
    "agrave",
    "aring",
    "atilde",
    "ccedilla",
    "eacute",
    "ecircumflex",
    "edieresis",
    "egrave",
    "iacute",
    "icircumflex",
    "idieresis",
    "igrave",
    "ntilde",
    "oacute",
    "ocircumflex",
    "odieresis",
    "ograve",
    "otilde",
    "scaron",
    "uacute",
    "ucircumflex",
    "udieresis",
    "ugrave",
    "yacute",
    "ydieresis",
    "zcaron",
    "exclamsmall",
    "Hungarumlautsmall",
    "dollaroldstyle",
    "dollarsuperior",
    "ampersandsmall",
    "Acutesmall",
    "parenleftsuperior",
    "parenrightsuperior",
    "twodotenleader",
    "onedotenleader",
    "zerooldstyle",
    "oneoldstyle",
    "twooldstyle",
    "threeoldstyle",
    "fouroldstyle",
    "fiveoldstyle",
    "sixoldstyle",
    "sevenoldstyle",
    "eightoldstyle",
    "nineoldstyle",
    "commasuperior",
    "threequartersemdash",
    "periodsuperior",
    "questionsmall",
    "asuperior",
    "bsuperior",
    "centsuperior",
    "dsuperior",
    "esuperior",
    "isuperior",
    "lsuperior",
    "msuperior",
    "nsuperior",
    "osuperior",
    "rsuperior",
    "ssuperior",
    "tsuperior",
    "ff",
    "ffi",
    "ffl",
    "parenleftinferior",
    "parenrightinferior",
    "Circumflexsmall",
    "hyphensuperior",
    "Gravesmall",
    "Asmall",
    "Bsmall",
    "Csmall",
    "Dsmall",
    "Esmall",
    "Fsmall",
    "Gsmall",
    "Hsmall",
    "Ismall",
    "Jsmall",
    "Ksmall",
    "Lsmall",
    "Msmall",
    "Nsmall",
    "Osmall",
    "Psmall",
    "Qsmall",
    "Rsmall",
    "Ssmall",
    "Tsmall",
    "Usmall",
    "Vsmall",
    "Wsmall",
    "Xsmall",
    "Ysmall",
    "Zsmall",
    "colonmonetary",
    "onefitted",
    "rupiah",
    "Tildesmall",
    "exclamdownsmall",
    "centoldstyle",
    "Lslashsmall",
    "Scaronsmall",
    "Zcaronsmall",
    "Dieresissmall",
    "Brevesmall",
    "Caronsmall",
    "Dotaccentsmall",
    "Macronsmall",
    "figuredash",
    "hypheninferior",
    "Ogoneksmall",
    "Ringsmall",
    "Cedillasmall",
    "questiondownsmall",
    "oneeighth",
    "threeeighths",
    "fiveeighths",
    "seveneighths",
    "onethird",
    "twothirds",
    "zerosuperior",
    "foursuperior",
    "fivesuperior",
    "sixsuperior",
    "sevensuperior",
    "eightsuperior",
    "ninesuperior",
    "zeroinferior",
    "oneinferior",
    "twoinferior",
    "threeinferior",
    "fourinferior",
    "fiveinferior",
    "sixinferior",
    "seveninferior",
    "eightinferior",
    "nineinferior",
    "centinferior",
    "dollarinferior",
    "periodinferior",
    "commainferior",
    "Agravesmall",
    "Aacutesmall",
    "Acircumflexsmall",
    "Atildesmall",
    "Adieresissmall",
    "Aringsmall",
    "AEsmall",
    "Ccedillasmall",
    "Egravesmall",
    "Eacutesmall",
    "Ecircumflexsmall",
    "Edieresissmall",
    "Igravesmall",
    "Iacutesmall",
    "Icircumflexsmall",
    "Idieresissmall",
    "Ethsmall",
    "Ntildesmall",
    "Ogravesmall",
    "Oacutesmall",
    "Ocircumflexsmall",
    "Otildesmall",
    "Odieresissmall",
    "OEsmall",
    "Oslashsmall",
    "Ugravesmall",
    "Uacutesmall",
    "Ucircumflexsmall",
    "Udieresissmall",
    "Yacutesmall",
    "Thornsmall",
    "Ydieresissmall",
    "001.000",
    "001.001",
    "001.002",
    "001.003",
    "Black",
    "Bold",
    "Book",
    "Light",
    "Medium",
    "Regular",
    "Roman",
    "Semibold",
};
static const unsigned short cffExpert[] = {
    0,   1,   229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 13,  14,  15,  99,  239, 240, 241, 242, 243,
    244, 245, 246, 247, 248, 27,  28,  249, 250, 251, 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262,
    263, 264, 265, 266, 109, 110, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281,
    282, 283, 284, 285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302,
    303, 304, 305, 306, 307, 308, 309, 310, 311, 312, 313, 314, 315, 316, 317, 318, 158, 155, 163, 319, 320,
    321, 322, 323, 324, 325, 326, 150, 164, 169, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338,
    339, 340, 341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359,
    360, 361, 362, 363, 364, 365, 366, 367, 368, 369, 370, 371, 372, 373, 374, 375, 376, 377, 378,
};
static const unsigned short cffExpertSubset[] = {
    0,   1,   231, 232, 235, 236, 237, 238, 13,  14,  15,  99,  239, 240, 241, 242, 243, 244, 245, 246, 247, 248,
    27,  28,  249, 250, 251, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, 265, 266, 109, 110, 267,
    268, 269, 270, 272, 300, 301, 302, 305, 314, 315, 158, 155, 163, 320, 321, 322, 323, 324, 325, 326, 150, 164,
    169, 327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346,
};

static uint32_t CffOffset(const unsigned char *p, unsigned int width) {
    uint32_t n = 0;
    while (width-- > 0) {
        n = (n << 8) | *p++;
    }
    return n;
}

/* Validate an entire CFF INDEX once so item lookups can safely share its data. */
static int CffIndex(Rbc_FontTable table, size_t *position, Rbc_FontTable *index) {
    const unsigned char *p;
    unsigned int count, width, i;
    size_t header, length;
    uint32_t previous = 1;
    if (*position > table.length || table.length - *position < 2) {
        return 0;
    }
    p = table.bytes + *position;
    count = Rbc_FontU16(p);
    index->bytes = p;
    index->length = 2;
    if (count == 0) {
        *position += 2;
        return 1;
    }
    if (table.length - *position < 3) {
        return 0;
    }
    width = p[2];
    if (width < 1 || width > 4) {
        return 0;
    }
    header = 3 + (size_t)(count + 1) * width;
    if (header > table.length - *position || CffOffset(p + 3, width) != 1) {
        return 0;
    }
    for (i = 1; i <= count; i++) {
        uint32_t end = CffOffset(p + 3 + (size_t)i * width, width);
        if (end < previous) {
            return 0;
        }
        previous = end;
    }
    if (previous - 1 > table.length - *position - header) {
        return 0;
    }
    length = header + previous - 1;
    index->length = length;
    *position += length;
    return 1;
}

static Rbc_FontTable CffItem(Rbc_FontTable index, unsigned int item) {
    Rbc_FontTable result = {NULL, 0};
    unsigned int count = Rbc_FontU16(index.bytes), width;
    uint32_t start, end;
    if (item >= count) {
        return result;
    }
    width = index.bytes[2];
    start = CffOffset(index.bytes + 3 + (size_t)item * width, width);
    end = CffOffset(index.bytes + 3 + (size_t)(item + 1) * width, width);
    result.bytes = index.bytes + 3 + (size_t)(count + 1) * width + start - 1;
    result.length = end - start;
    return result;
}

Rbc_FontTable Rbc_FontGlyphName(Rbc_ExportFont *font, int glyph) {
    unsigned int sid = font->sids[glyph];
    Rbc_FontTable name;
    if (sid < sizeof(cffStandardNames) / sizeof(cffStandardNames[0])) {
        name.bytes = (const unsigned char *)cffStandardNames[sid];
        name.length = strlen(cffStandardNames[sid]);
        return name;
    }
    return CffItem(font->cffStrings, sid - 391);
}

/* Read the name-keyed CFF charset, including the three predefined charsets.
 * CID-keyed CFF and CFF2 need a different export font mapping and are explicit
 * limitations; ordinary OpenType/CFF fonts use this Type1C path. */
static int FontCffInit(Rbc_ExportContext *token, Rbc_ExportFont *font) {
    Rbc_FontTable names, topIndex, top, strings, charstrings;
    size_t pos, at = 0;
    int64_t operands[48];
    int count = 0, i;
    uint32_t charset = 0, chars = 0;
    if (font->cff.length < 4 || font->cff.bytes[0] != 1 || font->cff.bytes[2] < 4) {
        goto invalid;
    }
    pos = font->cff.bytes[2];
    if (!CffIndex(font->cff, &pos, &names) || !CffIndex(font->cff, &pos, &topIndex) ||
        !CffIndex(font->cff, &pos, &strings) || Rbc_FontU16(names.bytes) != 1 || Rbc_FontU16(topIndex.bytes) != 1) {
        goto invalid;
    }
    font->cffStrings = strings;
    top = CffItem(topIndex, 0);
    while (at < top.length) {
        unsigned int b = top.bytes[at++], op = b;
        int64_t value;
        if (b <= 21) {
            if (b == 12) {
                if (at == top.length) {
                    goto invalid;
                }
                op = 1200 + top.bytes[at++];
            }
            if (op == 1230) {
                return FontError(token, "font export CID-keyed CFF fonts are not supported");
            }
            if (op == 15 || op == 17) {
                if (count != 1 || operands[0] < 0 || operands[0] > UINT32_MAX) {
                    goto invalid;
                }
                if (op == 15) {
                    charset = (uint32_t)operands[0];
                } else {
                    chars = (uint32_t)operands[0];
                }
            }
            count = 0;
            continue;
        }
        if (count == 48) {
            goto invalid;
        }
        if (b == 28) {
            if (top.length - at < 2) {
                goto invalid;
            }
            value = Rbc_FontS16(top.bytes + at);
            at += 2;
        } else if (b == 29) {
            if (top.length - at < 4) {
                goto invalid;
            }
            value = (int32_t)Rbc_FontU32(top.bytes + at);
            at += 4;
        } else if (b == 30) {
            /* Real operands occur in matrices, not the offsets used here. */
            int ended = 0;
            while (at < top.length) {
                unsigned int pair = top.bytes[at++];
                if ((pair & 15) == 15 || (pair >> 4) == 15) {
                    ended = 1;
                    break;
                }
            }
            if (!ended) {
                goto invalid;
            }
            value = -1;
        } else if (b >= 32 && b <= 246) {
            value = (int)b - 139;
        } else if (b >= 247 && b <= 254) {
            if (at == top.length) {
                goto invalid;
            }
            value = b <= 250 ? (int64_t)(b - 247) * 256 + top.bytes[at++] + 108
                             : -(int64_t)(b - 251) * 256 - top.bytes[at++] - 108;
        } else {
            goto invalid;
        }
        operands[count++] = value;
    }
    pos = chars;
    if (chars == 0 || !CffIndex(font->cff, &pos, &charstrings) || Rbc_FontU16(charstrings.bytes) != font->glyphCount)
        goto invalid;
    font->sids = (unsigned short *)ckalloc((size_t)font->glyphCount * sizeof(unsigned short));
    font->sids[0] = 0;
    if (charset <= 2) {
        if (charset == 0) {
            if (font->glyphCount > 229) {
                goto invalid;
            }
            for (i = 1; i < font->glyphCount; i++) {
                font->sids[i] = (unsigned short)i;
            }
        } else {
            const unsigned short *predefined = charset == 1 ? cffExpert : cffExpertSubset;
            size_t size = charset == 1 ? sizeof(cffExpert) : sizeof(cffExpertSubset);
            if ((size_t)font->glyphCount > size / sizeof(unsigned short)) {
                goto invalid;
            }
            memcpy(font->sids, predefined, font->glyphCount * sizeof(unsigned short));
        }
    } else {
        unsigned int format;
        pos = charset;
        if (pos >= font->cff.length) {
            goto invalid;
        }
        format = font->cff.bytes[pos++];
        if (format > 2) {
            goto invalid;
        }
        for (i = 1; i < font->glyphCount;) {
            unsigned int sid, left = 0;
            if (font->cff.length - pos < 2 + format) {
                goto invalid;
            }
            sid = Rbc_FontU16(font->cff.bytes + pos);
            pos += 2;
            if (format == 1) {
                left = font->cff.bytes[pos++];
            } else if (format == 2) {
                left = Rbc_FontU16(font->cff.bytes + pos);
                pos += 2;
            }
            if (left >= (unsigned int)(font->glyphCount - i) || sid + left > 65535) {
                goto invalid;
            }
            do {
                font->sids[i++] = (unsigned short)sid++;
            } while (left-- > 0);
        }
    }
    for (i = 0; i < font->glyphCount; i++) {
        Rbc_FontTable name = Rbc_FontGlyphName(font, i);
        if (name.bytes == NULL || name.length == 0 || name.length > 127) {
            goto invalid;
        }
    }
    return TCL_OK;
invalid:
    return FontError(token, "invalid OpenType/CFF font data for font export");
}

/* Validate the tables needed for metrics and Unicode lookup before using them. */
static int FontTables(Rbc_ExportContext *token, Rbc_ExportFont *font) {
    Rbc_FontTable os2 = Rbc_FontGetTable(font, "OS/2"), glyf = Rbc_FontGetTable(font, "glyf");
    Rbc_FontTable name = Rbc_FontGetTable(font, "name");
    unsigned int i;
    font->cff = Rbc_FontGetTable(font, "CFF ");
    font->isCff = glyf.bytes == NULL && font->cff.bytes != NULL;
    font->head = Rbc_FontGetTable(font, "head");
    font->hhea = Rbc_FontGetTable(font, "hhea");
    font->hmtx = Rbc_FontGetTable(font, "hmtx");
    font->cmap = Rbc_FontGetTable(font, "cmap");
    font->maxp = Rbc_FontGetTable(font, "maxp");
    if (font->head.length < 54 || font->hhea.length < 36 || font->maxp.length < 6 || font->cmap.length < 4 ||
        (glyf.bytes == NULL && font->cff.bytes == NULL) || os2.length < 10) {
        return FontError(token, "export font lacks required TrueType tables");
    }
    if (Rbc_FontGetTable(font, "COLR").bytes != NULL || Rbc_FontGetTable(font, "CBDT").bytes != NULL ||
        Rbc_FontGetTable(font, "sbix").bytes != NULL || Rbc_FontGetTable(font, "SVG ").bytes != NULL) {
        return FontError(token, "font export color fonts are not supported");
    }
    if (Rbc_FontU16(os2.bytes + 8) & 0x0202) {
        return FontError(token, "font embedding permissions prohibit font export outline embedding");
    }
    font->units = Rbc_FontU16(font->head.bytes + 18);
    font->glyphCount = Rbc_FontU16(font->maxp.bytes + 4);
    font->metricsCount = Rbc_FontU16(font->hhea.bytes + 34);
    if (font->units < 16 || font->units > 16384 || font->metricsCount == 0 || font->metricsCount > font->glyphCount ||
        font->hmtx.length < (size_t)font->metricsCount * 4 ||
        Rbc_FontU16(font->cmap.bytes + 2) > (font->cmap.length - 4) / 8) {
        return FontError(token, "invalid TrueType font metrics for font export");
    }
    snprintf(font->name, sizeof(font->name), "RBCFont%d", font->id);
    /* Prefer the Unicode/Windows PostScript name, retaining only font export name bytes. */
    if (name.length >= 6 && Rbc_FontU16(name.bytes + 2) <= (name.length - 6) / 12) {
        for (i = 0; i < Rbc_FontU16(name.bytes + 2); i++) {
            const unsigned char *record = name.bytes + 6 + 12 * i;
            size_t n = Rbc_FontU16(record + 8);
            size_t start = (size_t)Rbc_FontU16(name.bytes + 4) + Rbc_FontU16(record + 10), j;
            if (Rbc_FontU16(record + 6) != 6 || (Rbc_FontU16(record) != 0 && Rbc_FontU16(record) != 3) || n == 0 || n % 2 ||
                n / 2 >= sizeof(font->name) || start > name.length || n > name.length - start) {
                continue;
            }
            for (j = 0; j < n; j += 2) {
                unsigned int ch = Rbc_FontU16(name.bytes + start + j);
                if (ch < 33 || ch > 126 || strchr("()<>[]{}/%#", (int)ch) != NULL) {
                    break;
                }
                font->name[j / 2] = (char)ch;
            }
            if (j == n) {
                font->name[n / 2] = 0;
                break;
            }
            snprintf(font->name, sizeof(font->name), "RBCFont%d", font->id);
        }
    }
    return TCL_OK;
}

/* Unicode cmap formats 4 and 12 cover BMP and supplementary-plane TrueType
 * mappings. Every relative offset is checked before dereferencing it. */
static int FontGlyph(Rbc_ExportFont *font, Tcl_UniChar ch) {
    const unsigned char *cmap = font->cmap.bytes;
    unsigned int i, pass, count = Rbc_FontU16(cmap + 2);
    for (pass = 0; pass < 4; pass++) {
        for (i = 0; i < count; i++) {
            const unsigned char *record = cmap + 4 + 8 * i, *map;
            uint32_t offset = Rbc_FontU32(record + 4), glyph = 0;
            unsigned int platform = Rbc_FontU16(record), encoding = Rbc_FontU16(record + 2);
            size_t length;
            uint32_t code = (uint32_t)ch;
            if (pass < 2) {
                if (!(platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10)))) {
                    continue;
                }
            } else {
                char utf[TCL_UTF_MAX], bytes[8];
                int read, written, chars;
                int utfLength;
                if (platform != 3 || encoding != 0) {
                    continue;
                }
                /* Tk 9's Windows symbol fonts use the named Tcl encoding,
                 * then the cmap's F000 offset (or its legacy 8-bit range). */
                if (font->symbolEncoding != NULL) {
                    utfLength = Tcl_UniCharToUtf(ch, utf);
                    if (Tcl_UtfToExternal(NULL, font->symbolEncoding, utf, utfLength,
                                          TCL_ENCODING_START | TCL_ENCODING_END | TCL_ENCODING_STOPONERROR |
                                              TCL_ENCODING_NO_TERMINATE,
                                          NULL, bytes, sizeof(bytes), &read, &written, &chars) != TCL_OK ||
                        written != 1) {
                        continue;
                    }
                    code = (unsigned char)bytes[0];
                } else if (code >= 0xf000 && code <= 0xf0ff) {
                    code &= 255;
                } else if (code > 255) {
                    continue;
                }
                if (pass == 2) {
                    code += 0xf000;
                }
            }
            if (offset > font->cmap.length || font->cmap.length - offset < 4) {
                continue;
            }
            map = cmap + offset;
            if (pass == 0 && Rbc_FontU16(map) == 12 && font->cmap.length - offset >= 16) {
                uint32_t groups, low = 0, high;
                length = Rbc_FontU32(map + 4);
                groups = Rbc_FontU32(map + 12);
                if (length < 16 || length > font->cmap.length - offset || groups > (length - 16) / 12) {
                    continue;
                }
                high = groups;
                while (low < high) {
                    uint32_t middle = low + (high - low) / 2;
                    const unsigned char *group = map + 16 + 12 * middle;
                    uint32_t first = Rbc_FontU32(group), last = Rbc_FontU32(group + 4);
                    if (code < first) {
                        high = middle;
                    }
                    else if (code > last) {
                        low = middle + 1;
                    }
                    else {
                        uint64_t value = (uint64_t)Rbc_FontU32(group + 8) + code - first;
                        if (value < (unsigned int)font->glyphCount) {
                            glyph = (uint32_t)value;
                        }
                        break;
                    }
                }
            } else if (pass >= 1 && Rbc_FontU16(map) == 4 && code <= 0xffff) {
                unsigned int segments, j;
                length = Rbc_FontU16(map + 2);
                if (length < 16 || length > font->cmap.length - offset) {
                    continue;
                }
                segments = Rbc_FontU16(map + 6) / 2;
                if (segments == 0 || segments > (length - 16) / 8) {
                    continue;
                }
                for (j = 0; j < segments; j++) {
                    unsigned int last = Rbc_FontU16(map + 14 + 2 * j);
                    unsigned int first = Rbc_FontU16(map + 16 + 2 * segments + 2 * j);
                    unsigned int delta = Rbc_FontU16(map + 16 + 4 * segments + 2 * j);
                    size_t at = 16 + 6 * segments + 2 * j;
                    unsigned int range = Rbc_FontU16(map + at);
                    if (code < first || code > last) {
                        continue;
                    }
                    if (range == 0) {
                        glyph = (code + delta) & 0xffff;
                    }
                    else {
                        at += range + 2 * (code - first);
                        if (at <= length - 2) {
                            glyph = Rbc_FontU16(map + at);
                            if (glyph != 0) {
                                glyph = (glyph + delta) & 0xffff;
                            }
                        }
                    }
                    break;
                }
            }
            if (glyph > 0 && glyph < (unsigned int)font->glyphCount) {
                return (int)glyph;
            }
        }
    }
    return 0;
}

static void FontFree(Rbc_ExportFont *font) {
    Tcl_DeleteHashTable(&font->codes);
    if (font->key != NULL) {
        ckfree(font->key);
    }
    if (font->bytes != NULL) {
        ckfree(font->bytes);
    }
    if (font->glyphs != NULL) {
        ckfree(font->glyphs);
    }
    if (font->characters != NULL) {
        ckfree(font->characters);
    }
    if (font->sids != NULL) {
        ckfree(font->sids);
    }
    if (font->symbolEncoding != NULL) {
        Tcl_FreeEncoding(font->symbolEncoding);
    }
    ckfree(font);
}

/* Query Tk 9's actual per-character font, not merely the requested family.
 * Save the interpreter result; successful export must not leak query results. */
static Tcl_Obj *FontActual(Rbc_ExportContext *token, Tk_Font font, Tcl_UniChar ch) {
    Tcl_Obj *args[7], *result = NULL;
    Tcl_InterpState saved = Tcl_SaveInterpState(token->interp, TCL_OK);
    int i;
    args[0] = Tcl_NewStringObj("::font", -1);
    args[1] = Tcl_NewStringObj("actual", -1);
    args[2] = Tcl_NewStringObj(Tk_NameOfFont(font), -1);
    args[3] = Tcl_NewStringObj("-displayof", -1);
    args[4] = Tcl_NewStringObj(Tk_PathName(token->tkwin), -1);
    args[5] = Tcl_NewStringObj("--", -1);
    args[6] = Tcl_NewUnicodeObj(&ch, 1);
    for (i = 0; i < 7; i++) {
        Tcl_IncrRefCount(args[i]);
    }
    if (Tcl_EvalObjv(token->interp, 7, args, TCL_EVAL_GLOBAL) == TCL_OK) {
        result = Tcl_GetObjResult(token->interp);
        Tcl_IncrRefCount(result);
    } else {
        FontError(token, "cannot query Tk font attributes for font export");
    }
    for (i = 0; i < 7; i++) {
        Tcl_DecrRefCount(args[i]);
    }
    Tcl_RestoreInterpState(token->interp, saved);
    return result;
}

static Tcl_Obj *FontAttribute(Tcl_Obj *attributes, const char *name) {
    Tcl_Obj *key = Tcl_NewStringObj(name, -1), *value = NULL;
    Tcl_IncrRefCount(key);
    Tcl_DictObjGet(NULL, attributes, key, &value);
    Tcl_DecrRefCount(key);
    return value;
}

/* Resolve and cache a Unicode scalar and its native glyph. The one-based
 * character ID is independent of the glyph index so PDF extraction and SVG
 * character maps can retain distinct characters sharing the same outline. */
int Rbc_ExportFontCharacter(Rbc_ExportContext *token, Tk_Font tkfont, Tcl_UniChar ch, Rbc_ExportFont **fontPtr, int *cidPtr,
                            double *sizePtr, double *advancePtr) {
    ExportFontCache *cache = (ExportFontCache *)token->fontData;
    ExportCharacter *character;
    Rbc_ExportFont *font;
    Tcl_HashEntry *entry;
    Tcl_Obj *actual, *familyObj, *sizeObj, *weightObj, *slantObj, *key;
    const char *family;
    char lookup[80];
    int isNew, bold, italic, glyph, cid;
    double size;

    if (token->error != NULL) {
        return TCL_ERROR;
    }
    if ((uint32_t)ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff) || ch == 0) {
        return FontError(token, "font export text contains an invalid Unicode character");
    }
    if (cache == NULL) {
        cache = (ExportFontCache *)ckalloc(sizeof(*cache));
        cache->fonts = NULL;
        cache->serial = 0;
        Tcl_InitHashTable(&cache->characters, TCL_STRING_KEYS);
        token->fontData = cache;
    }
    snprintf(lookup, sizeof(lookup), "%p:%X", (void *)tkfont, (unsigned int)ch);
    entry = Tcl_FindHashEntry(&cache->characters, lookup);
    if (entry != NULL) {
        character = (ExportCharacter *)Tcl_GetHashValue(entry);
        font = character->font;
        *fontPtr = font;
        *cidPtr = character->cid;
        *sizePtr = character->size;
        *advancePtr = Rbc_FontU16(font->hmtx.bytes + 4 * MIN(font->glyphs[character->cid - 1], font->metricsCount - 1)) *
                      character->size / font->units;
        return TCL_OK;
    }
    actual = FontActual(token, tkfont, ch);
    if (actual == NULL) {
        return TCL_ERROR;
    }
    familyObj = FontAttribute(actual, "-family");
    sizeObj = FontAttribute(actual, "-size");
    weightObj = FontAttribute(actual, "-weight");
    slantObj = FontAttribute(actual, "-slant");
    if (familyObj == NULL || sizeObj == NULL || weightObj == NULL || slantObj == NULL ||
        Tcl_GetDoubleFromObj(NULL, sizeObj, &size) != TCL_OK || !isfinite(size) || size == 0) {
        Tcl_DecrRefCount(actual);
        return FontError(token, "invalid Tk font attributes for font export");
    }
    family = Tcl_GetString(familyObj);
    bold = strcmp(Tcl_GetString(weightObj), "bold") == 0;
    italic = strcmp(Tcl_GetString(slantObj), "italic") == 0;
    size = size < 0 ? -size : size * 25.4 / 72.0 * WidthOfScreen(Tk_Screen(token->tkwin)) /
                               WidthMMOfScreen(Tk_Screen(token->tkwin));
    if (size > INT_MAX) {
        Tcl_DecrRefCount(actual);
        return FontError(token, "export font size is too large");
    }
    /* Family and physical style identify the program; sizes share resources. */
    key = Tcl_ObjPrintf("%d:%d:%s", bold, italic, family);
    Tcl_IncrRefCount(key);
    for (font = cache->fonts; font != NULL; font = font->next) {
        if (strcmp(font->key, Tcl_GetString(key)) == 0 && FontGlyph(font, ch) != 0) {
            break;
        }
    }
    if (font == NULL) {
        FontSource source;
        memset(&source, 0, sizeof(source));
        font = (Rbc_ExportFont *)ckalloc(sizeof(*font));
        memset(font, 0, sizeof(*font));
        Tcl_InitHashTable(&font->codes, TCL_ONE_WORD_KEYS);
        font->key = (char *)ckalloc(strlen(Tcl_GetString(key)) + 1);
        strcpy(font->key, Tcl_GetString(key));
        font->id = ++cache->serial;
        if (FontSourceOpen(token, family, bold, italic, size, ch, &source) != TCL_OK ||
            FontProgram(token, &source, font) != TCL_OK || FontTables(token, font) != TCL_OK ||
            (font->isCff && FontCffInit(token, font) != TCL_OK)) {
            FontSourceFree(&source);
            FontFree(font);
            Tcl_DecrRefCount(key);
            Tcl_DecrRefCount(actual);
            return TCL_ERROR;
        }
        FontSourceFree(&source);
        {
            unsigned int map;
            for (map = 0; map < Rbc_FontU16(font->cmap.bytes + 2); map++) {
                const unsigned char *record = font->cmap.bytes + 4 + 8 * map;
                if (Rbc_FontU16(record) == 3 && Rbc_FontU16(record + 2) == 0) {
                    Tcl_DString lower;
                    Tcl_DStringInit(&lower);
                    Tcl_DStringAppend(&lower, family, -1);
                    Tcl_UtfToLower(Tcl_DStringValue(&lower));
                    font->symbolEncoding = Tcl_GetEncoding(NULL, Tcl_DStringValue(&lower));
                    Tcl_DStringFree(&lower);
                    break;
                }
            }
        }
        font->next = cache->fonts;
        cache->fonts = font;
    }
    Tcl_DecrRefCount(key);
    Tcl_DecrRefCount(actual);
    glyph = FontGlyph(font, ch);
    if (glyph == 0) {
        return FontError(token, "selected export font has no glyph for a text character");
    }
    entry = Tcl_CreateHashEntry(&font->codes, (char *)(uintptr_t)ch, &isNew);
    if (isNew) {
        if (font->count == 65535) {
            return FontError(token, "too many distinct characters in one export font");
        }
        if (font->count == font->capacity) {
            font->capacity += 128;
            font->glyphs = (int *)ckrealloc(font->glyphs, sizeof(int) * (size_t)font->capacity);
            font->characters = (Tcl_UniChar *)ckrealloc(font->characters, sizeof(Tcl_UniChar) * (size_t)font->capacity);
        }
        cid = ++font->count;
        font->glyphs[cid - 1] = glyph;
        font->characters[cid - 1] = ch;
        Tcl_SetHashValue(entry, (void *)(uintptr_t)cid);
    } else {
        cid = (int)(uintptr_t)Tcl_GetHashValue(entry);
    }
    character = (ExportCharacter *)ckalloc(sizeof(*character));
    character->font = font;
    character->cid = cid;
    character->size = size;
    entry = Tcl_CreateHashEntry(&cache->characters, lookup, &isNew);
    Tcl_SetHashValue(entry, character);
    *fontPtr = font;
    *cidPtr = cid;
    *sizePtr = size;
    *advancePtr = Rbc_FontU16(font->hmtx.bytes + 4 * MIN(glyph, font->metricsCount - 1)) * size / font->units;
    return TCL_OK;
}

void Rbc_ExportFontsFree(Rbc_ExportContext *token) {
    ExportFontCache *cache = (ExportFontCache *)token->fontData;
    Tcl_HashSearch search;
    Tcl_HashEntry *entry;
    Rbc_ExportFont *font, *next;
    if (cache == NULL) {
        return;
    }
    for (entry = Tcl_FirstHashEntry(&cache->characters, &search); entry != NULL; entry = Tcl_NextHashEntry(&search)) {
        ckfree(Tcl_GetHashValue(entry));
    }
    Tcl_DeleteHashTable(&cache->characters);
    for (font = cache->fonts; font != NULL; font = next) {
        next = font->next;
        FontFree(font);
    }
    ckfree(cache);
    token->fontData = NULL;
}

Rbc_ExportFont *Rbc_FirstExportFont(Rbc_ExportContext *token) {
    ExportFontCache *cache = (ExportFontCache *)token->fontData;
    return cache == NULL ? NULL : cache->fonts;
}

/* SVG keeps Unicode text. Map its used characters directly to the resolved
 * glyphs, including legacy Symbol encodings, without removing any outlines. */
typedef struct {
    uint32_t character, glyph;
} FontUnicodePair;

static int FontUnicodeCompare(const void *a, const void *b) {
    const FontUnicodePair *left = a, *right = b;
    return (left->character > right->character) - (left->character < right->character);
}

int Rbc_FontUnicodeProgram(Rbc_ExportContext *token, Rbc_ExportFont *font, Tcl_DString *program) {
    FontUnicodePair *pairs;
    FontSource source;
    Rbc_ExportFont rebuilt;
    unsigned char *cmap, *format4, *format12;
    size_t cmapLength, format4Length;
    int i, bmp = 0, segments, power, selector, result;

    memset(&source, 0, sizeof(source));
    memset(&rebuilt, 0, sizeof(rebuilt));
    pairs = (FontUnicodePair *)ckalloc(sizeof(*pairs) * font->count);
    for (i = 0; i < font->count; i++) {
        pairs[i].character = font->characters[i];
        pairs[i].glyph = font->glyphs[i];
    }
    qsort(pairs, font->count, sizeof(*pairs), FontUnicodeCompare);
    while (bmp < font->count && bmp < 8188 && pairs[bmp].character < 0xffff) {
        bmp++;
    }
    segments = bmp + 1;
    format4Length = 16 + 8 * segments;
    cmapLength = 20 + format4Length + 16 + 12 * font->count;
    source.length = font->length + cmapLength;
    source.bytes = (unsigned char *)ckalloc(source.length);
    memcpy(source.bytes, font->bytes, font->length);
    cmap = source.bytes + font->length;
    memset(cmap, 0, cmapLength);
    Rbc_FontPut16(cmap + 2, 2);
    Rbc_FontPut16(cmap + 4, 3);
    Rbc_FontPut16(cmap + 6, 1);
    Rbc_FontPut32(cmap + 8, 20);
    Rbc_FontPut16(cmap + 12, 3);
    Rbc_FontPut16(cmap + 14, 10);
    Rbc_FontPut32(cmap + 16, 20 + format4Length);
    format4 = cmap + 20;
    Rbc_FontPut16(format4, 4);
    Rbc_FontPut16(format4 + 2, format4Length);
    Rbc_FontPut16(format4 + 6, 2 * segments);
    for (power = 1, selector = 0; power * 2 <= segments; power *= 2) {
        selector++;
    }
    Rbc_FontPut16(format4 + 8, 2 * power);
    Rbc_FontPut16(format4 + 10, selector);
    Rbc_FontPut16(format4 + 12, 2 * segments - 2 * power);
    for (i = 0; i < segments; i++) {
        unsigned int ch = i < bmp ? pairs[i].character : 0xffff;
        unsigned int glyph = i < bmp ? pairs[i].glyph : 0;
        Rbc_FontPut16(format4 + 14 + 2 * i, ch);
        Rbc_FontPut16(format4 + 16 + 2 * segments + 2 * i, ch);
        Rbc_FontPut16(format4 + 16 + 4 * segments + 2 * i, glyph - ch);
    }
    format12 = format4 + format4Length;
    Rbc_FontPut16(format12, 12);
    Rbc_FontPut32(format12 + 4, 16 + 12 * font->count);
    Rbc_FontPut32(format12 + 12, font->count);
    for (i = 0; i < font->count; i++) {
        Rbc_FontPut32(format12 + 16 + 12 * i, pairs[i].character);
        Rbc_FontPut32(format12 + 20 + 12 * i, pairs[i].character);
        Rbc_FontPut32(format12 + 24 + 12 * i, pairs[i].glyph);
    }
    ckfree(pairs);
    for (i = 0; i < Rbc_FontU16(source.bytes + 4); i++) {
        unsigned char *entry = source.bytes + 12 + 16 * i;
        if (memcmp(entry, "cmap", 4) == 0) {
            Rbc_FontPut32(entry + 8, font->length);
            Rbc_FontPut32(entry + 12, cmapLength);
            break;
        }
    }
    result = FontProgram(token, &source, &rebuilt);
    if (result == TCL_OK) {
        Tcl_DStringAppend(program, (const char *)rebuilt.bytes, rebuilt.length);
    }
    if (rebuilt.bytes != NULL) {
        ckfree(rebuilt.bytes);
    }
    ckfree(source.bytes);
    return result;
}
