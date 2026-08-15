/*
 * rbcCanvEps.c --
 *
 *      This file implements Encapsulated PostScript items for canvas widgets.
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

/*
 * EpsParseInfo --
 *
 *    This structure is used to pass PostScript file information
 *    around to various routines while parsing the EPS file.
 */
typedef struct {
    size_t maxBytes;  /* Maximum bytes in PostScript section.
                       * Zero means read to EOF. */
    size_t bytesRead; /* Bytes consumed from PostScript section. */
    int lineNumber;
    char line[MAX_EPS_LINE_LENGTH + 1];
    char hexTable[256];
    char *nextPtr;
    FILE *f;
} EpsParseInfo;

#define DEF_EPS_ANCHOR "nw"
#define DEF_EPS_OUTLINE_COLOR RGB_BLACK
#define DEF_EPS_OUTLINE_MONO RGB_BLACK
#define DEF_EPS_BORDERWIDTH STD_BORDERWIDTH
#define DEF_EPS_FILE_NAME (char *)NULL
#define DEF_EPS_FONT STD_FONT
#define DEF_EPS_FILL_COLOR STD_NORMAL_FOREGROUND
#define DEF_EPS_FILL_MONO STD_NORMAL_FG_MONO
#define DEF_EPS_HEIGHT "0"
#define DEF_EPS_IMAGE_NAME (char *)NULL
#define DEF_EPS_JUSTIFY "center"
#define DEF_EPS_QUICK_RESIZE "no"
#define DEF_EPS_RELIEF "sunken"
#define DEF_EPS_SHADOW_COLOR (char *)NULL
#define DEF_EPS_SHADOW_MONO (char *)NULL
#define DEF_EPS_SHOW_IMAGE "yes"
#define DEF_EPS_STIPPLE (char *)NULL
#define DEF_EPS_TAGS (char *)NULL
#define DEF_EPS_TITLE (char *)NULL
#define DEF_EPS_TITLE_ANCHOR "center"
#define DEF_EPS_TITLE_COLOR RGB_BLACK
#define DEF_EPS_TITLE_ROTATE "0"
#define DEF_EPS_WIDTH "0"

/*
 * Information used for parsing configuration specs:
 */

static Tk_CustomOption tagsOption;

extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcShadowOption;

/*
 * The structure below defines the record for each EPS item.
 */
