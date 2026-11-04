/**
 * @file rbcVectorInt.h
 * @brief Private vector storage, interpreter registry and update machinery.
 *
 * Never include this header in an external vector consumer. The representation
 * and selected-range scratch fields are private to RBC; installed clients use
 * opaque types and the generated stubs interface.
 */

#ifndef _RBCVECTORINT
#define _RBCVECTORINT

#include <ctype.h>
#include <stdint.h>
#include "rbcInt.h"
#include "rbcChain.h"
#include "rbcVector.h"

#define VECTOR_THREAD_KEY "Rbc Vector Data"
#define VECTOR_MAGIC ((unsigned int) 0x46170277)
#define DEF_ARRAY_SIZE 64
#define BUFFER_SIZE 1024

#define TRACE_ALL (TCL_TRACE_WRITES | TCL_TRACE_READS | TCL_TRACE_UNSETS | TCL_TRACE_RESULT_OBJECT)

/* These defines allow parsing of different types of indices */

/* Recognize "min", "max", and "++end" as valid indices */
#define INDEX_SPECIAL (1 << 0)
/* Also recognize a range of indices separated by a colon */
#define INDEX_COLON (1 << 1)
/* Verify that the specified index or range of indices are within limits */
#define INDEX_CHECK (1 << 2)
#define INDEX_ALL_FLAGS (INDEX_SPECIAL | INDEX_COLON | INDEX_CHECK)
#define SPECIAL_INDEX -2

/* Never notify clients of updates to the vector */
#define NOTIFY_NEVER (1 << 3)
/* Notify clients after each update of the vector is made */
#define NOTIFY_ALWAYS (1 << 4)
/* Notify clients at the next idle point that the vector has been updated. */
#define NOTIFY_WHENIDLE (1 << 5)
/* A do-when-idle notification of the vector's clients is pending. */
#define NOTIFY_PENDING (1 << 6)
#define NOTIFY_UPDATED ((int)RBC_VECTOR_NOTIFY_UPDATE)
#define NOTIFY_DESTROYED ((int)RBC_VECTOR_NOTIFY_DESTROY)
/* The data of the vector has changed.  Update the min and max limits when they are needed */
#define UPDATE_RANGE (1 << 9)

#define VECTOR_CHAR(c) ((isalnum(UCHAR(c))) || (c == '_') || (c == ':') || (c == '@') || (c == '.'))

#define NS_SEARCH_NONE (0)
#define NS_SEARCH_CURRENT (1 << 0)
#define NS_SEARCH_GLOBAL (1 << 1)
#define NS_SEARCH_BOTH (NS_SEARCH_GLOBAL | NS_SEARCH_CURRENT)

extern double rbcNaN;

typedef struct {
    Tcl_HashTable vectorTable;   /* Table of vectors */
    Tcl_HashTable mathProcTable; /* Table of vector math functions */
    Tcl_HashTable indexProcTable;
    Tcl_Interp *interp;
    unsigned int nextId;
} VectorInterpData;

typedef union {
    double *real;
    Rbc_Complex *complex;
    void *raw;
} VectorData;

/*
 * Vector storage starts empty (NULL, length and capacity zero); growth allocates
 * an array, not an inline/static buffer. freeProc controls ownership of the
 * current array. Data pointers can change on growth or reset.
 *
 * The interpreter registry owns named vectors. The client chain observes the
 * same record without copying data or preserving its lifetime. On destruction,
 * callbacks run before storage and remaining client IDs are freed.
 */
