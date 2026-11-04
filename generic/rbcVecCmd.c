/*
 * rbcVecObjCmd.c --
 *
 *      This file contains all commands to process the operations
 *      on instances of a vector.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/**
 * @file rbcVecCmd.c
 * @brief Vector instance operations, conversions and binary input.
 */
/**
 * @defgroup vector_command_paths Vector instance command paths
 * @brief Selected private mutation and input helpers; see the developer manual
 * for partial-update and callback lifetime limitations.
 */

/**
 * @defgroup vector_edit_paths Vector indexed edits and rearrangement
 * @brief Indexed and multi-output handlers with callback-aware storage ownership.
 */
/**
 * @defgroup vector_numeric_paths Vector queries and numerical generators
 * @brief Queries and generators with callback-aware storage ownership.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "rbcVectorInt.h"

/**
 * @ingroup vector_command_paths
 * @brief Add two nonnegative logical sizes without signed overflow.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] a Nonnegative first logical count.
 * @param[in] b Nonnegative second logical count.
 * @param[out] resultPtr Required sum output, unchanged on failure.
 * @return TCL_OK with the sum, or TCL_ERROR for negative operands or overflow.
 * @see @ref rbc_calling_conventions
 *
 * Returns TCL_ERROR with a diagnostic for negative operands or a sum above TCL_SIZE_MAX; leaves the
 * output unchanged on failure. Otherwise writes the sum and returns TCL_OK. Does not allocate or
 * execute Tcl.
 */
static int AddVectorSizes(Tcl_Interp *interp, Tcl_Size a, Tcl_Size b, Tcl_Size *resultPtr) {
    if ((a < 0) || (b < 0) || (a > (TCL_SIZE_MAX - b))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector size is too large", -1));
        return TCL_ERROR;
    }
    *resultPtr = a + b;
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Multiply nonnegative logical sizes with overflow checking.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] a Nonnegative first factor, including zero.
 * @param[in] b Nonnegative second factor, including zero.
 * @param[out] resultPtr Required product output, unchanged on failure.
 * @return TCL_OK with the product, or TCL_ERROR for negative operands or overflow.
 * @see @ref rbc_calling_conventions
 *
 * Accepts zero operands, rejects negative operands and products above TCL_SIZE_MAX, and writes the
 * result only on success. Returns a Tcl status with a diagnostic on failure; no allocation or
 * callbacks occur.
 */
static int MultiplyVectorSizes(Tcl_Interp *interp, Tcl_Size a, Tcl_Size b, Tcl_Size *resultPtr) {
    if ((a < 0) || (b < 0) || ((b != 0) && (a > (TCL_SIZE_MAX / b)))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector size is too large", -1));
        return TCL_ERROR;
    }  
    *resultPtr = a * b;
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Convert a logical count to a checked allocation byte count.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] count Logical element count; negative counts are rejected.
 * @param[in] elementSize Nonzero size of one element in bytes, supplied by the caller.
 * @param[out] byteCountPtr Required byte-count output, unchanged on failure.
 * @return TCL_OK with count times elementSize, or TCL_ERROR if it cannot be represented.
 * @see @ref rbc_calling_conventions
 *
 * Requires a nonzero elementSize and writable output pointer. Rejects negative counts and
 * multiplication exceeding SIZE_MAX. Writes the product only on success and returns a Tcl status. Does
 * not allocate storage.
 */
static int GetArrayByteCount(Tcl_Interp *interp, Tcl_Size count, size_t elementSize, size_t *byteCountPtr) {
    if ((count < 0) || ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / elementSize))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("allocation size is too large", -1));
        return TCL_ERROR;
    }
    *byteCountPtr = (size_t)count * elementSize;
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Check logical liveness of a caller-retained vector.
 * @param[in] vPtr Valid caller-retained record with a caller-retained owning interpreter.
 * @return TCL_OK if both owners remain logically live; otherwise TCL_ERROR with RBC VECTOR DESTROYED.
 * @see @ref rbc_calling_conventions
 *
 * Requires a valid preserved record and interpreter. Rejects vector destruction or interpreter
 * deletion with RBC VECTOR DESTROYED. Does not retain the object itself, check a selection or validate
 * numerical storage contents.
 */
static int CheckCommandSource(VectorObject *vPtr) {
    if (Tcl_InterpDeleted(vPtr->interp) || vPtr->destroying) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector was destroyed during command evaluation", -1));
        Tcl_SetErrorCode(vPtr->interp, "RBC", "VECTOR", "DESTROYED", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Check a retained destination for liveness and writability.
 * @param[in] vPtr Valid caller-retained destination and owning interpreter.
 * @return TCL_OK if live and writable, otherwise TCL_ERROR with a diagnostic.
 * @see @ref rbc_calling_conventions
 *
 * First delegates to CheckCommandSource, then enforces the vector read-only flag. Returns a Tcl
 * status. Does not allocate, resize or publish changes; the caller maintains preservation references.
 */
static int CheckCommandDestination(VectorObject *vPtr) {
    if (CheckCommandSource(vPtr) != TCL_OK) return TCL_ERROR;
    return Rbc_VectorCheckWritable(vPtr->interp, (Rbc_Vector *)vPtr);
}

/**
 * @ingroup vector_command_paths
 * @brief Prepare reusable or enlarged storage without releasing old data.
 * @param[in] vPtr Live destination, with record and interpreter retained by the caller.
 * @param[in] length Nonnegative requested used count, validated by the caller.
 * @param[out] dataPtr Required prepared-storage output; consume only on TCL_OK. May alias current storage.
 * @param[out] sizePtr Required prepared capacity in values, written on success.
 * @return TCL_OK with reused storage or an owned allocation; TCL_ERROR on protection or allocation failure.
 * @see @ref rbc_calling_conventions
 *
 * Requires a nonnegative requested length and a retained destination/interpreter. Checks writability,
 * reuses existing capacity when sufficient, otherwise grows geometrically with overflow checks and
 * copies the existing logical prefix. Does not initialize the added tail or change metadata. On
 * success outputs either borrowed current storage or a new allocation; callers distinguish them when
 * abandoning preparation. No storage-release callback occurs here.
 */
static int PrepareCommandWrite(VectorObject *vPtr, Tcl_Size length, VectorData *dataPtr, Tcl_Size *sizePtr) {
    size_t bytes, usedBytes;
    size_t elementSize = (vPtr->type == RBC_VECTOR_REAL) ? sizeof(double) : sizeof(Rbc_Complex);
    Tcl_Size size = vPtr->size;

    if (CheckCommandDestination(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (length <= size) {
        *dataPtr = vPtr->data;
        *sizePtr = size;
        return TCL_OK;
    }
    if (size == 0) size = DEF_ARRAY_SIZE;
    while (size < length) {
        if (size > TCL_SIZE_MAX / 2) {
            size = length;
            break;
        }
        size *= 2;
    }
    if (GetArrayByteCount(vPtr->interp, size, elementSize, &bytes) != TCL_OK ||
        GetArrayByteCount(vPtr->interp, vPtr->length, elementSize, &usedBytes) != TCL_OK) {
        return TCL_ERROR;
    }
    dataPtr->raw = Tcl_AttemptAlloc(bytes);
    if (dataPtr->raw == NULL) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector command storage", -1));
        return TCL_ERROR;
    }
    if (usedBytes > 0) memcpy(dataPtr->raw, vPtr->data.raw, usedBytes);
    *sizePtr = size;
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Prepare storage using the explicit-resize capacity policy.
 * @param[in] vPtr Live destination, with record and interpreter retained by the caller.
 * @param[in] length Nonnegative requested used count, validated by the caller.
 * @param[out] dataPtr Required prepared-storage output; consume only on TCL_OK. May alias current storage.
 * @param[out] sizePtr Required capacity output; may be written before allocation failure.
 * @return TCL_OK with reused storage, an owned allocation or NULL for zero capacity; otherwise TCL_ERROR.
 * @see @ref rbc_calling_conventions
 *
 * Requires a nonnegative length and a retained destination/interpreter. Checks writability and
 * computes capacity from DEF_ARRAY_SIZE, with zero capacity for zero length. Reuses matching capacity
 * or allocates and copies the retained prefix. Does not zero newly added values, install metadata or
 * release old storage. The caller owns a newly allocated result until commit, but must not free a
 * reused current pointer.
 */
static int PrepareCommandResize(VectorObject *vPtr, Tcl_Size length, VectorData *dataPtr, Tcl_Size *sizePtr) {
    Tcl_Size size = length == 0 ? 0 : DEF_ARRAY_SIZE;
    Tcl_Size used = MIN(length, vPtr->length);
    size_t bytes, usedBytes;
    size_t elementSize = vPtr->type == RBC_VECTOR_REAL ? sizeof(double) : sizeof(Rbc_Complex);

    if (CheckCommandDestination(vPtr) != TCL_OK) return TCL_ERROR;
    while (size < length) {
        if (size > TCL_SIZE_MAX / 2) { size = length; break; }
        size *= 2;
    }
    *sizePtr = size;
    if (size == vPtr->size) {
        *dataPtr = vPtr->data;
        return TCL_OK;
    }
    dataPtr->raw = NULL;
    if (size == 0) return TCL_OK;
    if (GetArrayByteCount(vPtr->interp, size, elementSize, &bytes) != TCL_OK ||
        GetArrayByteCount(vPtr->interp, used, elementSize, &usedBytes) != TCL_OK) return TCL_ERROR;
    dataPtr->raw = Tcl_AttemptAlloc(bytes);
    if (dataPtr->raw == NULL) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector command storage", -1));
        return TCL_ERROR;
    }
    if (usedBytes > 0) memcpy(dataPtr->raw, vPtr->data.raw, usedBytes);
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Install completed storage before releasing the old allocation.
 * @param[in,out] vPtr Caller-retained destination, already validated as writable before commit.
 * @param[in] data Completed typed storage, either the current pointer or Tcl-allocated storage to transfer.
 * @param[in] length Valid nonnegative used count, no greater than size.
 * @param[in] size Valid capacity in values for data; NULL storage requires zero capacity.
 * @see @ref rbc_calling_conventions
 *
 * Caller validates dimensions, prepares all values and retains vector/interpreter. Installs data,
 * length, capacity, whole-vector selection and invalid extrema. A changed pointer acquires TCL_DYNAMIC
 * ownership; an unchanged pointer keeps its existing release procedure. Releases detached old storage
 * according to its previous policy, preserving interpreter state across a custom release callback.
 * Does not access vector storage after release, enforce writability or notify observers. Callback
 * effects are not rolled back.
 */
static void FinishCommandWrite(VectorObject *vPtr, VectorData data, Tcl_Size length, Tcl_Size size) {
    void *oldData = vPtr->data.raw;
    Tcl_FreeProc *oldFree = vPtr->freeProc;
    Tcl_Interp *interp = vPtr->interp;
    Tcl_InterpState state;

    vPtr->data = data;
    vPtr->length = length;
    vPtr->size = size;
    vPtr->first = 0;
    vPtr->last = length - 1;
    vPtr->min = vPtr->max = rbcNaN;
    vPtr->minIndex = vPtr->maxIndex = -1;
    vPtr->notifyFlags |= UPDATE_RANGE;
    if (oldData != data.raw) {
        vPtr->freeProc = TCL_DYNAMIC;
        if (oldData != NULL && oldFree != TCL_STATIC) {
            state = Tcl_SaveInterpState(interp, TCL_OK);
            if (oldFree == TCL_DYNAMIC) ckfree(oldData);
            else oldFree(oldData);
            Tcl_RestoreInterpState(interp, state);
        }
    }
}

/**
 * @ingroup vector_command_paths
 * @brief Flush and publish a committed write while preserving command state.
 * @param[in,out] vPtr Caller-retained vector and interpreter; logically destroyed owners are skipped.
 * @param[in] first First changed zero-based storage index for a suffix, or -1 for a full update.
 *     Out-of-range indices also request a full update after cache flushing.
 * @see @ref rbc_calling_conventions
 *
 * Requires caller-retained vector/interpreter. Skips destroyed owners, flushes an enabled mapped
 * cache, then checks liveness again before notification. A valid nonnegative first reports first
 * through the current final index; all other values report a full update. Restores the saved
 * interpreter result and return options across traces and immediate observers. Does not retain owners
 * itself or undo callback changes.
 */
static void NotifyCommandWrite(VectorObject *vPtr, Tcl_Size first) {
    Tcl_Interp *interp = vPtr->interp;
    Tcl_InterpState state = Tcl_SaveInterpState(interp, TCL_OK);

    if (!Tcl_InterpDeleted(interp) && !vPtr->destroying) {
        if (vPtr->flush) Rbc_VectorFlushCache(vPtr);
        if (!Tcl_InterpDeleted(interp) && !vPtr->destroying) {
            if (first >= 0 && first < vPtr->length) {
                Rbc_VectorUpdateClientsRange(vPtr, first, vPtr->length - 1);
            } else {
                Rbc_VectorUpdateClients(vPtr);
            }
        }
    }
    Tcl_RestoreInterpState(interp, state);
}

/**
 * @ingroup vector_command_paths
 * @brief Check two saved indices against current live storage bounds.
 * @param[in] vPtr Caller-retained vector and interpreter whose current length is checked.
 * @param[in] first Saved zero-based storage index, required to lie within current used storage.
 * @param[in] last Saved inclusive endpoint, independently checked against current used storage.
 * @return TCL_OK when both indices remain in bounds, otherwise TCL_ERROR; ordering is not checked.
 * @see @ref rbc_calling_conventions
 *
 * Requires a retained record. Rejects dead owners or either endpoint outside zero through length minus
 * one, reporting RBC VECTOR RANGE for invalid bounds. Does not check endpoint ordering, writability or
 * whether values changed since the indices were captured.
 */
static int CheckCommandRange(VectorObject *vPtr, Tcl_Size first, Tcl_Size last) {
    if (CheckCommandSource(vPtr) != TCL_OK) return TCL_ERROR;
    if (first < 0 || last < 0 || first >= vPtr->length || last >= vPtr->length) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector range changed during command evaluation", -1));
        Tcl_SetErrorCode(vPtr->interp, "RBC", "VECTOR", "RANGE", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Qualify an output name in the current namespace.
 * @param[in] interp Interpreter providing the current namespace; see @ref rbc_interp_argument.
 * @param[in] name NUL-terminated output name, absolute or relative to the current namespace.
 * @param[out] buffer Caller-owned DString initialized here; release with Tcl_DStringFree after using the result.
 * @return Qualified name borrowed from buffer, invalidated by its modification or release.
 * @see @ref rbc_calling_conventions
 *
 * Initializes and fills the caller DString, preserving absolute names and prefixing relative names
 * with the current namespace. Returns borrowed DString storage for the caller to free. Does not
 * resolve namespaces, interpret selections or validate the name.
 */
static const char *CommandOutputName(Tcl_Interp *interp, const char *name, Tcl_DString *buffer) {
    Tcl_DStringInit(buffer);
    if (name[0] != ':' || name[1] != ':') {
        Tcl_DStringAppend(buffer, Tcl_GetCurrentNamespace(interp)->fullName, -1);
        if (Tcl_DStringLength(buffer) > 2) Tcl_DStringAppend(buffer, "::", -1);
    }
    Tcl_DStringAppend(buffer, name, -1);
    return Tcl_DStringValue(buffer);
}

/**
 * @ingroup vector_command_paths
 * @brief Resolve or create a retained same-type command destination.
 * @param[in] source Caller-retained source supplying the interpreter and required numeric type.
 * @param[in] name NUL-terminated literal destination name, qualified in the current namespace.
 * @param[out] isNewPtr Required creation flag; consume only after a non-NULL return.
 * @return Preserved destination record, released with Tcl_Release by the caller, or NULL on error.
 * @see @ref rbc_calling_conventions
 *
 * Qualifies the name in the current namespace, so a relative output does not fall back to a global
 * vector. Existing outputs must be writable and match the source type; their commands and mapped
 * arrays are retained. Missing outputs are created without array mapping. Returns a preserved vector
 * and sets isNewPtr, or NULL on error. Caller releases the preservation reference; this helper does
 * not publish output data.
 */
static VectorObject *GetCommandOutput(VectorObject *source, const char *name, int *isNewPtr) {
    VectorObject *dest = NULL;
    Rbc_Vector *vector;
    Tcl_Interp *interp = source->interp;
    Tcl_DString buffer;

    if (CheckCommandSource(source) != TCL_OK) return NULL;
    name = CommandOutputName(interp, name, &buffer);
    *isNewPtr = FALSE;
    if (Rbc_VectorExists2(interp, name)) {
        if (Rbc_GetVector(interp, name, &vector) != TCL_OK) goto done;
        if (vector->type != source->type) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector type mismatch", -1));
            goto done;
        }
        if (CheckCommandDestination((VectorObject *)vector) != TCL_OK) goto done;
        dest = (VectorObject *)vector;
    } else {
        dest = Rbc_VectorCreate(source->dataPtr, name, name, NULL, source->type, isNewPtr);
    }
    if (dest != NULL) Tcl_Preserve(dest);
done:
    Tcl_DStringFree(&buffer);
    return dest;
}

typedef struct {
    VectorObject *vector;
    int isNew;
} CommandOutput;

/**
 * @ingroup vector_command_paths
 * @brief Release an output-record array and its preservation references.
 * @param[in] outputs Owned array from GetCommandOutputs, consumed and freed by this call.
 * @param[in] count Number of initialized slots, each owning a preservation reference when non-NULL.
 * @see @ref rbc_calling_conventions
 *
 * Releases every non-NULL vector slot, then frees the owned array. Requires count initialized slots,
 * which may include repeated identities with separate references. Does not destroy newly created
 * vectors or roll back their data.
 */
static void ReleaseCommandOutputs(CommandOutput *outputs, Tcl_Size count) {
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        if (outputs[i].vector != NULL) Tcl_Release(outputs[i].vector);
    }
    ckfree(outputs);
}

/**
 * @ingroup vector_command_paths
 * @brief Precheck existing destinations then collect retained output identities.
 * @param[in] source Caller-retained source supplying the interpreter and required output type.
 * @param[in] count Nonnegative number of output names and returned slots.
 * @param[in] names Borrowed array of count Tcl name objects, kept valid during collection.
 * @return Owned array with preserved output records, or NULL on failure. Release with ReleaseCommandOutputs.
 * @see @ref rbc_calling_conventions
 *
 * Allocates zero-initialized output records after checking the byte count. First validates existing
 * literal destinations for type and writability, then resolves or creates each output. On failure
 * releases collected references and the record array; already created vectors remain. Returns an owned
 * array, including an allocation for count zero. Does not reject duplicate names or source/output
 * aliasing; operation handlers impose their own rules.
 */
static CommandOutput *GetCommandOutputs(VectorObject *source, Tcl_Size count, Tcl_Obj *const names[]) {
    CommandOutput *outputs;
    Tcl_Size i;
    size_t bytes;
    if (GetArrayByteCount(source->interp, count, sizeof(*outputs), &bytes) != TCL_OK) return NULL;
    outputs = ckalloc(bytes > 0 ? bytes : 1);
    memset(outputs, 0, bytes);
    /* Reject existing read-only/type-mismatched destinations before creating
     * any missing names. Literal lookup cannot evaluate range expressions. */
    for (i = 0; i < count; i++) {
        Tcl_DString buffer;
        const char *name = CommandOutputName(source->interp, Tcl_GetString(names[i]), &buffer);
        if (Rbc_VectorExists2(source->interp, name)) {
            VectorObject *dest;
            int isNew;
            dest = GetCommandOutput(source, name, &isNew);
            if (dest == NULL) {
                Tcl_DStringFree(&buffer);
                ReleaseCommandOutputs(outputs, count);
                return NULL;
            }
            Tcl_Release(dest);
        }
        Tcl_DStringFree(&buffer);
    }
    for (i = 0; i < count; i++) {
        outputs[i].vector = GetCommandOutput(source, Tcl_GetString(names[i]), &outputs[i].isNew);
        if (outputs[i].vector == NULL) {
            ReleaseCommandOutputs(outputs, count);
            return NULL;
        }
    }
    return outputs;
}

/**
 * @ingroup vector_command_paths
 * @brief Map and notify a completed output without resolving its name again.
 * @param[in,out] dest Caller-retained destination with completed data already installed.
 * @param[in] isNew Nonzero if this operation created dest and should consider default array mapping.
 * @return TCL_OK after publication or if already destroyed; TCL_ERROR for mapping failure after commit.
 * @see @ref rbc_calling_conventions
 *
 * Requires a retained destination/interpreter and already committed data. A destroyed owner is treated
 * as successfully committed. New real vectors without parentheses receive their default mapped array;
 * existing outputs and complex or parenthesized names do not get a new mapping. Mapping failure
 * returns TCL_ERROR with its diagnostic. Otherwise publishes a full update while restoring prior
 * interpreter state. Does not roll back committed data or callback effects.
 */
static int PublishCommandOutput(VectorObject *dest, int isNew) {
    Tcl_Interp *interp = dest->interp;
    Tcl_InterpState state;

    if (Tcl_InterpDeleted(interp) || dest->destroying) return TCL_OK; /* Already committed. */
    state = Tcl_SaveInterpState(interp, TCL_OK);
    if (isNew && dest->type == RBC_VECTOR_REAL && strchr(dest->name, '(') == NULL) {
        if (Rbc_VectorMapVariable(interp, dest, dest->name) != TCL_OK) {
            Tcl_DiscardInterpState(state);
            return TCL_ERROR;
        }
    }
    NotifyCommandWrite(dest, -1);
    Tcl_RestoreInterpState(interp, state);
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Allocate uninitialized typed staging storage.
 * @param[in] vPtr Live vector supplying numeric type and diagnostic interpreter; storage is not changed.
 * @param[in] length Nonnegative number of typed values to allocate; negative values are rejected.
 * @param[out] dataPtr Required storage output; consume only on TCL_OK and release with ckfree unless transferred.
 * @return TCL_OK with an owned non-NULL allocation, including length zero; TCL_ERROR on failure.
 * @see @ref rbc_calling_conventions
 *
 * Checks byte-count overflow and attempts an owned allocation, reserving one byte for an empty result
 * so NULL denotes failure. Returns a Tcl status. Does not modify vector metadata, check writability or
 * invoke callbacks. The caller frees the allocation or transfers it at commit.
 */
static int AllocateCommandData(VectorObject *vPtr, Tcl_Size length, VectorData *dataPtr) {
    size_t bytes;
    size_t elementSize = vPtr->type == RBC_VECTOR_REAL ? sizeof(double) : sizeof(Rbc_Complex);
    if (GetArrayByteCount(vPtr->interp, length, elementSize, &bytes) != TCL_OK) return TCL_ERROR;
    dataPtr->raw = Tcl_AttemptAlloc(bytes > 0 ? bytes : 1);
    if (dataPtr->raw == NULL) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector command storage", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Convert a complete argument list into owned typed staging storage.
 * @param[in] vPtr Caller-retained destination and interpreter supplying type and protection state.
 * @param[in] objc Nonnegative number of values to convert; this is not a full command argument count.
 * @param[in] objv Borrowed array of objc value objects; no command name or operation prefix is present.
 * @param[out] dataPtr Required owned-storage output, initialized to NULL and reset to NULL on failure.
 * @return TCL_OK with staged data, or NULL data for zero values; TCL_ERROR on conversion or protection failure.
 * @see @ref rbc_calling_conventions
 *
 * Allocates storage and retains a private outer Tcl list representation before evaluating values. Each
 * successful conversion is followed by a destination liveness/writability check. Failure frees staging
 * storage and resets its pointer to NULL. Success returns owned data, or NULL for an empty list. Does
 * not install values or length; callbacks may change the destination themselves. Caller retains the
 * vector/interpreter throughout conversion.
 */
static int ConvertCommandList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[], VectorData *dataPtr) {
    Tcl_Size i;
    Tcl_Obj *listObj;
    Tcl_Obj **values;
    size_t bytes;
    size_t elementSize = (vPtr->type == RBC_VECTOR_REAL) ? sizeof(double) : sizeof(Rbc_Complex);

    dataPtr->raw = NULL;
    if (GetArrayByteCount(vPtr->interp, objc, elementSize, &bytes) != TCL_OK) return TCL_ERROR;
    if (bytes > 0) {
        dataPtr->raw = Tcl_AttemptAlloc(bytes);
        if (dataPtr->raw == NULL) {
            Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector conversion storage", -1));
            return TCL_ERROR;
        }
    }
    /* Keep a private list representation: a conversion callback may shimmer
     * the original argument into another Tcl internal representation. */
    listObj = Tcl_NewListObj(objc, objv);
    Tcl_IncrRefCount(listObj);
    Tcl_ListObjGetElements(NULL, listObj, &objc, &values);
    for (i = 0; i < objc; i++) {
        int result;
        if (vPtr->type == RBC_VECTOR_COMPLEX) {
            result = Rbc_GetComplex(vPtr->interp, values[i], &dataPtr->complex[i]);
        } else {
            result = Rbc_GetDouble(vPtr->interp, values[i], &dataPtr->real[i]);
        }
        if (result != TCL_OK || CheckCommandDestination(vPtr) != TCL_OK) {
            if (dataPtr->raw != NULL) ckfree(dataPtr->raw);
            dataPtr->raw = NULL;
            Tcl_DecrRefCount(listObj);
            return TCL_ERROR;
        }
    }
    Tcl_DecrRefCount(listObj);
    return TCL_OK;
}

/**
 * @brief Distinguish an explicit complex pair before scalar expression parsing.
 * @param[in] interp Evaluation interpreter; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed scalar or pair kept alive through component evaluation.
 * @param[out] valuePtr Required complex output; may be partially written on conversion failure.
 * @param[out] isComplexPtr Required flag, set from pair syntax before conversion, not from the imaginary value.
 * @return TCL_OK on conversion, otherwise TCL_ERROR; consume valuePtr only on success.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_numeric_paths
 *
 * Marks a two-item Tcl list as complex even if the imaginary part is zero,
 * then delegates conversion to Rbc_GetComplex. Thus a pair such as {3 -4}
 * is not treated as the arithmetic expression 3-4. Returns a Tcl status;
 * conversion may execute Tcl and partially populate the output on failure.
 */
static int GetArithmeticScalar(Tcl_Interp *interp, Tcl_Obj *objPtr, Rbc_Complex *valuePtr, int *isComplexPtr) {
    Tcl_Size objc;
    Tcl_Obj **objv;

    /*
     * A two-element Tcl list is explicitly a complex scalar.
     *
     * Do this test before Rbc_GetDouble(), because a value such
     * as {3 -4} could otherwise be interpreted as the Tcl
     * expression "3 - 4".
     */
    *isComplexPtr = FALSE;
    if (Tcl_ListObjGetElements(NULL, objPtr, &objc, &objv) == TCL_OK) {
        if (objc == 2) {
            *isComplexPtr = TRUE;
        }
    }
    return Rbc_GetComplex(interp, objPtr, valuePtr);
}

/**
 * @brief Apply one supported arithmetic operator to two complex scalars.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] operator Character code for +, -, * or /.
 * @param[in] a Left operand copied by value.
 * @param[in] b Right operand; a zero divisor is rejected for division.
 * @param[out] resultPtr Required result output, unchanged for rejected division by zero.
 * @return TCL_OK after arithmetic, or TCL_ERROR for zero division; finiteness is not guaranteed.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_numeric_paths
 *
 * Accepts +, -, * and /. Returns TCL_ERROR with divide-by-zero diagnostic for
 * an exactly zero complex divisor; otherwise writes resultPtr and returns
 * TCL_OK. An unsupported internal operator panics. Does not invoke callbacks.
 */
static int ComplexArithmetic(Tcl_Interp *interp, int operator, Rbc_Complex a, Rbc_Complex b, Rbc_Complex *resultPtr) {
    switch (operator) {
    case '+':
        *resultPtr = Rbc_ComplexAdd(a, b);
        break;
    case '-':
        *resultPtr = Rbc_ComplexSub(a, b);
        break;
    case '*':
        *resultPtr = Rbc_ComplexMul(a, b);
        break;
    case '/':
        if (Rbc_ComplexIsZero(b)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("divide by zero", -1));
            return TCL_ERROR;
        }
        *resultPtr = Rbc_ComplexDiv(a, b);
        break;
    default:
        Tcl_Panic("bad complex arithmetic operator %c", operator);
    }
    return TCL_OK;
}

enum NativeFormats {
    FMT_UNKNOWN = -1,
    FMT_UCHAR,
    FMT_CHAR,
    FMT_USHORT,
    FMT_SHORT,
    FMT_UINT,
    FMT_INT,
    FMT_ULONG,
    FMT_LONG,
    FMT_ULONGLONG,
    FMT_LONGLONG,
    FMT_FLOAT,
    FMT_DOUBLE
};

static int AppendVector(VectorObject *destPtr, VectorObject *srcPtr);
static int AppendList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]);
static int CopyValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, Tcl_Size length, int swap,
                      Tcl_Size *indexPtr);
static int CopyComplexValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, Tcl_Size length,
                             int swap, Tcl_Size *indexPtr);
