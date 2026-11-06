/**
 * @file rbcVecFft.c
 * @brief FFT workspace and commit helpers.
 *
 *      Radix-2 Fourier transforms for real and complex vectors.
 *      This is a zero-based implementation of the Cooley-Tukey algorithm;
 *      no BLT FFT source or external FFT library is used.
 *
 * See "license.terms" for information on usage and redistribution.
 */

/** @defgroup fft_internals FFT workspace and commit helpers
 * Private implementation helpers; not installed C API entry points.
 */
#include "rbcVectorInt.h"
#include <string.h>

/**
 * @brief Allocate checked FFT or generator workspace.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] count Positive number of elements; zero and negative counts are rejected.
 * @param[in] elementSize Nonzero byte width of one element, guaranteed by the caller.
 * @return Owned uninitialized Tcl allocation to release with ckfree, or NULL on failure.
 *
 * @ingroup fft_internals
 *
 * Requires a nonzero elementSize and a positive count. Rejects byte-count
 * overflow before Tcl_AttemptAlloc; reports allocation/size errors in interp.
 * Returns owned storage to release with ckfree, or NULL without changing vectors.
 * No callbacks run.
 */
static void *FftAllocate(Tcl_Interp *interp, Tcl_Size count, size_t elementSize) {
    void *data;

    if ((count <= 0) || ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / elementSize))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT allocation size is too large", -1));
        return NULL;
    }
    data = Tcl_AttemptAlloc((size_t)count * elementSize);
    if (data == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate FFT storage", -1));
    }
    return data;
}

/**
 * @brief Transform a power-of-two complex array in place.
 * @param[in,out] data Array of length complex values, overwritten by the transform.
 * @param[in] roots Read-only length/2 twiddle factors with the transform's sign; NULL is allowed for length one.
 * @param[in] length Positive power-of-two count, already validated by the caller.
 *
 * @ingroup fft_internals
 *
 * The caller supplies a nonempty power-of-two length and length/2 roots
 * exp(sign*2*pi*i*k/length), with negative sign for forward transformation.
 * Roots may be NULL for length one. Inverse normalization belongs to the caller:
 * scale input by 1/length before this kernel. Bit reversal precedes iterative
 * butterflies; output is unshifted. No allocation, validation or callbacks occur.
 * Arithmetic can become non-finite; the caller must validate the complete result.
 */
static void FftTransform(Rbc_Complex *data, const Rbc_Complex *roots, Tcl_Size length) {
    Tcl_Size i, j, width;

    j = 0;
    for (i = 1; i < length; i++) {
        Tcl_Size bit = length >> 1;

        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            Rbc_Complex tmp = data[i];
            data[i] = data[j];
            data[j] = tmp;
        }
    }
    if (length == 1) {
        return;
    }
    for (width = 2;; width *= 2) {
        Tcl_Size half = width / 2;
        Tcl_Size step = length / width;
        Tcl_Size base;

        for (base = 0; base < length; base += width) {
            for (j = 0; j < half; j++) {
                Rbc_Complex a = data[base + j];
                Rbc_Complex b = data[base + j + half];
                Rbc_Complex w = roots[j * step];
                double real = w.real * b.real - w.imag * b.imag;
                double imag = w.real * b.imag + w.imag * b.real;

                data[base + j].real = a.real + real;
                data[base + j].imag = a.imag + imag;
                data[base + j + half].real = a.real - real;
                data[base + j + half].imag = a.imag - imag;
            }
        }
        if (width == length) {
            break; /* Do not overflow when doubling the final width. */
        }
    }
}

/**
 * @brief Resolve an existing whole-vector operand.
 * @param[in] dataPtr Live owning registry and interpreter used for range-aware lookup.
 * @param[in] nameObj Borrowed vector-reference object, valid throughout lookup.
 * @param[out] vPtrPtr Required borrowed-record output, unchanged on failure; no preservation reference is
 *     acquired.
 * @return TCL_OK for a complete-vector selection, otherwise TCL_ERROR.
 *
 * @ingroup fft_internals
 *
 * Uses the caller namespace through Rbc_VectorLookupName and rejects a selected
 * range unless it spans the complete vector. Lookup can change the vector's
 * selected range. On TCL_OK, writes a borrowed record to vPtrPtr; on TCL_ERROR,
 * leaves that output unassigned and sets the interpreter result. Does not create,
 * resize, preserve or test the numeric type/writability of the vector.
 */