typedef struct {
    Tk_Item header;            /* Generic stuff that's the same for all
                                * types.  MUST BE FIRST IN STRUCTURE. */
    Tk_Canvas canvas;          /* Canvas containing the EPS item. */
    int canvasX, canvasY;      /* Translated (by the anchor) canvas
                                * coordinates of the EPS item. */
    int lastWidth, lastHeight; /* Last known dimensions of the EPS item.
                                * This is used to know if the color image
                                * preview needs to be resized. */
    Tcl_Interp *interp;
    FILE *psFile;      /* File pointer to Encapsulated
                        * PostScript file. We'll hold this as
                        * long as the EPS item is using this
                        * file. */
    size_t psStart;    /* File offset of PostScript code. */
    size_t psLength;   /* Length of PostScript code. If zero,
                        * indicates to read to EOF. */
    size_t wmfStart;   /* File offset of Windows Metafile preview.  */
    size_t wmfLength;  /* Length of WMF portion in bytes. If zero,
                        * indicates there is no WMF preview. */
    size_t tiffStart;  /* File offset of TIFF preview. */
    size_t tiffLength; /* Length of TIFF portion in bytes. If zero,
                        * indicates there is no TIFF preview. */
    char *previewName;
    int previewFormat;
    Tk_Image preview;          /* A Tk photo image provided as a
                                * preview of the EPS contents. This
                                * image supersedes any EPS preview
                                * embedded PostScript preview (EPSI). */
    Tk_Image tmpImage;         /* Used to display the resized preview image.
                                * Created and deleted internally. */
    Pixmap pixmap;             /* Pixmap representing scaled preview. This
                                * isn't currently used.  For now we're
                                * overwriting the Tk image everytime the
                                * EPS item is resized. In the future
                                * we'll use our own image routines. */
    ColorTable colorTable;     /* Pointer to color table */
    Rbc_ColorImage colorImage; /* The original photo or PostScript
                                * preview image converted to a color
                                * image.  This is kept around for
                                * resampling or resizing the image. */
    int firstLine, lastLine;   /* First and last line numbers of the
                                * PostScript preview.  They are used
                                * to skip over the preview when
                                * encapsulating PostScript for the
                                * canvas item. */
    GC fillGC;                 /* Graphics context to fill background
                                * of image outline if no preview image
                                * was present. */
    int llx, lly, urx, ury;    /* Lower left and upper right coordinates
                                * of PostScript bounding box, retrieved
                                * from file's "BoundingBox:" field. */
    char *title;               /* Title, retrieved from the file's "Title:"
                                * field, to be displayed over the top of
                                * the EPS preview (malloc-ed).  */
    Tcl_DString dString;       /* Contains the encapsulate PostScript. */

    /* User configurable fields */
    double x, y; /* Canvas coordinates of the item */
    Tk_Anchor anchor;
    char *fileName;    /* Name of the encapsulated PostScript file.
                        * If NULL, indicates that no EPS file
                        * has be successfully loaded yet. */
    char *reqTitle;    /* Title to be displayed in the EPS item.
                        * Supersedes the title found in the EPS
                        * file. If NULL, indicates that the title
                        * found in the EPS file should be used. */
    int width, height; /* Dimensions of EPS item. If set to zero,
                        * the dimension found in the "%%BoundingBox:"
                        * specification from the EPS file are
                        * used. */
    int showImage;     /* Indicates if the image or the outline
                        * rectangle should be displayed */
    int quick;
    XColor *fillColor;  /* Fill color of the image outline. */
    Tk_3DBorder border; /* Outline color */
    int borderWidth;
    int relief;
    TextStyle titleStyle; /* Font, color, etc. for title */
    Pixmap stipple;       /* Stipple for image fill */
    ClientData tiffPtr;
#ifdef WIN32
    HENHMETAFILE *hMetaFile; /* Windows metafile. */
#endif
} EpsItem;

static Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_ANCHOR, "-anchor", (char *)NULL, (char *)NULL, DEF_EPS_ANCHOR, offsetof(EpsItem, anchor),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_SYNONYM, "-bd", "borderWidth", (char *)NULL, (char *)NULL, 0, 0},
    {TK_CONFIG_CUSTOM, "-borderwidth", "borderWidth", (char *)NULL, DEF_EPS_BORDERWIDTH, offsetof(EpsItem, borderWidth),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_STRING, "-file", (char *)NULL, (char *)NULL, DEF_EPS_FILE_NAME, offsetof(EpsItem, fileName),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_FONT, "-font", "font", "Font", DEF_EPS_FONT, offsetof(EpsItem, titleStyle.font), 0},
    {TK_CONFIG_COLOR, "-fill", "fill", (char *)NULL, DEF_EPS_FILL_COLOR, offsetof(EpsItem, fillColor),
     TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-fill", "fill", (char *)NULL, DEF_EPS_FILL_MONO, offsetof(EpsItem, fillColor),
     TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_CUSTOM, "-height", (char *)NULL, (char *)NULL, DEF_EPS_HEIGHT, offsetof(EpsItem, height),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_STRING, "-image", (char *)NULL, (char *)NULL, DEF_EPS_IMAGE_NAME, offsetof(EpsItem, previewName),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_JUSTIFY, "-justify", "justify", "Justify", DEF_EPS_JUSTIFY, offsetof(EpsItem, titleStyle.justify),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BORDER, "-outline", "outline", (char *)NULL, DEF_EPS_OUTLINE_COLOR, offsetof(EpsItem, border),
     TK_CONFIG_COLOR_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_BORDER, "-outline", "outline", (char *)NULL, DEF_EPS_OUTLINE_MONO, offsetof(EpsItem, border),
     TK_CONFIG_MONO_ONLY | TK_CONFIG_NULL_OK},
    {TK_CONFIG_BOOLEAN, "-quick", "quick", "Quick", DEF_EPS_QUICK_RESIZE, offsetof(EpsItem, quick),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_RELIEF, "-relief", (char *)NULL, (char *)NULL, DEF_EPS_RELIEF, offsetof(EpsItem, relief),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", DEF_EPS_SHADOW_COLOR, offsetof(EpsItem, titleStyle.shadow),
     TK_CONFIG_COLOR_ONLY, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-shadow", "shadow", "Shadow", DEF_EPS_SHADOW_MONO, offsetof(EpsItem, titleStyle.shadow),
     TK_CONFIG_MONO_ONLY, &rbcShadowOption},
    {TK_CONFIG_BOOLEAN, "-showimage", "showImage", "ShowImage", DEF_EPS_SHOW_IMAGE, offsetof(EpsItem, showImage),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BITMAP, "-stipple", (char *)NULL, (char *)NULL, DEF_EPS_STIPPLE, offsetof(EpsItem, stipple),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-tags", (char *)NULL, (char *)NULL, DEF_EPS_TAGS, 0, TK_CONFIG_NULL_OK, &tagsOption},
    {TK_CONFIG_STRING, "-title", (char *)NULL, (char *)NULL, DEF_EPS_TITLE, offsetof(EpsItem, reqTitle),
     TK_CONFIG_NULL_OK},
    {TK_CONFIG_ANCHOR, "-titleanchor", (char *)NULL, (char *)NULL, DEF_EPS_TITLE_ANCHOR,
     offsetof(EpsItem, titleStyle.anchor), 0},
    {TK_CONFIG_COLOR, "-titlecolor", (char *)NULL, (char *)NULL, DEF_EPS_TITLE_COLOR,
     offsetof(EpsItem, titleStyle.color), TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_DOUBLE, "-titlerotate", "titleRotate", "TitleRotate", DEF_EPS_TITLE_ROTATE,
     offsetof(EpsItem, titleStyle.theta), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-width", (char *)NULL, (char *)NULL, DEF_EPS_WIDTH, offsetof(EpsItem, width),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
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

/*
 *----------------------------------------------------------------------
 *
 * SkipBlanks --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static char *SkipBlanks(EpsParseInfo *piPtr) {
    char *s;

    for (s = piPtr->line; isspace(UCHAR(*s)); s++) {
        /*empty*/
    }
    return s;
}