struct Rbc_Vector_s {
    Rbc_VectorType type;
    int readOnly; /* Protect values, length and index offset; not vector lifetime. */
    VectorData data;
    Tcl_Size length; /* Current number of values in the array. */
    Tcl_Size size;   /* Maximum number of values that can be stored
                      * in the value array. */
    double min, max; /* Minimum and maximum values in the vector */
    /*
     * Source indices of the cached finite extrema.
     *
     * -1 means that no finite extremum is currently cached.
     */
    Tcl_Size minIndex;
    Tcl_Size maxIndex;
    int dirty;       /* Indicates if the vector has been updated */
    /* The following fields are local to this module  */
    char *name; /* The namespace-qualified name of the vector command.
                 * It points to the hash key allocated for the
                 * entry in the vector hash table; destruction keeps a temporary
                 * copy while callbacks run after removing the registration. */
    VectorInterpData *dataPtr;
    Tcl_Interp *interp;     /* Interpreter associated with the vector */
    Tcl_HashEntry *hashPtr; /* If non-NULL, pointer in a hash table to
                             * track the vectors in use. */
    Tcl_FreeProc *freeProc; /* Address of procedure to call to
                             * release storage for the value
                             * array, Optionally can be one of the
                             * following: TCL_STATIC, TCL_DYNAMIC,
                             * or TCL_VOLATILE. */
    char *arrayName;        /* The namespace-qualified name of the
                             * Tcl array variable
                             * mapped to the vector
                             * (malloc'ed). If NULL, indicates
                             * that the vector isn't mapped to any variable */
    Tcl_Size offset;        /* Offset from zero of the vector's
                             * starting index */
    Tcl_Command cmdToken;   /* Token for vector's Tcl command. */
    Rbc_Chain *chainPtr;    /* List of clients using this vector */
    int destroying;        /* Destruction started; reject recursive deletion and new clients. */
    int notifyFlags;        /* Notification flags. See definitions
                             * below */
    int varFlags;           /* Indicate if the variable is global,
                             * namespace, or local */
    int freeOnUnset;        /* For backward compatibility only: If
                             * non-zero, free the vector when its
                             * variable is unset. */
    int flush;
    Tcl_Size first, last; /* Selected region of vector. This is used
                           * mostly for the math routines */
    /*
     * Coalesced source range modified since the previous client
     * notification.
     *
     * updateAll means that the modified range is unknown or that the
     * complete vector should be considered changed.
     */
    int updateAll;
    int updateRangeValid;
    Tcl_Size updateFirst;
    Tcl_Size updateLast;
};

typedef Rbc_Vector VectorObject;

/*
 * One observer of a vector. This is not a lifetime-preserving reference.
 * update* fields are a notification-local snapshot, restored after nested
 * delivery. Detaching invalidates the token immediately; active snapshots defer
 * reclamation. Rbc_VectorFree detaches remaining IDs after DESTROY delivery.
 */
struct Rbc_VectorIdStruct {
    unsigned int magic;          /* Magic value designating whether this
                                  * really is a vector token or not */
    VectorObject *serverPtr;     /* Borrowed vector; cleared when the ID is detached. */
    Rbc_VectorChangedProc *proc; /* Routine to call when the contents
                                  * of the vector change or the vector
                                  * is deleted. */
    ClientData clientData;       /* Data passed whenever the vector
                                  * change procedure is called. */
    Rbc_ChainLink *linkPtr;      /* Used to quickly remove this entry from
                                  * its server's client chain. */
    /*
     * Snapshot of the range associated with the notification currently
     * being delivered to this client.
     */
    int updateAll;
    int updateRangeValid;
    Tcl_Size updateFirst;
    Tcl_Size updateLast;    
};

typedef struct Rbc_VectorIdStruct VectorClient;

/*
 * Special index callback.
 */
typedef double(Rbc_VectorIndexProc)(Rbc_Vector *vecPtr);

/*
 *    The token types are defined below.  In addition, there is a
 *    table associating a precedence with each operator.  The order
 *    of types is important.  Consult the code before changing it.
 */
enum Tokens {
    VALUE,
    OPEN_PAREN,
    CLOSE_PAREN,
    COMMA,
    END,
    UNKNOWN,
    MULT = 8,
    DIVIDE,
    MOD,
    PLUS,
    MINUS,
    LEFT_SHIFT,
    RIGHT_SHIFT,
    LESS,
    GREATER,
    LEQ,
    GEQ,
    EQUAL,
    NEQ,
    OLD_BIT_AND,
    EXPONENT,
    OLD_BIT_OR,
    OLD_QUESTY,
    OLD_COLON,
    AND,
    OR,
    UNARY_MINUS,
    OLD_UNARY_PLUS,
    NOT,
    OLD_BIT_NOT
};

