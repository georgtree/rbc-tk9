Track ownership C reference
===========================

Track records and private services are declared in ``rbcGrTrack.h``. Static
configuration helpers live in ``rbcGrTrack.c``. See :doc:`tracks` for the
lifecycle, :doc:`mapping-reference` for viewport scopes and
:doc:`scroll-reference` for scroll notification.

.. doxygenfunction:: Rbc_FindTrack

.. doxygenfunction:: Rbc_DestroyTracks

.. doxygenfunction:: Rbc_CreateTrackComponentArgs

.. doxygenfunction:: Rbc_FreeTrackComponentArgs

.. doxygengroup:: track_ownership
   :members:
