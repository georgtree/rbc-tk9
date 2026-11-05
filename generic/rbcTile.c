/*
 * rbcTile.c --
 *
 *      This module manages images for tiled backgrounds for the rbc toolkit.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include "rbcChain.h"
#include "rbcImage.h"
#include <X11/Xutil.h>

#include "rbcTile.h"

/**
 * @defgroup tile_windows_drawing Windows tiled shape drawing
 * @defgroup tile_lifecycle Tile cache lifecycle
 * @brief Interpreter registries, native cache refresh and client notifications.
 */

#define TILE_THREAD_KEY "Rbc Tile Data"
#define TILE_MAGIC ((unsigned int) 0x46170277)

/**
 * @brief Interpreter-owned registry of shared tile servers.
 *
 * Owns tileTable and its servers; interp is borrowed. The associated-data
 * deletion callback destroys all registered servers before freeing this record.
 */
typedef struct {
    Tcl_HashTable tileTable; /* Hash table of tile structures keyed by
                              * the name of the image. */
    Tcl_Interp *interp;
} TileInterpData;

/**
 * @brief Shared image cache and notification state for one tile key.
 *
 * Owns name, Tk image, pixmap, mask, GC and client chain. Registry links
 * are borrowed. Destruction releases resources immediately; Tcl preservation
 * can defer only final record reclamation.
 */
typedef struct {
    char *name;       /* Name of image used to generate the pixmap.*/
    Display *display; /* Display where pixmap was created. */
    int flags;        /* See definitions below. */
    int destroying;   /* Resources released; record may be preserved by notification. */
    Tcl_Interp *interp;
    Tcl_HashEntry *hashPtr; /* Pointer to hash table location */
    Tcl_HashTable *tablePtr;
    Pixmap pixmap;      /* Pixmap generated from image. */
    Pixmap mask;        /* Monochrome pixmap used as
                         * transparency mask. */
    GC gc;              /* GC */
    Tk_Image tkImage;   /* Tk image token. */
    Rbc_Chain *clients; /* Chain of clients sharing this tile. */
    int width, height;
} Tile;

/* If set, indicates that the image
* associated with the tile has been
* updated or deleted.  The tile pixmap
* will be changed and the clients of the
* tile will be notified (if they supplied
* a TileChangedProc routine. */
#define TILE_NOTIFY_PENDING 1

/**
 * @brief Individual tile token with borrowed window and callback data.
 *
 * The server chain owns membership. Origin is client-local, while native
 * resources are shared. A detached preserved token has its magic, server,
 * link and callback cleared before eventual reclamation.
 */
typedef struct Rbc_TileClientStruct {
    unsigned int magic;
    Tk_Window tkwin;                 /* Client window. */
    int xOrigin, yOrigin;            /* Tiling origin in relation to the
                                      * client window. */
    Rbc_TileChangedProc *notifyProc; /* If non-NULL, routine to
                                      * call to when tile image changes. */
    ClientData clientData;           /* Data to pass to when calling the above
                                      * routine. */
    Tile *tilePtr;                   /* Pointer to actual tile information */
    Rbc_ChainLink *linkPtr;          /* Pointer to client entry in the server's
                                      * client list.  Used to delete the client */
} TileClient;

/**
 * @brief Composite key for interpreter-local tile sharing.
 *
 * Borrows the display and interned image-name UID and records screen depth.
 * The whole structure is zeroed before hashing because padding participates
 * in the fixed-size multiword key.
 */
typedef struct {
    Display *display;
    Tk_Uid nameId;
    int depth;
} TileKey;

static Tcl_IdleProc UpdateTile;
static Tk_ImageChangedProc ImageChangedProc;
static Tcl_InterpDeleteProc TileInterpDeleteProc;

static TileInterpData *GetTileInterpData(Tcl_Interp *interp);
static void DestroyClient(TileClient *clientPtr);
static void DestroyTile(Tile *tilePtr);
static void RedrawTile(Tk_Window tkwin, Tile *tilePtr);
static Tile *CreateTile(Tcl_Interp *interp, Tk_Window tkwin, const char *imageName);
static TileClient *CreateClient(Tcl_Interp *interp, Tk_Window tkwin, const char *name);

#ifdef WIN32
static void TileRegion(HDC srcDC, HDC destDC, HDC maskDC, TileClient *clientPtr, int x, int y, int width, int height);
#endif

/**
 * @ingroup tile_lifecycle
 * @brief Refresh a live tile server using a client window.
 *
 * Requires a live Tk image and compatible window. Allocates a replacement
 * pixmap before freeing the old one when size changes, redraws the image,
 * replaces the tiled GC, records dimensions and rebuilds the photo alpha mask.
 * Releases masks with their platform-specific allocator. The image display
 * procedure is an external callback boundary; this helper provides no lifetime
 * barrier around it and has no error return.
 *
 * @param[in] tkwin Borrowed live window supplying display, depth and coordinate context.
 * @param[in,out] tilePtr Live server whose native cache is rebuilt; Tk image must remain valid.
 */
static void RedrawTile(Tk_Window tkwin, Tile *tilePtr) {
    GC newGC;
    Tk_PhotoHandle photo;
    XGCValues gcValues;
    int width, height;
    unsigned int gcMask;

    Tk_SizeOfImage(tilePtr->tkImage, &width, &height);
    Tk_MakeWindowExist(tkwin);
    if ((tilePtr->pixmap == None) || (width != tilePtr->width) || (height != tilePtr->height)) {
        Pixmap pixmap;

        /*
         * Create the new pixmap *before* destroying the old one.  I don't
         * why this happens, but if you delete the old pixmap first, the
         * old pixmap sometimes gets used in the client's GCs.  I suspect
         * it has something to do with the way Tk reallocates X resource
         * identifiers.
         */
        pixmap = Tk_GetPixmap(Tk_Display(tkwin), Tk_WindowId(tkwin), width, height, Tk_Depth(tkwin));
        if (tilePtr->pixmap != None) {
            Tk_FreePixmap(Tk_Display(tkwin), tilePtr->pixmap);
        }
        tilePtr->pixmap = pixmap;
    }
    Tk_RedrawImage(tilePtr->tkImage, 0, 0, width, height, tilePtr->pixmap, 0, 0);
    gcMask = (GCTile | GCFillStyle);
    gcValues.fill_style = FillTiled;
    gcValues.tile = tilePtr->pixmap;
    newGC = Tk_GetGC(tkwin, gcMask, &gcValues);
    if (tilePtr->gc != NULL) {
        Tk_FreeGC(Tk_Display(tkwin), tilePtr->gc);
    }
    tilePtr->gc = newGC;
    tilePtr->width = width;
    tilePtr->height = height;
    if (tilePtr->mask != None) {
#ifdef WIN32
        Tk_FreePixmap(Tk_Display(tkwin), tilePtr->mask);
#else
        XFreePixmap(Tk_Display(tkwin), tilePtr->mask);
#endif /* WIN32 */
        tilePtr->mask = None;
    }
    photo = Tk_FindPhoto(tilePtr->interp, Rbc_NameOfImage(tilePtr->tkImage));
    if (photo != NULL) {
        Tk_PhotoImageBlock src;

        Tk_PhotoGetImage(photo, &src);
        if ((src.offset[3] < src.pixelSize) && (src.offset[3] >= 0)) {
            tilePtr->mask = Rbc_PhotoImageMask(tkwin, src);
        }
    }
}