static int InRange(double value, double min, double max);
static int CopyList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]);
static int CompareVectorIndices(VectorObject *const *vectorArr, Tcl_Size nVectors, Tcl_Size i1, Tcl_Size i2);
static void SiftVectorIndices(Tcl_Size *indexArr, Tcl_Size start, Tcl_Size end, VectorObject *const *vectorArr,
                              Tcl_Size nVectors);
static void SortVectorIndices(Tcl_Size *indexArr, Tcl_Size length, VectorObject *const *vectorArr, Tcl_Size nVectors);

/* Instance Functions Definitions (rbcVecObjCmd.c) */
typedef int RbcVectorCmdOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcVectorCmdOp *proc;
} VectorInstOpSpec;

static RbcVectorCmdOp TypeOp;
static RbcVectorCmdOp ReadOnlyOp;
static RbcVectorCmdOp AppendOp;
static RbcVectorCmdOp ArithOp;
static RbcVectorCmdOp BinreadOp;
static RbcVectorCmdOp ClearOp;
static RbcVectorCmdOp DeleteOp;
static RbcVectorCmdOp DupOp;
static RbcVectorCmdOp ExprOp;
static RbcVectorCmdOp IndexOp;
static RbcVectorCmdOp LengthOp;
static RbcVectorCmdOp MergeOp;
static RbcVectorCmdOp NormalizeOp;
static RbcVectorCmdOp OffsetOp;
static RbcVectorCmdOp RandomOp;
static RbcVectorCmdOp PopulateOp;
static RbcVectorCmdOp RangeOp;
static RbcVectorCmdOp SearchOp;
static RbcVectorCmdOp SeqOp;
static RbcVectorCmdOp SetOp;
static RbcVectorCmdOp SortOp;
static RbcVectorCmdOp SplitOp;
static RbcVectorCmdOp VariableOp;