/*
 *----------------------------------------------------------------------
 *
 * ReadPsLine --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * ReverseBits --
 *
 *      Convert a byte from a X image into PostScript image order.
 *      This requires not only the nybbles to be reversed but also
 *      their bit values.
 *
 * Results:
 *      The converted byte is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static unsigned char ReverseBits(unsigned char byte) {
    byte = ((byte >> 1) & 0x55) | ((byte << 1) & 0xaa);
    byte = ((byte >> 2) & 0x33) | ((byte << 2) & 0xcc);
    byte = ((byte >> 4) & 0x0f) | ((byte << 4) & 0xf0);
    return byte;
}

/*
 *----------------------------------------------------------------------
 *
 * GetHexValue --
 *
 *      Reads the next ASCII hex value from EPS preview image and
 *      converts it.
 *
 * Results:
 *      One of three Tcl return values is possible.
 *
 *      TCL_OK      the next byte was successfully parsed.
 *      TCL_ERROR   an error occurred processing the next hex value.
 *      TCL_RETURN  "%%EndPreview" line was detected.
 *
 *      The converted hex value is returned via "bytePtr".
 *
 * Side Effects:
 *      TODO: Side Effects:
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * ReadEPSI --
 *
 *      Reads the EPS preview image from the PostScript file, converting
 *      the image into a Rbc_ColorImage.  If an error occurs when parsing
 *      the preview, the preview is silently ignored.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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
                destPtr->Red = destPtr->Green = destPtr->Blue = ~byte;
                destPtr->Alpha = 0xFF;
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

/*
 *----------------------------------------------------------------------
 *
 * ReadPostScript --
 *
 *      This routine reads and parses the few fields we need out
 *      of an EPS file.
 *
 *      The EPS standards are outlined from Appendix H of the
 *      "PostScript Language Reference Manual" pp. 709-736.
 *
 *      Mandatory fields:
 *
 *      - Starts with "%!PS*"
 *      - Contains "%%BoundingBox: llx lly urx ury"
 *
 *      Optional fields for EPS item:
 *      - "%%BeginPreview: w h bpp #lines"
 *         Preview is in hexadecimal. Each line must start with "%"
 *          - "%%EndPreview"
 *      - "%%Title: (string)"
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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
            Rbc_AppendResultStrings(interp, "PostScript offset is too large in \"", epsPtr->fileName, "\"", (char *)NULL);
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
                    dscTitle = RbcStrdup(field + 6);
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

/*
 *----------------------------------------------------------------------
 *
 * OpenEpsFile --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int OpenEpsFile(Tcl_Interp *interp, EpsItem *epsPtr) {
    FILE *f;
#ifdef WIN32
    DOSEPSHEADER dosHeader;
    size_t nBytes;
#endif

    f = fopen(epsPtr->fileName, "rb");
    if (f == NULL) {
        Rbc_AppendResultStrings(epsPtr->interp, "can't open \"", epsPtr->fileName, "\": ", Tcl_PosixError(epsPtr->interp),
                         (char *)NULL);
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
        Rbc_AppendResultStrings(interp, "can't seek in \"", epsPtr->fileName, "\": ", Tcl_PosixError(interp), (char *)NULL);
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

/*
 *----------------------------------------------------------------------
 *
 * CloseEpsFile --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void CloseEpsFile(EpsItem *epsPtr) {
    if (epsPtr->psFile != NULL) {
        fclose(epsPtr->psFile);
        epsPtr->psFile = NULL;
    }
}

#ifdef HAVE_TIFF_H
/*
 *----------------------------------------------------------------------
 *
 * ReadTiffPreview --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * DeleteEps --
 *
 *      This procedure is called to clean up the data structure
 *      associated with a EPS item.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Resources associated with itemPtr are released.
 *
 *----------------------------------------------------------------------
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
        Rbc_DestroyTemporaryImage(epsPtr->interp, epsPtr->tmpImage);
    }
    if (epsPtr->pixmap != None) {
#ifdef notyet
        Rbc_FreeColorTable(epsPtr->colorTable);
#endif
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

/*
 *----------------------------------------------------------------------
 *
 * CreateEps --
 *
 *      This procedure is invoked to create a new EPS item
 *      in a canvas.
 *
 * Results:
 *      A standard Tcl return value.  If an error occurred in
 *      creating the item, then an error message is left in the
 *      interprwter esult;  in this case itemPtr is left uninitialized,
 *      so it can be safely freed by the caller.
 *
 * Side effects:
 *      A new EPS item is created.
 *
 *----------------------------------------------------------------------
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
        Rbc_AppendResultStrings(interp, "wrong # args: should be \"", Tk_PathName(tkwin), " create ", itemPtr->typePtr->name,
                         " x1 y1 ?options?\"", (char *)NULL);
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

/*
 *----------------------------------------------------------------------
 *
 * ImageChangedProc
 *
 *      The image is over-written each time the EPS item is resized.
 *      So we only worry if the image is deleted.
 *
 *      We always resample from the color image we saved when the
 *      photo image was specified (-image option).
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void ImageChangedProc(ClientData clientData, int x, int y, int width, int height, int imageWidth,
                             int imageHeight) {
    EpsItem *epsPtr = clientData;

    if ((epsPtr->preview == NULL) || (Tk_ImageIsDeleted(epsPtr->preview))) {
        epsPtr->preview = NULL;
        if (epsPtr->previewName != NULL) {
            ckfree((char *)epsPtr->previewName);
            epsPtr->previewName = NULL;
        }
        Tk_CanvasEventuallyRedraw(epsPtr->canvas, epsPtr->xLeft, epsPtr->yTop, epsPtr->xRight, epsPtr->yBottom);
    }
}

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

/*
 *----------------------------------------------------------------------
 *
 * ConfigureEps --
 *
 *      This procedure is invoked to configure various aspects
 *      of an EPS item, such as its background color.
 *
 * Results:
 *      A standard Tcl result code.  If an error occurs, then
 *      an error message is left in the interpreter result.
 *
 * Side effects:
 *      Configuration information may be set for itemPtr.
 *
 *----------------------------------------------------------------------
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
                Rbc_AppendResultStrings(interp, "image \"", epsPtr->previewName, "\" doesn't  exist or is not a photo image",
                                 (char *)NULL);
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
#ifdef notyet
            Rbc_FreeColorTable(epsPtr->colorTable);
#endif
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

/*
 *----------------------------------------------------------------------
 *
 * EpsCoords --
 *
 *      This procedure is invoked to process the "coords" widget
 *      command on EPS items.  See the user documentation for
 *      details on what it does.
 *
 * Results:
 *      Returns TCL_OK or TCL_ERROR, and sets the interpreter result.
 *
 * Side effects:
 *      The coordinates for the given item may be changed.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * ComputeEpsBbox --
 *
 *      This procedure is invoked to compute the bounding box of
 *      all the pixels that may be drawn as part of a EPS item.
 *      This procedure is where the preview image's placement is
 *      computed.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The fields x1, y1, x2, and y2 are updated in the header
 *      for itemPtr.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * DisplayEps --
 *
 *      This procedure is invoked to draw the EPS item in a
 *      given drawable.  The EPS item may be drawn as either
 *      a solid rectangle or a pixmap of the preview image.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      ItemPtr is drawn in drawable using the transformation
 *      information in canvas.
 *
 *----------------------------------------------------------------------
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
        } else {
#ifdef notyet
            epsPtr->pixmap = Rbc_ColorImageToPixmap(epsPtr->interp, tkwin, image, &(epsPtr->colorTable));
#endif
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

/*
 *----------------------------------------------------------------------
 *
 * EpsToPoint --
 *
 *      Computes the distance from a given point to a given
 *      rectangle, in canvas units.
 *
 * Results:
 *      The return value is 0 if the point whose x and y coordinates
 *      are coordPtr[0] and coordPtr[1] is inside the EPS item.  If the
 *      point isn't inside the item then the return value is the
 *      distance from the point to the EPS item.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
/*ARGSUSED*/
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

