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
 *
 * @param[in,out] interp Interpreter owning the registry and receiving acquisition diagnostics.
 * @param[in] tkwin Borrowed live window supplying display, depth and coordinate context.
 * @param[in] imageName Name of an existing Tk image; copied by a new server.
 * @param[out] tilePtr Required output receiving an owned client token only on success.
 */
int Rbc_GetTile(Tcl_Interp *interp, Tk_Window tkwin, const char *imageName, Rbc_Tile *tilePtr);
/**
 * @brief Release one client token and destroy the server if it was the last.
 * NULL is harmless. A released token is invalid; the magic check does not make
 * stale pointers or repeated frees safe. Server teardown cancels pending idle
 * notification. Active notification snapshots defer record reclamation.
 *
 * @param[in] tile Client token to release, or NULL; ownership ends at this call.
 */
void Rbc_FreeTile(Rbc_Tile tile);
/**
 * @brief Return the borrowed image name; NULL yields an empty string.
 * An invalid magic value yields "not a tile". Neither result validates a stale
 * pointer. The server owns a live name; callers must not modify or free it.
 *
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 */
char *Rbc_NameOfTile(Rbc_Tile tile);
/**
 * @brief Install or replace one client's borrowed notification callback and data.
 * A NULL callback disables notification. No immediate callback is made.
 * Notifications run after an idle pixmap update over preserved client records.
 * Callbacks may release clients or process nested updates. Removed clients are
 * skipped; newly added clients wait for a later update. Interpreter deletion
 * stops the current pass. Callback data remains owned by the caller.
 *
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 * @param[in] changeProc Borrowed callback, or NULL to disable delivery.
 * @param[in] clientData Opaque caller-owned callback data, passed unchanged at delivery.
 */
void Rbc_SetTileChangedProc(Rbc_Tile tile, Rbc_TileChangedProc *changeProc, ClientData clientData);
/**
 * @brief Draw one rectangle using borrowed native tile resources.
 * Requires a live client/window and a usable cached pixmap with positive tile
 * dimensions. None drawables or nonpositive dimensions are ignored. Does not
 * refresh pending image changes; native failures have no status return.
 * Transparency uses platform masks rather than continuous alpha blending.
 *
 * @param[in] tkwin Borrowed live window supplying display, depth and coordinate context.
 * @param[in] drawable Compatible destination drawable, or None to draw nothing.
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 * @param[in] x Left destination coordinate in pixels.
 * @param[in] y Top destination coordinate in pixels.
 * @param[in] width Rectangle width in pixels; nonpositive values draw nothing.
 * @param[in] height Rectangle height in pixels; nonpositive values draw nothing.
 */
void Rbc_TileRectangle(Tk_Window tkwin, Drawable drawable, Rbc_Tile tile, int x, int y, int width, int height);
/**
 * @brief Draw a borrowed rectangle array with one shared tile.
 * None drawables, NULL arrays and nonpositive counts are ignored. Other
 * lifetime requirements match Rbc_TileRectangle. Windows retains DCs across
 * the batch; X11 batches opaque requests but draws masked rectangles singly.
 *
 * @param[in] tkwin Borrowed live window supplying display, depth and coordinate context.
 * @param[in] drawable Compatible destination drawable, or None to draw nothing.
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 * @param[in] rectArr Borrowed native-coordinate rectangle array, or NULL to draw nothing.
 * @param[in] nRects Number of array entries; nonpositive counts draw nothing.
 */
void Rbc_TileRectangles(Tk_Window tkwin, Drawable drawable, Rbc_Tile tile, XRectangle *rectArr, Tcl_Size nRects);
/**
 * @brief Draw a borrowed nonempty polygon with a shared tile.
 * Requires a valid drawable, live client and usable cache. Callers must reject
 * empty arrays; unlike rectangle helpers, polygon guards differ by platform.
 * Uses native fill rules and temporary clipping resources for transparency.
 * Does not force image refresh or propagate native drawing errors.
 *
 * @param[in] tkwin Borrowed live window supplying display, depth and coordinate context.
 * @param[in] drawable Valid compatible destination drawable.
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 * @param[in] pointArr Borrowed nonempty array of native-coordinate polygon vertices.
 * @param[in] nPoints Positive number of accessible vertices.
 */
void Rbc_TilePolygon(Tk_Window tkwin, Drawable drawable, Rbc_Tile tile, XPoint *pointArr, int nPoints);
/**
 * @brief Return a borrowed cached pixmap, or None for no tile.
 * Do not free it or retain it across a tile update/release. Image changes are
 * processed at idle, so this is a cache query, not a forced refresh.
 *
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 */
Pixmap Rbc_PixmapOfTile(Rbc_Tile tile);
/**
 * @brief Read cached tile dimensions without forcing an idle refresh.
 * Both output pointers are required. NULL or invalid tokens yield zero sizes;
 * deleted images may retain their last cached dimensions.
 *
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 * @param[out] widthPtr Required output for cached width in pixels.
 * @param[out] heightPtr Required output for cached height in pixels.
 */
void Rbc_SizeOfTile(Rbc_Tile tile, int *widthPtr, int *heightPtr);
/**
 * @brief Borrow the tile's Tk image without acquiring another image reference.
 * Returns NULL for no tile or a deleted image. Do not call Tk_FreeImage on the
 * result; use it only while the tile and current image state remain valid.
 *
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 */
Tk_Image Rbc_ImageOfTile(Rbc_Tile tile);
/**
 * @brief Borrow photo pixel storage into a caller-owned block record.
 * Clears the record first. Returns false for no tile or a non-photo image;
 * a deleted image returns true with an empty block. The pixel pointer remains
 * Tk-owned and is invalidated by image changes or deletion. No copy is made.
 *
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 * @param[out] blockPtr Required caller-owned descriptor; cleared before querying borrowed photo pixels.
 */
int Rbc_GetTilePhoto(Rbc_Tile tile, Tk_PhotoImageBlock *blockPtr);
/**
 * @brief Align a client pattern origin through its window ancestry.
 * Requires a live client and window. Adds position and border offsets up to
 * the toplevel, negates the result and updates both client origin and shared
 * server GC. Does not retain windows or change other clients stored origins.
 *
 * @param[in] tkwin Borrowed live window supplying display, depth and coordinate context.
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 * @param[in] x Horizontal offset before ancestry translation, in pixels.
 * @param[in] y Vertical offset before ancestry translation, in pixels.
 */
void Rbc_SetTileOrigin(Tk_Window tkwin, Rbc_Tile tile, int x, int y);
/**
 * @brief Set a client origin directly in drawable coordinates.
 * Requires a live client and compatible window. Updates its stored origin
 * and the shared server GC; callers must establish origins for each draw.
 *
 * @param[in] tkwin Borrowed live window supplying display, depth and coordinate context.
 * @param[in] tile Borrowed client token; validity and NULL policy are described above.
 * @param[in] x Horizontal tile origin in drawable pixels.
 * @param[in] y Vertical tile origin in drawable pixels.
 */
void Rbc_SetTSOrigin(Tk_Window tkwin, Rbc_Tile tile, int x, int y);

#endif /* _RBCTILE */
