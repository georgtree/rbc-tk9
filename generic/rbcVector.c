/*
 * rbcVector.c --
 *
 *      Interpreter-local vectors, storage ownership and client notifications.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/**
 * @file rbcVector.c
 * @brief Interpreter-owned vector identities, storage, access and notification.
 */
/**
 * @defgroup vector_storage_public Vector storage public C API
 * @brief Reviewed stubs-facing lifetime, storage and protection contracts.
 */
/**
 * @defgroup vector_storage_private Vector storage private implementation
 * @brief Registry and storage primitives with guarded callback boundaries.
 */

#include "tcl.h"
#include "rbcVectorInt.h"

typedef int RbcVectorOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcVectorOp *proc;
} VectorOpSpec;

static Tcl_ObjCmdProc2 VectorObjCmd;
static Tcl_ObjCmdProc2 VectorCreateObjCmd;
static Tcl_ObjCmdProc2 VectorDestroyObjCmd;
static Tcl_ObjCmdProc2 VectorExprObjCmd;
static Tcl_ObjCmdProc2 VectorNamesObjCmd;
static Tcl_ObjCmdProc2 VectorRenameObjCmd;
static Tcl_ObjCmdProc2 VectorSimplifyObjCmd;
static Tcl_CmdDeleteProc VectorInstDeleteProc;
static Tcl_InterpDeleteProc VectorInterpDeleteProc;

static void VectorNotifyClients(ClientData clientData);
static char *VectorVarTrace(ClientData clientData, Tcl_Interp *interp, char *part1, char *part2, int flags);
static char *BuildQualifiedName(Tcl_Interp *interp, const char *name, Tcl_DString *fullName);
static int ParseQualifiedName(Tcl_Interp *interp, const char *qualName, Tcl_Namespace **nsPtrPtr,
                              const char **namePtrPtr);
static char *GetQualifiedName(Tcl_Namespace *nsPtr, const char *name, Tcl_DString *resultPtr);
static VectorObject *GetVectorObject(VectorInterpData *dataPtr, const char *name, int flags);
static VectorObject *FindVectorInNamespace(VectorInterpData *dataPtr, Tcl_Namespace *nsPtr, const char *vecName);
static void DeleteCommand(VectorObject *vPtr);
static void UnmapVariable(VectorObject *vPtr);

double rbcNaN;

#ifdef __BORLANDC__
static double MakeNaN(void) {
    union Real {
        struct DoubleWord {
            int lo, hi;
        } doubleWord;
        double number;
    } real;

    real.doubleWord.lo = real.doubleWord.hi = 0x7FFFFFFF;
    return real.number;
}
#endif /* __BORLANDC__ */

#ifdef _MSC_VER
static double MakeNaN(void) { return sqrt(-1.0); /* Generate IEEE 754 Quiet Not-A-Number. */ }
#endif /* _MSC_VER */

#if !defined(__BORLANDC__) && !defined(_MSC_VER)
static double MakeNaN(void) {
    /* copied over from generic/tclStrToD.c */
#define NAN_START 0x7FF8
#define NAN_MASK (((Tcl_WideUInt)1) << 51)
    int signum = 0; /* Sign bit (1=negative, 0=nonnegative. */
    union {
        Tcl_WideUInt iv;
        double dv;
    } theNaN;

    theNaN.iv = 0;
    theNaN.iv &= NAN_MASK - 1;
    if (signum) {
        theNaN.iv |= ((Tcl_WideUInt)(0x8000 | NAN_START)) << 48;
    } else {
        theNaN.iv |= ((Tcl_WideUInt)NAN_START) << 48;
    }
    return theNaN.dv;

    /* Generate IEEE 754 Not-A-Number. */
    //    return 0.0 / 0.0;
}
#endif /* !__BORLANDC__  && !_MSC_VER */

/**
 * @brief Return the byte stride for one real or complex value.
 * @ingroup vector_storage_private
 * @param[in] type Validated real or complex storage type.
 * @return Byte stride of one value; unsupported types panic.
 *
 * Uses sizeof(double) or sizeof(Rbc_Complex). Unsupported internal types panic;
 * this is not a recoverable public type validator.
 */
static size_t VectorElementSize(Rbc_VectorType type) {
    switch (type) {
    case RBC_VECTOR_REAL:
        return sizeof(double);
    case RBC_VECTOR_COMPLEX:
        return sizeof(Rbc_Complex);
    }
    Tcl_Panic("bad vector type %d", (int)type);
    return 0;
}

/**
 * @brief Validate a value count and convert it to an allocation byte count.
 * @ingroup vector_storage_private
 * @param[in] interp See @ref rbc_interp_argument.
 * @param[in] type Validated numeric storage type.
 * @param[in] count Nonnegative number of values, not bytes.
 * @param[out] byteCountPtr Required byte-count output; unchanged on failure.
 * @return TCL_OK on success; TCL_ERROR with a size diagnostic otherwise.
 *
 * Rejects negative counts and multiplication overflow with a Tcl error. Writes
 * the output only on success; calls VectorElementSize, so the type must already
 * be valid. Does not allocate storage or evaluate Tcl.
 */
static int GetVectorByteCount(Tcl_Interp *interp, Rbc_VectorType type, Tcl_Size count, size_t *byteCountPtr) {
    size_t elemSize;

    if (count < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector size cannot be negative", -1));
        return TCL_ERROR;
    }
    elemSize = VectorElementSize(type);
    if (elemSize == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown vector type", -1));
        return TCL_ERROR;
    }
    if ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / elemSize)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector size is too large", -1));
        return TCL_ERROR;
    }
    *byteCountPtr = (size_t)count * elemSize;
    return TCL_OK;
}

/**
 * @ingroup vector_storage_private
 * @brief Check logical liveness of a retained vector record.
 * @param[in] vPtr Valid retained record whose logical liveness is checked; its interpreter must also remain
 *   allocated.
 * @return TCL_OK if live; TCL_ERROR with RBC VECTOR DESTROYED otherwise.
 *
 * The caller must already protect storage lifetime; this cannot validate an arbitrary pointer. Rejects
 * destruction or interpreter deletion with RBC VECTOR DESTROYED. Does not check writability, name
 * identity or numeric storage stability.
 */
static int VectorAlive(VectorObject *vPtr) {
    if (vPtr->destroying || Tcl_InterpDeleted(vPtr->interp)) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector was destroyed during callback", -1));
        Tcl_SetErrorCode(vPtr->interp, "RBC", "VECTOR", "DESTROYED", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup vector_storage_private
 * @brief Release detached numeric storage according to its ownership policy.
 * @param[in] interp See @ref rbc_interp_argument. Caller protects its lifetime across a custom destructor.
 * @param[in] data Detached allocation to release, or NULL.
 * @param[in] freeProc TCL_STATIC, TCL_DYNAMIC, or a callable custom destructor. TCL_VOLATILE must already have been
 *   converted to owned storage.
 *
 * NULL storage and TCL_STATIC require no action; TCL_DYNAMIC uses ckfree. A custom release callback
 * runs with the outer interpreter state saved and restored. The caller must install the completed
 * replacement contract before calling, and separately protect record/interpreter lifetime across
 * arbitrary callback code.
 */
static void ReleaseVectorStorage(Tcl_Interp *interp, void *data, Tcl_FreeProc *freeProc) {
    if (data != NULL && freeProc != TCL_STATIC) {
        if (freeProc == TCL_DYNAMIC) ckfree(data);
        else {
            Tcl_InterpState state = Tcl_SaveInterpState(interp, TCL_OK);
            freeProc(data);
            Tcl_RestoreInterpState(interp, state);
        }
    }
}

/**
 * @ingroup vector_storage_private
 * @brief Publish an update while retaining the original vector identity.
 * @param[in,out] vPtr Valid borrowed record; retained internally across publication callbacks.
 * @param[in] all Nonzero publishes a full update; zero selects the interval.
 * @param[in] first Inclusive first storage index; ignored for a full update.
 * @param[in] last Inclusive last storage index; ignored for a full update.
 *
 * Preserves vector and interpreter, saves interpreter state, optionally flushes mapped-array cache,
 * then rechecks liveness before notifying all data or an inclusive interval. Callbacks may delete or
 * replace the vector; this never resolves a replacement by name. Restores state and releases both
 * records before returning.
 */
static void PublishVectorChange(VectorObject *vPtr, int all, Tcl_Size first, Tcl_Size last) {
    Tcl_Interp *interp = vPtr->interp;
    Tcl_InterpState state;

    Tcl_Preserve(interp);
    Tcl_Preserve(vPtr);
    state = Tcl_SaveInterpState(interp, TCL_OK);
    if (!vPtr->destroying && !Tcl_InterpDeleted(interp)) {
        if (vPtr->flush) Rbc_VectorFlushCache(vPtr);
        if (!vPtr->destroying && !Tcl_InterpDeleted(interp)) {
            if (all) Rbc_VectorUpdateClients(vPtr);
            else Rbc_VectorUpdateClientsRange(vPtr, first, last);
        }
    }
    Tcl_RestoreInterpState(interp, state);
    Tcl_Release(vPtr);
    Tcl_Release(interp);
}

/**
 * @ingroup vector_storage_private
 * @brief Validate a writable resize destination after callback activity.
 * @param[in] vPtr Retained destination to revalidate after callbacks.
 * @param[in] data Expected current storage address; compared without dereferencing.
 * @param[in] length Expected used length and full-selection extent.
 * @param[in] size Expected capacity in values.
 * @return TCL_OK if writable and matching the snapshot; TCL_ERROR otherwise.
 *
 * Requires a retained record. Checks liveness and read-only state, then compares data pointer, length,
 * capacity and full-vector selection against the caller snapshot. Reports RBC VECTOR CHANGED on layout
 * mismatch. Does not prove that numeric values remained unchanged or that no same-layout mutation
 * occurred.
 */
static int CheckResizedVector(VectorObject *vPtr, void *data, Tcl_Size length, Tcl_Size size) {
    if (VectorAlive(vPtr) != TCL_OK) return TCL_ERROR;
    if (Rbc_VectorCheckWritable(vPtr->interp, (Rbc_Vector *)vPtr) != TCL_OK) return TCL_ERROR;
    if (vPtr->data.raw != data || vPtr->length != length || vPtr->size != size ||
        vPtr->first != 0 || vPtr->last != length - 1) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector storage changed during callback", -1));
        Tcl_SetErrorCode(vPtr->interp, "RBC", "VECTOR", "CHANGED", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup vector_storage_private
 * @brief Retain a Tcl object as a variable-trace error result.
 * @param[in] objPtr Non-NULL error object; this helper adds the reference consumed by Tcl trace processing.
 * @return The retained object cast to char *, for TCL_TRACE_RESULT_OBJECT only.
 *
 * Increments the supplied object reference and returns it through the legacy char pointer signature.
 * Valid only with traces registered using TCL_TRACE_RESULT_OBJECT, which tells Tcl to consume an
 * object rather than interpret the pointer as message bytes.
 */
static char *VectorTraceError(Tcl_Obj *objPtr) {
    Tcl_IncrRefCount(objPtr);
    return (char *)objPtr;
}

/**
 * @brief Read cached finite extrema of a real vector.
 * Requires a live vector and two non-NULL output pointers. Returns TCL_ERROR for complex vectors
 * without changing outputs. Recomputes whole-vector extrema only when UPDATE_RANGE is set, otherwise
 * uses the cache. Empty or entirely nonfinite real vectors return TCL_OK with NaN bounds. Does not use
 * the current selection, flush mapped arrays or notify observers.
 */
int Rbc_VectorGetRange(Rbc_Vector *vecPtr, double *minPtr, double *maxPtr) {
    VectorObject *vPtr;

    vPtr = (VectorObject *)vecPtr;
    if (vPtr->type != RBC_VECTOR_REAL) {
        return TCL_ERROR;
    }
    /*
     * min/max are cached in the vector.  Recompute both in one pass
     * only when source data have invalidated that cache.
     */
    if (vPtr->notifyFlags & UPDATE_RANGE) {
        Rbc_VectorUpdateRange(vPtr);
    }
    *minPtr = vPtr->min;
    *maxPtr = vPtr->max;
    return TCL_OK;
}

/**
 * @brief Zero the half-open interval [first,last) of typed storage.
 * @ingroup vector_storage_private
 * @param[in] type Real or complex storage type.
 * @param[in] data Borrowed writable typed buffer; must cover the requested interval.
 * @param[in] first First storage index, inclusive; bounds are caller-validated.
 * @param[in] last End storage index, exclusive; must not exceed capacity.
 *
 * Zeros both components for complex values. Bounds and capacity are caller
 * preconditions; does not allocate, change metadata, invalidate extrema or notify.
 */
static void ZeroVectorElements(Rbc_VectorType type, VectorData data, Tcl_Size first, Tcl_Size last) {
    Tcl_Size i;

    switch (type) {
    case RBC_VECTOR_REAL:
        for (i = first; i < last; i++) {
            data.real[i] = 0.0;
        }
        break;
    case RBC_VECTOR_COMPLEX:
        for (i = first; i < last; i++) {
            data.complex[i].real = 0.0;
            data.complex[i].imag = 0.0;
        }
        break;
    }
}

/**
 * @ingroup vector_storage_private
 * @brief Register the vector ensemble and standalone simplifier.
 * @param[in] interp Owning interpreter receiving the ensemble and simplifier commands; see @ref
 *     rbc_interp_argument.
 * @return TCL_OK after registration, or TCL_ERROR on namespace-creation failure; earlier registration remains.
 *
 * Initializes the shared NaN value and obtains interpreter-local registry data. Creates rbc::vector,
 * ensures its command namespace exists, then registers rbc::vector::simplify. Returns TCL_ERROR if
 * namespace creation fails, otherwise TCL_OK. Registration is not transactional: an already installed
 * ensemble is not removed on that error. Registry cleanup belongs to the interpreter associated-data
 * delete procedure.
 */
int Rbc_VectorInit(Tcl_Interp *interp) {
    VectorInterpData *dataPtr; /* Interpreter-specific data. */
    
    rbcNaN = MakeNaN();
    dataPtr = Rbc_VectorGetInterpData(interp);
    Tcl_CreateObjCommand2(interp, "rbc::vector", VectorObjCmd, dataPtr, NULL);
    if (Tcl_FindNamespace(interp, "::rbc::vector", NULL, 0) == NULL) {
        if (Tcl_CreateNamespace(interp, "::rbc::vector", NULL, NULL) == NULL) {
            return TCL_ERROR;
        }
    }
    Tcl_CreateObjCommand2(interp, "::rbc::vector::simplify", VectorSimplifyObjCmd, dataPtr, NULL);
    return TCL_OK;
}

/**
 * @ingroup vector_storage_private
 * @brief Record deletion during a command rename transaction.
 * @param[in,out] clientData Caller-owned int flag, valid until the temporary trace is removed; set on deletion.
 * @param[in] interp Trace interpreter, unused here. See @ref rbc_interp_argument.
 * @param[in] oldName Borrowed old command name, unused here.
 * @param[in] newName Borrowed new name, possibly NULL for deletion; unused here.
 * @param[in] flags Trace-event mask; only TCL_TRACE_DELETE changes the flag.
 *
 * clientData points to a live caller-owned flag, set only for TCL_TRACE_DELETE. Ignores name strings
 * and never dereferences the vector, which user command traces may already have destroyed.
 */
static void VectorRenameTrace(ClientData clientData, Tcl_Interp *interp, const char *oldName, const char *newName,
                              int flags) {
    int *deletedPtr = clientData;
    (void)interp;
    (void)oldName;
    (void)newName;
    if (flags & TCL_TRACE_DELETE) {
        *deletedPtr = 1;
    }
}

/**
 * @ingroup vector_storage_private
 * @brief Rename vector identity and its command without copying storage.
 * @param[in] clientData Borrowed interpreter-owned VectorInterpData registry, valid throughout the call.
 * @param[in] interp Owning interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Argument count; the dispatcher checks ensemble-handler arity, while standalone simplify checks
 *     its own.
 * @param[in] objv Borrowed array. Full ensemble arguments; literal old and new names at indices 2 and 3.
 * @return TCL_OK with the qualified new name, otherwise TCL_ERROR; trace side effects are not undone.
 * @see @ref rbc_command_arguments
 *
 * Resolves a literal source name and an existing destination namespace, validates the destination
 * tail, and rejects registry or command collisions. A canonical unchanged name succeeds immediately.
 * If an instance command exists, invokes global ::rename with a temporary command trace to detect
 * deletion and recursive rename, then verifies the surviving command name. Rechecks registry collision
 * before moving the hash entry. Retains the same vector, storage, clients and mapped-array
 * association; the array name is not changed. Returns the qualified destination name. User trace
 * effects and an already completed command rename are not rolled back if a later check fails.
 */
static int VectorRenameObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorInterpData *dataPtr = clientData;
    VectorObject *vPtr = GetVectorObject(dataPtr, Tcl_GetString(objv[2]), NS_SEARCH_BOTH);
    Tcl_DString qualified;
    Tcl_Namespace *nsPtr;
    const char *tail, *name, *p;
    Tcl_Size depth = 0;
    Tcl_HashEntry *entry;
    int fresh, code = TCL_ERROR;

    (void)objc;
    if (vPtr == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't find vector \"%s\"", Tcl_GetString(objv[2])));
        return TCL_ERROR;
    }
    name = BuildQualifiedName(interp, Tcl_GetString(objv[3]), &qualified);
    if (ParseQualifiedName(interp, name, &nsPtr, &tail) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown namespace in \"%s\"", name));
        goto done;
    }
    for (p = tail; *p != '\0'; p++) {
        if (*p == '(') {
            depth++;
        } else if (*p == ')' && depth > 0) {
            depth--;
        } else if (!VECTOR_CHAR(*p)) {
            break;
        }
    }
    if (*tail == '\0' || *p != '\0' || depth != 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad vector name \"%s\"", name));
        goto done;
    }
    /* Use the same canonical namespace spelling as vector creation/lookups. */
    {
        Tcl_DString canonical;
        GetQualifiedName(nsPtr, tail, &canonical);
        Tcl_DStringSetLength(&qualified, 0);
        Tcl_DStringAppend(&qualified, Tcl_DStringValue(&canonical), -1);
        Tcl_DStringFree(&canonical);
        name = Tcl_DStringValue(&qualified);
    }
    if (strcmp(vPtr->name, name) == 0) {
        code = TCL_OK;
        goto done;
    }
    if (Tcl_FindHashEntry(&dataPtr->vectorTable, name) != NULL ||
        Tcl_FindCommand(interp, name, NULL, TCL_GLOBAL_ONLY) != NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector or command \"%s\" already exists", name));
        goto done;
    }
    if (vPtr->cmdToken != NULL) {
        Tcl_Command token = vPtr->cmdToken;
        Tcl_Obj *oldCommand = Tcl_NewObj();
        Tcl_Obj *newCommand = Tcl_NewStringObj(name, -1);
        Tcl_Obj *args[] = {Tcl_NewStringObj("::rename", -1), oldCommand, newCommand};
        int deleted = 0;
        for (int i = 0; i < 3; i++) {
            Tcl_IncrRefCount(args[i]);
        }
        Tcl_GetCommandFullName(interp, token, oldCommand);
        if (Tcl_CommandTraceInfo(interp, Tcl_GetString(oldCommand), TCL_TRACE_RENAME | TCL_TRACE_DELETE,
                                 VectorRenameTrace, NULL) != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector rename is already in progress", -1));
            code = TCL_ERROR;
        } else {
            Tcl_TraceCommand(interp, Tcl_GetString(oldCommand), TCL_TRACE_RENAME | TCL_TRACE_DELETE, VectorRenameTrace,
                             &deleted);
            code = Tcl_EvalObjv(interp, 3, args, TCL_EVAL_GLOBAL);
            if (!deleted) {
                Tcl_Obj *current = Tcl_NewObj();
                Tcl_IncrRefCount(current);
                Tcl_GetCommandFullName(interp, token, current);
                Tcl_UntraceCommand(interp, Tcl_GetString(current), TCL_TRACE_RENAME | TCL_TRACE_DELETE,
                                   VectorRenameTrace, &deleted);
                if (code == TCL_OK && strcmp(Tcl_GetString(current), name) != 0) {
                    Tcl_SetObjResult(interp, Tcl_NewStringObj("vector command changed during rename", -1));
                    code = TCL_ERROR;
                }
                Tcl_DecrRefCount(current);
            } else {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("vector was deleted during rename", -1));
                code = TCL_ERROR;
            }
        }
        for (int i = 0; i < 3; i++) {
            Tcl_DecrRefCount(args[i]);
        }
        if (code != TCL_OK) {
            goto done;
        }
    }
    /* Traces can create a destination vector even after the initial check. */
    entry = Tcl_CreateHashEntry(&dataPtr->vectorTable, name, &fresh);
    if (!fresh) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" was created during rename", name));
        code = TCL_ERROR;
        goto done;
    }
    Tcl_SetHashValue(entry, vPtr);
    Tcl_DeleteHashEntry(vPtr->hashPtr);
    vPtr->hashPtr = entry;
    vPtr->name = Tcl_GetHashKey(&dataPtr->vectorTable, entry);
    code = TCL_OK;
done:
    if (code == TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(name, -1));
    }
    Tcl_DStringFree(&qualified);
    return code;
}

/**
 * @ingroup vector_storage_private
 * @brief Measure distance from a finite point to a closed segment.
 * @param[in] ax Finite first segment endpoint X coordinate.
 * @param[in] ay Finite first segment endpoint Y coordinate.
 * @param[in] bx Finite second segment endpoint X coordinate.
 * @param[in] by Finite second segment endpoint Y coordinate.
 * @param[in] px Finite query point X coordinate.
 * @param[in] py Finite query point Y coordinate.
 * @return Nonnegative point-to-segment distance; may overflow the platform long-double range.
 *
 * Uses long-double differences, scaled projection and hypot rather than squaring raw coordinates. If
 * initial differences overflow, halves operands before subtraction and restores the factor afterward.
 * Handles coincident endpoints and projections beyond either endpoint. Returns nonnegative distance; a
 * distance outside the platform long-double range may be infinite. Callers validate finite input
 * coordinates.
 */