/**
 * @ingroup tile_lifecycle
 * @brief Reclaim a detached tile or client record after preservation ends.
 *
 * Receives storage transferred through Tcl_EventuallyFree. Frees only the
 * record: links, image and native resources must already have been released.
 *
 * @param[in] memory Detached heap record transferred by Tcl_EventuallyFree; freed here.
 */
static void FreeTileRecord(DestroyData memory) {
    ckfree(memory);
}

/**
 * @ingroup tile_lifecycle
 * @brief Refresh the cache and notify a preserved snapshot of clients.
 *
 * Preserves server and interpreter, clears the pending bit, and redraws using
 * the first client window. A deleted image instead releases only the cached
 * pixmap. Snapshots and preserves client membership after refresh, but reads
 * callback pointers at delivery time. Removed clients are skipped, new clients
 * wait for another pass, and callback replacement takes effect immediately.
 * Stops delivery if the server is destroying or interpreter is deleted. Releases
 * every snapshot reference even after early termination. Does not save or
 * restore interpreter result state, and refresh precedes the client snapshot.
 *
 * @param[in] clientData Live Tile server queued by the image-change callback.
 */
static void UpdateTile(ClientData clientData) {
    Tile *tilePtr = (Tile *)clientData;
    Tcl_Interp *interp = tilePtr->interp;
    TileClient **clients;
    Rbc_ChainLink *linkPtr;
    Tcl_Size count, i;

    Tcl_Preserve(tilePtr);
    Tcl_Preserve(interp);
    tilePtr->flags &= ~TILE_NOTIFY_PENDING;
    if (Tk_ImageIsDeleted(tilePtr->tkImage)) {
        if (tilePtr->pixmap != None) {
            Tk_FreePixmap(tilePtr->display, tilePtr->pixmap);
        }
        tilePtr->pixmap = None;
    } else {
        TileClient *clientPtr;
        /* Pick any client window to generate the new pixmap. */
        linkPtr = Rbc_ChainFirstLink(tilePtr->clients);
        clientPtr = Rbc_ChainGetValue(linkPtr);
        RedrawTile(clientPtr->tkwin, tilePtr);
    }
    /* Snapshot membership, not callbacks: removed clients are skipped, added
     * clients wait for a later update, and callback replacement takes effect. */
    count = Rbc_ChainGetLength(tilePtr->clients);
    clients = ckalloc(sizeof(*clients) * (size_t)count);
    i = 0;
    for (linkPtr = Rbc_ChainFirstLink(tilePtr->clients); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        clients[i] = Rbc_ChainGetValue(linkPtr);
        Tcl_Preserve(clients[i]);
        i++;
    }
    for (i = 0; i < count; i++) {
        TileClient *clientPtr = clients[i];
        if (tilePtr->destroying || Tcl_InterpDeleted(interp)) {
            break;
        }
        if (clientPtr->tilePtr == tilePtr && clientPtr->notifyProc != NULL) {
            (*clientPtr->notifyProc)(clientPtr->clientData, clientPtr);
        }
    }
    for (i = 0; i < count; i++) {
        Tcl_Release(clients[i]);
    }
    ckfree(clients);
    Tcl_Release(tilePtr);
    Tcl_Release(interp);
}

/**
 * @ingroup tile_lifecycle
 * @brief Coalesce Tk image changes into a deferred cache update.
 *
 * Ignores the changed rectangle and image dimensions. Schedules UpdateTile
 * only when TILE_NOTIFY_PENDING is clear. Does not redraw synchronously or
 * invoke client callbacks; server teardown must cancel the queued idle call.
 *
 * @param[in] clientData Live Tile server registered with Tk_GetImage.
 * @param[in] x Tk image-change metadata; ignored because refresh rebuilds the whole cache.
 * @param[in] y Tk image-change metadata; ignored because refresh rebuilds the whole cache.
 * @param[in] width Tk image-change metadata; ignored because refresh rebuilds the whole cache.
 * @param[in] height Tk image-change metadata; ignored because refresh rebuilds the whole cache.
 * @param[in] imageWidth Tk image-change metadata; ignored because refresh rebuilds the whole cache.
 * @param[in] imageHeight Tk image-change metadata; ignored because refresh rebuilds the whole cache.
 */
static void ImageChangedProc(ClientData clientData, int x, int y, int width, int height, int imageWidth,
                             int imageHeight) {
    Tile *tilePtr = (Tile *)clientData;
    if (!(tilePtr->flags & TILE_NOTIFY_PENDING)) {
        Tcl_DoWhenIdle(UpdateTile, tilePtr);
        tilePtr->flags |= TILE_NOTIFY_PENDING;
    }
}

/**
 * @ingroup tile_lifecycle
 * @brief Detach a server and release all owned resources.
 *
 * Marks destruction and cancels pending idle work. Invalidates remaining
 * client tokens, clears their links and callbacks, and defers record frees.
 * Destroys the client chain, removes a still-linked hash entry, releases pixmap,
 * mask, Tk image, GC and copied name, then defers the server record free.
 * Active notification preserves storage only, not these resources. Requires a
 * live server and is not an idempotent operation.
 *
 * @param[in] tilePtr Live owned server to detach and destroy; invalid after this call.
 */
static void DestroyTile(Tile *tilePtr) {
    Rbc_ChainLink *linkPtr;
    TileClient *clientPtr;

    tilePtr->destroying = TRUE;
    if (tilePtr->flags & TILE_NOTIFY_PENDING) {
        Tcl_CancelIdleCall(UpdateTile, tilePtr);
    }
    for (linkPtr = Rbc_ChainFirstLink(tilePtr->clients); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        clientPtr = Rbc_ChainGetValue(linkPtr);
        clientPtr->magic = 0;
        clientPtr->tilePtr = NULL;
        clientPtr->linkPtr = NULL;
        clientPtr->notifyProc = NULL;
        Tcl_EventuallyFree(clientPtr, FreeTileRecord);
    }
    Rbc_ChainDestroy(tilePtr->clients);
    if (tilePtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(tilePtr->hashPtr);
    }
    if (tilePtr->pixmap != None) {
        Tk_FreePixmap(tilePtr->display, tilePtr->pixmap);
    }
    if (tilePtr->mask != None) {
#ifdef WIN32
        Tk_FreePixmap(tilePtr->display, tilePtr->mask);
#else
        XFreePixmap(tilePtr->display, tilePtr->mask);
#endif
    }
    Tk_FreeImage(tilePtr->tkImage);

    if (tilePtr->gc != NULL) {
        Tk_FreeGC(tilePtr->display, tilePtr->gc);
    }
    if (tilePtr->name != NULL) {
        ckfree((char *)tilePtr->name);
    }
    Tcl_EventuallyFree(tilePtr, FreeTileRecord);
}