/*
 *----------------------------------------------------------------------
 *
 * EpsToArea --
 *
 *      This procedure is called to determine whether an item
 *      lies entirely inside, entirely outside, or overlapping
 *      a given rectangle.
 *
 * Results:
 *      -1 is returned if the item is entirely outside the area
 *      given by rectPtr, 0 if it overlaps, and 1 if it is entirely
 *      inside the given area.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * ScaleEps --
 *
 *      This procedure is invoked to rescale an item.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The item referred to by itemPtr is rescaled so that the
 *      following transformation is applied to all point coordinates:
 *          x' = originX + scaleX*(x-originX)
 *          y' = originY + scaleY*(y-originY)
 *
 *----------------------------------------------------------------------
 */
static void ScaleEps(Tk_Canvas canvas, Tk_Item *itemPtr, double originX, double originY, double scaleX, double scaleY) {
    EpsItem *epsPtr = (EpsItem *)itemPtr;

    epsPtr->x = originX + scaleX * (epsPtr->x - originX);
    epsPtr->y = originY + scaleY * (epsPtr->y - originY);
    ComputeEpsBbox(canvas, epsPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * TranslateEps --
 *
 *      This procedure is called to move an item by a given amount.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The position of the item is offset by (xDelta, yDelta), and
 *      the bounding box is updated in the generic part of the item
 *      structure.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_InitEpsCanvasItem --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_InitEpsCanvasItem(Tcl_Interp *interp) {
    Tk_CreateItemType(&epsItemType);
    /* Initialize custom canvas option routines. */
    tagsOption.parseProc = Tk_CanvasTagsParseProc;
    tagsOption.printProc = Tk_CanvasTagsPrintProc;
}
