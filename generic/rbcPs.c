/** @defgroup ps_token_lifetime PostScript token lifetime and buffers
 * Private ownership and borrowed buffer accessors.
 */
/** @defgroup ps_token_output PostScript syntax and prolog appending
 * Private append services; framing and delivery remain with the graph command.
 */
/**
 * @file rbcPs.c
 * @brief Private PostScript token, conversion and legacy drawing services.
 * @defgroup ps_legacy_primitives Legacy PostScript drawing primitives
 * @brief Append drawing programs to a borrowed token; no page delivery.
 * @defgroup ps_raster_encoding PostScript raster and bitmap encoding
 * @brief Legacy image conversion, bitmap payloads and capture wrappers.
 * @defgroup ps_text_mapping PostScript font mapping and text placement
 * @brief Legacy font names and screen-metric text, without embedded outlines.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include "rbcPs.h"

#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdarg.h>

#define PS_MAXPATH                                                                                                     \
    1500 /* Maximum number of components in a PostScript                                                               \
          * (level 1) path. */

static void XColorToPostScript(struct PsTokenStruct *tokenPtr, XColor *colorPtr);
INLINE static unsigned char ReverseBits(register unsigned char byte);
INLINE static void ByteToHex(register unsigned char byte, char *string);
#ifndef WIN32
static const char *NameOfAtom(Tk_Window tkwin, Atom atom);
#endif /* WIN32 */
static void TextLayoutToPostScript(struct PsTokenStruct *tokenPtr, int x, int y, TextLayout *textPtr);

/**
 * @ingroup ps_token_lifetime
 * Allocate an empty, color-mode PostScript token.
 *
 * The caller owns the token and releases it with Rbc_ReleasePsToken(). The
 * interpreter and window are borrowed; this function does not preserve them.
 * Font and color mapping variable names initially are NULL.
 *
 * @param[in] interp Interpreter borrowed for later diagnostics and mapping lookups; see @ref
 *    rbc_interp_argument.
 * @param[in] tkwin Tk window borrowed for font, color and display information throughout token use.
 */
PsToken Rbc_GetPsToken(Tcl_Interp *interp, Tk_Window tkwin) {
    struct PsTokenStruct *tokenPtr;

    tokenPtr = (struct PsTokenStruct *)ckalloc(sizeof(struct PsTokenStruct));
    assert(tokenPtr);
    tokenPtr->fontVarName = tokenPtr->colorVarName = NULL;
    tokenPtr->interp = interp;
    tokenPtr->tkwin = tkwin;
    tokenPtr->colorMode = PS_MODE_COLOR;
    Tcl_DStringInit(&(tokenPtr->dString));
    return tokenPtr;
}

/**
 * @ingroup ps_token_lifetime
 * Release a non-NULL token and its output buffer.
 *
 * Borrowed interpreter, window and mapping variable names are not released.
 * All pointers returned into the token become invalid.
 *
 * @param[in] tokenPtr Owned non-NULL token to release; all pointers into its buffers become invalid.
 */
void Rbc_ReleasePsToken(struct PsTokenStruct *tokenPtr) {
    Tcl_DStringFree(&(tokenPtr->dString));
    ckfree((char *)tokenPtr);
}

/**
 * @ingroup ps_token_lifetime
 * Return the borrowed, NUL-terminated output buffer.
 *
 * Do not free the result. Appending output may relocate it; releasing the
 * token invalidates it. This accessor does not finalize a document.
 *
 * @param[in] tokenPtr Live token whose output buffer is borrowed until the next relocating append or release.
 */
char *Rbc_PostScriptFromToken(struct PsTokenStruct *tokenPtr) { return Tcl_DStringValue(&(tokenPtr->dString)); }

/**
 * @ingroup ps_token_lifetime
 * Return borrowed mutable scratch storage of PSTOKEN_BUFSIZ + 1 bytes.
 *
 * Formatting and prolog reads reuse this storage. It is not the output buffer
 * and must not be retained across those operations or token release.
 *
 * @param[in] tokenPtr Live token whose reusable scratch storage is borrowed; contents are not initialized by
 *    this accessor.
 */
char *Rbc_ScratchBufferFromToken(struct PsTokenStruct *tokenPtr) { return tokenPtr->scratchArr; }

/**
 * @ingroup ps_token_output
 * Append a sequence of NUL-terminated strings, ending with a NULL pointer.
 *
 * Strings are copied without escaping. Callers supply PostScript syntax and
 * must not pass binary data or pointers invalidated by output growth.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] ... NUL-terminated C strings followed by a pointer-valued NULL sentinel; strings must survive
 *    output growth.
 */
void Rbc_AppendToPostScript(struct PsTokenStruct *tokenPtr, ...) {
    va_list argList;
    char *string;

    va_start(argList, tokenPtr);
    for (;;) {
        string = va_arg(argList, char *);
        if (string == NULL) {
            break;
        }
        Tcl_DStringAppend(&(tokenPtr->dString), string, -1);
    }
    va_end(argList);
}

/**
 * @ingroup ps_token_output
 * Append printf-formatted PostScript, using scratch storage or a temporary
 * allocation when the formatted result exceeds the scratch capacity.
 *
 * This void helper does not report a formatting failure to the interpreter.
 * It performs no PostScript string escaping.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] fmt Borrowed printf format, not aliased with scratch storage overwritten by formatting.
 * @param[in] ... Arguments matching the format; referenced strings must remain readable during formatting and
 *    output growth.
 */
void Rbc_FormatToPostScript(struct PsTokenStruct *tokenPtr, const char *fmt, ...) {
    va_list argList;
    char *buffer;
    int nBytes;

    va_start(argList, fmt);
    nBytes = vsnprintf(tokenPtr->scratchArr, sizeof(tokenPtr->scratchArr), fmt, argList);
    va_end(argList);
    if (nBytes < 0) {
        return;
    }
    if ((size_t)nBytes < sizeof(tokenPtr->scratchArr)) {
        Tcl_DStringAppend(&(tokenPtr->dString), tokenPtr->scratchArr, nBytes);
        return;
    }
    buffer = (char *)ckalloc((size_t)nBytes + 1);
    va_start(argList, fmt);
    nBytes = vsnprintf(buffer, (size_t)nBytes + 1, fmt, argList);
    va_end(argList);
    if (nBytes >= 0) {
        Tcl_DStringAppend(&(tokenPtr->dString), buffer, nBytes);
    }
    ckfree(buffer);
}

/**
 * @ingroup ps_token_output
 * Append escaped PostScript literal-string contents from Tcl UTF text.
 *
 * A negative length selects strlen; otherwise length is a byte count ending
 * at a character boundary. NULL input is ignored. The caller supplies the
 * surrounding parentheses. Delimiters, controls and non-ASCII characters use
 * octal escapes; characters above ISO-8859-1 are replaced with a space.
 *
 * @param[inout] psToken Live borrowed PostScript token receiving output.
 * @param[in] string Borrowed Tcl UTF text, or NULL to append nothing; must remain readable across output
 *    appends.
 * @param[in] length UTF byte count ending at a character boundary, or negative to use strlen; not a character
 *    count.
 */
void Rbc_AppendPostScriptString(PsToken psToken, const char *string, Tcl_Size length) {
    const char *src;
    const char *end;
    Tcl_UniChar ch;
    char c;
    char buf[5];

    if (string == NULL) {
        return;
    }
    src = string;
    if (length < 0) {
        end = string + strlen(string);
    } else {
        end = string + length;
    }
    while (src < end) {
        src += Tcl_UtfToUniChar(src, &ch);
        if ((ch == '\\') || (ch == '(') || (ch == ')') || (ch < ' ') || (ch >= 0x7f)) {
            unsigned int value;

            /*
             * PostScript text output is encoded as ISO-8859-1.
             * Characters outside that range cannot be represented
             * directly, so preserve the existing text-output
             * behaviour and substitute a space.
             */
            value = (ch <= 0xff) ? (unsigned int)ch : 0x20u;
            snprintf(buf, sizeof(buf), "\\%03o", value);
            Tcl_DStringAppend(&psToken->dString, buf, -1);
        } else {
            c = (char)ch;
            Tcl_DStringAppend(&psToken->dString, &c, 1);
        }
    }
}

/**
 * @ingroup ps_token_output
 * Append a text prolog from fileName relative to the rbc::library variable.
 *
 * Return TCL_ERROR for library lookup, open or read failure, with an
 * interpreter diagnostic; otherwise return TCL_OK. Partial output is not
 * rolled back. The channel is closed, but its close status is not propagated.
 * Reads overwrite token scratch storage. Tcl variable and channel operations
 * may execute callbacks; the enclosing export owns lifetime protection.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] fileName Borrowed NUL-terminated prolog filename appended to the global rbc::library directory.
 */