static long double SimplifyDistance(double ax, double ay, double bx, double by, double px, double py) {
    long double dx = (long double)bx - ax, dy = (long double)by - ay;
    long double ux = (long double)px - ax, uy = (long double)py - ay;
    long double scale, length, projection, factor = 1.0L;

    if (!isfinite(dx) || !isfinite(dy) || !isfinite(ux) || !isfinite(uy)) {
        dx = (long double)bx * 0.5L - (long double)ax * 0.5L;
        dy = (long double)by * 0.5L - (long double)ay * 0.5L;
        ux = (long double)px * 0.5L - (long double)ax * 0.5L;
        uy = (long double)py * 0.5L - (long double)ay * 0.5L;
        factor = 2.0L;
    }
    scale = fmaxl(fmaxl(fabsl(dx), fabsl(dy)), fmaxl(fabsl(ux), fabsl(uy)));
    if (scale == 0.0L) {
        return 0.0L;
    }
    dx /= scale;
    dy /= scale;
    ux /= scale;
    uy /= scale;
    length = hypotl(dx, dy);
    if (length == 0.0L) {
        return hypotl(ux, uy) * scale * factor;
    }
    dx /= length;
    dy /= length;
    projection = ux * dx + uy * dy;
    if (projection <= 0.0L) {
        return hypotl(ux, uy) * scale * factor;
    }
    if (projection >= length) {
        return hypotl(ux - length * dx, uy - length * dy) * scale * factor;
    }
    return fabsl(ux * dy - uy * dx) * scale * factor;
}

/**
 * @ingroup vector_storage_private
 * @brief Invalidate a temporary destination ID on vector destruction.
 * @param[in] interp Owning notification interpreter, unused here. See @ref rbc_interp_argument.
 * @param[in,out] clientData Caller-owned Rbc_VectorId slot, valid for the observer's lifetime.
 * @param[in] notify Notification kind; DESTROY clears the slot without freeing the server-owned client record.
 *
 * clientData addresses a live caller-owned ID slot. Destruction clears the slot before the server
 * frees remaining client records; update notifications leave it unchanged. Does not free the ID itself
 * or access a destroyed vector.
 */
static void SimplifyDeleted(Tcl_Interp *interp, ClientData clientData, Rbc_VectorNotify notify) {
    Rbc_VectorId *idPtr = clientData;
    (void)interp;
    if (notify == RBC_VECTOR_NOTIFY_DESTROY) {
        /* Vector destruction owns and frees its remaining client records. */
        *idPtr = NULL;
    }
}

/**
 * @ingroup vector_storage_private
 * @brief Refresh a simplified destination mapped array after commit.
 * @param[in,out] idPtr Required caller-owned observer-ID slot, possibly containing NULL; destruction callbacks
 *     clear it.
 *
 * Uses a temporary observer ID whose deletion callback clears the caller slot. Copies the array name
 * before unsetting and re-fetches the server after unset and sentinel write, restoring the trace only
 * while the server survives with the same name. Skips absent IDs, disabled flushing and unmapped
 * vectors. Trace side effects are not rolled back.
 */
static void SimplifyFlush(Rbc_VectorId *idPtr) {
    VectorObject *vPtr = (*idPtr != NULL) ? (*idPtr)->serverPtr : NULL;
    Tcl_Interp *interp;
    Tcl_DString name;
    int flags;

    if ((vPtr == NULL) || !vPtr->flush || (vPtr->arrayName == NULL)) {
        return;
    }
    interp = vPtr->interp;
    flags = vPtr->varFlags;
    Tcl_DStringInit(&name);
    Tcl_DStringAppend(&name, vPtr->arrayName, -1);
    Tcl_UntraceVar2(interp, Tcl_DStringValue(&name), NULL, TRACE_ALL | flags, (Tcl_VarTraceProc *)VectorVarTrace, vPtr);
    Tcl_UnsetVar2(interp, Tcl_DStringValue(&name), NULL, flags);
    vPtr = (*idPtr != NULL) ? (*idPtr)->serverPtr : NULL;
    if ((vPtr != NULL) && (vPtr->arrayName != NULL) && (strcmp(vPtr->arrayName, Tcl_DStringValue(&name)) == 0)) {
        Tcl_SetVar2(interp, Tcl_DStringValue(&name), "end", "", flags);
        vPtr = (*idPtr != NULL) ? (*idPtr)->serverPtr : NULL;
        if ((vPtr != NULL) && (vPtr->arrayName != NULL) && (strcmp(vPtr->arrayName, Tcl_DStringValue(&name)) == 0)) {
            Tcl_TraceVar2(interp, Tcl_DStringValue(&name), NULL, TRACE_ALL | flags, (Tcl_VarTraceProc *)VectorVarTrace,
                          vPtr);
        }
    }
    Tcl_DStringFree(&name);
}

/**
 * @ingroup vector_storage_private
 * @brief Simplify paired whole real vectors with staged output storage.
 * @param[in] clientData Borrowed interpreter-owned VectorInterpData registry, valid throughout the call.
 * @param[in] interp Owning interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Argument count; the dispatcher checks ensemble-handler arity, while standalone simplify checks
 *     its own.
 * @param[in] objv Borrowed array. Six standalone arguments: command, x, y, destX, destY, tolerance.
 * @return TCL_OK with the retained point count, otherwise TCL_ERROR before successful completion.
 * @see @ref rbc_command_arguments
 *
 * Requires finite coordinates, finite nonnegative tolerance, equal input lengths and distinct writable
 * existing destinations. Inputs may alias outputs. Iterative closed-segment Douglas-Peucker
 * subdivision computes both outputs before mutation. Validation/allocation failures leave destinations
 * unchanged. Commit installs both arrays before releasing old storage, flushing arrays or notifying
 * observers. Temporary IDs track destination deletion during callbacks. Preserves identity and
 * offsets, returns retained count, and does not roll back post-commit callback side effects.
 */
static int VectorSimplifyObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorInterpData *dataPtr = clientData;
    VectorObject *vectors[4];
    Rbc_VectorId ids[2] = {NULL, NULL};
    double *output[2] = {NULL, NULL};
    void *oldData[2];
    Tcl_FreeProc *oldFree[2];
    Tcl_Size *stack = NULL;
    Tcl_Size length, capacity, count = 0, low, top, i;
    double tolerance;
    int k;

    /* The dispatcher removes its own command word for the subcommand form. */
    if (objc != 6) {
        Tcl_WrongNumArgs(interp, 1, objv, "x y destX destY tolerance");
        return TCL_ERROR;
    }
    if (Tcl_GetDoubleFromObj(interp, objv[5], &tolerance) != TCL_OK) {
        return TCL_ERROR;
    }
    if (!isfinite(tolerance) || (tolerance < 0.0)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("tolerance must be finite and nonnegative", -1));
        return TCL_ERROR;
    }
    for (k = 0; k < 4; k++) {
        const char *name = Tcl_GetString(objv[k + 1]);

        vectors[k] = GetVectorObject(dataPtr, name, NS_SEARCH_BOTH);
        if (vectors[k] == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't find vector \"%s\"", name));
            return TCL_ERROR;
        }
        if (vectors[k]->type != RBC_VECTOR_REAL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("simplify requires real vectors", -1));
            return TCL_ERROR;
        }
    }
    for (k = 2; k < 4; k++) {
        if (Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vectors[k]) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (vectors[0]->length != vectors[1]->length) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("x and y vectors must have equal lengths", -1));
        return TCL_ERROR;
    }
    if (vectors[2] == vectors[3]) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("simplify destinations must be distinct vectors", -1));
        return TCL_ERROR;
    }
    length = vectors[0]->length;
    for (i = 0; i < length; i++) {
        if (!isfinite(vectors[0]->data.real[i]) || !isfinite(vectors[1]->data.real[i])) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("simplify requires finite coordinates", -1));
            return TCL_ERROR;
        }
    }
    capacity = (length > 0) ? length : 1;
    if (((Tcl_WideUInt)capacity > SIZE_MAX / sizeof(double)) ||
        ((Tcl_WideUInt)capacity > SIZE_MAX / sizeof(Tcl_Size))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("simplify allocation size is too large", -1));
        return TCL_ERROR;
    }
    output[0] = Tcl_AttemptAlloc((size_t)capacity * sizeof(double));
    output[1] = Tcl_AttemptAlloc((size_t)capacity * sizeof(double));
    stack = Tcl_AttemptAlloc((size_t)capacity * sizeof(Tcl_Size));
    if ((output[0] == NULL) || (output[1] == NULL) || (stack == NULL)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate simplify storage", -1));
        goto error;
    }
    if (length > 0) {
        output[0][count] = vectors[0]->data.real[0];
        output[1][count++] = vectors[1]->data.real[0];
    }
    low = 0;
    top = (length > 1) ? 0 : -1;
    stack[0] = length - 1;
    while (top >= 0) {
        Tcl_Size high = stack[top], split = -1;
        long double farthest = tolerance;
        const double *x = vectors[0]->data.real, *y = vectors[1]->data.real;

        for (i = low + 1; i < high; i++) {
            long double distance = SimplifyDistance(x[low], y[low], x[high], y[high], x[i], y[i]);

            if (distance > farthest) {
                farthest = distance;
                split = i;
            }
        }
        if (split >= 0) {
            stack[++top] = split;
        } else {
            output[0][count] = x[high];
            output[1][count++] = y[high];
            low = high;
            top--;
        }
    }
    for (k = 0; k < 2; k++) {
        ids[k] = Rbc_AllocVectorId(interp, vectors[k + 2]->name);
        if (ids[k] == NULL) {
            goto error;
        }
        Rbc_SetVectorChangedProc(ids[k], SimplifyDeleted, &ids[k]);
    }
    for (k = 0; k < 2; k++) {
        VectorObject *vPtr = vectors[k + 2];

        oldData[k] = vPtr->data.raw;
        oldFree[k] = vPtr->freeProc;
        vPtr->data.real = output[k];
        vPtr->freeProc = TCL_DYNAMIC;
        vPtr->length = count;
        vPtr->size = capacity;
        vPtr->first = 0;
        vPtr->last = count - 1;
        vPtr->min = vPtr->max = rbcNaN;
        vPtr->minIndex = vPtr->maxIndex = -1;
        vPtr->notifyFlags |= UPDATE_RANGE;
    }
    ckfree(stack);
    for (k = 0; k < 2; k++) {
        if ((oldData[k] != NULL) && (oldFree[k] != TCL_STATIC)) {
            if (oldFree[k] == TCL_DYNAMIC) {
                ckfree(oldData[k]);
            } else {
                oldFree[k](oldData[k]);
            }
        }
    }
    for (k = 0; k < 2; k++) {
        SimplifyFlush(&ids[k]);
        if (ids[k] != NULL) {
            Rbc_VectorUpdateClients(ids[k]->serverPtr);
        }
        if (ids[k] != NULL) {
            Rbc_FreeVectorId(ids[k]);
        }
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj(count));
    return TCL_OK;

error:
    for (k = 0; k < 2; k++) {
        if (output[k] != NULL) {
            ckfree(output[k]);
        }
        if (ids[k] != NULL) {
            Rbc_FreeVectorId(ids[k]);
        }
    }
    if (stack != NULL) {
        ckfree(stack);
    }
    return TCL_ERROR;
}

/**
 * @ingroup vector_storage_private
 * @brief Adapt ensemble syntax to the standalone simplify command.
 * @param[in] clientData Borrowed interpreter-owned VectorInterpData registry, valid throughout the call.
 * @param[in] interp Owning interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Argument count; the dispatcher checks ensemble-handler arity, while standalone simplify checks
 *     its own.
 * @param[in] objv Borrowed array. Seven full ensemble arguments; forwards objv+1 and objc-1 to the standalone
 *     handler.
 * @return The standalone simplifier status without additional copying or cleanup.
 * @see @ref rbc_command_arguments
 *
 * Borrows arguments and removes the ensemble command word by shifting the vector and count. The shared
 * implementation performs arity checking, validation, allocation and commit; no Tcl arguments or
 * vectors are copied by this adapter.
 */
static int VectorSimplifyOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return VectorSimplifyObjCmd(clientData, interp, objc - 1, objv + 1);
}

static const VectorOpSpec vectorOpCmd[] = {{{"create", 2, 0, "?vecName? ?switches...?"}, VectorCreateObjCmd},
                                           {{"destroy", 2, 0, "?vecName?..."}, VectorDestroyObjCmd},
                                           {{"expr", 3, 5, "?-name name? expression"}, VectorExprObjCmd},
                                           {{"names", 2, 3, "?pattern?..."}, VectorNamesObjCmd},
                                           {{"rename", 4, 4, "oldName newName"}, VectorRenameObjCmd},
                                           {{"simplify", 7, 7, "x y destX destY tolerance"}, VectorSimplifyOp},
                                           {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup vector_storage_private
 * @brief Validate and dispatch the vector ensemble operation.
 * @param[in] clientData Borrowed interpreter-owned VectorInterpData registry, valid throughout the call.
 * @param[in] interp Owning interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Argument count; the dispatcher checks ensemble-handler arity, while standalone simplify checks
 *     its own.
 * @param[in] objv Borrowed array. Full ensemble arguments: command at index 0, operation at index 1.
 * @return The selected handler status, or TCL_ERROR for operation/arity failure.
 * @see @ref rbc_command_arguments
 *
 * Uses the typed command table to check name and arity before calling the selected handler. Borrows
 * interpreter data and arguments, returning the handler status. Does not itself create vectors or
 * implement transaction rollback.
 */
static int VectorObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, vectorOpCmd, (Tcl_Size)sizeof(vectorOpCmd[0]), RBC_OP_ARG1, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    return vectorOpCmd[index].proc(clientData, interp, objc, objv);
}

/**
 * @ingroup vector_storage_private
 * @brief Consume one boolean creation-option argument.
 * @param[in] clientData Borrowed NUL-terminated option-name string used for missing-value diagnostics.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Number of remaining option-value arguments.
 * @param[in] objv Borrowed value slice, with the option value at index zero.
 * @param[out] dstPtr Required int receiving the converted boolean, changed only on success.
 * @return One consumed value, or -1 on error; this is not a Tcl status code.
 *
 * Returns one consumed argument or minus one with a diagnostic. Uses the borrowed clientData option
 * name for a missing-value error. Writes the destination integer only after successful conversion;
 * does not retain the Tcl object.
 */
static Tcl_Size ParseBool(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], void *dstPtr) {
    int value;
    int *dst;
    const char *optionName;

    dst = (int *)dstPtr;
    optionName = (const char *)clientData;
    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("option \"%s\" requires a boolean value", optionName));
        return -1;
    }
    if (Tcl_GetBooleanFromObj(interp, objv[0], &value) != TCL_OK) {
        return -1;
    }
    *dst = value;
    return 1;
}

/**
 * @ingroup vector_storage_private
 * @brief Consume one Tcl_Size creation-option argument.
 * @param[in] clientData Borrowed NUL-terminated option-name string used for missing-value diagnostics.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Number of remaining option-value arguments.
 * @param[in] objv Borrowed value slice, with the option value at index zero.
 * @param[out] dstPtr Required Tcl_Size receiving the parsed integer, changed only on success.
 * @return One consumed value, or -1 on error; this is not a Tcl status code.
 *
 * Returns one on success or minus one on missing/invalid input. Writes only after conversion. This
 * parses a size integer, not the full vector length policy; caller checks allowed ranges and
 * allocation limits.
 */
static Tcl_Size ParseVectorLength(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[],
                                  void *dstPtr) {
    Tcl_Size value;
    const char *optionName;

    optionName = (const char *)clientData;
    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("option \"%s\" requires an additional argument", optionName));
        return -1;
    }
    if (Tcl_GetSizeIntFromObj(interp, objv[0], &value) != TCL_OK) {
        return -1;
    }
    *(Tcl_Size *)dstPtr = value;
    return 1;
}

/**
 * @ingroup vector_storage_private
 * @brief Convert a NUL-terminated string to Tcl_Size.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] string Borrowed NUL-terminated integer spelling, copied into a temporary Tcl object.
 * @param[out] valuePtr Required Tcl_Size output; consume only on successful conversion.
 * @return TCL_OK or TCL_ERROR from Tcl integer conversion.
 *
 * Owns a temporary retained Tcl object for conversion and releases it on either outcome. Returns the
 * conversion status; does not evaluate an expression or validate vector-specific length limits.
 */
static int GetSizeFromString(Tcl_Interp *interp, const char *string, Tcl_Size *valuePtr) {
    Tcl_Obj *objPtr;
    int result;

    objPtr = Tcl_NewStringObj(string, -1);
    Tcl_IncrRefCount(objPtr);
    result = Tcl_GetSizeIntFromObj(interp, objPtr, valuePtr);
    Tcl_DecrRefCount(objPtr);
    return result;
}

/** @brief Explicit vector creation type parsed before allocating storage.
 * @ingroup vector_storage_private
 */
typedef struct {
    Rbc_VectorType type; /**< Requested real or complex storage type. */
    int specified; /**< Nonzero when the caller explicitly supplied a type option. */
} VectorTypeOption;

/**
 * @ingroup vector_storage_private
 * @brief Consume an explicit real or complex creation type.
 * @param[in] clientData Borrowed NUL-terminated option-name string used for missing-value diagnostics.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Number of remaining option-value arguments.
 * @param[in] objv Borrowed value slice, with the option value at index zero.
 * @param[out] dstPtr Required VectorTypeOption receiving the type and specified flag, changed only on success.
 * @return One consumed value, or -1 on error; this is not a Tcl status code.
 *
 * Accepts exact lowercase names only. On success stores type and marks specified, distinguishing
 * explicit selection from the caller default. Returns one consumed argument, or minus one without
 * changing option state on error.
 */
static Tcl_Size ParseVectorType(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[],
                                void *dstPtr) {
    VectorTypeOption *optionPtr;
    const char *string;
    const char *optionName;

    optionPtr = (VectorTypeOption *)dstPtr;
    optionName = (const char *)clientData;
    if (objc < 1) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("option \"%s\" requires an additional argument", optionName));
        return -1;
    }
    string = Tcl_GetString(objv[0]);
    if (strcmp(string, "real") == 0) {
        optionPtr->type = RBC_VECTOR_REAL;
    } else if (strcmp(string, "complex") == 0) {
        optionPtr->type = RBC_VECTOR_COMPLEX;
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad value \"%s\" for -type: must be real or complex", string));
        return -1;
    }
    optionPtr->specified = TRUE;
    return 1;
}

/**
 * @ingroup vector_storage_private
 * @brief Parse vector creation options and process names sequentially.
 * @param[in] clientData Borrowed interpreter-owned VectorInterpData registry, valid throughout the call.
 * @param[in] interp Owning interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Argument count; the dispatcher checks ensemble-handler arity, while standalone simplify checks
 *     its own.
 * @param[in] objv Borrowed array. Full ensemble arguments; names and creation options start at index 2.
 * @return TCL_OK with the final qualified name, otherwise TCL_ERROR; earlier creations remain.
 * @see @ref rbc_command_arguments
 *
 * Borrows ensemble arguments and owns the parsed name array, work string and result object. Prechecks
 * existing candidate vectors for read-only state before processing names. Exact registered names take
 * precedence over legacy numeric size/range suffixes; -literal disables suffix interpretation.
 * Explicit type changes are rejected. Real names without parentheses receive default array mappings;
 * complex or parenthesized names do not. Positive requested lengths resize storage; zero does not
 * truncate an existing vector. Retains each returned vector and interpreter across resize and
 * publication, checks liveness, and returns the last qualified name. Later failures do not roll back
 * earlier creations, reconfiguration or callback effects.
 */
static int VectorCreateObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorInterpData *dataPtr = clientData;
    VectorObject *vPtr;
    Tcl_Obj *resultPtr; /* for the result of this function */
    char *cmdName, *varName;
    VectorTypeOption typeOption;
    int freeOnUnset;
    int flush;
    int literal;
    Tcl_Size defLen;
    Tcl_Obj **objNameArray; /* holds all vector names specified */
    Tcl_Size count;
    Tcl_DString ds;
    Tcl_Size i;
    const Tcl_ArgvInfo argsTable[] = {{TCL_ARGV_STRING, "-command", NULL, &cmdName, NULL, NULL},
                                      {TCL_ARGV_GENFUNC, "-flush", ParseBool, &flush, NULL, "-flush"},
                                      {TCL_ARGV_GENFUNC, "-length", ParseVectorLength, &defLen, NULL, "-length"},
                                      {TCL_ARGV_GENFUNC, "-literal", ParseBool, &literal, NULL, "-literal"},
                                      {TCL_ARGV_GENFUNC, "-type", ParseVectorType, &typeOption, NULL, "-type"},
                                      {TCL_ARGV_STRING, "-variable", NULL, &varName, NULL, NULL},
                                      {TCL_ARGV_GENFUNC, "-watchunset", ParseBool, &freeOnUnset, NULL, "-watchunset"},
                                      TCL_ARGV_TABLE_END};

    /*
     * Handle switches to the vector command and collect the vector
     * name arguments into an array.
     */
    varName = NULL;  /* name of Tcl variable to link to the vector */
    cmdName = NULL;  /* name of Tcl command to link to vector */
    freeOnUnset = 0; /* value of the user level '-watchunset' switch */
    defLen = 0;      /* default vector length */
    flush = FALSE;
    literal = FALSE;
    typeOption.type = RBC_VECTOR_REAL;
    typeOption.specified = FALSE;
    count = objc - 1; /* start at "create" */
    if (Tcl_ParseArgsObjv(interp, argsTable, &count, objv + 1, &objNameArray)) {
        return TCL_ERROR;
    }
    /* finished parsing arguments -> do some sanity checks: */
    Tcl_DStringInit(&ds);
    resultPtr = Tcl_NewObj();
    Tcl_IncrRefCount(resultPtr);
    if (defLen < 0) {
        Tcl_AppendStringsToObj(resultPtr,
                               "value for \"-length\" option "
                               "must be zero or greater",
                               NULL);
        Tcl_SetObjResult(interp, resultPtr);
        goto error;
    }
    if (count == 1) {
        /* T_PAO always returns at least 1, "create" */
        Tcl_AppendStringsToObj(resultPtr, "no vector names supplied", NULL);
        Tcl_SetObjResult(interp, resultPtr);
        goto error;
    } else if (count > 2) {
        /* more than one vector was specified */
        if ((cmdName != NULL) && (cmdName[0] != '\0')) {
            Tcl_AppendStringsToObj(resultPtr, "can't specify more than one vector with \"-command\" switch", NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
        if ((varName != NULL) && (varName[0] != '\0')) {
            Tcl_AppendStringsToObj(resultPtr, "can't specify more than one vector with \"-variable\" switch", NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
    }
    /* Now process the vector names and check their validity
     * Use a Tcl_DString as workhorse since this code writes to the
     * char buffer.
     * Also, we start at 1 since objNameArray[0] holds "create"
     */
    vPtr = NULL;
    for (i = 1; i < count; i++) {
        VectorObject *existingPtr;
        char *name;
        char *suffix;

        Tcl_DStringSetLength(&ds, 0);
        Tcl_DStringAppend(&ds, Tcl_GetString(objNameArray[i]), -1);
        name = Tcl_DStringValue(&ds);
        existingPtr = GetVectorObject(dataPtr, name, NS_SEARCH_BOTH);
        if ((existingPtr == NULL) && !literal) {
            suffix = strrchr(name, '(');
            if (suffix != NULL) {
                *suffix = '\0';
                existingPtr = GetVectorObject(dataPtr, name, NS_SEARCH_BOTH);
            }
        }
        if ((existingPtr != NULL) &&
            (Rbc_VectorCheckWritable(interp, (Rbc_Vector *)existingPtr) != TCL_OK)) {
            goto error;
        }
    }
    for (i = 1; i < count; i++) {
        char *leftParen, *rightParen; /* positions of left and right parens in vector specification */
        int isNew;
        Tcl_Size size;
        Tcl_Size first;
        Tcl_Size last;
        char *vecName; /* name of a vector */
        const char *createVarName;
        VectorObject *existingPtr;
        Rbc_VectorType effectiveType;

        Tcl_DStringFree(&ds);
        Tcl_DStringAppend(&ds, Tcl_GetString(objNameArray[i]), -1);
        vecName = Tcl_DStringValue(&ds);
        size = defLen; /* set to default value */
        first = last = 0;
        /* An exact existing name wins over the legacy name(size) syntax.
         * Otherwise only a numeric final group is a size/range suffix.
         * -literal permits numeric parentheses in a new vector's name. */
        leftParen = NULL;
        rightParen = NULL;
        if (!literal && GetVectorObject(dataPtr, vecName, NS_SEARCH_BOTH) == NULL) {
            Tcl_Size depth = 0;
            char *group = NULL;
            for (char *p = vecName; *p != '\0'; p++) {
                if (*p == '(') {
                    if (depth++ == 0) {
                        group = p;
                    }
                } else if (*p == ')') {
                    if (depth == 0) {
                        break;
                    }
                    if (--depth == 0 && p[1] == '\0') {
                        const char *q = group + 1;
                        while (isspace(UCHAR(*q))) {
                            q++;
                        }
                        if (isdigit(UCHAR(*q)) || *q == '+' || *q == '-' || *q == ':' || *q == ')') {
                            leftParen = group;
                            rightParen = p;
                        }
                    }
                }
            }
        }
        if (leftParen != NULL) {
            int result;
            char *colon;

            *rightParen = '\0';
            colon = strchr(leftParen + 1, ':');
            if (colon != NULL) {
                /* Specification is in the form vecName(first:last) */
                *colon = '\0';
                result = GetSizeFromString(interp, leftParen + 1, &first);
                if ((*(colon + 1) != '\0') && (result == TCL_OK)) {
                    result = GetSizeFromString(interp, colon + 1, &last);
                    if (first > last) {
                        Tcl_AppendStringsToObj(resultPtr, "bad vector range \"", vecName, "\"", NULL);
                        Tcl_SetObjResult(interp, resultPtr);
                        result = TCL_ERROR;
                    } else if ((first < 0) && (last >= (TCL_SIZE_MAX + first))) {
                        Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector range \"%s\" is too large", vecName));
                        result = TCL_ERROR;
                    } else {
                        Tcl_Size difference;
                        difference = last - first;
                        if (difference == TCL_SIZE_MAX) {
                            Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector range \"%s\" is too large", vecName));
                            result = TCL_ERROR;
                        } else {
                            size = difference + 1;
                        }
                    }
                }
                *colon = ':';
            } else {
                /* Specification is in the form vecName(size) */
                result = GetSizeFromString(interp, leftParen + 1, &size);
            }
            *rightParen = ')';
            if (result != TCL_OK) {
                goto error;
            }
            if (size < 0) {
                Tcl_AppendStringsToObj(resultPtr, "bad vector size \"", vecName, "\"", NULL);
                Tcl_SetObjResult(interp, resultPtr);
                goto error;
            }
        }
        if (leftParen != NULL) {
            *leftParen = '\0';
        }
        existingPtr = GetVectorObject(dataPtr, vecName, NS_SEARCH_BOTH);
        if (existingPtr != NULL) {
            if (typeOption.specified && (existingPtr->type != typeOption.type)) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't change vector \"%s\" from type \"%s\" to \"%s\"",
                                                       existingPtr->name,
                                                       (existingPtr->type == RBC_VECTOR_REAL) ? "real" : "complex",
                                                       (typeOption.type == RBC_VECTOR_REAL) ? "real" : "complex"));
                goto error;
            }
            effectiveType = existingPtr->type;
        } else {
            effectiveType = typeOption.type;
        }
        /*
         * actually create the vector:
         */
        if (effectiveType == RBC_VECTOR_COMPLEX || strchr(vecName, '(') != NULL) {
            /*
             * Complex vectors and names with parentheses don't receive an automatic Tcl array mapping,
             * but an explicitly requested mapping is allowed.
             */
            createVarName = (varName == NULL) ? NULL : varName;
        } else {
            createVarName = (varName == NULL) ? vecName : varName;
        }
        vPtr = Rbc_VectorCreate(dataPtr, vecName, (cmdName == NULL) ? vecName : cmdName, createVarName, effectiveType,
                                &isNew);
        if (leftParen != NULL) {
            *leftParen = '(';
        }
        if (vPtr == NULL) {
            goto error;
        }
        Tcl_Preserve(interp);
        Tcl_Preserve(vPtr);
        vPtr->freeOnUnset = freeOnUnset;
        vPtr->flush = flush;
        vPtr->offset = first;
        if (size > 0) {
            if (Rbc_VectorChangeLength(vPtr, size) != TCL_OK) {
                Tcl_Release(vPtr);
                Tcl_Release(interp);
                goto error;
            }
        }
        if (!isNew) {
            PublishVectorChange(vPtr, TRUE, 0, 0);
        }
        if (VectorAlive(vPtr) != TCL_OK) {
            Tcl_Release(vPtr);
            Tcl_Release(interp);
            goto error;
        }
        Tcl_SetStringObj(resultPtr, vPtr->name, -1);
        Tcl_Release(vPtr);
        Tcl_Release(interp);
    }
    ckfree(objNameArray);
    Tcl_DStringFree(&ds);
    Tcl_SetObjResult(interp, resultPtr);
    Tcl_DecrRefCount(resultPtr);
    return TCL_OK;

error:
    Tcl_DecrRefCount(resultPtr);
    ckfree(objNameArray);
    Tcl_DStringFree(&ds);
    return TCL_ERROR;
}

/**
 * @ingroup vector_storage_private
 * @brief Destroy named vectors in argument order.
 * @param[in] clientData Borrowed interpreter-owned VectorInterpData registry, valid throughout the call.
 * @param[in] interp Owning interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Argument count; the dispatcher checks ensemble-handler arity, while standalone simplify checks
 *     its own.
 * @param[in] objv Borrowed array. Full ensemble arguments; references to destroy start at index 2.
 * @return TCL_OK after all requested destructions, otherwise TCL_ERROR; earlier destructions remain.
 * @see @ref rbc_command_arguments
 *
 * Uses the range-aware lookup helper, then destroys the entire returned vector, not only its selected
 * interval. Stops on the first lookup error; earlier destruction and observer side effects remain. No
 * names is a successful no-op. Read-only state protects data mutation, not destruction. Lookup and
 * destruction may run callbacks; this handler does not stage or retain all targets in advance.
 */
static int VectorDestroyObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    /* Not Implemented Correctly */

    VectorInterpData *dataPtr = clientData;
    VectorObject *vPtr;
    Tcl_Size i;
    for (i = 2; i < objc; i++) {
        if (Rbc_VectorLookupName(dataPtr, Tcl_GetString(objv[i]), &vPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        Rbc_VectorFree(vPtr);
    }
    return TCL_OK;
}

/**
 * @ingroup vector_storage_private
 * @brief Evaluate an expression as Tcl values or a newly named vector.
 * @param[in] clientData Unused ensemble registry pointer; the handler obtains registry data from interp.
 * @param[in] interp Owning evaluation interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Three for list output, or five for named output; other counts are rejected.
 * @param[in] objv Full ensemble array: expression at index 2, or -name, name, expression at indices 2 through 4.
 * @return TCL_OK with copied values or the new qualified name, otherwise TCL_ERROR; evaluation side effects
 *     remain.
 *
 * Without -name delegates directly to Rbc_ExprVector. Named output resolves an existing destination
 * namespace and rejects vector or command collisions before evaluation and again afterward. Evaluates
 * once into an owned temporary, creates an unmapped vector of the resulting type, and transfers its
 * storage without Tcl-list conversion. A final `#auto` component requests generated naming. Returns the
 * qualified new name; never overwrites an existing vector. Frees the temporary and work strings on
 * every exit. Expression callback side effects are not rolled back.
 */
static int VectorExprObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorInterpData *dataPtr = Rbc_VectorGetInterpData(interp);
    VectorObject *resultPtr = NULL;
    VectorObject *vPtr;
    Tcl_DString qualified;
    Tcl_DString canonical;
    Tcl_Namespace *nsPtr;
    const char *name;
    const char *tail;
    int isNew;
    int isAutoName;
    int result = TCL_ERROR;

    (void)clientData;
    if (objc == 3) {
        return Rbc_ExprVector(interp, Tcl_GetString(objv[2]), (Rbc_Vector *)NULL);
    }
    if ((objc != 5) || (strcmp(Tcl_GetString(objv[2]), "-name") != 0)) {
        Tcl_WrongNumArgs(interp, 2, objv, "?-name name? expression");
        return TCL_ERROR;
    }
    name = BuildQualifiedName(interp, Tcl_GetString(objv[3]), &qualified);
    Tcl_DStringInit(&canonical);
    if (ParseQualifiedName(interp, name, &nsPtr, &tail) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown namespace in \"%s\"", name));
        goto done;
    }
    isAutoName = (strcmp(tail, "#auto") == 0);
    name = GetQualifiedName(nsPtr, tail, &canonical);
    if (!isAutoName && ((Tcl_FindHashEntry(&dataPtr->vectorTable, name) != NULL) ||
                        (Tcl_FindCommand(interp, name, NULL, TCL_GLOBAL_ONLY) != NULL))) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector or command \"%s\" already exists", name));
        goto done;
    }
    if (Rbc_VectorEvaluate(interp, Tcl_GetString(objv[4]), &resultPtr) != TCL_OK) {
        goto done;
    }
    /* Callbacks may have created the target while evaluating the expression. */
    if (!isAutoName && ((Tcl_FindHashEntry(&dataPtr->vectorTable, name) != NULL) ||
                        (Tcl_FindCommand(interp, name, NULL, TCL_GLOBAL_ONLY) != NULL))) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector or command \"%s\" already exists", name));
        goto done;
    }
    /* Do not map an array: creation must not invoke user variable traces. */
    vPtr = Rbc_VectorCreate(dataPtr, name, name, NULL, resultPtr->type, &isNew);
    if (vPtr == NULL) {
        goto done;
    }
    /* Transfer ownership of the temporary's storage to the new empty vector. */
    vPtr->data = resultPtr->data;
    vPtr->length = resultPtr->length;
    vPtr->size = resultPtr->size;
    vPtr->freeProc = resultPtr->freeProc;
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;
    resultPtr->data.raw = NULL;
    resultPtr->length = resultPtr->size = 0;
    Tcl_SetObjResult(interp, Tcl_NewStringObj(vPtr->name, -1));
    result = TCL_OK;

done:
    if (resultPtr != NULL) {
        Rbc_VectorFree(resultPtr);
    }
    Tcl_DStringFree(&canonical);
    Tcl_DStringFree(&qualified);
    return result;
}

/**
 * @ingroup vector_storage_private
 * @brief List registered vector names with an optional glob filter.
 * @param[in] clientData Borrowed interpreter-owned VectorInterpData registry, valid throughout the call.
 * @param[in] interp Owning interpreter; see @ref rbc_interp_argument.
 * @param[in] objc Argument count; the dispatcher checks ensemble-handler arity, while standalone simplify checks
 *     its own.
 * @param[in] objv Borrowed array. Full ensemble arguments; optional glob pattern at index 2.
 * @return TCL_OK with a list of qualified registry names in hash traversal order.
 * @see @ref rbc_command_arguments
 *
 * Walks the interpreter vector hash table, appending fully qualified keys to a new Tcl list. Matching
 * uses Tcl_StringMatch on the full name. Order follows hash traversal and is not sorted. Does not
 * enumerate Tcl aliases, instance command spellings or mapped arrays, and does not invoke vector
 * observers.
 */
static int VectorNamesObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    VectorInterpData *dataPtr = clientData;
    Tcl_HashEntry *hPtr;
    const char *name;
    Tcl_HashSearch cursor;
    Tcl_Obj *resultPtr;

    resultPtr = Tcl_NewListObj(0, NULL);
    for (hPtr = Tcl_FirstHashEntry(&(dataPtr->vectorTable), &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        name = Tcl_GetHashKey(&(dataPtr->vectorTable), hPtr);
        if ((objc == 2) || (Tcl_StringMatch(name, Tcl_GetString(objv[2])))) {
            Tcl_ListObjAppendElement(interp, resultPtr, Tcl_NewStringObj(name, -1));
        }
    }
    Tcl_SetObjResult(interp, resultPtr);
    return TCL_OK;
}

/**
 * @brief Obtain or lazily initialize an interpreter-local vector registry.
 * @ingroup vector_storage_private
 * @param[in] interp See @ref rbc_interp_argument. Owns the lazily allocated registry.
 * @return Borrowed interpreter-associated registry; no ownership transfer.
 *
 * Returns borrowed associated data. First access allocates the vector, math and
 * special-index tables, installs built-in functions and registers interpreter
 * cleanup. Does not create Tcl commands or establish thread-shared storage.
 */
VectorInterpData *Rbc_VectorGetInterpData(Tcl_Interp *interp) {
    VectorInterpData *dataPtr;
    Tcl_InterpDeleteProc *proc;

    dataPtr = (VectorInterpData *)Tcl_GetAssocData(interp, VECTOR_THREAD_KEY, &proc);
    if (dataPtr == NULL) {
        dataPtr = (VectorInterpData *)ckalloc(sizeof(VectorInterpData));
        /***    assert(dataPtr); */
        dataPtr->interp = interp;
        dataPtr->nextId = 0;
        Tcl_SetAssocData(interp, VECTOR_THREAD_KEY, VectorInterpDeleteProc, dataPtr);
        Tcl_InitHashTable(&(dataPtr->vectorTable), TCL_STRING_KEYS);
        Tcl_InitHashTable(&(dataPtr->mathProcTable), TCL_STRING_KEYS);
        Tcl_InitHashTable(&(dataPtr->indexProcTable), TCL_STRING_KEYS);
        Rbc_VectorInstallMathFunctions(&(dataPtr->mathProcTable));
        Rbc_VectorInstallSpecialIndices(&(dataPtr->indexProcTable));
#ifdef HAVE_SRAND48
//        what does this do??
//        srand48(time((time_t *) NULL));
#endif
    }
    return dataPtr;
}

/**
 * @brief Destroy registered vectors and release interpreter-local tables.
 * @ingroup vector_storage_private
 * @param[in] clientData Owned VectorInterpData associated with the interpreter; released here.
 * @param[in] interp See @ref rbc_interp_argument. Interpreter undergoing teardown.
 *
 * Repeatedly takes the first remaining registry entry because destruction mutates
 * the table and invokes callbacks. Deletes the math/index table containers and
 * associated data, then frees the registry record. This is interpreter teardown,
 * not a general-purpose registry reset API.
 */
static void VectorInterpDeleteProc(ClientData clientData, Tcl_Interp *interp) {
    VectorInterpData *dataPtr = clientData;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    VectorObject *vPtr;

    while ((hPtr = Tcl_FirstHashEntry(&dataPtr->vectorTable, &cursor)) != NULL) {
        vPtr = (VectorObject *)Tcl_GetHashValue(hPtr);
        Rbc_VectorFree(vPtr);
    }
    Tcl_DeleteHashTable(&(dataPtr->vectorTable));
    /* If any user-defined math functions were installed, remove them.  */
    Tcl_DeleteHashTable(&(dataPtr->mathProcTable));
    Tcl_DeleteHashTable(&(dataPtr->indexProcTable));
    Tcl_DeleteAssocData(interp, VECTOR_THREAD_KEY);
    ckfree((char *)dataPtr);
}

/**
 * @brief Allocate an unregistered empty real vector record.
 * @ingroup vector_storage_private
 * @param[in] dataPtr Borrowed owning-interpreter registry; must outlive the new record.
 * @return Owned empty record, released with Rbc_VectorFree.
 *
 * Initializes static NULL storage, zero length/capacity, an empty observer chain,
 * idle notifications, invalid extrema and no array flushing. The caller owns the
 * record and may register it or use it as a temporary; release with Rbc_VectorFree.
 * Does not create a name, Tcl command, mapped array or numeric allocation.
 */
VectorObject *Rbc_VectorNew(VectorInterpData *dataPtr) {
    VectorObject *vPtr;

    vPtr = RbcCalloc(1, sizeof(VectorObject));
    vPtr->type = RBC_VECTOR_REAL;
    vPtr->notifyFlags = NOTIFY_WHENIDLE | UPDATE_RANGE;
    vPtr->freeProc = TCL_STATIC;
    vPtr->dataPtr = dataPtr;
    vPtr->data.raw = NULL;
    vPtr->length = vPtr->size = 0;
    vPtr->interp = dataPtr->interp;
    vPtr->hashPtr = NULL;
    vPtr->chainPtr = Rbc_ChainCreate();
    vPtr->flush = FALSE;
    vPtr->min = vPtr->max = rbcNaN;
    vPtr->minIndex = vPtr->maxIndex = -1;
    return vPtr;
}

/**
 * @brief Create or reuse a registered vector and configure its access surfaces.
 * @ingroup vector_storage_private
 * @param[in] dataPtr Borrowed registry receiving or reusing the identity.
 * @param[in] vecName Borrowed requested name; \#auto tail generates an unused name.
 * @param[in] cmdName Borrowed instance command name, or NULL to omit command setup.
 * @param[in] varName Borrowed array mapping name; NULL omits setup and empty requests unmapping.
 * @param[in] type Requested real/complex type; an existing identity must match.
 * @param[out] newPtr Required output: nonzero for a new identity, zero for reuse, written only on success.
 * @return Borrowed registered identity on success, or NULL with a diagnostic.
 *
 * Qualifies the name in the current namespace; \#auto selects an unused registry
 * and command name. Existing vectors must be writable and have the requested
 * type. Checks command collisions, replaces an existing instance command, and
 * optionally maps an array. NULL command/variable arguments suppress that setup.
 * Returns a borrowed record and writes newPtr on success, or NULL on error.
 * Failure frees a newly created record, but does not roll back existing-object
 * changes. Retains the record and interpreter across command deletion and mapping,
 * and rejects destruction, new protection or replacement commands before continuing.
 */