static const VectorInstOpSpec vectorInstOpCmd[] = {
    {{"*", 3, 3, "list"}, ArithOp},
    {{"+", 3, 3, "list"}, ArithOp},
    {{"-", 3, 3, "list"}, ArithOp},
    {{"/", 3, 3, "list"}, ArithOp},
    {{"append", 3, 0, "item ?item...?"}, AppendOp},
    {{"binread", 3, 0, "channel ?numValues? ?flags?"}, BinreadOp},
    {{"clear", 2, 2, ""}, ClearOp},
    {{"delete", 3, 0, "index ?index?..."}, DeleteOp},
    {{"dup", 3, 3, "vecname"}, DupOp},
    {{"expr", 3, 3, "expression"}, ExprOp},
    {{"fft", 3, 0, "?destComplex? ?-real destReal -imag destImag? ?-imaginput vecName? ?-length auto|size?"},
     Rbc_VectorFftOp},
    {{"fftfreq", 3, 0, "length ?-delta interval? ?-onesided boolean?"}, Rbc_VectorFftfreqOp},
    {{"ifft", 3, 0, "?destComplex? ?-real destReal -imag destImag? ?-imaginput vecName? ?-length auto|size?"},
     Rbc_VectorFftOp},
    {{"index", 3, 4, "index ?value?"}, IndexOp},
    {{"length", 2, 3, "?newSize?"}, LengthOp},
    {{"merge", 3, 0, "vecName ?vecName?..."}, MergeOp},
    {{"normalize", 2, 3, "?vecName?"}, NormalizeOp},
    {{"offset", 2, 3, "?offset?"}, OffsetOp},
    {{"populate", 4, 4, "vecName density"}, PopulateOp},
    {{"random", 2, 2, ""}, RandomOp},
    {{"range", 4, 4, "first last"}, RangeOp},
    {{"readonly", 2, 3, "?boolean?"}, ReadOnlyOp},
    {{"search", 3, 5, "?-value? value ?value?"}, SearchOp},
    {{"seq", 4, 5, "start end ?step?"}, SeqOp},
    {{"set", 3, 3, "list"}, SetOp},
    {{"sort", 2, 0, "?-reverse? ?vecName?..."}, SortOp},
    {{"split", 2, 0, "?vecName?..."}, SplitOp},
    {{"type", 2, 2, ""}, TypeOp},
    {{"variable", 2, 3, "?varName?"}, VariableOp},
    {{"window", 4, 0, "type length ?-periodic boolean?"}, Rbc_VectorWindowOp},
    {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup vector_command_paths
 * @brief Test whether an instance handler accepts complex vectors.
 * @param[in] proc Handler identity to test against the complex-operation allowlist; never invoked here.
 * @return Nonzero for an allowed handler, zero otherwise.
 *
 * Compare handler pointers against the explicit complex-operation allowlist.
 * Aliases sharing a handler inherit its eligibility. This is a dispatch gate,
 * not type validation for operands or output vectors. Adding a handler requires
 * reviewing this list separately from the operation table and write checks.
 */
static int ComplexOpSupported(RbcVectorCmdOp *proc) {
    return ((proc == ReadOnlyOp) || (proc == Rbc_VectorFftOp) || (proc == ExprOp) || (proc == AppendOp) ||
            (proc == ArithOp) || (proc == BinreadOp) || (proc == ClearOp) || (proc == DeleteOp) || (proc == DupOp) ||
            (proc == IndexOp) || (proc == LengthOp) || (proc == MergeOp) || (proc == OffsetOp) ||
            (proc == PopulateOp) || (proc == RandomOp) || (proc == RangeOp) || (proc == SearchOp) || (proc == SeqOp) ||
            (proc == SetOp) || (proc == SplitOp) || (proc == TypeOp) || (proc == VariableOp));
}

/**
 * @ingroup vector_command_paths
 * @brief Validate and dispatch one vector instance operation.
 * @param[in] clientData Live VectorObject owned by the instance command; retained while its handler runs.
 * @param[in] interp Owning interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Full Tcl command argument count, including command and operation names.
 * @param[in] objv Borrowed argument array; objv[0] is the command and objv[1] selects the operation.
 * @return Handler status on successful dispatch; TCL_ERROR for dispatch, type or protection errors.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * clientData is the live receiver. Resolve operation spelling and arity before
 * checking complex support, then reject protected receiver writes according to
 * the handler and argument count. Source-only operations check destinations in
 * their own handlers. Reset the shared inclusive selection to the whole vector
 * only after these checks succeed. Preserve the interpreter and receiver while
 * calling the handler, release both, and return its status. Preservation retains
 * the record, not storage after destruction; handlers must revalidate after
 * callbacks. Rejected dispatch does not reset a prior selection.
 */
int Rbc_VectorInstanceObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject *vPtr;
    int index;

    vPtr = clientData;
    if (Rbc_GetOpIndexFromObj(interp, vectorInstOpCmd, (Tcl_Size)sizeof(vectorInstOpCmd[0]), RBC_OP_ARG1, objc, objv,
                              &index) != TCL_OK) {
        return TCL_ERROR;
    }
    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        RbcVectorCmdOp *proc;

        proc = vectorInstOpCmd[index].proc;
        if (!ComplexOpSupported(proc)) {
            Tcl_SetObjResult(
                interp, Tcl_ObjPrintf("operation \"%s\" is not supported for complex vectors", Tcl_GetString(objv[1])));
            return TCL_ERROR;
        }
    }
    /* Check only operations that write this vector, not read-only sources. */
    {
        RbcVectorCmdOp *proc = vectorInstOpCmd[index].proc;
        if ((proc == AppendOp) || (proc == BinreadOp) || (proc == DeleteOp) ||
            (proc == ExprOp) || (proc == MergeOp) || (proc == RandomOp) ||
            (proc == SeqOp) || (proc == SetOp) || (proc == SortOp) ||
            (proc == Rbc_VectorFftfreqOp) || (proc == Rbc_VectorWindowOp) ||
            ((proc == IndexOp) && (objc == 4)) ||
            (((proc == LengthOp) || (proc == OffsetOp)) && (objc == 3))) {
            if (Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    /*
     * Reset the selected region before each instance operation.
     */
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;
    {
        int result;
        Tcl_Preserve(interp);
        Tcl_Preserve(vPtr);
        result = vectorInstOpCmd[index].proc(vPtr, interp, objc, objv);
        Tcl_Release(vPtr);
        Tcl_Release(interp);
        return result;
    }
}

/**
 * @ingroup vector_command_paths
 * @brief Query or set reversible vector data protection.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 2 for a query, or 3 to set protection.
 * @param[in] objv Borrowed full command argument array. Optional objv[2] is the new Tcl boolean.
 * @return TCL_OK with the current protection boolean, or TCL_ERROR for invalid boolean input.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * With objv[2], parse a Tcl boolean before setting the flag; invalid input
 * leaves the flag unchanged. Return the current boolean state for both forms.
 * Changing protection does not change values, length or type, flush the array,
 * or notify clients. Unlocking is allowed; this is not a lifetime/ownership lock.
 * The dispatcher preserves the live receiver for this handler.
 */
static int ReadOnlyOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int readOnly;

    if (objc == 3) {
        if (Tcl_GetBooleanFromObj(interp, objv[2], &readOnly) != TCL_OK) {
            return TCL_ERROR;
        }
        Rbc_VectorSetReadOnly(interp, (Rbc_Vector *)vPtr, readOnly);
    }
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(vPtr->readOnly));
    return TCL_OK;
}

/**
 * @ingroup vector_command_paths
 * @brief Return the receiver numeric type without converting storage.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 2; validated by the dispatcher.
 * @param[in] objv Borrowed full command argument array. No arguments after the operation name; this handler does
 *     not inspect the array.
 * @return TCL_OK with real or complex; an invalid internal type is a fatal invariant violation.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * Return real or complex as a Tcl string. Requires a live receiver and performs
 * no mutation, cache flush or client notification. An unrecognized internal type
 * is an invariant violation handled by Tcl_Panic, not a user conversion error.
 */
static int TypeOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    const char *typeName;

    (void)objc;
    (void)objv;
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        typeName = "real";
        break;
    case RBC_VECTOR_COMPLEX:
        typeName = "complex";
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)vPtr->type);
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(typeName, -1));
    return TCL_OK;
}

/**
 * @brief Append vector ranges or Tcl lists in argument order.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc At least 3.
 * @param[in] objv Borrowed full command argument array. Entries from objv[2] are vector references or Tcl lists,
 *     processed in order.
 * @return TCL_OK after all appends, or TCL_ERROR; earlier successful arguments remain committed.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_command_paths
 *
 * Vector lookup precedes list conversion. Earlier arguments remain appended
 * if a later argument fails. Reports the appended range when length grew,
 * including the error path; otherwise successful calls report a full update.
 * Flushes the mapped cache when configured. Notification may execute callbacks.
 */
static int AppendOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size oldLength = vPtr->length;
    Tcl_Size i;
    int result = TCL_OK;
    int changed = FALSE;

    for (i = 2; i < objc; i++) {
        VectorObject *srcPtr =
            Rbc_VectorParseElement(NULL, vPtr->dataPtr, Tcl_GetString(objv[i]), NULL, NS_SEARCH_BOTH);
        if (CheckCommandDestination(vPtr) != TCL_OK) {
            result = TCL_ERROR;
            break;
        }
        if (srcPtr != NULL) {
            result = AppendVector(vPtr, srcPtr);
        } else {
            Tcl_Size count;
            Tcl_Obj **values;
            result = Tcl_ListObjGetElements(interp, objv[i], &count, &values);
            if (result == TCL_OK) {
                result = AppendList(vPtr, count, values);
            }
        }
        if (result != TCL_OK) {
            break;
        }
        changed = TRUE;
        if (i + 1 < objc && CheckCommandDestination(vPtr) != TCL_OK) {
            result = TCL_ERROR;
            break;
        }
    }
    if (changed) {
        NotifyCommandWrite(vPtr, oldLength);
    }
    return result;
}

/**
 * @brief Return element-wise arithmetic as a Tcl list without assigning it.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 3; validated by the dispatcher.
 * @param[in] objv Borrowed full command argument array. objv[1] selects +, -, * or /; objv[2] is a vector
 *     reference or scalar operand.
 * @return TCL_OK with a list of copied arithmetic results, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_numeric_paths
 *
 * Resolves a vector operand before scalar conversion. A vector selection must
 * match receiver length; scalars are broadcast. Produces complex pairs if
 * either operand is complex, otherwise doubles. Real arithmetic uses native
 * double operators; complex division explicitly rejects a zero divisor.
 * Returns a Tcl status. Rechecks receiver lifetime after operand evaluation;
 * arithmetic then reads current storage without callbacks. Owns the result list
 * through error cleanup, including complex division failure.
 */
static int ArithOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv) {
    VectorObject *v2Ptr;
    Tcl_Obj *listObjPtr;
    const char *operatorString;
    int operator;
    Tcl_Size i;

    operatorString = Tcl_GetString(objv[1]);
    operator= operatorString[0];
    v2Ptr = Rbc_VectorParseElement(NULL, vPtr->dataPtr, Tcl_GetString(objv[2]), NULL, NS_SEARCH_BOTH);
    if (CheckCommandSource(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    listObjPtr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(listObjPtr);
    if (v2Ptr != NULL) {
        Tcl_Size j;
        Tcl_Size length;
        int complexResult;

        length = v2Ptr->last - v2Ptr->first + 1;
        if (length != vPtr->length) {
            Rbc_AppendResultStrings(interp, "vectors \"", Tcl_GetString(objv[0]), "\" and \"", Tcl_GetString(objv[2]),
                                    "\" are not the same length", (char *)NULL);
            Tcl_DecrRefCount(listObjPtr);
            return TCL_ERROR;
        }
        complexResult = ((vPtr->type == RBC_VECTOR_COMPLEX) || (v2Ptr->type == RBC_VECTOR_COMPLEX));
        if (!complexResult) {
            double value;

            /*
             * Keep the original real arithmetic path unchanged.
             */
            switch (operator) {
            case '*':
                for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                    value = vPtr->data.real[i] * v2Ptr->data.real[j];
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '/':
                for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                    value = vPtr->data.real[i] / v2Ptr->data.real[j];
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '-':
                for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                    value = vPtr->data.real[i] - v2Ptr->data.real[j];
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '+':
                for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                    value = vPtr->data.real[i] + v2Ptr->data.real[j];
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            }
        } else {
            for (i = 0, j = v2Ptr->first; i < vPtr->length; i++, j++) {
                Rbc_Complex a;
                Rbc_Complex b;
                Rbc_Complex result;

                a = Rbc_VectorValueAsComplex(vPtr, i);
                b = Rbc_VectorValueAsComplex(v2Ptr, j);
                if (ComplexArithmetic(interp, operator, a, b, &result) != TCL_OK) {
                    Tcl_DecrRefCount(listObjPtr);
                    return TCL_ERROR;
                }
                Tcl_ListObjAppendElement(interp, listObjPtr, Rbc_NewComplexObj(result));
            }
        }
    } else {
        Rbc_Complex scalar;
        int scalarIsComplex;
        int complexResult;

        if (GetArithmeticScalar(interp, objv[2], &scalar, &scalarIsComplex) != TCL_OK) {
            Tcl_DecrRefCount(listObjPtr);
            return TCL_ERROR;
        }
        if (CheckCommandSource(vPtr) != TCL_OK) {
            Tcl_DecrRefCount(listObjPtr);
            return TCL_ERROR;
        }
        complexResult = ((vPtr->type == RBC_VECTOR_COMPLEX) || scalarIsComplex);
        if (!complexResult) {
            double value;
            double realScalar;

            realScalar = scalar.real;
            /*
             * Again preserve the existing real implementation.
             */
            switch (operator) {
            case '*':
                for (i = 0; i < vPtr->length; i++) {
                    value = vPtr->data.real[i] * realScalar;
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '/':
                for (i = 0; i < vPtr->length; i++) {
                    value = vPtr->data.real[i] / realScalar;
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '-':
                for (i = 0; i < vPtr->length; i++) {
                    value = vPtr->data.real[i] - realScalar;
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            case '+':
                for (i = 0; i < vPtr->length; i++) {
                    value = vPtr->data.real[i] + realScalar;
                    Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(value));
                }
                break;
            }
        } else {
            for (i = 0; i < vPtr->length; i++) {
                Rbc_Complex a;
                Rbc_Complex result;

                a = Rbc_VectorValueAsComplex(vPtr, i);
                if (ComplexArithmetic(interp, operator, a, scalar, &result) != TCL_OK) {
                    Tcl_DecrRefCount(listObjPtr);
                    return TCL_ERROR;
                }
                Tcl_ListObjAppendElement(interp, listObjPtr, Rbc_NewComplexObj(result));
            }
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    Tcl_DecrRefCount(listObjPtr);
    return TCL_OK;
}

/*
 * Types and helper functions (for Tcl_ParseArgsObjv) for "binread" vector
 * instance command
 */
struct NativeFmtValue {
    int size;
    enum NativeFormats format;
};

/**
 * @brief Select a supported native scalar type from a format and byte width.
 * @param[in] clientData Unused callback data; no ownership transfer.
 * @param[in] interp Diagnostic/evaluation interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Number of remaining option-value arguments; zero reports a missing value.
 * @param[in] objv Borrowed option-value slice; objv[0] is the value, not the option name.
 * @param[out] dstPtr Required NativeFmtValue output, written only for a supported native representation.
 * @return One consumed argument on success, or -1 on failure; not a Tcl status code.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_command_paths
 *
 * Floating, signed and unsigned formats resolve against native C sizeof
 * values. Returns the consumed argument count, or -1 on error. This is host representation, not a portable
 * serialized type description; conversion to double may lose integer precision.
 */
static Tcl_Size ParseFormat(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv, void *dstPtr) {
    const struct NativeFmtValue *table, *cursor;
    int size;
    Tcl_Size len;
    const char *string;

    (void)clientData;
    static const struct NativeFmtValue rtable[] = {
        {sizeof(float), FMT_FLOAT}, {sizeof(double), FMT_DOUBLE}, {-1, FMT_UNKNOWN}};
    static const struct NativeFmtValue itable[] = {
        {sizeof(char), FMT_CHAR}, {sizeof(short), FMT_SHORT},        {sizeof(int), FMT_INT},
        {sizeof(long), FMT_LONG}, {sizeof(long long), FMT_LONGLONG}, {-1, FMT_UNKNOWN}};
    static const struct NativeFmtValue utable[] = {{sizeof(unsigned char), FMT_UCHAR},
                                                   {sizeof(unsigned short), FMT_USHORT},
                                                   {sizeof(unsigned int), FMT_UINT},
                                                   {sizeof(unsigned long), FMT_ULONG},
                                                   {sizeof(unsigned long long), FMT_ULONGLONG},
                                                   {-1, FMT_UNKNOWN}};
    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("option \"-format\" requires an additional argument", -1));
        return -1;
    }
    string = Tcl_GetStringFromObj(objv[0], &len);
    if (len < 2) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown binary format \"%s\"", string));
        return -1;
    }

    Tcl_Obj *sizeObjPtr;
    int result;

    sizeObjPtr = Tcl_NewStringObj(string + 1, len - 1);
    Tcl_IncrRefCount(sizeObjPtr);
    result = Tcl_GetIntFromObj(NULL, sizeObjPtr, &size);
    Tcl_DecrRefCount(sizeObjPtr);
    if (result != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown binary format \"%s\": incorrect byte size", string));
        return -1;
    }
    switch (tolower(string[0])) {
    case 'r':
    case 'f':
        table = rtable;
        break;
    case 'i':
        table = itable;
        break;
    case 'u':
        table = utable;
        break;
    default:
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown binary format \"%s\": "
                                               "should be i#, r#, or u#, "
                                               "where # is the size in bytes",
                                               string));
        return -1;
    }
    for (cursor = table; cursor->size != -1; cursor++) {
        if (cursor->size == size) {
            *(struct NativeFmtValue *)dstPtr = *cursor;
            return 1;
        }
    }
    Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't handle format \"%s\"", string));
    return -1;
}

/**
 * @brief Parse a binary-input starting storage position.
 * @param[in] clientData Borrowed VectorObject retained by the binread caller through index evaluation.
 * @param[in] interp Diagnostic/evaluation interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Number of remaining option-value arguments; zero reports a missing value.
 * @param[in] objv Borrowed option-value slice; objv[0] is the value, not the option name.
 * @param[out] dstPtr Required Tcl_Size output receiving a zero-based storage position, written only on success.
 * @return One consumed argument on success, or -1 on failure; not a Tcl status code.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_command_paths
 *
 * Uses vector index conversion, allowing positions from zero through length
 * inclusive. Returns the consumed argument count, or -1 on error.
 * Index expressions may evaluate Tcl.
 */
static Tcl_Size ParseAt(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const *objv, void *dstPtr) {
    VectorObject *vPtr = (VectorObject *)clientData;
    const char *string;
    Tcl_Size first;

    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("option \"-at\" requires an additional argument", -1));
        return -1;
    }
    string = Tcl_GetString(objv[0]);
    if (Rbc_VectorGetIndex(interp, vPtr, string, &first, 0, (Rbc_VectorIndexProc **)NULL) != TCL_OK) {
        return -1;
    }
    if (first > vPtr->length || first < 0) {
        Rbc_AppendResultStrings(interp, "index \"", string, "\" is out of range", NULL);
        return -1;
    }
    *(Tcl_Size *)dstPtr = first;
    return 1;
}
/**
 * @brief Read native-format channel scalars into vector storage.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc At least 3.
 * @param[in] objv Borrowed full command argument array. objv[2] names the channel; later entries contain the
 *     optional count and binary-input switches.
 * @return TCL_OK with the logical value count read, or TCL_ERROR; earlier complete chunks may remain committed.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_command_paths
 *
 * Retains a readable channel, temporarily sets binary translation, and restores
 * the saved translation if the caller has not closed the channel. Real values use one scalar, complex values
 * two interleaved scalars. A positive count performs one read; zero reads
 * chunks until EOF or a zero-byte read. Rejects incomplete values rather than
 * buffering partial records. Completed chunks remain on error and trigger cache
 * flushing/notification while preserving the original error state. Success
 * reports logical value count. Checks destination lifetime/read-only state and
 * channel registration after reflected reads. The dispatcher retains the vector.
 */