static int FftVector(VectorInterpData *dataPtr, Tcl_Obj *nameObj, VectorObject **vPtrPtr) {
    VectorObject *vPtr;

    if (Rbc_VectorLookupName(dataPtr, Tcl_GetString(nameObj), &vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((vPtr->first != 0) || (vPtr->last != vPtr->length - 1)) {
        Tcl_SetObjResult(dataPtr->interp, Tcl_NewStringObj("FFT requires whole vector names", -1));
        return TCL_ERROR;
    }
    *vPtrPtr = vPtr;
    return TCL_OK;
}

/**
 * @brief Install completed storage without crossing a callback boundary.
 * @param[in,out] vPtr Live writable destination with numeric type matching data.
 * @param[in] data Owned independent Tcl-allocated buffer transferred to vPtr; must not alias its old storage.
 * @param[in] length Nonnegative used count and capacity in typed values.
 * @param[out] oldDataPtr Required output receiving detached old storage; caller releases it according to
 *     oldFreeProcPtr.
 * @param[out] oldFreeProcPtr Required output receiving the detached storage's release policy.
 *
 * @ingroup fft_internals
 *
 * Requires a writable destination of the correct type and an owned Tcl-allocated
 * buffer with length values. Returns old storage and its release policy through
 * oldDataPtr/oldFreeProcPtr; the caller must release that storage later. Transfers
 * the new buffer to the vector as TCL_DYNAMIC, resets length/capacity/selection,
 * and invalidates extrema. Retains identity, type, offset and observer bindings.
 * Does not flush caches, notify clients, allocate, or release old storage.
 */
static void FftInstall(VectorObject *vPtr, void *data, Tcl_Size length,
                       void **oldDataPtr, Tcl_FreeProc **oldFreeProcPtr) {
    *oldDataPtr = vPtr->data.raw;
    *oldFreeProcPtr = vPtr->freeProc;
    vPtr->data.raw = data;
    vPtr->freeProc = TCL_DYNAMIC;
    vPtr->length = vPtr->size = length;
    vPtr->first = 0;
    vPtr->last = length - 1;
    vPtr->min = vPtr->max = rbcNaN;
    vPtr->minIndex = vPtr->maxIndex = -1;
    vPtr->notifyFlags |= UPDATE_RANGE;
}

/**
 * @brief Commit one complex or one/two real output buffers.
 * @param[in] interp Owning interpreter shared by the destinations; see @ref rbc_interp_argument.
 * @param[in] dest Array of count distinct live writable destinations, validated before entry.
 * @param[in] data Array of count independent completed Tcl allocations, all consumed during installation.
 * @param[in] count Exactly one or two outputs, matching the fixed local bookkeeping arrays.
 * @param[in] length Common used count and capacity in values for every output buffer.
 * @return TCL_OK with name snapshots in the interpreter result; callback changes are not rolled back.
 *
 * @ingroup fft_internals
 *
 * count must be one or two; destinations must be distinct, alive, writable and
 * compatible with the supplied buffers. Takes ownership of every buffer and
 * installs all outputs before releasing any old allocation. Preserves destination
 * records and interp across custom free procedures, mapped-array traces and
 * notifications. Skips destroyed records and stops updates after interpreter
 * deletion; it never resolves destination names again after a callback.
 *
 * Returns TCL_OK with the committed qualified name, or a two-name list, saved
 * before callbacks. Callback mutations are not rolled back; a returned name can
 * have been renamed or destroyed. Restores interpreter state around callbacks.
 * There is no fallible numerical work after installation.
 */
static int FftCommit(Tcl_Interp *interp, VectorObject **dest, void **data, int count, Tcl_Size length) {
    void *oldData[2];
    Tcl_FreeProc *oldFree[2];
    Tcl_Obj *result = Tcl_NewListObj(0, NULL);
    Tcl_InterpState state;
    int k;

    Tcl_Preserve(interp);
    Tcl_IncrRefCount(result);
    for (k = 0; k < count; k++) {
        Tcl_Preserve(dest[k]);
        Tcl_ListObjAppendElement(NULL, result, Tcl_NewStringObj(dest[k]->name, -1));
        FftInstall(dest[k], data[k], length, &oldData[k], &oldFree[k]);
    }
    state = Tcl_SaveInterpState(interp, TCL_OK);
    for (k = 0; k < count; k++) {
        if ((oldData[k] != NULL) && (oldFree[k] != TCL_STATIC)) {
            if (oldFree[k] == TCL_DYNAMIC) {
                ckfree(oldData[k]);
            } else {
                oldFree[k](oldData[k]);
            }
        }
    }
    for (k = 0; k < count; k++) {
        if (Tcl_InterpDeleted(interp)) {
            break;
        }
        if (dest[k]->destroying) {
            continue;
        }
        if (dest[k]->flush) {
            Rbc_VectorFlushCache(dest[k]);
        }
        if (!Tcl_InterpDeleted(interp) && !dest[k]->destroying) {
            Rbc_VectorUpdateClients(dest[k]);
        }
    }
    Tcl_RestoreInterpState(interp, state);
    if (count == 1) {
        Tcl_Obj *name;
        Tcl_ListObjIndex(NULL, result, 0, &name);
        Tcl_SetObjResult(interp, name);
    } else {
        Tcl_SetObjResult(interp, result);
    }
    Tcl_DecrRefCount(result);
    for (k = 0; k < count; k++) {
        Tcl_Release(dest[k]);
    }
    Tcl_Release(interp);
    return TCL_OK;
}

/* Operation contract and dispatcher preconditions are in rbcVectorInt.h. */
int Rbc_VectorFftOp(VectorObject *srcPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *const options[] = {"-real", "-imag", "-imaginput", "-length", NULL};
    Tcl_Obj *optionValues[4] = {NULL, NULL, NULL, NULL};
    Tcl_Obj *destName = NULL;
    VectorObject *dest[2] = {NULL, NULL};
    VectorObject *imagPtr = NULL;
    Rbc_Complex *data = NULL, *roots = NULL;
    double *realData = NULL, *imagData = NULL;
    void *output[2];
    Tcl_Size length, i;
    int inverse, count, k, endOptions = FALSE;

    inverse = (Tcl_GetString(objv[1])[0] == 'i');
    for (i = 2; i < objc; i++) {
        const char *arg = Tcl_GetString(objv[i]);
        int option;

        if (!endOptions && (strcmp(arg, "--") == 0)) {
            endOptions = TRUE;
        } else if (!endOptions && (arg[0] == '-')) {
            if (Tcl_GetIndexFromObj(interp, objv[i], options, "option", TCL_EXACT, &option) != TCL_OK) {
                return TCL_ERROR;
            }
            if (optionValues[option] != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("duplicate option \"%s\"", arg));
                return TCL_ERROR;
            }
            if (++i == objc) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing value for \"%s\"", arg));
                return TCL_ERROR;
            }
            optionValues[option] = objv[i];
        } else if (destName == NULL) {
            destName = objv[i];
        } else {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT accepts only one complex destination", -1));
            return TCL_ERROR;
        }
    }
    if ((destName != NULL) && ((optionValues[0] != NULL) || (optionValues[1] != NULL))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("use a complex destination or -real and -imag, not both", -1));
        return TCL_ERROR;
    }
    if (destName != NULL) {
        count = 1;
        if (FftVector(srcPtr->dataPtr, destName, &dest[0]) != TCL_OK) {
            return TCL_ERROR;
        }
        if (dest[0]->type != RBC_VECTOR_COMPLEX) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT destination must be a complex vector", -1));
            return TCL_ERROR;
        }
    } else {
        count = 2;
        if ((optionValues[0] == NULL) || (optionValues[1] == NULL)) {
            Tcl_SetObjResult(interp,
                             Tcl_NewStringObj("FFT requires a complex destination or both -real and -imag", -1));
            return TCL_ERROR;
        }
        for (k = 0; k < count; k++) {
            if (FftVector(srcPtr->dataPtr, optionValues[k], &dest[k]) != TCL_OK) {
                return TCL_ERROR;
            }
            if (dest[k]->type != RBC_VECTOR_REAL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT -real and -imag destinations must be real vectors", -1));
                return TCL_ERROR;
            }
        }
        if (dest[0] == dest[1]) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT -real and -imag destinations must be distinct", -1));
            return TCL_ERROR;
        }
    }
    if (optionValues[2] != NULL) {
        if (FftVector(srcPtr->dataPtr, optionValues[2], &imagPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if ((srcPtr->type != RBC_VECTOR_REAL) || (imagPtr->type != RBC_VECTOR_REAL)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT -imaginput requires two real input vectors", -1));
            return TCL_ERROR;
        }
        if (srcPtr->length != imagPtr->length) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT input vectors must have equal lengths", -1));
            return TCL_ERROR;
        }
    }
    if (srcPtr->length == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT input vector must not be empty", -1));
        return TCL_ERROR;
    }
    length = srcPtr->length;
    if (optionValues[3] != NULL) {
        if (strcmp(Tcl_GetString(optionValues[3]), "auto") == 0) {
            length = 1;
            while (length < srcPtr->length) {
                if (length > TCL_SIZE_MAX / 2) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT length is too large", -1));
                    return TCL_ERROR;
                }
                length *= 2;
            }
        } else if (Tcl_GetSizeIntFromObj(interp, optionValues[3], &length) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if ((length <= 0) || ((length & (length - 1)) != 0)) {
        Tcl_SetObjResult(interp,
                         Tcl_NewStringObj("FFT length must be a positive power of two; use -length auto to pad", -1));
        return TCL_ERROR;
    }
    if (length < srcPtr->length) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("FFT length must not be smaller than the input length", -1));
        return TCL_ERROR;
    }
    for (k = 0; k < count; k++) {
        if (Rbc_VectorCheckWritable(interp, (Rbc_Vector *)dest[k]) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    data = FftAllocate(interp, length, sizeof(*data));
    if (data == NULL) {
        goto error;
    }
    for (i = 0; i < srcPtr->length; i++) {
        data[i] = Rbc_VectorValueAsComplex(srcPtr, i);
        if (imagPtr != NULL) {
            data[i].imag = imagPtr->data.real[i];
        }
        if (!FINITE(data[i].real) || !FINITE(data[i].imag)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("non-finite FFT input at index %" TCL_SIZE_MODIFIER "d", i));
            goto error;
        }
        if (inverse) {
            data[i].real /= (double)length;
            data[i].imag /= (double)length;
        }
    }
    for (; i < length; i++) {
        data[i].real = data[i].imag = 0.0;
    }
    if (length > 1) {
        roots = FftAllocate(interp, length / 2, sizeof(*roots));
        if (roots == NULL) {
            goto error;
        }
        for (i = 0; i < length / 2; i++) {
            double theta = (inverse ? 2.0 : -2.0) * acos(-1.0) * ((double)i / (double)length);
            roots[i].real = cos(theta);
            roots[i].imag = sin(theta);
        }
    }
    FftTransform(data, roots, length);
    if (roots != NULL) {
        ckfree(roots);
        roots = NULL;
    }
    for (i = 0; i < length; i++) {
        if (!FINITE(data[i].real) || !FINITE(data[i].imag)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("non-finite FFT result", -1));
            goto error;
        }
    }
    if (count == 2) {
        realData = FftAllocate(interp, length, sizeof(*realData));
        imagData = FftAllocate(interp, length, sizeof(*imagData));
        if ((realData == NULL) || (imagData == NULL)) {
            goto error;
        }
        for (i = 0; i < length; i++) {
            realData[i] = data[i].real;
            imagData[i] = data[i].imag;
        }
    }
    if (count == 1) {
        output[0] = data;
    } else {
        output[0] = realData;
        output[1] = imagData;
        ckfree(data);
    }
    return FftCommit(interp, dest, output, count, length);