int Rbc_FileToPostScript(struct PsTokenStruct *tokenPtr, char *fileName) {
    Tcl_Channel channel;
    Tcl_DString dString;
    Tcl_Interp *interp;
    char *buf;
    char *libDir;
    Tcl_Size nBytes;

    interp = tokenPtr->interp;
    buf = tokenPtr->scratchArr;
    /*
     * Read in a standard prolog file from file and append it to the
     * PostScript output stored in the Tcl_DString in tokenPtr.
     */
    libDir = (char *)Tcl_GetVar(interp, "rbc::library", TCL_GLOBAL_ONLY);
    if (libDir == NULL) {
        Rbc_AppendResultStrings(interp, "couldn't find rbc script library:", "global variable \"rbc::library\" doesn't exist",
                         (char *)NULL);
        return TCL_ERROR;
    }
    Tcl_DStringInit(&dString);
    Tcl_DStringAppend(&dString, libDir, -1);
    Tcl_DStringAppend(&dString, "/", -1);
    Tcl_DStringAppend(&dString, fileName, -1);
    fileName = Tcl_DStringValue(&dString);
    Rbc_AppendToPostScript(tokenPtr, "\n% including file \"", fileName, "\"\n\n", (char *)NULL);
    channel = Tcl_OpenFileChannel(interp, fileName, "r", 0);
    if (channel == NULL) {
        Rbc_AppendResultStrings(interp, "couldn't open prologue file \"", fileName, "\": ", Tcl_PosixError(interp),
                         (char *)NULL);
        Tcl_DStringFree(&dString);
        return TCL_ERROR;
    }
    for (;;) {
        nBytes = Tcl_Read(channel, buf, PSTOKEN_BUFSIZ);
        if (nBytes < 0) {
            Rbc_AppendResultStrings(interp, "error reading prologue file \"", fileName, "\": ", Tcl_PosixError(interp),
                             (char *)NULL);
            Tcl_Close(interp, channel);
            Tcl_DStringFree(&dString);
            return TCL_ERROR;
        }
        if (nBytes == 0) {
            break;
        }
        buf[nBytes] = '\0';
        Rbc_AppendToPostScript(tokenPtr, buf, (char *)NULL);
    }
    Tcl_DStringFree(&dString);
    Tcl_Close(interp, channel);
    return TCL_OK;
}
/**
 * @brief Append three normalized RGB operands from a borrowed XColor.
 * @ingroup ps_legacy_primitives
 *
 * Uses only the upper eight bits of each 16-bit component, divided by 255.
 * Emits no color-setting operator and performs no variable lookup.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] colorPtr Non-NULL borrowed color; only the upper eight bits of each RGB component are emitted.
 */
static void XColorToPostScript(struct PsTokenStruct *tokenPtr, XColor *colorPtr) {
    /*
     * Shift off the lower byte before dividing because some versions
     * of Tk don't fill the lower byte correctly.
     */
    Rbc_FormatToPostScript(tokenPtr, "%g %g %g", ((double)(colorPtr->red >> 8) / 255.0),
                           ((double)(colorPtr->green >> 8) / 255.0), ((double)(colorPtr->blue >> 8) / 255.0));
}

/**
 * @brief Append mapped background code or RGB operands followed by SetBgColor.
 * @ingroup ps_legacy_primitives
 *
 * If colorVarName is set, look up the color name in that Tcl array and append
 * a found value verbatim, bypassing the default operator. Missing entries
 * fall back to RGB conversion. Variable reads may run traces; the enclosing
 * caller must protect token, interpreter and borrowed input lifetimes.
 * This void helper neither reports lookup failure nor rolls back output.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] colorPtr Non-NULL borrowed color; keep it valid across optional mapping-variable traces.
 */
void Rbc_BackgroundToPostScript(struct PsTokenStruct *tokenPtr, XColor *colorPtr) {
    /* If the color name exists in Tcl array variable, use that translation */
    if (tokenPtr->colorVarName != NULL) {
        const char *psColor;

        psColor = Tcl_GetVar2(tokenPtr->interp, tokenPtr->colorVarName, Tk_NameOfColor(colorPtr), 0);
        if (psColor != NULL) {
            Rbc_AppendToPostScript(tokenPtr, " ", psColor, "\n", (char *)NULL);
            return;
        }
    }
    XColorToPostScript(tokenPtr, colorPtr);
    Rbc_AppendToPostScript(tokenPtr, " SetBgColor\n", (char *)NULL);
}

/**
 * @brief Append mapped foreground code or RGB operands followed by SetFgColor.
 * @ingroup ps_legacy_primitives
 *
 * Uses the same optional Tcl color-array lookup as background output, with
 * the same trace/lifetime requirements. Found values are executable source,
 * not escaped color names. Otherwise the prolog controls color, greyscale
 * and monochrome conversion. No graphics-state save is emitted.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] colorPtr Non-NULL borrowed color; keep it valid across optional mapping-variable traces.
 */
void Rbc_ForegroundToPostScript(struct PsTokenStruct *tokenPtr, XColor *colorPtr) {
    /* If the color name exists in Tcl array variable, use that translation */
    if (tokenPtr->colorVarName != NULL) {
        const char *psColor;

        psColor = Tcl_GetVar2(tokenPtr->interp, tokenPtr->colorVarName, Tk_NameOfColor(colorPtr), 0);
        if (psColor != NULL) {
            Rbc_AppendToPostScript(tokenPtr, " ", psColor, "\n", (char *)NULL);
            return;
        }
    }
    XColorToPostScript(tokenPtr, colorPtr);
    Rbc_AppendToPostScript(tokenPtr, " SetFgColor\n", (char *)NULL);
}

/**
 * @brief Return a byte with its eight bit positions reversed.
 * @ingroup ps_raster_encoding
 *
 * Swaps adjacent bits, pairs and nibbles. No allocation or output state is
 * involved; bitmap serializers use it to place the first pixel in bit seven.
 *
 * @param[in] byte Eight-bit value whose bit positions are reversed.
 */
INLINE static unsigned char ReverseBits(unsigned char byte) {
    byte = ((byte >> 1) & 0x55) | ((byte << 1) & 0xaa);
    byte = ((byte >> 2) & 0x33) | ((byte << 2) & 0xcc);
    byte = ((byte >> 4) & 0x0f) | ((byte << 4) & 0xf0);
    return byte;
}

/**
 * @brief Write two uppercase hexadecimal digits into caller-owned storage.
 * @ingroup ps_raster_encoding
 *
 * string must have at least two writable bytes. No NUL terminator is written;
 * callers add delimiters, line breaks or a terminator separately.
 *
 * @param[in] byte Byte to encode as two uppercase hexadecimal digits.
 * @param[out] string At least two writable bytes; no terminating NUL is added.
 */
INLINE static void ByteToHex(unsigned char byte, char *string) {
    static char hexDigits[] = "0123456789ABCDEF";
    string[0] = hexDigits[byte >> 4];
    string[1] = hexDigits[byte & 0x0F];
}

#ifdef WIN32
/**
 * @brief Append a Windows depth-one bitmap as a PostScript hex string.
 * @ingroup ps_raster_encoding
 *
 * Borrows a valid bitmap and token with positive matching dimensions.
 * Rbc_GetBitmapData supplies owned bottom-up DIB bytes and padded row stride;
 * rows are traversed in reverse order and each output row is independently
 * packed most-significant pixel first, padding its last byte with zero bits.
 * Releases the DIB bytes after emission. Acquisition failure writes a Windows
 * debug message and returns without payload or Tcl error. A caller that has
 * already emitted image operators must not assume output is complete.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] display Display owning the borrowed bitmap.
 * @param[in] bitmap Live borrowed depth-one bitmap; not freed by this helper.
 * @param[in] width Positive bitmap width in pixels, matching the captured bitmap.
 * @param[in] height Positive bitmap height in pixels, matching the captured bitmap.
 */
