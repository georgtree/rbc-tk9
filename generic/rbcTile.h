/*
 * rbcTile.h --
 *
 *      TODO: Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCTILE
#define _RBCTILE

#define TILE_THREAD_KEY "Rbc Tile Data"
#define TILE_MAGIC ((unsigned int) 0x46170277)

typedef struct Rbc_TileClientStruct *Rbc_Tile; /* Opaque type for tiles */

typedef void(Rbc_TileChangedProc)(ClientData clientData, Rbc_Tile tile);

int Rbc_GetTile(Tcl_Interp *interp, Tk_Window tkwin, const char *imageName, Rbc_Tile *tilePtr);

void Rbc_FreeTile(Rbc_Tile tile);

char *Rbc_NameOfTile(Rbc_Tile tile);

void Rbc_SetTileChangedProc(Rbc_Tile tile, Rbc_TileChangedProc *changeProc, ClientData clientData);

void Rbc_TileRectangle(Tk_Window tkwin, Drawable drawable, Rbc_Tile tile, int x, int y, unsigned int width,
                       unsigned int height);
void Rbc_TileRectangles(Tk_Window tkwin, Drawable drawable, Rbc_Tile tile, XRectangle *rectArr, int nRects);
void Rbc_TilePolygon(Tk_Window tkwin, Drawable drawable, Rbc_Tile tile, XPoint *pointArr, int nPoints);
Pixmap Rbc_PixmapOfTile(Rbc_Tile tile);

void Rbc_SizeOfTile(Rbc_Tile tile, int *widthPtr, int *heightPtr);

void Rbc_SetTileOrigin(Tk_Window tkwin, Rbc_Tile tile, int x, int y);

void Rbc_SetTSOrigin(Tk_Window tkwin, Rbc_Tile tile, int x, int y);

#endif /* _RBCTILE */