static int BinreadOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Channel channel = NULL;
    int retained = FALSE;
    const char *channelName;
    const char *string;
    char *byteArr;
    enum NativeFormats fmt;
    struct NativeFmtValue nfmt;
    Tcl_DString translation;
    Tcl_InterpState savedState;
    Tcl_Size count;
    Tcl_Size first;
    Tcl_Size total;
    Tcl_Size optionStart;
    Tcl_Size argc;
    Tcl_Size bufferValues;
    Tcl_Size bufferBytes;
    Tcl_Size bytesRead;
    Tcl_Size length;
    Tcl_Size newTotal;
    size_t bufferByteCount;
    Tcl_Size componentsPerValue;
    Tcl_Size bytesPerValue;
    int mode;
    int size;
    int swap;
    int translationChanged;
    int result;

    byteArr = NULL;
    savedState = NULL;
    count = 0;
    first = vPtr->length;
    total = 0;
    optionStart = 3;
    swap = FALSE;
    translationChanged = FALSE;
    result = TCL_ERROR;
    nfmt.size = sizeof(double);
    nfmt.format = FMT_DOUBLE;
    Tcl_DStringInit(&translation);
    channelName = Tcl_GetString(objv[2]);
    channel = Tcl_GetChannel(interp, channelName, &mode);
    if (channel == NULL) {
        goto cleanup;
    }
    if ((mode & TCL_READABLE) == 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("channel \"%s\" wasn't opened for reading", channelName));
        goto cleanup;
    }
    Tcl_RegisterChannel(NULL, channel);
    retained = TRUE;
    /*
     * An optional count precedes the options. A zero count retains the
     * historical meaning of reading until EOF.
     */
    if (optionStart < objc) {
        string = Tcl_GetString(objv[optionStart]);
        if (string[0] != '-') {
            if (Tcl_GetSizeIntFromObj(interp, objv[optionStart], &count) != TCL_OK) {
                goto cleanup;
            }
            if (count < 0) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("count can't be negative", -1));
                goto cleanup;
            }
            optionStart++;
        }
    }
    /*
     * Tcl_ParseArgsObjv() treats its first argument as the command name.
     * Use either the channel name or the optional count as that ignored
     * argument, then parse the remaining options.
     */
    argc = objc - optionStart + 1;
    if (argc > 1) {
        const Tcl_ArgvInfo binreadOpts[] = {{TCL_ARGV_CONSTANT, "-swap", (void *)TRUE, &swap, NULL, NULL},
                                            {TCL_ARGV_GENFUNC, "-format", ParseFormat, &nfmt, NULL, NULL},
                                            {TCL_ARGV_GENFUNC, "-at", ParseAt, &first, NULL, vPtr},
                                            TCL_ARGV_TABLE_END};
        if (Tcl_ParseArgsObjv(interp, binreadOpts, &argc, objv + optionStart - 1, NULL) != TCL_OK) {
            goto cleanup;
        }
    }
    if (CheckCommandDestination(vPtr) != TCL_OK) {
        goto cleanup;
    }
    if (!Tcl_IsChannelRegistered(interp, channel)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("channel was closed during command evaluation", -1));
        goto cleanup;
    }
    fmt = nfmt.format;
    size = nfmt.size;
    if ((fmt == FMT_UNKNOWN) || (size <= 0)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid binary format", -1));
        goto cleanup;
    }
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        componentsPerValue = 1;
        break;
    case RBC_VECTOR_COMPLEX:
        componentsPerValue = 2;
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)vPtr->type);
        goto cleanup;
    }
    if (MultiplyVectorSizes(interp, (Tcl_Size)size, componentsPerValue, &bytesPerValue) != TCL_OK) {
        goto cleanup;
    }
    /*
     * With no explicit count, read BUFFER_SIZE values per iteration.
     * With a count, allocate enough space for that one requested read.
     */
    bufferValues = (count == 0) ? (Tcl_Size)BUFFER_SIZE : count;
    if (GetArrayByteCount(interp, bufferValues, (size_t)bytesPerValue, &bufferByteCount) != TCL_OK) {
        goto cleanup;
    }
    /*
     * Tcl_Read() accepts its byte count as Tcl_Size.
     */
    if (bufferByteCount > (size_t)TCL_SIZE_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("binary read size is too large", -1));
        goto cleanup;
    }
    bufferBytes = (Tcl_Size)bufferByteCount;
    byteArr = Tcl_AttemptAlloc(bufferByteCount);
    if ((byteArr == NULL) && (bufferByteCount > 0)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate binary input buffer", -1));
        goto cleanup;
    }
    /*
     * Preserve the channel's original translation setting.
     */
    if (Tcl_GetChannelOption(interp, channel, "-translation", &translation) != TCL_OK) {
        goto cleanup;
    }
    if (Tcl_SetChannelOption(interp, channel, "-translation", "binary") != TCL_OK) {
        goto cleanup;
    }
    translationChanged = TRUE;
    for (;;) {
        if (CheckCommandDestination(vPtr) != TCL_OK) {
            goto cleanup;
        }
        if (!Tcl_IsChannelRegistered(interp, channel)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("channel was closed during command evaluation", -1));
            goto cleanup;
        }
        bytesRead = Tcl_Read(channel, byteArr, bufferBytes);
        if (CheckCommandDestination(vPtr) != TCL_OK) {
            goto cleanup;
        }
        if (!Tcl_IsChannelRegistered(interp, channel)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("channel was closed during command evaluation", -1));
            goto cleanup;
        }
        if (bytesRead < 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("error reading channel: %s", Tcl_PosixError(interp)));
            goto cleanup;
        }
        /*
         * This also prevents an infinite loop on a nonblocking channel
         * that currently has no input available.
         */
        if (bytesRead == 0) {
            break;
        }
        if ((bytesRead % bytesPerValue) != 0) {
            if (vPtr->type == RBC_VECTOR_COMPLEX) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("error reading channel: "
                                                          "input ended inside a complex binary value",
                                                          -1));
            } else {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("error reading channel: "
                                                          "input ended inside a binary value",
                                                          -1));
            }
            goto cleanup;
        }
        length = bytesRead / bytesPerValue;
        if (AddVectorSizes(interp, total, length, &newTotal) != TCL_OK) {
            goto cleanup;
        }
        switch (vPtr->type) {
        case RBC_VECTOR_REAL:
            if (CopyValues(vPtr, byteArr, fmt, size, length, swap, &first) != TCL_OK) {
                goto cleanup;
            }
            break;
        case RBC_VECTOR_COMPLEX:
            if (CopyComplexValues(vPtr, byteArr, fmt, size, length, swap, &first) != TCL_OK) {
                goto cleanup;
            }
            break;
        default:
            Tcl_Panic("bad vector type %d", (int)vPtr->type);
            goto cleanup;
        }
        total = newTotal;
        /*
         * An explicit count requests at most one Tcl_Read(). This
         * preserves the existing behavior for blocking and nonblocking
         * channels.
         */
        if (count > 0) {
            break;
        }
        if (Tcl_Eof(channel)) {
            break;
        }
    }
    result = TCL_OK;

cleanup:
    /* Keep the complete original error across restoration, channel finalizers,
     * cache traces and notifications. Partial successful chunks remain valid. */
    if (result != TCL_OK) {
        savedState = Tcl_SaveInterpState(interp, result);
    }
    if (translationChanged && !Tcl_InterpDeleted(interp) && Tcl_IsChannelRegistered(interp, channel)) {
        if (Tcl_SetChannelOption(interp, channel, "-translation", Tcl_DStringValue(&translation)) != TCL_OK &&
            result == TCL_OK) {
            result = TCL_ERROR;
            savedState = Tcl_SaveInterpState(interp, result);
        }
    }
    if (retained) {
        Tcl_InterpState state = Tcl_SaveInterpState(interp, result);
        Tcl_UnregisterChannel(NULL, channel);
        Tcl_RestoreInterpState(interp, state);
    }
    Tcl_DStringFree(&translation);
    if (byteArr != NULL) {
        ckfree(byteArr);
    }
    if (savedState != NULL) {
        Tcl_RestoreInterpState(interp, savedState);
    }
    if (total > 0 || result == TCL_OK) {
        NotifyCommandWrite(vPtr, -1);
    }
    if (result == TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)total));
    }
    return result;
}
/**
 * @ingroup vector_command_paths
 * @brief Flush the mapped-array cache without emptying vector storage.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 2; validated by the dispatcher.
 * @param[in] objv Borrowed full command argument array. No arguments after the operation name; this handler does
 *     not inspect the array.
 * @return TCL_OK after requesting cache flushing; lower-level variable errors are not propagated.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * Delegate to Rbc_VectorFlushCache even when automatic flushing is disabled.
 * Read-only vectors are accepted because this is not a numerical write. The
 * helper may invoke user variable traces, whose side effects are not undone.
 * Return TCL_OK after the void helper; do not access the receiver afterward or
 * claim that cache recreation errors are propagated. No value notification is
 * requested by this handler.
 */
static int ClearOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_VectorFlushCache(vPtr);
    return TCL_OK;
}

/**
 * @brief Delete selected values by bitmap compaction, or destroy the receiver.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc At least 3 through normal dispatch; the internal two-argument branch destroys the receiver.
 * @param[in] objv Borrowed full command argument array. Entries from objv[2] are inclusive index/range selections
 *     to remove.
 * @return TCL_OK after compaction or internal destruction, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_edit_paths
 *
 * With no indices, destroys the vector. Otherwise parses inclusive ranges into
 * a bitmap, then compacts surviving real or complex entries in place and
 * reduces length without shrinking capacity. Flushes when configured and
 * reports a full update. Returns a Tcl status. Index expressions may run Tcl;
 * rejects length changes, destroyed/read-only receivers and invalid selections
 * before accessing the original bitmap. Callback side effects are not undone.
 */
static int DeleteOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    unsigned char *unsetArr;
    Tcl_Size i;
    Tcl_Size valueIndex, j;
    Tcl_Size count;
    char *string;
    Tcl_Size bitmapSize;
    size_t bitmapBytes;
    Tcl_Size originalLength = vPtr->length;

    /* FIXME: Don't delete vector with no indices.  */
    if (objc == 2) {
        Rbc_VectorFree(vPtr);
        return TCL_OK;
    }
    /*
     * Allocate an "unset" bitmap the size of the vector.
     */
    /*
     * One bit per vector element.  Avoid "(length + 7) / 8", since
     * the addition can overflow Tcl_Size before the division.
     */
    bitmapSize = vPtr->length / 8;
    if ((vPtr->length & 7) != 0) {
        bitmapSize++;
    }
    if (GetArrayByteCount(interp, bitmapSize, sizeof(*unsetArr), &bitmapBytes) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Keep one byte for the empty-vector case so the existing cleanup
     * path can remain unconditional.
     */
    if (bitmapBytes == 0) {
        bitmapBytes = 1;
    }
    unsetArr = ckalloc(bitmapBytes);
    memset(unsetArr, 0, bitmapBytes);

#define SetBit(i) unsetArr[(i) >> 3] |= (1 << ((i) & 0x07))
#define GetBit(i) (unsetArr[(i) >> 3] & (1 << ((i) & 0x07)))

    for (i = 2; i < objc; i++) {
        string = Tcl_GetString(objv[i]);
        if (Rbc_VectorGetIndexRange(interp, vPtr, string, (INDEX_COLON | INDEX_CHECK), (Rbc_VectorIndexProc **)NULL) !=
            TCL_OK) {
            ckfree((char *)unsetArr);
            return TCL_ERROR;
        }
        if (CheckCommandDestination(vPtr) != TCL_OK) {
            ckfree(unsetArr);
            return TCL_ERROR;
        }
        if (vPtr->length != originalLength) {
            ckfree(unsetArr);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector length changed during command evaluation", -1));
            return TCL_ERROR;
        }
        if (originalLength == 0) continue;
        if (CheckCommandRange(vPtr, vPtr->first, vPtr->last) != TCL_OK) {
            ckfree(unsetArr);
            return TCL_ERROR;
        }
        for (j = vPtr->first; j <= vPtr->last; j++) {
            SetBit(j); /* Mark the range of elements for deletion. */
        }
    }
    count = 0;
    for (valueIndex = 0; valueIndex < vPtr->length; valueIndex++) {
        if (GetBit(valueIndex)) {
            continue;
        }
        if (count < valueIndex) {
            switch (vPtr->type) {
            case RBC_VECTOR_REAL:
                vPtr->data.real[count] = vPtr->data.real[valueIndex];
                break;
            case RBC_VECTOR_COMPLEX:
                vPtr->data.complex[count] = vPtr->data.complex[valueIndex];
                break;
            default:
                Tcl_Panic("bad vector type %d", (int)vPtr->type);
            }
        }
        count++;
    }
    ckfree((char *)unsetArr);
    FinishCommandWrite(vPtr, vPtr->data, count, vPtr->size);
    NotifyCommandWrite(vPtr, -1);
    return TCL_OK;
}

/**
 * @brief Copy the receiver into a named destination.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 3; validated by the dispatcher.
 * @param[in] objv Borrowed full command argument array. objv[2] is the literal output vector name.
 * @return TCL_OK after copying or a self-copy no-op, otherwise TCL_ERROR; publication may fail after commit.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_edit_paths
 *
 * Creates a missing destination with the source type; skips the receiver itself.
 * Copies the source selection and offset into prepared storage before releasing
 * the previous allocation. Retains destination identity through mapping and
 * notification callbacks. Returns a Tcl status. The command table permits one
 * destination; source data need not be snapshotted because no callback occurs
 * before the copy completes.
 */
static int DupOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject *dest;
    VectorData data;
    Tcl_Size first = vPtr->first, length = vPtr->last - vPtr->first + 1, capacity;
    size_t bytes, elementSize = vPtr->type == RBC_VECTOR_REAL ? sizeof(double) : sizeof(Rbc_Complex);
    int isNew, result = TCL_ERROR;

    (void)objc;
    if (GetArrayByteCount(interp, length, elementSize, &bytes) != TCL_OK) {
        return TCL_ERROR;
    }
    dest = GetCommandOutput(vPtr, Tcl_GetString(objv[2]), &isNew);
    if (dest == NULL) {
        return TCL_ERROR;
    }
    if (dest == vPtr) {
        result = TCL_OK;
        goto done;
    }
    if (PrepareCommandResize(dest, length, &data, &capacity) != TCL_OK) {
        goto done;
    }
    if (bytes > 0) {
        memcpy(data.raw, (char *)vPtr->data.raw + (size_t)first * elementSize, bytes);
    }
    dest->offset = vPtr->offset;
    FinishCommandWrite(dest, data, length, capacity);
    result = PublishCommandOutput(dest, isNew);
done:
    Tcl_Release(dest);
    return result;
}

/**
 * @ingroup vector_command_paths
 * @brief Evaluate an expression into the existing receiver, then publish.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 3; validated by the dispatcher.
 * @param[in] objv Borrowed full command argument array. objv[2] is the vector expression assigned to the existing
 *     receiver.
 * @return TCL_OK after assignment and publication, otherwise TCL_ERROR; no result vector is created.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * Pass objv[2] to Rbc_ExprVector with the receiver as destination. Evaluation
 * uses temporary result storage and validates destination survival, writability
 * and compatible result type before assignment. Propagate an evaluation or
 * assignment error without calling NotifyCommandWrite. On success, request a
 * full update through that callback-aware helper and return TCL_OK. This path
 * does not create a named result vector or return its values as a Tcl list;
 * expression and notification callback side effects remain observable.
 */