void Rbc_BitmapDataToPostScript(struct PsTokenStruct *tokenPtr, Display *display, Pixmap bitmap, int width,
                                int height) {
    register unsigned char byte;
    register int x, y, bitPos;
    unsigned long pixel;
    int byteCount;
    char string[10];
    unsigned char *srcBits, *srcPtr;
    int bytesPerRow;

    srcBits = Rbc_GetBitmapData(display, bitmap, width, height, &bytesPerRow);
    if (srcBits == NULL) {
        OutputDebugStringA("Can't get bitmap data");
        return;
    }
    Rbc_AppendToPostScript(tokenPtr, "\t<", (char *)NULL);
    byteCount = bitPos = 0; /* Suppress compiler warning */
    for (y = height - 1; y >= 0; y--) {
        srcPtr = srcBits + ((size_t)bytesPerRow * (size_t)y);
        byte = 0;
        for (x = 0; x < width; x++) {
            bitPos = x % 8;
            pixel = (*srcPtr & (0x80 >> bitPos));
            if (pixel) {
                byte |= (unsigned char)(1 << bitPos);
            }
            if (bitPos == 7) {
                byte = ReverseBits(byte);
                ByteToHex(byte, string);
                string[2] = '\0';
                byteCount++;
                srcPtr++;
                byte = 0;
                if (byteCount >= 30) {
                    string[2] = '\n';
                    string[3] = '\t';
                    string[4] = '\0';
                    byteCount = 0;
                }
                Rbc_AppendToPostScript(tokenPtr, string, (char *)NULL);
            }
        } /* x */
        if (bitPos != 7) {
            byte = ReverseBits(byte);
            ByteToHex(byte, string);
            string[2] = '\0';
            Rbc_AppendToPostScript(tokenPtr, string, (char *)NULL);
            byteCount++;
        }
    } /* y */
    ckfree((char *)srcBits);
    Rbc_AppendToPostScript(tokenPtr, ">\n", (char *)NULL);
}

#else

/**
 * @brief Append an X11 depth-one bitmap as a PostScript hex string.
 * @ingroup ps_raster_encoding
 *
 * Borrows a valid bitmap and token with positive matching dimensions.
 * Captures plane one with XGetImage, reads rows top-to-bottom with XGetPixel,
 * and packs the first pixel into the high bit of each output byte. Partial
 * bytes are zero-padded per row. Emits angle-bracket delimiters, uppercase
 * hex and wrapping after batches of thirty complete bytes, then destroys
 * the XImage. A failed capture returns without appending a payload, matching
 * the Windows failure policy. No Tcl status is returned.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] display Display owning the borrowed bitmap.
 * @param[in] bitmap Live borrowed depth-one bitmap; not freed by this helper.
 * @param[in] width Positive bitmap width in pixels, matching the captured bitmap.
 * @param[in] height Positive bitmap height in pixels, matching the captured bitmap.
 */
void Rbc_BitmapDataToPostScript(struct PsTokenStruct *tokenPtr, Display *display, Pixmap bitmap, int width,
                                int height) {
    register unsigned char byte = 0;
    register int x, y, bitPos;
    unsigned long pixel;
    XImage *imagePtr;
    int byteCount;
    char string[10];

    imagePtr = XGetImage(display, bitmap, 0, 0, width, height, 1, ZPixmap);
    if (imagePtr == NULL) {
        return;
    }
    Rbc_AppendToPostScript(tokenPtr, "\t<", (char *)NULL);
    byteCount = bitPos = 0; /* Suppress compiler warning */
    for (y = 0; y < height; y++) {
        byte = 0;
        for (x = 0; x < width; x++) {
            pixel = XGetPixel(imagePtr, x, y);
            bitPos = x % 8;
            byte |= (unsigned char)(pixel << bitPos);
            if (bitPos == 7) {
                byte = ReverseBits(byte);
                ByteToHex(byte, string);
                string[2] = '\0';
                byteCount++;
                byte = 0;
                if (byteCount >= 30) {
                    string[2] = '\n';
                    string[3] = '\t';
                    string[4] = '\0';
                    byteCount = 0;
                }
                Rbc_AppendToPostScript(tokenPtr, string, (char *)NULL);
            }
        } /* x */
        if (bitPos != 7) {
            byte = ReverseBits(byte);
            ByteToHex(byte, string);
            string[2] = '\0';
            Rbc_AppendToPostScript(tokenPtr, string, (char *)NULL);
            byteCount++;
        }
    } /* y */
    Rbc_AppendToPostScript(tokenPtr, ">\n", (char *)NULL);
    XDestroyImage(imagePtr);
}

#endif /* WIN32 */

/**
 * @brief Append bottom-to-top raster samples as wrapped hexadecimal text.
 * @ingroup ps_raster_encoding
 *
 * Borrows a valid image, initialized result DString and NUL-terminated prefix.
 * Three components emit RGB bytes; one component emits the complement of
 * each red byte, assuming greyscale conversion was already performed.
 * Other component counts append nothing and return zero. Alpha is ignored.
 * Each physical data line starts with prefix plus a space, holds up to
 * sixty hex digits, and ends with a newline, including a partial final line.
 * Row boundaries do not force line breaks. Returns the number of appended
 * data lines, not a Tcl status; caller sizes must keep this int count valid.
 * The image is unchanged. No image operator or surrounding hex delimiters
 * are emitted; prefix must not alias storage invalidated by DString growth.
 *
 * @param[in] image Borrowed readable image; RGB output ignores alpha, and one-component output assumes
 *    greyscale already computed.
 * @param[in] nComponents Three for RGB or one for complemented red-channel samples; other values append
 *    nothing.
 * @param[inout] resultPtr Caller-initialized DString receiving hex text and line separators.
 * @param[in] prefix NUL-terminated prefix for each physical data line; must not alias storage relocated by
 *    result growth.
 */
int Rbc_ColorImageToPsData(Rbc_ColorImage image, int nComponents, Tcl_DString *resultPtr, char *prefix) {
    char string[10];
    register int count;
    register int x, y;
    register Pix32 *pixelPtr;
    unsigned char byte;
    int width, height;
    int nLines;

    width = Rbc_ColorImageWidth(image);
    height = Rbc_ColorImageHeight(image);
    nLines = 0;
    count = 0;
    if (nComponents == 3) {
        for (y = height - 1; y >= 0; y--) {
            pixelPtr = Rbc_ColorImagePixel(image, 0, y);
            for (x = 0; x < width; x++, pixelPtr++) {
                if (count == 0) {
                    Tcl_DStringAppend(resultPtr, prefix, -1);
                    Tcl_DStringAppend(resultPtr, " ", -1);
                }
                count += 6;
                ByteToHex(pixelPtr->rgba.red, string);
                ByteToHex(pixelPtr->rgba.green, string + 2);
                ByteToHex(pixelPtr->rgba.blue, string + 4);
                string[6] = '\0';
                if (count >= 60) {
                    string[6] = '\n';
                    string[7] = '\0';
                    count = 0;
                    nLines++;
                }
                Tcl_DStringAppend(resultPtr, string, -1);
            }
        }
    } else if (nComponents == 1) {
        for (y = height - 1; y >= 0; y--) {
            pixelPtr = Rbc_ColorImagePixel(image, 0, y);
            for (x = 0; x < width; x++, pixelPtr++) {
                if (count == 0) {
                    Tcl_DStringAppend(resultPtr, prefix, -1);
                    Tcl_DStringAppend(resultPtr, " ", -1);
                }
                count += 2;
                byte = ~(pixelPtr->rgba.red);
                ByteToHex(byte, string);
                string[2] = '\0';
                if (count >= 60) {
                    string[2] = '\n';
                    string[3] = '\0';
                    count = 0;
                    nLines++;
                }
                Tcl_DStringAppend(resultPtr, string, -1);
            }
        }
    }
    if (count != 0) {
        Tcl_DStringAppend(resultPtr, "\n", -1);
        nLines++;
    }
    return nLines;
}

#ifndef WIN32
/**
 * @brief Borrow an X11 atom name, translating Tk's bad-atom sentinel to NULL.
 * @ingroup ps_text_mapping
 *
 * Requires a live Tk window. The returned name belongs to Tk and must not
 * be freed. The helper does not acquire a font or allocate output storage.
 *
 * @param[in] tkwin Live window selecting the Tk display/atom cache.
 * @param[in] atom Atom whose Tk-owned name is requested.
 */
static const char *NameOfAtom(Tk_Window tkwin, Atom atom) {
    const char *result;

    result = Tk_GetAtomName(tkwin, atom);
    if ((result[0] == '?') && (strcmp(result, "?bad atom?") == 0)) {
        return NULL;
    }
    return result;
}
#endif /* WIN32 */

/**
 * @brief Immutable family alias and PostScript fallback-name pair.
 * @ingroup ps_text_mapping
 * Strings are static; lookup does not create or own font resources.
 */
typedef struct {
    const char *alias; /**< Borrowed alias recognized by the resolver. */
    const char *fontName; /**< Borrowed PostScript font name. */
} FontMap;

static const FontMap psFontMap[] = {
    {
        "Arial",
        "Helvetica",
    },
    {
        "AvantGarde",
        "AvantGarde",
    },
    {
        "Courier New",
        "Courier",
    },
    {
        "Courier",
        "Courier",
    },
    {
        "Geneva",
        "Helvetica",
    },
    {
        "Helvetica",
        "Helvetica",
    },
    {
        "Monaco",
        "Courier",
    },
    {
        "New Century Schoolbook",
        "NewCenturySchlbk",
    },
    {
        "New York",
        "Times",
    },
    {
        "Palatino",
        "Palatino",
    },
    {
        "Symbol",
        "Symbol",
    },
    {
        "Times New Roman",
        "Times",
    },
    {
        "Times Roman",
        "Times",
    },
    {
        "Times",
        "Times",
    },
    {
        "Utopia",
        "Utopia",
    },
    {
        "ZapfChancery",
        "ZapfChancery",
    },
    {
        "ZapfDingbats",
        "ZapfDingbats",
    },
};

