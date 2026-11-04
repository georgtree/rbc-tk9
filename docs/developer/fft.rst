FFT workspace and output ownership
==================================

``rbcVecFft.c`` implements radix-2 transforms and the frequency/window generators.
It uses its own zero-based Cooley-Tukey implementation, with no external FFT
library or BLT FFT source. The handlers are private instance operations declared
in ``rbcVectorInt.h``; see :doc:`fft-reference`.

Dispatch and validation
-----------------------

The vector instance dispatcher validates argument counts and resets the source's
selected range to its full length. It allows ``fft`` and ``ifft`` on real or
complex sources, including read-only inputs. The transform handler resolves
existing destinations, checks their types and checks every destination's
writability before allocating numerical storage.

A single destination must be complex. Split ``-real`` and ``-imag`` destinations
must be distinct real vectors. ``-imaginput`` requires two equally sized real
inputs. Lookups use the caller's namespace and reject selections that do not
cover the whole vector. Nothing creates a destination or changes its type.
Input/output aliasing is supported: all input values are copied before any
output storage is installed.

``fftfreq`` and ``window`` write the instance vector itself. Their real-only and
writable checks belong to the dispatcher, not the handlers. Direct internal
callers must establish those preconditions; calling the handlers as if they
were self-contained public C APIs would bypass them.

Transform length and normalization
----------------------------------

Input must be nonempty. By default its length must already be a power of two.
``-length auto`` rounds up with an overflow check; an explicit length must be a
positive power of two at least as large as the input. Extra values are zeros.
Padding is explicit and never truncates data.

:c:func:`FftTransform` first applies a bit-reversal permutation, then iterative
butterflies of widths 2 through N. The last-width check prevents integer
overflow when doubling the stage width. A length-one transform returns without
using a roots table. The kernel assumes valid power-of-two storage and performs
no allocation or Tcl callbacks.

Forward roots use exp(-2*pi*i*k/N); inverse roots use the positive sign.
Forward output is unscaled. The caller divides inverse input by N before the
butterflies, reducing avoidable intermediate overflow. The complete output is
in ordinary FFT order, with DC first and no shifting or one-sided folding.
There is no amplitude, window-gain or PSD normalization in this module.

Workspace and commit sequence
-----------------------------

:c:func:`FftAllocate` checks positive counts and byte-size overflow before attempted
allocation. A transform allocates N complex values and, except for N=1, N/2
complex roots. Split output additionally allocates two N-double arrays.
Roots are released before those split arrays are allocated. Workspace is
linear in N and is not cached between calls.

The handler checks finite input components while copying, transforms the
scratch data, then checks every output component. For split output it extracts
both complete real arrays before committing either one. Allocation, validation
or arithmetic failure releases temporary storage and leaves destination data
unchanged.

:c:func:`FftInstall` saves the old pointer and release policy, installs the new
``TCL_DYNAMIC`` allocation, sets length and capacity to N and resets the selected
range. It invalidates extrema and their indices and marks the range dirty.
It does not allocate, release the old storage, flush an array cache or notify
clients. Vector identity, numeric type, index offset and observer connections
are retained.

For split output, both installs finish before any old buffer is released.
Old static buffers are left to their owners, dynamic buffers are freed, and
custom release procedures are invoked according to the saved ownership policy.
The handler then flushes enabled mapped-array caches and updates clients for
each destination. Returned names are copied into Tcl objects before callbacks;
they describe the destination names at commit time, even if a callback renames
one. See :doc:`vectors` for storage contracts and notification policies.

Callback lifetime and reentry
-----------------------------

:c:func:`FftCommit` preserves every destination record and the interpreter before
releasing old storage. It installs all output buffers first, then releases each
saved old buffer exactly once, even if an earlier release callback deletes an
output. Destruction frees the new storage normally; preservation retains only
the record needed to test its destruction state.

Destroyed destinations are skipped before cache flushing and checked again
before client notification. Interpreter deletion stops further updates.
:c:func:`Rbc_VectorFlushCache` also preserves its record and copies the mapped name,
then rechecks destruction and the current mapping after user unset/write traces.
It does not restore an old mapping when a callback remaps the vector.