VectorObject *Rbc_VectorCreate(VectorInterpData *dataPtr, const char *vecName, const char *cmdName, const char *varName,
                               Rbc_VectorType type, int *newPtr) {
    Tcl_Obj *resultPtr = Tcl_NewStringObj("", -1);
    VectorObject *vPtr;
    int isNew;
    int isAutoName = 0;       /* is the name autmatically generated? */
    char *qualVecName = NULL; /* qualified name of the vector */
    const char *vecNameTail;  /* the name of the vector without namespace */
    Tcl_Namespace *nsPtr;     /* namespace of the vector name */
    Tcl_HashEntry *hPtr;
    Tcl_Interp *interp = dataPtr->interp;
    Tcl_DString qualVecNamePtr;
    int held = FALSE;
    char *savedVarName = NULL;

    Tcl_IncrRefCount(resultPtr);
    isNew = 0;
    nsPtr = NULL;
    vPtr = NULL;
    if (varName != NULL && (strchr(varName, '(') != NULL || strchr(varName, ')') != NULL)) {
        Tcl_SetObjResult(
            interp,
            Tcl_NewStringObj(
                "mapped array name must not contain parentheses; use a separate array name or an empty name", -1));
        Tcl_DecrRefCount(resultPtr);
        return NULL;
    }
    Tcl_Preserve(interp);
    if (varName != NULL) {
        savedVarName = RbcStrdup(varName);
    }
    varName = savedVarName;
    /* process the vector name: */
    vecName = BuildQualifiedName(interp, vecName, &qualVecNamePtr);
    if (ParseQualifiedName(interp, vecName, &nsPtr, &vecNameTail) != TCL_OK) {
        Tcl_AppendStringsToObj(resultPtr, "unknown namespace in \"", vecName, "\"", NULL);
        Tcl_SetObjResult(interp, resultPtr);
        goto error;
    }
    if ((vecNameTail[0] == '#') && (strcmp(vecNameTail, "#auto") == 0)) {
        /* generate a unique automatic name for the vector: */
        char string[200];

        do {
            snprintf(string, sizeof(string), "vector%d", dataPtr->nextId++);
            qualVecName = GetQualifiedName(nsPtr, string, &qualVecNamePtr);
            hPtr = Tcl_FindHashEntry(&(dataPtr->vectorTable), qualVecName);
        } while ((hPtr != NULL) || (Tcl_FindCommand(interp, qualVecName, NULL, TCL_GLOBAL_ONLY) != NULL));
        isAutoName = 1;
    } else {
        /* Parentheses are permitted in literal names, but must balance.
         * Keep operators/whitespace out of names so expression tokenization
         * and the legacy range syntax remain unambiguous. */
        const char *p;
        Tcl_Size depth = 0;
        for (p = vecNameTail; *p != '\0'; p++) {
            if (*p == '(') {
                depth++;
            } else if (*p == ')' && depth > 0) {
                depth--;
            } else if (!VECTOR_CHAR(*p)) {
                break;
            }
        }
        if (*p != '\0' || depth != 0 || p == vecNameTail) {
            Tcl_AppendStringsToObj(resultPtr, "bad vector name \"", vecName,
                                   "\": must contain digits, letters, underscore, or period with balanced parentheses",
                                   NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
        qualVecName = (char *)vecName;
        vPtr = GetVectorObject(dataPtr, qualVecName, NS_SEARCH_CURRENT);
    }
    /*
     * A vector's numeric type is immutable.  Only reject a mismatch
     * when -type was explicitly supplied; an omitted -type preserves
     * the type of an existing vector.
     */
    if (vPtr == NULL) {
        hPtr = Tcl_CreateHashEntry(&(dataPtr->vectorTable), qualVecName, &isNew);
        vPtr = Rbc_VectorNew(dataPtr);
        vPtr->type = type;
        vPtr->hashPtr = hPtr;
        vPtr->name = Tcl_GetHashKey(&(dataPtr->vectorTable), hPtr);
        Tcl_SetHashValue(hPtr, vPtr);
    } else if (vPtr->type != type) {
        /*
         * Defensive internal invariant.  The Tcl create command should
         * already have resolved the effective type.
         */
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector type mismatch", -1));
        goto error;
    }
    Tcl_Preserve(vPtr);
    held = TRUE;
    if (!isNew && Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr) != TCL_OK) {
        goto error;
    }
    /* process the command name: */
    if (cmdName != NULL) {
        Tcl_CmdInfo cmdInfo;

        if (isAutoName) {
            cmdName = qualVecName;
        } else {
            Tcl_DStringFree(&qualVecNamePtr);
            cmdName = BuildQualifiedName(interp, cmdName, &qualVecNamePtr);
        }
        nsPtr = NULL;
        vecNameTail = NULL;
        if (ParseQualifiedName(interp, cmdName, &nsPtr, &vecNameTail) != TCL_OK) {
            Tcl_AppendStringsToObj(resultPtr, "unknown namespace in \"", cmdName, "\"", NULL);
            Tcl_SetObjResult(interp, resultPtr);
            goto error;
        }
        if (Tcl_GetCommandInfo(interp, cmdName, &cmdInfo)) {
            /*
             * Vector instance commands are registered through
             * Tcl_CreateObjCommand2(), so their original callback and client
             * data are stored in objProc2 and objClientData2.
             */
            if ((cmdInfo.isNativeObjectProc != 2) || (cmdInfo.objProc2 != Rbc_VectorInstanceObjCmd) ||
                (cmdInfo.objClientData2 != vPtr)) {
                Tcl_AppendStringsToObj(resultPtr, "command \"", cmdName, "\" already exists", NULL);
                Tcl_SetObjResult(interp, resultPtr);
                goto error;
            }
        }
    }
    if (vPtr->cmdToken != 0) {
        DeleteCommand(vPtr); /* Command already exists, delete old first */
        if (VectorAlive(vPtr) != TCL_OK || Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr) != TCL_OK)
            goto error;
        if (vPtr->cmdToken != NULL ||
            (cmdName != NULL && Tcl_FindCommand(interp, cmdName, NULL, TCL_GLOBAL_ONLY) != NULL)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector command changed during callback", -1));
            goto error;
        }
    }
    if (cmdName != NULL) {
        vPtr->cmdToken = Tcl_CreateObjCommand2(interp, cmdName, Rbc_VectorInstanceObjCmd, vPtr, VectorInstDeleteProc);
    }
    /* process array variable: */
    if (varName != NULL && varName[0] != '\0') {
        if ((varName[0] == '#') && (strcmp(varName, "#auto") == 0)) {
            varName = vPtr->name;
        } else {
            Tcl_DStringFree(&qualVecNamePtr);
            varName = BuildQualifiedName(interp, varName, &qualVecNamePtr);
        }
        if (Rbc_VectorMapVariable(interp, vPtr, varName) != TCL_OK) {
            goto error;
        }
    }
    if (VectorAlive(vPtr) != TCL_OK || Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr) != TCL_OK) {
        goto error;
    }
    *newPtr = isNew;
    Tcl_DStringFree(&qualVecNamePtr);
    Tcl_DecrRefCount(resultPtr);
    if (savedVarName != NULL) {
        ckfree(savedVarName);
    }
    Tcl_Release(vPtr);
    Tcl_Release(interp);
    return vPtr;

error:
    if ((vPtr != NULL) && isNew && !vPtr->destroying) {
        Tcl_InterpState state = Tcl_SaveInterpState(interp, TCL_ERROR);
        Rbc_VectorFree(vPtr);
        Tcl_RestoreInterpState(interp, state);
    }
    Tcl_DStringFree(&qualVecNamePtr);
    Tcl_DecrRefCount(resultPtr);
    if (savedVarName != NULL) {
        ckfree(savedVarName);
    }
    if (held) {
        Tcl_Release(vPtr);
    }
    Tcl_Release(interp);
    return NULL;
}

/**
 * @brief Destroy a vector when its instance command is deleted.
 * @ingroup vector_storage_private
 *
 * Clears the command token before calling Rbc_VectorFree, avoiding deletion of
 * the same command again. Called by Tcl; no record access follows destruction.
 *
 * @param[in,out] clientData Vector record owned by the deleted instance command; cleared command token prevents
 *   recursive deletion.
 */
static void VectorInstDeleteProc(ClientData clientData) {
    VectorObject *vPtr = clientData;
    vPtr->cmdToken = 0;
    Rbc_VectorFree(vPtr);
}

/**
 * @brief Release a record after Tcl preservation references have drained.
 * @ingroup vector_storage_private
 * @param[in] data Owned vector or client record whose resources are already detached; freed here.
 *
 * Calls ckfree on the supplied allocation. Used for deferred vector and client
 * record reclamation; resources must already have been detached.
 */
static void FreeVectorRecord(DestroyData data) {
    ckfree((char *)data);
}

/**
 * @brief Destroy a vector identity, its observers and owned resources.
 * @ingroup vector_storage_private
 * @param[in] vPtr Valid non-NULL identity to destroy; may already be marked destroying during recursive cleanup.
 *
 * Ignores recursive destruction while the retained record is marked destroying.
 * Preserves record/interpreter, deletes the command and mapping, unregisters the
 * name, cancels pending idle work and delivers synchronous DESTROY with length
 * zero. A temporary name copy remains valid during observer delivery. Detaches
 * remaining clients, releases non-static storage, then defers record reclamation.
 * Callbacks can run during command deletion, unmapping, notification and custom
 * storage release. Preservation delays record reclamation, not logical destruction
 * or storage cleanup. Requires a live non-NULL record; ignores read-only state.
 */
void Rbc_VectorFree(VectorObject *vPtr) {
    Rbc_ChainLink *linkPtr;
    VectorClient *clientPtr;
    Tcl_Interp *interp = vPtr->interp;
    char *savedName = NULL;

    if (vPtr->destroying) {
        return;
    }
    vPtr->destroying = TRUE;
    /* Keep the record and interpreter alive across destruction callbacks. */
    Tcl_Preserve(vPtr);
    Tcl_Preserve(interp);
    if (vPtr->cmdToken != 0) {
        DeleteCommand(vPtr);
    }
    if (vPtr->arrayName != NULL) {
        UnmapVariable(vPtr);
    }
    /* Remove the old registration before callbacks can recreate this name. */
    if (vPtr->hashPtr != NULL) {
        savedName = RbcStrdup(vPtr->name);
        Tcl_DeleteHashEntry(vPtr->hashPtr);
        vPtr->hashPtr = NULL;
        vPtr->name = savedName;
    }
    vPtr->length = 0;
    /* Immediately notify clients that vector is going away */
    if (vPtr->notifyFlags & NOTIFY_PENDING) {
        vPtr->notifyFlags &= ~NOTIFY_PENDING;
        Tcl_CancelIdleCall(VectorNotifyClients, vPtr);
    }
    vPtr->notifyFlags |= NOTIFY_DESTROYED;
    VectorNotifyClients(vPtr);
    while ((linkPtr = Rbc_ChainFirstLink(vPtr->chainPtr)) != NULL) {
        clientPtr = Rbc_ChainGetValue(linkPtr);
        Rbc_FreeVectorId((Rbc_VectorId)clientPtr);
    }
    Rbc_ChainDestroy(vPtr->chainPtr);
    if ((vPtr->data.raw != NULL) && (vPtr->freeProc != TCL_STATIC)) {
        if (vPtr->freeProc == TCL_DYNAMIC) {
            ckfree(vPtr->data.raw);
        } else {
            (*vPtr->freeProc)(vPtr->data.raw);
        }
    }
    if (savedName != NULL) {
        ckfree(savedName);
    }
#ifdef NAMESPACE_DELETE_NOTIFY
    if (vPtr->nsPtr != NULL) {
        /* Not Implemented Yet */
        /*** Rbc_DestroyNsDeleteNotify(vPtr->interp, vPtr->nsPtr, vPtr); */
    }
#endif /* NAMESPACE_DELETE_NOTIFY */
    Tcl_EventuallyFree(vPtr, FreeVectorRecord);
    Tcl_Release(vPtr);
    Tcl_Release(interp);
}

/**
 * @brief Resize an existing destination and copy the selected source values.
 * @ingroup vector_storage_private
 * @param[in,out] destPtr Live writable same-type destination; may equal the source.
 * @param[in] srcPtr Live source with a valid inclusive first/last selection; values are copied, not adopted.
 * @return TCL_OK on successful copy and post-callback checks; TCL_ERROR otherwise.
 *
 * Requires matching numeric types; selected length is last-first+1. Checks byte
 * arithmetic and writability, copies with memmove and copies the source offset.
 * Completes copying before releasing old storage, including same-object slices.
 * Invalidates extrema but does not create a vector, flush or notify. Retains the
 * destination through custom release and rejects destruction, protection or
 * storage/selection changes before returning success. Callback effects remain.
 */
int Rbc_VectorDuplicate(VectorObject *destPtr, VectorObject *srcPtr) {
    Tcl_Size first = srcPtr->first, length = srcPtr->last - first + 1;
    Tcl_Size size = length == 0 ? 0 : DEF_ARRAY_SIZE;
    size_t bytes, capacityBytes;
    VectorData data;
    void *oldData;
    Tcl_FreeProc *oldFree;
    Tcl_Interp *interp = destPtr->interp;
    int result;

    if (destPtr->type != srcPtr->type) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't duplicate vectors of different types", -1));
        return TCL_ERROR;
    }
    if (Rbc_VectorCheckWritable(interp, (Rbc_Vector *)destPtr) != TCL_OK ||
        GetVectorByteCount(interp, srcPtr->type, length, &bytes) != TCL_OK) {
        return TCL_ERROR;
    }
    if (first < 0 || first > srcPtr->length || length > srcPtr->length - first) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid source vector selection", -1));
        return TCL_ERROR;
    }
    while (size < length) {
        if (size > TCL_SIZE_MAX / 2) { size = length; break; }
        size *= 2;
    }
    if (GetVectorByteCount(interp, srcPtr->type, size, &capacityBytes) != TCL_OK) return TCL_ERROR;
    data.raw = NULL;
    if (size > 0) {
        data = destPtr->data;
        if (size != destPtr->size) {
            data.raw = Tcl_AttemptAlloc(capacityBytes);
            if (data.raw == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate vector copy", -1));
                return TCL_ERROR;
            }
        }
        if (bytes > 0) {
            memmove(data.raw, (char *)srcPtr->data.raw + (size_t)first * VectorElementSize(srcPtr->type), bytes);
        }
    }
    /* No callbacks occur before the copy is complete, including self-slices. */
    Tcl_Preserve(interp);
    Tcl_Preserve(destPtr);
    oldData = destPtr->data.raw;
    oldFree = destPtr->freeProc;
    destPtr->offset = srcPtr->offset;
    destPtr->data = data;
    destPtr->length = length;
    destPtr->size = size;
    destPtr->first = 0;
    destPtr->last = length - 1;
    destPtr->notifyFlags |= UPDATE_RANGE;
    if (oldData != data.raw) {
        destPtr->freeProc = data.raw == NULL ? TCL_STATIC : TCL_DYNAMIC;
        ReleaseVectorStorage(interp, oldData, oldFree);
    }
    result = CheckResizedVector(destPtr, data.raw, length, size);
    Tcl_Release(destPtr);
    Tcl_Release(interp);
    return result;
}

/**
 * @brief Discard cached Tcl array entries while retaining vector data.
 *
 * Return immediately for an unmapped/destroying vector or deleted interpreter.
 * Otherwise preserve the record and interpreter, copy the mapping name/flags,
 * remove RBC's trace and unset the array. User traces can destroy or remap the
 * vector. Recreate the end entry and reinstall RBC's trace only while the saved
 * association still matches, rechecking after each callback-capable write.
 * Release the copied name and preservation references on completion. The void
 * interface does not propagate unset/write/trace status or preserve the Tcl
 * result. No vector values are changed or client notification requested here;
 * user-trace side effects are not rolled back.
 *
 * @param[in,out] vPtr Borrowed live vector record on its owning interpreter thread; this call does not transfer
 *   ownership.
 */
void Rbc_VectorFlushCache(VectorObject *vPtr) {
    Tcl_Interp *interp = vPtr->interp;
    char *arrayName;
    int varFlags;

    if (vPtr->arrayName == NULL || vPtr->destroying || Tcl_InterpDeleted(interp)) {
        return; /* Doesn't use the variable API, or is being destroyed. */
    }
    Tcl_Preserve(vPtr);
    Tcl_Preserve(interp);
    arrayName = RbcStrdup(vPtr->arrayName);
    varFlags = vPtr->varFlags;
    /* User unset/write traces may delete the vector or remap its variable. */
    Tcl_UntraceVar2(interp, arrayName, NULL, TRACE_ALL | varFlags,
                    (Tcl_VarTraceProc *)VectorVarTrace, vPtr);
    Tcl_UnsetVar2(interp, arrayName, NULL, varFlags);
    if (!Tcl_InterpDeleted(interp) && !vPtr->destroying &&
        vPtr->arrayName != NULL && strcmp(vPtr->arrayName, arrayName) == 0 && vPtr->varFlags == varFlags) {
        Tcl_SetVar2(interp, arrayName, "end", "", varFlags);
        if (!Tcl_InterpDeleted(interp) && !vPtr->destroying &&
            vPtr->arrayName != NULL && strcmp(vPtr->arrayName, arrayName) == 0 && vPtr->varFlags == varFlags) {
            Tcl_TraceVar2(interp, arrayName, NULL, TRACE_ALL | varFlags,
                          (Tcl_VarTraceProc *)VectorVarTrace, vPtr);
        }
    }
    ckfree(arrayName);
    Tcl_Release(vPtr);
    Tcl_Release(interp);
}

/**
 * @brief Replace a vector array mapping with callback-aware lifetime checks.
 *
 * Borrow the live vector, interpreter and name; NULL or empty names request
 * unmapping. Reject parentheses before changing an existing association. Copy
 * the requested name and preserve the vector/interpreter while unmapping the
 * old array, unsetting the target variable and creating its end entry. After
 * callback boundaries, reject destruction or a competing reentrant mapping.
 * On a successful initial write install RBC's read/write/unset trace. Store the
 * requested name even when that initial write fails, and return its Tcl status;
 * a failure does not restore the old mapping or undo user trace effects.
 * This changes the Tcl access surface, not vector storage or command identity.
 * Callers must not infer that preservation keeps destroyed storage valid.
 *
 * @param[in,out] interp Borrowed interpreter for lookup, evaluation and diagnostics; caller keeps it valid
 *   throughout the call.
 * @param[in,out] vPtr Borrowed live vector record on its owning interpreter thread; this call does not transfer
 *   ownership.
 * @param[in] name Borrowed array name, copied before callbacks; NULL or empty removes the current mapping.
 *   Parentheses are rejected.
 */
int Rbc_VectorMapVariable(Tcl_Interp *interp, VectorObject *vPtr, const char *name) {
    const char *result;
    char *savedName;
    int code = TCL_ERROR;
    Tcl_Namespace *varNsPtr;

    if (name != NULL && (strchr(name, '(') != NULL || strchr(name, ')') != NULL)) {
        Tcl_SetObjResult(
            interp,
            Tcl_NewStringObj(
                "mapped array name must not contain parentheses; use a separate array name or an empty name", -1));
        return TCL_ERROR;
    }
    Tcl_Preserve(vPtr);
    Tcl_Preserve(interp);
    savedName = RbcStrdup(name != NULL ? name : "");
    name = savedName;
    if (vPtr->arrayName != NULL) {
        UnmapVariable(vPtr);
        if (vPtr->destroying || Tcl_InterpDeleted(interp)) {
            goto destroyed;
        }
        if (vPtr->arrayName != NULL) {
            goto remapped;
        }
    }
    if ((name == NULL) || (name[0] == '\0')) {
        /* If the variable name is the empty string, simply return after removing any existing variable. */
        code = TCL_OK;
        goto done;
    }
    /*
     * To play it safe, delete the variable first.  This has
     * side-effect of unmapping the variable from any vector that may
     * be currently using it.
     */
    Tcl_UnsetVar2(interp, name, NULL, 0);
    if (vPtr->destroying || Tcl_InterpDeleted(interp)) {
        goto destroyed;
    }
    if (vPtr->arrayName != NULL) {
        goto remapped;
    }
    /* Set the index "end" in the array.  This will create the
     * variable immediately so that we can check its namespace
     * context.
     */
    result = Tcl_SetVar2(interp, name, "end", "", TCL_LEAVE_ERR_MSG);
    if (vPtr->destroying || Tcl_InterpDeleted(interp)) {
        goto destroyed;
    }
    if (vPtr->arrayName != NULL) {
        goto remapped;
    }
    /* Determine if the variable is global or not.  If there wasn't a
     * namespace qualifier, it still may be global.  We need to look
     * inside the Var structure to see what it's namespace field says.
     * NULL indicates that it's local.
     */
    varNsPtr = Tcl_FindNamespace(interp, name, NULL, 0);
    vPtr->varFlags = (varNsPtr != NULL) ? (TCL_NAMESPACE_ONLY | TCL_GLOBAL_ONLY) : 0;
    if (result != NULL) {
        /* Trace the array on reads, writes, and unsets */
        /*printf("trace on %s with variable %s\n",vPtr->name,name);*/
        Tcl_TraceVar2(interp, name, NULL, (TRACE_ALL | vPtr->varFlags), (Tcl_VarTraceProc *)VectorVarTrace, vPtr);
    }
    vPtr->arrayName = RbcStrdup(name);
    code = (result == NULL) ? TCL_ERROR : TCL_OK;
    goto done;

remapped:
    Tcl_SetObjResult(interp, Tcl_NewStringObj("vector mapping changed during variable trace", -1));
    goto done;
destroyed:
    Tcl_SetObjResult(interp, Tcl_NewStringObj("vector was destroyed during variable mapping", -1));
done:
    ckfree(savedName);
    Tcl_Release(vPtr);
    Tcl_Release(interp);
    return code;
}

/**
 * @anchor rbc_vector_reset_storage_contract
 * @brief Validate and replace the complete storage ownership contract.
 * @ingroup vector_storage_private
 * @param[in,out] vPtr Live writable destination; its current numeric type selects the element size.
 * @param[in] valueArr Buffer to copy, borrow or adopt; NULL selects empty storage.
 * @param[in] length Nonnegative used count, at most size.
 * @param[in] size Nonnegative capacity in values; zero selects empty storage.
 * @param[in] freeProc TCL_STATIC borrows, TCL_DYNAMIC adopts, TCL_VOLATILE copies, or a custom release callback.
 *   Ownership is accepted only on success.
 * @return TCL_OK once the storage contract is accepted, even if later callbacks destroy the vector; TCL_ERROR
 *   before acceptance.
 *
 * Checks writability, nonnegative length/capacity, length<=capacity and byte
 * counts. NULL data or zero capacity installs empty static storage. TCL_VOLATILE
 * copies used values into capacity-sized owned storage, even for the same input
 * address. Otherwise adopts/borrows the supplied pointer under freeProc. Releases
 * the old owned allocation only when its address differs, after installing the
 * completed metadata. Retains the record and interpreter through release and
 * publication, skipping publication after destruction. Returns TCL_OK once the
 * buffer ownership contract is accepted, even if callbacks destroy the vector;
 * callers must not free an adopted buffer again. Precommit errors leave ownership
 * with the caller. Does not reset the shared first/last selection.
 */