static int ExprOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (Rbc_ExprVector(interp, Tcl_GetString(objv[2]), (Rbc_Vector *)vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    NotifyCommandWrite(vPtr, -1);
    return TCL_OK;
}

/**
 * @brief Read an inclusive selection or fill it with a scalar value.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 3 to read, or 4 to assign.
 * @param[in] objv Borrowed full command argument array. objv[2] is the index/range; optional objv[3] is the real
 *     scalar or complex pair to replicate.
 * @return TCL_OK with a copied list for reads or the original value object for writes; TCL_ERROR on failure.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_edit_paths
 *
 * Reads return a Tcl list, including one-element selections. Writes convert a
 * real scalar or complex pair and return the original input object. The append
 * position may grow storage by one value but cannot be read. Reports a ranged
 * update for existing real writes or recognized appends, otherwise a full
 * update. Rechecks lifetime, writability and saved bounds after conversion;
 * append follows the current end. Complete data precedes custom storage release.
 * Cache flushing and notification retain identity and preserve the Tcl result.
 */
static int IndexOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size first, last, length, size, i;
    VectorData data;
    Rbc_Complex value;
    int appendWrite, ranged;

    if (Rbc_VectorGetIndexRange(interp, vPtr, Tcl_GetString(objv[2]), INDEX_ALL_FLAGS, NULL) != TCL_OK ||
        CheckCommandSource(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    first = vPtr->first;
    last = vPtr->last;
    length = vPtr->length;
    appendWrite = first == length && last == length;
    if (objc == 3) {
        if (appendWrite) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't get index \"%s\"", Tcl_GetString(objv[2])));
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Rbc_GetValues(vPtr, first, last));
        return TCL_OK;
    }
    if (first == SPECIAL_INDEX) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't set index \"%s\"", Tcl_GetString(objv[2])));
        return TCL_ERROR;
    }
    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        if (Rbc_GetComplex(interp, objv[3], &value) != TCL_OK) {
            return TCL_ERROR;
        }
    } else if (Rbc_GetDouble(interp, objv[3], &value.real) != TCL_OK) {
        return TCL_ERROR;
    }
    if (CheckCommandDestination(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (appendWrite) {
        /* The append position follows the current end after conversion. */
        first = last = vPtr->length;
        if (AddVectorSizes(interp, vPtr->length, 1, &length) != TCL_OK ||
            PrepareCommandWrite(vPtr, length, &data, &size) != TCL_OK) {
            return TCL_ERROR;
        }
        if (vPtr->type == RBC_VECTOR_REAL) {
            data.real[first] = value.real;
        } else {
            data.complex[first] = value;
        }
        FinishCommandWrite(vPtr, data, length, size);
    } else {
        if (CheckCommandRange(vPtr, first, last) != TCL_OK) {
            return TCL_ERROR;
        }
        if (vPtr->type == RBC_VECTOR_REAL) {
            Rbc_ReplicateValue(vPtr, first, last, value.real);
        } else {
            for (i = first; i <= last; i++) {
                vPtr->data.complex[i] = value;
            }
        }
    }
    Tcl_SetObjResult(interp, objv[3]);
    ranged = vPtr->type == RBC_VECTOR_REAL || appendWrite;
    /* Retain exact ranges for the ordinary real write fast path. Flush traces
     * may change length or destroy the record, so recheck before notifying. */
    {
        Tcl_InterpState state = Tcl_SaveInterpState(interp, TCL_OK);
        if (!Tcl_InterpDeleted(interp) && !vPtr->destroying) {
            if (vPtr->flush) {
                Rbc_VectorFlushCache(vPtr);
            }
            if (!Tcl_InterpDeleted(interp) && !vPtr->destroying) {
                if (ranged && last < vPtr->length) {
                    Rbc_VectorUpdateClientsRange(vPtr, first, last);
                } else {
                    Rbc_VectorUpdateClients(vPtr);
                }
            }
        }
        Tcl_RestoreInterpState(interp, state);
    }
    return TCL_OK;
}

/**
 * @brief Query or resize the receiver and return its resulting length.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 2 to query, or 3 to resize.
 * @param[in] objv Borrowed full command argument array. Optional objv[2] is the new nonnegative logical length.
 * @return TCL_OK with the queried or requested length, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_edit_paths
 *
 * Accepts a nonnegative Tcl_Size, reuses capacity or prepares larger storage,
 * and zero-fills new entries before committing. Returns the requested length
 * even if a post-commit callback destroys the vector. Flushes/notifies only a
 * still-live identity, preserving the result across callbacks.
 */
static int LengthOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        Tcl_Size size, capacity;
        VectorData data;
        size_t elementSize = vPtr->type == RBC_VECTOR_REAL ? sizeof(double) : sizeof(Rbc_Complex);
        if (Tcl_GetSizeIntFromObj(interp, objv[2], &size) != TCL_OK) {
            return TCL_ERROR;
        }
        if (size < 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad vector size \"%s\"", Tcl_GetString(objv[2])));
            return TCL_ERROR;
        }
        if (PrepareCommandResize(vPtr, size, &data, &capacity) != TCL_OK) {
            return TCL_ERROR;
        }
        if (size > vPtr->length) {
            memset((char *)data.raw + (size_t)vPtr->length * elementSize, 0,
                   (size_t)(size - vPtr->length) * elementSize);
        }
        FinishCommandWrite(vPtr, data, size, capacity);
        Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)size));
        NotifyCommandWrite(vPtr, -1);
        return TCL_OK;
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)vPtr->length));
    return TCL_OK;
}

/**
 * @brief Interleave equal-length, same-type source selections into the receiver.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc At least 3.
 * @param[in] objv Borrowed full command argument array. Entries from objv[2] are same-type vector references with
 *     equal selected lengths.
 * @return TCL_OK after interleaving and publication, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_edit_paths
 *
 * Resolves sources and saves their first indices, then builds the complete
 * interleaved array before replacing destination storage. Retains all resolved
 * source identities and revalidates their selections after the final lookup.
 * Supports self-merge, checks count/byte arithmetic, and frees scratch storage
 * on all exits. Completed data precedes release/flush callbacks. Returns a Tcl status.
 */
static int MergeOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject **vecArr;
    Tcl_Size *firstArr;
    VectorData valueData;
    Tcl_Size nVectors;
    Tcl_Size refSize;
    Tcl_Size nElem;
    Tcl_Size i;
    Tcl_Size j;
    Tcl_Size valueIndex;
    Tcl_Size outIndex;
    Tcl_Size length;
    size_t vectorBytes;
    size_t firstBytes;
    size_t valueBytes;
    size_t elementSize;
    int result;
    Tcl_Size retained = 0;

    vecArr = NULL;
    firstArr = NULL;
    valueData.raw = NULL;
    result = TCL_ERROR;
    nVectors = objc - 2;
    /*
     * "merge" requires at least one source vector according to the
     * operation table, so nVectors should always be positive here.
     */
    if (GetArrayByteCount(interp, nVectors, sizeof(*vecArr), &vectorBytes) != TCL_OK) {
        goto cleanup;
    }
    if (GetArrayByteCount(interp, nVectors, sizeof(*firstArr), &firstBytes) != TCL_OK) {
        goto cleanup;
    }
    vecArr = ckalloc(vectorBytes);
    firstArr = ckalloc(firstBytes);
    refSize = -1;
    /*
     * Resolve and validate every source before modifying the
     * destination.
     */
    for (i = 0; i < nVectors; i++) {
        VectorObject *srcPtr;

        if (Rbc_VectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i + 2]), &srcPtr) != TCL_OK) {
            goto cleanup;
        }
        vecArr[i] = srcPtr;
        Tcl_Preserve(srcPtr);
        retained++;
        if (CheckCommandDestination(vPtr) != TCL_OK) {
            goto cleanup;
        }
        if (srcPtr->type != vPtr->type) {
            Tcl_SetObjResult(interp,
                             Tcl_ObjPrintf("vectors \"%s\" and \"%s\" have different types", vPtr->name, srcPtr->name));
            goto cleanup;
        }
        length = srcPtr->last - srcPtr->first + 1;
        if (refSize < 0) {
            refSize = length;
        } else if (length != refSize) {
            Rbc_AppendResultStrings(interp, "vectors \"", vPtr->name, "\" and \"", srcPtr->name, "\" differ in length",
                                    (char *)NULL);
            goto cleanup;
        }
        vecArr[i] = srcPtr;
        /*
         * Preserve the selected source range. This matters if the
         * destination is itself one of the source vectors.
         */
        firstArr[i] = srcPtr->first;
    }
    for (i = 0; i < nVectors; i++) {
        if (CheckCommandSource(vecArr[i]) != TCL_OK) {
            goto cleanup;
        }
        if (refSize > 0 && CheckCommandRange(vecArr[i], firstArr[i], firstArr[i] + refSize - 1) != TCL_OK) {
            goto cleanup;
        }
    }
    if (CheckCommandDestination(vPtr) != TCL_OK) {
        goto cleanup;
    }
    if (MultiplyVectorSizes(interp, refSize, nVectors, &nElem) != TCL_OK) {
        goto cleanup;
    }
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        elementSize = sizeof(double);
        break;
    case RBC_VECTOR_COMPLEX:
        elementSize = sizeof(Rbc_Complex);
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)vPtr->type);
        goto cleanup;
    }
    if (GetArrayByteCount(interp, nElem, elementSize, &valueBytes) != TCL_OK) {
        goto cleanup;
    }
    if (valueBytes > 0) {
        valueData.raw = ckalloc(valueBytes);
    }
    /*
     * Build the complete result before changing vPtr. This makes
     * "v merge v other" safe.
     */
    outIndex = 0;
    for (valueIndex = 0; valueIndex < refSize; valueIndex++) {
        for (j = 0; j < nVectors; j++) {
            Tcl_Size sourceIndex;

            sourceIndex = firstArr[j] + valueIndex;
            switch (vPtr->type) {
            case RBC_VECTOR_REAL:
                valueData.real[outIndex] = vecArr[j]->data.real[sourceIndex];
                break;
            case RBC_VECTOR_COMPLEX:
                valueData.complex[outIndex] = vecArr[j]->data.complex[sourceIndex];
                break;
            default:
                Tcl_Panic("bad vector type %d", (int)vPtr->type);
            }
            outIndex++;
        }
    }
    /*
     * Only now modify the destination.
     */
    FinishCommandWrite(vPtr, valueData, nElem, nElem);
    valueData.raw = NULL;
    NotifyCommandWrite(vPtr, -1);
    result = TCL_OK;

cleanup:
    for (i = 0; i < retained; i++) {
        Tcl_Release(vecArr[i]);
    }
    if (valueData.raw != NULL) {
        ckfree(valueData.raw);
    }
    if (firstArr != NULL) {
        ckfree(firstArr);
    }
    if (vecArr != NULL) {
        ckfree(vecArr);
    }
    return result;
}

/**
 * @brief Normalize real values by the cached source minimum and range.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 2 for list output, or 3 for vector output.
 * @param[in] objv Borrowed full command argument array. Optional objv[2] is a literal real destination name; no
 *     destination means a copied Tcl list.
 * @return TCL_OK with a list or after output publication, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_numeric_paths
 *
 * Computes (value-min)/(max-min) after refreshing source extrema. Without a
 * destination, returns a Tcl list and does not modify the receiver. Otherwise
 * creates/resizes a real destination, fills it, and flushes/notifies existing
 * outputs. No explicit zero-range or finite-value policy is implemented here.
 * Stages the complete result before output resolution and commits it before
 * invoking storage release or mapping callbacks. Returns a Tcl status.
 */
static int NormalizeOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    double range;

    Rbc_VectorUpdateRange(vPtr);
    range = vPtr->max - vPtr->min;
    if (objc > 2) {
        VectorObject *v2Ptr;
        int isNew;
        char *string;
        VectorData data;
        Tcl_Size length = vPtr->length;
        int result;

        if (AllocateCommandData(vPtr, length, &data) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < length; i++) {
            data.real[i] = (vPtr->data.real[i] - vPtr->min) / range;
        }
        string = Tcl_GetString(objv[2]);
        v2Ptr = GetCommandOutput(vPtr, string, &isNew);
        if (v2Ptr == NULL) {
            ckfree(data.raw);
            return TCL_ERROR;
        }
        FinishCommandWrite(v2Ptr, data, length, length);
        result = PublishCommandOutput(v2Ptr, isNew);
        Tcl_Release(v2Ptr);
        return result;
    } else {
        double norm;
        Tcl_Obj *listObjPtr;

        listObjPtr = Tcl_NewListObj(0, (Tcl_Obj **)NULL);
        for (i = 0; i < vPtr->length; i++) {
            norm = (vPtr->data.real[i] - vPtr->min) / range;
            Tcl_ListObjAppendElement(interp, listObjPtr, Tcl_NewDoubleObj(norm));
        }
        Tcl_SetObjResult(interp, listObjPtr);
    }
    return TCL_OK;
}

/**
 * @brief Query or assign the external numeric index offset.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 2 to query, or 3 to assign.
 * @param[in] objv Borrowed full command argument array. Optional objv[2] is the new external Tcl_Size index
 *     offset.
 * @return TCL_OK with the current offset, or TCL_ERROR for conversion failure.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_edit_paths
 *
 * Parses a Tcl_Size and returns the current offset as an integer. Does not move
 * values, flush array entries or notify observers. The dispatcher treats the
 * setter as a receiver mutation and rejects read-only vectors.
 */
static int OffsetOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc == 3) {
        Tcl_Size value;
        if (Tcl_GetSizeIntFromObj(interp, objv[2], &value) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->offset = value;
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)vPtr->offset));
    return TCL_OK;
}

/**
 * @brief Insert evenly spaced values between neighboring source values.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 4; validated by the dispatcher.
 * @param[in] objv Borrowed full command argument array. objv[2] names the destination; objv[3] is the positive
 *     number of inserted values per interval.
 * @return TCL_OK after output publication, otherwise TCL_ERROR; output mapping can fail after commit.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_edit_paths
 *
 * Requires integer density at least one. For nonempty length N, output length
 * is (N-1)*(density+1)+1 with overflow checks; empty input produces empty output.
 * Builds a complete same-type result before resolving the destination, allowing
 * self-output without a temporary vector object. Interpolates real and imaginary
 * components independently and retains the last endpoint. Publishes only after
 * commit; callbacks see complete data. Returns a Tcl status.
 */
static int PopulateOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject *v2Ptr;
    VectorObject *sourcePtr = vPtr;
    VectorData data;
    int result;
    Tcl_Size density;
    Tcl_Size intervals;
    Tcl_Size valuesPerInterval;
    Tcl_Size populatedValues;
    Tcl_Size newSize;
    Tcl_Size i;
    Tcl_Size j;
    const char *name;
    int isNew;

    (void)objc;
    /*
     * Parse and validate the number of values inserted between each
     * pair of source values.
     */
    if (Tcl_GetSizeIntFromObj(interp, objv[3], &density) != TCL_OK) {
        return TCL_ERROR;
    }
    if (density < 1) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad density \"%s\"", Tcl_GetString(objv[3])));
        return TCL_ERROR;
    }
    /*
     * Calculate:
     *
     *     newSize =
     *         (sourceLength - 1) * (density + 1) + 1
     *
     * Check each operation before performing it, because Tcl_Size is
     * signed and overflowing it would be undefined behaviour.
     */
    if (vPtr->length == 0) {
        newSize = 0;
    } else {
        intervals = vPtr->length - 1;
        if (density == TCL_SIZE_MAX) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("density is too large", -1));
            return TCL_ERROR;
        }
        valuesPerInterval = density + 1;
        if (MultiplyVectorSizes(interp, intervals, valuesPerInterval, &populatedValues) != TCL_OK) {
            return TCL_ERROR;
        }
        if (AddVectorSizes(interp, populatedValues, 1, &newSize) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (AllocateCommandData(vPtr, newSize, &data) != TCL_OK) {
        return TCL_ERROR;
    }
    if (sourcePtr->length > 0) {
        valuesPerInterval = density + 1;
        switch (sourcePtr->type) {
        case RBC_VECTOR_REAL: {
            double *valuePtr;
            double range;
            double slice;

            valuePtr = data.real;
            for (i = 0; i < (sourcePtr->length - 1); i++) {
                range = sourcePtr->data.real[i + 1] - sourcePtr->data.real[i];
                slice = range / (double)valuesPerInterval;
                /*
                 * Write the interval's starting value followed by the
                 * requested intermediate values. The ending value is
                 * written as the start of the next interval.
                 */
                for (j = 0; j < valuesPerInterval; j++) {
                    *valuePtr++ = sourcePtr->data.real[i] + (slice * (double)j);
                }
            }
            /*
             * The final source value is not written by the interval loop.
             */
            *valuePtr++ = sourcePtr->data.real[sourcePtr->length - 1];
            assert(valuePtr == (data.real + newSize));
            break;
        }
        case RBC_VECTOR_COMPLEX: {
            Rbc_Complex *valuePtr;
            double realRange, imagRange;
            double realSlice, imagSlice;

            valuePtr = data.complex;
            for (i = 0; i < (sourcePtr->length - 1); i++) {
                realRange = sourcePtr->data.complex[i + 1].real - sourcePtr->data.complex[i].real;
                imagRange = sourcePtr->data.complex[i + 1].imag - sourcePtr->data.complex[i].imag;
                realSlice = realRange / (double)valuesPerInterval;
                imagSlice = imagRange / (double)valuesPerInterval;
                for (j = 0; j < valuesPerInterval; j++) {
                    valuePtr->real = sourcePtr->data.complex[i].real + (realSlice * (double)j);
                    valuePtr->imag = sourcePtr->data.complex[i].imag + (imagSlice * (double)j);
                    valuePtr++;
                }
            }
            *valuePtr++ = sourcePtr->data.complex[sourcePtr->length - 1];
            assert(valuePtr == (data.complex + newSize));
            break;
        }
        default:
            Tcl_Panic("bad vector type %d", (int)sourcePtr->type);
            break;
        }
    }
    name = Tcl_GetString(objv[2]);
    v2Ptr = GetCommandOutput(vPtr, name, &isNew);
    if (v2Ptr == NULL) {
        ckfree(data.raw);
        return TCL_ERROR;
    }
    FinishCommandWrite(v2Ptr, data, newSize, newSize);
    result = PublishCommandOutput(v2Ptr, isNew);
    Tcl_Release(v2Ptr);
    return result;
}

/**
 * @brief Replace existing values with samples from the shared random generator.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 2; validated by the dispatcher.
 * @param[in] objv Borrowed full command argument array. No arguments after the operation name; this handler does
 *     not inspect the array.
 * @return TCL_OK after replacing existing values and requesting publication.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_numeric_paths
 *
 * Keeps length and type. Real entries receive one Rbc_RandomDouble sample;
 * complex entries receive separate samples for real and imaginary components.
 * Samples lie in [0,1); this is not a cryptographic generator. Flushes when
 * configured and reports a full update. The dispatcher checks writable state;
 * skips notification if a cache-flush callback destroys the receiver.
 */
static int RandomOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;

    (void)interp;
    (void)objc;
    (void)objv;
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        for (i = 0; i < vPtr->length; i++) {
            vPtr->data.real[i] = Rbc_RandomDouble();
        }
        break;
    case RBC_VECTOR_COMPLEX:
        for (i = 0; i < vPtr->length; i++) {
            vPtr->data.complex[i].real = Rbc_RandomDouble();
            vPtr->data.complex[i].imag = Rbc_RandomDouble();
        }
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)vPtr->type);
        return TCL_ERROR;
    }
    NotifyCommandWrite(vPtr, -1);
    return TCL_OK;
}

