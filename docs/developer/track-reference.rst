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

Widget integration
------------------

.. doxygenfunction:: Rbc_ConfigureTrackGraph

.. doxygenfunction:: Rbc_TrackOp

.. doxygenfunction:: Rbc_DrawTrackSeparators

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in
the reference does not make private layouts part of the installed C API.

.. doxygenstruct:: TrackDefaults
   :members:

.. doxygenstruct:: RbcTrack
   :members:
