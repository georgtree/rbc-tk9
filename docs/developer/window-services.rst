Shared drawing and window services
==================================

These are private Tk support services, not another graph renderer. They are
used by graph components, image code and standalone canvas/window operations.
All handles belong to the owning Tk thread; none of these functions supplies
locking, reference-counted widget lifetime or automatic callback protection.

Graphics-context ownership
--------------------------

``rbcDraw.c`` creates private GCs directly through XCreateGC. Unlike Tk_GetGC,
these do not share a cache entry with other widgets. Use them for mutable dash
state or other settings that cannot be represented by the Tk cache key.

.. list-table::
   :header-rows: 1
   :widths: 35 35 30

   * - Acquisition
     - Ownership
     - Release
   * - Rbc_GetPrivateGC / Rbc_GetPrivateGCFromDrawable
     - Exclusive caller-owned GC
     - Rbc_FreePrivateGC
   * - Tk_GetGC
     - Reference to Tk's shared cache
     - Tk_FreeGC
   * - Tk_3DBorderGC / DefaultGC
     - Borrowed GC
     - No release by borrower

An unrealized Tk window is not forced into existence merely to obtain a GC.
Rbc_GetPrivateGC uses the screen root when depths agree, or a temporary 1x1
pixmap at the window depth otherwise. The pixmap is released before returning
and the GC remains caller-owned. The drawable form requires the caller to
supply a compatible drawable. Neither destructor accepts arbitrary ownership
or makes repeated release safe.

Rbc_SetDashes reads a bounded, zero-terminated byte sequence and changes the
GC dash offset/pattern. It does not choose the line style. Do not use this as
permission to mutate a borrowed shared GC.

Request sizing and relief
-------------------------

Rbc_MaxRequestSize reports an element count, not bytes. It reserves four
32-bit protocol words, converts the remaining limit to bytes and divides by
the caller's element size. It prefers the extended X request limit when
available, falls back to the ordinary limit, returns zero for unusable inputs
and saturates the result at INT_MAX. A caller still needs to account for its
actual request format and split batches. See :doc:`native-rendering`.

Rbc_Draw3DRectangle preserves RBC's historical raised/sunken outer-edge
appearance, then delegates the inset to Tk_Draw3DRectangle. Its X11 and Windows
edge GCs differ. It borrows all drawing resources and establishes no clipping
region. Graph paths should enter through their renderer service where one
exists; standalone canvas callers do not acquire a graph context merely to
use this utility. See :doc:`rendering` for the module boundary.

Native identifiers
------------------

``rbcWindow.c`` distinguishes a Tk client from its window-manager wrapper.
:c:func:`Rbc_GetRealWindowId` can realize the client and returns the parent for a
toplevel. On Windows its result is a raw HWND cast to Window, not Tk's drawable wrapper.
Passing that value to an API expecting a Tk drawable is incorrect. The winop snapshot
path deliberately uses Tk_WindowId for Tk path arguments.

On X11 the helper queries the tree and releases the returned child array.
If the parent lookup fails or returns None, the helper keeps the realized
client window as its fallback. Windows retains its platform-specific wrapper
lookup; the X11 fallback does not change the Windows handle representation.

Window instance associations
----------------------------

The same module stores window-to-payload associations in an interpreter-owned
Tcl hash table using one-word Tk_Window keys. It replaces older reliance on
private Tk instance fields. Setting a value creates the table lazily. Getting
a value borrows it; replacing or deleting an entry never destroys its payload.
Interpreter cleanup releases the table storage only.

The owner must delete its association while the Tk window is still valid.
There is no automatic per-window destroy handler in this service. A missing
entry or an absent table returns NULL from lookup. Deletion is a no-op in both
cases and does not allocate a table. A live Tk window is still required; these
guards do not make a stale Tk_Window valid.

Identifier and table lifetime details
-------------------------------------

Neither platform recursively searches for a decoration window. The helpers
select the immediate parent only for Tk toplevels. Realizing a native client
is distinct from explicitly mapping it; these calls do not request mapping or
wait for a window manager to finish reparenting it. A returned ID is therefore
a current native identity, not a retained resource or a stable wrapper promise.

On X11, :c:func:`rbcWindowHelpers.rbcGetParent` owns only the child-array allocation
returned by XQueryTree. It frees that array before returning the parent ID.
:c:func:`rbcWindowHelpers.GetWindowId` retains the realized client when the parent query
fails. On Windows, :c:func:`rbcWindowsHandles.GetWindowHandle` uses Tk_GetHWND and
GetParent directly; a NULL parent is not replaced by the client. The shared Window
return type does not erase this platform difference. Callers needing Tk drawing
operations must use Tk's own drawable token, not reinterpret a returned HWND as one.

Association storage is attached to the interpreter obtained from the supplied
Tk window. It is not a global path-name registry: keys are Tk_Window pointer
identities, and another interpreter has its own table. A NULL stored payload is
allowed but indistinguishable from a missing entry through the lookup API.
Replacing a payload neither calls its destructor nor informs its former owner.

Deleting the final entry leaves the empty table attached to the interpreter.
:c:func:`rbcWindowHelpers.AssocDataCleanup` eventually deletes hash entries and the
table allocation; it does not visit, release or notify payloads. Owners must remove
associations while their Tk windows are still live, before pointer identities can become
stale or be reused. There is no reference acquisition, weak-reference mechanism or
automatic per-window cleanup in this layer. These requirements also apply when lookup
happens during component teardown or callbacks.

Maintenance checks
------------------

Keep GC acquisition/release pairs explicit, preserve depth selection for
unrealized windows, and review both platform branches when changing native
identifiers. Run ``make test-headers`` after declaration changes. Window
capture regressions belong in the existing ``RBC.winop.snap*.test`` files;
renderer changes also need the appropriate component drawing tests.

``make test-resources`` exercises lookup/deletion before the first setter,
payload replacement, repeated deletion and the X11 parent-query fallback. The
private harness injects parent-query failure only on X11; other tests also run
on Windows. Ordinary suite runs skip it unless RBC_RESOURCE_TEST_LIB is set.

Resource and stroke argument boundaries
---------------------------------------

:c:func:`Rbc_GetPrivateGC` and :c:func:`Rbc_GetPrivateGCFromDrawable` create
uncached, caller-owned GCs. Their value masks select which XGCValues fields
must be initialized. Release these GCs with :c:func:`Rbc_FreePrivateGC`;
a cached Tk GC is a different ownership contract.

:c:func:`Rbc_NativeLineWidth` maps widths at most one to the native zero-width
representation. :c:func:`Rbc_SetDashes` changes dash bytes and offset on a
caller-mutable GC without choosing its line style.
:c:func:`Rbc_MaxRequestSize` returns a record count, using a byte size per record,
not a byte allocation size. :c:func:`Rbc_Draw3DRectangle` takes drawable pixel
coordinates and preserves the native relief border policy.