static int ResetVectorStorage(VectorObject *vPtr, void *valueArr, Tcl_Size length, Tcl_Size size,
                              Tcl_FreeProc *freeProc) {
    void *newArr;
    size_t sizeBytes;
    size_t lengthBytes;

    if (Rbc_VectorCheckWritable(vPtr->interp, (Rbc_Vector *)vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (length < 0) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector length cannot be negative", -1));
        return TCL_ERROR;
    }
    if (size < 0) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector array size cannot be negative", -1));
        return TCL_ERROR;
    }
    if (length > size) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector length exceeds array size", -1));
        return TCL_ERROR;
    }
    if ((GetVectorByteCount(vPtr->interp, vPtr->type, size, &sizeBytes) != TCL_OK) ||
        (GetVectorByteCount(vPtr->interp, vPtr->type, length, &lengthBytes) != TCL_OK)) {
        return TCL_ERROR;
    }
    /*
     * A NULL array or zero capacity represents an empty vector.
     */
    if ((valueArr == NULL) || (size == 0)) {
        valueArr = NULL;
        length = 0;
        size = 0;
        freeProc = TCL_STATIC;
    } else if (freeProc == TCL_VOLATILE) {
        /*
         * Volatile storage must be copied even when valueArr happens
         * to be the same address as the vector's current array.
         */
        newArr = ckalloc(sizeBytes);
        if (lengthBytes > 0) {
            memcpy(newArr, valueArr, lengthBytes);
        }
        valueArr = newArr;
        freeProc = TCL_DYNAMIC;
    }
    {
        void *oldData = vPtr->data.raw;
        Tcl_FreeProc *oldFree = vPtr->freeProc;
        Tcl_Interp *interp = vPtr->interp;
        Tcl_Preserve(interp);
        Tcl_Preserve(vPtr);
        vPtr->freeProc = freeProc;
        vPtr->data.raw = valueArr;
        vPtr->size = size;
        vPtr->length = length;
        vPtr->notifyFlags |= UPDATE_RANGE;
        if (oldData != valueArr) {
            ReleaseVectorStorage(interp, oldData, oldFree);
        }
        PublishVectorChange(vPtr, TRUE, 0, 0);
        Tcl_Release(vPtr);
        Tcl_Release(interp);
    }
    /* Ownership has transferred at commit, even if a later callback destroys
     * the vector. Returning an error here would invite callers to free it twice. */
    return TCL_OK;
}

/**
 * @brief Apply a real storage reset after checking the vector type.
 * @ingroup vector_storage_private
 * @param[in,out] vPtr Live real destination; complex type is rejected.
 * @param[in] valueArr Real buffer under the freeProc ownership policy, or NULL.
 * @param[in] length Nonnegative used count, at most size.
 * @param[in] size Nonnegative capacity in doubles, not bytes.
 * @param[in] freeProc Storage policy passed to the shared reset implementation.
 * @return Tcl status with the shared reset ownership-acceptance semantics.
 *
 * Rejects a complex vector with a Tcl error; otherwise delegates to
 * ResetVectorStorage, including its ownership and callback restrictions.
 * Does not convert types or make a separate numeric result.
 */
int Rbc_VectorReset(VectorObject *vPtr, double *valueArr, Tcl_Size length, Tcl_Size size, Tcl_FreeProc *freeProc) {
    if (vPtr->type != RBC_VECTOR_REAL) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("Rbc_ResetVector cannot reset a complex vector", -1));
        return TCL_ERROR;
    }
    return ResetVectorStorage(vPtr, valueArr, length, size, freeProc);
}

/**
 * @ingroup vector_storage_private
 * @brief Deliver a snapshot of vector observers with stable update metadata.
 * @param[in] clientData Valid VectorObject for this delivery, including destruction delivery; retained internally,
 *   but not owned by the callback.
 *
 * Preserves vector, interpreter and snapshotted client IDs, not chain links. Clears pending update
 * state before invoking callbacks. New clients wait for a later notification; detached or reassigned
 * IDs are skipped and destruction stops remaining update callbacks. Temporarily installs the event
 * interval on each client and restores its outer interval after nested delivery. Releases every
 * retained ID even when delivery stops early.
 */
static void VectorNotifyClients(ClientData clientData) {
    VectorObject *vPtr = clientData;
    Tcl_Interp *interp = vPtr->interp;
    Rbc_ChainLink *linkPtr;
    VectorClient **clients;
    Tcl_Size count = 0, i;
    Rbc_VectorNotify notify;
    int updateAll, updateRangeValid;
    Tcl_Size updateFirst, updateLast;

    Tcl_Preserve(vPtr);
    Tcl_Preserve(interp);
    notify = (vPtr->notifyFlags & NOTIFY_DESTROYED) ? RBC_VECTOR_NOTIFY_DESTROY : RBC_VECTOR_NOTIFY_UPDATE;
    if (vPtr->destroying && notify != RBC_VECTOR_NOTIFY_DESTROY) {
        goto done;
    }
    updateAll = (notify == RBC_VECTOR_NOTIFY_DESTROY) || vPtr->updateAll;
    updateRangeValid = !updateAll && vPtr->updateRangeValid;
    updateFirst = vPtr->updateFirst;
    updateLast = vPtr->updateLast;
    vPtr->updateAll = FALSE;
    vPtr->updateRangeValid = FALSE;
    vPtr->updateFirst = vPtr->updateLast = 0;
    vPtr->notifyFlags &= ~(NOTIFY_UPDATED | NOTIFY_DESTROYED | NOTIFY_PENDING);

    /* Snapshot and preserve IDs, not chain links. Callbacks may unlink any
     * client, add new clients, or destroy the complete vector. New clients
     * join the next notification; detached clients are skipped. */
    for (linkPtr = Rbc_ChainFirstLink(vPtr->chainPtr); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        count++;
    }
    clients = (VectorClient **)Tcl_Alloc(count * sizeof(VectorClient *));
    i = 0;
    for (linkPtr = Rbc_ChainFirstLink(vPtr->chainPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        clients[i] = Rbc_ChainGetValue(linkPtr);
        Tcl_Preserve(clients[i]);
        i++;
    }
    for (i = 0; i < count; i++) {
        VectorClient *clientPtr = clients[i];
        if (vPtr->destroying && notify == RBC_VECTOR_NOTIFY_UPDATE) {
            break;
        }
        if (clientPtr->magic == VECTOR_MAGIC && clientPtr->serverPtr == vPtr && clientPtr->proc != NULL) {
            int savedAll = clientPtr->updateAll;
            int savedValid = clientPtr->updateRangeValid;
            Tcl_Size savedFirst = clientPtr->updateFirst, savedLast = clientPtr->updateLast;
            clientPtr->updateAll = updateAll;
            clientPtr->updateRangeValid = updateRangeValid;
            clientPtr->updateFirst = updateFirst;
            clientPtr->updateLast = updateLast;
            (*clientPtr->proc)(interp, clientPtr->clientData, notify);
            /* Nested immediate notifications must not overwrite the outer
             * callback's interval. The preserved ID can already be detached. */
            clientPtr->updateAll = savedAll;
            clientPtr->updateRangeValid = savedValid;
            clientPtr->updateFirst = savedFirst;
            clientPtr->updateLast = savedLast;
        }
    }
    for (i = 0; i < count; i++) {
        Tcl_Release(clients[i]);
    }
    Tcl_Free((char *)clients);
done:
    Tcl_Release(vPtr);
    Tcl_Release(interp);
}

int Rbc_VectorNotifyPending(Rbc_VectorId clientId) {
    VectorClient *clientPtr = (VectorClient *)clientId;
    if ((clientPtr == NULL) || (clientPtr->magic != VECTOR_MAGIC) || (clientPtr->serverPtr == NULL)) {
        return 0;
    }
    return (clientPtr->serverPtr->notifyFlags & NOTIFY_PENDING);
}

/**
 * @brief Resize typed storage without publishing a change to clients.
 * @ingroup vector_storage_private
 * @param[in,out] vPtr Live writable destination; its selection and extrema metadata are updated.
 * @param[in] length Nonnegative requested used count in values, subject to allocation limits.
 * @return TCL_OK on success; TCL_ERROR for protection, size or post-callback failures.
 *
 * Checks writability and a nonnegative length. Chooses a capacity starting at
 * DEF_ARRAY_SIZE and doubling to fit, with overflow protection. Reuses an equal
 * capacity allocation; otherwise copies the retained prefix into new storage.
 * Zeros added values and installs length/capacity, selection 0..length-1 and
 * invalid extrema before releasing old owned storage. Zero length releases storage.
 * Does not flush or notify. Retains the record and interpreter across custom
 * release; returns TCL_ERROR if it destroys, protects or changes the installed
 * storage/selection. Such errors do not roll back the commit or callback effects.
 */
int Rbc_VectorChangeLength(VectorObject *vPtr, Tcl_Size length) {
    Tcl_Size newSize;
    Tcl_Size used;
    VectorData newData;
    Tcl_FreeProc *freeProc;

    if (Rbc_VectorCheckWritable(vPtr->interp, (Rbc_Vector *)vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (length < 0) {
        Tcl_SetObjResult(vPtr->interp, Tcl_ObjPrintf("bad vector size \"%" TCL_SIZE_MODIFIER "d\"", length));
        return TCL_ERROR;
    }
    newData.raw = NULL;
    newSize = 0;
    freeProc = TCL_STATIC;
    if (length > 0) {
        size_t byteCount;
        size_t usedByteCount;

        newSize = DEF_ARRAY_SIZE;
        while (newSize < length) {
            if (newSize > (TCL_SIZE_MAX / 2)) {
                newSize = length;
                break;
            }
            newSize *= 2;
        }
        if (GetVectorByteCount(vPtr->interp, vPtr->type, newSize, &byteCount) != TCL_OK) {
            return TCL_ERROR;
        }
        used = vPtr->length;
        if (used > length) {
            used = length;
        }
        freeProc = vPtr->freeProc;
        if (newSize == vPtr->size) {
            newData.raw = vPtr->data.raw;
        } else {
            newData.raw = Tcl_AttemptAlloc(byteCount);
            if (newData.raw == NULL) {
                Tcl_SetObjResult(vPtr->interp,
                                 Tcl_ObjPrintf("can't allocate %" TCL_SIZE_MODIFIER "d elements for vector \"%s\"",
                                               newSize, vPtr->name));
                return TCL_ERROR;
            }
            if (used > 0) {
                if (GetVectorByteCount(vPtr->interp, vPtr->type, used, &usedByteCount) != TCL_OK) {
                    ckfree(newData.raw);
                    return TCL_ERROR;
                }
                memcpy(newData.raw, vPtr->data.raw, usedByteCount);
            }
            freeProc = TCL_DYNAMIC;
        }
        if (length > used) {
            ZeroVectorElements(vPtr->type, newData, used, length);
        }
    }
    {
        void *oldData = vPtr->data.raw;
        Tcl_FreeProc *oldFree = vPtr->freeProc;
        Tcl_Interp *interp = vPtr->interp;
        int result;
        Tcl_Preserve(interp);
        Tcl_Preserve(vPtr);
        vPtr->data = newData;
        vPtr->size = newSize;
        vPtr->length = length;
        vPtr->first = 0;
        vPtr->last = length - 1;
        vPtr->freeProc = freeProc;
        vPtr->notifyFlags |= UPDATE_RANGE;
        if (oldData != newData.raw) {
            ReleaseVectorStorage(interp, oldData, oldFree);
        }
        result = CheckResizedVector(vPtr, newData.raw, length, newSize);
        Tcl_Release(vPtr);
        Tcl_Release(interp);
        return result;
    }
}

/**
 * @ingroup vector_storage_private
 * @brief Resolve a complete vector reference including an optional selection.
 * @param[in] dataPtr Live owning registry; its interpreter receives lookup diagnostics.
 * @param[in] vecName NUL-terminated complete reference, including any parenthesized selection.
 * @param[out] vPtrPtr Required borrowed-vector output, written only on success.
 * @return TCL_OK on complete consumption, otherwise TCL_ERROR.
 * @see Rbc_VectorParseElement
 *
 * Delegates to Rbc_VectorParseElement with current/global namespace search and rejects trailing
 * unconsumed characters. Returns TCL_OK and writes a borrowed vector pointer only on success,
 * otherwise TCL_ERROR with an interpreter diagnostic. Parsing may update the selection and evaluate
 * index expressions; this is not a literal-name lookup or a lifetime-retaining API. Callers needing to
 * survive subsequent callbacks must retain and revalidate the object separately.
 */
int Rbc_VectorLookupName(VectorInterpData *dataPtr, const char *vecName, VectorObject **vPtrPtr) {
    VectorObject *vPtr;
    const char *endPtr;

    vPtr = Rbc_VectorParseElement(dataPtr->interp, dataPtr, vecName, &endPtr, NS_SEARCH_BOTH);
    if (vPtr == NULL) {
        return TCL_ERROR;
    }
    if (*endPtr != '\0') {
        Tcl_SetObjResult(dataPtr->interp, Tcl_NewStringObj("extra characters after vector name", -1));
        return TCL_ERROR;
    }
    *vPtrPtr = vPtr;
    return TCL_OK;
}

/**
 * @brief Recompute whole-vector finite extrema and their first indices.
 * @param[in,out] vPtr Live vector whose whole-storage extrema cache and invalidation flag are updated.
 *
 * Scans real storage in ascending index order, ignores NaNs and infinities, and keeps the earliest
 * index on equal extrema. Empty or entirely nonfinite real vectors, and all complex vectors, receive
 * NaN bounds and indices -1. Clears UPDATE_RANGE after installing the cache. Requires live valid
 * storage; does not change values, selection, dirty count or notifications.
 */
void Rbc_VectorUpdateRange(VectorObject *vPtr) {
    double min;
    double max;
    Tcl_Size minIndex;
    Tcl_Size maxIndex;
    Tcl_Size i;

    if (vPtr->type != RBC_VECTOR_REAL) {
        vPtr->min = vPtr->max = rbcNaN;
        vPtr->minIndex = vPtr->maxIndex = -1;
        vPtr->notifyFlags &= ~UPDATE_RANGE;
        return;
    }
    min = max = rbcNaN;
    minIndex = maxIndex = -1;
    /*
     * Find the first finite value.
     */
    for (i = 0; i < vPtr->length; i++) {
        double value;

        value = vPtr->data.real[i];
        if (FINITE(value)) {
            min = max = value;
            minIndex = maxIndex = i;
            i++;
            break;
        }
    }
    for (; i < vPtr->length; i++) {
        double value;

        value = vPtr->data.real[i];
        if (!FINITE(value)) {
            continue;
        }
        if (value < min) {
            min = value;
            minIndex = i;
        }
        if (value > max) {
            max = value;
            maxIndex = i;
        }
    }
    vPtr->min = min;
    vPtr->max = max;
    vPtr->minIndex = minIndex;
    vPtr->maxIndex = maxIndex;
    vPtr->notifyFlags &= ~UPDATE_RANGE;
}

/**
 * @brief Translate an external index with optional Tcl expression evaluation.
 * Requires a live vector, NUL-terminated string and output index. Clears an optional special-procedure
 * output initially. Empty input fails; end-prefixed forms use Tcl_GetIntForIndex and always check
 * bounds; ++end returns length. Registered special names are recognized only with a procedure output
 * and return SPECIAL_INDEX without calling the procedure. Other input is parsed as Tcl_Size or
 * evaluated in the owning interpreter, then subtracts the current external offset with overflow
 * checks. INDEX_CHECK enforces the upper bound for ordinary numeric indices. Retains
 * vector/interpreter during evaluation and rejects deletion; a NULL diagnostic interpreter does not
 * disable evaluation. Writes indexPtr only on success. Callers must distinguish append/special indices
 * from dereferenceable storage indices.
 */
int Rbc_VectorGetIndex(Tcl_Interp *interp, VectorObject *vPtr, const char *string, Tcl_Size *indexPtr, int flags,
                       Rbc_VectorIndexProc **procPtrPtr) {
    Tcl_Obj *indexObjPtr;
    Tcl_Size value;
    int result;

    assert(vPtr != NULL);
    assert(string != NULL);
    assert(indexPtr != NULL);
    if (procPtrPtr != NULL) {
        *procPtrPtr = NULL;
    }
    if (string[0] == '\0') {
        /*
         * Do not pass the empty string to the expression parser, where
         * it could otherwise be interpreted as zero.
         */
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can not use the empty string as index", -1));
        }
        return TCL_ERROR;
    }
    if (string[0] == 'e') {
        /*
         * Parse "end" and index expressions based on "end", such as
         * "end-1".
         */
        if (vPtr->length < 1) {
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad index \"%s\": vector is empty", string));
            }
            return TCL_ERROR;
        }
        indexObjPtr = Tcl_NewStringObj(string, -1);
        Tcl_IncrRefCount(indexObjPtr);
        result = Tcl_GetIntForIndex(interp, indexObjPtr, vPtr->length - 1, &value);
        Tcl_DecrRefCount(indexObjPtr);
        if (result != TCL_OK) {
            return TCL_ERROR;
        }
        if ((value < 0) || (value >= vPtr->length)) {
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("index \"%s\" is out of range", string));
            }
            return TCL_ERROR;
        }
        *indexPtr = value;
        return TCL_OK;
    }
    if ((string[0] == '+') && (strcmp(string, "++end") == 0)) {
        /*
         * The special append index denotes the position immediately
         * after the final vector value.
         */
        *indexPtr = vPtr->length;
        return TCL_OK;
    }
    if (procPtrPtr != NULL) {
        Tcl_HashEntry *hPtr;

        hPtr = Tcl_FindHashEntry(&vPtr->dataPtr->indexProcTable, string);
        if (hPtr != NULL) {
            *indexPtr = SPECIAL_INDEX;
            *procPtrPtr = (Rbc_VectorIndexProc *)Tcl_GetHashValue(hPtr);
            return TCL_OK;
        }
    }
    indexObjPtr = Tcl_NewStringObj(string, -1);
    Tcl_IncrRefCount(indexObjPtr);
    /*
     * First try a plain Tcl-sized integer without modifying either
     * interpreter result.
     */
    result = Tcl_GetSizeIntFromObj(NULL, indexObjPtr, &value);
    if (result != TCL_OK) {
        Tcl_Obj *valueObjPtr;

        valueObjPtr = NULL;
        /*
         * Preserve the historical support for index expressions. Use the
         * vector's interpreter because the public interp argument may be
         * NULL.
         */
        Tcl_Interp *owner = vPtr->interp;
        int deleted;

        Tcl_Preserve(owner);
        Tcl_Preserve(vPtr);
        result = Tcl_ExprObj(owner, indexObjPtr, &valueObjPtr);
        if (result == TCL_OK) {
            result = Tcl_GetSizeIntFromObj(owner, valueObjPtr, &value);
            Tcl_DecrRefCount(valueObjPtr);
        }
        deleted = Tcl_InterpDeleted(owner) || vPtr->destroying;
        if (deleted && interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector was destroyed during index evaluation", -1));
            Tcl_SetErrorCode(interp, "RBC", "VECTOR", "DESTROYED", NULL);
        }
        Tcl_Release(vPtr);
        Tcl_Release(owner);
        if (deleted) {
            Tcl_DecrRefCount(indexObjPtr);
            return TCL_ERROR;
        }
        if (result != TCL_OK) {
            Tcl_ResetResult(vPtr->interp);
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad index \"%s\"", string));
            }
            Tcl_DecrRefCount(indexObjPtr);
            return TCL_ERROR;
        }
    }
    Tcl_DecrRefCount(indexObjPtr);
    /*
     * Convert the externally visible index to the vector's zero-based
     * storage index.
     *
     * Check before subtraction to avoid signed Tcl_Size overflow. If the
     * external value is smaller than the offset, the resulting internal
     * index would be negative. A negative offset can instead cause an
     * upward overflow.
     */
    if ((value < vPtr->offset) || ((vPtr->offset < 0) && (value > (TCL_SIZE_MAX + vPtr->offset)))) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("index \"%s\" is out of range", string));
        }
        return TCL_ERROR;
    }
    value -= vPtr->offset;
    if ((flags & INDEX_CHECK) && (value >= vPtr->length)) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("index \"%s\" is out of range", string));
        }
        return TCL_ERROR;
    }
    *indexPtr = value;
    return TCL_OK;
}

/**
 * @ingroup vector_storage_private
 * @brief Parse an index from a bounded string span.
 * @param[in] interp Optional diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Live vector supplying index context; evaluation can run Tcl callbacks.
 * @param[in] start First byte of the readable index span.
 * @param[in] end One-past-last byte in the same buffer, at or after start.
 * @param[out] indexPtr Required output, written only after successful parsing.
 * @param[in] flags Index flags forwarded to Rbc_VectorGetIndex.
 * @return TCL_OK or TCL_ERROR from index parsing; registered special indices are not enabled.
 * @see Rbc_VectorGetIndex
 *
 * Requires start and end in one readable buffer with end not before start. Copies the half-open span
 * into a temporary retained Tcl object, delegates index and flag semantics to Rbc_VectorGetIndex, then
 * releases the object. Does not require a NUL byte at end.
 */
static int GetVectorIndexFromSpan(Tcl_Interp *interp, VectorObject *vPtr, const char *start, const char *end,
                                  Tcl_Size *indexPtr, int flags) {
    Tcl_Obj *objPtr;
    int result;

    assert(start != NULL);
    assert(end >= start);
    objPtr = Tcl_NewStringObj(start, (Tcl_Size)(end - start));
    Tcl_IncrRefCount(objPtr);
    result = Rbc_VectorGetIndex(interp, vPtr, Tcl_GetString(objPtr), indexPtr, flags, NULL);
    Tcl_DecrRefCount(objPtr);
    return result;
}

