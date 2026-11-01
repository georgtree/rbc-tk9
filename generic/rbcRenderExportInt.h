/* Shared export state and helpers. Not installed. See license.terms. */
/** @file rbcRenderExportInt.h
 * Borrowed export state and private backend construction helpers.
 */
/** @defgroup export_backend Export backend support
 * Private helpers shared by document renderers; not installed C APIs.
 */
#ifndef RBC_RENDER_EXPORT_INT_H
#define RBC_RENDER_EXPORT_INT_H

#include "rbcRenderInt.h"
#include "rbcFont.h"
#include <stdint.h>

/* The core must be first so backend callbacks can downcast. Styles are shallow
 * copies: colors/bitmaps/tiles remain borrowed unless stored as XColor values.
 * Pattern IDs belong to the export builder, not to this short-lived context. */
typedef struct {
    Rbc_RenderContext core;
    Rbc_ExportContext *exportPtr;
    Rbc_RenderFillStyle fillStyle;
    double symbolSize;
    /* Shared style state for the direct SVG and PDF exporters. */
    XColor vectorColor, vectorOffColor;
    int vectorHasColor, vectorHasOffColor, vectorWidth, vectorCap, vectorJoin;
    Rbc_Dashes vectorDashes;
    Rbc_RenderSymbolStyle vectorSymbol;
    int vectorBarSymbol;
    unsigned int vectorPatternId;
    Rbc_Tile vectorTile;
} Rbc_RenderExportState;

#define RENDER_EXPORT(ctx) ((Rbc_RenderExportState *)(ctx))
/**
 * @brief Allocate a zeroed PostScript renderer borrowing an initialized token.
 * @ingroup export_backend
 *
 * Requires backendData to hold a live PsToken. Owns only the returned context; release through
 * Rbc_RenderEnd. Does not begin a page, load the prolog or own the export buffer.
 */
Rbc_RenderContext *Rbc_RenderCreatePostscript(Rbc_ExportContext *);
/**
 * @brief Allocate a zeroed SVG renderer borrowing an export token.
 * @ingroup export_backend
 *
 * Installs the SVG operation table without beginning or finalizing a document. The token outlives the
 * context; release through Rbc_RenderEnd.
 */
Rbc_RenderContext *Rbc_RenderCreateSvg(Rbc_ExportContext *);
/**
 * @brief Allocate a zeroed PDF renderer borrowing an initialized export token.
 * @ingroup export_backend
 *
 * The PDF builder must already be attached before drawing. Installs operations but does not initialize
 * fonts, serialize the page or own document resources. Release through Rbc_RenderEnd.
 */
Rbc_RenderContext *Rbc_RenderCreatePdf(Rbc_ExportContext *);
/**
 * @brief Emit PostScript line attributes and remember whether dashes are enabled.
 * @ingroup export_backend
 *
 * Requires a PostScript context. Delegates width/color/dash conversion to the token and borrows input
 * resources. Dash-background procedure setup is separate.
 */
void Rbc_RenderPostscriptStroke(Rbc_RenderContext *, const XColor *, int, const Rbc_Dashes *, int, int);
/**
 * @brief Define a legacy PostScript symbol batch and its size correction.
 * @ingroup export_backend
 *
 * Requires a valid symbol type, live graph and valid bitmap resources when selected. Defines
 * DrawSymbolProc in the document dictionary, chooses a prolog macro, and emits outline width/dashes.
 * Finish this batch before another context redefines that procedure.
 */
void Rbc_RenderPostscriptSymbol(Rbc_RenderContext *, const Rbc_RenderSymbolStyle *);
/**
 * @brief Define the unscaled square procedure used for bar legend samples.
 * @ingroup export_backend
 *
 * Requires a PostScript fill context and live graph for stipples. Borrows style resources, emits
 * DrawSymbolProc and records size. Retains the legacy solid/stipple fallback and ignores image-tile
 * opacity.
 */
void Rbc_RenderPostscriptBarSymbol(Rbc_RenderContext *, const Rbc_RenderFillStyle *, int);
/**
 * @brief Free only the allocated renderer context.
 * @ingroup export_backend
 *
 * Called after common Rbc_RenderEnd unwinds explicit clips. Does not balance a separately opened plot
 * scope or free the borrowed export token, graph, styles, tiles, document resources or output buffer.
 * Requires non-NULL context.
 */
void Rbc_RenderExportEnd(Rbc_RenderContext *);
/**
 * @brief Cache PDF/SVG cap and join settings for later drawing.
 * @ingroup export_backend
 *
 * Requires an export context using the shared state layout. Emits nothing and does not affect existing
 * output; PostScript has its own immediate setter.
 */
void Rbc_RenderExportLineStyle(Rbc_RenderContext *, int, int);
/**
 * @brief Copy an optional PDF/SVG dash-background color.
 * @ingroup export_backend
 *
 * NULL disables the background. Stores the XColor value, not a borrowed color pointer, and emits
 * nothing immediately.
 */
void Rbc_RenderExportDashBackground(Rbc_RenderContext *, const XColor *);
/**
 * @brief Capture a Tk image over black and white to reconstruct RGBA.
 * @ingroup export_backend
 *
 * Requires a live export window and valid Tk image on its owning thread. Deleted, empty or NULL images
 * return NULL without recording an error. Allocation/capture failures record an export error. Owns
 * temporary pixmap, GC and captures; frees all intermediates and returns one caller-owned
 * Rbc_ColorImage on success. Release it with Rbc_FreeColorImage. Alpha is inferred from the maximum
 * channel difference and RGB is unpremultiplied; this is a raster approximation, not access to
 * original image pixels. Tk image display procedures run synchronously; the helper does not itself
 * guard against arbitrary custom-image callback destruction.
 */
Rbc_ColorImage Rbc_RenderExportRasterImage(Rbc_ExportContext *, Tk_Image);
/**
 * @brief Round a finite symbol measurement halfway away from zero.
 * @ingroup export_backend
 *
 * Requires the adjusted value to fit int. Preserves legacy export size corrections; does not provide
 * overflow checking.
 */
int Rbc_RenderExportSymbolRound(double);
#endif