Callbacks can destroy themselves or another destination, recreate the same
command name, rename vectors or run a nested transform. The outer commit never
resolves names again, so a replacement is not mistaken for the original output.
Nested changes are not rolled back. Surviving outputs receive the outer update
against their current data. Callback result changes do not replace the saved
command result; returned names describe the original commit, even if those
commands no longer exist afterward. The generators use this same commit helper.

Frequency and window generators
-------------------------------

``fftfreq`` accepts any positive N independently of the transform's radix-2
restriction. Full output uses bins 0 through floor((N-1)/2), then negative bins;
for even N the Nyquist bin is negative. One-sided output uses floor(N/2)+1
nonnegative bins and a positive Nyquist bin when N is even. Each frequency is
computed as ``(bin / N) / delta`` to avoid forming an overflowing product
``N * delta`` or reciprocal ``1 / delta``. Delta must be finite and positive,
and every generated frequency is checked before installation.

``window`` supports rectangular, Hann, Hamming, Blackman and Bartlett windows.
Periodic windows use denominator N; symmetric windows use N-1. Length one
always produces one, avoiding a zero denominator. Folding the index around the
centre gives matching symmetric coefficients; sine-squared forms reduce
cancellation near endpoints. Coefficients are unscaled and are not multiplied
into another vector by this operation.

Both generators stage complete real arrays, then transfer ownership through
:c:func:`FftStoreReal` and the same installation helper. They preserve identity and
type while replacing length, capacity and storage. Source expressions can
apply windows or further scaling separately; see :doc:`expressions`.

Internal helper boundaries
--------------------------

The generated :doc:`fft-reference` includes the allocation, lookup, transform
and commit helpers as well as the command handlers. They form three distinct
layers:

.. list-table::
   :header-rows: 1
   :widths: 25 40 35

   * - Layer
     - Preconditions supplied by caller
     - Failure/ownership boundary
   * - :c:func:`FftAllocate` / :c:func:`FftVector`
     - Nonzero element size; live interpreter data
     - Allocation is owned by caller; lookup returns a borrowed record and may
       change its selected range
   * - :c:func:`FftTransform`
     - Positive power-of-two length, complete roots, pre-scaled inverse input
     - Overwrites scratch data; caller checks all components for finiteness
   * - :c:func:`FftInstall` / :c:func:`FftCommit`
     - One or two distinct, writable, correctly typed destinations; complete
       Tcl-allocated buffers
     - Transfers ownership and commits before callbacks; no numerical rollback

:c:func:`FftCommit` has fixed storage for two destinations. It is not a general
multi-vector transaction API. :c:func:`FftStoreReal` is its single-real-output adapter,
not an additional validation layer. New callers must establish the same
preconditions as the current command handlers rather than relying on commit
to discover invalid lengths, destination aliases or read-only vectors.

Count and ownership conventions
-------------------------------

FFT lengths and capacities count **typed values**, not bytes.
:c:func:`FftAllocate` alone takes a separate byte stride. The transform
kernel consumes N complex values and N/2 roots, with no roots needed for
N=1. It does not normalize inverse output itself: inverse input is scaled
before the call.

The pointer returned by :c:func:`FftVector` is borrowed. By contrast,
:c:func:`FftAllocate` returns caller-owned storage. :c:func:`FftInstall`
transfers an independent completed allocation to the vector and returns
its detached old pointer and release policy. :c:func:`FftCommit` consumes
all supplied output allocations before running release or notification
callbacks. The buffer arrays themselves remain caller-owned; transferring
their entries does not clear those entries. Callers must not free them after
commit, even if a callback destroys an output.

:c:func:`FftStoreReal` supplies the same ownership transfer for a single
real generator. None of these commit helpers validates its caller's
preconditions or provides rollback after callbacks. The parameter contracts
in :doc:`fft-reference` distinguish borrowed records, consumed buffers, and
old-storage outputs explicitly.