error:
    if (data != NULL) {
        ckfree(data);
    }
    if (roots != NULL) {
        ckfree(roots);
    }
    if (realData != NULL) {
        ckfree(realData);
    }
    if (imagData != NULL) {
        ckfree(imagData);
    }
    return TCL_ERROR;
}

/**
 * @brief Transfer a completed real generator buffer to its vector.
 * @param[in,out] vPtr Live writable real destination, already validated by the caller.
 * @param[in] interp Owning interpreter; see @ref rbc_interp_argument.
 * @param[in] data Completed independent Tcl-allocated real buffer, consumed by the commit.
 * @param[in] length Number of valid finite values in data and resulting destination capacity.
 * @return TCL_OK with the destination name captured before commit callbacks.
 *
 * @ingroup fft_internals
 *
 * The caller has checked real type, writability, length and finite values.
 * Consumes the Tcl-allocated data buffer through FftCommit, including old-storage
 * release, cache flushing and notification. Returns TCL_OK with the qualified
 * name recorded at commit time; callbacks can change or destroy the vector.
 */
static int FftStoreReal(VectorObject *vPtr, Tcl_Interp *interp, double *data, Tcl_Size length) {
    void *output = data;

    return FftCommit(interp, &vPtr, &output, 1, length);
}

/* Operation contract and dispatcher preconditions are in rbcVectorInt.h. */
int Rbc_VectorFftfreqOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *const options[] = {"-delta", "-onesided", NULL};
    Tcl_Size length, count, i, positive;
    double delta = 1.0;
    double *data;
    int oneSided = FALSE;
    unsigned int seen = 0;

    if (Tcl_GetSizeIntFromObj(interp, objv[2], &length) != TCL_OK) {
        return TCL_ERROR;
    }
    if (length <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("frequency length must be positive", -1));
        return TCL_ERROR;
    }
    for (i = 3; i < objc; i += 2) {
        int option;

        if (Tcl_GetIndexFromObj(interp, objv[i], options, "option", TCL_EXACT, &option) != TCL_OK) {
            return TCL_ERROR;
        }
        if (seen & (1u << option)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("duplicate option \"%s\"", Tcl_GetString(objv[i])));
            return TCL_ERROR;
        }
        seen |= 1u << option;
        if (i + 1 == objc) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("missing value for \"%s\"", Tcl_GetString(objv[i])));
            return TCL_ERROR;
        }
        if (option == 0) {
            if (Tcl_GetDoubleFromObj(interp, objv[i + 1], &delta) != TCL_OK) {
                return TCL_ERROR;
            }
        } else if (Tcl_GetBooleanFromObj(interp, objv[i + 1], &oneSided) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (!FINITE(delta) || (delta <= 0.0)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("sample interval must be finite and positive", -1));
        return TCL_ERROR;
    }
    count = oneSided ? length / 2 + 1 : length;
    positive = (length - 1) / 2 + 1;
    data = FftAllocate(interp, count, sizeof(*data));
    if (data == NULL) {
        return TCL_ERROR;
    }
    for (i = 0; i < count; i++) {
        Tcl_Size bin = (oneSided || (i < positive)) ? i : i - length;

        /* Divide in this order to avoid overflowing N*delta or 1/delta. */
        data[i] = ((double)bin / (double)length) / delta;
        if (!FINITE(data[i])) {
            ckfree(data);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("non-finite frequency bin", -1));
            return TCL_ERROR;
        }
    }
    return FftStoreReal(vPtr, interp, data, count);
}