static const size_t nFontNames = sizeof(psFontMap) / sizeof(psFontMap[0]);

#ifndef WIN32
/**
 * @brief Build a legacy PostScript name from borrowed X11 font properties.
 * @ingroup ps_text_mapping
 *
 * Requires an initialized result DString and a valid XFontStruct. Missing
 * FULL_NAME, FOUNDRY or FAMILY_NAME returns NULL without replacing the result.
 * Adobe families are kept; other foundries remove an optional ITC prefix
 * and use an exact case-insensitive alias match, defaulting to Helvetica.
 * If the full name starts with the original family, its remaining typeface
 * is appended after a dash with spaces and dashes removed. Success replaces
 * resultPtr and returns a borrowed pointer into it. No font is released here.
 *
 * @param[in] tkwin Live window used to resolve property atom names.
 * @param[in] fontPtr Borrowed valid X font structure with the required name/family/foundry properties.
 * @param[inout] resultPtr Initialized DString replaced on success; returned name borrows this buffer.
 */
static const char *XFontStructToPostScript(Tk_Window tkwin, XFontStruct *fontPtr, Tcl_DString *resultPtr) {
    Atom atom;
    const char *fullName, *family, *foundry, *src;
    size_t familyLen;
    size_t i;

    if (XGetFontProperty(fontPtr, XA_FULL_NAME, &atom) == False) {
        return NULL;
    }
    fullName = NameOfAtom(tkwin, atom);
    if (fullName == NULL) {
        return NULL;
    }
    family = foundry = NULL;
    if (XGetFontProperty(fontPtr, Tk_InternAtom(tkwin, "FOUNDRY"), &atom)) {
        foundry = NameOfAtom(tkwin, atom);
    }
    if (XGetFontProperty(fontPtr, XA_FAMILY_NAME, &atom)) {
        family = NameOfAtom(tkwin, atom);
    }
    if ((foundry == NULL) || (family == NULL)) {
        return NULL;
    }
    /*
     * Save the typeface portion of the full name before mapping the
     * family name to its PostScript equivalent.
     */
    src = NULL;
    familyLen = strlen(family);
    if (strncasecmp(fullName, family, familyLen) == 0) {
        src = fullName + familyLen;
    }
    if (strcmp(foundry, "Adobe") != 0) {
        if (strncasecmp(family, "itc ", 4) == 0) {
            family += 4;
        }
        for (i = 0; i < nFontNames; i++) {
            if (strcasecmp(family, psFontMap[i].alias) == 0) {
                family = psFontMap[i].fontName;
                break;
            }
        }
        if (i == nFontNames) {
            family = "Helvetica";
        }
    }
    Tcl_DStringSetLength(resultPtr, 0);
    Tcl_DStringAppend(resultPtr, family, -1);
    /*
     * Append the typeface portion, removing spaces and dashes.
     *
     * Example:
     *
     *      " Bold Italic" -> "-BoldItalic"
     */
    if (src != NULL) {
        const char *p;
        int haveTypeface;

        haveTypeface = 0;
        for (p = src; *p != '\0'; p++) {
            if ((*p != ' ') && (*p != '-')) {
                haveTypeface = 1;
                break;
            }
        }
        if (haveTypeface) {
            Tcl_DStringAppend(resultPtr, "-", 1);
            while (*src != '\0') {
                if ((*src != ' ') && (*src != '-')) {
                    Tcl_DStringAppend(resultPtr, src, 1);
                }
                src++;
            }
        }
    }
    return Tcl_DStringValue(resultPtr);
}

#endif /* !WIN32 */

/*
 * -------------------------------------------------------------------
 * Routines to convert X drawing functions to PostScript commands.
 * -------------------------------------------------------------------
 */

/**
 * @brief Select white through the prolog background-color operator.
 * @ingroup ps_legacy_primitives
 *
 * Appends SetBgColor operands only; it does not erase or fill any region.
 * It bypasses the optional Tcl color map and does not restore prior color.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 */
void Rbc_ClearBackgroundToPostScript(struct PsTokenStruct *tokenPtr) {
    Rbc_AppendToPostScript(tokenPtr, " 1.0 1.0 1.0 SetBgColor\n", (char *)NULL);
}

/**
 * @brief Append an X11-to-PostScript line-cap conversion.
 * @ingroup ps_legacy_primitives
 *
 * CapNotLast and CapButt both map to butt; positive X11 values are reduced
 * by one. The caller supplies a valid cap enumeration; no range validation
 * or graphics-state restoration is performed.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] capStyle Valid X cap-style enumeration; CapNotLast and CapButt both produce butt caps.
 */
void Rbc_CapStyleToPostScript(struct PsTokenStruct *tokenPtr, int capStyle) {
    /*
     * X11:not last = 0, butt = 1, round = 2, projecting = 3
     * PS: butt = 0, round = 1, projecting = 2
     */
    if (capStyle > 0) {
        capStyle--;
    }
    Rbc_FormatToPostScript(tokenPtr, "%d setlinecap\n", capStyle);
}

/**
 * @brief Append the join enumeration directly to setlinejoin.
 * @ingroup ps_legacy_primitives
 *
 * X11 and PostScript share miter=0, round=1 and bevel=2. The caller must
 * supply a valid enumeration; the previous setting is not restored.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] joinStyle Valid X join-style enumeration, directly compatible with PostScript.
 */
void Rbc_JoinStyleToPostScript(struct PsTokenStruct *tokenPtr, int joinStyle) {
    /*
     * miter = 0, round = 1, bevel = 2
     */
    Rbc_FormatToPostScript(tokenPtr, "%d setlinejoin\n", joinStyle);
}

/**
 * @brief Append an integer line width, clamped to at least one.
 * @ingroup ps_legacy_primitives
 *
 * Zero and negative inputs become one rather than a PostScript hairline.
 * Width is expressed in the current user coordinate system.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] lineWidth Integer width in current user units; values below one are clamped to one.
 */
void Rbc_LineWidthToPostScript(struct PsTokenStruct *tokenPtr, int lineWidth) {
    if (lineWidth < 1) {
        lineWidth = 1;
    }
    Rbc_FormatToPostScript(tokenPtr, "%d setlinewidth\n", lineWidth);
}

/**
 * @brief Append a zero-terminated byte dash list with zero phase.
 * @ingroup ps_legacy_primitives
 *
 * A NULL pointer or empty values array emits a solid-line dash array.
 * A non-NULL values array must contain a zero terminator. The stored dash
 * offset is not emitted; this helper always uses phase zero.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] dashesPtr Borrowed zero-terminated byte pattern, or NULL for solid lines; offset is ignored.
 */
void Rbc_LineDashesToPostScript(struct PsTokenStruct *tokenPtr, Rbc_Dashes *dashesPtr) {

    Rbc_AppendToPostScript(tokenPtr, "[ ", (char *)NULL);
    if (dashesPtr != NULL) {
        unsigned char *p;

        for (p = dashesPtr->values; *p != 0; p++) {
            Rbc_FormatToPostScript(tokenPtr, " %d", *p);
        }
    }
    Rbc_AppendToPostScript(tokenPtr, "] 0 setdash\n", (char *)NULL);
}

/**
 * @brief Append join, cap, foreground, width and dashes in that order.
 * @ingroup ps_legacy_primitives
 *
 * Borrows all inputs and inherits the foreground color-map callback boundary.
 * Finally defines an empty DashesProc in the current dictionary. This is
 * not a scoped state change; subsequent strokes use the emitted attributes.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] colorPtr Non-NULL borrowed color; keep it valid across optional mapping-variable traces.
 * @param[in] lineWidth Integer width in current user units; values below one are clamped to one.
 * @param[in] dashesPtr Borrowed zero-terminated byte pattern, or NULL for solid lines; offset is ignored.
 * @param[in] capStyle Valid X cap-style enumeration; CapNotLast and CapButt both produce butt caps.
 * @param[in] joinStyle Valid X join-style enumeration, directly compatible with PostScript.
 */
