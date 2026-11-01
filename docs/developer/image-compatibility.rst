Temporary images and private Tk adapters
========================================

The tail of ``rbcImage.c`` bridges gaps in the image API used by older RBC
components. It contains copied image-instance, image-master and photo-instance
structure layouts, plus temporary-photo creation and deletion helpers.
These are private RBC facilities. Their ``Tk_`` names do not make them public
Tk APIs or Tk stub entries.

Private-layout access
---------------------

``Tk_ImageIsDeleted`` inspects the instance's master and type pointers. It can
recognize a deleted image while the caller still owns a retained instance;
it cannot make a NULL or dangling instance pointer safe. ``Tk_ImageGetMaster``
returns a borrowed pointer. ``Tk_ImageGetType`` requires that master to exist
and can return NULL after deletion.

``Tk_ImageGetPhotoPixmap`` and ``Tk_ImageGetPhotoGC`` require a live type before
checking whether it is a photo. They expose Tk-owned display resources rather
than independent pixel data. Callers must not free these resources or assume
that they remain stable across image changes. The GC is not caller-owned
scratch state. A different live image type yields ``None`` or NULL respectively.

``Rbc_NameOfImage`` passes the borrowed master to ``Tk_NameOfImage``. Its result
is a borrowed name, not a retained Tcl object. Copy the name before any operation
that could invalidate its owner. See :doc:`images-tiles` for independent photo
snapshots, which use photo blocks instead of these display-resource accessors.

Tk compatibility boundary
-------------------------

The copied structures are an ABI assumption: the fields actually read must
match the linked Tk version and platform. Merely compiling against Tk headers
or passing a header-order test does not establish that runtime layout contract.
When changing supported Tk versions, compare against that version's
``generic/tkImage.c`` and ``generic/tkImgPhoto.h``, including field types and
alignment. A field appended beyond the accessed prefix differs from one inserted
before an accessed field; avoid blanket conclusions from structure names alone.

Keep these casts localized. New components should prefer supported photo-block
and image APIs where those satisfy the requirement, rather than duplicating
private structures or adding more consumers of Tk-owned pixmaps and GCs.

Two resources in a temporary photo
----------------------------------

A temporary photo has a named image in Tk's image registry and an acquired
``Tk_Image`` instance used by the component. Creation evaluates
``image create photo`` and then calls ``Tk_GetImage`` for the returned name.
It retains the interpreter result object while consuming that name, since
instance acquisition may replace the interpreter result on failure.

On success, the caller owns the instance and is responsible for removing the
named image as well. The installed ``TempImageChangedProc`` does nothing;
``clientData`` is passed through without ownership transfer. Despite its old
comment, the callback does not currently take action when the image is deleted.
Temporary photos are ordinary named Tk images, not hidden storage immune to
script-side deletion or modification.

Image markers use temporary photos for scaled display regions and release them
when returning to natural size or changing their source. The EPS canvas item
also uses them. Source snapshots, temporary photos and native pixmaps have
separate release mechanisms; do not substitute one for another.

Deletion and failure ownership
------------------------------

``Rbc_DestroyTemporaryImage`` copies the current name into a Tcl object,
evaluates ``image delete name``, and calls ``Tk_FreeImage`` only after command
success. NULL input is a successful no-op. On error the token remains allocated
and caller-owned; a caller that clears its field unconditionally can lose that
reference. A later retry also requires the image/master state to remain valid.

Creation has a corresponding incomplete-cleanup path: if Tcl image creation
succeeds but ``Tk_GetImage`` fails, the function returns NULL without deleting
the newly created named image. These are current failure-path limitations, not
ownership patterns to copy into new code. This documentation does not alter
their runtime behavior.

Callbacks and interpreter state
-------------------------------

Both helpers evaluate the command name ``image`` with ordinary Tcl evaluation
flags. They do not bypass command wrappers, namespace command resolution or
execution traces. Such evaluation can run arbitrary Tcl and invalidate the
component, window or interpreter used by the caller. Retaining the result name
protects that object only; it does not retain the owning component.

The helpers do not save and restore the interpreter result, preserve the owner,
or revalidate it after evaluation. Enclosing operations must provide appropriate
lifetime protection and avoid assuming their graph or marker survives a callback.
See :doc:`graph-lifecycle` and :doc:`markers` for the broader ownership rules.
Cleanup callers must also decide how an image-command error affects their own
result and resource release, rather than silently treating every call as success.

Maintenance checks
------------------

Cover normal create/delete cycles, scaled markers returning to natural size,
external image deletion, command wrappers that fail creation or deletion,
instance-acquisition failure and owner destruction during evaluation. Observe
both image names and retained instances: removing a registry name alone does
not prove the client reference was released. Private-layout checks need the
supported Tk versions and both native platforms; documentation generation
cannot validate that ABI boundary.