/**
 * @brief Return an inclusive interval of copied Tcl values.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 4; validated by the dispatcher.
 * @param[in] objv Borrowed full command argument array. objv[2] and objv[3] are inclusive first and last index
 *     expressions.
 * @return TCL_OK with copied values, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_numeric_paths
 *
 * Parses both external indices and traverses forward or backward according to
 * their order. Real entries are doubles; complex entries are two-item lists.
 * Returns a Tcl status and a list without writing vector data. Index parsing
 * can evaluate Tcl; revalidates both endpoints and receiver lifetime after
 * evaluation before reading any values. The append position is not readable.
 */
static int RangeOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *listObjPtr;
    Tcl_Size first, last;
    Tcl_Size i;

    if ((Rbc_VectorGetIndex(interp, vPtr, Tcl_GetString(objv[2]), &first, INDEX_CHECK, (Rbc_VectorIndexProc **)NULL) !=
         TCL_OK) ||
        (Rbc_VectorGetIndex(interp, vPtr, Tcl_GetString(objv[3]), &last, INDEX_CHECK, (Rbc_VectorIndexProc **)NULL) !=
         TCL_OK)) {
        return TCL_ERROR;
    }
    if (CheckCommandRange(vPtr, first, last) != TCL_OK) {
        return TCL_ERROR;
    }
    listObjPtr = Tcl_NewListObj(0, NULL);
    if (first > last) {
        for (i = first; i >= last; i--) {
            Tcl_ListObjAppendElement(interp, listObjPtr, Rbc_NewVectorValueObj(vPtr, i));
        }
    } else {
        for (i = first; i <= last; i++) {
            Tcl_ListObjAppendElement(interp, listObjPtr, Rbc_NewVectorValueObj(vPtr, i));
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    return TCL_OK;
}

/**
 * @brief Search for a complex value by matching both scalar components.
 * @param[in,out] vPtr Live complex receiver retained by the instance dispatcher with its interpreter.
 * @param[in] interp Owning evaluation interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Full command argument count inherited from the delegating handler.
 * @param[in] objv Borrowed full command array. Optional -value at index 2, then exactly one complex target;
 *     two-bound searches fail.
 * @return TCL_OK on completion, otherwise TCL_ERROR; conversion callbacks can have independent side effects.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_numeric_paths
 *
 * Accepts one target only; interval search is rejected. Uses InRange separately
 * for real and imaginary equality. Returns external indices with checked
 * positive offset addition, or pairs with -value. Target conversion may run
 * Tcl; rechecks receiver lifetime before scanning its current storage without
 * further callbacks. Returns a Tcl status.
 */
static int ComplexSearchOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Complex target;
    Tcl_Obj *listObjPtr;
    Tcl_Size i;
    int wantValue;
    const char *string;

    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    wantValue = FALSE;
    string = Tcl_GetString(objv[2]);
    if ((string[0] == '-') && (strcmp(string, "-value") == 0)) {
        wantValue = TRUE;
        objv++;
        objc--;
    }
    /*
     * Complex numbers have no natural interval ordering.  Support
     * searching for one complex value, but keep the two-bound range
     * form real-only.
     */
    if (objc > 3) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("range search is not supported for complex vectors", -1));
        return TCL_ERROR;
    }
    if (Rbc_GetComplex(interp, objv[2], &target) != TCL_OK) {
        return TCL_ERROR;
    }
    if (CheckCommandSource(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    listObjPtr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(listObjPtr);
    for (i = 0; i < vPtr->length; i++) {
        Rbc_Complex value;

        value = vPtr->data.complex[i];
        /*
         * Match the tolerance used by the existing real search for a
         * single value.  InRange(value, target, target) compares within
         * DBL_EPSILON.
         */
        if (InRange(value.real, target.real, target.real) && InRange(value.imag, target.imag, target.imag)) {
            Tcl_Obj *objPtr;

            if (wantValue) {
                objPtr = Rbc_NewVectorValueObj(vPtr, i);
            } else {
                Tcl_Size index;
                if ((vPtr->offset > 0) && (i > (TCL_SIZE_MAX - vPtr->offset))) {
                    Tcl_DecrRefCount(listObjPtr);
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("vector index is too large", -1));
                    return TCL_ERROR;
                }
                index = i + vPtr->offset;
                objPtr = Tcl_NewWideIntObj((Tcl_WideInt)index);
            }
            Tcl_ListObjAppendElement(interp, listObjPtr, objPtr);
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    Tcl_DecrRefCount(listObjPtr);
    return TCL_OK;
}

/**
 * @brief Find real values in a tolerant interval or delegate complex matching.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 3 to 5, subject to the optional flag and target count.
 * @param[in] objv Borrowed full command argument array. Optional -value at objv[2] requests values; remaining
 *     arguments specify one target or two real bounds.
 * @return TCL_OK with matching external indices or values, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_numeric_paths
 *
 * Returns external indices including the offset, or values with -value. One
 * bound means an equality search using InRange tolerance. A sufficiently
 * reversed interval returns TCL_OK without scanning. Checks positive index
 * offset overflow. Bound conversion may evaluate Tcl; checks receiver lifetime
 * afterward and scans current storage without further callbacks. Reversed
 * bounds explicitly return an empty result.
 */
static int SearchOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    double min, max;
    Tcl_Size i;
    int wantValue;
    char *string;
    Tcl_Obj *listObjPtr;

    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        return ComplexSearchOp(vPtr, interp, objc, objv);
    }
    assert(vPtr->type == RBC_VECTOR_REAL);
    wantValue = FALSE;
    string = Tcl_GetString(objv[2]);
    if ((string[0] == '-') && (strcmp(string, "-value") == 0)) {
        wantValue = TRUE;
        objv++, objc--;
    }
    if (Rbc_GetDouble(interp, objv[2], &min) != TCL_OK) {
        return TCL_ERROR;
    }
    max = min;
    if ((objc > 3) && (Rbc_GetDouble(interp, objv[3], &max) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (CheckCommandSource(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((min - max) >= DBL_EPSILON) {
        Tcl_ResetResult(interp);
        return TCL_OK; /* Bogus range. Don't bother looking. */
    }
    if (CheckCommandSource(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    listObjPtr = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(listObjPtr);
    for (i = 0; i < vPtr->length; i++) {
        if (InRange(vPtr->data.real[i], min, max)) {
            Tcl_Obj *objPtr;

            if (wantValue) {
                objPtr = Tcl_NewDoubleObj(vPtr->data.real[i]);
            } else {
                Tcl_Size index;
                if ((vPtr->offset > 0) && (i > (TCL_SIZE_MAX - vPtr->offset))) {
                    Tcl_DecrRefCount(listObjPtr);
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("vector index is too large", -1));
                    return TCL_ERROR;
                }
                index = i + vPtr->offset;
                objPtr = Tcl_NewWideIntObj((Tcl_WideInt)index);
            }
            Tcl_ListObjAppendElement(interp, listObjPtr, objPtr);
        }
    }
    Tcl_SetObjResult(interp, listObjPtr);
    Tcl_DecrRefCount(listObjPtr);
    return TCL_OK;
}

/**
 * @brief Derive one component's finite, representable sequence count.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] componentName Borrowed component label used in zero-step diagnostics.
 * @param[in] start First component value, required finite.
 * @param[in] finish Last requested component bound, required finite.
 * @param[in] step Component increment, required finite; zero is allowed only for constant bounds.
 * @param[out] hasLengthPtr Required constraint flag, initialized false; may change before a later size error.
 * @param[out] lengthPtr Required count output, initialized zero; consume both outputs only on TCL_OK.
 * @return TCL_OK with an optional count constraint, otherwise TCL_ERROR.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_numeric_paths
 *
 * Initializes hasLengthPtr false and lengthPtr zero. Rejects nonfinite bounds
 * or step. Zero step is accepted only for equal bounds and leaves the component
 * unconstrained. Otherwise sets hasLengthPtr true and computes
 * trunc((finish-start)/step)+1, clamping nonpositive counts to zero and rejecting
 * excessive counts before integer conversion. Returns a Tcl status.
 */
static int GetComplexSequenceComponentLength(Tcl_Interp *interp, const char *componentName, double start, double finish,
                                             double step, int *hasLengthPtr, Tcl_Size *lengthPtr) {
    double span;
    double steps;

    *hasLengthPtr = FALSE;
    *lengthPtr = 0;
    if ((!FINITE(start)) || (!FINITE(finish)) || (!FINITE(step))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence bounds and step must be finite", -1));
        return TCL_ERROR;
    }
    /*
     * A zero step is valid for one component when that component is
     * constant.  It then places no constraint on the sequence length;
     * the other component determines it.
     */
    if (step == 0.0) {
        if (start != finish) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s component of complex sequence has zero step "
                                                   "but different bounds",
                                                   componentName));
            return TCL_ERROR;
        }
        return TCL_OK;
    }
    /*
     * Use the same length calculation as the existing real SeqOp:
     *
     *     trunc((finish - start) / step) + 1
     */
    span = (finish - start) / step;
    if (!FINITE(span)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence size is too large", -1));
        return TCL_ERROR;
    }
    steps = trunc(span) + 1.0;
    *hasLengthPtr = TRUE;
    if (steps <= 0.0) {
        *lengthPtr = 0;
        return TCL_OK;
    }
    if (steps >= (double)TCL_SIZE_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence size is too large", -1));
        return TCL_ERROR;
    }
    *lengthPtr = (Tcl_Size)steps;
    return TCL_OK;
}

/**
 * @brief Generate a complex progression with compatible component lengths.
 * @param[in,out] vPtr Live complex receiver retained by the instance dispatcher with its interpreter.
 * @param[in] interp Owning evaluation interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Full command argument count inherited from the delegating handler.
 * @param[in] objv Borrowed full command array. Start at index 2, finish or end at index 3, and optional step at
 *     index 4.
 * @return TCL_OK on completion, otherwise TCL_ERROR; conversion callbacks can have independent side effects.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_numeric_paths
 *
 * Defaults step to 1+0i. Numeric finish requires each varying component to
 * imply the same count; a zero-step constant component imposes no count. Both
 * constant components with zero step cannot determine a numeric-finish count.
 * Literal end instead uses current length and permits a zero step. Positive
 * counts resize, fill each component and flush/notify; zero count is a no-op.
 * Rechecks lifetime and writability after argument conversion. Fills prepared
 * storage before committing and releasing the old allocation. Returns a Tcl status.
 */
static int ComplexSeqOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Complex start;
    Rbc_Complex finish;
    Rbc_Complex step;
    Tcl_Size realSteps;
    Tcl_Size imagSteps;
    Tcl_Size nSteps;
    Tcl_Size i;
    int realHasLength;
    int imagHasLength;
    int fillVector;
    const char *string;

    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    if (Rbc_GetComplex(interp, objv[2], &start) != TCL_OK) {
        return TCL_ERROR;
    }
    fillVector = FALSE;
    string = Tcl_GetString(objv[3]);
    if ((string[0] == 'e') && (strcmp(string, "end") == 0)) {
        fillVector = TRUE;
    } else {
        if (Rbc_GetComplex(interp, objv[3], &finish) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /*
     * Preserve the legacy default step of 1.  For a complex vector
     * this means 1+0i.
     */
    step.real = 1.0;
    step.imag = 0.0;
    if (objc == 5) {
        if (Rbc_GetComplex(interp, objv[4], &step) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (CheckCommandDestination(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (fillVector) {
        /*
         * "end" means that the vector's existing length determines
         * the number of values.  Either component of step may therefore
         * be zero, including both components.
         */
        nSteps = vPtr->length;
    } else {
        realSteps = 0;
        imagSteps = 0;
        if (GetComplexSequenceComponentLength(interp, "real", start.real, finish.real, step.real, &realHasLength,
                                              &realSteps) != TCL_OK) {
            return TCL_ERROR;
        }
        if (GetComplexSequenceComponentLength(interp, "imaginary", start.imag, finish.imag, step.imag, &imagHasLength,
                                              &imagSteps) != TCL_OK) {
            return TCL_ERROR;
        }
        /*
         * If neither component changes, a zero complex step gives no
         * way to determine the desired sequence length.
         */
        if ((!realHasLength) && (!imagHasLength)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence step cannot be zero", -1));
            return TCL_ERROR;
        }
        if (realHasLength && imagHasLength) {
            if (realSteps != imagSteps) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("real and imaginary sequences have different lengths", -1));
                return TCL_ERROR;
            }
            nSteps = realSteps;
        } else if (realHasLength) {
            nSteps = realSteps;
        } else {
            nSteps = imagSteps;
        }
    }
    /*
     * Match the existing real behavior: a sequence whose calculated
     * length is non-positive leaves the vector unchanged.
     */
    if (nSteps > 0) {
        VectorData data;
        Tcl_Size capacity;
        if (PrepareCommandResize(vPtr, nSteps, &data, &capacity) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < nSteps; i++) {
            data.complex[i].real = start.real + step.real * (double)i;
            data.complex[i].imag = start.imag + step.imag * (double)i;
        }
        FinishCommandWrite(vPtr, data, nSteps, capacity);
        NotifyCommandWrite(vPtr, -1);
    }
    return TCL_OK;
}

/**
 * @brief Generate a real arithmetic progression or delegate complex generation.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 4 or 5.
 * @param[in] objv Borrowed full command argument array. objv[2] is start; objv[3] is finish or end; optional
 *     objv[4] is step.
 * @return TCL_OK after generation or a zero-count no-op, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_numeric_paths
 *
 * Defaults step to one. Literal end fills the current length; numeric finish
 * uses trunc((finish-start)/step)+1 with finite and size checks. A nonpositive
 * calculated count leaves data unchanged. Positive counts resize and fill
 * values as start+i*step, then flush/notify. The end form bypasses count-derived
 * finite and zero-step validation. Returns a Tcl status; converting arguments
 * may execute Tcl. Rechecks lifetime and writability after conversion, fills
 * prepared storage, and commits before custom storage-release callbacks.
 */
static int SeqOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    Tcl_Size nSteps;
    double start;
    double finish;
    double step;
    double span;
    double steps;
    int fillVector;
    const char *string;

    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        return ComplexSeqOp(vPtr, interp, objc, objv);
    }
    assert(vPtr->type == RBC_VECTOR_REAL);
    if (Rbc_GetDouble(interp, objv[2], &start) != TCL_OK) {
        return TCL_ERROR;
    }
    fillVector = FALSE;
    string = Tcl_GetString(objv[3]);
    if ((string[0] == 'e') && (strcmp(string, "end") == 0)) {
        fillVector = TRUE;
    } else if (Rbc_GetDouble(interp, objv[3], &finish) != TCL_OK) {
        return TCL_ERROR;
    }
    step = 1.0;
    if ((objc == 5) && (Rbc_GetDouble(interp, objv[4], &step) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (CheckCommandDestination(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (fillVector) {
        /*
         * "end" means fill the vector's existing length.  There is
         * no floating-point-to-size conversion in this case.
         */
        nSteps = vPtr->length;
    } else {
        /*
         * These values determine the resulting allocation size, so
         * they must yield a finite and representable count.
         */
        if ((!FINITE(start)) || (!FINITE(finish)) || (!FINITE(step))) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence bounds and step must be finite", -1));
            return TCL_ERROR;
        }
        if (step == 0.0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence step cannot be zero", -1));
            return TCL_ERROR;
        }
        span = (finish - start) / step;
        if (!FINITE(span)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence size is too large", -1));
            return TCL_ERROR;
        }
        /*
         * Preserve the old C cast semantics: the old `(int)span`
         * truncated toward zero.
         */
        steps = trunc(span) + 1.0;
        if (steps <= 0.0) {
            return TCL_OK;
        }
        /*
         * (double)TCL_SIZE_MAX rounds upward on a 64-bit Tcl_Size,
         * which makes this a conservative boundary before the cast.
         */
        if (steps >= (double)TCL_SIZE_MAX) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("sequence size is too large", -1));
            return TCL_ERROR;
        }
        nSteps = (Tcl_Size)steps;
    }
    if (nSteps > 0) {
        VectorData data;
        Tcl_Size capacity;
        if (PrepareCommandResize(vPtr, nSteps, &data, &capacity) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < nSteps; i++) {
            data.real[i] = start + step * (double)i;
        }
        FinishCommandWrite(vPtr, data, nSteps, capacity);
        NotifyCommandWrite(vPtr, -1);
    }
    return TCL_OK;
}

/**
 * @brief Replace values from a vector range or Tcl list.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 3; validated by the dispatcher.
 * @param[in] objv Borrowed full command argument array. objv[2] is a vector reference or a Tcl list of replacement
 *     values.
 * @return TCL_OK after replacement and publication, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_command_paths
 *
 * Stages vector ranges (including self-copy) or converted list values before
 * replacing storage. Failed conversion does not write destination data. Tcl
 * callbacks retain their own side effects. Flushes/notifies only a still-live
 * committed object; callbacks cannot redirect the operation by recreating a name.
 */
static int SetOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject *srcPtr;
    int result;

    (void)objc;
    srcPtr = Rbc_VectorParseElement(NULL, vPtr->dataPtr, Tcl_GetString(objv[2]), NULL, NS_SEARCH_BOTH);
    if (CheckCommandDestination(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (srcPtr != NULL) {
        VectorData data;
        Tcl_Size length = srcPtr->last - srcPtr->first + 1;
        size_t bytes;
        size_t elementSize = (vPtr->type == RBC_VECTOR_REAL) ? sizeof(double) : sizeof(Rbc_Complex);
        if (srcPtr->type != vPtr->type) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't duplicate vectors of different types", -1));
            return TCL_ERROR;
        }
        if (GetArrayByteCount(interp, length, elementSize, &bytes) != TCL_OK) {
            return TCL_ERROR;
        }
        data.raw = (bytes > 0) ? Tcl_AttemptAlloc(bytes) : NULL;
        if (bytes > 0 && data.raw == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate vector command storage", -1));
            return TCL_ERROR;
        }
        if (bytes > 0) {
            memcpy(data.raw, (char *)srcPtr->data.raw + srcPtr->first * elementSize, bytes);
        }
        vPtr->offset = srcPtr->offset;
        FinishCommandWrite(vPtr, data, length, length);
        result = TCL_OK;
    } else {
        Tcl_Size count;
        Tcl_Obj **values;
        result = Tcl_ListObjGetElements(interp, objv[2], &count, &values);
        if (result == TCL_OK) {
            result = CopyList(vPtr, count, values);
        }
    }
    if (result == TCL_OK) {
        NotifyCommandWrite(vPtr, -1);
    }
    return result;
}

/**
 * @brief Sort real values and apply their permutation to companion vectors.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc At least 2.
 * @param[in] objv Borrowed full command argument array. Optional -reverse starts at objv[2]; following entries are
 *     companion vector references.
 * @return TCL_OK after applying the permutation and publication, otherwise TCL_ERROR.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_command_paths
 *
 * Retains resolved destination identities and validates all keys before any
 * write. Reuses one scratch array to apply the permutation without callbacks;
 * only then flushes and notifies surviving objects. Notifications may alter or
 * destroy committed vectors, but never expose a partially applied permutation.
 */
static int SortOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorObject **vectors;
    Tcl_Size vectorCount, j, retained = 0;
    size_t vectorBytes;
    char *string;
    double *mergeArr;
    Tcl_Size *iArr;
    Tcl_Size refSize;
    size_t nBytes;
    int result;
    int reverse;
    Tcl_Size i;
    Tcl_Size n;

    reverse = FALSE;
    if (objc > 2) {
        Tcl_Size length;
        string = Tcl_GetStringFromObj(objv[2], &length);
        if (string[0] == '-') {
            if ((length > 1) && (strncmp(string, "-reverse", length) == 0)) {
                reverse = TRUE;
            } else {
                Rbc_AppendResultStrings(interp, "unknown flag \"", string, "\": should be \"-reverse\"", (char *)NULL);
                return TCL_ERROR;
            }
            objc--;
            objv++;
        }
    }
    vectorCount = objc - 1;
    if (GetArrayByteCount(interp, vectorCount, sizeof(*vectors), &vectorBytes) != TCL_OK) {
        return TCL_ERROR;
    }
    vectors = ckalloc(vectorBytes);
    vectors[0] = vPtr;
    Tcl_Preserve(vPtr);
    retained = 1;
    result = TCL_ERROR;
    for (i = 1; i < vectorCount; i++) {
        if (Rbc_VectorLookupName(vPtr->dataPtr, Tcl_GetString(objv[i + 1]), &vectors[i]) != TCL_OK) {
            goto lookupError;
        }
        Tcl_Preserve(vectors[i]);
        retained++;
        if (CheckCommandDestination(vPtr) != TCL_OK || CheckCommandDestination(vectors[i]) != TCL_OK) {
            goto lookupError;
        }
        if (vectors[i]->length != vPtr->length) {
            Tcl_SetObjResult(
                interp, Tcl_ObjPrintf("vector \"%s\" is not the same size as \"%s\"", vectors[i]->name, vPtr->name));
            goto lookupError;
        }
        for (j = 0; j < i; j++) {
            if (vectors[j] == vectors[i]) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" specified more than once", vectors[i]->name));
                goto lookupError;
            }
        }
    }
    for (i = 0; i < vectorCount; i++) {
        if (CheckCommandDestination(vectors[i]) != TCL_OK) {
            goto lookupError;
        }
        if (vectors[i]->length != vPtr->length) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector lengths changed during sort evaluation", -1));
            goto lookupError;
        }
    }
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;
    iArr = Rbc_VectorSortIndex(vectors, vectorCount);
    if (iArr == NULL) {
        goto lookupError;
    }
    refSize = vPtr->length;
    /*
     * Rbc_VectorSortIndex always returns ascending order.  Reverse the
     * permutation locally for the instance command's -reverse option.
     * This keeps reverse ordering out of the shared sort implementation.
     */
    if ((reverse) && (refSize > 1)) {
        Tcl_Size left;
        Tcl_Size right;

        left = 0;
        right = refSize - 1;
        while (left < right) {
            Tcl_Size tmp;

            tmp = iArr[left];
            iArr[left] = iArr[right];
            iArr[right] = tmp;
            left++;
            right--;
        }
    }
    /*
     * Create an array to store a copy of the current values of the
     * vector. We'll merge the values back into the vector based upon
     * the indices found in the index array.
     */
    if (GetArrayByteCount(interp, refSize, sizeof(*mergeArr), &nBytes) != TCL_OK) {
        ckfree(iArr);
        goto lookupError;
    }
    mergeArr = ckalloc(nBytes);
    /* No callback-capable operations between validation and the last write.
     * Reuse one scratch array rather than staging a full copy per destination. */
    for (i = 0; i < vectorCount; i++) {
        if (nBytes > 0) {
            memcpy(mergeArr, vectors[i]->data.raw, nBytes);
        }
        for (n = 0; n < refSize; n++) {
            vectors[i]->data.real[n] = mergeArr[iArr[n]];
        }
        vectors[i]->notifyFlags |= UPDATE_RANGE;
        vectors[i]->minIndex = vectors[i]->maxIndex = -1;
    }
    ckfree(mergeArr);
    ckfree(iArr);
    /* All data is final before any cache trace or observer can inspect it.
     * Retained identities survive renames; deleted/recreated names are skipped. */
    for (i = 0; i < vectorCount; i++) {
        NotifyCommandWrite(vectors[i], -1);
    }
    result = TCL_OK;