/**
 * @brief Parse and commit an inclusive shared selection.
 * With INDEX_COLON uses the first colon as the separator; omitted endpoints mean storage zero and the
 * current final index, with zero used for an empty upper endpoint. Otherwise parses one index and sets
 * both endpoints. Range endpoints do not accept registered special procedures. Does not modify the
 * input buffer. Parses endpoints into locals, validates ordering and, for a nonempty checked vector,
 * current bounds before committing first/last. Expression callbacks may themselves change selection,
 * values or length and are not rolled back. Returns a Tcl status. This helper does not turn empty,
 * append or special selections into valid storage intervals; callers validate before accessing data.
 */
int Rbc_VectorGetIndexRange(Tcl_Interp *interp, VectorObject *vPtr, const char *string, int flags,
                            Rbc_VectorIndexProc **procPtrPtr) {
    const char *colon;
    Tcl_Size first;
    Tcl_Size last;
    Tcl_Size index;

    assert(vPtr != NULL);
    assert(string != NULL);
    if (procPtrPtr != NULL) {
        *procPtrPtr = NULL;
    }
    colon = NULL;
    if (flags & INDEX_COLON) {
        colon = strchr(string, ':');
    }
    if (colon == NULL) {
        if (Rbc_VectorGetIndex(interp, vPtr, string, &index, flags, procPtrPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->first = index;
        vPtr->last = index;
        return TCL_OK;
    }
    /*
     * Parse the first part without writing a temporary NUL into string.
     */
    if (colon == string) {
        first = 0;
    } else {
        if (GetVectorIndexFromSpan(interp, vPtr, string, colon, &first, flags) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /*
     * Parse the second part. It is already NUL-terminated.
     */
    if (colon[1] == '\0') {
        last = (vPtr->length > 0) ? vPtr->length - 1 : 0;
    } else {
        if (Rbc_VectorGetIndex(interp, vPtr, colon + 1, &last, flags, NULL) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (first > last) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad range \"%s\" (first > last)", string));
        }
        return TCL_ERROR;
    }
    if ((flags & INDEX_CHECK) && vPtr->length > 0 && (first >= vPtr->length || last >= vPtr->length)) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vector range changed during index evaluation", -1));
        }
        return TCL_ERROR;
    }
    /*
     * Commit only after the complete range has been validated.
     */
    vPtr->first = first;
    vPtr->last = last;
    return TCL_OK;
}

/**
 * @brief Resolve the longest registered name and optional balanced selection.
 * Searches balanced name boundaries using the requested namespace flags, so exact literal
 * parenthesized names take precedence over range suffixes. Defaults to the whole-vector selection,
 * then parses any following parenthesized range from a retained copy while preserving the vector
 * record. Restores the previous selection on range failure if the vector survives. With endPtr returns
 * a borrowed pointer into the original string for expression parsing; without it rejects trailing text
 * and restores selection. Returns a borrowed vector or NULL. Does not modify the input, create a slice
 * object or retain the result for later callbacks.
 */
VectorObject *Rbc_VectorParseElement(Tcl_Interp *interp, VectorInterpData *dataPtr, const char *start,
                                     const char **endPtr, int flags) {
    const char *p;
    VectorObject *vPtr;
    Tcl_Obj *nameObjPtr;
    Tcl_Size oldFirst;
    Tcl_Size oldLast;

    assert(dataPtr != NULL);
    assert(start != NULL);
    /* Find the longest existing name ending at a balanced boundary.
     * The next parenthesized group, if any, is an index/range. This gives
     * exact names precedence over ranges, including literal numeric names.
     * Stop at operators: range expressions are parsed separately below. */
    const char *nameEnd = NULL;
    Tcl_Size nameDepth = 0;
    vPtr = NULL;
    p = start;
    while (*p != '\0') {
        if (*p == '(') {
            nameDepth++;
        } else if (*p == ')' && nameDepth > 0) {
            nameDepth--;
        } else if (!VECTOR_CHAR(*p)) {
            break;
        }
        p++;
        if (nameDepth == 0 && (!VECTOR_CHAR(*p) || *p == '\0')) {
            VectorObject *candidate;
            nameObjPtr = Tcl_NewStringObj(start, (Tcl_Size)(p - start));
            Tcl_IncrRefCount(nameObjPtr);
            candidate = GetVectorObject(dataPtr, Tcl_GetString(nameObjPtr), flags);
            Tcl_DecrRefCount(nameObjPtr);
            if (candidate != NULL) {
                vPtr = candidate;
                nameEnd = p;
            }
        }
    }
    if (nameEnd != NULL) {
        p = nameEnd;
    } else {
        p = start;
        while (VECTOR_CHAR(*p)) {
            p++;
        }
    }
    nameObjPtr = Tcl_NewStringObj(start, (Tcl_Size)(p - start));
    Tcl_IncrRefCount(nameObjPtr);
    if (vPtr == NULL) {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't find vector \"%s\"", Tcl_GetString(nameObjPtr)));
        }
        Tcl_DecrRefCount(nameObjPtr);
        return NULL;
    }
    Tcl_DecrRefCount(nameObjPtr);
    oldFirst = vPtr->first;
    oldLast = vPtr->last;
    /*
     * With no explicit range, select the complete vector.
     */
    vPtr->first = 0;
    vPtr->last = vPtr->length - 1;
    if (*p == '(') {
        const char *rangeStart;
        Tcl_Size depth;
        Tcl_Obj *rangeObjPtr;
        int result;
        rangeStart = p + 1;
        p = rangeStart;
        depth = 1;
        /*
         * Find the matching closing parenthesis. Nested parentheses are
         * allowed in index expressions.
         */
        while (*p != '\0') {
            if (*p == '(') {
                depth++;
            } else if (*p == ')') {
                depth--;
                if (depth == 0) {
                    break;
                }
            }
            p++;
        }
        if (depth != 0) {
            if (interp != NULL) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("unbalanced parentheses \"%s\"", rangeStart));
            }
            vPtr->first = oldFirst;
            vPtr->last = oldLast;
            return NULL;
        }
        rangeObjPtr = Tcl_NewStringObj(rangeStart, (Tcl_Size)(p - rangeStart));
        Tcl_IncrRefCount(rangeObjPtr);
        Tcl_Preserve(vPtr);
        result = Rbc_VectorGetIndexRange(interp, vPtr, Tcl_GetString(rangeObjPtr), INDEX_COLON | INDEX_CHECK, NULL);
        Tcl_DecrRefCount(rangeObjPtr);
        if (result != TCL_OK) {
            if (!vPtr->destroying) {
                vPtr->first = oldFirst;
                vPtr->last = oldLast;
            }
            Tcl_Release(vPtr);
            return NULL;
        }
        Tcl_Release(vPtr);
        /*
         * Advance beyond the closing parenthesis.
         */
        p++;
    }
    /* Standalone references must consume the complete argument. Expression
     * callers receive endPtr and parse the remaining operators themselves. */
    if (endPtr == NULL && *p != '\0') {
        if (interp != NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("extra characters after vector name", -1));
        }
        vPtr->first = oldFirst;
        vPtr->last = oldLast;
        return NULL;
    }
    if (endPtr != NULL) {
        *endPtr = p;
    }
    return vPtr;
}

/**
 * @ingroup vector_storage_private
 * @brief Coalesce changed intervals and apply the notification policy.
 * @param[in,out] vPtr Valid vector record; logically destroyed records are ignored.
 * @param[in] all Nonzero supersedes ranged metadata with a full update.
 * @param[in] first Validated inclusive first changed storage index; ignored when all is nonzero.
 * @param[in] last Validated inclusive last changed storage index; ignored when all is nonzero.
 *
 * Destroyed vectors are ignored. Increments dirty even when notifications are disabled. A full update
 * supersedes interval metadata; ranged updates merge inclusive endpoints. Immediate mode can invoke
 * callbacks before return; idle mode queues at most one pending callback. Does not itself invalidate
 * or recompute extrema.
 */
static void ScheduleVectorUpdate(VectorObject *vPtr, int all, Tcl_Size first, Tcl_Size last) {
    if (vPtr->destroying) {
        return;
    }
    vPtr->dirty++;
    if (vPtr->notifyFlags & NOTIFY_NEVER) {
        return;
    }
    if (all) {
        vPtr->updateAll = TRUE;
        vPtr->updateRangeValid = FALSE;
    } else if (!vPtr->updateAll) {
        if (!vPtr->updateRangeValid) {
            vPtr->updateFirst = first;
            vPtr->updateLast = last;
            vPtr->updateRangeValid = TRUE;
        } else {
            if (first < vPtr->updateFirst) {
                vPtr->updateFirst = first;
            }
            if (last > vPtr->updateLast) {
                vPtr->updateLast = last;
            }
        }
    }
    vPtr->notifyFlags |= NOTIFY_UPDATED;
    if (vPtr->notifyFlags & NOTIFY_ALWAYS) {
        VectorNotifyClients(vPtr);
        return;
    }
    if (!(vPtr->notifyFlags & NOTIFY_PENDING)) {
        vPtr->notifyFlags |= NOTIFY_PENDING;
        Tcl_DoWhenIdle(VectorNotifyClients, vPtr);
    }
}

/**
 * @ingroup vector_storage_private
 * @brief Mark cached extrema as requiring recomputation.
 * @param[in,out] vPtr Valid record whose extrema cache is invalidated; storage is not examined.
 *
 * Stores NaN bounds and negative indices, then sets UPDATE_RANGE. Does not inspect numeric data,
 * increment dirty or schedule observers; callers separately publish the change.
 */
static void InvalidateVectorRange(VectorObject *vPtr) {
    vPtr->min = vPtr->max = rbcNaN;
    vPtr->minIndex = vPtr->maxIndex = -1;
    vPtr->notifyFlags |= UPDATE_RANGE;
}

/**
 * @brief Invalidate global extrema and schedule a full update.
 * Requires a live vector record. Invalidates cached bounds and their indices before delegating to the
 * notification scheduler. Does not alter storage, check writability or flush mapped arrays. Immediate
 * notification may execute callbacks and destroy the vector before return; callers needing further
 * access must preserve and revalidate it.
 */
void Rbc_VectorUpdateClients(VectorObject *vPtr) {
    InvalidateVectorRange(vPtr);
    ScheduleVectorUpdate(vPtr, TRUE, 0, 0);
}

/**
 * @ingroup vector_storage_private
 * @brief Maintain exact real-vector extrema after a bounded modification.
 * @param[in,out] vPtr Valid real vector with completed in-place writes and existing cache metadata.
 * @param[in] first First changed storage index, inclusive.
 * @param[in] last Last changed storage index, inclusive; invalid intervals cannot update the cache incrementally.
 * @return Nonzero if the extrema cache was updated; zero requests full recomputation later.
 *
 * Requires a real vector whose caller has already changed only the stated inclusive interval. Returns
 * FALSE without committing when the cache is invalid, bounds are invalid, an old extremum was touched
 * or cache state is inconsistent. Otherwise scans only changed finite values, retaining the earliest
 * index on equal extrema, and commits the exact cache. TRUE also covers a vector with no finite
 * values.
 */
static int UpdateVectorRangeForChangedRange(VectorObject *vPtr, Tcl_Size first, Tcl_Size last) {
    double min;
    double max;
    Tcl_Size minIndex;
    Tcl_Size maxIndex;
    Tcl_Size i;
    int haveFiniteRange;

    assert(vPtr->type == RBC_VECTOR_REAL);
    if (vPtr->notifyFlags & UPDATE_RANGE) {
        return FALSE;
    }
    if ((first < 0) || (last < first) || (last >= vPtr->length)) {
        return FALSE;
    }
    /*
     * If either previously cached extremum was overwritten, its
     * replacement cannot be determined from the changed range alone.
     */
    if (((vPtr->minIndex >= first) && (vPtr->minIndex <= last)) ||
        ((vPtr->maxIndex >= first) && (vPtr->maxIndex <= last))) {
        return FALSE;
    }
    min = vPtr->min;
    max = vPtr->max;
    minIndex = vPtr->minIndex;
    maxIndex = vPtr->maxIndex;
    haveFiniteRange = FINITE(min) && FINITE(max) && (minIndex >= 0) && (maxIndex >= 0);
    /*
     * NaN/NaN with -1/-1 is the valid cached representation of a
     * vector containing no finite values.
     */
    if (!haveFiniteRange) {
        if (FINITE(min) || FINITE(max) || (minIndex >= 0) || (maxIndex >= 0)) {
            return FALSE;
        }
    }
    for (i = first; i <= last; i++) {
        double value;

        value = vPtr->data.real[i];
        if (!FINITE(value)) {
            continue;
        }
        if (!haveFiniteRange) {
            min = max = value;
            minIndex = maxIndex = i;
            haveFiniteRange = TRUE;
            continue;
        }
        if (value < min) {
            min = value;
            minIndex = i;
        } else if ((value == min) && (i < minIndex)) {
            minIndex = i;
        }
        if (value > max) {
            max = value;
            maxIndex = i;
        } else if ((value == max) && (i < maxIndex)) {
            maxIndex = i;
        }
    }
    vPtr->min = min;
    vPtr->max = max;
    vPtr->minIndex = minIndex;
    vPtr->maxIndex = maxIndex;
    return TRUE;
}

/**
 * @brief Publish an inclusive changed interval with incremental extrema maintenance.
 * Requires a live vector whose caller has already changed only the stated storage interval. Invalid or
 * reversed bounds delegate to a full update. Valid real intervals update cached extrema when untouched
 * old extrema suffice, otherwise invalidate the cache for a later full scan. Schedules a ranged
 * notification for valid intervals, including complex vectors. Does not enforce writability, change
 * storage or flush arrays. Immediate callbacks may destroy the vector; no vector access follows
 * scheduling.
 */
void Rbc_VectorUpdateClientsRange(VectorObject *vPtr, Tcl_Size first, Tcl_Size last) {
    if ((first < 0) || (last < first) || (last >= vPtr->length)) {
        Rbc_VectorUpdateClients(vPtr);
        return;
    }
    /*
     * Real vectors can usually maintain their cached global range from
     * the changed source interval alone.
     */
    if (vPtr->type == RBC_VECTOR_REAL) {
        if (!UpdateVectorRangeForChangedRange(vPtr, first, last)) {
            InvalidateVectorRange(vPtr);
        }
    }
    ScheduleVectorUpdate(vPtr, FALSE, first, last);
}

/**
 * @ingroup vector_storage_private
 * @brief Perform one mapped-array access while the wrapper retains its owners.
 * Reads materialize copied values; writes stage conversion before committing an
 * inclusive interval; unsets compact storage and publish a full update. Value
 * and index expressions may execute Tcl. Revalidate liveness, writability and
 * storage layout before writing, and retain a private conversion object so
 * callback changes to the array cannot invalidate a complex pair. Callback
 * effects are not rolled back. Errors are retained Tcl objects for traces
 * registered with TCL_TRACE_RESULT_OBJECT.
 *
 * @param[in,out] clientData Borrowed traced vector record; the wrapper preserves its allocation, not destroyed
 *   numeric storage.
 * @param[in,out] interp Borrowed interpreter for lookup, evaluation and diagnostics; caller keeps it valid
 *   throughout the call.
 * @param[in] part1 Borrowed array variable name supplied by Tcl for this trace invocation.
 * @param[in] part2 Borrowed element index text, or NULL for a whole-array operation.
 * @param[in] flags Tcl trace operation and namespace flags; error results follow TCL_TRACE_RESULT_OBJECT
 *   ownership.
 */
static char *VectorVarTraceImpl(ClientData clientData, Tcl_Interp *interp, char *part1, char *part2, int flags) {
    Rbc_VectorIndexProc *indexProc;
    VectorObject *vPtr = clientData;
    Tcl_Size first, last;
    int varFlags;

    if (part2 == NULL) {
        if (flags & TCL_TRACE_UNSETS) {
            /* vector is deleted via an unset on the whole array variable */
            ckfree((char *)vPtr->arrayName);
            vPtr->arrayName = NULL;
            if (vPtr->freeOnUnset) {
                Rbc_VectorFree(vPtr);
            }
        }
        return NULL;
    }
    if (Rbc_VectorGetIndexRange(interp, vPtr, part2, INDEX_ALL_FLAGS, &indexProc) != TCL_OK) {
        goto error;
    }
    if (VectorAlive(vPtr) != TCL_OK) {
        goto error;
    }
    first = vPtr->first;
    last = vPtr->last;
    varFlags = TCL_LEAVE_ERR_MSG | (flags & (TCL_GLOBAL_ONLY | TCL_NAMESPACE_ONLY));
    if ((flags & (TCL_TRACE_WRITES | TCL_TRACE_UNSETS)) && vPtr->readOnly) {
        Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr);
        return VectorTraceError(Tcl_GetObjResult(interp));
    }
    if (flags & TCL_TRACE_WRITES) {
        Tcl_Obj *objPtr;
        double realValue;
        Rbc_Complex complexValue;
        int result;
        void *data = vPtr->data.raw;
        Tcl_Size length = vPtr->length;
        Tcl_Size size = vPtr->size;
        Tcl_Size offset = vPtr->offset;

        if ((first == SPECIAL_INDEX) || (last == SPECIAL_INDEX)) {
            return VectorTraceError(Tcl_NewStringObj("read-only index", -1));
        }
        objPtr = Tcl_GetVar2Ex(interp, part1, part2, varFlags);
        if (objPtr == NULL) {
            goto error;
        }
        /* Conversion can unset or replace the source array entry. */
        objPtr = Tcl_DuplicateObj(objPtr);
        Tcl_IncrRefCount(objPtr);
        switch (vPtr->type) {
        case RBC_VECTOR_REAL:
            result = Rbc_GetDouble(interp, objPtr, &realValue);
            break;
        case RBC_VECTOR_COMPLEX:
            result = Rbc_GetComplex(interp, objPtr, &complexValue);
            break;
        default:
            Tcl_Panic("bad vector type %d", (int)vPtr->type);
            result = TCL_ERROR;
            break;
        }
        Tcl_DecrRefCount(objPtr);
        if (result != TCL_OK) {
            goto error;
        }
        if (VectorAlive(vPtr) != TCL_OK ||
            Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr) != TCL_OK) {
            goto error;
        }
        if (vPtr->data.raw != data || vPtr->length != length ||
            vPtr->size != size || vPtr->offset != offset) {
            return VectorTraceError(Tcl_NewStringObj("vector changed during variable conversion", -1));
        }
        /* The saved selection is valid only for the unchanged layout. */
        if ((first == vPtr->length) || (last == vPtr->length)) {
            if (vPtr->length == TCL_SIZE_MAX) {
                return VectorTraceError(Tcl_NewStringObj("vector is too large to append", -1));
            }
            if (Rbc_VectorChangeLength(vPtr, vPtr->length + 1) != TCL_OK) {
                return VectorTraceError(Tcl_NewStringObj("error resizing vector", -1));
            }
        }
        switch (vPtr->type) {
        case RBC_VECTOR_REAL:
            Rbc_ReplicateValue(vPtr, first, last, realValue);
            break;
        case RBC_VECTOR_COMPLEX: {
            Tcl_Size i;

            for (i = first; i <= last; i++) {
                vPtr->data.complex[i] = complexValue;
            }
            vPtr->notifyFlags |= UPDATE_RANGE;
            break;
        }
        default:
            Tcl_Panic("bad vector type %d", (int)vPtr->type);
        }
    } else if (flags & TCL_TRACE_READS) {
        Tcl_Obj *objPtr;

        if ((first == vPtr->length) || (last == vPtr->length)) {
            return VectorTraceError(Tcl_NewStringObj("write-only index", -1));
        }
        if (vPtr->length == 0) {
            if (Tcl_SetVar2(interp, part1, part2, "", varFlags) == NULL) {
                goto error;
            }
            return NULL;
        }
        if (first == last) {
            if (first >= 0) {
                objPtr = Rbc_NewVectorValueObj(vPtr, first);
            } else {
                double value;

                /*
                 * min/max/mean/sum/prod still use real-only
                 * Rbc_VectorIndexProc callbacks.
                 */
                if (vPtr->type != RBC_VECTOR_REAL) {
                    return VectorTraceError(
                        Tcl_NewStringObj("special vector indices are not supported for complex vectors", -1));
                }
                vPtr->first = 0;
                vPtr->last = vPtr->length - 1;
                value = (*indexProc)((Rbc_Vector *)vPtr);
                if (VectorAlive(vPtr) != TCL_OK) {
                    goto error;
                }
                objPtr = Tcl_NewDoubleObj(value);
            }
            Tcl_IncrRefCount(objPtr);
            Tcl_Obj *stored = Tcl_SetVar2Ex(interp, part1, part2, objPtr, varFlags);
            Tcl_DecrRefCount(objPtr);
            if (stored == NULL) {
                goto error;
            }
        } else {
            objPtr = Rbc_GetValues(vPtr, first, last);
            Tcl_IncrRefCount(objPtr);
            Tcl_Obj *stored = Tcl_SetVar2Ex(interp, part1, part2, objPtr, varFlags);
            Tcl_DecrRefCount(objPtr);
            if (stored == NULL) {
                goto error;
            }
        }
    } else if (flags & TCL_TRACE_UNSETS) {
        Tcl_Size i, j;

        if ((first == vPtr->length) || (first == SPECIAL_INDEX)) {
            return VectorTraceError(Tcl_NewStringObj("special vector index", -1));
        }
        /*
         * Collapse the vector from the point of the first unset element.
         * Also flush any array variable entries so that the shift is
         * reflected when the array variable is read.
         */
        for (i = first, j = last + 1; j < vPtr->length; i++, j++) {
            switch (vPtr->type) {
            case RBC_VECTOR_REAL:
                vPtr->data.real[i] = vPtr->data.real[j];
                break;
            case RBC_VECTOR_COMPLEX:
                vPtr->data.complex[i] = vPtr->data.complex[j];
                break;
            default:
                Tcl_Panic("bad vector type %d", (int)vPtr->type);
            }
        }
        vPtr->length -= ((last - first) + 1);
        if (vPtr->flush) {
            Rbc_VectorFlushCache(vPtr);
            if (VectorAlive(vPtr) != TCL_OK) {
                goto error;
            }
        }
    } else {
        return VectorTraceError(Tcl_NewStringObj("unknown variable trace flag", -1));
    }
    if (flags & TCL_TRACE_WRITES) {
        /*
         * The write path knows the exact inclusive source range that
         * was modified.  Preserve it for clients such as graph display
         * decimation caches.
         *
         * Appends are harmless here: if the vector length changed,
         * clients will detect the length/storage mismatch and fall back
         * to a complete rebuild.
         */
        Rbc_VectorUpdateClientsRange(vPtr, first, last);

    } else if (flags & TCL_TRACE_UNSETS) {
        /*
         * Removing a range shifts every following source index, so the
         * resulting modification is not restricted to the unset range.
         */
        Rbc_VectorUpdateClients(vPtr);
    }
    Tcl_ResetResult(interp);
    return NULL;