typedef struct {
    VectorObject *vPtr;
} Value;

/*
 *    The data structure below describes the state of parsing an
 *    expression.  It's passed among the routines in this module.
 */
typedef struct {
    char *expr;        /* The entire right-hand side of the
                        * expression, as originally passed to
                        * Rbc_ExprVector. */
    char *nextPtr;     /* Position of the next character to
                        * be scanned from the expression
                        * string. */
    enum Tokens token; /* Type of the last token to be parsed
                        * from nextPtr.  See below for
                        * definitions.  Corresponds to the
                        * characters just before nextPtr. */
} ParseInfo;

/* Vector Operators Definitions (rbcVector.c) */
/*
 * Internal vector functions.
 */

/**
 * Transform a real/complex source into existing complex or split-real outputs.
 *
 * @param vPtr Live source vector in interp; its full length is transformed.
 * @param interp Owning interpreter, kept alive for the operation.
 * @param objc Argument count already checked by the instance dispatcher.
 * @param objv Instance command arguments, including fft or ifft at index 1.
 * @return TCL_OK with a qualified destination name or a two-name list;
 * TCL_ERROR for validation, allocation or non-finite arithmetic errors before
 * commit, with destination data unchanged.
 *
 * Checks all output types and writability. Copies inputs to owned temporary
 * storage, permitting input/output aliasing. Length must be a nonzero power
 * of two unless explicit padding selects a larger power of two. Forward output
 * is unscaled; inverse input is scaled by 1/N before the butterflies.
 * Installs every output before releasing old storage, flushing caches or
 * notifying clients. Preserves vector identity, type, offset and observers.
 *
 * Preserves destination records and the interpreter across post-install
 * callbacks. Destroyed destinations are skipped; replacement vectors with the
 * same name are not updated. Returned names are snapshots taken before callbacks.
 */
int Rbc_VectorFftOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
/**
 * Fill an existing real vector with FFT bin frequencies.
 *
 * @param vPtr Live, writable real destination, validated by the dispatcher.
 * @param interp Owning interpreter, kept alive for the operation.
 * @param objc Argument count already checked by the instance dispatcher.
 * @param objv Instance command arguments, with positive transform length at index 2.
 * @return TCL_OK with the destination name; TCL_ERROR before commit leaves
 * destination data unchanged.
 *
 * Supports any positive length, not only powers of two. Delta must be finite
 * and positive. Full output uses negative Nyquist for even lengths; one-sided
 * output contains floor(N/2)+1 nonnegative bins. Allocates and validates all
 * values before replacing storage and notifying clients. Does not repeat the
 * dispatcher's real-type or writable checks. The shared commit helper preserves
 * records across callbacks and skips updates after destruction.
 */
int Rbc_VectorFftfreqOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
/**
 * Generate unscaled window coefficients in an existing real vector.
 *
 * @param vPtr Live, writable real destination, validated by the dispatcher.
 * @param interp Owning interpreter, kept alive for the operation.
 * @param objc Argument count already checked by the instance dispatcher.
 * @param objv Instance arguments, with window kind and length at indices 2 and 3.
 * @return TCL_OK with the destination name; TCL_ERROR before commit leaves
 * destination data unchanged.
 *
 * Accepts rectangular, Hann, Hamming, Blackman and Bartlett windows of any
 * positive length. Periodic is the default; symmetric uses N-1 instead of N.
 * A length-one window is always one. Stages all coefficients before installing
 * them and notifying clients. Does not repeat the dispatcher's real-type or
 * writable checks. The shared commit helper preserves records across callbacks
 * and skips updates after destruction.
 */