lookupError:
    for (i = 0; i < retained; i++) {
        Tcl_Release(vectors[i]);
    }
    ckfree(vectors);
    return result;
}

/**
 * @brief Append round-robin source subsequences to named destinations.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc At least 2; no destinations is a successful no-op.
 * @param[in] objv Borrowed full command argument array. Entries from objv[2] are literal destination names
 *     receiving round-robin source subsequences.
 * @return TCL_OK after appending and publishing all outputs, otherwise TCL_ERROR; earlier commits remain.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * @ingroup vector_edit_paths
 *
 * Requires source length divisible by destination count; no destinations is a
 * no-op. Prechecks writable existing destinations and creates missing ones.
 * Snapshots the source and retains all destination identities. Each append is
 * complete before releasing its old storage; publication follows the write loop,
 * flushing before notification. A later failure leaves earlier appends committed
 * and published. Destroyed/recreated identities are never written again.
 * Returns a Tcl status; this is not an all-output transaction.
 */
static int SplitOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size count = objc - 2, length = vPtr->length, extra, i, j;
    size_t bytes, elementSize = vPtr->type == RBC_VECTOR_REAL ? sizeof(double) : sizeof(Rbc_Complex);
    VectorData snapshot;
    CommandOutput *outputs;
    int result = TCL_ERROR;

    if (count == 0)
        return TCL_OK;
    if (length % count != 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't split vector \"%s\" into "
                                               "%" TCL_SIZE_MODIFIER "d even parts.",
                                               vPtr->name, count));
        return TCL_ERROR;
    }
    if (GetArrayByteCount(interp, length, elementSize, &bytes) != TCL_OK ||
        AllocateCommandData(vPtr, length, &snapshot) != TCL_OK)
        return TCL_ERROR;
    if (bytes > 0) {
        memcpy(snapshot.raw, vPtr->data.raw, bytes);
    }
    extra = length / count;
    outputs = GetCommandOutputs(vPtr, count, objv + 2);
    if (outputs == NULL) {
        goto done;
    }
    for (i = 0; i < count; i++) {
        VectorObject *dest = outputs[i].vector;
        VectorData data;
        Tcl_Size newSize, capacity, oldSize;
        if (CheckCommandDestination(dest) != TCL_OK) {
            goto done;
        }
        oldSize = dest->length;
        if (AddVectorSizes(interp, oldSize, extra, &newSize) != TCL_OK ||
            PrepareCommandWrite(dest, newSize, &data, &capacity) != TCL_OK) {
            goto done;
        }
        for (j = 0; j < extra; j++) {
            memcpy((char *)data.raw + (size_t)(oldSize + j) * elementSize,
                   (char *)snapshot.raw + (size_t)(i + j * count) * elementSize, elementSize);
        }
        FinishCommandWrite(dest, data, newSize, capacity);
    }
    result = TCL_OK;
done:
    /* Publish earlier committed appends even if a release callback prevented a
     * later commit. A destroyed identity is skipped, never resolved by name. */
    if (outputs != NULL) {
        Tcl_InterpState state = Tcl_SaveInterpState(interp, result);
        Tcl_Size committed = (result == TCL_OK) ? count : i;
        for (j = 0; j < committed; j++) {
            int code = PublishCommandOutput(outputs[j].vector, outputs[j].isNew);
            if (code != TCL_OK && result == TCL_OK) {
                Tcl_DiscardInterpState(state);
                result = TCL_ERROR;
                state = Tcl_SaveInterpState(interp, result);
            }
        }
        Tcl_RestoreInterpState(interp, state);
        ReleaseCommandOutputs(outputs, count);
    }
    ckfree(snapshot.raw);
    return result;
}

/**
 * @ingroup vector_command_paths
 * @brief Query, replace or remove the receiver mapped-array association.
 * @param[in,out] vPtr Receiver retained by the dispatcher; this handler's contract specifies any mutation.
 * @param[in] interp Owning interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc 2 to query, or 3 to replace/remove the mapping.
 * @param[in] objv Borrowed full command argument array. Optional objv[2] is the array name; an empty string
 *     requests unmapping.
 * @return TCL_OK with the stored mapping name when present, or TCL_ERROR from mapping.
 * @see @ref rbc_command_arguments
 * @see @ref rbc_callback_lifetimes
 *
 * With objv[2], delegate to Rbc_VectorMapVariable and propagate its error.
 * An empty name removes the mapping; it does not rename the vector command.
 * On success, copy the stored array name into the Tcl result when non-NULL;
 * otherwise do not explicitly set a result. The name is the stored mapping
 * spelling, not a newly qualified command name. Read-only and complex receivers
 * are supported. Mapping can invoke traces and is not a rollback transaction.
 */
static int VariableOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (objc > 2) {
        if (Rbc_VectorMapVariable(interp, vPtr, Tcl_GetString(objv[2])) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (vPtr->arrayName != NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(vPtr->arrayName, -1));
    }
    return TCL_OK;
}

/**
 * @brief Append a selected vector range without Tcl-list conversion.
 * @param[in,out] destPtr Caller-retained writable destination and interpreter; old-storage release may run
 *     callbacks.
 * @param[in] srcPtr Live matching-type source with a caller-validated inclusive selection; may equal destPtr.
 * @return TCL_OK after append commit, otherwise TCL_ERROR; publication remains the caller's responsibility.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_command_paths
 *
 * Requires matching real/complex types. Saves source selection before resize
 * and uses memmove against current storage, permitting self-append. Checks
 * length/byte arithmetic. Returns a Tcl status; does not flush or notify.
 */