/* Operation contract and dispatcher preconditions are in rbcVectorInt.h. */
int Rbc_VectorWindowOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *const windows[] = {"rectangular", "hann", "hamming", "blackman", "bartlett", NULL};
    static const char *const options[] = {"-periodic", NULL};
    enum { RECTANGULAR, HANN, HAMMING, BLACKMAN, BARTLETT };
    Tcl_Size length, i;
    int kind, periodic = TRUE, seen = FALSE;
    double denominator, pi = acos(-1.0);
    double *data;

    if (Tcl_GetIndexFromObj(interp, objv[2], windows, "window", TCL_EXACT, &kind) != TCL_OK ||
        Tcl_GetSizeIntFromObj(interp, objv[3], &length) != TCL_OK) {
        return TCL_ERROR;
    }
    if (length <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("window length must be positive", -1));
        return TCL_ERROR;
    }
    for (i = 4; i < objc; i += 2) {
        int option;

        if (Tcl_GetIndexFromObj(interp, objv[i], options, "option", TCL_EXACT, &option) != TCL_OK) {
            return TCL_ERROR;
        }
        if (seen) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("duplicate option \"-periodic\"", -1));
            return TCL_ERROR;
        }
        seen = TRUE;
        if (i + 1 == objc) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("missing value for \"-periodic\"", -1));
            return TCL_ERROR;
        }
        if (Tcl_GetBooleanFromObj(interp, objv[i + 1], &periodic) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    data = FftAllocate(interp, length, sizeof(*data));
    if (data == NULL) {
        return TCL_ERROR;
    }
    denominator = (double)(periodic ? length : length - 1);
    for (i = 0; i < length; i++) {
        if ((length == 1) || (kind == RECTANGULAR)) {
            data[i] = 1.0;
        } else {
            /* Fold about the centre for identical symmetric coefficients.
             * Sine-squared forms avoid cancellation near the endpoints. */
            Tcl_Size mirror = (periodic ? length : length - 1) - i;
            double x = (double)((i < mirror) ? i : mirror) / denominator;
            double sine = sin(pi * x);
            double square = sine * sine;

            switch (kind) {
            case HANN:
                data[i] = square;
                break;
            case HAMMING:
                data[i] = 0.08 + 0.92 * square;
                break;
            case BLACKMAN:
                data[i] = square * (0.68 - 0.32 * cos(2.0 * pi * x));
                break;
            case BARTLETT:
                data[i] = 2.0 * x;
                break;
            default:
                Tcl_Panic("bad FFT window type");
            }
        }
    }
    return FftStoreReal(vPtr, interp, data, length);
}