void Rbc_LineAttributesToPostScript(struct PsTokenStruct *tokenPtr, XColor *colorPtr, int lineWidth,
                                    Rbc_Dashes *dashesPtr, int capStyle, int joinStyle) {
    Rbc_JoinStyleToPostScript(tokenPtr, joinStyle);
    Rbc_CapStyleToPostScript(tokenPtr, capStyle);
    Rbc_ForegroundToPostScript(tokenPtr, colorPtr);
    Rbc_LineWidthToPostScript(tokenPtr, lineWidth);
    Rbc_LineDashesToPostScript(tokenPtr, dashesPtr);
    Rbc_AppendToPostScript(tokenPtr, "/DashesProc {} def\n", (char *)NULL);
}

/**
 * @brief Append a Box followed by the built-in fill operator.
 * @ingroup ps_legacy_primitives
 *
 * Receives already mapped coordinates and dimensions. The prolog Box starts
 * a fresh closed path; fill consumes it. No color, clipping or validation
 * is added, and no outline is stroked.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] x Finite mapped horizontal position in the current PostScript user coordinate system.
 * @param[in] y Finite mapped vertical position in the current PostScript user coordinate system.
 * @param[in] width Rectangle width in mapped user units; caller supplies valid bounded geometry.
 * @param[in] height Rectangle height in mapped user units; caller supplies valid bounded geometry.
 */
void Rbc_RectangleToPostScript(struct PsTokenStruct *tokenPtr, double x, double y, int width, int height) {
    Rbc_FormatToPostScript(tokenPtr, "%g %g %d %d Box fill\n\n", x, y, width, height);
}

/**
 * @brief Append a fresh closed rectangular path without painting it.
 * @ingroup ps_legacy_primitives
 *
 * Uses the prolog Box procedure. The caller decides whether to fill, stroke
 * or clip the resulting path and supplies valid mapped dimensions.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] x Finite mapped horizontal position in the current PostScript user coordinate system.
 * @param[in] y Finite mapped vertical position in the current PostScript user coordinate system.
 * @param[in] width Rectangle width in mapped user units; caller supplies valid bounded geometry.
 * @param[in] height Rectangle height in mapped user units; caller supplies valid bounded geometry.
 */
void Rbc_RegionToPostScript(struct PsTokenStruct *tokenPtr, double x, double y, int width, int height) {
    Rbc_FormatToPostScript(tokenPtr, "%g %g %d %d Box\n\n", x, y, width, height);
}

/**
 * @brief Append a new open path from borrowed floating-point vertices.
 * @ingroup ps_legacy_primitives
 *
 * A nonpositive count appends nothing and leaves the prior path untouched.
 * For a positive count, screenPts must contain that many points. Emits one
 * moveto and remaining lineto operations, with no stroke, close, clipping
 * or path-size splitting. Numeric inputs must be suitable for PostScript.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] screenPts Borrowed array of nScreenPts mapped finite vertices when the count is positive.
 * @param[in] nScreenPts Vertex count; nonpositive counts leave output and the current path unchanged.
 */
void Rbc_PathToPostScript(struct PsTokenStruct *tokenPtr, register Point2D *screenPts, Tcl_Size nScreenPts) {
    register Point2D *pointPtr, *endPtr;

    if (nScreenPts <= 0) {
        return;
    }
    pointPtr = screenPts;
    Rbc_FormatToPostScript(tokenPtr, "newpath %g %g moveto\n", pointPtr->x, pointPtr->y);
    pointPtr++;
    endPtr = screenPts + nScreenPts;
    while (pointPtr < endPtr) {
        Rbc_FormatToPostScript(tokenPtr, "%g %g lineto\n", pointPtr->x, pointPtr->y);
        pointPtr++;
    }
}

/**
 * @brief Close and fill a borrowed floating-point polygon through Fill.
 * @ingroup ps_legacy_primitives
 *
 * Nonpositive counts append nothing. Otherwise builds a fresh path, adds
 * an explicit line back to the first point, closes it and calls the prolog
 * Fill procedure. Unlike built-in fill, Fill preserves the path. This helper
 * does not validate polygon area, split large paths or select a color.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] screenPts Borrowed array of nScreenPts mapped finite vertices when the count is positive.
 * @param[in] nScreenPts Vertex count; nonpositive counts append nothing; polygon validity is not checked.
 */
void Rbc_PolygonToPostScript(struct PsTokenStruct *tokenPtr, Point2D *screenPts, Tcl_Size nScreenPts) {
    if (nScreenPts <= 0) {
        return;
    }
    Rbc_PathToPostScript(tokenPtr, screenPts, nScreenPts);
    Rbc_FormatToPostScript(tokenPtr, "%g %g ", screenPts[0].x, screenPts[0].y);
    Rbc_AppendToPostScript(tokenPtr, " lineto closepath Fill\n", (char *)NULL);
}

/**
 * @brief Append independent integer segments with DashesProc stroke.
 * @ingroup ps_legacy_primitives
 *
 * Borrows nSegments XSegment entries; nonpositive counts append nothing.
 * No initial newpath is emitted, so callers must ensure there is no pending
 * path to stroke along with the first segment. Requires DashesProc to be
 * defined. Each stroke consumes its path and restarts the dash pattern.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] segPtr Borrowed array of nSegments integer segments when the count is positive.
 * @param[in] nSegments Number of segment records; nonpositive counts append nothing.
 */
void Rbc_SegmentsToPostScript(struct PsTokenStruct *tokenPtr, register XSegment *segPtr, int nSegments) {
    register int i;

    for (i = 0; i < nSegments; i++, segPtr++) {
        Rbc_FormatToPostScript(tokenPtr, "%d %d moveto\n", segPtr->x1, segPtr->y1);
        Rbc_FormatToPostScript(tokenPtr, " %d %d lineto\n", segPtr->x2, segPtr->y2);
        Rbc_AppendToPostScript(tokenPtr, "DashesProc stroke\n", (char *)NULL);
    }
}

/**
 * @brief Fill each borrowed integer rectangle with the current color.
 * @ingroup ps_legacy_primitives
 *
 * Delegates each entry to Rbc_RectangleToPostScript; nonpositive counts
 * append nothing. No outline, color selection or clipping is supplied.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] rectArr Borrowed array of nRects mapped integer rectangles when the count is positive.
 * @param[in] nRects Number of rectangle records; nonpositive counts append nothing.
 */
void Rbc_RectanglesToPostScript(struct PsTokenStruct *tokenPtr, XRectangle rectArr[], int nRects) {
    register int i;

    for (i = 0; i < nRects; i++) {
        Rbc_RectangleToPostScript(tokenPtr, (double)rectArr[i].x, (double)rectArr[i].y, (int)rectArr[i].width,
                                  (int)rectArr[i].height);
    }
}

/**
 * @brief Append the border bands of a Tk relief rectangle.
 * @ingroup ps_legacy_primitives
 *
 * Borrows a live border and the token window. Requires nonnegative bounded
 * dimensions and borderWidth with representable integer arithmetic. Returns
 * without output if either dimension is smaller than twice borderWidth.
 * Initializes Tk shadow colors before querying them. Groove and ridge use
 * two recursive half-width relief bands; raised and sunken swap light/dark
 * top-left and bottom-right colors. Other cases retain the existing solid
 * or background-color selection. Emits bottom/right rectangles and a
 * top/left polygon; does not fill the whole interior or restore color/path.
 * Color-map reads inherit the enclosing export callback requirements.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] border Borrowed live Tk border compatible with the token window.
 * @param[in] x Finite mapped horizontal position in the current PostScript user coordinate system.
 * @param[in] y Finite mapped vertical position in the current PostScript user coordinate system.
 * @param[in] width Rectangle width in mapped user units; caller supplies valid bounded geometry.
 * @param[in] height Rectangle height in mapped user units; caller supplies valid bounded geometry.
 * @param[in] borderWidth Nonnegative band width with representable doubling and inset arithmetic.
 * @param[in] relief Tk relief selecting solid, raised, sunken, groove, ridge or flat band treatment.
 */
