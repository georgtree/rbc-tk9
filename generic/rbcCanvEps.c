/**
 * @file rbcCanvEps.c
 * @brief Legacy EPS canvas item parsing, preview ownership and canvas callbacks.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/*
 * To do:
 *
 *    1. Add -rotate option.  Allow arbitrary rotation of image and EPS.
 *    2. Draw color images instead of photos. This will eliminate the need
 *       to create hidden photo images.
 *    3. Create a spiffy demo that lets you edit your page description.
 */

/** @defgroup eps_canvas EPS canvas item internals
 * Private command/canvas callbacks, not installed C APIs.
 */

#include "rbcInt.h"
#include "rbcPs.h"
#include "rbcImage.h"

#ifdef HAVE_TIFF_H
#include "tiff.h"
#endif
#include <fcntl.h>

#if defined(_MSC_VER) || defined(__BORLANDC__)
#include <io.h>
#define open _open
#define close _close
#define write _write
#define unlink _unlink
#define lseek _lseek
#define fdopen _fdopen
#define fcntl _fcntl
#ifdef _MSC_VER
#define O_RDWR _O_RDWR
#define O_CREAT _O_CREAT
#define O_TRUNC _O_TRUNC
#define O_EXCL _O_EXCL
#endif /* _MSC_VER */
#endif /* _MSC_VER || __BORLANDC__ */

#define DEBUG_READER 0
#ifndef WIN32
#define PurifyPrintf printf
#endif
#define PS_PREVIEW_EPSI 0
#define PS_PREVIEW_WMF 1
#define PS_PREVIEW_TIFF 2

#define xLeft header.x1
#define xRight header.x2
#define yTop header.y1
#define yBottom header.y2

#define MAX_EPS_LINE_LENGTH 255 /* Maximum line length for a EPS file */

/** @brief Temporary EPS input parser state. */
typedef struct {
    size_t maxBytes; /**< Maximum bytes in PostScript section. Zero means read to EOF. */
    size_t bytesRead; /**< Bytes consumed from PostScript section. */
    int lineNumber; /**< Current input line number. */
    char line[MAX_EPS_LINE_LENGTH + 1]; /**< Current line buffer. */
    char hexTable[256]; /**< Hexadecimal-byte lookup table used for preview decoding. */
    char *nextPtr; /**< Current position within the buffered input. */
    FILE *f; /**< Borrowed open input stream. */
} EpsParseInfo;

/*
 * Information used for parsing configuration specs:
 */

static Tk_CustomOption tagsOption;

extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcShadowOption;

/** @brief Canvas EPS item owning parsed PostScript and preview resources. */
typedef struct {
    Tk_Item header; /**< Generic stuff that's the same for all types. MUST BE FIRST IN STRUCTURE. */
    Tk_Canvas canvas; /**< Canvas containing the EPS item. */
    int canvasX; /**< Translated (by the anchor) canvas coordinates of the EPS item. */
    int canvasY; /**< Translated (by the anchor) canvas coordinates of the EPS item. */
    int lastWidth; /**< Last known dimensions of the EPS item. This is used to know if the color image preview needs to
     * be resized.
     */
    int lastHeight; /**< Last known dimensions of the EPS item. This is used to know if the color image preview needs to
     * be resized.
     */
    Tcl_Interp *interp; /**< Borrowed canvas interpreter. */
    FILE *psFile; /**< File pointer to Encapsulated PostScript file. We'll hold this as long as the EPS item is using
     * this file.
     */
    size_t psStart; /**< File offset of PostScript code. */
    size_t psLength; /**< Length of PostScript code. If zero, indicates to read to EOF. */
    size_t wmfStart; /**< File offset of Windows Metafile preview. */
    size_t wmfLength; /**< Length of WMF portion in bytes. If zero, indicates there is no WMF preview. */
    size_t tiffStart; /**< File offset of TIFF preview. */
    size_t tiffLength; /**< Length of TIFF portion in bytes. If zero, indicates there is no TIFF preview. */
    char *previewName; /**< Configured preview image name. */
    int previewFormat; /**< Preview format selector. */
    Tk_Image preview; /**< A Tk photo image provided as a preview of the EPS contents. This image supersedes any EPS
     * preview embedded PostScript preview (EPSI).
     */
    Tk_Image tmpImage; /**< Used to display the resized preview image. Created and deleted internally. */
    Pixmap pixmap; /**< Pixmap representing scaled preview. This isn't currently used. For now we're overwriting the Tk
     * image everytime the EPS item is resized. In the future we'll use our own image routines.
     */
    Rbc_ColorImage colorImage; /**< The original photo or PostScript preview image converted to a color image. This is kept around
     * for resampling or resizing the image.
     */
    int firstLine; /**< First and last line numbers of the PostScript preview. They are used to skip over the preview
     * when encapsulating PostScript for the canvas item.
     */
    int lastLine; /**< First and last line numbers of the PostScript preview. They are used to skip over the preview
     * when encapsulating PostScript for the canvas item.
     */
    GC fillGC; /**< Graphics context to fill background of image outline if no preview image was present. */
    int llx; /**< Lower left and upper right coordinates of PostScript bounding box, retrieved from file's
     * "BoundingBox:" field.
     */
    int lly; /**< Lower left and upper right coordinates of PostScript bounding box, retrieved from file's
     * "BoundingBox:" field.
     */
    int urx; /**< Lower left and upper right coordinates of PostScript bounding box, retrieved from file's
     * "BoundingBox:" field.
     */
    int ury; /**< Lower left and upper right coordinates of PostScript bounding box, retrieved from file's
     * "BoundingBox:" field.
     */
    char *title; /**< Title, retrieved from the file's "Title:" field, to be displayed over the top of the EPS preview
     * (allocated).
     */
    Tcl_DString dString; /**< Contains the encapsulate PostScript. */

    /* User configurable fields */
    double x; /**< Canvas coordinates of the item */
    double y; /**< Canvas coordinates of the item */
    Tk_Anchor anchor; /**< Tk anchor locating the item relative to x/y. */
    char *fileName; /**< Name of the encapsulated PostScript file. If NULL, indicates that no EPS file has be
     * successfully loaded yet.
     */
    char *reqTitle; /**< Title to be displayed in the EPS item. Supersedes the title found in the EPS file. If NULL,
     * indicates that the title found in the EPS file should be used.
     */
    int width; /**< Dimensions of EPS item. If set to zero, the dimension found in the "%%BoundingBox:"
     * specification from the EPS file are used.
     */
    int height; /**< Dimensions of EPS item. If set to zero, the dimension found in the "%%BoundingBox:"
     * specification from the EPS file are used.
     */
    int showImage; /**< Indicates if the image or the outline rectangle should be displayed */
    int quick; /**< Nonzero requests quick preview resampling. */
    XColor *fillColor; /**< Fill color of the image outline. */
    Tk_3DBorder border; /**< Outline color */
    int borderWidth; /**< Preview border thickness in pixels. */
    int relief; /**< Tk preview border relief. */
    TextStyle titleStyle; /**< Font, color, etc. for title */
    Pixmap stipple; /**< Stipple for image fill */
    ClientData tiffPtr; /**< TIFF decoder handle when TIFF preview support is compiled in. */
#ifdef WIN32
    HENHMETAFILE *hMetaFile; /**< Optional Windows metafile state retained by the EPS item. */
#endif
} EpsItem;

/** @ingroup component_option_tables
 * @brief Legacy Tk_ConfigSpec table used by the EPS canvas item; its offsets address the item rather than a graph component.
 * @hideinitializer
 */
static Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", (char *)NULL, (char *)NULL, "nw", offsetof(EpsItem, anchor),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_SYNONYM, "-bd", "borderWidth", (char *)NULL, (char *)NULL, 0, 0},
    {TK_CONFIG_CUSTOM, "-borderwidth", "borderWidth", (char *)NULL, STD_BORDERWIDTH, offsetof(EpsItem, borderWidth),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_STRING, "-file", (char *)NULL, (char *)NULL, NULL, offsetof(EpsItem, fileName), TK_CONFIG_NULL_OK},
    {TK_CONFIG_FONT, "-font", "font", "Font", STD_FONT, offsetof(EpsItem, titleStyle.font), 0},
    {TK_CONFIG_COLOR, "-fill", "fill", (char *)NULL, STD_NORMAL_FOREGROUND, offsetof(EpsItem, fillColor),
     TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-fill", "fill", (char *)NULL, STD_NORMAL_FG_MONO, offsetof(EpsItem, fillColor),
     TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_CUSTOM, "-height", (char *)NULL, (char *)NULL, "0", offsetof(EpsItem, height),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_STRING, "-image", (char *)NULL, (char *)NULL, NULL, offsetof(EpsItem, previewName), TK_CONFIG_NULL_OK},
    {TK_CONFIG_JUSTIFY, "-justify", "justify", "Justify", "center", offsetof(EpsItem, titleStyle.justify),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BORDER, "-outline", "outline", (char *)NULL, RGB_BLACK, offsetof(EpsItem, border),
     TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_BORDER, "-outline", "outline", (char *)NULL, RGB_BLACK, offsetof(EpsItem, border),
     TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-quick", "quick", "Quick", "no", offsetof(EpsItem, quick), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_RELIEF, "-relief", (char *)NULL, (char *)NULL, "sunken", offsetof(EpsItem, relief),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", NULL, offsetof(EpsItem, titleStyle.shadow), TK_CONFIG_COLOR_ONLY,
     &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", NULL, offsetof(EpsItem, titleStyle.shadow), TK_CONFIG_MONO_ONLY,
     &rbcShadowOption},
    {TK_CONFIG_BOOLEAN, "-showimage", "showImage", "ShowImage", "yes", offsetof(EpsItem, showImage),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BITMAP, "-stipple", (char *)NULL, (char *)NULL, NULL, offsetof(EpsItem, stipple), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-tags", (char *)NULL, (char *)NULL, NULL, 0, TK_CONFIG_NULL_OK, &tagsOption},
    {TK_CONFIG_STRING, "-title", (char *)NULL, (char *)NULL, NULL, offsetof(EpsItem, reqTitle), TK_CONFIG_NULL_OK},
    {TK_CONFIG_ANCHOR, "-titleanchor", (char *)NULL, (char *)NULL, "center", offsetof(EpsItem, titleStyle.anchor), 0},
    {TK_CONFIG_COLOR, "-titlecolor", (char *)NULL, (char *)NULL, RGB_BLACK, offsetof(EpsItem, titleStyle.color),
     TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_DOUBLE, "-titlerotate", "titleRotate", "TitleRotate", "0", offsetof(EpsItem, titleStyle.theta),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-width", (char *)NULL, (char *)NULL, "0", offsetof(EpsItem, width), TK_CONFIG_DONT_SET_DEFAULT,
     &rbcDistanceOption},
    {TK_CONFIG_END, (char *)NULL, (char *)NULL, (char *)NULL, (char *)NULL, 0, 0}};

/*
 * Prototypes for procedures defined in this file:
 */

static char *SkipBlanks(EpsParseInfo *);
static int ReadPsLine(EpsParseInfo *);
static unsigned char ReverseBits(register unsigned char);
static int GetHexValue(EpsParseInfo *, unsigned char *);
static void ReadEPSI(EpsItem *, EpsParseInfo *);
static int OpenEpsFile(Tcl_Interp *, EpsItem *);
static void CloseEpsFile(EpsItem *);
static void ImageChangedProc(ClientData, int, int, int, int, int, int);
static int EpsCoords(Tcl_Interp *, Tk_Canvas, Tk_Item *, Tcl_Size, Tcl_Obj *const[]);
static int EpsToArea(Tk_Canvas, Tk_Item *, double *);
static double EpsToPoint(Tk_Canvas, Tk_Item *, double *);
static void ComputeEpsBbox(Tk_Canvas, EpsItem *);
static int ConfigureEps(Tcl_Interp *, Tk_Canvas, Tk_Item *, Tcl_Size, Tcl_Obj *const[], int);
static int CreateEps(Tcl_Interp *, Tk_Canvas, Tk_Item *, Tcl_Size, Tcl_Obj *const[]);
static void DeleteEps(Tk_Canvas, Tk_Item *, Display *);
static void DisplayEps(Tk_Canvas, Tk_Item *, Display *, Drawable, int, int, int, int);
static void ScaleEps(Tk_Canvas, Tk_Item *, double, double, double, double);
static void TranslateEps(Tk_Canvas, Tk_Item *, double, double);
static int EpsToPostScript(Tcl_Interp *, Tk_Canvas, Tk_Item *, int);
static int ReadPostScript(Tcl_Interp *, EpsItem *);
static void AppendPsLine(Tcl_DString *, const char *);

/**
 * @ingroup eps_canvas
 * @brief Locate the first non-whitespace byte in the current parser line.
 * Returns a borrowed pointer into piPtr->line, possibly its terminating NUL. Uses unsigned-byte
 * character classification and does not modify the buffer, read the stream or advance nextPtr.
 *
 * @param[in] piPtr Borrowed parser record whose current line is inspected without advancing state.
 */
static char *SkipBlanks(EpsParseInfo *piPtr) {
    char *s;

    for (s = piPtr->line; isspace(UCHAR(*s)); s++) {
        /*empty*/
    }
    return s;
}

/**
 * @ingroup eps_canvas
 * @brief Read one bounded chunk of the PostScript stream.
 *
 * Borrow the FILE in piPtr and update bytesRead, lineNumber and line. A nonzero
 * maxBytes limits the PostScript section; zero reads to EOF. Return FALSE at
 * the limit or when fgets fails. Long physical lines may be split into chunks;
 * this is not an arbitrary-length line reader.
 *
 * @param[in,out] piPtr Caller-owned parser state with the current line and borrowed input stream.
 */
static int ReadPsLine(EpsParseInfo *piPtr) {
    int maxChars;

    if ((piPtr->maxBytes != 0) && (piPtr->bytesRead >= piPtr->maxBytes)) {
        return FALSE;
    }
    maxChars = MAX_EPS_LINE_LENGTH;
    if (piPtr->maxBytes != 0) {
        size_t remaining;

        remaining = piPtr->maxBytes - piPtr->bytesRead;
        /*
         * fgets reads at most n - 1 characters.
         */
        if (remaining < (size_t)(MAX_EPS_LINE_LENGTH - 1)) {
            maxChars = (int)remaining + 1;
        }
    }
    if (fgets(piPtr->line, maxChars, piPtr->f) == NULL) {
        return FALSE;
    }
    piPtr->bytesRead += strlen(piPtr->line);
    piPtr->lineNumber++;
#if DEBUG_READER
    PurifyPrintf("%d: %s\n", piPtr->lineNumber, piPtr->line);
#endif
    return TRUE;
}

/**
 * @ingroup eps_canvas
 * @brief Retain a string chunk with exactly one required trailing newline.
 * Appends the complete NUL-terminated input to an initialized caller-owned DString. Adds a newline
 * only when the chunk is empty or lacks a final newline; existing content and multiple existing
 * newlines are not normalized. Does not preserve byte identity for unterminated or size-limited source
 * chunks.
 *
 * @param[in,out] dStringPtr Initialized caller-owned DString receiving appended text.
 * @param[in] line Borrowed NUL-terminated text chunk.
 */
static void AppendPsLine(Tcl_DString *dStringPtr, const char *line) {
    size_t length;

    length = strlen(line);
    Tcl_DStringAppend(dStringPtr, line, -1);
    /*
     * fgets normally retains the newline.  Add one only when the
     * final line, or a size-limited read, did not contain it.
     */
    if ((length == 0) || (line[length - 1] != '\n')) {
        Tcl_DStringAppend(dStringPtr, "\n", 1);
    }
}

/**
 * @ingroup eps_canvas
 * @brief Reverse the order of all eight bits in one byte.
 * Uses successive adjacent-bit, two-bit and nibble swaps. Returns the reversed byte without
 * allocation, parser changes or host-endianness assumptions.
 *
 * @param[in] byte Eight-bit value whose bit order is reversed.
 */
static unsigned char ReverseBits(unsigned char byte) {
    byte = ((byte >> 1) & 0x55) | ((byte << 1) & 0xaa);
    byte = ((byte >> 2) & 0x33) | ((byte << 2) & 0xcc);
    byte = ((byte >> 4) & 0x0f) | ((byte << 4) & 0xf0);
    return byte;
}

/**
 * @ingroup eps_canvas
 * @brief Consume the next two-digit EPSI preview byte.
 * Uses nextPtr within the current line or reads another bounded chunk. New chunks must start with a
 * percent sign; whitespace after that prefix and between byte pairs is skipped. Requires both hex
 * digits in the same chunk. Returns TCL_OK and writes bytePtr only for a complete byte, TCL_RETURN for
 * an EndPreview comment prefix, or TCL_ERROR for malformed/truncated input. Updates parser stream
 * position and nextPtr on success; does not set a Tcl diagnostic.
 *
 * @param[in,out] piPtr Caller-owned parser state with the current line and borrowed input stream.
 * @param[out] bytePtr Required output written only when a complete preview byte is decoded.
 */