error:
    return VectorTraceError(Tcl_GetObjResult(interp));
}

/**
 * @ingroup vector_storage_private
 * @brief Retain the vector and interpreter for the complete variable trace.
 * Keeps the record alive across index/value expressions, array traces, custom
 * index procedures, storage release and immediate observer notification. This
 * does not retain destroyed numeric storage; the implementation checks state
 * again before access after callbacks. Returns an owned trace-error object or
 * NULL, releasing both preservation references on every exit.
 *
 * @param[in,out] clientData Borrowed traced vector record; the wrapper preserves its allocation, not destroyed
 *   numeric storage.
 * @param[in,out] interp Borrowed interpreter for lookup, evaluation and diagnostics; caller keeps it valid
 *   throughout the call.
 * @param[in] part1 Borrowed array variable name supplied by Tcl for this trace invocation.
 * @param[in] part2 Borrowed element index text, or NULL for a whole-array operation.
 * @param[in] flags Tcl trace operation and namespace flags; error results follow TCL_TRACE_RESULT_OBJECT
 *   ownership.
 */
static char *VectorVarTrace(ClientData clientData, Tcl_Interp *interp, char *part1, char *part2, int flags) {
    VectorObject *vPtr = clientData;
    char *result;

    Tcl_Preserve(interp);
    Tcl_Preserve(vPtr);
    result = VectorVarTraceImpl(clientData, interp, part1, part2, flags);
    Tcl_Release(vPtr);
    Tcl_Release(interp);
    return result;
}

/**
 * @ingroup vector_storage_private
 * @brief Build an absolute name relative to the current namespace.
 * NULL input returns NULL without initializing the output. Otherwise initializes the caller DString,
 * preserves an already absolute spelling, or prefixes the current namespace. Returns borrowed DString
 * storage that the caller must free. Does not create or validate namespace existence.
 *
 * @param[in,out] interp Borrowed interpreter for lookup, evaluation and diagnostics; caller keeps it valid
 *   throughout the call.
 * @param[in] name Borrowed name, or NULL to return without initializing fullName.
 * @param[out] fullName Caller-owned DString initialized for non-NULL name; caller frees it after using the
 *   returned buffer.
 */
static char *BuildQualifiedName(Tcl_Interp *interp, const char *name, Tcl_DString *fullName) {
    Tcl_Namespace *nsPtr;

    if (name == NULL) {
        return NULL;
    }
    Tcl_DStringInit(fullName);
    /* FIXME: Doesn't work in Tcl 8.4 */
    nsPtr = Tcl_GetCurrentNamespace(interp);
    if ((name[0] == ':') && (name[1] == ':')) {
        /* we have a fully qualified name already -> just return the given name */
        Tcl_DStringAppend(fullName, name, -1);
        return Tcl_DStringValue(fullName);
    }
    /* build a qualified name */
    Tcl_DStringAppend(fullName, nsPtr->fullName, -1);
    if (Tcl_DStringLength(fullName) > 2) {
        /* namespace is not the root namespace, so we need a separator */
        Tcl_DStringAppend(fullName, "::", -1);
    }
    Tcl_DStringAppend(fullName, name, -1);
    return Tcl_DStringValue(fullName);
}

/**
 * @ingroup vector_storage_private
 * @brief Split a name at its final namespace separator.
 * Without a separator, returns a NULL namespace and the original name. Otherwise resolves the
 * namespace and returns a tail pointer into the borrowed input. Does not create namespaces, parse
 * vector ranges or validate the tail. Output pointers are meaningful only on TCL_OK; a missing
 * namespace returns TCL_ERROR without constructing a vector-specific diagnostic.
 *
 * @param[in,out] interp Borrowed interpreter for lookup, evaluation and diagnostics; caller keeps it valid
 *   throughout the call.
 * @param[in] qualName Borrowed NUL-terminated name, possibly unqualified or empty.
 * @param[out] nsPtrPtr Required borrowed namespace output on success; NULL denotes an unqualified name.
 * @param[out] namePtrPtr Required tail output pointing into qualName on success; no string is allocated.
 */
static int ParseQualifiedName(Tcl_Interp *interp, const char *qualName, Tcl_Namespace **nsPtrPtr,
                              const char **namePtrPtr) {
    const char *p;
    const char *separator;
    const char *name;
    Tcl_Namespace *nsPtr;

    separator = NULL;
    name = qualName;
    /*
     * Find the final namespace separator without walking before the
     * beginning of an empty or one-character string.
     */
    p = qualName + strlen(qualName);
    while (p > (qualName + 1)) {
        p--;
        if ((*p == ':') && (p[-1] == ':')) {
            separator = p - 1;
            name = p + 1;
            break;
        }
    }
    if (separator == NULL) {
        *nsPtrPtr = NULL;
        *namePtrPtr = qualName;
        return TCL_OK;
    }
    if (separator == qualName) {
        /*
         * A name beginning with "::" belongs to the global namespace.
         */
        nsPtr = Tcl_GetGlobalNamespace(interp);
    } else {
        Tcl_Obj *namespaceObjPtr;
        namespaceObjPtr = Tcl_NewStringObj(qualName, (Tcl_Size)(separator - qualName));
        Tcl_IncrRefCount(namespaceObjPtr);
        nsPtr = Tcl_FindNamespace(interp, Tcl_GetString(namespaceObjPtr), NULL, 0);
        Tcl_DecrRefCount(namespaceObjPtr);
    }
    if (nsPtr == NULL) {
        return TCL_ERROR;
    }
    *nsPtrPtr = nsPtr;
    *namePtrPtr = name;
    return TCL_OK;
}

/**
 * @ingroup vector_storage_private
 * @brief Join a known namespace and an unqualified tail.
 * Initializes the caller DString and returns its borrowed buffer, adding exactly the join separator
 * for the global namespace. Requires a live namespace and readable tail; does not parse or normalize a
 * qualified tail. Caller releases the DString.
 *
 * @param[in] nsPtr Borrowed live namespace supplying its absolute fullName.
 * @param[in] name Borrowed unqualified NUL-terminated tail; not parsed or normalized.
 * @param[out] resultPtr Caller-owned DString initialized here; caller frees it after using the borrowed return
 *   buffer.
 */
static char *GetQualifiedName(Tcl_Namespace *nsPtr, const char *name, Tcl_DString *resultPtr) {
    Tcl_DStringInit(resultPtr);
    if ((nsPtr->fullName[0] != ':') || (nsPtr->fullName[1] != ':') || (nsPtr->fullName[2] != '\0')) {
        Tcl_DStringAppend(resultPtr, nsPtr->fullName, -1);
    }
    Tcl_DStringAppend(resultPtr, "::", -1);
    Tcl_DStringAppend(resultPtr, (char *)name, -1);
    return Tcl_DStringValue(resultPtr);
}

/**
 * @ingroup vector_storage_private
 * @brief Resolve a literal vector registry name with controlled namespace search.
 * Explicit namespace syntax restricts lookup to that namespace. Otherwise flags select current
 * namespace first and optional global fallback. Returns a borrowed vector or NULL, without changing
 * its index selection. Range syntax belongs to higher-level parsing, not this lookup helper.
 *
 * @param[in] dataPtr Borrowed live per-interpreter vector registry; lookup does not transfer ownership.
 * @param[in] name Borrowed literal registry name; no range suffix is parsed.
 * @param[in] flags NS_SEARCH_CURRENT and NS_SEARCH_GLOBAL bits for unqualified lookup, in that order.
 */
static VectorObject *GetVectorObject(VectorInterpData *dataPtr, const char *name, int flags) {
    const char *vecName;
    Tcl_Namespace *nsPtr;
    VectorObject *vPtr;

    nsPtr = NULL;
    vecName = name;
    if (ParseQualifiedName(dataPtr->interp, name, &nsPtr, &vecName) != TCL_OK) {
        return NULL; /* Can't find namespace. */
    }
    vPtr = NULL;
    if (nsPtr != NULL) {
        vPtr = FindVectorInNamespace(dataPtr, nsPtr, vecName);
    } else {
        if (flags & NS_SEARCH_CURRENT) {
            nsPtr = Tcl_GetCurrentNamespace(dataPtr->interp);
            vPtr = FindVectorInNamespace(dataPtr, nsPtr, vecName);
        }
        if ((vPtr == NULL) && (flags & NS_SEARCH_GLOBAL)) {
            nsPtr = Tcl_GetGlobalNamespace(dataPtr->interp);
            vPtr = FindVectorInNamespace(dataPtr, nsPtr, vecName);
        }
    }
    return vPtr;
}

/**
 * @ingroup vector_storage_private
 * @brief Find a literal vector in one namespace registry scope.
 * @param[in] dataPtr Live interpreter-local vector registry.
 * @param[in] nsPtr Namespace used to qualify vecName.
 * @param[in] vecName NUL-terminated literal name, without range parsing.
 * @return Borrowed registry record, or NULL if the qualified key does not exist.
 *
 * Builds a temporary qualified key, queries the interpreter-local vector table and frees the key.
 * Returns a borrowed record or NULL. Does not resolve Tcl command aliases or acquire a vector client
 * ID.
 */
static VectorObject *FindVectorInNamespace(VectorInterpData *dataPtr, Tcl_Namespace *nsPtr, const char *vecName) {
    Tcl_DString dString;
    const char *name;
    Tcl_HashEntry *hPtr;

    name = GetQualifiedName(nsPtr, vecName, &dString);
    hPtr = Tcl_FindHashEntry(&(dataPtr->vectorTable), name);
    Tcl_DStringFree(&dString);
    if (hPtr != NULL) {
        return (VectorObject *)Tcl_GetHashValue(hPtr);
    }
    return NULL;
}

/**
 * @brief Copy an inclusive storage interval into a Tcl list.
 * Requires live storage and caller-validated bounds. Appends a newly copied scalar or complex pair for
 * each zero-based index, without consulting the external offset or changing selection. Returns a new
 * reference-count-zero list, empty for a reversed interval. No storage is shared with the returned
 * values and no observers are notified.
 */
Tcl_Obj *Rbc_GetValues(VectorObject *vPtr, Tcl_Size first, Tcl_Size last) {
    Tcl_Size i;
    Tcl_Obj *listObjPtr;

    listObjPtr = Tcl_NewListObj(0, NULL);
    for (i = first; i <= last; i++) {
        Tcl_ListObjAppendElement(vPtr->interp, listObjPtr, Rbc_NewVectorValueObj(vPtr, i));
    }
    return listObjPtr;
}

/**
 * @brief Overwrite an inclusive interval of real storage.
 * @param[in,out] vPtr Live writable real vector; caller enforces protection and publishes the change.
 * @param[in] first First zero-based storage index, validated by the caller for a nonempty interval.
 * @param[in] last Inclusive final storage index; a reversed interval performs no writes.
 * @param[in] value Double assigned to every selected storage slot.
 *
 * Requires a live real vector, writable destination and valid zero-based bounds, all checked by the
 * caller. Reversed intervals do no work. Performs only direct assignments: does not enforce read-only
 * state, resize, invalidate extrema, flush mapped arrays, increment dirty or notify clients. Callers
 * must publish the change separately.
 */
void Rbc_ReplicateValue(VectorObject *vPtr, Tcl_Size first, Tcl_Size last, double value) {
    Tcl_Size i;

    for (i = first; i <= last; i++) {
        vPtr->data.real[i] = value;
    }
}

/**
 * @ingroup vector_storage_private
 * @brief Remove the instance command without recursively destroying its vector.
 * @param[in,out] vPtr Vector whose instance command is detached and deleted; caller retains its lifetime.
 *
 * Clears the stored command token first, disables the vector command deletion callback and deletes by
 * token rather than caller-relative name. Other command traces may still run. Caller owns
 * record/interpreter lifetime across deletion; this helper does not remove the registry entry or
 * mapped variable.
 */
static void DeleteCommand(VectorObject *vPtr) {
    Tcl_Interp *interp = vPtr->interp;
    Tcl_CmdInfo cmdInfo;
    Tcl_Command token = vPtr->cmdToken;

    /* A command's short name is relative to the caller's namespace. Use the
     * token so deleting a vector from another namespace cannot leave a
     * dangling instance command (or delete an unrelated same-name command). */
    vPtr->cmdToken = 0;
    if (token != NULL && Tcl_GetCommandInfoFromToken(token, &cmdInfo)) {
        /* Disable the callback before deleting the Tcl command. */
        cmdInfo.deleteProc = NULL;
        Tcl_SetCommandInfoFromToken(token, &cmdInfo);
        Tcl_DeleteCommandFromToken(interp, token);
    }
}

/**
 * @ingroup vector_storage_private
 * @brief Detach and unset the vector mapped-array access surface.
 * @param[in,out] vPtr Vector whose owned array name is detached and released; caller retains its lifetime.
 *
 * Captures the owned name and flags, clears arrayName before removing the RBC trace and unsetting the
 * variable, then frees the captured name. User unset traces may destroy or remap the vector. No vector
 * fields are accessed after unset; caller owns surrounding lifetime and continuation checks.
 */
static void UnmapVariable(VectorObject *vPtr) {
    Tcl_Interp *interp = vPtr->interp;
    char *name = vPtr->arrayName;
    int flags = vPtr->varFlags;

    if (name == NULL) {
        return;
    }
    /* Detach before user unset traces can recursively destroy or remap it. */
    vPtr->arrayName = NULL;
    Tcl_UntraceVar2(interp, name, NULL, TRACE_ALL | flags, (Tcl_VarTraceProc *)VectorVarTrace, vPtr);
    Tcl_UnsetVar2(interp, name, NULL, flags);
    ckfree(name);
}

int Rbc_GetDouble(Tcl_Interp *interp, Tcl_Obj *objPtr, double *valuePtr) {
    /* First try to extract the value as a double precision number. */
    if (Tcl_GetDoubleFromObj(interp, objPtr, valuePtr) == TCL_OK) {
        return TCL_OK;
    }
    Tcl_ResetResult(interp);
    /* Then try to parse it as an expression. */
    if (Tcl_ExprDoubleObj(interp, objPtr, valuePtr) == TCL_OK) {
        return TCL_OK;
    }
    return TCL_ERROR;
}

Tcl_Obj *Rbc_NewComplexObj(Rbc_Complex value) {
    Tcl_Obj *objv[2];

    objv[0] = Tcl_NewDoubleObj(value.real);
    objv[1] = Tcl_NewDoubleObj(value.imag);
    return Tcl_NewListObj(2, objv);
}

Tcl_Obj *Rbc_NewVectorValueObj(VectorObject *vPtr, Tcl_Size index) {
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        return Tcl_NewDoubleObj(vPtr->data.real[index]);
    case RBC_VECTOR_COMPLEX:
        return Rbc_NewComplexObj(vPtr->data.complex[index]);
    }
    Tcl_Panic("bad vector type %d", (int)vPtr->type);
    return NULL;
}

int Rbc_GetComplex(Tcl_Interp *interp, Tcl_Obj *objPtr, Rbc_Complex *valuePtr) {
    Tcl_Size objc;
    Tcl_Obj **objv;
    double real;

    /*
     * A two-element Tcl list is the canonical complex representation:
     *
     *     {real imag}
     *
     * Check this before Rbc_GetDouble(), because Rbc_GetDouble()
     * also accepts Tcl expressions.  For example, {3 -4} would
     * otherwise be evaluated as the expression "3 - 4".
     */
    if (Tcl_ListObjGetElements(NULL, objPtr, &objc, &objv) == TCL_OK) {
        if (objc == 2) {
            if (Rbc_GetDouble(interp, objv[0], &valuePtr->real) != TCL_OK) {
                return TCL_ERROR;
            }
            if (Rbc_GetDouble(interp, objv[1], &valuePtr->imag) != TCL_OK) {
                return TCL_ERROR;
            }
            return TCL_OK;
        }
    }
    /*
     * Otherwise accept a scalar real value (including an expression)
     * and promote it to complex with zero imaginary part.
     */
    if (Rbc_GetDouble(interp, objPtr, &real) == TCL_OK) {
        valuePtr->real = real;
        valuePtr->imag = 0.0;
        return TCL_OK;
    }
    Tcl_ResetResult(interp);
    Tcl_SetObjResult(interp, Tcl_NewStringObj("expected complex value as a real number or {real imag}", -1));
    return TCL_ERROR;
}

Rbc_Complex Rbc_ComplexFromReal(double value) {
    Rbc_Complex result;

    result.real = value;
    result.imag = 0.0;
    return result;
}

int Rbc_ComplexIsZero(Rbc_Complex value) { return ((value.real == 0.0) && (value.imag == 0.0)); }

Rbc_Complex Rbc_ComplexAdd(Rbc_Complex a, Rbc_Complex b) {
    Rbc_Complex result;

    result.real = a.real + b.real;
    result.imag = a.imag + b.imag;
    return result;
}

Rbc_Complex Rbc_ComplexSub(Rbc_Complex a, Rbc_Complex b) {
    Rbc_Complex result;

    result.real = a.real - b.real;
    result.imag = a.imag - b.imag;
    return result;
}

Rbc_Complex Rbc_ComplexMul(Rbc_Complex a, Rbc_Complex b) {
    Rbc_Complex result;

    result.real = (a.real * b.real) - (a.imag * b.imag);
    result.imag = (a.real * b.imag) + (a.imag * b.real);
    return result;
}

Rbc_Complex Rbc_ComplexDiv(Rbc_Complex a, Rbc_Complex b) {
    Rbc_Complex result;
    /*
     * Use the ratio form rather than directly computing
     *
     *     b.real*b.real + b.imag*b.imag
     *
     * to reduce unnecessary overflow/underflow.
     *
     * The caller must reject b == 0+0i.
     */
    if (fabs(b.real) >= fabs(b.imag)) {
        double ratio;
        double denominator;

        ratio = b.imag / b.real;
        denominator = b.real + b.imag * ratio;
        result.real = (a.real + a.imag * ratio) / denominator;
        result.imag = (a.imag - a.real * ratio) / denominator;

    } else {
        double ratio;
        double denominator;

        ratio = b.real / b.imag;
        denominator = b.imag + b.real * ratio;
        result.real = (a.real * ratio + a.imag) / denominator;
        result.imag = (a.imag * ratio - a.real) / denominator;
    }
    return result;
}

Rbc_Complex Rbc_VectorValueAsComplex(VectorObject *vPtr, Tcl_Size index) {
    switch (vPtr->type) {
    case RBC_VECTOR_REAL:
        return Rbc_ComplexFromReal(vPtr->data.real[index]);
    case RBC_VECTOR_COMPLEX:
        return vPtr->data.complex[index];
    }
    Tcl_Panic("bad vector type %d", (int)vPtr->type);
    return Rbc_ComplexFromReal(0.0);
}

void Rbc_FreeVectorId(Rbc_VectorId clientId) {
    VectorClient *clientPtr = (VectorClient *)clientId;
    if (clientPtr == NULL || clientPtr->magic != VECTOR_MAGIC) {
        return;
    }
    if (clientPtr->serverPtr != NULL) {
        Rbc_ChainDeleteLink(clientPtr->serverPtr->chainPtr, clientPtr->linkPtr);
    }
    clientPtr->serverPtr = NULL;
    clientPtr->linkPtr = NULL;
    clientPtr->proc = NULL;
    clientPtr->magic = 0;
    Tcl_EventuallyFree(clientPtr, FreeVectorRecord);
}

int Rbc_GetVectorById(Tcl_Interp *interp, Rbc_VectorId clientId, Rbc_Vector **vecPtrPtr) {
    VectorClient *clientPtr = (VectorClient *)clientId;
    if (clientPtr->magic != VECTOR_MAGIC) {
        Rbc_AppendResultStrings(interp, "bad vector token", (char *)NULL);
        return TCL_ERROR;
    }
    if (clientPtr->serverPtr == NULL) {
        Rbc_AppendResultStrings(interp, "vector no longer exists", (char *)NULL);
        return TCL_ERROR;
    }
    /*
     * Preserve the existing behaviour that obtaining a vector by ID
     * leaves its cached range current, but do not rescan an already
     * current vector.
     */    
    if (clientPtr->serverPtr->notifyFlags & UPDATE_RANGE) {
        Rbc_VectorUpdateRange(clientPtr->serverPtr);
    }
    *vecPtrPtr = (Rbc_Vector *)clientPtr->serverPtr;
    return TCL_OK;
}

/**
 * @brief Test whether a literal registered vector name exists.
 * @ingroup vector_storage_public
 * @param[in] interp See @ref rbc_interp_argument; uses its vector registry.
 * @param[in] vecName Borrowed literal name, resolved in the current/global namespaces.
 * @return Nonzero if found, zero otherwise; no ownership is acquired.
 *
 * Searches the current/global namespace rules without parsing a vector range.
 * Returns a boolean; accepts an interpreter and name, not an observer token.
 * May lazily initialize interpreter vector data. Does not retain a found vector.
 */
int Rbc_VectorExists2(Tcl_Interp *interp, const char *vecName) {
    VectorInterpData *dataPtr; /* Interpreter-specific data. */

    dataPtr = Rbc_VectorGetInterpData(interp);
    if (GetVectorObject(dataPtr, vecName, NS_SEARCH_BOTH) != NULL) {
        return TRUE;
    }
    return FALSE;
}