void Rbc_Draw3DRectangleToPostScript(struct PsTokenStruct *tokenPtr, Tk_3DBorder border, double x, double y, int width,
                                     int height, int borderWidth, int relief) {
    XColor lightColor, darkColor;
    XColor *lightColorPtr, *darkColorPtr;
    XColor *topColor, *bottomColor;
    Point2D points[7];
    int twiceWidth = (borderWidth * 2);
    XColor bdBgColor, bdLightColor, bdDarkColor;

    if ((width < twiceWidth) || (height < twiceWidth)) {
        return;
    }
    /* Export can precede the first screen draw that initializes shadows. */
    (void)Tk_3DBorderGC(tokenPtr->tkwin, border, TK_3D_LIGHT_GC);
    Tk_Get3DBorderColors(border, &bdBgColor, &bdDarkColor, &bdLightColor);
    if (relief == TK_RELIEF_SOLID) {
        if (bdBgColor.pixel == bdDarkColor.pixel && bdBgColor.pixel == bdLightColor.pixel) {
            Screen *screenPtr;

            lightColor = bdBgColor;
            screenPtr = Tk_Screen(tokenPtr->tkwin);
            if (lightColor.pixel == WhitePixelOfScreen(screenPtr)) {
                darkColor.red = darkColor.blue = darkColor.green = 0x00;
            } else {
                darkColor.red = darkColor.blue = darkColor.green = 0xFF;
            }
        } else {
            darkColor.red = darkColor.blue = darkColor.green = 0x00;
            lightColor.red = lightColor.blue = lightColor.green = 0x00;
            relief = TK_RELIEF_SUNKEN;
        }
        lightColorPtr = &lightColor;
        darkColorPtr = &darkColor;
    } else {
        lightColorPtr = &bdLightColor;
        darkColorPtr = &bdDarkColor;
    }
    /*
     * Handle grooves and ridges with recursive calls.
     */
    if ((relief == TK_RELIEF_GROOVE) || (relief == TK_RELIEF_RIDGE)) {
        int halfWidth, insideOffset;

        halfWidth = borderWidth / 2;
        insideOffset = borderWidth - halfWidth;
        Rbc_Draw3DRectangleToPostScript(tokenPtr, border, (double)x, (double)y, width, height, halfWidth,
                                        (relief == TK_RELIEF_GROOVE) ? TK_RELIEF_SUNKEN : TK_RELIEF_RAISED);
        Rbc_Draw3DRectangleToPostScript(tokenPtr, border, (double)(x + insideOffset), (double)(y + insideOffset),
                                        width - insideOffset * 2, height - insideOffset * 2, halfWidth,
                                        (relief == TK_RELIEF_GROOVE) ? TK_RELIEF_RAISED : TK_RELIEF_SUNKEN);
        return;
    }
    if (relief == TK_RELIEF_RAISED) {
        topColor = lightColorPtr;
        bottomColor = darkColorPtr;
    } else if (relief == TK_RELIEF_SUNKEN) {
        topColor = darkColorPtr;
        bottomColor = lightColorPtr;
    } else {
        topColor = bottomColor = &bdBgColor;
    }
    Rbc_BackgroundToPostScript(tokenPtr, bottomColor);
    Rbc_RectangleToPostScript(tokenPtr, x, y + height - borderWidth, width, borderWidth);
    Rbc_RectangleToPostScript(tokenPtr, x + width - borderWidth, y, borderWidth, height);
    points[0].x = points[1].x = points[6].x = x;
    points[0].y = points[6].y = y + height;
    points[1].y = points[2].y = y;
    points[2].x = x + width;
    points[3].x = x + width - borderWidth;
    points[3].y = points[4].y = y + borderWidth;
    points[4].x = points[5].x = x + borderWidth;
    points[5].y = y + height - borderWidth;
    if (relief != TK_RELIEF_FLAT) {
        Rbc_BackgroundToPostScript(tokenPtr, topColor);
    }
    Rbc_PolygonToPostScript(tokenPtr, points, 7);
}

/**
 * @brief Fill the background rectangle, then append its relief border.
 * @ingroup ps_legacy_primitives
 *
 * Borrows the border and token window. Selects its background with
 * SetBgColor semantics before the fill, then delegates border geometry to
 * Rbc_Draw3DRectangleToPostScript. Inherits that helper arithmetic and
 * lifetime requirements; border omission does not undo the initial fill.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] border Borrowed live Tk border compatible with the token window.
 * @param[in] x Finite mapped horizontal position in the current PostScript user coordinate system.
 * @param[in] y Finite mapped vertical position in the current PostScript user coordinate system.
 * @param[in] width Rectangle width in mapped user units; caller supplies valid bounded geometry.
 * @param[in] height Rectangle height in mapped user units; caller supplies valid bounded geometry.
 * @param[in] borderWidth Nonnegative band width with representable doubling and inset arithmetic.
 * @param[in] relief Tk relief applied after filling the entire background rectangle.
 */
void Rbc_Fill3DRectangleToPostScript(struct PsTokenStruct *tokenPtr, Tk_3DBorder border, double x, double y, int width,
                                     int height, int borderWidth, int relief) {
    XColor bdBgColor;

    Tk_Get3DBorderColors(border, &bdBgColor, NULL, NULL);
    /*
     * I'm assuming that the rectangle is to be drawn as a background.
     * Setting the pen color as foreground or background only affects
     * the plot when the colormode option is "monochrome".
     */
    Rbc_BackgroundToPostScript(tokenPtr, &bdBgColor);
    Rbc_RectangleToPostScript(tokenPtr, x, y, width, height);
    Rbc_Draw3DRectangleToPostScript(tokenPtr, border, x, y, width, height, borderWidth, relief);
}

/**
 * @brief Fill the current clipping shape with a borrowed Tk bitmap pattern.
 * @ingroup ps_raster_encoding
 *
 * Queries bitmap dimensions, saves graphics state, intersects the clip with
 * the current path, emits bitmap data and calls prolog StippleFill before
 * restoring state. Requires a valid current path and registered Tk bitmap.
 * Bitmap acquisition failure has no status return and is not rolled back.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] display Display owning the borrowed bitmap.
 * @param[in] bitmap Borrowed registered Tk bitmap with valid dimensions; repeated within the current path
 *    clip.
 */
void Rbc_StippleToPostScript(struct PsTokenStruct *tokenPtr, Display *display, Pixmap bitmap) {
    int width, height;

    Tk_SizeOfBitmap(display, bitmap, &width, &height);
    Rbc_FormatToPostScript(tokenPtr, "gsave\n  clip\n  %d %d\n", width, height);
    Rbc_BitmapDataToPostScript(tokenPtr, display, bitmap, width, height);
    Rbc_AppendToPostScript(tokenPtr, "  StippleFill\ngrestore\n", (char *)NULL);
}

/**
 * @brief Append a positioned RGB or greyscale raster image program.
 * @ingroup ps_raster_encoding
 *
 * Borrows a valid image and token; does not release the image. Color mode
 * selects three-component colorimage; other modes convert image RGB values
 * to greyscale in place and emit complemented red samples through image.
 * Alpha is not composited or encoded. Translation and scaling are enclosed
 * in gsave/grestore, but tmpStr is a dictionary definition, not saved state.
 * Uses an eight-bit image matrix with negative vertical scale. No Tcl error
 * status is returned. The row-byte count is formatted as size_t without
 * narrowing to int.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[inout] image Borrowed valid image; noncolor modes replace RGB values with greyscale in place, and all
 *    modes ignore alpha.
 * @param[in] x Finite mapped horizontal position in the current PostScript user coordinate system.
 * @param[in] y Finite mapped vertical position in the current PostScript user coordinate system.
 */
void Rbc_ColorImageToPostScript(struct PsTokenStruct *tokenPtr, Rbc_ColorImage image, double x, double y) {
    int width, height;
    size_t tmpSize;

    width = Rbc_ColorImageWidth(image);
    height = Rbc_ColorImageHeight(image);
    tmpSize = (size_t)width;
    if (tokenPtr->colorMode == PS_MODE_COLOR) {
        if (tmpSize > SIZE_MAX / 3) {
            return;
        }
        tmpSize *= 3;
    }
    Rbc_FormatToPostScript(tokenPtr, "\n/tmpStr %zu string def\n", tmpSize);
    Rbc_AppendToPostScript(tokenPtr, "gsave\n", (char *)NULL);
    Rbc_FormatToPostScript(tokenPtr, "  %g %g translate\n", x, y);
    Rbc_FormatToPostScript(tokenPtr, "  %d %d scale\n", width, height);
    Rbc_FormatToPostScript(tokenPtr, "  %d %d 8\n", width, height);
    Rbc_FormatToPostScript(tokenPtr, "  [%d 0 0 %d 0 %d] ", width, -height, height);
    Rbc_AppendToPostScript(tokenPtr, "{\n    currentfile tmpStr readhexstring pop\n  } ", (char *)NULL);
    if (tokenPtr->colorMode != PS_MODE_COLOR) {
        Rbc_AppendToPostScript(tokenPtr, "image\n", (char *)NULL);
        Rbc_ColorImageToGreyscale(image);
        Rbc_ColorImageToPsData(image, 1, &(tokenPtr->dString), " ");
    } else {
        Rbc_AppendToPostScript(tokenPtr, "false 3 colorimage\n", (char *)NULL);
        Rbc_ColorImageToPsData(image, 3, &(tokenPtr->dString), " ");
    }
    Rbc_AppendToPostScript(tokenPtr, "\ngrestore\n\n", (char *)NULL);
}