/**
 * @ingroup tile_lifecycle
 * @brief Acquire a Tk image and construct an unregistered tile server.
 *
 * Allocates a zeroed record and acquires the image with ImageChangedProc.
 * Acquisition failure frees the record and returns NULL with the Tk error.
 * Success owns the copied name, image and client chain, and immediately builds
 * native cache resources before returning. Hash registration and first-client
 * attachment belong to CreateClient. Tk image acquisition and drawing can call
 * image-provider code before that attachment exists.
 *
 * @param[in,out] interp Interpreter owning the registry and receiving acquisition diagnostics.
 * @param[in] tkwin Borrowed live window supplying display, depth and coordinate context.
 * @param[in] imageName Borrowed existing image name; copied on successful acquisition.
 */
static Tile *CreateTile(Tcl_Interp *interp, Tk_Window tkwin, const char *imageName) {
    Tile *tilePtr;
    Tk_Image tkImage;

    tilePtr = RbcCalloc(1, sizeof(Tile));
    assert(tilePtr);
    /*
     * Get the image. Funnel all change notifications to a single routine.
     */
    tkImage = Tk_GetImage(interp, tkwin, imageName, ImageChangedProc, tilePtr);
    if (tkImage == NULL) {
        ckfree((char *)tilePtr);
        return NULL;
    }
    /*
     * Initialize the tile server.
     */
    tilePtr->display = Tk_Display(tkwin);
    tilePtr->interp = interp;
    tilePtr->name = RbcStrdup(imageName);
    tilePtr->clients = Rbc_ChainCreate();
    tilePtr->tkImage = tkImage;
    RedrawTile(tkwin, tilePtr);
    return tilePtr;
}

/**
 * @ingroup tile_lifecycle
 * @brief Detach one live client and destroy an unused server.
 *
 * Removes its chain link first. If no clients remain, destroys the server;
 * then clears client validity, server/link pointers and callback and defers
 * record reclamation. Requires a valid attached client. The removed record
 * is no longer in the server chain when last-client teardown occurs.
 *
 * @param[in] clientPtr Attached client to detach and release; invalid after this call.
 */
static void DestroyClient(TileClient *clientPtr) {
    Tile *tilePtr;
    
    tilePtr = clientPtr->tilePtr;
    /* Remove the client from the server's list */
    if (clientPtr->linkPtr != NULL) {
        Rbc_ChainDeleteLink(tilePtr->clients, clientPtr->linkPtr);
    }
    if (Rbc_ChainGetLength(tilePtr->clients) == 0) {
        /*
         * If there are no more clients of the tile, then remove the
         * pixmap, image, and the server record.
         */
        DestroyTile(tilePtr);
    }
    clientPtr->magic = 0;
    clientPtr->tilePtr = NULL;
    clientPtr->linkPtr = NULL;
    clientPtr->notifyProc = NULL;
    Tcl_EventuallyFree(clientPtr, FreeTileRecord);
}

/**
 * @ingroup tile_lifecycle
 * @brief Acquire an interpreter-local server and append a new client token.
 *
 * Zeroes the complete display/depth/interned-name key, including padding.
 * Creates a hash entry and server if absent, removing the entry on acquisition
 * failure. Every success allocates a distinct zeroed client with a borrowed
 * window and no callback, appends it to the server chain and returns ownership
 * to the caller. Existing server resources are shared, not copied.
 *
 * @param[in,out] interp Interpreter owning the registry and receiving acquisition diagnostics.
 * @param[in] tkwin Borrowed live window supplying display, depth and coordinate context.
 * @param[in] name Borrowed Tk image name used in the display/depth/name registry key.
 */
static TileClient *CreateClient(Tcl_Interp *interp, Tk_Window tkwin, const char *name) {
    TileClient *clientPtr;
    Tile *tilePtr;
    TileInterpData *dataPtr;
    Tcl_HashEntry *hPtr;
    int isNew;
    TileKey key;

    dataPtr = GetTileInterpData(interp);
    /* The hash key includes structure padding. */
    memset(&key, 0, sizeof(key));    
    key.nameId = Tk_GetUid(name);
    key.display = Tk_Display(tkwin);
    key.depth = Tk_Depth(tkwin);
    hPtr = Tcl_CreateHashEntry(&dataPtr->tileTable, (char *)&key, &isNew);
    if (isNew) {
        tilePtr = CreateTile(interp, tkwin, name);
        if (tilePtr == NULL) {
            Tcl_DeleteHashEntry(hPtr);
            return NULL;
        }
        tilePtr->hashPtr = hPtr;
        tilePtr->tablePtr = &(dataPtr->tileTable);
        Tcl_SetHashValue(hPtr, tilePtr);
    } else {
        tilePtr = Tcl_GetHashValue(hPtr);
    }
    clientPtr = RbcCalloc(1, sizeof(TileClient));
    assert(clientPtr);
    /* Initialize client information. */
    clientPtr->magic = TILE_MAGIC;
    clientPtr->tkwin = tkwin;
    clientPtr->linkPtr = Rbc_ChainAppend(tilePtr->clients, clientPtr);
    clientPtr->tilePtr = tilePtr;
    return clientPtr;
}

/**
 * @ingroup tile_lifecycle
 * @brief Destroy all tile servers when interpreter associated data is removed.
 *
 * The clientData argument is interpreter-owned TileInterpData, not thread
 * local storage. Clears each server hash pointer before destroying it so hash
 * iteration remains valid, then deletes the table and associated-data entry
 * and frees the registry. Invalidates outstanding client tokens.
 *
 * @param[in] clientData Owned TileInterpData registry supplied as interpreter associated data.
 * @param[in,out] interp Interpreter whose tile registry is being removed.
 */
static void TileInterpDeleteProc(ClientData clientData, Tcl_Interp *interp) {
    TileInterpData *dataPtr = clientData;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Tile *tilePtr;

    for (hPtr = Tcl_FirstHashEntry(&(dataPtr->tileTable), &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        tilePtr = Tcl_GetHashValue(hPtr);
        tilePtr->hashPtr = NULL;
        DestroyTile(tilePtr);
    }
    Tcl_DeleteHashTable(&(dataPtr->tileTable));
    Tcl_DeleteAssocData(interp, TILE_THREAD_KEY);
    ckfree((char *)dataPtr);
}

/**
 * @ingroup tile_lifecycle
 * @brief Find or create the interpreter-owned tile registry.
 *
 * Lazily allocates associated data and installs TileInterpDeleteProc. Uses
 * a fixed-size multiword key for TileKey. Returns borrowed registry storage
 * valid until associated-data teardown; no global or cross-interpreter sharing.
 *
 * @param[in,out] interp Live interpreter owning the lazily allocated associated-data registry.
 */
static TileInterpData *GetTileInterpData(Tcl_Interp *interp) {
    TileInterpData *dataPtr;
    Tcl_InterpDeleteProc *proc;

    dataPtr = (TileInterpData *)Tcl_GetAssocData(interp, TILE_THREAD_KEY, &proc);
    if (dataPtr == NULL) {
        dataPtr = (TileInterpData *)ckalloc(sizeof(TileInterpData));
        assert(dataPtr);
        dataPtr->interp = interp;
        Tcl_SetAssocData(interp, TILE_THREAD_KEY, TileInterpDeleteProc, dataPtr);
        Tcl_InitHashTable(&(dataPtr->tileTable), sizeof(TileKey) / sizeof(int));
    }
    return dataPtr;
}

/* Public API for tiles. */

int Rbc_GetTile(Tcl_Interp *interp, Tk_Window tkwin, const char *imageName, Rbc_Tile *tokenPtr) {
    TileClient *clientPtr;

    clientPtr = CreateClient(interp, tkwin, imageName);
    if (clientPtr == NULL) {
        return TCL_ERROR;
    }
    *tokenPtr = clientPtr;
    return TCL_OK;
}

void Rbc_FreeTile(TileClient *clientPtr) {
    if ((clientPtr == NULL) || (clientPtr->magic != TILE_MAGIC)) {
        return; /* No tile */
    }
    DestroyClient(clientPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_NameOfTile
 *
 *      Returns the name of the image from which the tile was
 *      generated.
 *
 * Parameters:
 *      TileClient *clientPtr - Tile to query
 *
 * Results:
 *      The name of the image is returned.  The name is not unique.
 *      Many tiles may use the same image.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
char *Rbc_NameOfTile(TileClient *clientPtr) {
    if (clientPtr == NULL) {
        return "";
    }
    if (clientPtr->magic != TILE_MAGIC) {
        return "not a tile";
    }
    return clientPtr->tilePtr->name;
}

Pixmap Rbc_PixmapOfTile(TileClient *clientPtr) {
    if ((clientPtr == NULL) || (clientPtr->magic != TILE_MAGIC)) {
        return None;
    }
    return clientPtr->tilePtr->pixmap;
}

/* Borrow the image until the next tile change; the caller must not free it. */
Tk_Image Rbc_ImageOfTile(TileClient *clientPtr) {
    if (clientPtr == NULL || clientPtr->magic != TILE_MAGIC ||
        Tk_ImageIsDeleted(clientPtr->tilePtr->tkImage)) {
        return NULL;
    }
    return clientPtr->tilePtr->tkImage;
}

/* Borrow photo pixels until the next image change. Deleted images are empty. */
int Rbc_GetTilePhoto(TileClient *clientPtr, Tk_PhotoImageBlock *blockPtr) {
    Tile *tilePtr;
    Tk_PhotoHandle photo;

    memset(blockPtr, 0, sizeof(*blockPtr));
    if ((clientPtr == NULL) || (clientPtr->magic != TILE_MAGIC)) {
        return FALSE;
    }
    tilePtr = clientPtr->tilePtr;
    if (Tk_ImageIsDeleted(tilePtr->tkImage)) {
        return TRUE;
    }
    photo = Tk_FindPhoto(tilePtr->interp, Rbc_NameOfImage(tilePtr->tkImage));
    return (photo != NULL) && Tk_PhotoGetImage(photo, blockPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_SizeOfTile
 *
 *      Returns the width and height of the tile.
 *
 * Parameters:
 *      TileClient *clientPtr - Tile to query
 *      int *widthPtr - -
 *      int *heightPtr - Returned dimensions of the tile (out) 
 *
 * Results:
 *      The width and height of the tile are returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_SizeOfTile(TileClient *clientPtr, int *widthPtr, int *heightPtr) {
    if ((clientPtr == NULL) || (clientPtr->magic != TILE_MAGIC)) {
        *widthPtr = *heightPtr = 0;
        return; /* No tile given. */
    }
    *widthPtr = clientPtr->tilePtr->width;
    *heightPtr = clientPtr->tilePtr->height;
}

void Rbc_SetTileChangedProc(TileClient *clientPtr, Rbc_TileChangedProc *notifyProc, ClientData clientData) {
    if ((clientPtr != NULL) && (clientPtr->magic == TILE_MAGIC)) {
        clientPtr->notifyProc = notifyProc;
        clientPtr->clientData = clientData;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_SetTileOrigin --
 *
 *      Set the pattern origin of the tile to a common point (i.e. the
 *      origin (0,0) of the top level window) so that tiles from two
 *      different widgets will match up.  This done by setting the
 *      GCTileStipOrigin field is set to the translated origin of the
 *      toplevel window in the hierarchy.
 *
 * Parameters:
 *      Tk_Window tkwin
 *      TileClient *clientPtr
 *      int x
 *      int y
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The GCTileStipOrigin is reset in the GC.  This will cause the
 *      tile origin to change when the GC is used for drawing.
 *
 *----------------------------------------------------------------------
 */
void Rbc_SetTileOrigin(Tk_Window tkwin, TileClient *clientPtr, int x, int y) {
    while (!Tk_IsTopLevel(tkwin)) {
        x += Tk_X(tkwin) + Tk_Changes(tkwin)->border_width;
        y += Tk_Y(tkwin) + Tk_Changes(tkwin)->border_width;
        tkwin = Tk_Parent(tkwin);
    }
    XSetTSOrigin(Tk_Display(tkwin), clientPtr->tilePtr->gc, -x, -y);
    clientPtr->xOrigin = -x;
    clientPtr->yOrigin = -y;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_SetTSOrigin --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *      TileClient *clientPtr
 *      int x
 *      int y
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_SetTSOrigin(Tk_Window tkwin, TileClient *clientPtr, int x, int y) {
    XSetTSOrigin(Tk_Display(tkwin), clientPtr->tilePtr->gc, x, y);
    clientPtr->xOrigin = x;
    clientPtr->yOrigin = y;
}

#ifdef WIN32
#define MASKPAT 0x00E20746 /* dest = (src & pat) | (!src & dst) */
#define COPYFG 0x00CA0749  /* dest = (pat & src) | (!pat & dst) */
#define COPYBG 0x00AC0744  /* dest = (!pat & src) | (pat & dst) */

/**
 * @brief Blit a Windows tile repeatedly across a rectangular region.
 *
 * Requires positive cached tile dimensions and borrowed source/destination
 * DCs. Aligns the first repetition to the client origin, then clips edge
 * copies to the requested rectangle. Opaque copies use SRCCOPY. Transparent
 * copies combine SRCAND and SRCPAINT with matching mask/color source offsets
 * at cropped tile boundaries. Does not
 * acquire/release DCs or report BitBlt failure.
 *
 * @param[in] srcDC Borrowed memory DC with the cached tile color bitmap selected.
 * @param[in,out] destDC Borrowed destination DC receiving repeated clipped copies; clipping/mapping state is
 *   established by the caller.
 * @param[in] maskDC Borrowed matching monochrome-mask DC, or NULL for opaque SRCCOPY.
 * @param[in] clientPtr Borrowed live tile client with current cached pixmap, positive tile dimensions and
 *   destination-relative origin.
 * @param[in] x Left destination coordinate in pixels; coordinate arithmetic must remain representable.
 * @param[in] y Top destination coordinate in pixels; coordinate arithmetic must remain representable.
 * @param[in] width Positive rectangle width in pixels; endpoint arithmetic must remain representable.
 * @param[in] height Positive rectangle height in pixels; endpoint arithmetic must remain representable.
 * @ingroup tile_windows_drawing
 */
static void TileRegion(HDC srcDC, HDC destDC, HDC maskDC, TileClient *clientPtr, int x, int y, int width, int height) {
    Tile *tilePtr = clientPtr->tilePtr;
    int destX, destY;
    int destWidth, destHeight;
    int srcX, srcY;
    int startX, startY; /* Starting upper left corner of region. */
    int delta;
    int left, top, right, bottom;

    startX = x;
    if (x < clientPtr->xOrigin) {
        delta = (clientPtr->xOrigin - x) % tilePtr->width;
        if (delta > 0) {
            startX -= (tilePtr->width - delta);
        }
    } else if (x > clientPtr->xOrigin) {
        delta = (x - clientPtr->xOrigin) % tilePtr->width;
        if (delta > 0) {
            startX -= delta;
        }
    }
    startY = y;
    if (y < clientPtr->yOrigin) {
        delta = (clientPtr->yOrigin - y) % tilePtr->height;
        if (delta > 0) {
            startY -= (tilePtr->height - delta);
        }
    } else if (y >= clientPtr->yOrigin) {
        delta = (y - clientPtr->yOrigin) % tilePtr->height;
        if (delta > 0) {
            startY -= delta;
        }
    }
#ifdef notdef
    PurifyPrintf("tile is (%d,%d,%d,%d)\n", clientPtr->xOrigin, clientPtr->yOrigin, tilePtr->width, tilePtr->height);
    PurifyPrintf("region is (%d,%d,%d,%d)\n", x, y, width, height);
    PurifyPrintf("starting at %d,%d\n", startX, startY);
#endif
    left = x;
    right = x + width;
    top = y;
    bottom = y + height;
    for (y = startY; y < bottom; y += tilePtr->height) {
        srcY = 0;
        destY = y;
        destHeight = tilePtr->height;
        if (y < top) {
            srcY = (top - y);
            destHeight = tilePtr->height - srcY;
            destY = top;
        }
        if ((destY + destHeight) > bottom) {
            destHeight = (bottom - destY);
        }
        for (x = startX; x < right; x += tilePtr->width) {
            srcX = 0;
            destX = x;
            destWidth = tilePtr->width;
            if (x < left) {
                srcX = (left - x);
                destWidth = tilePtr->width - srcX;
                destX = left;
            }
            if ((destX + destWidth) > right) {
                destWidth = (right - destX);
            }
#ifdef notdef
            PurifyPrintf("drawing pattern (%d,%d,%d,%d) at %d,%d\n", srcX, srcY, destWidth, destHeight, destX, destY);
#endif
            if (tilePtr->mask != None) { /* With transparency. */
#ifdef notdef
                HDC maskDC;
                Rbc_WinDrawableDC *maskStatePtr;

                maskDC = Rbc_WinAcquireDrawableDC(tilePtr->display, tilePtr->mask, &maskStatePtr);
                SetBkColor(destDC, RGB(255, 255, 255));
                SetTextColor(destDC, RGB(0, 0, 0));
#endif
                BitBlt(destDC, destX, destY, destWidth, destHeight, maskDC, srcX, srcY, SRCAND);
                BitBlt(destDC, destX, destY, destWidth, destHeight, srcDC, srcX, srcY, SRCPAINT);
#ifdef notdef
                Rbc_WinReleaseDrawableDC(maskStatePtr);
#endif
            } else { /* Opaque tile. */
                BitBlt(destDC, destX, destY, destWidth, destHeight, srcDC, srcX, srcY, SRCCOPY);
            }
        }
    }
}

/**
 * @brief Fill a Windows polygon by clipping tiled bounding-box copies.
 *
 * Requires a nonempty point array and live cached tile; None drawable is
 * ignored. Copies points relative to the inclusive bounding box, converts
 * them to device coordinates, installs a polygon region using the GC fill
 * rule and tiles the bounds. Restores bitmap selection, deletes temporary
 * DC/region and releases acquired DCs. Explicitly clears the polygon clip
 * before release; does not report allocation or native drawing failures.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] drawable Borrowed destination drawable compatible with the window/display; no ownership is
 *   transferred.
 * @param[in] clientPtr Borrowed live tile client with current cached pixmap, positive tile dimensions and
 *   destination-relative origin.
 * @param[in] pointArr Borrowed nonempty array of nPoints absolute XPoint coordinates; not modified.
 * @param[in] nPoints Positive number of initialized point entries; caller validates array bounds.
 * @ingroup tile_windows_drawing
 */
void Rbc_TilePolygon(Tk_Window tkwin, Drawable drawable, TileClient *clientPtr, XPoint pointArr[], int nPoints) {
    HBITMAP oldBitmap;
    HDC hDC, memDC;
    HRGN hRgn;
    POINT *p, *winPts;
    Region2D bbox;
    Tile *tilePtr;
    Rbc_WinDrawableDC *dcStatePtr;
    HBITMAP hBitmap;
    XPoint *endPtr, *pointPtr;
    int fillMode;
    int width, height;

    if (drawable == None) {
        return;
    }
    tilePtr = clientPtr->tilePtr;
    /* Determine the bounding box of the polygon. */
    bbox.left = bbox.right = pointArr[0].x;
    bbox.top = bbox.bottom = pointArr[0].y;
    endPtr = pointArr + nPoints;
    for (pointPtr = pointArr; pointPtr < endPtr; pointPtr++) {
        if (pointPtr->x < bbox.left) {
            bbox.left = pointPtr->x;
        }
        if (pointPtr->x > bbox.right) {
            bbox.right = pointPtr->x;
        }
        if (pointPtr->y < bbox.top) {
            bbox.top = pointPtr->y;
        }
        if (pointPtr->y > bbox.bottom) {
            bbox.bottom = pointPtr->y;
        }
    }
    width = bbox.right - bbox.left + 1;
    height = bbox.bottom - bbox.top + 1;
    /* Allocate and fill an array of POINTS to create the polygon path. */
    p = winPts = (POINT *)ckalloc(sizeof(POINT) * nPoints);
    for (pointPtr = pointArr; pointPtr < endPtr; pointPtr++) {
        p->x = pointPtr->x - bbox.left;
        p->y = pointPtr->y - bbox.top;
        p++;
    }
    hDC = Rbc_WinAcquireDrawableDC(Tk_Display(tkwin), drawable, &dcStatePtr);
    Rbc_WinSetROP2(hDC, tilePtr->gc->function);
    fillMode = (tilePtr->gc->fill_rule == EvenOddRule) ? ALTERNATE : WINDING;
    /* Use the polygon as a clip path. */
    LPtoDP(hDC, winPts, nPoints);
    hRgn = CreatePolygonRgn(winPts, nPoints, fillMode);
    SelectClipRgn(hDC, hRgn);
    OffsetClipRgn(hDC, bbox.left, bbox.top);
    ckfree((char *)winPts);
    hBitmap = Rbc_WinGetPixmapHandle(tilePtr->pixmap);
    memDC = CreateCompatibleDC(hDC);
    oldBitmap = SelectBitmap(memDC, hBitmap);
    /* Tile the bounding box. */
    if (tilePtr->mask != None) {
        Rbc_WinDrawableDC *maskStatePtr;
        HDC maskDC;

        maskDC = Rbc_WinAcquireDrawableDC(tilePtr->display, tilePtr->mask, &maskStatePtr);
        SetBkColor(hDC, RGB(255, 255, 255));
        SetTextColor(hDC, RGB(0, 0, 0));
        TileRegion(memDC, hDC, maskDC, clientPtr, bbox.left, bbox.top, width, height);
        Rbc_WinReleaseDrawableDC(maskStatePtr);
    } else {
        TileRegion(memDC, hDC, NULL, clientPtr, bbox.left, bbox.top, width, height);
    }
    SelectBitmap(memDC, oldBitmap);
    DeleteDC(memDC);
    SelectClipRgn(hDC, NULL);
    DeleteRgn(hRgn);
    Rbc_WinReleaseDrawableDC(dcStatePtr);
}

/**
 * @brief Draw one tiled rectangle through Windows DCs.
 *
 * Ignores None drawables and nonpositive dimensions. Requires a live client
 * with usable cached pixmap and positive tile size. Acquires the destination
 * DC, selects the source bitmap into a temporary memory DC, and optionally
 * acquires the mask DC for TileRegion. Restores the selected bitmap, deletes
 * the memory DC and releases acquired DCs. Native API failures are not
 * propagated; no tile refresh or window ownership is acquired.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] drawable Borrowed compatible destination drawable; None causes no drawing.
 * @param[in] clientPtr Borrowed live tile client with current cached pixmap, positive tile dimensions and
 *   destination-relative origin.
 * @param[in] x Left destination coordinate in pixels; coordinate arithmetic must remain representable.
 * @param[in] y Top destination coordinate in pixels; coordinate arithmetic must remain representable.
 * @param[in] width Destination width in pixels; nonpositive values cause no drawing.
 * @param[in] height Destination height in pixels; nonpositive values cause no drawing.
 * @ingroup tile_windows_drawing
 */
void Rbc_TileRectangle(Tk_Window tkwin, Drawable drawable, TileClient *clientPtr, int x, int y, int width, int height) {
    HBITMAP oldBitmap;
    HDC hDC, memDC;
    Tile *tilePtr;
    Rbc_WinDrawableDC *dcStatePtr;
    HBITMAP hBitmap;

    if ((drawable == None) || (width <= 0) || (height <= 0)) {
        return;
    }
    tilePtr = clientPtr->tilePtr;
    hDC = Rbc_WinAcquireDrawableDC(Tk_Display(tkwin), drawable, &dcStatePtr);
    Rbc_WinSetROP2(hDC, tilePtr->gc->function);
    hBitmap = Rbc_WinGetPixmapHandle(tilePtr->pixmap);
    memDC = CreateCompatibleDC(hDC);
    oldBitmap = SelectBitmap(memDC, hBitmap);
    /* Tile the bounding box. */
    if (tilePtr->mask != None) {
        Rbc_WinDrawableDC *maskStatePtr;
        HDC maskDC;

        maskDC = Rbc_WinAcquireDrawableDC(tilePtr->display, tilePtr->mask, &maskStatePtr);
        SetBkColor(hDC, RGB(255, 255, 255));
        SetTextColor(hDC, RGB(0, 0, 0));
        TileRegion(memDC, hDC, maskDC, clientPtr, x, y, width, height);
        Rbc_WinReleaseDrawableDC(maskStatePtr);
    } else {
        TileRegion(memDC, hDC, NULL, clientPtr, x, y, width, height);
    }
    SelectBitmap(memDC, oldBitmap);
    DeleteDC(memDC);
    Rbc_WinReleaseDrawableDC(dcStatePtr);
}

/**
 * @brief Draw a rectangle batch while retaining Windows drawing contexts.
 *
 * Ignores None drawables, NULL arrays and nonpositive counts. Acquires and
 * selects source/destination resources once, optionally acquires one mask DC,
 * and calls TileRegion for each rectangle. Restores bitmap selection and
 * releases DCs after the batch. Borrows the array; uses client-stored origin
 * and requires usable cached tile resources.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] drawable Borrowed compatible destination drawable; None causes no drawing.
 * @param[in] clientPtr Borrowed live tile client with current cached pixmap, positive tile dimensions and
 *   destination-relative origin.
 * @param[in] rectArr Borrowed array of nRectangles absolute rectangles; NULL causes no drawing.
 * @param[in] nRectangles Rectangle count; nonpositive counts cause no drawing.
 * @ingroup tile_windows_drawing
 */
void Rbc_TileRectangles(Tk_Window tkwin, Drawable drawable, TileClient *clientPtr, XRectangle rectArr[],
                        Tcl_Size nRectangles) {
    HBITMAP oldBitmap;
    HDC hDC, memDC;
    Tile *tilePtr;
    Rbc_WinDrawableDC *dcStatePtr;
    HBITMAP hBitmap;
    XRectangle *rectPtr, *endPtr;

    if ((drawable == None) || (rectArr == NULL) || (nRectangles <= 0)) {
        return;
    }
    tilePtr = clientPtr->tilePtr;
    hDC = Rbc_WinAcquireDrawableDC(Tk_Display(tkwin), drawable, &dcStatePtr);
    Rbc_WinSetROP2(hDC, tilePtr->gc->function);
    hBitmap = Rbc_WinGetPixmapHandle(tilePtr->pixmap);
    memDC = CreateCompatibleDC(hDC);
    oldBitmap = SelectBitmap(memDC, hBitmap);
    endPtr = rectArr + nRectangles;
    /* Tile the bounding box. */
    if (tilePtr->mask != None) {
        Rbc_WinDrawableDC *maskStatePtr;
        HDC maskDC;

        maskDC = Rbc_WinAcquireDrawableDC(tilePtr->display, tilePtr->mask, &maskStatePtr);
        SetBkColor(hDC, RGB(255, 255, 255));
        SetTextColor(hDC, RGB(0, 0, 0));
        for (rectPtr = rectArr; rectPtr < endPtr; rectPtr++) {
            TileRegion(memDC, hDC, maskDC, clientPtr, (int)rectPtr->x, (int)rectPtr->y, (int)rectPtr->width,
                       (int)rectPtr->height);
        }
        Rbc_WinReleaseDrawableDC(maskStatePtr);
    } else {
        for (rectPtr = rectArr; rectPtr < endPtr; rectPtr++) {
            TileRegion(memDC, hDC, NULL, clientPtr, (int)rectPtr->x, (int)rectPtr->y, (int)rectPtr->width,
                       (int)rectPtr->height);
        }
    }
    SelectBitmap(memDC, oldBitmap);
    DeleteDC(memDC);
    Rbc_WinReleaseDrawableDC(dcStatePtr);
}

#else

/**
 * @defgroup tile_native_masks X11 tile transparency masks
 * @brief Temporary mask construction for native tiled shapes.
 */

/**
 * @ingroup tile_native_masks
 * @brief Build an X11 rectangle-sized repeating transparency mask.
 *
 * Requires positive dimensions and a valid one-bit stipple. Allocates a
 * depth-one pixmap, fills it using FillOpaqueStippled at the client origin
 * translated into mask-local coordinates, and releases the temporary private
 * GC. Returns an owned pixmap for Tk_FreePixmap; does not modify the source
 * mask or return an error status.
 *
 * @param[in] display Borrowed live X display owning the supplied drawable and resources.
 * @param[in] drawable Borrowed destination drawable compatible with the window/display; no ownership is
 *   transferred.
 * @param[in] x Left destination coordinate in pixels; coordinate arithmetic must remain representable.
 * @param[in] y Top destination coordinate in pixels; coordinate arithmetic must remain representable.
 * @param[in] width Positive rectangle width in pixels; endpoint arithmetic must remain representable.
 * @param[in] height Positive rectangle height in pixels; endpoint arithmetic must remain representable.
 * @param[in] mask Borrowed valid depth-one repeating stipple; not consumed or modified.
 * @param[in] xOrigin Horizontal tile origin in destination coordinates, translated into mask-local coordinates
 *   here.
 * @param[in] yOrigin Vertical tile origin in destination coordinates, translated into mask-local coordinates
 *   here.
 */
static Pixmap RectangleMask(Display *display, Drawable drawable, int x, int y, int width, int height, Pixmap mask,
                            int xOrigin, int yOrigin) {
    GC gc;
    Pixmap bitmap;
    XGCValues gcValues;
    unsigned long gcMask;

    bitmap = Tk_GetPixmap(display, drawable, width, height, 1);
    gcMask = (GCForeground | GCBackground | GCFillStyle | GCTileStipXOrigin | GCTileStipYOrigin | GCStipple);
    gcValues.foreground = 0x1;
    gcValues.background = 0x0;
    gcValues.fill_style = FillOpaqueStippled;
    gcValues.ts_x_origin = xOrigin - x;
    gcValues.ts_y_origin = yOrigin - y;
    gcValues.stipple = mask;
    gc = XCreateGC(display, bitmap, gcMask, &gcValues);
    XFillRectangle(display, bitmap, gc, 0, 0, (unsigned int)width, (unsigned int)height);
    Rbc_FreePrivateGC(display, gc);
    return bitmap;
}

/**
 * @brief Draw one tiled rectangle through an X11 tiled GC.
 *
 * Ignores None drawables and nonpositive dimensions. Opaque drawing uses
 * XFillRectangle directly. Transparency allocates a rectangle-local mask,
 * installs it on the shared server GC, fills, resets clip mask/origin to
 * None/zero and frees the mask. Does not preserve a previously installed
 * server-GC clip or force tile refresh.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] drawable Borrowed compatible destination drawable; None causes no drawing.
 * @param[in,out] clientPtr Borrowed live client with usable cached tile resources; shared GC clip state may be
 *   changed and reset during masked drawing.
 * @param[in] x Left destination coordinate in pixels; coordinate arithmetic must remain representable.
 * @param[in] y Top destination coordinate in pixels; coordinate arithmetic must remain representable.
 * @param[in] width Destination width in pixels; nonpositive values cause no drawing.
 * @param[in] height Destination height in pixels; nonpositive values cause no drawing.
 */
void Rbc_TileRectangle(Tk_Window tkwin, Drawable drawable, TileClient *clientPtr, int x, int y, int width, int height) {
    Tile *tilePtr;
    Display *display;

    if ((drawable == None) || (width <= 0) || (height <= 0)) {
        return;
    }
    display = Tk_Display(tkwin);
    tilePtr = clientPtr->tilePtr;
    if (clientPtr->tilePtr->mask != None) {
        Pixmap mask;

        mask = RectangleMask(display, drawable, x, y, width, height, tilePtr->mask, clientPtr->xOrigin,
                             clientPtr->yOrigin);
        XSetClipMask(display, tilePtr->gc, mask);
        XSetClipOrigin(display, tilePtr->gc, x, y);
        XFillRectangle(display, drawable, tilePtr->gc, x, y, (unsigned int)width, (unsigned int)height);
        XSetClipMask(display, tilePtr->gc, None);
        XSetClipOrigin(display, tilePtr->gc, 0, 0);
        Tk_FreePixmap(display, mask);
    } else {
        XFillRectangle(display, drawable, tilePtr->gc, x, y, (unsigned int)width, (unsigned int)height);
    }
}

/**
 * @brief Draw an X11 rectangle batch with transparency-aware batching.
 *
 * Ignores None drawables, NULL arrays and nonpositive counts. Masked tiles
 * use Rbc_TileRectangle separately for each rectangle. Opaque tiles use
 * XFillRectangles in request-sized chunks while retaining Tcl_Size offsets.
 * If no rectangle fits the reported request limit, returns without drawing.
 * Borrows array and tile resources; does not refresh the cache.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] drawable Borrowed compatible destination drawable; None causes no drawing.
 * @param[in,out] clientPtr Borrowed live client with usable cached tile resources; shared GC clip state may be
 *   changed and reset during masked drawing.
 * @param[in] rectArr Borrowed array of nRectangles absolute rectangles; NULL causes no drawing.
 * @param[in] nRectangles Rectangle count; nonpositive counts cause no drawing.
 */
void Rbc_TileRectangles(Tk_Window tkwin, Drawable drawable, TileClient *clientPtr, XRectangle rectArr[],
                        Tcl_Size nRectangles) {
    Tile *tilePtr;

    if ((drawable == None) || (rectArr == NULL) || (nRectangles <= 0)) {
        return;
    }
    tilePtr = clientPtr->tilePtr;
    if (tilePtr->mask != None) {
        Tcl_Size i;

        for (i = 0; i < nRectangles; i++) {
            Rbc_TileRectangle(tkwin, drawable, clientPtr, (int)rectArr[i].x, (int)rectArr[i].y, (int)rectArr[i].width,
                              (int)rectArr[i].height);
        }
    } else {
        Display *display;
        Tcl_Size offset;
        int maxRects;

        display = Tk_Display(tkwin);
        maxRects = Rbc_MaxRequestSize(display, sizeof(XRectangle));
        if (maxRects < 1) {
            return;
        }
        offset = 0;
        while (offset < nRectangles) {
            Tcl_Size remaining;
            int chunk;

            remaining = nRectangles - offset;
            chunk = (remaining > (Tcl_Size)maxRects) ? maxRects : (int)remaining;
            XFillRectangles(display, drawable, tilePtr->gc, rectArr + offset, chunk);
            offset += chunk;
        }
    }
}

/**
 * @ingroup tile_native_masks
 * @brief Build an X11 polygon-shaped repeating transparency mask.
 *
 * Uses the supplied inclusive bounding box to allocate a depth-one pixmap.
 * Copies and translates XPoint coordinates, clears the mask with the default
 * GC foreground, then draws a Complex polygon using FillStippled and the
 * translated client origin. Frees temporary points and GC; returns an owned
 * pixmap for Tk_FreePixmap. Requires a valid nonempty point array and bounds;
 * translated coordinates are still stored as XPoint shorts.
 *
 * @param[in] display Borrowed live X display owning the supplied drawable and resources.
 * @param[in] pointArr Borrowed nonempty array of nPoints absolute XPoint coordinates; not modified.
 * @param[in] nPoints Positive number of initialized point entries; caller validates array bounds.
 * @param[in] regionPtr Borrowed inclusive nonempty polygon bounds; translated coordinates must fit XPoint
 *   shorts.
 * @param[in] mask Borrowed valid depth-one repeating stipple; not consumed or modified.
 * @param[in] xOrigin Horizontal tile origin in destination coordinates, translated into mask-local coordinates
 *   here.
 * @param[in] yOrigin Vertical tile origin in destination coordinates, translated into mask-local coordinates
 *   here.
 */
static Pixmap PolygonMask(Display *display, XPoint *pointArr, int nPoints, Region2D *regionPtr, Pixmap mask,
                          int xOrigin, int yOrigin) {
    unsigned int width, height;
    Pixmap bitmap;
    GC gc;
    XPoint *destArr;
    register XPoint *srcPtr, *destPtr, *endPtr;

    width = regionPtr->right - regionPtr->left + 1;
    height = regionPtr->bottom - regionPtr->top + 1;
    bitmap = Tk_GetPixmap(display, DefaultRootWindow(display), width, height, 1);
    destArr = (XPoint *)ckalloc(sizeof(XPoint) * nPoints);
    endPtr = destArr + nPoints;
    srcPtr = pointArr;
    for (destPtr = destArr; destPtr < endPtr; destPtr++) {
        destPtr->x = srcPtr->x - regionPtr->left;
        destPtr->y = srcPtr->y - regionPtr->top;
        srcPtr++;
    }
    gc = XCreateGC(display, bitmap, 0, NULL);
    XFillRectangle(display, bitmap, gc, 0, 0, width, height);
    XSetForeground(display, gc, 0x01);
    XSetFillStyle(display, gc, FillStippled);
    XSetTSOrigin(display, gc, xOrigin - regionPtr->left, yOrigin - regionPtr->top);
    XSetStipple(display, gc, mask);
    XFillPolygon(display, bitmap, gc, destArr, nPoints, Complex, CoordModeOrigin);
    XFreeGC(display, gc);
    ckfree((char *)destArr);
    return bitmap;
}

/**
 * @brief Fill an X11 polygon with an optional repeated transparency mask.
 *
 * Requires a valid drawable, live tile and nonempty point array. Opaque
 * drawing calls XFillPolygon with Complex/CoordModeOrigin. Masked drawing
 * computes inclusive bounds, installs PolygonMask on the shared GC, fills,
 * resets clip mask/origin to None/zero and frees the temporary mask. Unlike
 * rectangle helpers this path does not reject None or empty input itself.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] drawable Borrowed destination drawable compatible with the window/display; no ownership is
 *   transferred.
 * @param[in,out] clientPtr Borrowed live client with usable cached tile resources; shared GC clip state may be
 *   changed and reset during masked drawing.
 * @param[in] pointArr Borrowed nonempty array of nPoints absolute XPoint coordinates; not modified.
 * @param[in] nPoints Positive number of initialized point entries; caller validates array bounds.
 */
void Rbc_TilePolygon(Tk_Window tkwin, Drawable drawable, TileClient *clientPtr, XPoint pointArr[], int nPoints) {
    Tile *tilePtr;
    Display *display;

    display = Tk_Display(tkwin);
    tilePtr = clientPtr->tilePtr;
    if (tilePtr->mask != None) {
        XPoint *pointPtr, *endPtr;
        Region2D region;
        Pixmap mask;

        /* Determine the bounding box of the polygon. */
        pointPtr = pointArr;
        region.left = region.right = pointPtr->x;
        region.top = region.bottom = pointPtr->y;
        endPtr = pointArr + nPoints;
        for (pointPtr = pointArr; pointPtr < endPtr; pointPtr++) {
            if (region.left > pointPtr->x) {
                region.left = pointPtr->x;
            } else if (region.right < pointPtr->x) {
                region.right = pointPtr->x;
            }
            if (region.top > pointPtr->y) {
                region.top = pointPtr->y;
            } else if (region.bottom < pointPtr->y) {
                region.bottom = pointPtr->y;
            }
        }
        mask = PolygonMask(display, pointArr, nPoints, &region, tilePtr->mask, clientPtr->xOrigin, clientPtr->yOrigin);
        XSetClipMask(display, tilePtr->gc, mask);
        XSetClipOrigin(display, tilePtr->gc, region.left, region.top);
        XFillPolygon(display, drawable, tilePtr->gc, pointArr, nPoints, Complex, CoordModeOrigin);
        XSetClipMask(display, tilePtr->gc, None);
        XSetClipOrigin(display, tilePtr->gc, 0, 0);
        Tk_FreePixmap(display, mask);
    } else {
        XFillPolygon(display, drawable, tilePtr->gc, pointArr, nPoints, Complex, CoordModeOrigin);
    }
}
#endif