static int GetHexValue(EpsParseInfo *piPtr, unsigned char *bytePtr) {
    register char *p;
    unsigned int byte;

    p = piPtr->nextPtr;
    if (p == NULL) {
    nextLine:
        if (!ReadPsLine(piPtr)) {
#if DEBUG_READER
            PurifyPrintf("short file\n");
#endif
            return TCL_ERROR; /* Short file */
        }
        if (piPtr->line[0] != '%') {
#if DEBUG_READER
            PurifyPrintf("line doesn't start with %% (%s)\n", piPtr->line);
#endif
            return TCL_ERROR;
        }
        if ((piPtr->line[1] == '%') && (strncmp(piPtr->line + 2, "EndPreview", 10) == 0)) {
#if DEBUG_READER
            PurifyPrintf("end of preview (%s)\n", piPtr->line);
#endif
            return TCL_RETURN;
        }
        p = piPtr->line + 1;
    }
    while (isspace(UCHAR(*p))) {
        p++;
    }
    if (*p == '\0') {
        goto nextLine;
    }
    if ((!isxdigit(UCHAR(p[0]))) || (!isxdigit(UCHAR(p[1])))) {
#if DEBUG_READER
        PurifyPrintf("not a hex digit (%s)\n", piPtr->line);
#endif
        return TCL_ERROR;
    }
    byte = (piPtr->hexTable[UCHAR(p[0])] << 4) | piPtr->hexTable[UCHAR(p[1])];
    p += 2;
    piPtr->nextPtr = p;
    *bytePtr = byte;
    return TCL_OK;
}

/**
 * @ingroup eps_canvas
 * @brief Decode an optional one-bit or eight-bit EPSI preview.
 * Requires the current line to contain the recognized BeginPreview header. Accepts positive dimensions
 * through SHRT_MAX and depths 1 or 8; parses but does not enforce the declared line count. Allocates a
 * zeroed image and consumes the required hex bytes. Eight-bit samples become opaque inverted grayscale
 * in bottom-to-top row order. One-bit rows consume high bits first with byte padding per row; zero
 * bits become opaque white and set bits retain the zeroed pixel. Installs the owned image and
 * consumed-line metadata on success. Decode failure frees the temporary and clears line markers,
 * without rewinding input or reporting a Tcl error. Caller supplies an item ready to receive preview
 * ownership.
 *
 * @param[in,out] epsPtr Canvas-owned EPS item; stream, preview and text ownership changes are described above.
 * @param[in,out] piPtr Caller-owned parser state with the current line and borrowed input stream.
 */
static void ReadEPSI(EpsItem *epsPtr, EpsParseInfo *piPtr) {
    Rbc_ColorImage image;
    int width, height, bitsPerPixel, nLines;
    char *dscBeginPreview;

    dscBeginPreview = piPtr->line + 16;
    if (sscanf(dscBeginPreview, "%d %d %d %d", &width, &height, &bitsPerPixel, &nLines) != 4) {
#if DEBUG_READER
        PurifyPrintf("bad %%BeginPreview (%s) format\n", dscBeginPreview);
#endif
        return;
    }
    if (((bitsPerPixel != 1) && (bitsPerPixel != 8)) || (width < 1) || (width > SHRT_MAX) || (height < 1) ||
        (height > SHRT_MAX)) {
#if DEBUG_READER
        PurifyPrintf("Bad %%BeginPreview (%s) values\n", dscBeginPreview);
#endif
        return; /* Bad "%%BeginPreview:" information */
    }
    epsPtr->firstLine = piPtr->lineNumber;
    Rbc_InitHexTable(piPtr->hexTable);
    piPtr->nextPtr = NULL;
    image = Rbc_CreateColorImage(width, height);
    if (bitsPerPixel == 8) {
        int result;
        register Pix32 *destPtr;
        register int x, y;
        unsigned char byte;

        for (y = height - 1; y >= 0; y--) {
            destPtr = Rbc_ColorImagePixel(image, 0, y);
            for (x = 0; x < width; x++, destPtr++) {
                result = GetHexValue(piPtr, &byte);
                if (result != TCL_OK) {
                    goto error;
                }
                destPtr->rgba.red = destPtr->rgba.green = destPtr->rgba.blue = ~byte;
                destPtr->rgba.alpha = 0xFF;
            }
        }
    } else if (bitsPerPixel == 1) {
        int result;
        register Pix32 *destPtr;
        register int x, y;
        unsigned char byte;
        register int bit;

        destPtr = Rbc_ColorImageBits(image);
        for (y = 0; y < height; y++) {
            bit = 8;
            for (x = 0; x < width; x++, destPtr++) {
                if (bit == 8) {
                    result = GetHexValue(piPtr, &byte);
                    if (result != TCL_OK) {
                        goto error;
                    }
                    byte = ReverseBits(byte);
                    bit = 0;
                }
                if (((byte >> bit) & 0x01) == 0) {
                    destPtr->value = 0xFFFFFFFF;
                }
                bit++;
            }
        }
    } else {
        fprintf(stderr, "unknown EPSI bitsPerPixel (%d)\n", bitsPerPixel);
    }
    epsPtr->colorImage = image;
    epsPtr->lastLine = piPtr->lineNumber + 1;
    return;

error:
    epsPtr->firstLine = epsPtr->lastLine = -1;
    if (image != NULL) {
        Rbc_FreeColorImage(image);
    }
}

/**
 * @ingroup eps_canvas
 * @brief Parse EPS header metadata and retain its PostScript body.
 *
 * Borrow the open psFile; reset the item DString and seek to psStart if needed.
 * Require a percent-bang PS signature and an integer BoundingBox with positive,
 * int-representable extents. Retain title and optional EPSI color preview on the
 * item. Preserve an already-consumed first body line. Return TCL_ERROR with a
 * diagnostic on header/seek failure; this does not restore prior item state.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in,out] epsPtr Canvas-owned EPS item; stream, preview and text ownership changes are described above.
 */
static int ReadPostScript(Tcl_Interp *interp, EpsItem *epsPtr) {
    char *field;
    char *dscTitle;
    EpsParseInfo pi;
    int haveBoundingBox;
    int sawEndComments;
    int haveBodyLine;

    pi.line[0] = '\0';
    pi.maxBytes = epsPtr->psLength;
    pi.bytesRead = 0;
    pi.lineNumber = 0;
    pi.f = epsPtr->psFile;
    Tcl_DStringSetLength(&epsPtr->dString, 0);
    if (epsPtr->psStart > 0) {
        if (epsPtr->psStart > (size_t)LONG_MAX) {
            Rbc_AppendResultStrings(interp, "PostScript offset is too large in \"", epsPtr->fileName, "\"",
                                    (char *)NULL);
            return TCL_ERROR;
        }
        if (fseek(epsPtr->psFile, (long)epsPtr->psStart, SEEK_SET) != 0) {
            Rbc_AppendResultStrings(interp, "can't seek to start of PostScript code in \"", epsPtr->fileName, "\"",
                                    (char *)NULL);
            return TCL_ERROR;
        }
    }
    if (!ReadPsLine(&pi)) {
        Rbc_AppendResultStrings(interp, "file \"", epsPtr->fileName, "\" is empty?", (char *)NULL);
        return TCL_ERROR;
    }
    if (strncmp(pi.line, "%!PS", 4) != 0) {
        Rbc_AppendResultStrings(interp, "file \"", epsPtr->fileName, "\" doesn't start with \"%!PS\"", (char *)NULL);
        return TCL_ERROR;
    }
    dscTitle = NULL;
    haveBoundingBox = FALSE;
    sawEndComments = FALSE;
    haveBodyLine = FALSE;
    /*
     * Scan the DSC header.  Stop at %%EndComments, %%BeginProlog,
     * %%BeginSetup, or the first actual PostScript statement.
     */
    while (ReadPsLine(&pi)) {
        field = SkipBlanks(&pi);
        if ((field[0] == '%') && (field[1] == '%')) {
            field += 2;
            if (field[0] == 'B') {
                if ((strncmp(field, "BeginSetup", 10) == 0) || (strncmp(field, "BeginProlog", 11) == 0)) {
                    haveBodyLine = TRUE;
                    break;
                }
                if ((strncmp(field, "BoundingBox:", 12) == 0) && (!haveBoundingBox)) {
                    long long llx, lly, urx, ury;
                    int nFields;

                    field += 12;
                    nFields = sscanf(field, "%lld %lld %lld %lld", &llx, &lly, &urx, &ury);
                    if ((nFields != 4) || (llx < INT_MIN) || (llx > INT_MAX) || (lly < INT_MIN) || (lly > INT_MAX) ||
                        (urx < INT_MIN) || (urx > INT_MAX) || (ury < INT_MIN) || (ury > INT_MAX) || (urx <= llx) ||
                        (ury <= lly) || ((urx - llx) > INT_MAX) || ((ury - lly) > INT_MAX)) {
                        Rbc_AppendResultStrings(interp, "bad \"%%BoundingBox\" values: \"", field, "\"", (char *)NULL);
                        goto error;
                    }
                    epsPtr->llx = (int)llx;
                    epsPtr->lly = (int)lly;
                    epsPtr->urx = (int)urx;
                    epsPtr->ury = (int)ury;
                    haveBoundingBox = TRUE;
                }
            } else if ((field[0] == 'T') && (strncmp(field, "Title:", 6) == 0)) {
                if (dscTitle == NULL) {
                    dscTitle = Rbc_Strdup(field + 6);
                }
            } else if ((field[0] == 'E') && (strncmp(field, "EndComments", 11) == 0)) {
                sawEndComments = TRUE;
                break;
            }
            continue;
        }
        /*
         * Blank lines and ordinary PostScript comments are still
         * compatible with being in the header.  The first executable
         * line marks the beginning of the body.
         */
        if ((field[0] != '\0') && (field[0] != '%')) {
            haveBodyLine = TRUE;
            break;
        }
    }
    if (!haveBoundingBox) {
        Rbc_AppendResultStrings(interp, "no \"%%BoundingBox:\" found in \"", epsPtr->fileName, "\"", (char *)NULL);
        goto error;
    }
    /*
     * An EPSI preview may immediately follow %%EndComments.
     * Ignore intervening blank lines.
     */
    if (sawEndComments) {
        while (ReadPsLine(&pi)) {
            field = SkipBlanks(&pi);
            if (field[0] == '\0') {
                continue;
            }
            if (strncmp(pi.line, "%%BeginPreview:", 15) == 0) {
                ReadEPSI(epsPtr, &pi);
            } else {
                haveBodyLine = TRUE;
            }
            break;
        }
    }
    if (dscTitle != NULL) {
        epsPtr->title = dscTitle;
    }
    /*
     * Save the first body line already consumed by the parser.
     */
    if (haveBodyLine) {
        AppendPsLine(&epsPtr->dString, pi.line);
    }
    while (ReadPsLine(&pi)) {
        AppendPsLine(&epsPtr->dString, pi.line);
    }
    return TCL_OK;

error:
    if (dscTitle != NULL) {
        ckfree(dscTitle);
    }
    return TCL_ERROR;
}