/**
 * @brief Capture a window into an owned temporary image and serialize it.
 * @ingroup ps_raster_encoding
 *
 * Borrows a live window and token; captures its current dimensions at GAMMA.
 * On success frees the temporary image after serialization. A failed capture
 * instead emits a comment and a grey background rectangle of the same size.
 * The fallback changes background color without restoring it and does not
 * report a Tcl error. This is capture of native pixels, not widget commands.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] tkwin Live window with native pixels available for capture at its current dimensions.
 * @param[in] x Finite mapped horizontal position in the current PostScript user coordinate system.
 * @param[in] y Finite mapped vertical position in the current PostScript user coordinate system.
 */
void Rbc_WindowToPostScript(struct PsTokenStruct *tokenPtr, Tk_Window tkwin, double x, double y) {
    Rbc_ColorImage image;
    int width, height;

    width = Tk_Width(tkwin);
    height = Tk_Height(tkwin);
    image = Rbc_DrawableToColorImage(tkwin, Tk_WindowId(tkwin), 0, 0, width, height, GAMMA);
    if (image == NULL) {
        /* Can't grab window image so paint the window area grey */
        Rbc_AppendToPostScript(tokenPtr, "% Can't grab window \"", Tk_PathName(tkwin), "\"\n", (char *)NULL);
        Rbc_AppendToPostScript(tokenPtr, "0.5 0.5 0.5 SetBgColor\n", (char *)NULL);
        Rbc_RectangleToPostScript(tokenPtr, x, y, width, height);
        return;
    }
    Rbc_ColorImageToPostScript(tokenPtr, image, x, y);
    Rbc_FreeColorImage(image);
}

/**
 * @brief Copy a Tk photo into a temporary color image and serialize it.
 * @ingroup ps_raster_encoding
 *
 * Borrows the photo and token. Releases the temporary image after output,
 * so greyscale conversion does not modify the original photo. Requires a
 * valid photo handle; no conversion failure status is propagated. Inherits
 * the serializer alpha omission and positioning conventions.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] photo Valid borrowed Tk photo; a temporary image copy absorbs any greyscale conversion.
 * @param[in] x Finite mapped horizontal position in the current PostScript user coordinate system.
 * @param[in] y Finite mapped vertical position in the current PostScript user coordinate system.
 */
void Rbc_PhotoToPostScript(struct PsTokenStruct *tokenPtr, Tk_PhotoHandle photo, double x, double y) {
    Rbc_ColorImage image;

    image = Rbc_PhotoToColorImage(photo);
    Rbc_ColorImageToPostScript(tokenPtr, image, x, y);
    Rbc_FreeColorImage(image);
}

/**
 * @brief Append a PostScript font name and size using the legacy fallback chain.
 * @ingroup ps_text_mapping
 *
 * Borrows the token and Tk font. A found font-map array entry takes priority:
 * its first list element supplies the raw font name; exactly two elements
 * with an integer second element supply the size. Otherwise size remains
 * twelve, including one-element entries. Parse errors do not stop output.
 * Without a mapped entry, recognized family prefixes use Tk_PostscriptFontName.
 * Other X11 families attempt XLoadQueryFont and property-based mapping, freeing
 * the loaded font after copying its name into a temporary DString. Windows
 * skips this attempt. Final fallback is Helvetica-Bold, normally at size twelve.
 * Releases temporary Tcl descriptions, split lists and DStrings. No glyphs
 * are embedded. Variable reads can execute traces; enclosing export code
 * owns callback lifetime protection. This void helper does not report failure.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] font Borrowed Tk font, kept valid across optional font-map variable traces; no program is
 *    embedded.
 */
void Rbc_FontToPostScript(struct PsTokenStruct *tokenPtr, Tk_Font font) {
#ifndef WIN32
    XFontStruct *fontPtr = (XFontStruct *)font;
    Tcl_DString mappedName;
#endif /* !WIN32 */
    Tcl_Interp *interp = tokenPtr->interp;
    const char *fontName;
    double pointSize;
    Tk_Uid family;
    size_t i;
    Tcl_Obj *fontDescObj, *familyObj;

    fontName = Tk_NameOfFont(font);
    pointSize = 12.0;
    /*
     * Use the font variable information if it exists.
     */
    if (tokenPtr->fontVarName != NULL) {
        const char *fontInfo;

        fontInfo = Tcl_GetVar2(interp, tokenPtr->fontVarName, fontName, 0);
        if (fontInfo != NULL) {
            Tcl_Size nProps;
            const char **propArr = NULL;

            if (Tcl_SplitList(interp, fontInfo, &nProps, &propArr) == TCL_OK) {
                int newSize;

                if (nProps > 0) {
                    fontName = propArr[0];
                }
                if ((nProps == 2) && (Tcl_GetInt(interp, propArr[1], &newSize) == TCL_OK)) {
                    pointSize = (double)newSize;
                }
            }
            Rbc_FormatToPostScript(tokenPtr, "%g /%s SetFont\n", pointSize, fontName);
            if (propArr != NULL) {
                ckfree((char *)propArr);
            }
            return;
        }
    }
    /*
     * Otherwise do a quick test to see if it's a PostScript font.
     * Tk_PostScriptFontName will silently generate a bogus PostScript
     * font description, so we have to check to see if this is really a
     * PostScript font.
     */
    fontDescObj = Tk_FontGetDescription(font);
    Tcl_ListObjIndex(NULL, fontDescObj, 0, &familyObj); /* index 0 is family */
    family = Tcl_GetString(familyObj);
    for (i = 0; i < nFontNames; i++) {
        if (strncasecmp(psFontMap[i].alias, family, strlen(psFontMap[i].alias)) == 0) {
            Tcl_DString dString;

            Tcl_DStringInit(&dString);
            pointSize = (double)Tk_PostscriptFontName(font, &dString);
            fontName = Tcl_DStringValue(&dString);
            Rbc_FormatToPostScript(tokenPtr, "%g /%s SetFont\n", pointSize, fontName);
            Tcl_DStringFree(&dString);
            Tcl_DecrRefCount(fontDescObj);
            return;
        }
    }
    Tcl_DecrRefCount(fontDescObj);
    /*
     * Can't find it. Try to use the current point size.
     */
    fontName = NULL;
    pointSize = 12.0;
#ifndef WIN32
    /*
     * Can you believe what I have to go through to get an XFontStruct?
     */
    Tcl_DStringInit(&mappedName);
    fontPtr = XLoadQueryFont(Tk_Display(tokenPtr->tkwin), Tk_NameOfFont(font));
    if (fontPtr != NULL) {
        unsigned long fontProp;

        if (XGetFontProperty(fontPtr, XA_POINT_SIZE, &fontProp) != False) {
            pointSize = (double)fontProp / 10.0;
        }
        fontName = XFontStructToPostScript(tokenPtr->tkwin, fontPtr, &mappedName);
        XFreeFont(Tk_Display(tokenPtr->tkwin), fontPtr);
    }
#endif /* !WIN32 */
    if ((fontName == NULL) || (fontName[0] == '\0')) {
        fontName = "Helvetica-Bold";
    }
    Rbc_FormatToPostScript(tokenPtr, "%g /%s SetFont\n", pointSize, fontName);
#ifndef WIN32
    Tcl_DStringFree(&mappedName);
#endif /* !WIN32 */
}

/**
 * @brief Append nonempty layout fragments as adjusted PostScript text.
 * @ingroup ps_text_mapping
 *
 * Borrows a live token and layout. Empty fragments are skipped, avoiding the
 * prolog division by string length. Each other fragment uses escaped Latin-1
 * literal contents and its measured width with DrawAdjText. Coordinates add
 * x/y to fragment baselines in Tcl_WideInt and emit the full sum, avoiding
 * signed int overflow. Does not choose fonts, colors or transforms.
 *
 * @param[inout] psToken Live borrowed PostScript token receiving output.
 * @param[in] x Horizontal offset added to fragment baselines using wide arithmetic.
 * @param[in] y Vertical offset added to fragment baselines using wide arithmetic.
 * @param[in] textPtr Borrowed layout with live UTF fragment strings, byte counts and measured screen widths.
 */
static void TextLayoutToPostScript(PsToken psToken, int x, int y, TextLayout *textPtr) {
    TextFragment *fragPtr;
    Tcl_Size i;

    fragPtr = textPtr->fragArr;
    for (i = 0; i < textPtr->nFrags; i++, fragPtr++) {
        if (fragPtr->count < 1) {
            continue;
        }
        Rbc_AppendToPostScript(psToken, "(", (char *)NULL);
        Rbc_AppendPostScriptString(psToken, fragPtr->text, fragPtr->count);
        Rbc_FormatToPostScript(psToken, ") %d %" TCL_LL_MODIFIER "d %" TCL_LL_MODIFIER "d DrawAdjText\n",
                               fragPtr->width, (Tcl_WideInt)x + fragPtr->x, (Tcl_WideInt)y + fragPtr->y);
    }
}

