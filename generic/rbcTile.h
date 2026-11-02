/**
 * @file rbcTile.h
 * @brief Private image storage and tile resource services.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCTILE
#define _RBCTILE

#include <tk.h>

#define TILE_THREAD_KEY "Rbc Tile Data"
#define TILE_MAGIC ((unsigned int) 0x46170277)

typedef struct Rbc_TileClientStruct *Rbc_Tile; /* Opaque type for tiles */

typedef void(Rbc_TileChangedProc)(ClientData clientData, Rbc_Tile tile);

/**
 * @brief Acquire a new client token for a named Tk image.
 * Requires a live window. The interpreter-local server is shared by display,
 * depth and image name, but each successful call returns its own client.
 * On TCL_OK, the caller owns the token and must call Rbc_FreeTile before its
 * window/client data disappear. On TCL_ERROR, the output is not assigned.
 */
int Rbc_GetTile(Tcl_Interp *interp, Tk_Window tkwin, const char *imageName, Rbc_Tile *tilePtr);
/**
 * @brief Release one client token and destroy the server if it was the last.
 * NULL is harmless. A released token is invalid; the magic check does not make
 * stale pointers or repeated frees safe. Server teardown cancels pending idle
 * notification. Active notification snapshots defer record reclamation.
 */
void Rbc_FreeTile(Rbc_Tile tile);
/**
 * @brief Return the borrowed image name; NULL yields an empty string.
 * An invalid magic value yields "not a tile". Neither result validates a stale
 * pointer. The server owns a live name; callers must not modify or free it.
 */
char *Rbc_NameOfTile(Rbc_Tile tile);
/**
 * @brief Install or replace one client's borrowed notification callback and data.
 * A NULL callback disables notification. No immediate callback is made.
 * Notifications run after an idle pixmap update over preserved client records.
 * Callbacks may release clients or process nested updates. Removed clients are
 * skipped; newly added clients wait for a later update. Interpreter deletion
 * stops the current pass. Callback data remains owned by the caller.
 */
void Rbc_SetTileChangedProc(Rbc_Tile tile, Rbc_TileChangedProc *changeProc, ClientData clientData);
void Rbc_TileRectangle(Tk_Window tkwin, Drawable drawable, Rbc_Tile tile, int x, int y, int width, int height);
void Rbc_TileRectangles(Tk_Window tkwin, Drawable drawable, Rbc_Tile tile, XRectangle *rectArr, Tcl_Size nRects);
void Rbc_TilePolygon(Tk_Window tkwin, Drawable drawable, Rbc_Tile tile, XPoint *pointArr, int nPoints);
/**
 * @brief Return a borrowed cached pixmap, or None for no tile.
 * Do not free it or retain it across a tile update/release. Image changes are
 * processed at idle, so this is a cache query, not a forced refresh.
 */
Pixmap Rbc_PixmapOfTile(Rbc_Tile tile);
/**
 * @brief Read cached tile dimensions without forcing an idle refresh.
 * Both output pointers are required. NULL or invalid tokens yield zero sizes;
 * deleted images may retain their last cached dimensions.
 */
void Rbc_SizeOfTile(Rbc_Tile tile, int *widthPtr, int *heightPtr);
/**
 * @brief Borrow the tile's Tk image without acquiring another image reference.
 * Returns NULL for no tile or a deleted image. Do not call Tk_FreeImage on the
 * result; use it only while the tile and current image state remain valid.
 */
Tk_Image Rbc_ImageOfTile(Rbc_Tile tile);
/**
 * @brief Borrow photo pixel storage into a caller-owned block record.
 * Clears the record first. Returns false for no tile or a non-photo image;
 * a deleted image returns true with an empty block. The pixel pointer remains
 * Tk-owned and is invalidated by image changes or deletion. No copy is made.
 */
int Rbc_GetTilePhoto(Rbc_Tile tile, Tk_PhotoImageBlock *blockPtr);
/**
 * @brief Align a client pattern origin through its window ancestry.
 * Requires a live client and window. Adds position and border offsets up to
 * the toplevel, negates the result and updates both client origin and shared
 * server GC. Does not retain windows or change other clients stored origins.
 */
void Rbc_SetTileOrigin(Tk_Window tkwin, Rbc_Tile tile, int x, int y);
/**
 * @brief Set a client origin directly in drawable coordinates.
 * Requires a live client and compatible window. Updates its stored origin
 * and the shared server GC; callers must establish origins for each draw.
 */
void Rbc_SetTSOrigin(Tk_Window tkwin, Rbc_Tile tile, int x, int y);

#endif /* _RBCTILE */