/**
 * @ingroup eps_canvas
 * @brief Open the item file and parse its EPS section.
 *
 * Own a binary stdio stream in psFile. On Windows recognize the DOS EPS header
 * and record section offsets and lengths before parsing. Close the stream on
 * seek or parse failure; on success the caller must eventually CloseEpsFile().
 * Recorded preview formats do not imply a general TIFF/WMF renderer.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in,out] epsPtr Canvas-owned EPS item; stream, preview and text ownership changes are described above.
 */
static int OpenEpsFile(Tcl_Interp *interp, EpsItem *epsPtr) {
    FILE *f;
#ifdef WIN32
    DOSEPSHEADER dosHeader;
    size_t nBytes;
#endif

    f = fopen(epsPtr->fileName, "rb");
    if (f == NULL) {
        Rbc_AppendResultStrings(epsPtr->interp, "can't open \"", epsPtr->fileName,
                                "\": ", Tcl_PosixError(epsPtr->interp), (char *)NULL);
        return TCL_ERROR;
    }
    epsPtr->psFile = f;
    epsPtr->psStart = epsPtr->psLength = 0L;
    epsPtr->wmfStart = epsPtr->wmfLength = 0L;
    epsPtr->tiffStart = epsPtr->tiffLength = 0L;
#ifdef WIN32
    nBytes = fread(&dosHeader, 1, sizeof(dosHeader), f);
    if ((nBytes == sizeof(dosHeader)) && (dosHeader.magic[0] == 0xC5) && (dosHeader.magic[1] == 0xD0) &&
        (dosHeader.magic[2] == 0xD3) && (dosHeader.magic[3] == 0xC6)) {
        epsPtr->psStart = dosHeader.psStart;
        epsPtr->psLength = dosHeader.psLength;
        epsPtr->wmfStart = dosHeader.wmfStart;
        epsPtr->wmfLength = dosHeader.wmfLength;
        epsPtr->tiffStart = dosHeader.tiffStart;
        epsPtr->tiffLength = dosHeader.tiffLength;
        epsPtr->previewFormat = PS_PREVIEW_EPSI;
#ifdef HAVE_TIFF_H
        if (epsPtr->tiffLength > 0) {
            epsPtr->previewFormat = PS_PREVIEW_TIFF;
        }
#endif
        if (epsPtr->wmfLength > 0) {
            epsPtr->previewFormat = PS_PREVIEW_WMF;
        }
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        Rbc_AppendResultStrings(interp, "can't seek in \"", epsPtr->fileName, "\": ", Tcl_PosixError(interp),
                                (char *)NULL);
        CloseEpsFile(epsPtr);
        return TCL_ERROR;
    }
#endif /* WIN32 */
    {
        int result;

        result = ReadPostScript(interp, epsPtr);
        if (result != TCL_OK) {
            CloseEpsFile(epsPtr);
        }
        return result;
    }
}

/**
 * @ingroup eps_canvas
 * @brief Close and clear an item-owned stdio stream if present.
 *
 * Does not release parsed text, preview pixels or other item resources.
 *
 * @param[in,out] epsPtr Canvas-owned EPS item; stream, preview and text ownership changes are described above.
 */
static void CloseEpsFile(EpsItem *epsPtr) {
    if (epsPtr->psFile != NULL) {
        fclose(epsPtr->psFile);
        epsPtr->psFile = NULL;
    }
}

#ifdef HAVE_TIFF_H
/**
 * @ingroup eps_canvas
 * @brief Attempt preview decoding in the conditional TIFF implementation.
 * Compiled only with HAVE_TIFF_H. Reads dimensions and RGBA pixels from the item TIFF handle, frees
 * the temporary on decoder failure and installs it on success. The component-reordering step is
 * unfinished; this function is not evidence of supported TIFF preview rendering. Does not close the
 * TIFF handle or release a previous colorImage; callers would need to establish dimensions and
 * ownership.
 */