Rbc_VectorId Rbc_AllocVectorId(Tcl_Interp *interp, const char *name) {
    VectorInterpData *dataPtr; /* Interpreter-specific data. */
    VectorObject *vPtr;
    VectorClient *clientPtr;
    Rbc_VectorId clientId;
    int result;
    char *nameCopy;

    dataPtr = Rbc_VectorGetInterpData(interp);
    /*
     * If the vector name was passed via a read-only string (e.g. "x"),
     * the VectorParseName routine will segfault when it tries to write
     * into the string.  Therefore make a writable copy and free it
     * when we're done.
     */
    nameCopy = RbcStrdup(name);
    result = Rbc_VectorLookupName(dataPtr, nameCopy, &vPtr);
    ckfree((char *)nameCopy);
    if (result != TCL_OK) {
        return (Rbc_VectorId)0;
    }
    if (vPtr->destroying) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vector is being destroyed", -1));
        return NULL;
    }
    /* Allocate a new client structure */
    clientPtr = RbcCalloc(1, sizeof(VectorClient));
    assert(clientPtr);
    clientPtr->magic = VECTOR_MAGIC;
    /* Add the new client to the server's list of clients */
    clientPtr->linkPtr = Rbc_ChainAppend(vPtr->chainPtr, clientPtr);
    clientPtr->serverPtr = vPtr;
    clientId = (Rbc_VectorId)clientPtr;
    return clientId;
}

void Rbc_SetVectorChangedProc(Rbc_VectorId clientId, Rbc_VectorChangedProc *proc, ClientData clientData) {
    VectorClient *clientPtr = (VectorClient *)clientId;
    if (clientPtr->magic != VECTOR_MAGIC) {
        return; /* Not a valid token */
    }
    clientPtr->clientData = clientData;
    clientPtr->proc = proc;
}

int Rbc_VectorGetChangedRange(Rbc_VectorId clientId, Tcl_Size *firstPtr, Tcl_Size *lastPtr) {
    VectorClient *clientPtr;

    clientPtr = (VectorClient *)clientId;
    if ((clientPtr == NULL) || (clientPtr->magic != VECTOR_MAGIC) || (clientPtr->serverPtr == NULL) ||
        clientPtr->updateAll || !clientPtr->updateRangeValid) {
        return FALSE;
    }
    if (firstPtr != NULL) {
        *firstPtr = clientPtr->updateFirst;
    }
    if (lastPtr != NULL) {
        *lastPtr = clientPtr->updateLast;
    }
    return TRUE;
}

char *Rbc_NameOfVectorId(Rbc_VectorId clientId) {
    VectorClient *clientPtr = (VectorClient *)clientId;
    if ((clientPtr->magic != VECTOR_MAGIC) || (clientPtr->serverPtr == NULL)) {
        return NULL;
    }
    return clientPtr->serverPtr->name;
}

/**
 * @brief Resolve a vector reference into a borrowed C vector pointer.
 * @ingroup vector_storage_public
 * @param[in] interp See @ref rbc_interp_argument; index expressions may execute Tcl.
 * @param[in] name Borrowed vector reference, optionally including a range.
 * @param[out] vecPtrPtr Required pointer receiving the borrowed identity on success; unchanged on error.
 * @return TCL_OK on lookup success; TCL_ERROR with a diagnostic otherwise.
 *
 * Returns TCL_OK and writes vecPtrPtr on success; reports lookup failure with
 * TCL_ERROR. Allows range syntax, updates the shared first/last selection and
 * refreshes invalid cached extrema. Index expressions may execute Tcl. The result
 * is the original vector identity, not a slice or ownership reference; callers
 * must manage lifetime before using it across subsequent evaluation.
 */
int Rbc_GetVector(Tcl_Interp *interp, const char *name, Rbc_Vector **vecPtrPtr) {
    VectorInterpData *dataPtr;
    VectorObject *vPtr;

    dataPtr = Rbc_VectorGetInterpData(interp);
    if (Rbc_VectorLookupName(dataPtr, name, &vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (vPtr->notifyFlags & UPDATE_RANGE) {
        Rbc_VectorUpdateRange(vPtr);
    }
    *vecPtrPtr = (Rbc_Vector *)vPtr;
    return TCL_OK;
}

/**
 * @brief Create or reuse a typed vector with explicit command and array names.
 * @ingroup vector_storage_private
 *
 * Validates nonnegative initialSize and real/complex type, then delegates to
 * Rbc_VectorCreate. A positive size resizes; zero leaves an existing vector
 * length unchanged. Writes an optional borrowed output pointer on success.
 * Returns a Tcl status; initial resize failure removes a newly created identity
 * if still alive. The output pointer is untouched on failure. Retains and checks
 * the identity across callbacks; does not explicitly publish the initial resize.
 * Existing-object changes and callback effects are not rolled back.
 *
 * @param[in,out] interp Borrowed interpreter for lookup, evaluation and diagnostics; caller keeps it valid
 *   throughout the call.
 * @param[in] vecName Borrowed registry name or `#auto` request, resolved by Rbc_VectorCreate.
 * @param[in] cmdName Borrowed command name, or NULL to omit command setup.
 * @param[in] varName Borrowed array mapping name, or NULL to omit mapping setup; passed unchanged to
 *   Rbc_VectorCreate.
 * @param[in] initialSize Nonnegative element count; zero leaves an existing length unchanged, positive values
 *   request resizing.
 * @param[in] type RBC_VECTOR_REAL or RBC_VECTOR_COMPLEX; existing vectors must have the requested type.
 * @param[out] vecPtrPtr Optional borrowed vector output, written only on success; no client retention token is
 *   created.
 */
int Rbc_CreateVector2(Tcl_Interp *interp, const char *vecName, const char *cmdName, const char *varName,
                      Tcl_Size initialSize, Rbc_VectorType type, Rbc_Vector **vecPtrPtr) {
    VectorInterpData *dataPtr;
    VectorObject *vPtr;
    int isNew, result = TCL_ERROR;

    if (initialSize < 0) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad vector size \"%" TCL_SIZE_MODIFIER "d\"", initialSize));
        return TCL_ERROR;
    }
    if ((type != RBC_VECTOR_REAL) && (type != RBC_VECTOR_COMPLEX)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad vector type %d", (int)type));
        return TCL_ERROR;
    }
    dataPtr = Rbc_VectorGetInterpData(interp);
    vPtr = Rbc_VectorCreate(dataPtr, vecName, cmdName, varName, type, &isNew);
    if (vPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_Preserve(interp);
    Tcl_Preserve(vPtr);
    if (initialSize > 0 && Rbc_VectorChangeLength(vPtr, initialSize) != TCL_OK) {
        goto done;
    }
    if (VectorAlive(vPtr) != TCL_OK) {
        goto done;
    }
    if (vecPtrPtr != NULL) {
        *vecPtrPtr = (Rbc_Vector *)vPtr;
    }
    result = TCL_OK;
done:
    if (result != TCL_OK && isNew && !vPtr->destroying) {
        Tcl_InterpState state = Tcl_SaveInterpState(interp, result);
        Rbc_VectorFree(vPtr);
        Tcl_RestoreInterpState(interp, state);
    }
    Tcl_Release(vPtr);
    Tcl_Release(interp);
    return result;
}

/**
 * @brief Create or reuse a real vector through the typed creation interface.
 * @ingroup vector_storage_public
 * @param[in] interp See @ref rbc_interp_argument.
 * @param[in] name Borrowed requested name; \#auto requests a generated name.
 * @param[in] size Nonnegative initial length in values; zero retains an existing length.
 * @param[out] vecPtrPtr Optional borrowed identity output; unchanged on error.
 * @return Tcl creation status; callback side effects are not rolled back.
 *
 * Delegates to @ref Rbc_CreateVectorWithType with RBC_VECTOR_REAL. Returns a Tcl
 * status and optionally a borrowed vector pointer. Existing same-type writable
 * vectors may be reused; this is not a create-only operation.
 */
int Rbc_CreateVector(Tcl_Interp *interp, const char *name, Tcl_Size size, Rbc_Vector **vecPtrPtr) {
    return Rbc_CreateVectorWithType(interp, name, size, RBC_VECTOR_REAL, vecPtrPtr);
}

/**
 * @brief Create or reuse a typed vector with its same-named Tcl command.
 * @ingroup vector_storage_public
 * @param[in] interp See @ref rbc_interp_argument.
 * @param[in] name Borrowed requested name; \#auto requests a generated name.
 * @param[in] size Nonnegative initial length in values; zero retains an existing length.
 * @param[in] type RBC_VECTOR_REAL or RBC_VECTOR_COMPLEX; reused vectors must match.
 * @param[out] vecPtrPtr Optional borrowed identity output; unchanged on error.
 * @return Tcl creation status; callback side effects are not rolled back.
 *
 * Real names without an opening parenthesis request a same-named mapped array;
 * complex names and real names containing parentheses do not. Delegates to
 * @ref Rbc_CreateVector2. Type must be real or complex. A zero requested size does
 * not clear an existing vector. Returns a Tcl status; output ownership and
 * callback handling are those of the underlying creation helpers.
 */
int Rbc_CreateVectorWithType(Tcl_Interp *interp, const char *name, Tcl_Size size, Rbc_VectorType type,
                             Rbc_Vector **vecPtrPtr) {
    const char *varName;

    /*
     * Match the Tcl-level creation semantics: real vectors receive
     * their traditional automatic array mapping unless their name contains
     * parentheses; complex vectors do not.
     */
    varName = (type == RBC_VECTOR_REAL && strchr(name, '(') == NULL) ? name : NULL;
    return Rbc_CreateVector2(interp, name, name, varName, size, type, vecPtrPtr);
}

/**
 * @brief Resize a live vector and publish a full change.
 * @ingroup vector_storage_public
 * @param[in,out] vecPtr Live borrowed destination; must be writable.
 * @param[in] length Nonnegative requested length in values, subject to allocation limits.
 * @return Tcl resize status; success does not guarantee survival of notification callbacks.
 *
 * Delegates to @ref Rbc_VectorChangeLength, flushes when configured and reports a
 * full update. On resize error replaces the message with a generic resize error
 * unless the identity was destroyed. Retains the record and interpreter across
 * custom release and publication. May invalidate borrowed storage; success does
 * not promise that a later publication callback left the vector alive.
 */
int Rbc_ResizeVector(Rbc_Vector *vecPtr, Tcl_Size length) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Interp *interp = vPtr->interp;
    int result;

    Tcl_Preserve(interp);
    Tcl_Preserve(vPtr);
    result = Rbc_VectorChangeLength(vPtr, length);
    if (result == TCL_OK) {
        PublishVectorChange(vPtr, TRUE, 0, 0);
    } else if (!vPtr->destroying && !Tcl_InterpDeleted(interp)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't resize vector \"%s\"", vPtr->name));
    }
    Tcl_Release(vPtr);
    Tcl_Release(interp);
    return result;
}

/**
 * @brief Return the borrowed current registered name.
 * @ingroup vector_storage_public
 * @param[in] vecPtr Live borrowed registered vector.
 * @return Borrowed registered name, invalidated by rename or destruction.
 *
 * Valid until rename or destruction; copy it if needed across either operation.
 * Requires a live registered vector. Does not allocate, retain or change it.
 */
char *Rbc_NameOfVector(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    return vPtr->name;
}

/**
 * @brief Replace the storage contract of a complex vector.
 * @ingroup vector_storage_public
 * @param[in,out] vecPtr Live writable destination of the matching numeric type.
 * @param[in] valueArr Storage to copy, borrow or adopt according to freeProc; NULL requests empty storage.
 * @param[in] length Nonnegative used value count; must not exceed size.
 * @param[in] size Nonnegative capacity in values, not bytes; zero requests empty storage.
 * @param[in] freeProc TCL_STATIC borrows, TCL_DYNAMIC adopts Tcl storage, TCL_VOLATILE copies; otherwise a custom
 *   release callback. Ownership transfers only on successful acceptance.
 * @return TCL_OK once the storage contract is accepted; TCL_ERROR before acceptance.
 *
 * Rejects real vectors, then delegates to @ref rbc_vector_reset_storage_contract "ResetVectorStorage". Length/capacity
 * count complex values, not components or bytes. Honors static, dynamic, volatile
 * and custom ownership policies; publishes a full change on success. Returns a
 * Tcl status with the ownership-commit semantics of @ref rbc_vector_reset_storage_contract "ResetVectorStorage".
 */
int Rbc_ResetComplexVector(Rbc_Vector *vecPtr, Rbc_Complex *valueArr, Tcl_Size length, Tcl_Size size,
                           Tcl_FreeProc *freeProc) {
    VectorObject *vPtr;

    vPtr = (VectorObject *)vecPtr;
    if (vPtr->type != RBC_VECTOR_COMPLEX) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("Rbc_ResetComplexVector cannot reset a real vector", -1));
        return TCL_ERROR;
    }
    return ResetVectorStorage(vPtr, valueArr, length, size, freeProc);
}

/**
 * @brief Replace the storage contract of a real vector.
 * @ingroup vector_storage_public
 * @param[in,out] vecPtr Live writable destination of the matching numeric type.
 * @param[in] valueArr Storage to copy, borrow or adopt according to freeProc; NULL requests empty storage.
 * @param[in] length Nonnegative used value count; must not exceed size.
 * @param[in] size Nonnegative capacity in values, not bytes; zero requests empty storage.
 * @param[in] freeProc TCL_STATIC borrows, TCL_DYNAMIC adopts Tcl storage, TCL_VOLATILE copies; otherwise a custom
 *   release callback. Ownership transfers only on successful acceptance.
 * @return TCL_OK once the storage contract is accepted; TCL_ERROR before acceptance.
 *
 * Delegates to @ref Rbc_VectorReset and @ref rbc_vector_reset_storage_contract "ResetVectorStorage". Length/capacity count
 * doubles. Use @ref Rbc_VectorChanged instead for an in-place write with unchanged
 * length and ownership. Returns a Tcl status; ownership transfer and release/cache
 * callback handling follow the shared storage-reset implementation.
 */
int Rbc_ResetVector(Rbc_Vector *vecPtr, double *valueArr, Tcl_Size length, Tcl_Size size, Tcl_FreeProc *freeProc) {
    return Rbc_VectorReset((VectorObject *)vecPtr, valueArr, length, size, freeProc);
}

/**
 * @brief Return the current data-protection flag.
 * @ingroup vector_storage_public
 * @param[in] vecPtr Live borrowed vector; NULL is not accepted.
 * @return Current boolean data-protection state.
 *
 * Requires a live vector. Does not protect ownership/lifetime or intercept native
 * pointer writes. No notification, allocation or Tcl evaluation occurs.
 */
int Rbc_VectorIsReadOnly(Rbc_Vector *vecPtr) {
    return ((VectorObject *)vecPtr)->readOnly;
}

/**
 * @brief Set reversible data protection without publishing a value change.
 * @ingroup vector_storage_public
 * @param[in] interp Unused here; see @ref rbc_interp_argument.
 * @param[in,out] vecPtr Live borrowed vector whose protection flag is changed.
 * @param[in] readOnly Zero unlocks; any nonzero value locks data writes.
 * @return TCL_OK; no notification is emitted.
 *
 * Normalizes any nonzero argument to true and returns TCL_OK. The interpreter
 * argument is unused. Requires a live vector; does not alter storage, type,
 * identity, clients or lifetime ownership.
 */
int Rbc_VectorSetReadOnly(Tcl_Interp *interp, Rbc_Vector *vecPtr, int readOnly) {
    (void)interp;
    ((VectorObject *)vecPtr)->readOnly = (readOnly != 0);
    return TCL_OK;
}

/**
 * @brief Reject a protected destination before writing through the C API.
 * @ingroup vector_storage_public
 * @param[in] interp See @ref rbc_interp_argument; receives the protection diagnostic.
 * @param[in] vecPtr Borrowed valid vector record; not an arbitrary-pointer validator.
 * @return TCL_OK if writable; TCL_ERROR for a destroyed or protected record.
 *
 * Returns TCL_OK when writable, or TCL_ERROR with RBC VECTOR READONLY and a
 * name-bearing message. Requires a live record; it is not a destroyed-pointer
 * validator. Native producers must call it before direct writes and recheck after
 * callbacks that could change protection. Does not undo a prior write.
 */
int Rbc_VectorCheckWritable(Tcl_Interp *interp, Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;

    if (VectorAlive(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (vPtr->readOnly) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" is read-only", vPtr->name));
        Tcl_SetErrorCode(interp, "RBC", "VECTOR", "READONLY", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @brief Publish a full change after an in-place native write.
 * @ingroup vector_storage_public
 * @param[in,out] vecPtr Borrowed live vector after a completed writable in-place update.
 *
 * Flushes the mapped cache when configured, then invalidates extrema and follows
 * the notification policy. Does not check writability, replace storage or change
 * length. The caller checks before writing. Retains the record and interpreter,
 * preserves interpreter result state and skips notification if flushing destroys
 * the identity. A same-named replacement is never used as the notification target.
 */
void Rbc_VectorChanged(Rbc_Vector *vecPtr) {
    PublishVectorChange((VectorObject *)vecPtr, TRUE, 0, 0);
}

/**
 * @brief Publish an inclusive changed interval of existing storage.
 * @ingroup vector_storage_public
 * @param[in,out] vecPtr Borrowed live vector after completed in-place writes.
 * @param[in] first First changed zero-based storage index, inclusive.
 * @param[in] last Last changed zero-based storage index, inclusive; invalid intervals force a full update.
 *
 * Indices are zero-based storage positions, independent of external offset.
 * Invalid intervals conservatively become full updates. Storage ownership and
 * length must remain unchanged. Flushes before ranged notification and does not
 * check writability; shares @ref Rbc_VectorChanged lifetime restrictions.
 */
void Rbc_VectorChangedRange(Rbc_Vector *vecPtr, Tcl_Size first, Tcl_Size last) {
    PublishVectorChange((VectorObject *)vecPtr, FALSE, first, last);
}

/**
 * @brief Destroy a vector identity through the private destruction implementation.
 * @ingroup vector_storage_public
 * @param[in] v Live non-NULL identity to destroy; all borrowed views become invalid.
 *
 * Delegates to @ref Rbc_VectorFree, including synchronous DESTROY notification and
 * custom storage release. Requires a live non-NULL pointer; read-only state does
 * not block destruction. All borrowed storage/name pointers and observer IDs
 * become unusable according to the destruction contract.
 */
void Rbc_FreeVector(Rbc_Vector *v) { Rbc_VectorFree((VectorObject *)v); }

/**
 * @brief Borrow the complex storage pointer, or return NULL for a real vector.
 * @ingroup vector_storage_public
 * @param[in] vPtr Live borrowed vector; NULL is not accepted.
 * @return Borrowed complex buffer, or NULL for a real vector or empty storage.
 *
 * Requires a live record. NULL may also denote empty complex storage. Length and
 * capacity count pairs. Reacquire after storage replacement or callbacks; check
 * writability before native writes and publish changes afterward.
 */
Rbc_Complex *Rbc_VectorComplexData(Rbc_Vector *vPtr) {
    if (vPtr->type != RBC_VECTOR_COMPLEX) {
        return NULL;
    }
    return vPtr->data.complex;
}

/**
 * @brief Borrow the real storage pointer, or return NULL for a complex vector.
 * @ingroup vector_storage_public
 * @param[in] vPtr Live borrowed vector; NULL is not accepted.
 * @return Borrowed real buffer, or NULL for a complex vector or empty storage.
 *
 * Requires a live record. NULL may also denote empty real storage. Does not
 * retain the record or buffer. Reacquire after storage replacement or callbacks;
 * check writability before native writes and publish changes afterward.
 */
double *Rbc_VectorData(Rbc_Vector *vPtr) {
    if (vPtr->type != RBC_VECTOR_REAL) {
        return NULL;
    }
    return vPtr->data.real;
}

/**
 * @brief Return the number of used values in a live vector.
 * @ingroup vector_storage_public
 * @param[in] vPtr Live borrowed vector; NULL is not accepted.
 * @return Used value count, including complex pairs as single values.
 *
 * Real and complex vectors both count values, not bytes. Does not refresh
 * extrema, retain storage or evaluate Tcl.
 */
Tcl_Size Rbc_VectorLength(Rbc_Vector *vPtr) { return vPtr->length; }
/**
 * @brief Return allocated capacity in values, not the used length or bytes.
 * @ingroup vector_storage_public
 * @param[in] vPtr Live borrowed vector; NULL is not accepted.
 * @return Allocated capacity in values.
 *
 * Requires a live vector. Real capacity counts doubles; complex capacity counts
 * Rbc_Complex values. Capacity is an implementation/storage contract detail and
 * may change on resize or reset.
 */
Tcl_Size Rbc_VectorSize(Rbc_Vector *vPtr) { return vPtr->size; }
/**
 * @brief Return the current modification counter from a live vector.
 * @ingroup vector_storage_public
 * @param[in] vPtr Live borrowed vector; NULL is not accepted.
 * @return Modification counter; reading it does not reset it.
 *
 * Returns an int counter, not a boolean or pending-notification flag. Internal
 * update scheduling increments it even when ordinary notifications are disabled.
 * Direct unreported pointer writes do not increment it. Does not clear it.
 */
int Rbc_VectorDirty(Rbc_Vector *vPtr) { return vPtr->dirty; }
/**
 * @brief Return the immutable real/complex type of a live vector.
 * @ingroup vector_storage_public
 * @param[in] vPtr Live borrowed vector; NULL is not accepted.
 * @return RBC_VECTOR_REAL or RBC_VECTOR_COMPLEX.
 *
 * Does not inspect contents, convert storage, retain the record or evaluate Tcl.
 */
Rbc_VectorType Rbc_VectorGetType(Rbc_Vector *vPtr) { return vPtr->type; }
