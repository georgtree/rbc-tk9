Shared value conversion and numeric policy
===========================================

``rbcConfig.c`` provides scalar and compound value parsers as well as the component-resource helpers described in
:doc:`configuration`. Parsing a value is not the same as committing an option transaction. Most scalar/compound parsers
stage local values, but their output and resource rules differ.

.. list-table::
   :header-rows: 1
   :widths: 24 38 38

   * - Helper family
     - Accepted input
     - Output/error ownership
   * - Pixels
     - Tk screen distance, followed by RBC sign check
     - Integer assigned only on success
   * - Padding
     - One or two nonnegative screen distances
     - Whole record assigned only on success
   * - Shadow
     - Empty, or color with optional nonnegative offset
     - Success transfers one color reference; failure releases staged color
   * - Dashes
     - Exact named pattern or bounded list of integer expressions
     - Whole descriptor committed on success; expressions may execute Tcl
   * - Fill
     - Nonempty prefix of none/no, x, y or both
     - Integer assigned only on success
   * - State
     - Exact normal, active or disabled
     - Integer assigned only on success
   * - String list
     - Tcl list string, NULL/empty allowed
     - Output cleared before parsing; nonempty success owns one block
   * - XY position
     - Optional @x,y screen distances
     - Both values committed together; empty sets sentinel coordinates

Screen units and staged values
------------------------------

:c:func:`Rbc_GetPixels` and :c:func:`Rbc_GetPixelsFromObj` delegate unit conversion and rounding to Tk before applying
``PIXELS_ANY``, ``PIXELS_NONNEGATIVE`` or ``PIXELS_POSITIVE``. ANY skips RBC's extra sign restriction; it does not force
Tk to accept input rejected by its own parser. The helpers do not evaluate Tcl expressions or map graph axes. A
component's own signed-pixel option specification remains a separate concern, including TK_OPTION_NEG_OK compatibility
across Tk versions.

Padding uses one distance for both sides or two in their supplied order. It stages the complete Rbc_Pad, so failure on
the second value keeps the old result. :c:func:`Rbc_GetXY` copies the X substring instead of modifying the caller's
string. Its NULL/empty input returns -SHRT_MAX for both coordinates. Otherwise it parses both Tk distances and only then
commits; these are not graph data coordinates.

A shadow parser acquires a Tk color, defaults the offset to one, and releases the staged color if an explicit offset
fails. Empty input yields no color and zero offset. Success does not free a previously owned shadow already stored in
the destination. The owning transaction must replace/release that resource.

Dashes and callbacks
--------------------

The descriptor contains at most ``RBC_MAX_DASH_VALUES`` values plus a terminator, and an offset. Exact names dash, dot,
dashdot and dashdotdot select fixed byte sequences. NULL/empty input or a singleton zero requests a solid line. Other
entries must evaluate to integers from 1 through 255. Successful parsing starts from a zeroed descriptor, including
offset zero.

List entries use ``Tcl_ExprLongObj``, not a non-evaluating integer conversion. A braced list can therefore contain
expressions whose substitutions execute commands or variable traces during C parsing. Staging newDashes prevents a
partial value commit, but does not preserve a graph, pen, marker, interpreter or borrowed list-element array across
arbitrary callbacks. Callers must audit that lifetime boundary separately; this chapter does not claim those paths are
reentrant merely because option rollback exists.

Applying the descriptor to a GC is another operation, :c:func:`Rbc_SetDashes`; see :doc:`window-services`. Screen and
document backends still own their respective dash encoding and phase conventions.

Strings and matching policies
-----------------------------

Fill prefixes are nonempty and case-sensitive; no remains a compatibility alias for none. State parsing intentionally
accepts only complete exact names. Neither should be silently replaced with the other's matching policy. Directional
fill selection is also distinct from graph area fills in :doc:`fills`.

:c:func:`Rbc_GetStringListFromObj` converts through the object's string representation and ``Tcl_SplitList``. The result
is a single block containing the pointer array and characters, freed once with ckfree. NULL input and empty lists
produce NULL. The destination pointer is cleared before parsing, even on failure. Parse into a temporary pointer before
replacing an existing owned list; passing its sole owning field directly can lose the old allocation. These are
NUL-terminated strings, not a binary-preserving Tcl_Obj list interface.

Numeric compatibility macros
----------------------------

``rbcMath.h`` centralizes platform math configuration and fallbacks. FINITE selects an available platform finite-value
predicate. DEFINED/UNDEFINED only test NaN: infinity is defined, but not finite. A valid data sentinel check is
therefore not necessarily a valid screen-coordinate range check.