/**
 * @brief Append anchored, optionally shadowed text under a saved transform.
 * @ingroup ps_text_mapping
 *
 * NULL or empty strings append nothing. Borrows the string, style and Tk
 * resources; allocates and frees a temporary layout. Requires finite bounded
 * geometry and angles compatible with FMOD and ROUND. The bounding-box
 * calculation normalizes the remainder into [0, 360), while BeginText
 * receives the original style angle.
 * Selects the font, optional positive-offset shadow, then active or normal
 * foreground. Does not implement native disabled/emphasis two-pass relief.
 * BeginText/EndText save and restore graphics state; style fields are not
 * modified. Font/color array traces require enclosing lifetime protection,
 * including the borrowed string retained by layout fragments.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] string Borrowed NUL-terminated Tcl UTF text; NULL/empty emits nothing; must survive mapping
 *    traces.
 * @param[in] tsPtr Borrowed style and live font/colors; theta is unchanged and must satisfy FMOD/ROUND bounds.
 * @param[in] x Finite horizontal anchor coordinate in mapped user units.
 * @param[in] y Finite vertical anchor coordinate in mapped user units.
 */
void Rbc_TextToPostScript(struct PsTokenStruct *tokenPtr, char *string, TextStyle *tsPtr, double x, double y) {
    double theta;
    double rotWidth, rotHeight;
    TextLayout *textPtr;
    Point2D anchorPos;

    if ((string == NULL) || (*string == '\0')) { /* Empty string, do nothing */
        return;
    }
    theta = FMOD(tsPtr->theta, (double)360.0);
    if (theta < 0.0) {
        theta += 360.0;
    }
    textPtr = Rbc_GetTextLayout(string, tsPtr);
    Rbc_GetBoundingBox(textPtr->width, textPtr->height, theta, &rotWidth, &rotHeight, (Point2D *)NULL);
    /*
     * Find the center of the bounding box
     */
    anchorPos.x = x, anchorPos.y = y;
    anchorPos = Rbc_TranslatePoint(&anchorPos, ROUND(rotWidth), ROUND(rotHeight), tsPtr->anchor);
    anchorPos.x += (rotWidth * 0.5);
    anchorPos.y += (rotHeight * 0.5);
    /* Initialize text (sets translation and rotation) */
    Rbc_FormatToPostScript(tokenPtr, "%d %d %g %g %g BeginText\n", textPtr->width, textPtr->height, tsPtr->theta,
                           anchorPos.x, anchorPos.y);
    Rbc_FontToPostScript(tokenPtr, tsPtr->font);
    /* All coordinates are now relative to what was set by BeginText */
    if ((tsPtr->shadow.offset > 0) && (tsPtr->shadow.color != NULL)) {
        Rbc_ForegroundToPostScript(tokenPtr, tsPtr->shadow.color);
        TextLayoutToPostScript(tokenPtr, tsPtr->shadow.offset, tsPtr->shadow.offset, textPtr);
    }
    Rbc_ForegroundToPostScript(tokenPtr, (tsPtr->state & STATE_ACTIVE) ? tsPtr->activeColor : tsPtr->color);
    TextLayoutToPostScript(tokenPtr, 0, 0, textPtr);
    ckfree((char *)textPtr);
    Rbc_AppendToPostScript(tokenPtr, "EndText\n", (char *)NULL);
}

/**
 * @brief Append a stroked integer polyline with bounded path batches.
 * @ingroup ps_legacy_primitives
 *
 * Counts below two are no-ops, without reading the point array or changing
 * the output. Otherwise pointPtr must contain nPoints readable entries.
 * Starts a new path, then emits DashesProc stroke at each PS_MAXPATH
 * boundary and restarts at the shared endpoint.
 * Stroke boundaries retain connected positions but restart stroke/dash
 * processing. Requires a previously defined DashesProc; borrows the array.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] pointPtr Borrowed array of nPoints mapped integer vertices when the count is at least two.
 * @param[in] nPoints Vertex count; fewer than two appends nothing.
 */
void Rbc_LineToPostScript(struct PsTokenStruct *tokenPtr, register XPoint *pointPtr, int nPoints) {
    register int i;

    if (nPoints < 2) {
        return;
    }
    Rbc_FormatToPostScript(tokenPtr, " newpath %d %d moveto\n", pointPtr->x, pointPtr->y);
    pointPtr++;
    for (i = 1; i < (nPoints - 1); i++, pointPtr++) {
        Rbc_FormatToPostScript(tokenPtr, " %d %d lineto\n", pointPtr->x, pointPtr->y);
        if ((i % PS_MAXPATH) == 0) {
            Rbc_FormatToPostScript(tokenPtr, "DashesProc stroke\n newpath  %d %d moveto\n", pointPtr->x, pointPtr->y);
        }
    }
    Rbc_FormatToPostScript(tokenPtr, " %d %d lineto\n", pointPtr->x, pointPtr->y);
    Rbc_AppendToPostScript(tokenPtr, "DashesProc stroke\n", (char *)NULL);
}

/**
 * @brief Emit a scaled bitmap mask centered on the current origin.
 * @ingroup ps_raster_encoding
 *
 * Borrows a registered Tk bitmap and token. Queries dimensions, translates
 * by negative half scaled width and positive half scaled height, then scales
 * by scaled width and negative scaled height. Emits a byte-packed
 * one-bit imagemask with true polarity, restoring graphics state afterward.
 * Paints set bits with the current color; unset bits remain unpainted.
 * The caller supplies finite usable scale factors. Capture failures are not
 * returned or rolled back, and no bitmap resource is transferred.
 *
 * @param[inout] tokenPtr Live borrowed PostScript token receiving output; caller owns document framing, prolog
 *    and lifetime.
 * @param[in] display Display owning the borrowed bitmap.
 * @param[in] bitmap Borrowed registered Tk bitmap, drawn as a one-bit mask centered on the current origin.
 * @param[in] scaleX Finite usable horizontal scale factor, yielding a nonsingular image transform.
 * @param[in] scaleY Finite usable vertical scale factor, yielding a nonsingular image transform.
 */
void Rbc_BitmapToPostScript(struct PsTokenStruct *tokenPtr, Display *display, Pixmap bitmap, double scaleX,
                            double scaleY) {
    int width, height;
    double scaledWidth, scaledHeight;

    Tk_SizeOfBitmap(display, bitmap, &width, &height);
    scaledWidth = (double)width * scaleX;
    scaledHeight = (double)height * scaleY;
    Rbc_AppendToPostScript(tokenPtr, "  gsave\n", (char *)NULL);
    Rbc_FormatToPostScript(tokenPtr, "    %g %g translate\n", scaledWidth * -0.5, scaledHeight * 0.5);
    Rbc_FormatToPostScript(tokenPtr, "    %g %g scale\n", scaledWidth, -scaledHeight);
    Rbc_FormatToPostScript(tokenPtr, "    %d %d true [%d 0 0 %d 0 %d] {", width, height, width, -height, height);
    Rbc_BitmapDataToPostScript(tokenPtr, display, bitmap, width, height);
    Rbc_AppendToPostScript(tokenPtr, "    } imagemask\n  grestore\n", (char *)NULL);
}

/**
 * @brief Append independent floating segments after clearing any pending path.
 * @ingroup ps_legacy_primitives
 *
 * Nonpositive counts append nothing. Otherwise starts with newpath, then
 * emits each segment followed by DashesProc stroke. This prevents a path
 * retained by Fill from being stroked accidentally. Requires a defined
 * DashesProc and a readable borrowed array of nSegments entries. No numeric
 * validation, clipping or state restoration is performed.
 *
 * @param[inout] psToken Live borrowed PostScript token receiving output.
 * @param[in] segPtr Borrowed array of nSegments finite mapped floating segments when the count is positive.
 * @param[in] nSegments Number of segment records; nonpositive counts append nothing.
 */
void Rbc_2DSegmentsToPostScript(PsToken psToken, register Segment2D *segPtr, Tcl_Size nSegments) {
    register Segment2D *endPtr;

    if (nSegments <= 0) {
        return;
    }
    /* Fill preserves its path; do not stroke a preceding border or polygon. */
    Rbc_AppendToPostScript(psToken, "newpath\n", (char *)NULL);
    for (endPtr = segPtr + nSegments; segPtr < endPtr; segPtr++) {
        Rbc_FormatToPostScript(psToken, "%g %g moveto\n", segPtr->p.x, segPtr->p.y);
        Rbc_FormatToPostScript(psToken, " %g %g lineto\n", segPtr->q.x, segPtr->q.y);
        Rbc_AppendToPostScript(psToken, "DashesProc stroke\n", (char *)NULL);
    }
}
