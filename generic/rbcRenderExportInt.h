/* Shared export state and helpers. Not installed. See license.terms. */
#ifndef RBC_RENDER_EXPORT_INT_H
#define RBC_RENDER_EXPORT_INT_H

#include "rbcRenderInt.h"
#include "rbcFont.h"
#include <stdint.h>

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
Rbc_RenderContext *Rbc_RenderCreatePostscript(Rbc_ExportContext *);
Rbc_RenderContext *Rbc_RenderCreateSvg(Rbc_ExportContext *);
Rbc_RenderContext *Rbc_RenderCreatePdf(Rbc_ExportContext *);
void Rbc_RenderPostscriptStroke(Rbc_RenderContext *, const XColor *, int, const Rbc_Dashes *, int, int);
void Rbc_RenderPostscriptSymbol(Rbc_RenderContext *, const Rbc_RenderSymbolStyle *);
void Rbc_RenderPostscriptBarSymbol(Rbc_RenderContext *, const Rbc_RenderFillStyle *, int);
void Rbc_RenderExportError(Rbc_ExportContext *, const char *);
void Rbc_RenderExportEnd(Rbc_RenderContext *);
void Rbc_RenderExportLineStyle(Rbc_RenderContext *, int, int);
void Rbc_RenderExportDashBackground(Rbc_RenderContext *, const XColor *);
Rbc_ColorImage Rbc_RenderExportRasterImage(Rbc_ExportContext *, Tk_Image);
int Rbc_RenderExportSymbolRound(double);
#endif