static int AppendVector(VectorObject *destPtr, VectorObject *srcPtr) {
    VectorData data;
    Tcl_Size size;
    Tcl_Size sourceFirst;
    Tcl_Size sourceLength;
    Tcl_Size oldSize;
    Tcl_Size newSize;
    size_t byteCount;
    size_t elementSize;

    /*
     * Do not reinterpret storage when appending one vector to another.
     */
    if (destPtr->type != srcPtr->type) {
        Tcl_SetObjResult(destPtr->interp, Tcl_NewStringObj("can't append vectors of different types", -1));
        return TCL_ERROR;
    }
    /*
     * Preserve the selected source range before resizing the
     * destination. destPtr and srcPtr may refer to the same vector,
     * and resizing resets its first/last selection.
     */
    sourceFirst = srcPtr->first;
    sourceLength = srcPtr->last - sourceFirst + 1;
    oldSize = destPtr->length;
    if (AddVectorSizes(destPtr->interp, oldSize, sourceLength, &newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (destPtr->type) {
    case RBC_VECTOR_REAL:
        elementSize = sizeof(double);
        break;
    case RBC_VECTOR_COMPLEX:
        elementSize = sizeof(Rbc_Complex);
        break;
    default:
        Tcl_Panic("bad vector type %d", (int)destPtr->type);
        return TCL_ERROR;
    }
    if (GetArrayByteCount(destPtr->interp, sourceLength, elementSize, &byteCount) != TCL_OK) {
        return TCL_ERROR;
    }
    if (PrepareCommandWrite(destPtr, newSize, &data, &size) != TCL_OK) {
        return TCL_ERROR;
    }
    if (byteCount > 0) {
        switch (destPtr->type) {
        case RBC_VECTOR_REAL:
            memmove(data.real + oldSize, srcPtr->data.real + sourceFirst, byteCount);
            break;
        case RBC_VECTOR_COMPLEX:
            memmove(data.complex + oldSize, srcPtr->data.complex + sourceFirst, byteCount);
            break;
        default:
            Tcl_Panic("bad vector type %d", (int)destPtr->type);
        }
    }
    FinishCommandWrite(destPtr, data, newSize, size);
    return TCL_OK;
}

/**
 * @brief Stage list values before appending the completed list.
 * @param[in,out] vPtr Caller-retained destination and interpreter; value conversion may execute Tcl.
 * @param[in] objc Nonnegative number of values, excluding command and operation names.
 * @param[in] objv Borrowed value-only array, copied into a private list representation for conversion.
 * @return TCL_OK after commit, otherwise TCL_ERROR; callback side effects are not rolled back.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_command_paths
 *
 * Converts a complete list into temporary storage and rechecks lifetime and
 * writable state after conversion. Appends to the current length, accounting
 * for callback edits. Conversion failure does not append a partial list.
 * Does not flush or notify; the instance dispatcher retains the destination.
 */
static int AppendList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorData values, data;
    Tcl_Size length, size;
    size_t bytes;
    size_t elementSize = (vPtr->type == RBC_VECTOR_REAL) ? sizeof(double) : sizeof(Rbc_Complex);

    if (ConvertCommandList(vPtr, objc, objv, &values) != TCL_OK) {
        return TCL_ERROR;
    }
    /* A conversion may have changed length: append to the current destination. */
    if (CheckCommandDestination(vPtr) != TCL_OK ||
        AddVectorSizes(vPtr->interp, vPtr->length, objc, &length) != TCL_OK ||
        GetArrayByteCount(vPtr->interp, objc, elementSize, &bytes) != TCL_OK ||
        PrepareCommandWrite(vPtr, length, &data, &size) != TCL_OK) {
        if (values.raw != NULL) {
            ckfree(values.raw);
        }
        return TCL_ERROR;
    }
    if (bytes > 0) {
        memcpy((char *)data.raw + vPtr->length * elementSize, values.raw, bytes);
    }
    if (values.raw != NULL) {
        ckfree(values.raw);
    }
    FinishCommandWrite(vPtr, data, length, size);
    return TCL_OK;
}

/**
 * @brief Convert a binary scalar buffer into real vector storage.
 * @param[in,out] vPtr Live writable real destination; caller retains record and interpreter through commit.
 * @param[in,out] byteArr Writable suitably aligned native-format buffer with one scalar per value; swap modifies
 *     it.
 * @param[in] fmt Supported native scalar format matching the supplied size and buffer representation.
 * @param[in] size Positive native scalar width in bytes, validated against fmt by the caller.
 * @param[in] length Nonnegative number of real values to copy.
 * @param[in] swap Nonzero to reverse the bytes of each scalar in byteArr before conversion.
 * @param[in,out] indexPtr Required zero-based destination position, from zero through current length; advanced on
 *     success.
 * @return TCL_OK after copying and committing, otherwise TCL_ERROR; no cache flushing or notification.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_command_paths
 *
 * Checks writable state, range and size arithmetic, extends storage as needed,
 * and advances indexPtr. Optional byte swapping modifies the input buffer
 * in place per scalar. Returns a Tcl status; does not flush or notify.
 */
static int CopyValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, Tcl_Size length, int swap,
                      Tcl_Size *indexPtr) {
    VectorData data;
    Tcl_Size capacity;
    Tcl_Size finalLength;
    Tcl_Size i;
    Tcl_Size n;
    Tcl_Size newSize;

    if (Rbc_VectorCheckWritable(vPtr->interp, (Rbc_Vector *)vPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    if ((length < 0) || (indexPtr == NULL) || (*indexPtr < 0) || (*indexPtr > vPtr->length)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("invalid binary vector range", -1));
        return TCL_ERROR;
    }
    if ((fmt == FMT_UNKNOWN) || (size <= 0)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("invalid binary format", -1));
        return TCL_ERROR;
    }
    if (AddVectorSizes(vPtr->interp, *indexPtr, length, &newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    if (swap && (size > 1)) {
        size_t nBytes;
        size_t byteOffset;
        unsigned char *p;
        unsigned char temp;
        int left;
        int right;

        if (GetArrayByteCount(vPtr->interp, length, (size_t)size, &nBytes) != TCL_OK) {
            return TCL_ERROR;
        }
        for (byteOffset = 0; byteOffset < nBytes; byteOffset += (size_t)size) {
            p = (unsigned char *)byteArr + byteOffset;
            for (left = 0, right = size - 1; left < right; left++, right--) {
                temp = p[left];
                p[left] = p[right];
                p[right] = temp;
            }
        }
    }
    finalLength = MAX(newSize, vPtr->length);
    if (PrepareCommandWrite(vPtr, finalLength, &data, &capacity) != TCL_OK) {
        return TCL_ERROR;
    }
#define CopyArrayToVector(array)                                                                                       \
    do {                                                                                                               \
        for (i = 0, n = *indexPtr; i < length; i++, n++) {                                                             \
            data.real[n] = (double)(array)[i];                                                                         \
        }                                                                                                              \
    } while (0)
    switch (fmt) {
    case FMT_CHAR:
        CopyArrayToVector((char *)byteArr);
        break;
    case FMT_UCHAR:
        CopyArrayToVector((unsigned char *)byteArr);
        break;
    case FMT_SHORT:
        CopyArrayToVector((short *)byteArr);
        break;
    case FMT_USHORT:
        CopyArrayToVector((unsigned short *)byteArr);
        break;
    case FMT_INT:
        CopyArrayToVector((int *)byteArr);
        break;
    case FMT_UINT:
        CopyArrayToVector((unsigned int *)byteArr);
        break;
    case FMT_LONG:
        CopyArrayToVector((long *)byteArr);
        break;
    case FMT_ULONG:
        CopyArrayToVector((unsigned long *)byteArr);
        break;
    case FMT_LONGLONG:
        CopyArrayToVector((long long *)byteArr);
        break;
    case FMT_ULONGLONG:
        CopyArrayToVector((unsigned long long *)byteArr);
        break;
    case FMT_FLOAT:
        CopyArrayToVector((float *)byteArr);
        break;
    case FMT_DOUBLE:
        CopyArrayToVector((double *)byteArr);
        break;
    case FMT_UNKNOWN:
        /*
         * Rejected above. Keep the case to satisfy exhaustive-switch
         * diagnostics.
         */
        assert(0);
        return TCL_ERROR;
    }
#undef CopyArrayToVector
    *indexPtr = newSize;
    FinishCommandWrite(vPtr, data, finalLength, capacity);
    return TCL_OK;
}

/**
 * @brief Convert interleaved binary components into complex storage.
 * @param[in,out] vPtr Live writable complex destination; caller retains record and interpreter through commit.
 * @param[in,out] byteArr Writable suitably aligned native-format buffer with two scalars per logical value; swap
 *     modifies it.
 * @param[in] fmt Supported native scalar format matching the supplied size and buffer representation.
 * @param[in] size Positive native scalar width in bytes, validated against fmt by the caller.
 * @param[in] length Nonnegative number of complex values, not scalar components to copy.
 * @param[in] swap Nonzero to reverse the bytes of each scalar in byteArr before conversion.
 * @param[in,out] indexPtr Required zero-based destination position, from zero through current length; advanced on
 *     success.
 * @return TCL_OK after copying and committing, otherwise TCL_ERROR; no cache flushing or notification.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_command_paths
 *
 * Requires a complex vector. Checks writable state and size arithmetic;
 * extends storage and advances indexPtr by logical values. Swaps bytes within
 * each scalar component, never exchanging real and imaginary components.
 * Modifies the input buffer when swapping; does not flush or notify.
 */
static int CopyComplexValues(VectorObject *vPtr, char *byteArr, enum NativeFormats fmt, int size, Tcl_Size length,
                             int swap, Tcl_Size *indexPtr) {
    VectorData data;
    Tcl_Size capacity;
    Tcl_Size finalLength;
    Tcl_Size i;
    Tcl_Size n;
    Tcl_Size newSize;
    Tcl_Size componentCount;

    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    if (Rbc_VectorCheckWritable(vPtr->interp, (Rbc_Vector *)vPtr) != TCL_OK) {
        return TCL_ERROR;
    }

    if ((length < 0) || (indexPtr == NULL) || (*indexPtr < 0) || (*indexPtr > vPtr->length)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("invalid binary vector range", -1));
        return TCL_ERROR;
    }
    if ((fmt == FMT_UNKNOWN) || (size <= 0)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("invalid binary format", -1));
        return TCL_ERROR;
    }
    if (AddVectorSizes(vPtr->interp, *indexPtr, length, &newSize) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Each complex value contains two formatted scalar components.
     */
    if (MultiplyVectorSizes(vPtr->interp, length, 2, &componentCount) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Swap each scalar component independently.  Do not swap the
     * real/imaginary components themselves.
     */
    if (swap && (size > 1)) {
        size_t nBytes;
        size_t byteOffset;
        unsigned char *p;
        unsigned char temp;
        int left;
        int right;

        if (GetArrayByteCount(vPtr->interp, componentCount, (size_t)size, &nBytes) != TCL_OK) {
            return TCL_ERROR;
        }
        for (byteOffset = 0; byteOffset < nBytes; byteOffset += (size_t)size) {
            p = (unsigned char *)byteArr + byteOffset;
            for (left = 0, right = size - 1; left < right; left++, right--) {
                temp = p[left];
                p[left] = p[right];
                p[right] = temp;
            }
        }
    }
    finalLength = MAX(newSize, vPtr->length);
    if (PrepareCommandWrite(vPtr, finalLength, &data, &capacity) != TCL_OK) {
        return TCL_ERROR;
    }
#define CopyArrayToComplexVector(array)                                                                                \
    do {                                                                                                               \
        for (i = 0, n = *indexPtr; i < length; i++, n++) {                                                             \
            data.complex[n].real = (double)(array)[2 * i];                                                             \
            data.complex[n].imag = (double)(array)[2 * i + 1];                                                         \
        }                                                                                                              \
    } while (0)
    switch (fmt) {
    case FMT_CHAR:
        CopyArrayToComplexVector((char *)byteArr);
        break;
    case FMT_UCHAR:
        CopyArrayToComplexVector((unsigned char *)byteArr);
        break;
    case FMT_SHORT:
        CopyArrayToComplexVector((short *)byteArr);
        break;
    case FMT_USHORT:
        CopyArrayToComplexVector((unsigned short *)byteArr);
        break;
    case FMT_INT:
        CopyArrayToComplexVector((int *)byteArr);
        break;
    case FMT_UINT:
        CopyArrayToComplexVector((unsigned int *)byteArr);
        break;
    case FMT_LONG:
        CopyArrayToComplexVector((long *)byteArr);
        break;
    case FMT_ULONG:
        CopyArrayToComplexVector((unsigned long *)byteArr);
        break;
    case FMT_LONGLONG:
        CopyArrayToComplexVector((long long *)byteArr);
        break;
    case FMT_ULONGLONG:
        CopyArrayToComplexVector((unsigned long long *)byteArr);
        break;
    case FMT_FLOAT:
        CopyArrayToComplexVector((float *)byteArr);
        break;
    case FMT_DOUBLE:
        CopyArrayToComplexVector((double *)byteArr);
        break;
    case FMT_UNKNOWN:
        assert(0);
        return TCL_ERROR;
    }
#undef CopyArrayToComplexVector
    *indexPtr = newSize;
    FinishCommandWrite(vPtr, data, finalLength, capacity);
    return TCL_OK;
}

/**
 * @brief Test scalar membership using the legacy search tolerance.
 * @param[in] value Candidate scalar.
 * @param[in] min Lower bound as supplied; not reordered or checked for finiteness.
 * @param[in] max Upper bound as supplied; used as the equality target for a near-zero or negative range.
 * @return Nonzero for membership under the legacy tolerance rule, zero otherwise.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_numeric_paths
 *
 * When max-min is below DBL_EPSILON, compares absolute distance from max
 * strictly below DBL_EPSILON. Otherwise accepts normalized positions at least
 * -DBL_EPSILON and less than one plus DBL_EPSILON. Does not reorder bounds, check
 * finiteness, allocate storage or invoke Tcl. Returns a Boolean integer.
 */
static int InRange(double value, double min, double max) {
    double range;

    range = max - min;
    if (range < DBL_EPSILON) {
        return (FABS(max - value) < DBL_EPSILON);
    } else {
        double norm;
        norm = (value - min) / range;
        return ((norm >= -DBL_EPSILON) && ((norm - 1.0) < DBL_EPSILON));
    }
}

/**
 * @brief Convert a complete Tcl list before replacing vector storage.
 * @param[in,out] vPtr Caller-retained destination and interpreter; value conversion may execute Tcl.
 * @param[in] objc Nonnegative number of values, excluding command and operation names.
 * @param[in] objv Borrowed value-only array, copied into a private list representation for conversion.
 * @return TCL_OK after commit, otherwise TCL_ERROR; callback side effects are not rolled back.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup vector_command_paths
 *
 * Returns a Tcl status. Conversion failure leaves destination values/length
 * unchanged except for the callbacks' own changes. Transfers staged storage
 * on success, with lifetime/read-only checks before commit. Does not notify;
 * callers retain the destination and interpreter through callbacks.
 */
static int CopyList(VectorObject *vPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorData data;
    if (ConvertCommandList(vPtr, objc, objv, &data) != TCL_OK) {
        return TCL_ERROR;
    }
    if (CheckCommandDestination(vPtr) != TCL_OK) {
        if (data.raw != NULL) {
            ckfree(data.raw);
        }
        return TCL_ERROR;
    }
    FinishCommandWrite(vPtr, data, objc, objc);
    return TCL_OK;
}

/**
 * @brief Allocate an ascending permutation for a real-vector selection.
 * Requires at least one live vector and valid corresponding indices in every key vector. Rejects
 * complex keys, uses the first vector selection to build storage indices, and sorts them
 * lexicographically without moving values. Returns an owned allocation to free with ckfree, including
 * a non-NULL allocation for an empty successful sort; NULL denotes failure. Does not check key lengths
 * or retain vectors. Ordering is unstable and NaNs do not form a total order.
 */
Tcl_Size *Rbc_VectorSortIndex(VectorObject **vPtrPtr, Tcl_Size nVectors) {
    VectorObject *vPtr;
    Tcl_Size *indexArr;
    Tcl_Size length;
    Tcl_Size i;
    size_t byteCount;

    vPtr = *vPtrPtr;
    /*
     * Sorting requires an ordering relation.  Complex values have no
     * natural ordering, and the sorting implementation operates on
     * real vector storage.
     */
    for (i = 0; i < nVectors; i++) {
        if (vPtrPtr[i]->type != RBC_VECTOR_REAL) {
            Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't sort complex vectors", -1));
            return NULL;
        }
    }
    length = vPtr->last - vPtr->first + 1;
    if (length == 0) {
        /*
         * Callers use NULL to indicate failure.  Return a harmless
         * allocation for an empty successful sort instead of relying on
         * platform-specific zero-size allocation behavior.
         */
        indexArr = Tcl_AttemptAlloc(sizeof(*indexArr));
        if (indexArr == NULL) {
            Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector sort indices", -1));
            return NULL;
        }
        return indexArr;
    }
    if (GetArrayByteCount(vPtr->interp, length, sizeof(*indexArr), &byteCount) != TCL_OK) {
        return NULL;
    }
    indexArr = Tcl_AttemptAlloc(byteCount);
    if ((indexArr == NULL) && (byteCount > 0)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate vector sort indices", -1));
        return NULL;
    }
    for (i = 0; i < length; i++) {
        indexArr[i] = vPtr->first + i;
    }
    SortVectorIndices(indexArr, length, vPtrPtr, nVectors);
    return indexArr;
}

/**
 * @ingroup vector_command_paths
 * @brief Compare two storage indices lexicographically across real keys.
 * @param[in] vectorArr Array of live real key vectors, in primary-to-secondary order.
 * @param[in] nVectors Number of key vectors; every key must contain both requested storage indices.
 * @param[in] i1 First zero-based storage index to compare.
 * @param[in] i2 Second zero-based storage index to compare.
 * @return -1, 0 or 1 according to the first ordered unequal key; NaN comparisons advance to the next key.
 * @see @ref rbc_calling_conventions
 *
 * Requires valid indices in every key vector. Returns -1 or 1 at the first unequal comparable key,
 * otherwise zero. A NaN comparison falls through to the next key because neither less-than nor
 * greater-than succeeds. Does not break ties by original index or invoke callbacks.
 */
static int CompareVectorIndices(VectorObject *const *vectorArr, Tcl_Size nVectors, Tcl_Size i1, Tcl_Size i2) {
    Tcl_Size vectorIndex;

    for (vectorIndex = 0; vectorIndex < nVectors; vectorIndex++) {
        double value1;
        double value2;

        value1 = vectorArr[vectorIndex]->data.real[i1];
        value2 = vectorArr[vectorIndex]->data.real[i2];
        if (value1 < value2) {
            return -1;
        }
        if (value1 > value2) {
            return 1;
        }
    }
    return 0;
}

/**
 * @ingroup vector_command_paths
 * @brief Restore a max heap within an inclusive permutation boundary.
 * @param[in,out] indexArr Permutation containing valid storage indices for all real key vectors.
 * @param[in] start Nonnegative root position in the permutation; child subtrees must already be heaps.
 * @param[in] end Inclusive final heap position, not a storage index; end <= start performs no work.
 * @param[in] vectorArr Borrowed array of stable real key vectors covering every referenced storage index.
 * @param[in] nVectors Number of key vectors in primary-to-secondary order.
 * @see @ref rbc_calling_conventions
 *
 * Requires child subtrees below start already to be heaps and valid key storage for each permutation
 * entry. Moves the root down by comparing children through CompareVectorIndices. The root bound
 * prevents overflow in twice-root-plus-one arithmetic. Does nothing when end is not above start;
 * modifies only permutation entries and allocates nothing.
 */
static void SiftVectorIndices(Tcl_Size *indexArr, Tcl_Size start, Tcl_Size end, VectorObject *const *vectorArr,
                              Tcl_Size nVectors) {
    Tcl_Size root;

    if (end <= start) {
        return;
    }
    root = start;
    /*
     * Build/restore a max heap.  The bound on root also guarantees
     * that root * 2 + 1 cannot overflow Tcl_Size.
     */
    while (root <= ((end - 1) / 2)) {
        Tcl_Size child;
        Tcl_Size swapIndex;
        Tcl_Size tmp;

        child = root * 2 + 1;
        swapIndex = root;
        if (CompareVectorIndices(vectorArr, nVectors, indexArr[swapIndex], indexArr[child]) < 0) {
            swapIndex = child;
        }
        if ((child < end) &&
            (CompareVectorIndices(vectorArr, nVectors, indexArr[swapIndex], indexArr[child + 1]) < 0)) {
            swapIndex = child + 1;
        }
        if (swapIndex == root) {
            return;
        }
        tmp = indexArr[root];
        indexArr[root] = indexArr[swapIndex];
        indexArr[swapIndex] = tmp;
        root = swapIndex;
    }
}

/**
 * @ingroup vector_command_paths
 * @brief Heap-sort an existing index array in ascending key order.
 * @param[in,out] indexArr Permutation array with length entries, rearranged in place without moving vector values.
 * @param[in] length Nonnegative permutation length; fewer than two entries require no work.
 * @param[in] vectorArr Borrowed array of stable real key vectors covering every referenced storage index.
 * @param[in] nVectors Number of key vectors in primary-to-secondary order.
 * @see @ref rbc_calling_conventions
 *
 * Heapifies the permutation, then moves successive maxima to the end and repairs the remaining heap.
 * Uses constant auxiliary storage and O(length log length) comparisons, each examining up to nVectors
 * keys. Requires stable valid real key storage for the call. Does not move vector values, invoke Tcl
 * or guarantee stable ordering of equal keys.
 */
static void SortVectorIndices(Tcl_Size *indexArr, Tcl_Size length, VectorObject *const *vectorArr, Tcl_Size nVectors) {
    Tcl_Size start;
    Tcl_Size end;

    if (length < 2) {
        return;
    }
    /*
     * Heapify.
     */
    start = (length - 2) / 2;
    for (;;) {
        SiftVectorIndices(indexArr, start, length - 1, vectorArr, nVectors);
        if (start == 0) {
            break;
        }
        start--;
    }
    /*
     * Repeatedly move the largest remaining index to the end.
     */
    end = length - 1;
    while (end > 0) {
        Tcl_Size tmp;

        tmp = indexArr[end];
        indexArr[end] = indexArr[0];
        indexArr[0] = tmp;
        end--;
        SiftVectorIndices(indexArr, 0, end, vectorArr, nVectors);
    }
}