ROUND rounds halfway away from zero by adding a signed half before an int cast. It requires a finite adjusted value that
fits int. FMOD computes a legacy remainder using an int-truncated quotient, requiring a nonzero divisor and a finite
quotient representable as int. Neither macro saturates large inputs. ABS, FABS, MIN/MAX and related macros can evaluate
arguments repeatedly; avoid side-effecting arguments. Signed ABS also cannot represent the magnitude of the most
negative integer in the same type.

:c:func:`Rbc_ClampToInt` is the safe saturating boundary for an already-computed Tcl_WideInt. It does not protect
arithmetic that overflowed before the call, round doubles or enforce widget-specific minimum dimensions. Keep these
three steps separate: validate input, compute in an adequate type, then narrow with the destination's policy. See
:doc:`geometry` and :doc:`mapping`.

Maintenance checks
------------------

Use the owning component's existing configuration tests for invalid second values, preserved old state, exact/prefix
matching, empty lists, maximum dash length and resource replacement. Distinguish local parser commit tests from callback
deletion tests and option-transaction tests. Documentation-only changes do not alter any accepted values or runtime
error behavior.

Legacy color-pair adapter
-------------------------

``rbcGrMisc.c`` still defines the old ``Tk_CustomOption`` color-pair adapter, ``rbcColorPairOption``. No current in-tree
C option table references that adapter. It should not be confused with the maintained Tk option transactions used by the
graph components.

Its private :c:func:`GetColorPair` acquires foreground and background independently, then assigns the output pair only
after both succeed. If background lookup fails, it releases an acquired foreground reference while retaining the
original Tk diagnostic and leaving the output pair unchanged. NULL and default-color sentinels are never passed to
``Tk_FreeColor``. Successful parsing transfers both references to the pair, even when both names resolve to the same
cached color. The private resource harness checks balanced acquisition/release on failure and success, including empty
and default colors, through ``make test-resources``.

Legacy adapter replacement and printing
---------------------------------------

The old string callbacks receive a record and byte offset. Those fields must already contain valid initialized
values. :c:func:`StringToDistance` forwards the sign policy encoded in client data and commits only a converted integer.
:c:func:`StringToShadow` stages a color and optional offset, then releases the old color
itself. :c:func:`StringToColorPair` similarly stages the complete pair before releasing the old fields. These
replacement callbacks are distinct from :c:func:`Rbc_GetShadowFromObj` and :c:func:`GetColorPair`, whose successful
outputs transfer resources without disposing of the previous destination.

NULL/empty shadow input clears both fields. A nonempty shadow list requires one or two elements; its default offset is
one. The color-pair list accepts zero through two names, and a single name sets the foreground only. A nonzero
color-pair client-data policy permits nonempty prefixes of ``defcolor``. That sentinel is a symbolic default, not a Tk
color reference.

Printer ownership is equally specific:

.. list-table::
   :header-rows: 1
   :widths: 25 45 30

   * - Printer
     - Returned representation
     - Release policy
   * - DistanceToString
     - Allocated decimal integer
     - Sets Tcl_Free
   * - ShadowToString
     - Static empty string when disabled; otherwise allocated color/offset list
     - Leaves policy untouched for empty; sets Tcl_Free for allocated list
   * - ColorPairToString
     - Allocated two-element list, even when both colors are empty
     - Sets Tcl_Free

The Tk caller supplies the initial free policy for static printer results. Do not call these callbacks directly with an
uninitialized release callback. Color-pair printing deliberately transfers a heap-backed DString rather than freeing
it; an inline DString is copied first. Adding an unconditional ``Tcl_DStringFree`` at the end would invalidate the
returned heap buffer. :c:func:`NameOfColor` itself returns borrowed text, which the list builder copies.

The private parser reference also records the smaller matching boundaries: :c:func:`IsFillPrefix` checks byte lengths
and rejects empty prefixes, :c:func:`DoCheck` applies only a sign restriction, and :c:func:`GetState` requires exact
names. :c:func:`GetStringList` clears its output before parsing, so its failure behavior must not be mistaken for the
staged replacement behavior of these adapters.

Output parameters are not interchangeable
-----------------------------------------

Parameter directions describe where data is written, not whether a previous resource is
released. :c:func:`Rbc_GetShadowFromObj` transfers a new color reference only on success; the caller separately owns
replacement of any old shadow. :c:func:`Rbc_GetStringListFromObj` instead clears its output before parsing, including
failure, so that output must not be the sole untracked owner of an existing allocation.

:c:func:`Rbc_GetDashesFromObj` stages the descriptor but can evaluate Tcl while converting entries. The caller must keep
both the input objects and destination storage valid across those callbacks. A successful scalar parse alone does not
prove that a widget or component still exists; see :doc:`configuration` for the surrounding transaction and callback
checks.