static void ReadTiffPreview(EpsItem *epsPtr) {
    unsigned int width, height;
    Rbc_ColorImage image;
    Pix32 *dataPtr;
    FILE *f;
    int n;

    TIFFGetField(epsPtr->tiffPtr, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(epsPtr->tiffPtr, TIFFTAG_IMAGELENGTH, &height);
    image = Rbc_CreateColorImage(width, height);
    dataPtr = Rbc_ColorImageBits(image);
    if (!TIFFReadRGBAImage(epsPtr->tiffPtr, width, height, dataPtr, 0)) {
        Rbc_FreeColorImage(image);
        return;
    }
    /* Reverse the order of the components for each pixel. */
    /* ... */
    epsPtr->colorImage = image;
}
#endif

/**
 * @ingroup eps_canvas
 * @brief Release resources of a canvas-owned EPS item.
 *
 * Release options, file, DString, color snapshot, source-image instance, private
 * photo, pixmaps, fill GC, title style and parsed title through their matching
 * APIs. Detach tmpImage before temporary-image cleanup can notify clients.
 * The canvas owns the item record itself; this function does not free it.
 *
 * @param[in] canvas Borrowed owning Tk canvas; supplies window and coordinate conversion.
 * @param[in,out] itemPtr Canvas-owned initialized EpsItem; owned resources are released, but the record is not freed.
 * @param[in] display Borrowed display matching the item graphics resources.
 */
static void DeleteEps(Tk_Canvas canvas, /* Info about overall canvas widget. */
                      Tk_Item *itemPtr, /* Item that is being deleted. */
                      Display *display) /* Display containing window for
                                         * canvas. */
{
    EpsItem *epsPtr = (EpsItem *)itemPtr;
    Tk_FreeOptions(configSpecs, (char *)epsPtr, display, 0);
    CloseEpsFile(epsPtr);
    Tcl_DStringFree(&epsPtr->dString);
    if (epsPtr->colorImage != NULL) {
        Rbc_FreeColorImage(epsPtr->colorImage);
    }
    if (epsPtr->preview != NULL) {
        Tk_FreeImage(epsPtr->preview);
    }
    if (epsPtr->previewName != NULL) {
        ckfree((char *)epsPtr->previewName);
    }
    if (epsPtr->tmpImage != NULL) {
        Tk_Image tmpImage = epsPtr->tmpImage;

        epsPtr->tmpImage = NULL;
        Rbc_DestroyTemporaryImage(epsPtr->interp, tmpImage);
    }
    if (epsPtr->pixmap != None) {
        Tk_FreePixmap(display, epsPtr->pixmap);
    }
    if (epsPtr->stipple != None) {
        Tk_FreePixmap(display, epsPtr->stipple);
    }
    if (epsPtr->fillGC != NULL) {
        Tk_FreeGC(display, epsPtr->fillGC);
    }
    Rbc_FreeTextStyle(display, &(epsPtr->titleStyle));
    if (epsPtr->title != NULL) {
        ckfree((char *)epsPtr->title);
    }
}

/**
 * @ingroup eps_canvas
 * @brief Initialize a canvas-owned EPS item and apply its creation options.
 *
 * Returns Tcl status. A configuration failure releases initialized resources through
 * DeleteEps; the item record remains caller-owned.
 * @param[in,out] interp Borrowed result interpreter; see @ref rbc_interp_argument.
 * @param[in] canvas Borrowed owning Tk canvas; supplies window and coordinate conversion.
 * @param[in,out] itemPtr Caller-allocated EpsItem with its Tk_Item header initialized; record ownership stays with the canvas.
 * @param[in] objc Number of creation arguments; at least two coordinates are required.
 * @param[in] objv Borrowed x/y coordinates followed by option/value pairs; see @ref rbc_command_arguments.
 */
static int CreateEps(Tcl_Interp *interp,    /* Interpreter for error reporting. */
                     Tk_Canvas canvas,      /* Canvas to hold new item. */
                     Tk_Item *itemPtr,      /* Record to hold new item;  header
                                             * has been initialized by caller. */
                     Tcl_Size objc,         /* Number of arguments in objv. */
                     Tcl_Obj *const objv[]) /* Arguments describing rectangle. */
{
    EpsItem *epsPtr = (EpsItem *)itemPtr;
    Tk_Window tkwin;
    const char *arg0, *arg1;

    tkwin = Tk_CanvasTkwin(canvas);
    if (objc < 2) {
        Rbc_AppendResultStrings(interp, "wrong # args: should be \"", Tk_PathName(tkwin), " create ",
                                itemPtr->typePtr->name, " x1 y1 ?options?\"", (char *)NULL);
        return TCL_ERROR;
    }
    /*
     * Initialize the item's record by hand (bleah).
     */
    epsPtr->anchor = TK_ANCHOR_NW;
    epsPtr->border = NULL;
    epsPtr->borderWidth = 2;
    epsPtr->canvas = canvas;
    epsPtr->fileName = NULL;
    epsPtr->psFile = NULL;
    epsPtr->fillGC = NULL;
    epsPtr->fillColor = NULL;
    epsPtr->colorImage = NULL;
    epsPtr->previewName = NULL;
    epsPtr->preview = NULL;
    epsPtr->interp = interp;
    epsPtr->tmpImage = NULL;
    epsPtr->pixmap = None;
    epsPtr->firstLine = epsPtr->lastLine = -1;
    epsPtr->relief = TK_RELIEF_SUNKEN;
    epsPtr->reqTitle = NULL;
    epsPtr->stipple = None;
    epsPtr->showImage = TRUE;
    epsPtr->quick = FALSE;
    epsPtr->title = NULL;
    epsPtr->lastWidth = epsPtr->lastHeight = 0;
    epsPtr->width = epsPtr->height = 0;
    epsPtr->x = epsPtr->y = 0.0;
    epsPtr->llx = epsPtr->lly = epsPtr->urx = epsPtr->ury = 0;
    epsPtr->canvasX = epsPtr->canvasY = 0;
    Tcl_DStringInit(&epsPtr->dString);
    memset(&(epsPtr->titleStyle), 0, sizeof(TextStyle));
#define PAD 8
    epsPtr->titleStyle.padLeft = epsPtr->titleStyle.padRight = PAD;
    epsPtr->titleStyle.padTop = epsPtr->titleStyle.padBottom = PAD;
    /*
     * Process the arguments to fill in the item record.
     */
    arg0 = Tcl_GetString(objv[0]);
    arg1 = Tcl_GetString(objv[1]);
    if ((Tk_CanvasGetCoord(interp, canvas, arg0, &(epsPtr->x)) != TCL_OK) ||
        (Tk_CanvasGetCoord(interp, canvas, arg1, &(epsPtr->y)) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (ConfigureEps(interp, canvas, itemPtr, objc - 2, objv + 2, 0) != TCL_OK) {
        DeleteEps(canvas, itemPtr, Tk_Display(tkwin));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup eps_canvas
 * @brief Notice deletion of a supplied EPS preview image.
 * Ordinary image changes do not refresh the saved color snapshot. If the retained instance is absent
 * or deleted, frees and clears the source name and schedules redraw of the item rectangle. Keeps the
 * image instance token for later reconfiguration/teardown because Tk still visits it after its
 * deletion callback returns. Does not release the snapshot or temporary display photo.
 *
 * @param[in,out] clientData Borrowed canvas-owned EpsItem registered for image-change notifications.
 * @param[in] x Changed image rectangle origin; ignored by this deletion-notification callback.
 * @param[in] y Changed image rectangle origin; ignored by this deletion-notification callback.
 * @param[in] width Changed image rectangle width; ignored by this callback.
 * @param[in] height Changed image rectangle height; ignored by this callback.
 * @param[in] imageWidth Current image width reported by Tk; ignored by this callback.
 * @param[in] imageHeight Current image height reported by Tk; ignored by this callback.
 */
static void ImageChangedProc(ClientData clientData, int x, int y, int width, int height, int imageWidth,
                             int imageHeight) {
    EpsItem *epsPtr = clientData;
    if ((epsPtr->preview == NULL) || (Rbc_ImageIsDeleted(epsPtr->preview))) {
        /* Tk's deletion loop still visits this instance after the callback.
         * Retain it until reconfiguration or DeleteEps can release it safely. */
        if (epsPtr->previewName != NULL) {
            ckfree((char *)epsPtr->previewName);
            epsPtr->previewName = NULL;
        }
        Tk_CanvasEventuallyRedraw(epsPtr->canvas, epsPtr->xLeft, epsPtr->yTop, epsPtr->xRight, epsPtr->yBottom);
    }
}

/**
 * @ingroup eps_canvas
 * @brief Find a supplied option prefix among validated option-value pairs.
 * Checks every option word with a following value against optionName, accepting a prefix no longer
 * than the full name. Returns a Boolean without modifying arguments. Does not validate ambiguity,
 * option syntax or values; ConfigureEps calls it after Tk configuration has validated the arguments. A
 * trailing unmatched word is ignored by this helper.
 *
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed validated option/value pairs; a trailing unmatched entry is ignored.
 * @param[in] optionName Borrowed full option name against which supplied prefixes are compared.
 */
static int EpsOptionSpecified(Tcl_Size objc, Tcl_Obj *const objv[], const char *optionName) {
    Tcl_Size i;
    size_t optionNameLength;

    optionNameLength = strlen(optionName);
    /*
     * Configuration arguments consist of option-value pairs.
     * Prefix matching preserves Tk's abbreviated-option behaviour.
     */
    for (i = 0; i + 1 < objc; i += 2) {
        const char *option;
        size_t optionLength;

        option = Tcl_GetString(objv[i]);
        optionLength = strlen(option);
        if ((optionLength <= optionNameLength) && (strncmp(optionName, option, optionLength) == 0)) {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * @ingroup eps_canvas
 * @brief Apply legacy canvas options and rebuild EPS preview resources.
 *
 * Use Tk_ConfigureWidget and explicit option-prefix detection for image, file
 * and quick changes. Source images are photo-only and copied to owned pixels.
 * Create or discard the temporary display photo as needed, rebuild the fill GC
 * and bbox, and close the file on success. Return Tcl status. Resource updates
 * are incremental, not a Tk_SetOptions saved-options rollback transaction.
 *
 * @param[in,out] interp Borrowed result interpreter; see @ref rbc_interp_argument.
 * @param[in] canvas Borrowed owning Tk canvas; supplies window and coordinate conversion.
 * @param[in,out] itemPtr Borrowed initialized EpsItem; updates can remain partially applied on failure.
 * @param[in] objc Number of option/value objects in objv.
 * @param[in] objv Borrowed option/value array; see @ref rbc_command_arguments.
 * @param[in] flags Tk_ConfigureWidget configuration flags, including TK_CONFIG_ARGV_ONLY for reconfiguration.
 */
static int ConfigureEps(Tcl_Interp *interp,    /* Used for error reporting. */
                        Tk_Canvas canvas,      /* Canvas containing itemPtr. */
                        Tk_Item *itemPtr,      /* EPS item to reconfigure. */
                        Tcl_Size objc,         /* Number of elements in argv.  */
                        Tcl_Obj *const objv[], /* Arguments describing things to configure. */
                        int flags)             /* Flags to pass to Tk_ConfigureWidget. */
{
    EpsItem *epsPtr = (EpsItem *)itemPtr;
    Tk_Window tkwin;
    XGCValues gcValues;
    unsigned long gcMask;
    GC newGC;
    int width, height;
    int imageModified;
    int fileModified;
    int quickModified;

    tkwin = Tk_CanvasTkwin(canvas);
    if (Tk_ConfigureWidget(interp, tkwin, configSpecs, objc, objv, (char *)epsPtr, flags) != TCL_OK) {
        return TCL_ERROR;
    }
    imageModified = EpsOptionSpecified(objc, objv, "-image");
    fileModified = EpsOptionSpecified(objc, objv, "-file");
    quickModified = EpsOptionSpecified(objc, objv, "-quick");
    /* Determine the size of the EPS item */
    width = height = 0;
    /*
     * Check for a "-image" option specifying an image to be displayed
     * representing the EPS canvas item.
     */
    if (imageModified) {
        if (epsPtr->preview != NULL) {
            Tk_FreeImage(epsPtr->preview);
            epsPtr->preview = NULL;
        }
        if (epsPtr->colorImage != NULL) {
            Rbc_FreeColorImage(epsPtr->colorImage);
            epsPtr->colorImage = NULL;
        }
        if (epsPtr->previewName != NULL) {
            Tk_PhotoHandle photo; /* Photo handle to Tk image. */
            /*
             * Allocate a new image, if one was named.
             */
            photo = Tk_FindPhoto(interp, epsPtr->previewName);
            if (photo == NULL) {
                Rbc_AppendResultStrings(interp, "image \"", epsPtr->previewName,
                                        "\" doesn't  exist or is not a photo image", (char *)NULL);
                return TCL_ERROR;
            }
            epsPtr->preview = Tk_GetImage(interp, tkwin, epsPtr->previewName, ImageChangedProc, epsPtr);
            if (epsPtr->preview == NULL) {
                Rbc_AppendResultStrings(interp, "can't find an image \"", epsPtr->previewName, "\"", (char *)NULL);
                ckfree((char *)epsPtr->previewName);
                epsPtr->previewName = NULL;
                return TCL_ERROR;
            }
            epsPtr->colorImage = Rbc_PhotoToColorImage(photo);
            width = Rbc_ColorImageWidth(epsPtr->colorImage);
            height = Rbc_ColorImageHeight(epsPtr->colorImage);
        }
    }
    if (fileModified) {
        if (epsPtr->title != NULL) {
            ckfree(epsPtr->title);
            epsPtr->title = NULL;
        }
        CloseEpsFile(epsPtr);
        if (epsPtr->pixmap != None) {
            Tk_FreePixmap(Tk_Display(tkwin), epsPtr->pixmap);
            epsPtr->pixmap = None;
        }
        if (epsPtr->colorImage != NULL) {
            Rbc_FreeColorImage(epsPtr->colorImage);
            epsPtr->colorImage = NULL;
        }
        epsPtr->firstLine = epsPtr->lastLine = -1;
        if (epsPtr->fileName != NULL) {
            if (OpenEpsFile(interp, epsPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    if ((epsPtr->colorImage != NULL) && (epsPtr->tmpImage == NULL)) {
        epsPtr->tmpImage = Rbc_CreateTemporaryImage(interp, tkwin, epsPtr);
        if (epsPtr->tmpImage == NULL) {
            return TCL_ERROR;
        }
    } else if ((epsPtr->colorImage == NULL) && (epsPtr->tmpImage != NULL)) {
        Tk_Image tmpImage;

        tmpImage = epsPtr->tmpImage;
        epsPtr->tmpImage = NULL;
        if (Rbc_DestroyTemporaryImage(epsPtr->interp, tmpImage) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (epsPtr->preview != NULL) {
        Tk_SizeOfImage(epsPtr->preview, &width, &height);
    }
    if (epsPtr->width == 0) {
        if (epsPtr->fileName != NULL) {
            width = (epsPtr->urx - epsPtr->llx);
        }
        epsPtr->width = width;
    }
    if (epsPtr->height == 0) {
        if (epsPtr->fileName != NULL) {
            height = (epsPtr->ury - epsPtr->lly);
        }
        epsPtr->height = height;
    }
    Rbc_ResetTextStyle(tkwin, &(epsPtr->titleStyle));
    if (quickModified) {
        epsPtr->lastWidth = epsPtr->lastHeight = 0;
    }
    /* Fill color GC */
    newGC = NULL;
    if (epsPtr->fillColor != NULL) {
        gcMask = GCForeground;
        gcValues.foreground = epsPtr->fillColor->pixel;
        if (epsPtr->stipple != None) {
            gcMask |= (GCStipple | GCFillStyle);
            gcValues.stipple = epsPtr->stipple;
            if (epsPtr->border != NULL) {
                gcValues.foreground = Tk_3DBorderColor(epsPtr->border)->pixel;
                gcValues.background = epsPtr->fillColor->pixel;
                gcMask |= GCBackground;
                gcValues.fill_style = FillOpaqueStippled;
            } else {
                gcValues.fill_style = FillStippled;
            }
        }
        newGC = Tk_GetGC(tkwin, gcMask, &gcValues);
    }
    if (epsPtr->fillGC != NULL) {
        Tk_FreeGC(Tk_Display(tkwin), epsPtr->fillGC);
    }
    epsPtr->fillGC = newGC;
    CloseEpsFile(epsPtr);
    ComputeEpsBbox(canvas, epsPtr);
    return TCL_OK;
}

/**
 * @ingroup eps_canvas
 * @brief Query or replace the EPS anchor coordinates.
 *
 * Returns TCL_OK or TCL_ERROR. A query returns a two-value coordinate list;
 * replacement converts both values before changing the item.
 * @param[in,out] interp Borrowed result interpreter; see @ref rbc_interp_argument.
 * @param[in] canvas Borrowed owning Tk canvas; supplies window and coordinate conversion.
 * @param[in,out] itemPtr Borrowed initialized EpsItem; anchor and bbox change only after both coordinates parse.
 * @param[in] objc Zero for a query or two for an X/Y replacement; other counts are rejected.
 * @param[in] objv Borrowed coordinate objects for replacement; not read for a query.
 */
static int EpsCoords(Tcl_Interp *interp,    /* Used for error reporting. */
                     Tk_Canvas canvas,      /* Canvas containing item. */
                     Tk_Item *itemPtr,      /* Item whose coordinates are to be
                                             * read or modified. */
                     Tcl_Size objc,         /* Number of coordinates supplied in
                                             * argv. */
                     Tcl_Obj *const objv[]) /* Array of coordinates: x1, y1,
                                             * x2, y2, ... */
{
    EpsItem *epsPtr = (EpsItem *)itemPtr;
    if ((objc != 0) && (objc != 2)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong # coordinates: "
                                               "expected 0 or 2, got %" TCL_SIZE_MODIFIER "d",
                                               objc));
        return TCL_ERROR;
    }
    if (objc == 2) {
        const char *arg0 = Tcl_GetString(objv[0]);
        const char *arg1 = Tcl_GetString(objv[1]);
        double x, y; /* Don't overwrite old coordinates on errors */

        if ((Tk_CanvasGetCoord(interp, canvas, arg0, &x) != TCL_OK) ||
            (Tk_CanvasGetCoord(interp, canvas, arg1, &y) != TCL_OK)) {
            return TCL_ERROR;
        }
        epsPtr->x = x;
        epsPtr->y = y;
        ComputeEpsBbox(canvas, epsPtr);
        return TCL_OK;
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("%f %f", epsPtr->x, epsPtr->y));
    return TCL_OK;
}

/**
 * @ingroup eps_canvas
 * @brief Map the item anchor and size into integer canvas bounds.
 *
 * Round x/y, translate the anchor, then update canvasX/Y and the Tk_Item bbox.
 * Right and bottom bounds are exclusive for canvas redraw. No graph axes,
 * track viewport or renderer clip stack participates.
 *
 * @param[in] canvas Borrowed owning canvas; retained item bounds supply the geometry.
 * @param[in,out] epsPtr Canvas-owned EPS item; stream, preview and text ownership changes are described above.
 */
static void ComputeEpsBbox(Tk_Canvas canvas, EpsItem *epsPtr) {
    int x, y;

    x = ROUND(epsPtr->x), y = ROUND(epsPtr->y);
    Rbc_TranslateAnchor(x, y, epsPtr->width, epsPtr->height, epsPtr->anchor, &x, &y);
    epsPtr->xLeft = epsPtr->canvasX = x;
    epsPtr->yTop = epsPtr->canvasY = y;
    /*
     * The right and bottom are (weirdly) exterior to the item.  Can't
     * complain much since it's documented in the Tk_CreateItemType
     * manual page.
     *
     * "These fields give a bounding box for the items using integer
     * canvas coordinates: the item should not cover any pixels with
     * x-coordinate lower than x1 or y-coordinate lower than y1, nor
     * should it cover any pixels with x-coordinate greater than or
     * equal to x2 or y-coordinate greater than or equal to y2."
     */
    epsPtr->xRight = x + epsPtr->width;
    epsPtr->yBottom = y + epsPtr->height;
}

/**
 * @ingroup eps_canvas
 * @brief Draw the preview or placeholder through Tk canvas/native services.
 *
 * Rescale the saved color snapshot only when cached dimensions differ. Quick
 * mode uses resize; otherwise use box resampling. Publish into the private
 * photo, release the intermediate buffer, and draw pixmap/photo or filled
 * placeholder, optional title and border. Borrow the drawable and canvas.
 * This path does not construct a graph renderer context.
 *
 * @param[in] canvas Borrowed owning Tk canvas; supplies window and coordinate conversion.
 * @param[in,out] itemPtr Borrowed configured EpsItem; preview caches may be rebuilt.
 * @param[in] display Borrowed display associated with the destination.
 * @param[in] drawable Borrowed canvas drawable receiving the preview, title and border.
 * @param[in] x Canvas redisplay rectangle X origin; unused.
 * @param[in] y Canvas redisplay rectangle Y origin; unused.
 * @param[in] width Canvas redisplay rectangle width; unused.
 * @param[in] height Canvas redisplay rectangle height; unused.
 */
static void DisplayEps(Tk_Canvas canvas,      /* Canvas that contains item. */
                       Tk_Item *itemPtr,      /* Item to be displayed. */
                       Display *display,      /* Display on which to draw item. */
                       Drawable drawable,     /* Pixmap or window in which to draw
                                               * item. */
                       int x, int y,          /* Describes region of canvas that */
                       int width, int height) /* must be redisplayed (not used). */
{
    Tk_Window tkwin;
    EpsItem *epsPtr = (EpsItem *)itemPtr;
    short int drawableX, drawableY;
    char *title;
    int twiceBW;
    int noImage;

    if ((epsPtr->width < 1) || (epsPtr->height < 1)) {
        return;
    }
    tkwin = Tk_CanvasTkwin(canvas);
    if ((epsPtr->showImage) && (epsPtr->colorImage != NULL) &&
        ((epsPtr->lastWidth != epsPtr->width) || (epsPtr->lastHeight != epsPtr->height))) {
        Rbc_ColorImage image;

        if (epsPtr->quick) {
            image = Rbc_ResizeColorImage(epsPtr->colorImage, 0, 0, Rbc_ColorImageWidth(epsPtr->colorImage),
                                         Rbc_ColorImageHeight(epsPtr->colorImage), epsPtr->width, epsPtr->height);
        } else {
            image = Rbc_ResampleColorImage(epsPtr->colorImage, epsPtr->width, epsPtr->height, rbcBoxFilterPtr,
                                           rbcBoxFilterPtr);
        }
        if (epsPtr->tmpImage != NULL) {
            Tk_PhotoHandle photo;
            /*
             * Resize the Tk photo image used to represent the EPS item.
             * We will over-write the temporary image with a resampled one.
             */
            photo = Tk_FindPhoto(epsPtr->interp, Rbc_NameOfImage(epsPtr->tmpImage));
            Rbc_ColorImageToPhoto(epsPtr->interp, image, photo);
        }
        epsPtr->lastHeight = epsPtr->height;
        epsPtr->lastWidth = epsPtr->width;
        Rbc_FreeColorImage(image);
    }
    /*
     * Translate the coordinates to those of the EPS item, then redisplay it.
     */
    Tk_CanvasDrawableCoords(canvas, (double)epsPtr->canvasX, (double)epsPtr->canvasY, &drawableX, &drawableY);
    x = (int)drawableX;
    y = (int)drawableY;
    twiceBW = epsPtr->borderWidth * 2;
    title = epsPtr->title;
    if (epsPtr->reqTitle != NULL) {
        title = epsPtr->reqTitle;
    }
    width = epsPtr->width;
    height = epsPtr->height;
    noImage = ((!epsPtr->showImage) || ((epsPtr->tmpImage == NULL) && (epsPtr->pixmap == None)));
    if (noImage) {
        if ((twiceBW >= width) || (twiceBW >= height)) {
            return;
        }
        width -= twiceBW;
        height -= twiceBW;
        if (epsPtr->fillGC != NULL) {
            XSetTSOrigin(display, epsPtr->fillGC, x, y);
            XFillRectangle(display, drawable, epsPtr->fillGC, x, y, epsPtr->width, epsPtr->height);
            XSetTSOrigin(display, epsPtr->fillGC, 0, 0);
        }
    } else {
        if (epsPtr->pixmap != None) {
            XCopyArea(Tk_Display(tkwin), epsPtr->pixmap, drawable, epsPtr->fillGC, 0, 0, width, height, x, y);
        } else {
            Tk_RedrawImage(epsPtr->tmpImage, 0, 0, width, height, drawable, x, y);
        }
    }
    if (title != NULL) {
        TextLayout *textPtr;
        double rotWidth, rotHeight;
        int destWidth, destHeight;

        /* Translate the title to an anchor position within the EPS item */
        textPtr = Rbc_GetTextLayout(title, &(epsPtr->titleStyle));
        Rbc_GetBoundingBox(textPtr->width, textPtr->height, epsPtr->titleStyle.theta, &rotWidth, &rotHeight,
                           (Point2D *)NULL);
        destWidth = (int)ceil(rotWidth);
        destHeight = (int)ceil(rotHeight);
        if ((destWidth <= width) && (destHeight <= height)) {
            int titleX, titleY;

            Rbc_TranslateAnchor(x, y, width, height, epsPtr->titleStyle.anchor, &titleX, &titleY);
            if (noImage) {
                titleX += epsPtr->borderWidth;
                titleY += epsPtr->borderWidth;
            }
            Rbc_DrawTextLayout(tkwin, drawable, textPtr, &(epsPtr->titleStyle), titleX, titleY);
        }
        ckfree((char *)textPtr);
    }
    if ((noImage) && (epsPtr->border != NULL)) {
        Rbc_Draw3DRectangle(tkwin, drawable, epsPtr->border, x, y, epsPtr->width, epsPtr->height, epsPtr->borderWidth,
                            epsPtr->relief);
    }
}

/**
 * @ingroup eps_canvas
 * @brief Measure point distance to the cached EPS item rectangle.
 * Uses cached canvas bounds and hypot of horizontal/vertical separation. Returns zero inside or on any
 * rectangle edge, including right and bottom. Does not inspect preview alpha, PostScript paths or
 * relief borders, change the item or invoke callbacks.
 *
 * @param[in] canvas Borrowed owning canvas; retained item bounds supply the geometry.
 * @param[in] itemPtr Borrowed canvas-owned Tk_Item header of an EpsItem.
 * @param[in] coordArr Borrowed two-element query point {x,y} in canvas coordinates.
 */
static double EpsToPoint(Tk_Canvas canvas, Tk_Item *itemPtr, double *coordArr) {
    EpsItem *epsPtr = (EpsItem *)itemPtr;
    double dx, dy;

    /*
     * Point is outside rectangle.
     */
    if (coordArr[0] < epsPtr->xLeft) {
        dx = epsPtr->xLeft - coordArr[0];
    } else if (coordArr[0] > epsPtr->xRight) {
        dx = coordArr[0] - epsPtr->xRight;
    } else {
        dx = 0;
    }
    if (coordArr[1] < epsPtr->yTop) {
        dy = epsPtr->yTop - coordArr[1];
    } else if (coordArr[1] > epsPtr->yBottom) {
        dy = coordArr[1] - epsPtr->yBottom;
    } else {
        dy = 0;
    }
    return hypot(dx, dy);
}

/**
 * @ingroup eps_canvas
 * @brief Classify the EPS rectangle against a canvas query rectangle.
 * Requires ordered query bounds. Returns -1 for disjoint rectangles, including edge-only contact, 1
 * when the query contains the entire item, and 0 for partial overlap. Uses cached bounds without
 * examining image pixels or modifying state.
 *
 * @param[in] canvas Borrowed owning canvas; retained item bounds supply the geometry.
 * @param[in] itemPtr Borrowed canvas-owned Tk_Item header of an EpsItem.
 * @param[in] area Borrowed four-element ordered query rectangle {left,top,right,bottom}.
 */
static int EpsToArea(Tk_Canvas canvas, Tk_Item *itemPtr, double *area) {
    EpsItem *epsPtr = (EpsItem *)itemPtr;

    if ((area[2] <= epsPtr->xLeft) || (area[0] >= epsPtr->xRight) || (area[3] <= epsPtr->yTop) ||
        (area[1] >= epsPtr->yBottom)) {
        return -1;
    }
    if ((area[0] <= epsPtr->xLeft) && (area[1] <= epsPtr->yTop) && (area[2] >= epsPtr->xRight) &&
        (area[3] >= epsPtr->yBottom)) {
        return 1;
    }
    return 0;
}

/**
 * @ingroup eps_canvas
 * @brief Scale the anchor position about a canvas origin and recompute bounds.
 *
 * Only x/y change. Configured width and height are not multiplied by scaleX/Y;
 * canvas scale is not equivalent to resizing the preview.
 *
 * @param[in] canvas Borrowed owning canvas; retained item bounds supply the geometry.
 * @param[in,out] itemPtr Borrowed canvas-owned Tk_Item header of an EpsItem.
 * @param[in] originX Horizontal canvas coordinate about which the anchor is scaled.
 * @param[in] originY Vertical canvas coordinate about which the anchor is scaled.
 * @param[in] scaleX Horizontal anchor scale factor; configured width is unchanged.
 * @param[in] scaleY Vertical anchor scale factor; configured height is unchanged.
 */
static void ScaleEps(Tk_Canvas canvas, Tk_Item *itemPtr, double originX, double originY, double scaleX, double scaleY) {
    EpsItem *epsPtr = (EpsItem *)itemPtr;
    epsPtr->x = originX + scaleX * (epsPtr->x - originX);
    epsPtr->y = originY + scaleY * (epsPtr->y - originY);
    ComputeEpsBbox(canvas, epsPtr);
}

/**
 * @ingroup eps_canvas
 * @brief Move the canvas anchor and recompute the cached item bounds.
 * Adds deltaX/deltaY to the anchor, then calls ComputeEpsBbox. Does not change configured dimensions,
 * resample preview pixels or request redraw itself; the canvas operation handles redisplay.
 *
 * @param[in] canvas Borrowed owning canvas; retained item bounds supply the geometry.
 * @param[in,out] itemPtr Borrowed canvas-owned Tk_Item header of an EpsItem.
 * @param[in] deltaX Horizontal displacement in canvas coordinates.
 * @param[in] deltaY Vertical displacement in canvas coordinates.
 */
static void TranslateEps(Tk_Canvas canvas, Tk_Item *itemPtr, double deltaX, double deltaY) {
    EpsItem *epsPtr = (EpsItem *)itemPtr;
    epsPtr->x += deltaX;
    epsPtr->y += deltaY;
    ComputeEpsBbox(canvas, epsPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * EpsToPostscript --
 *
 *      This procedure is called to generate Postscript for EPS
 *      canvas items.
 *
 * Results:
 *      The return value is a standard Tcl result.  If an error
 *      occurs in generating Postscript then an error message is
 *      left in the interpreter result, replacing whatever used
 *      to be there.  If no error occurs, then Postscript for the
 *      item is appended to the result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
/**
 * @ingroup eps_canvas
 * @brief Append EPS item output to the canvas PostScript result.
 *
 * Prepass is a no-op. With a file, load rbcCanvEps.pro, transform and clip to the
 * EPS bounding box, and append the retained body between encapsulation guards.
 * Without a file, serialize the cached temporary photo when present. Own a
 * PsToken for output construction and release it on the handled output paths.
 * This callback does not regenerate a stale preview or export SVG/PDF.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] canvas Borrowed owning canvas; retained item bounds supply the geometry.
 * @param[in,out] itemPtr Borrowed canvas-owned Tk_Item header of an EpsItem.
 * @param[in] prepass Nonzero requests the canvas prepass, which emits no output.
 */
static int EpsToPostScript(Tcl_Interp *interp, Tk_Canvas canvas, Tk_Item *itemPtr, int prepass) {
    EpsItem *epsPtr = (EpsItem *)itemPtr;
    PsToken psToken;
    Tk_Window tkwin;
    double xScale, yScale;
    int x, y, width, height;

    if (prepass) {
        return TCL_OK;
    }
    tkwin = Tk_CanvasTkwin(epsPtr->canvas);
    psToken = Rbc_GetPsToken(interp, tkwin);
    x = epsPtr->canvasX;
    y = (int)Tk_CanvasPsY(canvas, (double)epsPtr->canvasY + epsPtr->height);
    if (epsPtr->fileName == NULL) {
        /* No PostScript file, generate PostScript of resized image instead. */
        if (epsPtr->tmpImage != NULL) {
            Tk_PhotoHandle photo;

            Rbc_FormatToPostScript(psToken, "gsave\n");
            /*
             * First flip the PostScript y-coordinate axis so that the
             * origin is the upper-left corner like our color image.
             */
            Rbc_FormatToPostScript(psToken, "  %d %d translate\n", x, y + epsPtr->height);
            Rbc_FormatToPostScript(psToken, "  1 -1 scale\n");
            photo = Tk_FindPhoto(epsPtr->interp, Rbc_NameOfImage(epsPtr->tmpImage));
            Rbc_PhotoToPostScript(psToken, photo, 0.0, 0.0);
            Rbc_FormatToPostScript(psToken, "grestore\n");
            Rbc_AppendResultStrings(interp, Rbc_PostScriptFromToken(psToken), (char *)NULL);
            Rbc_ReleasePsToken(psToken);
        }
        return TCL_OK;
    }
    /* Copy in the PostScript prolog for EPS encapsulation. */
    if (Rbc_FileToPostScript(psToken, "rbcCanvEps.pro") != TCL_OK) {
        goto error;
    }
    Rbc_AppendToPostScript(psToken, "BeginEPSF\n", (char *)NULL);
    width = epsPtr->width;
    height = epsPtr->height;
    xScale = (double)width / (double)(epsPtr->urx - epsPtr->llx);
    yScale = (double)height / (double)(epsPtr->ury - epsPtr->lly);
    /* Set up scaling and translation transformations for the EPS item */
    Rbc_FormatToPostScript(psToken, "%d %d translate\n", x, y);
    Rbc_FormatToPostScript(psToken, "%g %g scale\n", xScale, yScale);
    Rbc_FormatToPostScript(psToken, "%d %d translate\n", -(epsPtr->llx), -(epsPtr->lly));
    Rbc_FormatToPostScript(psToken, "%d %d %d %d SetClipRegion\n", epsPtr->llx, epsPtr->lly, epsPtr->urx, epsPtr->ury);
    Rbc_AppendToPostScript(psToken, "%% including \"", epsPtr->fileName, "\"\n\n", (char *)NULL);
    Rbc_AppendToPostScript(psToken, Tcl_DStringValue(&epsPtr->dString), (char *)NULL);
    Rbc_AppendToPostScript(psToken, "EndEPSF\n", (char *)NULL);
    Rbc_AppendResultStrings(interp, Rbc_PostScriptFromToken(psToken), (char *)NULL);
    Rbc_ReleasePsToken(psToken);
    return TCL_OK;

error:
    Rbc_ReleasePsToken(psToken);
    return TCL_ERROR;
}

/*
 * The structures below defines the EPS item type in terms of
 * procedures that can be invoked by generic item code.
 */
static Tk_ItemType epsItemType = {
    "eps",                        /* name */
    sizeof(EpsItem),              /* itemSize */
    CreateEps,                    /* createProc */
    configSpecs,                  /* configSpecs */
    ConfigureEps,                 /* configureProc */
    EpsCoords,                    /* coordProc */
    DeleteEps,                    /* deleteProc */
    DisplayEps,                   /* displayProc */
    0,                            /* alwaysRedraw */
    EpsToPoint,                   /* pointProc */
    EpsToArea,                    /* areaProc */
    EpsToPostScript,              /* postscriptProc */
    ScaleEps,                     /* scaleProc */
    TranslateEps,                 /* translateProc */
    (Tk_ItemIndexProc *)NULL,     /* indexProc */
    (Tk_ItemCursorProc *)NULL,    /* icursorProc */
    (Tk_ItemSelectionProc *)NULL, /* selectionProc */
    (Tk_ItemInsertProc *)NULL,    /* insertProc */
    (Tk_ItemDCharsProc *)NULL,    /* dTextProc */
    (Tk_ItemType *)NULL           /* nextPtr */
};

/**
 * @ingroup eps_canvas
 * @brief Register the EPS canvas item type and Tk tag option callbacks.
 *
 * Registers the static Tk item-type descriptor. Does not create a canvas or item.
 * @param[in] interp Interpreter supplied by package initialization; currently unused.
 */
void Rbc_InitEpsCanvasItem(Tcl_Interp *interp) {
    Tk_CreateItemType(&epsItemType);
    /* Initialize custom canvas option routines. */
    tagsOption.parseProc = Tk_CanvasTagsParseProc;
    tagsOption.printProc = Tk_CanvasTagsPrintProc;
}
