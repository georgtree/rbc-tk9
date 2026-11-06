Temporary images and private Tk adapters
========================================

The tail of ``rbcImage.c`` bridges gaps in the image API used by older RBC components. It contains copied
image-instance, image-master and photo-instance structure layouts, plus temporary-photo creation and deletion helpers.
These are private RBC facilities. Their ``Tk_`` names do not make them public Tk APIs or Tk stub entries.

Private-layout access
---------------------

:c:func:`Tk_ImageIsDeleted` inspects the instance's master and type pointers. It can recognize a deleted image while the
caller still owns a retained instance; it cannot make a ``NULL`` or dangling instance pointer
safe. :c:func:`Tk_ImageGetMaster` returns a borrowed pointer. :c:func:`Tk_ImageGetType` requires that master to exist
and can return ``NULL`` after deletion.

:c:func:`Tk_ImageGetPhotoPixmap` and :c:func:`Tk_ImageGetPhotoGC` require a live type before checking whether it is a
photo. They expose Tk-owned display resources rather than independent pixel data. Callers must not free these resources
or assume that they remain stable across image changes. The GC is not caller-owned scratch state. A different live image
type yields ``None`` or ``NULL`` respectively.

:c:func:`Rbc_NameOfImage` passes the borrowed master to ``Tk_NameOfImage``. Its result is a borrowed name, not a
retained Tcl object. Copy the name before any operation that could invalidate its owner. See :doc:`images-tiles` for
independent photo snapshots, which use photo blocks instead of these display-resource accessors.

Tk compatibility boundary
-------------------------

The copied structures are an ABI assumption: the fields actually read must match the linked Tk version and
platform. Merely compiling against Tk headers or passing a header-order test does not establish that runtime layout
contract. When changing supported Tk versions, compare against that version's ``generic/tkImage.c`` and
``generic/tkImgPhoto.h``, including field types and alignment. A field appended beyond the accessed prefix differs from
one inserted before an accessed field; avoid blanket conclusions from structure names alone.

Keep these casts localized. New components should prefer supported photo-block and image APIs where those satisfy the
requirement, rather than duplicating private structures or adding more consumers of Tk-owned pixmaps and GCs.

Two resources in a temporary photo
----------------------------------

A temporary photo has a named image in Tk's image registry and an acquired ``Tk_Image`` instance used by the
component. Creation evaluates ``image create photo`` and then calls ``Tk_GetImage`` for the returned name. It retains
the interpreter result object while consuming that name, since instance acquisition may replace the interpreter result
on failure.

On success, the caller owns the instance and is responsible for removing the named image as well. The installed
:c:func:`rbcImageStorage.TempImageChangedProc` does nothing; ``clientData`` is passed through without ownership
transfer. Despite its old comment, the callback does not currently take action when the image is deleted. Temporary
photos are ordinary named Tk images, not hidden storage immune to script-side deletion or modification.

Image markers use temporary photos for scaled display regions and release them when returning to natural size or
changing their source. The EPS canvas item also uses them. Source snapshots, temporary photos and native pixmaps have
separate release mechanisms; do not substitute one for another.

Deletion and failure ownership
------------------------------

:c:func:`Rbc_DestroyTemporaryImage` copies the name of a live image, deletes it with ``Tk_DeleteImage`` and releases the
retained instance with ``Tk_FreeImage``. An already-deleted image still has its instance released. ``NULL`` is a
successful no-op. Cleanup always consumes a non-``NULL`` token and returns ``TCL_OK``; callers must not retry with that
token.

Deletion uses Tk's C API rather than evaluating the replaceable Tcl ``image`` command. A failing wrapper or a renamed
command therefore cannot block private resource release. The helper preserves the interpreter and saves/restores its
state around cleanup, retaining the caller's result and error information.

Creation retains its result-name object through acquisition and rollback. If ``Tk_GetImage`` fails after successful
photo creation, the helper deletes the named image through Tk's C API while preserving the acquisition error. Scaled
image markers also release their temporary color buffer on this failure path.

Callbacks and interpreter state
-------------------------------

Creation still evaluates ``image create photo`` with ordinary Tcl evaluation flags. Command wrappers, namespace command
resolution and execution traces can run arbitrary Tcl. Retaining the result name protects that object only; it does not
preserve the component or its window. Enclosing operations remain responsible for lifetime protection and callback
validity.

C-level deletion does not execute the Tcl ``image`` command, but Tk can notify other clients attached to the
image. Callers detach their temporary-image field before cleanup so a reentrant teardown cannot release the same
instance twice. Detaching a field does not itself preserve the enclosing component; retain the broader lifetime
discipline described in :doc:`graph-lifecycle` and :doc:`markers`.

Private cleanup tests
---------------------

Run ``make test-image`` with a Tk display. The private harness includes the image implementation to inject acquisition
failure and count instance releases without adding production commands or installed APIs. It checks rollback,
original-error retention, ordinary deletion, a failing or renamed Tcl image command, external image deletion and NULL
cleanup. Normal suite runs skip these tests unless ``RBC_IMAGE_TEST_LIB`` identifies the harness library.

Maintenance checks
------------------

Cover normal create/delete cycles, scaled markers returning to natural size, external image deletion, command wrappers
that fail creation or deletion, instance-acquisition failure and owner destruction during evaluation. Observe both image
names and retained instances: removing a registry name alone does not prove the client reference was
released. Private-layout checks need the supported Tk versions and both native platforms; documentation generation
cannot validate that ABI boundary.

Image region and notification arguments
---------------------------------------

:c:func:`rbcImageStorage.Rbc_SetRegion` writes inclusive integer endpoints: ``right = x + width - 1`` and ``bottom = y +
height - 1``. It checks endpoint overflow but does not normalize nonpositive sizes. Its returned pointer is the same
caller-owned record.

:c:func:`rbcImageStorage.TempImageChangedProc` intentionally ignores every Tk notification argument. It neither
dereferences client data nor schedules redraw; temporary-image owners manage updates themselves.
