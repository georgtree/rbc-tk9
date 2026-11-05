Image and tile C reference
==========================

These interfaces are private. See :doc:`images-tiles` for resource lifetimes.

.. doxygenfunction:: Rbc_GetTile

.. doxygenfunction:: Rbc_FreeTile

.. doxygenfunction:: Rbc_SetTileChangedProc

.. doxygenfunction:: Rbc_PixmapOfTile

.. doxygenfunction:: Rbc_ImageOfTile

.. doxygenfunction:: Rbc_GetTilePhoto

.. doxygenfunction:: Rbc_CreateColorImage

.. doxygengroup:: image_storage_release
   :members:
   :content-only:

.. doxygenfunction:: Rbc_PhotoToColorImage

.. doxygenfunction:: Rbc_PhotoRegionToColorImage

.. doxygenfunction:: Rbc_ColorImageToPhoto

Tile queries and origins
------------------------

.. doxygenfunction:: Rbc_NameOfTile

.. doxygenfunction:: Rbc_SizeOfTile

.. doxygenfunction:: Rbc_SetTileOrigin

.. doxygenfunction:: Rbc_SetTSOrigin

Tile lifecycle implementation
-----------------------------

.. c:namespace:: rbcTileLifecycle

.. doxygengroup:: tile_lifecycle
   :members:
   :content-only:

.. doxygenstruct:: TileInterpData
   :members:

.. doxygenstruct:: Tile
   :members:

.. doxygenstruct:: Rbc_TileClientStruct
   :members:

.. doxygenstruct:: TileKey
   :members:

.. c:namespace:: NULL

Native tiled shapes
-------------------

.. doxygenfunction:: Rbc_TileRectangle

.. doxygenfunction:: Rbc_TileRectangles

.. doxygenfunction:: Rbc_TilePolygon

.. c:namespace:: rbcTileMasks

.. doxygengroup:: tile_native_masks
   :members:
   :content-only:

.. c:namespace:: NULL

The mask helper reference above describes X11. Windows tiled shapes and
:c:func:`rbcWindowsTiles.TileRegion` are extracted separately in
:doc:`native-windows-reference`; see :doc:`images-tiles` for origin and DC batching
rules.

Storage and coordinate implementation
--------------------------------------

.. c:namespace:: rbcImageStorage

.. doxygengroup:: image_storage_internals
   :members:

.. c:namespace-pop::

Drawable snapshot to photo
--------------------------

.. doxygenfunction:: Rbc_SnapPhoto