int Rbc_VectorWindowOp(VectorObject *vPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
/**
 * @brief Discard cached array elements while retaining the current mapping.
 *
 * Preserves the vector and interpreter across variable traces. Reinstalls the
 * end entry and RBC trace only while the same mapping remains live. This void
 * helper does not notify clients, report trace errors, or save the Tcl result.
 */
void Rbc_VectorFlushCache(VectorObject *vPtr);
/**
 * @brief Resolve a vector name and optional range into a borrowed vector.
 * @param[in] interp Diagnostic interpreter, or NULL to suppress lookup diagnostics; see @ref rbc_interp_argument.
 * @param[in] dataPtr Live owning interpreter registry, retained by the caller through index evaluation.
 * @param[in] start NUL-terminated reference buffer, valid throughout the call.
 * @param[out] endPtr Optional unconsumed-suffix output, written only on success; aliases start storage.
 * @param[in] flags Namespace search mask: NS_SEARCH_CURRENT, NS_SEARCH_GLOBAL or NS_SEARCH_BOTH.
 * @return Borrowed vector with committed selection, or NULL on failure.
 * @see @ref rbc_calling_conventions
 *
 * Prefers the longest existing name at a balanced-parenthesis boundary, so
 * parentheses may belong to the name itself. Sets the shared first/last
 * selection; restores it on range or trailing-text errors. With endPtr, returns
 * the unconsumed suffix there; without it, requires complete consumption.
 * Returns NULL on failure. Index expressions may evaluate Tcl; no lifetime
 * reference is acquired for the returned object.
 */
VectorObject *Rbc_VectorParseElement(Tcl_Interp *interp, VectorInterpData *dataPtr, const char *start,
                                     const char **endPtr, int flags);
int Rbc_VectorChangeLength(VectorObject *vPtr, Tcl_Size length);

/**
 * @brief Invalidate extrema and report a full/unknown data change internally.
 * @param[in,out] vPtr Live vector, already modified by the caller.
 *
 * Increments the dirty counter and follows the vector's notification policy.
 * Immediate policy invokes callbacks before return. Does not flush the mapped
 * Tcl array cache or check read-only state; mutation code owns those steps.
 */
void Rbc_VectorUpdateClients(VectorObject *vPtr);

/**
 * @brief Report changed existing storage using inclusive zero-based indices.
 * @param[in,out] vPtr Live vector, already modified in place.
 * @param[in] first First changed storage index, independent of Tcl index offset.
 * @param[in] last Last changed storage index, inclusive.
 *
 * Invalid intervals become full updates. Valid intervals coalesce until
 * delivery; real extrema are maintained incrementally when possible. Does not
 * change length/ownership, flush array caches or enforce read-only state.
 */
void Rbc_VectorUpdateClientsRange(VectorObject *vPtr, Tcl_Size first, Tcl_Size last);

/**
 * @brief Read the interval snapshot during this client's UPDATE callback.
 * @param[in] clientId Live observer token, or NULL.
 * @param[out] firstPtr Optional first inclusive zero-based index.
 * @param[out] lastPtr Optional last inclusive zero-based index.
 * @return TRUE if a range is available; FALSE for a full/unknown update,
 * DESTROY, outside delivery, or a NULL/disconnected token. Outputs are left
 * unchanged on FALSE. Never pass a token freed by vector destruction.
 *
 * Coalescing may include unchanged values between separate modified intervals.
 * This helper is private and has no public stub entry.
 */
int Rbc_VectorGetChangedRange(Rbc_VectorId clientId, Tcl_Size *firstPtr, Tcl_Size *lastPtr);
/**
 * @brief Replace or remove a vector's mapped Tcl array.
 *
 * An empty or NULL name unmaps; parentheses in mapping names are rejected.
 * Unsets the old mapping and target variable, creates an end entry, and installs
 * RBC traces. Preserves the vector/interpreter and checks destruction or
 * reentrant remapping after trace-capable operations. Returns TCL_OK or
 * TCL_ERROR; failure does not restore the previous mapping.
 */
int Rbc_VectorMapVariable(Tcl_Interp *interp, VectorObject *vPtr, const char *name);
VectorObject *Rbc_VectorCreate(VectorInterpData *dataPtr, const char *vecName, const char *cmdName, const char *varName,
                               Rbc_VectorType type, int *newPtr);
/**
 * @brief Translate an external index into a storage index or special callback.
 * @param[in] interp Optional diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Live vector supplying length, offset and owning evaluation interpreter.
 * @param[in] string NUL-terminated index text, valid throughout evaluation.
 * @param[out] indexPtr Required output, written only on success; may be an append or special sentinel.
 * @param[in] flags INDEX_CHECK requests ordinary numeric upper-bound validation.
 * @param[out] procPtrPtr Optional callback output, cleared on entry and set for a registered special name.
 * @return TCL_OK for a recognized index; TCL_ERROR for conversion, bounds or destruction failure.
 * @see @ref rbc_calling_conventions
 *
 * Numeric indices subtract the vector offset with overflow checks. end-based
 * indices are relative to length; ++end denotes the append position.
 * INDEX_CHECK bounds ordinary indices; callers must handle append and special
 * indices separately. An optional procPtrPtr receives a registered special
 * index procedure and indexPtr receives SPECIAL_INDEX. Expression fallback
 * evaluates Tcl in the owning interpreter even with NULL diagnostic interp.
 * Returns TCL_OK or TCL_ERROR. Retains the vector/interpreter during expression
 * evaluation and rejects destruction before using its result.
 */
int Rbc_VectorGetIndex(Tcl_Interp *interp, VectorObject *vPtr, const char *string, Tcl_Size *indexPtr, int flags,
                       Rbc_VectorIndexProc **procPtrPtr);
/**
 * @brief Convert an object to a double, with Tcl expression fallback.
 * @param[in] interp Evaluation and diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] objPtr Input value or expression, kept alive by the caller through evaluation.
 * @param[out] valuePtr Required double output; consume it only after TCL_OK.
 * @return TCL_OK on conversion, otherwise TCL_ERROR with an interpreter diagnostic.
 * @see @ref rbc_calling_conventions
 *
 * Tries Tcl_GetDoubleFromObj first, then clears its error and calls
 * Tcl_ExprDoubleObj. Returns a Tcl status. Expression evaluation can execute
 * commands; callers own lifetime and writable-state checks after conversion.
 */
int Rbc_GetDouble(Tcl_Interp *interp, Tcl_Obj *objPtr, double *valuePtr);
/**
 * @brief Convert a two-item list or a real scalar to a complex value.
 * @param[in] interp Evaluation and diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] objPtr Input pair or scalar, kept alive by the caller through component evaluation.
 * @param[out] valuePtr Required output; its real component may be written before imaginary conversion fails.
 * @return TCL_OK with both components, otherwise TCL_ERROR; the output is not transactional.
 * @see @ref rbc_calling_conventions
 *
 * Each pair component uses Rbc_GetDouble; scalar input receives zero imaginary
 * part. Returns a Tcl status. Conversion may execute Tcl and can modify the
 * real output component before failure of the imaginary component.
 */
int Rbc_GetComplex(Tcl_Interp *interp, Tcl_Obj *objPtr, Rbc_Complex *valuePtr);
/**
 * @brief Create a reference-count-zero two-double Tcl list.
 * @param[in] value Real and imaginary components copied into the result.
 * @return New two-element Tcl list with reference count zero; no vector storage is borrowed.
 * @see @ref rbc_calling_conventions
 *
 * The result owns copies of the real and imaginary values; the caller follows
 * normal Tcl object ownership rules.
 */
Tcl_Obj *Rbc_NewComplexObj(Rbc_Complex value);
/**
 * @brief Create a reference-count-zero Tcl value for one storage index.
 * @param[in] vPtr Live real or complex source with valid storage.
 * @param[in] index Valid zero-based storage index, independent of the external offset and selection.
 * @return New reference-count-zero Tcl double or complex pair containing a copy.
 * @see @ref rbc_calling_conventions
 *
 * Returns a double for real vectors or a two-double list for complex vectors.
 * The caller supplies a live vector and valid zero-based index. Does not check
 * bounds and does not expose or retain vector storage.
 */
Tcl_Obj *Rbc_NewVectorValueObj(VectorObject *vPtr, Tcl_Size index);

void Rbc_VectorFree(VectorObject *vPtr);
/**
 * @brief Set the vector's inclusive first/last selection.
 * @param[in] interp Optional diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Live vector whose shared first/last selection is committed on success.
 * @param[in] string NUL-terminated index or range text, unchanged by the parser.
 * @param[in] flags INDEX_COLON enables ranges; INDEX_CHECK requests numeric bounds validation.
 * @param[out] procPtrPtr Optional special-index callback output, cleared on entry; ranges leave it NULL.
 * @return TCL_OK after committing selection, otherwise TCL_ERROR; callback side effects are not undone.
 * @see @ref rbc_calling_conventions
 *
 * INDEX_COLON enables first:last syntax with omitted endpoints. A single
 * index selects one value. Commits selection only after both endpoints and
 * ordering succeed; returns TCL_ERROR otherwise. Selection is shared mutable
 * object state, not a retained slice. Callers must handle empty vectors and
 * special/append indices before accessing storage. Index conversion may run Tcl.
 */
int Rbc_VectorGetIndexRange(Tcl_Interp *interp, VectorObject *vPtr, const char *string, int flags,
                            Rbc_VectorIndexProc **procPtrPtr);
int Rbc_VectorDuplicate(VectorObject *destPtr, VectorObject *srcPtr);
/**
 * @brief Copy an inclusive storage interval into a new Tcl list.
 * @param[in] vPtr Live real or complex source, kept valid throughout copying.
 * @param[in] first First zero-based storage index; validated by the caller when the interval is nonempty.
 * @param[in] last Inclusive final storage index; values below first produce an empty list.
 * @return New reference-count-zero Tcl list of copied values.
 * @see @ref rbc_calling_conventions
 *
 * Returns a reference-count-zero list, empty when first exceeds last. The
 * caller validates storage bounds and lifetime. Complex entries are pairs;
 * real entries are doubles. Does not change the vector selection.
 */
Tcl_Obj *Rbc_GetValues(VectorObject *vPtr, Tcl_Size first, Tcl_Size last);
void Rbc_ReplicateValue(VectorObject *vPtr, Tcl_Size first, Tcl_Size last, double value);
int Rbc_VectorLookupName(VectorInterpData *dataPtr, const char *vecName, VectorObject **vPtrPtr);
int Rbc_VectorReset(VectorObject *vPtr, double *valueArr, Tcl_Size length, Tcl_Size size, Tcl_FreeProc *freeProc);
void Rbc_VectorUpdateRange(VectorObject *vPtr);
VectorObject *Rbc_VectorNew(VectorInterpData *dataPtr);
VectorInterpData *Rbc_VectorGetInterpData(Tcl_Interp *interp);
/*
 * Internal convenience interface.
 *
 * This isn't part of the public stub API.
 */
int Rbc_CreateVector2(Tcl_Interp *interp, const char *vecName, const char *cmdName, const char *varName,
                      Tcl_Size initialSize, Rbc_VectorType type, Rbc_Vector **vecPtrPtr);
/*
 * Vector instance command.
 */
Tcl_ObjCmdProc2 Rbc_VectorInstanceObjCmd;
/*
 * Legacy real-valued vector math callback signatures.
 *
 * Complex-capable expression functions use private callback types and
 * dispatch wrappers in rbcVecMath.c. Special index callbacks remain
 * real-only for compatibility.
 */
typedef double(ComponentProc)(double value);
typedef int(VectorProc)(VectorObject *vPtr);
typedef double(ScalarProc)(VectorObject *vPtr);
/**
 * @brief Scan the full real vector for its finite minimum.
 * @ingroup expression_numerics
 *
 * Requires real storage and a live record. Ignores first/last selection and
 * nonfinite values, stores the result in the min cache and returns it. Empty
 * or entirely nonfinite input returns NaN. Does not update extrema indices,
 * clear cache-invalid flags, allocate storage or send notifications.
 */
double Rbc_VecMin(Rbc_Vector *vecPtr);
/**
 * @brief Scan the full real vector for its finite maximum.
 * @ingroup expression_numerics
 *
 * Requires real storage and a live record. Ignores first/last selection and
 * nonfinite values, stores the result in the max cache and returns it. Empty
 * or entirely nonfinite input returns NaN. Does not update extrema indices,
 * clear cache-invalid flags, allocate storage or send notifications.
 */
double Rbc_VecMax(Rbc_Vector *vecPtr);
Rbc_Complex Rbc_ComplexFromReal(double value);
int Rbc_ComplexIsZero(Rbc_Complex value);
Rbc_Complex Rbc_ComplexAdd(Rbc_Complex a, Rbc_Complex b);
Rbc_Complex Rbc_ComplexSub(Rbc_Complex a, Rbc_Complex b);
Rbc_Complex Rbc_ComplexMul(Rbc_Complex a, Rbc_Complex b);
Rbc_Complex Rbc_ComplexDiv(Rbc_Complex a, Rbc_Complex b);
Rbc_Complex Rbc_VectorValueAsComplex(VectorObject *vPtr, Tcl_Size index);
/**
 * Evaluate an expression once into an owned, unregistered vector temporary.
 *
 * @param interp Owning interpreter, used for lookup, Tcl evaluation and errors.
 * @param string NUL-terminated expression buffer, kept valid throughout the call.
 * The parser temporarily edits delimiters and restores them; storage must be writable.
 * @param resultPtr Required output pointer, set to NULL before evaluation.
 * @return TCL_OK with an owned real or complex vector, or TCL_ERROR with no
 * returned vector. Release a successful result with Rbc_VectorFree.
 *
 * The temporary has no registered command or mapped array. Final values must
 * be finite. Tcl substitutions and map callbacks can have arbitrary side
 * effects; failure cleans up the temporary but does not undo those effects.
 * The caller keeps the interpreter and its vector subsystem alive throughout
 * evaluation and until the returned temporary has been released.
 */
int Rbc_VectorEvaluate(Tcl_Interp *interp, char *string, VectorObject **resultPtr);
/**
 * Evaluate an expression and copy it to an existing vector or return a Tcl list.
 *
 * @param interp Owning interpreter, kept alive by the caller during evaluation.
 * @param string NUL-terminated writable expression buffer, valid for the call.
 * @param vecPtr Existing destination in this interpreter, or NULL for list output.
 * @return TCL_OK on success, otherwise TCL_ERROR with a diagnostic. With NULL
 * destination the interpreter result is a list of numbers or complex pairs.
 * Destination form does not promise a particular successful Tcl result.
 *
 * Evaluates into a detached temporary before writing the destination. Checks
 * writability before evaluation and again before copying, and observes
 * destination destruction during Tcl substitutions or map callbacks. Copies
 * matching types or promotes real values into an existing complex destination;
 * a complex result cannot be stored in a real destination. Named vector type
 * is unchanged. Callback side effects are not rolled back on failure.
 *
 * Does not flush a mapped-array cache or notify destination clients. The
 * instance expr operation performs those steps after successful return.
 * The caller must keep the interpreter and vector subsystem alive; the
 * destruction observer protects destination identity, not interpreter lifetime.
 */
int Rbc_ExprVector(Tcl_Interp *interp, char *string, Rbc_Vector *vecPtr);
void Rbc_VectorInstallMathFunctions(Tcl_HashTable *tablePtr);
void Rbc_VectorInstallSpecialIndices(Tcl_HashTable *tablePtr);
/**
 * @brief Allocate a lexicographic permutation for real vector keys.
 * @param[in] vPtrPtr Array of live real key vectors in primary-to-secondary comparison order.
 * @param[in] nVectors Positive array length; all keys must cover the first vector's selected storage indices.
 * @return Owned Tcl-allocated index array, released with ckfree, or NULL on failure.
 * @see Rbc_VectorGetIndexRange
 * @see @ref rbc_calling_conventions
 *
 * Requires nVectors greater than zero, live vectors, and valid storage for
 * every index in the first vector's inclusive selection. Returns a Tcl-allocated
 * index array for the caller to free, including an allocation for an empty
 * successful sort; NULL reports failure in the first vector's interpreter.
 * Does not reorder data. Heap sorting is not stable; comparisons do not define
 * a total ordering for NaNs. Complex keys are rejected.
 */
Tcl_Size *Rbc_VectorSortIndex(VectorObject **vPtrPtr, Tcl_Size nVectors);

#endif /* _RBCVECTORINT */
