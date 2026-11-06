/**
 * @file rbcVecMath.c
 * @brief Vector-expression parser, temporary storage and math-function adapters.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup expression_engine Expression engine internals
 * Private evaluator helpers. Registered vector types remain immutable.
 */
/** @defgroup expression_numerics Expression numerical kernels
 * Private reductions and scalar kernels used by the expression adapters.
 */
#include "rbcVectorInt.h"
#include <stdint.h>
#include <errno.h>
#include <math.h>

typedef int(GenericMathProc)(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr);

typedef Rbc_Complex(ComplexComponentProc)(Rbc_Complex value);
typedef double(ComplexRealProc)(Rbc_Complex value);
typedef Rbc_Complex(ComplexScalarProc)(VectorObject *vPtr);

/** @brief Paired real/complex expression implementations selected by input type. */
typedef struct {
    ComponentProc *realProc; /**< Real-input implementation. */
    ComplexComponentProc *complexProc; /**< Complex-input implementation. */
} ComplexComponentFunction;

/** @brief Paired real/complex expression implementations selected by input type. */
typedef struct {
    ComponentProc *realProc; /**< Real-input implementation. */
    ComplexRealProc *complexProc; /**< Complex-input implementation. */
} ComplexRealFunction;

/** @brief Paired real/complex expression implementations selected by input type. */
typedef struct {
    Rbc_VectorIndexProc *realProc; /**< Real-input implementation. */
    ComplexScalarProc *complexProc; /**< Complex-input implementation. */
} ComplexScalarFunction;

typedef double(ComplexRealScalarProc)(VectorObject *vPtr);

/** @brief Paired real/complex expression implementations selected by input type. */
typedef struct {
    Rbc_VectorIndexProc *realProc; /**< Real-input implementation. */
    ComplexRealScalarProc *complexProc; /**< Complex-input implementation. */
} ComplexRealScalarFunction;

/** @brief Static expression-function registration entry. */
typedef struct {
    char *name; /**< Built-in name; NULL terminates the descriptor array. */
    GenericMathProc *proc; /**< Procedure that implements this math function. */
    ClientData clientData; /**< Argument to pass when invoking the function. */
} MathFunction;


#ifdef DBL_MAX
#define IS_INF(v) (((v) > DBL_MAX) || ((v) < -DBL_MAX))
#else
#define IS_INF(v) 0
#endif

static int precTable[] = {
    0,  0,  0,  0, 0, 0, 0, 0, 12, 12, 12, /* MULT, DIVIDE, MOD */
    11, 11,                                /* PLUS, MINUS */
    10, 10,                                /* LEFT_SHIFT, RIGHT_SHIFT */
    9,  9,  9,  9,                         /* LESS, GREATER, LEQ, GEQ */
    8,  8,                                 /* EQUAL, NEQ */
    7,                                     /* OLD_BIT_AND */
    13,                                    /* EXPONENTIATION */
    5,                                     /* OLD_BIT_OR */
    4,                                     /* AND */
    3,                                     /* OR */
    2,                                     /* OLD_QUESTY */
    1,                                     /* OLD_COLON */
    14, 14, 14, 14                         /* UNARY_MINUS, OLD_UNARY_PLUS, NOT, OLD_BIT_NOT */
};

static void InstallIndexProc(Tcl_HashTable *tablePtr, char *string, Rbc_VectorIndexProc *procPtr);
static Tcl_Size First(VectorObject *vPtr);
static Tcl_Size Next(VectorObject *vPtr, Tcl_Size current);
static double Identity(double value);
static double Zero(double value);
static double Arg(double value);
static double ComplexAbs(Rbc_Complex value);
static double ComplexArg(Rbc_Complex value);
static double ComplexReal(Rbc_Complex value);
static double ComplexImag(Rbc_Complex value);
static Rbc_Complex ComplexConj(Rbc_Complex value);
static int ComplexComponentFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr);
static int ComplexRealFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr);
static Rbc_Complex ComplexSqrt(Rbc_Complex value);
static Rbc_Complex ComplexExp(Rbc_Complex value);
static Rbc_Complex ComplexLog(Rbc_Complex value);
static Rbc_Complex ComplexLog10(Rbc_Complex value);
static Rbc_Complex ComplexSin(Rbc_Complex value);
static Rbc_Complex ComplexCos(Rbc_Complex value);
static Rbc_Complex ComplexSinh(Rbc_Complex value);
static Rbc_Complex ComplexCosh(Rbc_Complex value);
static double LogHypot(double x, double y);
static double RealAcosh(double value);
static Rbc_Complex ComplexTan(Rbc_Complex value);
static Rbc_Complex ComplexTanh(Rbc_Complex value);
static Rbc_Complex ComplexAsin(Rbc_Complex value);
static Rbc_Complex ComplexAcos(Rbc_Complex value);
static Rbc_Complex ComplexAtan(Rbc_Complex value);
static int ComplexPower(Tcl_Interp *interp, Rbc_Complex base, Rbc_Complex exponent, Rbc_Complex *resultPtr);
static int ComplexValueIsFinite(Rbc_Complex value);
static Rbc_Complex ComplexSum(VectorObject *vPtr);
static Rbc_Complex ComplexMean(VectorObject *vPtr);
static Rbc_Complex ComplexProduct(VectorObject *vPtr);
static int ComplexScalarFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr);
static double ComplexLength(VectorObject *vPtr);
static int ComplexRealScalarFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr);
static int ApplyComplexBooleanOperator(Tcl_Interp *interp, int operator, VectorObject * vPtr, VectorObject *v2Ptr);
static int ApplyComplexNotOperator(Tcl_Interp *interp, VectorObject *vPtr);
static Rbc_Complex ComplexCeil(Rbc_Complex value);
static Rbc_Complex ComplexFloor(Rbc_Complex value);
static Rbc_Complex ComplexRound(Rbc_Complex value);
static double Random(double value);
static Rbc_Complex ComplexRandom(Rbc_Complex value);
static double Mean(Rbc_Vector *vecPtr);
static double Sum(Rbc_Vector *vecPtr);
static double Product(Rbc_Vector *vecPtr);
static double Fabs(double value);
static double AvgDeviation(Rbc_Vector *vecPtr);
static double Kurtosis(Rbc_Vector *vecPtr);
static double Length(Rbc_Vector *vecPtr);
static double Median(Rbc_Vector *vecPtr);
static int Norm(Rbc_Vector *vecPtr);
static double Nonzeros(Rbc_Vector *vecPtr);
static double ComplexNonzeros(VectorObject *vPtr);
static double Q1(Rbc_Vector *vecPtr);
static double Q3(Rbc_Vector *vecPtr);
static double Round(double value);
static double StdDeviation(Rbc_Vector *vecPtr);
static double Skew(Rbc_Vector *vecPtr);
static double Skewness(Rbc_Vector *vecPtr);
static int Sort(VectorObject *vPtr);
static double Sum(Rbc_Vector *vecPtr);
static double Variance(Rbc_Vector *vecPtr);
static double ComplexAvgDeviation(VectorObject *vPtr);
static double ComplexVariance(VectorObject *vPtr);
static double ComplexStdDeviation(VectorObject *vPtr);
static int EvaluateExpression(Tcl_Interp *interp, char *string, Value *valuePtr);
static int NextValue(Tcl_Interp *interp, ParseInfo *parsePtr, int prec, Value *valuePtr);
static void MathError(Tcl_Interp *interp, double value);
static int NextToken(Tcl_Interp *interp, ParseInfo *parsePtr, Value *valuePtr);
static double Fmod(double x, double y);
static int ParseString(Tcl_Interp *interp, const char *string, Value *valuePtr);
static int ParseMathFunction(Tcl_Interp *interp, char *start, ParseInfo *parsePtr, Value *valuePtr);
static int ComponentFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr);
static int ScalarFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr);
static int VectorFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr);
static int ParseSubstitutionResult(Tcl_Interp *interp, Value *valuePtr);
static int ParseBracketValue(Tcl_Interp *interp, const char *string, const char **termPtr, Value *valuePtr);
static int ParseQuotedValue(Tcl_Interp *interp, const char *string, const char **termPtr, Value *valuePtr);
static int ParseBracedValue(Tcl_Interp *interp, const char *string, const char **termPtr, Value *valuePtr);

static ComplexRealFunction absFunction = {Fabs, ComplexAbs};
static ComplexRealFunction argFunction = {Arg, ComplexArg};
static ComplexRealFunction realFunction = {Identity, ComplexReal};
static ComplexRealFunction imagFunction = {Zero, ComplexImag};
static ComplexComponentFunction conjFunction = {Identity, ComplexConj};
static ComplexComponentFunction sqrtFunction = {sqrt, ComplexSqrt};
static ComplexComponentFunction expFunction = {exp, ComplexExp};
static ComplexComponentFunction logFunction = {log, ComplexLog};
static ComplexComponentFunction log10Function = {log10, ComplexLog10};
static ComplexComponentFunction sinFunction = {sin, ComplexSin};
static ComplexComponentFunction cosFunction = {cos, ComplexCos};
static ComplexComponentFunction sinhFunction = {sinh, ComplexSinh};
static ComplexComponentFunction coshFunction = {cosh, ComplexCosh};
static ComplexComponentFunction tanFunction = {tan, ComplexTan};
static ComplexComponentFunction tanhFunction = {tanh, ComplexTanh};
static ComplexComponentFunction asinFunction = {asin, ComplexAsin};
static ComplexComponentFunction acosFunction = {acos, ComplexAcos};
static ComplexComponentFunction atanFunction = {atan, ComplexAtan};
static ComplexScalarFunction sumFunction = {Sum, ComplexSum};
static ComplexScalarFunction meanFunction = {Mean, ComplexMean};
static ComplexScalarFunction productFunction = {Product, ComplexProduct};
static ComplexRealScalarFunction lengthFunction = {Length, ComplexLength};
static ComplexRealScalarFunction nonzerosFunction = {Nonzeros, ComplexNonzeros};
static ComplexRealScalarFunction avgDeviationFunction = {AvgDeviation, ComplexAvgDeviation};
static ComplexRealScalarFunction varianceFunction = {Variance, ComplexVariance};
static ComplexRealScalarFunction stdDeviationFunction = {StdDeviation, ComplexStdDeviation};
static ComplexComponentFunction randomFunction = {Random, ComplexRandom};
static ComplexComponentFunction ceilFunction = {ceil, ComplexCeil};
static ComplexComponentFunction floorFunction = {floor, ComplexFloor};
static ComplexComponentFunction roundFunction = {Round, ComplexRound};

static MathFunction mathFunctions[] = {
    {"abs", (GenericMathProc *)ComplexRealFunc, (ClientData)&absFunction},
    {"arg", (GenericMathProc *)ComplexRealFunc, (ClientData)&argFunction},
    {"conj", (GenericMathProc *)ComplexComponentFunc, (ClientData)&conjFunction},
    {"imag", (GenericMathProc *)ComplexRealFunc, (ClientData)&imagFunction},
    {"real", (GenericMathProc *)ComplexRealFunc, (ClientData)&realFunction},
    {"acos", (GenericMathProc *)ComplexComponentFunc, (ClientData)&acosFunction},
    {"asin", (GenericMathProc *)ComplexComponentFunc, (ClientData)&asinFunction},
    {"atan", (GenericMathProc *)ComplexComponentFunc, (ClientData)&atanFunction},
    {"adev", (GenericMathProc *)ComplexRealScalarFunc, (ClientData)&avgDeviationFunction},
    {"ceil", (GenericMathProc *)ComplexComponentFunc, (ClientData)&ceilFunction},
    {"cos", (GenericMathProc *)ComplexComponentFunc, (ClientData)&cosFunction},
    {"cosh", (GenericMathProc *)ComplexComponentFunc, (ClientData)&coshFunction},
    {"exp", (GenericMathProc *)ComplexComponentFunc, (ClientData)&expFunction},
    {"floor", (GenericMathProc *)ComplexComponentFunc, (ClientData)&floorFunction},
    {"kurtosis", (GenericMathProc *)ScalarFunc, (ClientData)Kurtosis},
    {"length", (GenericMathProc *)ComplexRealScalarFunc, (ClientData)&lengthFunction},
    {"log", (GenericMathProc *)ComplexComponentFunc, (ClientData)&logFunction},
    {"log10", (GenericMathProc *)ComplexComponentFunc, (ClientData)&log10Function},
    {"max", (GenericMathProc *)ScalarFunc, (ClientData)Rbc_VecMax},
    {"mean", (GenericMathProc *)ComplexScalarFunc, (ClientData)&meanFunction},
    {"median", (GenericMathProc *)ScalarFunc, (ClientData)Median},
    {"min", (GenericMathProc *)ScalarFunc, (ClientData)Rbc_VecMin},
    {"norm", (GenericMathProc *)VectorFunc, (ClientData)Norm},
    {"nz", (GenericMathProc *)ComplexRealScalarFunc, (ClientData)&nonzerosFunction},
    {"q1", (GenericMathProc *)ScalarFunc, (ClientData)Q1},
    {"q3", (GenericMathProc *)ScalarFunc, (ClientData)Q3},
    {"prod", (GenericMathProc *)ComplexScalarFunc, (ClientData)&productFunction},
    {"random", (GenericMathProc *)ComplexComponentFunc, (ClientData)&randomFunction},
    {"round", (GenericMathProc *)ComplexComponentFunc, (ClientData)&roundFunction},
    {"sdev", (GenericMathProc *)ComplexRealScalarFunc, (ClientData)&stdDeviationFunction},
    {"sin", (GenericMathProc *)ComplexComponentFunc, (ClientData)&sinFunction},
    {"sinh", (GenericMathProc *)ComplexComponentFunc, (ClientData)&sinhFunction},
    {"skew", (GenericMathProc *)ScalarFunc, (ClientData)Skew},
    {"skewness", (GenericMathProc *)ScalarFunc, (ClientData)Skewness},
    {"sort", (GenericMathProc *)VectorFunc, (ClientData)Sort},
    {"sqrt", (GenericMathProc *)ComplexComponentFunc, (ClientData)&sqrtFunction},
    {"sum", (GenericMathProc *)ComplexScalarFunc, (ClientData)&sumFunction},
    {"tan", (GenericMathProc *)ComplexComponentFunc, (ClientData)&tanFunction},
    {"tanh", (GenericMathProc *)ComplexComponentFunc, (ClientData)&tanhFunction},
    {"var", (GenericMathProc *)ComplexRealScalarFunc, (ClientData)&varianceFunction},
    {
        (char *)NULL,
    },
};

/**
 * @brief Check a temporary-array allocation size.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] count Nonnegative logical element count; negative values are rejected.
 * @param[in] elementSize Nonzero element width in bytes, guaranteed by the caller.
 * @param[out] byteCountPtr Required byte-count output, unchanged on failure.
 * @return TCL_OK with the product, otherwise TCL_ERROR; no allocation occurs.
 *
 * @ingroup expression_engine
 *
 * Requires nonzero elementSize. Rejects negative count and size_t multiplication overflow with a Tcl
 * error; writes byteCountPtr only on success. Does not allocate storage.
 */
static int GetArrayByteCount(Tcl_Interp *interp, Tcl_Size count, size_t elementSize, size_t *byteCountPtr) {
    if (count < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("array size cannot be negative", -1));
        return TCL_ERROR;
    }
    if ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / elementSize)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("array size is too large", -1));
        return TCL_ERROR;
    }
    *byteCountPtr = (size_t)count * elementSize;
    return TCL_OK;
}

/**
 * @brief Check the byte count for real expression storage.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] count Logical double count, subject to nonnegative/overflow validation.
 * @param[out] byteCountPtr Required output, unchanged on failure.
 * @return TCL_OK with the byte count, otherwise TCL_ERROR.
 *
 * @ingroup expression_engine
 *
 * Delegates to GetArrayByteCount with sizeof(double); output remains untouched on validation failure.
 */
static int GetDoubleArrayByteCount(Tcl_Interp *interp, Tcl_Size count, size_t *byteCountPtr) {
    return GetArrayByteCount(interp, count, sizeof(double), byteCountPtr);
}

/**
 * @brief Convert a scalar shift into a bounded rotation offset.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] scalar Requested rotation count; nonpositive values are a no-op.
 * @param[in] length Nonnegative used length supplied by the caller; lengths at most one bypass finite validation.
 * @param[out] offsetPtr Required offset output, initialized zero before validation.
 * @return TCL_OK with a bounded offset, or TCL_ERROR for a nonfinite count when length exceeds one.
 *
 * @ingroup expression_engine
 *
 * Initializes the output to zero. Length zero or one returns before finite validation. Otherwise
 * rejects nonfinite counts, leaves nonpositive counts as a no-op for compatibility, reduces positive
 * counts modulo length and truncates the remainder to Tcl_Size.
 */
static int GetRotationOffset(Tcl_Interp *interp, double scalar, Tcl_Size length, Tcl_Size *offsetPtr) {
    double remainder;

    *offsetPtr = 0;
    if (length <= 1) {
        return TCL_OK;
    }
    if (!FINITE(scalar)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("shift count must be finite", -1));
        return TCL_ERROR;
    }
    /*
     * Preserve the old behavior where negative shift counts did
     * nothing.
     */
    if (scalar <= 0.0) {
        return TCL_OK;
    }
    remainder = fmod(scalar, (double)length);
    if (remainder > 0.0) {
        *offsetPtr = (Tcl_Size)remainder;
    }
    return TCL_OK;
}

/**
 * @brief Rotate real or complex temporary storage in place.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @param[in] operator LEFT_SHIFT or RIGHT_SHIFT token, guaranteed by the caller.
 * @param[in] scalar Requested rotation count passed through GetRotationOffset.
 * @return TCL_OK after rotation or a no-op, otherwise TCL_ERROR before storage mutation.
 *
 * @ingroup expression_engine
 *
 * Requires LEFT_SHIFT or RIGHT_SHIFT and an evaluator-owned vector. Allocates a hold buffer for the
 * wrapped portion, then uses memmove for overlap. Validation/allocation errors precede data mutation;
 * successful rotation leaves type, length and selection unchanged and sends no notifications.
 */
static int RotateExpressionVector(Tcl_Interp *interp, VectorObject *vPtr, int operator, double scalar) {
    unsigned char *data;
    unsigned char *hold;
    Tcl_Size offset;
    Tcl_Size remaining;
    size_t elementSize;
    size_t holdBytes;
    size_t remainingBytes;

    assert((operator== LEFT_SHIFT) || (operator== RIGHT_SHIFT));
    if (GetRotationOffset(interp, scalar, vPtr->length, &offset) != TCL_OK) {
        return TCL_ERROR;
    }
    if (offset == 0) {
        return TCL_OK;
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
        return TCL_ERROR;
    }
    remaining = vPtr->length - offset;
    if (GetArrayByteCount(interp, offset, elementSize, &holdBytes) != TCL_OK) {
        return TCL_ERROR;
    }
    if (GetArrayByteCount(interp, remaining, elementSize, &remainingBytes) != TCL_OK) {
        return TCL_ERROR;
    }
    hold = Tcl_AttemptAlloc(holdBytes);
    if (hold == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate vector rotation buffer", -1));
        return TCL_ERROR;
    }
    data = (unsigned char *)vPtr->data.raw;
    if (operator== LEFT_SHIFT) {
        memcpy(hold, data, holdBytes);
        memmove(data, data + holdBytes, remainingBytes);
        memcpy(data + remainingBytes, hold, holdBytes);
    } else {
        memcpy(hold, data + remainingBytes, holdBytes);
        memmove(data + holdBytes, data, remainingBytes);
        memcpy(data, hold, holdBytes);
    }
    ckfree(hold);
    return TCL_OK;
}

/**
 * @brief Release a temporary allocation according to its current policy.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * No metadata is cleared; caller must not use the released pointer afterward.
 *
 * @ingroup expression_engine
 *
 * Static storage is borrowed, dynamic storage uses ckfree, and a custom policy is invoked directly.
 * Does not clear metadata or retain the record across custom callbacks. Evaluator temporaries normally
 * own dynamic storage; this is not the guarded public reset path.
 */
static void FreeExpressionData(VectorObject *vPtr) {
    if ((vPtr->data.raw != NULL) && (vPtr->freeProc != TCL_STATIC)) {
        if (vPtr->freeProc == TCL_DYNAMIC) {
            ckfree(vPtr->data.raw);
        } else {
            vPtr->freeProc(vPtr->data.raw);
        }
    }
}

/**
 * @brief Release and clear an expression temporary's storage metadata.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * Caller guarantees that a custom storage release cannot destroy or reenter this temporary.
 *
 * @ingroup expression_engine
 *
 * Resets data, length, capacity, selection and freeProc after FreeExpressionData. Leaves type and
 * registration fields unchanged. Requires exclusive temporary ownership and no destructive release
 * reentry.
 */
static void ReleaseExpressionStorage(VectorObject *vPtr) {
    FreeExpressionData(vPtr);
    vPtr->data.raw = NULL;
    vPtr->length = 0;
    vPtr->size = 0;
    vPtr->first = 0;
    vPtr->last = -1;
    vPtr->freeProc = TCL_STATIC;
}

/**
 * @brief Discard storage when changing an unregistered temporary's type.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @param[in] type Desired real or complex type; a change discards the existing values.
 *
 * @ingroup expression_engine
 *
 * Same type is a no-op. A change asserts no registry entry, command or mapped array, releases old
 * storage, then sets type. Not valid for changing a named vector's immutable type.
 */
static void SetExpressionVectorType(VectorObject *vPtr, Rbc_VectorType type) {
    if (vPtr->type == type) {
        return;
    }
    /*
     * Expression vectors are unnamed temporaries only.
     */
    assert(vPtr->hashPtr == NULL);
    assert(vPtr->cmdToken == 0);
    assert(vPtr->arrayName == NULL);
    ReleaseExpressionStorage(vPtr);
    vPtr->type = type;
}

/**
 * @brief Copy a selected source into an evaluator temporary of matching type.
 * @param[in,out] destPtr Detached destination temporary; may be cleared or retyped even on failure.
 * @param[in] srcPtr Live source with a valid inclusive selection, kept alive through synchronous copying.
 * @return TCL_OK after typed duplication, otherwise TCL_ERROR; no named output is published.
 *
 * @ingroup expression_engine
 *
 * Changes the destination temporary's type before private duplication. The caller retains the source
 * for the synchronous copy. Failure can leave the temporary cleared or retyped; no named output is
 * committed here.
 */
static int CopyExpressionVector(VectorObject *destPtr, VectorObject *srcPtr) {
    SetExpressionVectorType(destPtr, srcPtr->type);
    return Rbc_VectorDuplicate(destPtr, srcPtr);
}

/**
 * @brief Promote a real temporary while preserving length and capacity.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK after promotion or for already-complex input; TCL_ERROR on size/allocation failure.
 *
 * @ingroup expression_engine
 *
 * Allocates complex capacity, copies used real values with zero imaginary parts and then releases old
 * storage. Already-complex input is unchanged. Size/allocation failures leave old storage intact;
 * requires exclusive temporary ownership across release.
 */
static int PromoteExpressionVectorToComplex(VectorObject *vPtr) {
    Rbc_Complex *newArr;
    size_t byteCount;
    Tcl_Size i;

    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        return TCL_OK;
    }
    assert(vPtr->type == RBC_VECTOR_REAL);
    if ((Tcl_WideUInt)vPtr->size > (Tcl_WideUInt)(SIZE_MAX / sizeof(Rbc_Complex))) {
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("vector size is too large", -1));
        return TCL_ERROR;
    }
    byteCount = (size_t)vPtr->size * sizeof(Rbc_Complex);
    newArr = NULL;
    if (byteCount > 0) {
        newArr = Tcl_AttemptAlloc(byteCount);
        if (newArr == NULL) {
            Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate complex expression vector", -1));
            return TCL_ERROR;
        }
    }
    for (i = 0; i < vPtr->length; i++) {
        newArr[i].real = vPtr->data.real[i];
        newArr[i].imag = 0.0;
    }
    FreeExpressionData(vPtr);
    vPtr->data.complex = newArr;
    vPtr->type = RBC_VECTOR_COMPLEX;
    vPtr->freeProc = (newArr == NULL) ? TCL_STATIC : TCL_DYNAMIC;
    return TCL_OK;
}

/**
 * @brief Replace an expression temporary with one real value.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @param[in] value Double copied into the single-element result; finiteness is not checked.
 * @return TCL_OK after replacement, otherwise TCL_ERROR; earlier type changes are not rolled back.
 *
 * @ingroup expression_engine
 *
 * Sets real type, resizes to one and stores the value. Returns Tcl status without checking finiteness;
 * evaluation or the calling adapter owns that check.
 */
static int SetExpressionRealScalar(VectorObject *vPtr, double value) {
    SetExpressionVectorType(vPtr, RBC_VECTOR_REAL);
    if (Rbc_VectorChangeLength(vPtr, 1) != TCL_OK) {
        return TCL_ERROR;
    }
    vPtr->data.real[0] = value;
    return TCL_OK;
}

/**
 * @brief Replace an expression temporary with one complex value.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @param[in] value Complex pair copied into the single-element result; finiteness is not checked.
 * @return TCL_OK after replacement, otherwise TCL_ERROR; earlier type changes are not rolled back.
 *
 * @ingroup expression_engine
 *
 * Sets complex type, resizes to one and stores the pair. Returns Tcl status; does not publish a named-
 * vector change or validate component finiteness.
 */
static int SetExpressionComplexScalar(VectorObject *vPtr, Rbc_Complex value) {
    SetExpressionVectorType(vPtr, RBC_VECTOR_COMPLEX);
    if (Rbc_VectorChangeLength(vPtr, 1) != TCL_OK) {
        return TCL_ERROR;
    }
    vPtr->data.complex[0] = value;
    return TCL_OK;
}

/**
 * @brief Compute one supported complex arithmetic operation.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] operator PLUS, MINUS, MULT, DIVIDE or EXPONENT token; others report an error.
 * @param[in] a Left scalar operand, copied by value.
 * @param[in] b Right scalar operand, copied by value.
 * @param[out] resultPtr Required result output; consume only on TCL_OK, which does not guarantee finiteness.
 * @return TCL_OK after arithmetic, otherwise TCL_ERROR for an unsupported operator or domain failure.
 *
 * @ingroup expression_engine
 *
 * Handles addition, subtraction, multiplication, division and power. Rejects zero divisor and
 * unsupported operators; power delegates domain checks. Writes the output on success and returns Tcl
 * status. The enclosing evaluator checks final finiteness.
 */
static int ApplyComplexBinaryValue(Tcl_Interp *interp, int operator, Rbc_Complex a, Rbc_Complex b,
                                   Rbc_Complex *resultPtr) {
    switch (operator) {
    case PLUS:
        *resultPtr = Rbc_ComplexAdd(a, b);
        return TCL_OK;
    case MINUS:
        *resultPtr = Rbc_ComplexSub(a, b);
        return TCL_OK;
    case MULT:
        *resultPtr = Rbc_ComplexMul(a, b);
        return TCL_OK;
    case DIVIDE:
        if (Rbc_ComplexIsZero(b)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("divide by zero", -1));
            return TCL_ERROR;
        }
        *resultPtr = Rbc_ComplexDiv(a, b);
        return TCL_OK;
    case EXPONENT:
        return ComplexPower(interp, a, b, resultPtr);        
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("operator is not supported for complex vectors", -1));
    return TCL_ERROR;
}

/**
 * @brief Replace a complex temporary with real boolean negation values.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK with real boolean values, otherwise TCL_ERROR before replacing storage. Input must be complex.
 *
 * @ingroup expression_engine
 *
 * Allocates a separate real array and computes exact complex-zero tests before releasing the input.
 * Sets capacity equal to length and resets selection to the complete result. Validation/allocation
 * failure leaves the input untouched.
 */
static int ApplyComplexNotOperator(Tcl_Interp *interp, VectorObject *vPtr) {
    double *newArr;
    size_t byteCount;
    Tcl_Size length;
    Tcl_Size i;

    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    length = vPtr->length;
    if (GetDoubleArrayByteCount(interp, length, &byteCount) != TCL_OK) {
        return TCL_ERROR;
    }
    newArr = NULL;
    if (byteCount > 0) {
        newArr = Tcl_AttemptAlloc(byteCount);
        if (newArr == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate real expression vector", -1));
            return TCL_ERROR;
        }
    }
    for (i = 0; i < length; i++) {
        newArr[i] = (double)Rbc_ComplexIsZero(vPtr->data.complex[i]);
    }
    FreeExpressionData(vPtr);
    vPtr->data.real = newArr;
    vPtr->type = RBC_VECTOR_REAL;
    vPtr->length = length;
    vPtr->size = length;
    vPtr->first = 0;
    vPtr->last = length - 1;
    vPtr->freeProc = (newArr == NULL) ? TCL_STATIC : TCL_DYNAMIC;
    return TCL_OK;
}

/**
 * @brief Compute broadcast complex equality or logical results as real values.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] operator EQUAL, NEQ, AND or OR.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @param[in] v2Ptr Live right operand temporary; scalar broadcasting and shape rules depend on the operator.
 * @return TCL_OK after applying the operator, otherwise TCL_ERROR; the left temporary may be partially changed.
 *
 * @ingroup expression_engine
 *
 * Requires EQUAL, NEQ, AND or OR. Length-one operands broadcast; other lengths must match. Computes
 * into a new real array before replacing the left temporary. Equality compares both components
 * exactly; truth means either component is nonzero. Both operands have already been evaluated, so this
 * is not short-circuit evaluation.
 */
static int ApplyComplexBooleanOperator(Tcl_Interp *interp, int operator, VectorObject * vPtr, VectorObject *v2Ptr) {
    double *newArr;
    size_t byteCount;
    Tcl_Size length;
    Tcl_Size i;

    assert((operator== EQUAL) || (operator== NEQ) || (operator== AND) || (operator== OR));
    /*
     * Preserve the existing expression broadcasting rules.
     */
    if (v2Ptr->length == 1) {
        length = vPtr->length;
    } else if (vPtr->length == 1) {
        length = v2Ptr->length;
    } else {
        if (vPtr->length != v2Ptr->length) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("vectors are different lengths", -1));
            return TCL_ERROR;
        }
        length = vPtr->length;
    }
    if (GetDoubleArrayByteCount(interp, length, &byteCount) != TCL_OK) {
        return TCL_ERROR;
    }
    newArr = NULL;
    if (byteCount > 0) {
        newArr = Tcl_AttemptAlloc(byteCount);
        if (newArr == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate real expression vector", -1));
            return TCL_ERROR;
        }
    }
    /*
     * Compute everything before replacing vPtr's storage,
     * because vPtr itself is the left operand.
     */
    for (i = 0; i < length; i++) {
        Rbc_Complex a;
        Rbc_Complex b;
        int aTrue;
        int bTrue;
        int result;

        if (vPtr->length == 1) {
            a = Rbc_VectorValueAsComplex(vPtr, 0);
        } else {
            a = Rbc_VectorValueAsComplex(vPtr, i);
        }
        if (v2Ptr->length == 1) {
            b = Rbc_VectorValueAsComplex(v2Ptr, 0);
        } else {
            b = Rbc_VectorValueAsComplex(v2Ptr, i);
        }
        switch (operator) {
        case EQUAL:
            result = (a.real == b.real) && (a.imag == b.imag);
            break;
        case NEQ:
            result = (a.real != b.real) || (a.imag != b.imag);
            break;
        case AND:
            aTrue = !Rbc_ComplexIsZero(a);
            bTrue = !Rbc_ComplexIsZero(b);
            result = aTrue && bTrue;
            break;
        case OR:
            aTrue = !Rbc_ComplexIsZero(a);
            bTrue = !Rbc_ComplexIsZero(b);
            result = aTrue || bTrue;
            break;
        default:
            Tcl_Panic("bad complex boolean operator %d", operator);
            result = 0;
            break;
        }
        newArr[i] = (double)result;
    }
    FreeExpressionData(vPtr);
    vPtr->data.real = newArr;
    vPtr->type = RBC_VECTOR_REAL;
    vPtr->length = length;
    vPtr->size = length;
    vPtr->first = 0;
    vPtr->last = length - 1;
    vPtr->freeProc = (newArr == NULL) ? TCL_STATIC : TCL_DYNAMIC;
    return TCL_OK;
}

/**
 * @brief Validate a real-valued scalar count and rotate the left temporary.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] operator LEFT_SHIFT or RIGHT_SHIFT.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @param[in] v2Ptr Live right operand temporary; scalar broadcasting and shape rules depend on the operator.
 * @return TCL_OK after applying the operator, otherwise TCL_ERROR; the left temporary may be partially changed.
 *
 * @ingroup expression_engine
 *
 * Requires a length-one right operand. A complex scalar with zero imaginary part is accepted; nonzero
 * imaginary part is rejected. Rotation retains the left type and length.
 */
static int ApplyComplexShiftOperator(Tcl_Interp *interp, int operator, VectorObject * vPtr, VectorObject *v2Ptr) {
    Rbc_Complex shift;

    if (v2Ptr->length != 1) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("second shift operand must be scalar", -1));
        return TCL_ERROR;
    }
    shift = Rbc_VectorValueAsComplex(v2Ptr, 0);
    /*
     * A complex value with zero imaginary component is a valid
     * real shift count, so C << {1 0} is equivalent to C << 1.
     */
    if (shift.imag != 0.0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("shift count must be real", -1));
        return TCL_ERROR;
    }
    return RotateExpressionVector(interp, vPtr, operator, shift.real);
}

/**
 * @brief Dispatch complex arithmetic, boolean or rotation operations.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] operator An expression binary token; unsupported ordering/modulus operators are rejected.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @param[in] v2Ptr Live right operand temporary; scalar broadcasting and shape rules depend on the operator.
 * @return TCL_OK after applying the operator, otherwise TCL_ERROR; the left temporary may be partially changed.
 *
 * @ingroup expression_engine
 *
 * Either operand may be real. Arithmetic promotes the left temporary and broadcasts scalars, otherwise
 * requires equal lengths; boolean operations return real storage and shifts retain the left type.
 * Unsupported ordering/modulus operators fail. Elementwise arithmetic can partially change the
 * temporary before an error; named destinations are committed by the outer layer only.
 */
static int ApplyComplexBinaryOperator(Tcl_Interp *interp, int operator, VectorObject * vPtr, VectorObject *v2Ptr) {
    Tcl_Size i;

    switch (operator) {
    case EQUAL:
    case NEQ:
    case AND:
    case OR:
        return ApplyComplexBooleanOperator(interp, operator, vPtr, v2Ptr);
    case LEFT_SHIFT:
    case RIGHT_SHIFT:
        return ApplyComplexShiftOperator(interp, operator, vPtr, v2Ptr);
    case PLUS:
    case MINUS:
    case MULT:
    case DIVIDE:
    case EXPONENT:
        break;
    default:
        Tcl_SetObjResult(interp, Tcl_NewStringObj("operator is not supported for complex vectors", -1));
        return TCL_ERROR;
    }
    /*
     * Vector op scalar.
     */
    if (v2Ptr->length == 1) {
        Rbc_Complex scalar;

        scalar = Rbc_VectorValueAsComplex(v2Ptr, 0);
        if (PromoteExpressionVectorToComplex(vPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < vPtr->length; i++) {
            Rbc_Complex result;

            if (ApplyComplexBinaryValue(interp, operator, vPtr->data.complex[i], scalar, &result) != TCL_OK) {
                return TCL_ERROR;
            }
            vPtr->data.complex[i] = result;
        }
        return TCL_OK;
    }
    /*
     * Scalar op vector.
     */
    if (vPtr->length == 1) {
        Rbc_Complex scalar;
        Tcl_Size length;

        scalar = Rbc_VectorValueAsComplex(vPtr, 0);
        length = v2Ptr->length;
        if (PromoteExpressionVectorToComplex(vPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Rbc_VectorChangeLength(vPtr, length) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0; i < length; i++) {
            Rbc_Complex operand;
            Rbc_Complex result;

            operand = Rbc_VectorValueAsComplex(v2Ptr, i);
            if (ApplyComplexBinaryValue(interp, operator, scalar, operand, &result) != TCL_OK) {
                return TCL_ERROR;
            }
            vPtr->data.complex[i] = result;
        }
        return TCL_OK;
    }
    if (vPtr->length != v2Ptr->length) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("vectors are different lengths", -1));
        return TCL_ERROR;
    }
    if (PromoteExpressionVectorToComplex(vPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    for (i = 0; i < vPtr->length; i++) {
        Rbc_Complex operand;
        Rbc_Complex result;

        operand = Rbc_VectorValueAsComplex(v2Ptr, i);
        if (ApplyComplexBinaryValue(interp, operator, vPtr->data.complex[i], operand, &result) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->data.complex[i] = result;
    }
    return TCL_OK;
}

/**
 * @brief Test finiteness of both complex components.
 * @param[in] value Complex value whose two components are tested.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_engine
 *
 * Returns true only if both real and imaginary values are finite. Does not inspect errno or set a Tcl
 * diagnostic.
 */
static int ComplexValueIsFinite(Rbc_Complex value) { return FINITE(value.real) && FINITE(value.imag); }

void Rbc_VectorInstallMathFunctions(Tcl_HashTable *tablePtr) {
    Tcl_HashEntry *hPtr;
    MathFunction *mathPtr;
    int isNew;

    for (mathPtr = mathFunctions; mathPtr->name != NULL; mathPtr++) {
        hPtr = Tcl_CreateHashEntry(tablePtr, mathPtr->name, &isNew);
        Tcl_SetHashValue(hPtr, (ClientData)mathPtr);
    }
}

void Rbc_VectorInstallSpecialIndices(Tcl_HashTable *tablePtr) {
    InstallIndexProc(tablePtr, "min", Rbc_VecMin);
    InstallIndexProc(tablePtr, "max", Rbc_VecMax);
    InstallIndexProc(tablePtr, "mean", Mean);
    InstallIndexProc(tablePtr, "sum", Sum);
    InstallIndexProc(tablePtr, "prod", Product);
}

/**
 * @brief Install or remove a named special-index procedure.
 * @param[in,out] tablePtr Initialized string-key table owned by the vector subsystem.
 * @param[in] string NUL-terminated index name; Tcl copies the key when creating an entry.
 * @param[in] procPtr Borrowed callback to store, or NULL to remove the name. The table does not own callback
 *     state.
 *
 * @ingroup expression_engine
 *
 * Requires an initialized string-key hash table. Stores a borrowed function pointer, or deletes the
 * entry for NULL. Does not own or release callback state.
 */
static void InstallIndexProc(Tcl_HashTable *tablePtr, char *string, Rbc_VectorIndexProc *procPtr) {
    Tcl_HashEntry *hPtr;
    int dummy;

    hPtr = Tcl_CreateHashEntry(tablePtr, string, &dummy);
    if (procPtr == NULL) {
        Tcl_DeleteHashEntry(hPtr);
    } else {
        Tcl_SetHashValue(hPtr, (ClientData)procPtr);
    }
}

/**
 * @brief Find the first finite real value in the selected interval.
 * @param[in] vPtr Live real vector with a caller-validated inclusive first/last selection.
 * @return First finite storage index in the selection, or -1 when none exists.
 *
 * @ingroup expression_engine
 *
 * Requires valid real storage and first/last bounds. Returns its storage index or -1. Does not alter
 * selection or cached extrema.
 */
static Tcl_Size First(VectorObject *vPtr) {
    Tcl_Size i;

    for (i = vPtr->first; i <= vPtr->last; i++) {
        if (FINITE(vPtr->data.real[i])) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Find the next finite real value after a current index.
 * @param[in] vPtr Live real vector with a caller-validated inclusive selection.
 * @param[in] current Previous storage index; scanning starts at current+1 through last.
 * @return Next finite storage index, or -1 when no later finite value exists.
 *
 * @ingroup expression_engine
 *
 * Scans through last inclusive, returning a storage index or -1. Requires valid real storage and a
 * current index from the same traversal.
 */
static Tcl_Size Next(VectorObject *vPtr, Tcl_Size current) {
    Tcl_Size i;

    for (i = current + 1; i <= vPtr->last; i++) {
        if (FINITE(vPtr->data.real[i])) {
            return i;
        }
    }
    return -1;
}

double Rbc_VecMin(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;

    double min;
    Tcl_Size i;

    min = rbcNaN;
    for (i = 0; i < vPtr->length; i++) {
        if (FINITE(vPtr->data.real[i])) {
            min = vPtr->data.real[i];
            break;
        }
    }
    for (/* empty */; i < vPtr->length; i++) {
        if (FINITE(vPtr->data.real[i])) {
            if (min > vPtr->data.real[i]) {
                min = vPtr->data.real[i];
            }
        }
    }
    vPtr->min = min;
    return vPtr->min;
}

/**
 * @brief Draw one real random value, ignoring the input value.
 * @param[in] value Ignored real input; the shared random state advances.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Delegates to Rbc_RandomDouble and advances its random state. Invocation count is controlled by the
 * component adapter; this helper does not seed or create an independent generator.
 */
static double Random(double value) {
    (void)value;
    return Rbc_RandomDouble();
}

/**
 * @brief Draw independent real and imaginary random components.
 * @param[in] value Ignored complex input; the shared random state advances twice.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Ignores the supplied pair and calls Rbc_RandomDouble twice in real-then-imaginary order. Uses the
 * shared random helper, not a vector-owned generator.
 */
static Rbc_Complex ComplexRandom(Rbc_Complex value) {
    Rbc_Complex result;

    (void)value;
    result.real = Rbc_RandomDouble();
    result.imag = Rbc_RandomDouble();
    return result;
}

double Rbc_VecMax(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    double max;
    Tcl_Size i;

    max = rbcNaN;
    for (i = 0; i < vPtr->length; i++) {
        if (FINITE(vPtr->data.real[i])) {
            max = vPtr->data.real[i];
            break;
        }
    }
    for (/* empty */; i < vPtr->length; i++) {
        if (FINITE(vPtr->data.real[i])) {
            if (max < vPtr->data.real[i]) {
                max = vPtr->data.real[i];
            }
        }
    }
    vPtr->max = max;
    return vPtr->max;
}

/**
 * @brief Compute the real mean over finite selected values.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Arithmetic mean, nonfinite when no finite sample exists.
 *
 * @ingroup expression_numerics
 *
 * Uses First/Next and ordinary double summation. Divides by the finite count; no finite values produce
 * a nonfinite result rejected by expression evaluation. Does not modify storage or selection.
 */
static double Mean(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size i;
    Tcl_Size count;
    double sum;

    sum = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        sum += vPtr->data.real[i];
        count++;
    }
    return sum / (double)count;
}

/**
 * @brief Sum finite real values in the selected interval.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Sum, or zero when no finite sample exists.
 *
 * @ingroup expression_numerics
 *
 * Starts at zero and accumulates in storage order without compensation. Empty or entirely nonfinite
 * selections return zero. No allocation, notification or Tcl evaluation.
 */
static double Sum(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size i;
    double sum;

    sum = 0.0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        sum += vPtr->data.real[i];
    }
    return sum;
}

/**
 * @brief Multiply finite real values in the selected interval.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Product, or one when no finite sample exists.
 *
 * @ingroup expression_numerics
 *
 * Starts at one; empty or entirely nonfinite selections return one. Ordinary multiplication can
 * overflow or underflow; final validation belongs to the adapter/evaluator.
 */
static double Product(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size i;
    double prod;

    prod = 1.0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        prod *= vPtr->data.real[i];
    }
    return prod;
}

/**
 * @brief Sort the selected real interval through an index permutation.
 * @param[in,out] vPtr Caller-owned detached real temporary with a valid inclusive first/last interval. Only
 *     selected values are rearranged.
 * @return TCL_OK after sorting or for at most one selected value; TCL_ERROR on size/allocation failure, with
 *     values unchanged.
 *
 * @ingroup expression_numerics
 *
 * Selections of at most one value are unchanged. Allocates an index array and a temporary value array,
 * copies sorted values back only after allocation succeeds, then frees both arrays. Returns TCL_ERROR
 * on size/allocation failure. Intended for detached expression storage; sends no notifications.
 */
static int Sort(VectorObject *vPtr) {
    Tcl_Size *indexArr;
    double *tempArr;
    Tcl_Size rangeLength;
    Tcl_Size i;
    size_t byteCount;

    rangeLength = vPtr->last - vPtr->first + 1;
    if (rangeLength <= 1) {
        return TCL_OK;
    }
    indexArr = Rbc_VectorSortIndex(&vPtr, 1);
    if (indexArr == NULL) {
        return TCL_ERROR;
    }
    if (GetDoubleArrayByteCount(vPtr->interp, rangeLength, &byteCount) != TCL_OK) {
        ckfree(indexArr);
        return TCL_ERROR;
    }
    tempArr = Tcl_AttemptAlloc(byteCount);
    if (tempArr == NULL) {
        ckfree(indexArr);
        Tcl_SetObjResult(vPtr->interp, Tcl_NewStringObj("can't allocate temporary sort array", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < rangeLength; i++) {
        tempArr[i] = vPtr->data.real[indexArr[i]];
    }
    for (i = 0; i < rangeLength; i++) {
        vPtr->data.real[vPtr->first + i] = tempArr[i];
    }
    ckfree(tempArr);
    ckfree(indexArr);
    return TCL_OK;
}

/**
 * @brief Count finite real values in the selected interval.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Finite selected sample count as a double.
 *
 * @ingroup expression_numerics
 *
 * Returns the count as double, not allocated capacity or unfiltered vector length. Uses First/Next and
 * does not mutate the record.
 */
static double Length(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size count;
    Tcl_Size i;

    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        count++;
    }
    return (double)count;
}

/**
 * @brief Return the real middle order statistic without rearranging data.
 * @param[in] vecPtr Borrowed real RBC VectorObject with selection covering its entire used length. The sort
 *     index is temporary; source values are not rearranged.
 * @return Median, or -DBL_MAX for empty input or sort-index allocation failure; this sentinel is finite.
 *
 * @ingroup expression_numerics
 *
 * Allocates a sort index and frees it before returning. Uses full vector length to choose one middle
 * value or the average of two; requires the selected interval to cover the full vector, as expression
 * copies normally do. Empty input or sort-index failure returns the finite sentinel -DBL_MAX rather
 * than a Tcl status.
 */
static double Median(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size *iArr;
    Tcl_Size mid;
    double q2;

    if (vPtr->length == 0) {
        return -DBL_MAX;
    }
    iArr = Rbc_VectorSortIndex(&vPtr, 1);
    if (iArr == NULL) {
        return -DBL_MAX;
    }
    mid = (vPtr->length - 1) / 2;
    /*
     * Determine Q2 by checking whether the number of elements is odd
     * or even. For an even count, average the two middle values.
     */
    if (vPtr->length & 1) {
        q2 = vPtr->data.real[iArr[mid]];
    } else {
        q2 = vPtr->data.real[iArr[mid]] * 0.5 + vPtr->data.real[iArr[mid + 1]] * 0.5;
    }
    ckfree(iArr);
    return q2;
}

/**
 * @brief Compute real sample variance over finite selected values.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Sample variance, or zero for fewer than two finite samples.
 *
 * @ingroup expression_numerics
 *
 * Computes Mean first, then sums squared deviations and divides by count-1. Fewer than two finite
 * samples return zero. Uses ordinary double arithmetic without compensated summation or overflow
 * scaling.
 */
static double Variance(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    double dx, var, mean;
    Tcl_Size i;
    Tcl_Size count;

    mean = Mean(vecPtr);
    var = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        dx = vPtr->data.real[i] - mean;
        var += dx * dx;
        count++;
    }
    if (count < 2) {
        return 0.0;
    }
    var /= (double)(count - 1);
    return var;
}

/**
 * @brief Compute the legacy absolute standardized third moment.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Legacy absolute standardized third moment; constant samples may yield a nonfinite result.
 *
 * @ingroup expression_numerics
 *
 * Accumulates absolute cubed deviations and divides by count times sample variance to the power 3/2.
 * Fewer than two finite samples return zero; constant samples of length at least two yield a nonfinite
 * division. This is not conventional signed skewness; symmetry does not generally give zero.
 */
static double Skew(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    double diff, var, skew, mean, diffsq;
    Tcl_Size i;
    Tcl_Size count;

    mean = Mean(vecPtr);
    var = skew = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        diff = vPtr->data.real[i] - mean;
        diff = FABS(diff);
        diffsq = diff * diff;
        var += diffsq;
        skew += diffsq * diff;
        count++;
    }
    if (count < 2) {
        return 0.0;
    }
    var /= (double)(count - 1);
    skew /= count * var * sqrt(var);
    return skew;
}

/**
 * @brief Compute signed skewness using population central moments.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Signed population-moment skewness, or NaN for fewer than two samples or zero variance.
 *
 * @ingroup expression_numerics
 *
 * Uses finite selected real samples and returns m3/(m2*sqrt(m2)), where
 * mk=sum((x-mean)^k)/count. This is the unadjusted moment coefficient, not a
 * bias-corrected sample estimator. Scales inputs by their maximum magnitude
 * before accumulation to avoid overflow/underflow from cubing raw values.
 * Returns NaN for empty or constant input; final expression validation reports
 * a domain error. Does not modify storage or change the legacy Skew formula.
 */
static double Skewness(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size i, count = 0;
    double scale = 0.0, mean = 0.0, m2 = 0.0, m3 = 0.0;

    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        scale = MAX(scale, FABS(vPtr->data.real[i]));
        count++;
    }
    if (count < 2 || scale == 0.0) {
        return rbcNaN;
    }
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        mean += vPtr->data.real[i] / scale;
    }
    mean /= (double)count;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        double diff = vPtr->data.real[i] / scale - mean;
        double square = diff * diff;

        m2 += square;
        m3 += square * diff;
    }
    if (m2 == 0.0) {
        return rbcNaN;
    }
    m2 /= (double)count;
    m3 /= (double)count;
    return m3 / (m2 * sqrt(m2));
}

/**
 * @brief Return the square root of positive real sample variance.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Square root of positive variance, otherwise zero.
 *
 * @ingroup expression_numerics
 *
 * Delegates to Variance. Returns zero when variance is not greater than zero, including a NaN
 * comparison; does not independently propagate a Tcl status.
 */
static double StdDeviation(Rbc_Vector *vecPtr) {
    double var;

    var = Variance(vecPtr);
    if (var > 0.0) {
        return sqrt(var);
    }
    return 0.0;
}

/**
 * @brief Compute mean absolute real deviation from the finite-sample mean.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Mean absolute deviation, or zero for fewer than two finite samples.
 *
 * @ingroup expression_numerics
 *
 * Uses finite selected values, divides by their count, and returns zero for fewer than two. Does not
 * use sample count-1 normalization.
 */
static double AvgDeviation(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    double diff, avg, mean;
    Tcl_Size i;
    Tcl_Size count;

    mean = Mean(vecPtr);
    avg = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        diff = vPtr->data.real[i] - mean;
        avg += FABS(diff);
        count++;
    }
    if (count < 2) {
        return 0.0;
    }
    avg /= (double)count;
    return avg;
}

/**
 * @brief Compute the legacy fourth-moment excess statistic.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Legacy excess statistic, or zero for fewer than two finite samples or zero variance.
 *
 * @ingroup expression_numerics
 *
 * Uses finite selected values and sample variance. Returns sum of fourth powers divided by
 * count*variance*variance, minus three. Returns zero for fewer than two samples or zero variance. This
 * is not a bias-corrected estimator and is not population-moment normalization.
 */
static double Kurtosis(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    double diff, diffsq, kurt, var, mean;
    Tcl_Size i;
    Tcl_Size count;

    mean = Mean(vecPtr);
    var = kurt = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        diff = vPtr->data.real[i] - mean;
        diffsq = diff * diff;
        var += diffsq;
        kurt += diffsq * diffsq;
        count++;
    }
    if (count < 2) {
        return 0.0;
    }
    var /= (double)(count - 1);
    if (var == 0.0) {
        return 0.0;
    }
    kurt /= (count * var * var);
    return kurt - 3.0; /* Fisher Kurtosis */
}

/**
 * @brief Return the legacy lower quartile from a sorted index.
 * @param[in] vecPtr Borrowed real RBC VectorObject with selection covering its entire used length. The sort
 *     index is temporary; source values are not rearranged.
 * @return Legacy lower quartile, or -DBL_MAX for empty input or sort-index allocation failure; this sentinel
 *     is finite.
 *
 * @ingroup expression_numerics
 *
 * Empty input or index allocation failure returns -DBL_MAX. Lengths below four select the minimum.
 * Otherwise mid=(length-1)/2 and q=mid/2; odd mid selects q, even mid averages q and q+1. Requires a
 * full-length selection. Frees the index without rearranging source data.
 */
static double Q1(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size *iArr;
    double q1;

    if (vPtr->length == 0) {
        return -DBL_MAX;
    }
    iArr = Rbc_VectorSortIndex(&vPtr, 1);
    if (iArr == NULL) {
        return -DBL_MAX;
    }
    if (vPtr->length < 4) {
        q1 = vPtr->data.real[iArr[0]];
    } else {
        Tcl_Size mid;
        Tcl_Size q;
        mid = (vPtr->length - 1) / 2;
        q = mid / 2;
        if (mid & 1) {
            q1 = vPtr->data.real[iArr[q]];
        } else {
            q1 = vPtr->data.real[iArr[q]] * 0.5 + vPtr->data.real[iArr[q + 1]] * 0.5;
        }
    }
    ckfree(iArr);
    return q1;
}

/**
 * @brief Return the legacy upper quartile from a sorted index.
 * @param[in] vecPtr Borrowed real RBC VectorObject with selection covering its entire used length. The sort
 *     index is temporary; source values are not rearranged.
 * @return Legacy upper quartile, or -DBL_MAX for empty input or sort-index allocation failure; this sentinel
 *     is finite.
 *
 * @ingroup expression_numerics
 *
 * Empty input or index allocation failure returns -DBL_MAX. Lengths below four select the maximum.
 * Otherwise mid=(length-1)/2 and q=mid+(length-mid)/2; odd mid selects q, even mid averages q and q+1.
 * Requires a full-length selection. These are legacy index rules, not a selectable percentile
 * convention.
 */
static double Q3(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size *iArr;
    double q3;

    if (vPtr->length == 0) {
        return -DBL_MAX;
    }
    iArr = Rbc_VectorSortIndex(&vPtr, 1);
    if (iArr == NULL) {
        return -DBL_MAX;
    }
    if (vPtr->length < 4) {
        q3 = vPtr->data.real[iArr[vPtr->length - 1]];
    } else {
        Tcl_Size mid;
        Tcl_Size q;
        mid = (vPtr->length - 1) / 2;
        q = mid + (vPtr->length - mid) / 2;
        if (mid & 1) {
            q3 = vPtr->data.real[iArr[q]];
        } else {
            q3 = vPtr->data.real[iArr[q]] * 0.5 + vPtr->data.real[iArr[q + 1]] * 0.5;
        }
    }
    ckfree(iArr);
    return q3;
}

/**
 * @brief Normalize every real value using full-vector finite extrema.
 * @param[in,out] vecPtr Caller-owned detached real VectorObject passed through its Rbc_Vector prefix. Rewrites
 *     all used values and refreshes extrema while computing the range.
 * @return TCL_OK after normalization; this status does not imply finite output for a zero range.
 *
 * @ingroup expression_numerics
 *
 * Computes min/max, then writes (value-min)/(max-min) in place over the full length. Has no zero-range
 * guard: constant nonempty input produces nonfinite values rejected later by the expression evaluator.
 * Returns TCL_OK after the loop and does not notify; operates on detached expression storage.
 */
static int Norm(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    double norm, range, min, max;
    Tcl_Size i;

    min = Rbc_VecMin(vecPtr);
    max = Rbc_VecMax(vecPtr);
    range = max - min;
    for (i = 0; i < vPtr->length; i++) {
        norm = (vPtr->data.real[i] - min) / range;
        vPtr->data.real[i] = norm;
    }
    return TCL_OK;
}

/**
 * @brief Count nonzero finite real values in the selected interval.
 * @param[in] vecPtr Borrowed real RBC VectorObject passed through its Rbc_Vector prefix, with valid inclusive
 *     first/last bounds. Storage and selection remain unchanged.
 * @return Count of finite selected samples unequal to zero, as a double.
 *
 * @ingroup expression_numerics
 *
 * Uses exact comparison with zero, so both signed zeros are excluded. Returns a double count; no
 * tolerance or magnitude threshold is applied.
 */
static double Nonzeros(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size i;
    Tcl_Size count;

    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        if (vPtr->data.real[i] != 0.0) {
            count++;
        }
    }
    return (double)count;
}

/**
 * @brief Return a real component unchanged.
 * @param[in] value Real component returned unchanged.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Used by real branches of component adapters. Does not validate finiteness or normalize signed zero.
 */
static double Identity(double value) { return value; }

/**
 * @brief Return real zero regardless of the input.
 * @param[in] value Ignored real component.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Supplies the imaginary-part adapter for real input. Finite-input iteration belongs to the enclosing
 * adapter.
 */
static double Zero(double value) {
    (void)value;

    return 0.0;
}

/**
 * @brief Return the phase of a real value in radians.
 * @param[in] value Real component whose phase is requested.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Uses atan2 with positive zero as the imaginary argument. Branch and signed-zero behavior follow that
 * libm call, rather than a hand-coded sign test.
 */
static double Arg(double value) { return atan2(0.0, value); }

/**
 * @brief Return a complex value's Euclidean magnitude.
 * @param[in] value Complex value whose magnitude is requested.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Uses hypot to avoid unnecessary overflow in intermediate squares. The magnitude itself can exceed
 * the finite double range.
 */
static double ComplexAbs(Rbc_Complex value) { return hypot(value.real, value.imag); }

/**
 * @brief Return complex phase in radians.
 * @param[in] value Complex value whose phase is requested.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Uses atan2(imag, real), including its branch and signed-zero behavior. Does not alter the value or
 * report a Tcl status.
 */
static double ComplexArg(Rbc_Complex value) { return atan2(value.imag, value.real); }

/**
 * @brief Extract the real component.
 * @param[in] value Complex value whose real component is copied.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Returns the stored double directly; validation is the adapter's responsibility.
 */
static double ComplexReal(Rbc_Complex value) { return value.real; }

/**
 * @brief Extract the imaginary component.
 * @param[in] value Complex value whose imaginary component is copied.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Returns the stored double directly; validation is the adapter's responsibility.
 */
static double ComplexImag(Rbc_Complex value) { return value.imag; }

/**
 * @brief Return the complex conjugate.
 * @param[in] value Complex value to conjugate.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Negates only the imaginary component, including its sign bit for zero. Does not allocate or validate
 * the pair.
 */
static Rbc_Complex ComplexConj(Rbc_Complex value) {
    value.imag = -value.imag;
    return value;
}

/**
 * @brief Compute the square-root branch with nonnegative real part.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Square root on the implemented branch. The caller validates errno and component finiteness; no Tcl
 *     status is returned.
 *
 * @ingroup expression_numerics
 *
 * Returns complex zero unchanged. Scales very large components before hypot/2 to avoid avoidable
 * overflow. For negative real input, imaginary sign uses imag<0, so negative zero takes the same
 * branch as positive zero. The adapter checks errno and finite components; this is not a full C99
 * signed-zero branch guarantee.
 */
static Rbc_Complex ComplexSqrt(Rbc_Complex value) {
    Rbc_Complex result;
    double halfMagnitude;
    double t;

    if (Rbc_ComplexIsZero(value)) {
        return value;
    }
    /*
     * Compute hypot(value.real, value.imag) / 2 without introducing
     * an avoidable overflow for very large finite components.
     */
    if ((FABS(value.real) > DBL_MAX * 0.5) || (FABS(value.imag) > DBL_MAX * 0.5)) {
        halfMagnitude = hypot(value.real * 0.5, value.imag * 0.5);
    } else {
        halfMagnitude = 0.5 * hypot(value.real, value.imag);
    }
    if (value.real >= 0.0) {
        t = sqrt(halfMagnitude + 0.5 * value.real);
        result.real = t;
        result.imag = value.imag / (2.0 * t);
    } else {
        t = sqrt(halfMagnitude - 0.5 * value.real);
        result.real = FABS(value.imag) / (2.0 * t);
        result.imag = (value.imag < 0.0) ? -t : t;
    }
    return result;
}

/**
 * @brief Compute exp(real) times the unit complex phase.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Complex exponential. The caller validates errno and component finiteness; no Tcl status is returned.
 *
 * @ingroup expression_numerics
 *
 * Uses exp(real), cos(imag) and sin(imag). Can overflow or underflow; the component adapter owns errno
 * and finite-result checks.
 */
static Rbc_Complex ComplexExp(Rbc_Complex value) {
    Rbc_Complex result;
    double scale;

    scale = exp(value.real);
    result.real = scale * cos(value.imag);
    result.imag = scale * sin(value.imag);
    return result;
}

/**
 * @brief Compute log of a magnitude without first forming that magnitude.
 * @param[in] x First real component of the magnitude.
 * @param[in] y Second real component of the magnitude.
 * @return Logarithmic magnitude; two zero components invoke log(0), leaving error handling to the caller.
 *
 * @ingroup expression_numerics
 *
 * For finite inputs, factors out the larger absolute component and uses log(max)+log(1+ratio*ratio)/2.
 * Both zero calls log(0), preserving libm error behavior. Does not clear errno or set Tcl errors.
 */
static double LogHypot(double x, double y) {
    double a, b;
    double maximum, minimum;
    double ratio;

    a = FABS(x);
    b = FABS(y);
    if (a >= b) {
        maximum = a;
        minimum = b;
    } else {
        maximum = b;
        minimum = a;
    }
    if (maximum == 0.0) {
        return log(0.0);
    }
    ratio = minimum / maximum;
    return log(maximum) + 0.5 * log(1.0 + ratio * ratio);
}

/**
 * @brief Return logarithmic magnitude and principal atan2 phase.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Logarithmic magnitude plus imaginary principal phase. The caller validates errno and component
 *     finiteness; no Tcl status is returned.
 *
 * @ingroup expression_numerics
 *
 * Uses LogHypot for the real part and atan2(imag, real) for the imaginary part. Zero input is
 * singular; the adapter reports its libm/nonfinite failure.
 */
static Rbc_Complex ComplexLog(Rbc_Complex value) {
    Rbc_Complex result;

    result.real = LogHypot(value.real, value.imag);
    result.imag = atan2(value.imag, value.real);
    return result;
}

/**
 * @brief Scale the complex natural logarithm to base ten.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Complex logarithm divided by ln(10). The caller validates errno and component finiteness; no Tcl
 *     status is returned.
 *
 * @ingroup expression_numerics
 *
 * Multiplies both components by 1/ln(10), retaining the same phase branch and zero singularity as
 * ComplexLog.
 */
static Rbc_Complex ComplexLog10(Rbc_Complex value) {
    Rbc_Complex result;
    const double invLn10 = 0.43429448190325182765;

    result = ComplexLog(value);
    result.real *= invLn10;
    result.imag *= invLn10;
    return result;
}

/**
 * @brief Compute complex sine using real trigonometric and hyperbolic functions.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Complex sine. The caller validates errno and component finiteness; no Tcl status is returned.
 *
 * @ingroup expression_numerics
 *
 * Returns sin(x)*cosh(y) + i*cos(x)*sinh(y). Intermediate hyperbolic overflow is not scaled away;
 * caller validates errno and result components.
 */
static Rbc_Complex ComplexSin(Rbc_Complex value) {
    Rbc_Complex result;
    double sinhImag;
    double coshImag;

    sinhImag = sinh(value.imag);
    coshImag = cosh(value.imag);
    result.real = sin(value.real) * coshImag;
    result.imag = cos(value.real) * sinhImag;
    return result;
}

/**
 * @brief Compute complex cosine from real trigonometric/hyperbolic functions.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Complex cosine. The caller validates errno and component finiteness; no Tcl status is returned.
 *
 * @ingroup expression_numerics
 *
 * Returns cos(x)*cosh(y) - i*sin(x)*sinh(y). Does not independently handle overflow or report a Tcl
 * error.
 */
static Rbc_Complex ComplexCos(Rbc_Complex value) {
    Rbc_Complex result;
    double sinhImag;
    double coshImag;

    sinhImag = sinh(value.imag);
    coshImag = cosh(value.imag);
    result.real = cos(value.real) * coshImag;
    result.imag = -sin(value.real) * sinhImag;
    return result;
}

/**
 * @brief Compute complex hyperbolic sine.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Complex hyperbolic sine. The caller validates errno and component finiteness; no Tcl status is
 *     returned.
 *
 * @ingroup expression_numerics
 *
 * Returns sinh(x)*cos(y) + i*cosh(x)*sin(y). Caller owns errno and finite-result validation.
 */
static Rbc_Complex ComplexSinh(Rbc_Complex value) {
    Rbc_Complex result;
    double sinhReal;
    double coshReal;

    sinhReal = sinh(value.real);
    coshReal = cosh(value.real);
    result.real = sinhReal * cos(value.imag);
    result.imag = coshReal * sin(value.imag);
    return result;
}

/**
 * @brief Compute complex hyperbolic cosine.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Complex hyperbolic cosine. The caller validates errno and component finiteness; no Tcl status is
 *     returned.
 *
 * @ingroup expression_numerics
 *
 * Returns cosh(x)*cos(y) + i*sinh(x)*sin(y). Caller owns errno and finite-result validation.
 */
static Rbc_Complex ComplexCosh(Rbc_Complex value) {
    Rbc_Complex result;
    double sinhReal;
    double coshReal;

    sinhReal = sinh(value.real);
    coshReal = cosh(value.real);
    result.real = coshReal * cos(value.imag);
    result.imag = sinhReal * sin(value.imag);
    return result;
}

/**
 * @brief Compute complex tangent using a decaying exponential formulation.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Complex tangent. The caller validates errno and component finiteness; no Tcl status is returned.
 *
 * @ingroup expression_numerics
 *
 * On the real axis delegates to tan and preserves imaginary zero. Otherwise uses exp(-2*abs(imag)) and
 * a denominator arranged to reduce cancellation near poles. For abs(imag)>350 sets the exponential
 * term to zero to avoid irrelevant underflow errno. Does not suppress true singular or nonfinite
 * results.
 */
static Rbc_Complex ComplexTan(Rbc_Complex value) {
    Rbc_Complex result;
    double a;
    double t;
    double s, c;
    double oneMinusT;
    double denominator;

    /*
     * Preserve the ordinary real tan() behavior on the real axis.
     * This is particularly useful near pi/2, where an algebraically
     * equivalent doubled-angle formula can lose precision.
     */
    if (value.imag == 0.0) {
        result.real = tan(value.real);
        result.imag = value.imag;
        return result;
    }
    a = FABS(value.imag);
    /*
     * Beyond this point exp(-2*a) is numerically irrelevant to the
     * result. Avoid an otherwise harmless exp() underflow setting
     * errno and turning a finite tan() result into an expression error.
     */
    if (a > 350.0) {
        t = 0.0;
    } else {
        t = exp(-2.0 * a);
    }
    s = sin(value.real);
    c = cos(value.real);
    oneMinusT = 1.0 - t;
    /*
     * Equivalent to:
     *
     *     1 + t*t + 2*t*cos(2*x)
     *
     * but avoids cancellation near x = pi/2 and small imag.
     */
    denominator = oneMinusT * oneMinusT + 4.0 * t * c * c;
    result.real = (4.0 * t * s * c) / denominator;
    result.imag = copysign((1.0 - t * t) / denominator, value.imag);
    return result;
}

/**
 * @brief Compute complex hyperbolic tangent with scaled exponential terms.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Complex hyperbolic tangent. The caller validates errno and component finiteness; no Tcl status is
 *     returned.
 *
 * @ingroup expression_numerics
 *
 * On the imaginary axis uses i*tan(imag) and preserves real zero. Otherwise uses exp(-2*abs(real)),
 * setting it to zero above 350, with a cancellation-reduced denominator. Caller checks
 * singularities/nonfinite results.
 */
static Rbc_Complex ComplexTanh(Rbc_Complex value) {
    Rbc_Complex result;
    double a;
    double t;
    double s, c;
    double oneMinusT;
    double denominator;

    /*
     * Preserve tanh(i*y) = i*tan(y), including behavior near
     * the imaginary-axis poles.
     */
    if (value.real == 0.0) {
        result.real = value.real;
        result.imag = tan(value.imag);
        return result;
    }
    a = FABS(value.real);
    if (a > 350.0) {
        t = 0.0;
    } else {
        t = exp(-2.0 * a);
    }
    s = sin(value.imag);
    c = cos(value.imag);
    oneMinusT = 1.0 - t;
    denominator = oneMinusT * oneMinusT + 4.0 * t * c * c;
    result.real = copysign((1.0 - t * t) / denominator, value.real);
    result.imag = (4.0 * t * s * c) / denominator;
    return result;
}

/**
 * @brief Compute real acosh for an argument at least one.
 * @param[in] value Real scalar at least one; the caller establishes the domain.
 * @return Real inverse hyperbolic cosine; no independent domain or finite-output check is performed.
 *
 * @ingroup expression_numerics
 *
 * Uses log(value+sqrt(value-1)*sqrt(value+1)); above DBL_MAX/2 uses log(value)+ln(2). Does not
 * validate its domain; ComplexAsin clamps its argument before calling.
 */
static double RealAcosh(double value) {
    const double ln2 = 0.69314718055994530942;

    if (value > DBL_MAX * 0.5) {
        return log(value) + ln2;
    }
    return log(value + sqrt(value - 1.0) * sqrt(value + 1.0));
}

/**
 * @brief Compute an inverse-sine branch from distances to plus/minus one.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Inverse sine on the implemented branch. The caller validates errno and component finiteness; no Tcl
 *     status is returned.
 *
 * @ingroup expression_numerics
 *
 * Forms alpha as two half magnitudes to avoid adding them before division. Clamps alpha to at least
 * one and beta=real/alpha into [-1,1], then combines asin(beta) and signed acosh(alpha). Uses copysign
 * for imaginary sign. Individual magnitudes can still overflow for extreme finite inputs.
 */
static Rbc_Complex ComplexAsin(Rbc_Complex value) {
    Rbc_Complex result;
    double rPlus;
    double rMinus;
    double alpha;
    double beta;

    rPlus = hypot(value.real + 1.0, value.imag);
    rMinus = hypot(value.real - 1.0, value.imag);
    /*
     * Write this as two halves to avoid overflowing rPlus+rMinus
     * when the final alpha itself is representable.
     */
    alpha = 0.5 * rPlus + 0.5 * rMinus;
    /*
     * Mathematically alpha >= 1, but rounding at points very close
     * to [-1,1] may put it a tiny amount below one.
     */
    if (alpha < 1.0) {
        alpha = 1.0;
    }
    /*
     * alpha * beta == x.  Computing beta this way avoids the
     * cancellation in (rPlus-rMinus)/2.
     */
    beta = value.real / alpha;
    if (beta > 1.0) {
        beta = 1.0;
    } else if (beta < -1.0) {
        beta = -1.0;
    }
    result.real = asin(beta);
    result.imag = copysign(RealAcosh(alpha), value.imag);
    return result;
}

/**
 * @brief Compute inverse cosine from the inverse-sine branch.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Inverse cosine derived from ComplexAsin. The caller validates errno and component finiteness; no Tcl
 *     status is returned.
 *
 * @ingroup expression_numerics
 *
 * Returns pi/2 minus the real part of ComplexAsin and negates its imaginary part. Inherits that
 * helper's numerical limits and branch behavior.
 */
static Rbc_Complex ComplexAcos(Rbc_Complex value) {
    Rbc_Complex result;
    Rbc_Complex asinValue;

    asinValue = ComplexAsin(value);
    result.real = M_PI_2 - asinValue.real;
    result.imag = -asinValue.imag;
    return result;
}

/**
 * @brief Compute complex inverse tangent with scaled angle and logarithmic distances.
 * @param[in] value Complex scalar passed by value; no vector or Tcl-object ownership is transferred.
 * @return Inverse tangent on the implemented branch. The caller validates errno and component finiteness; no
 *     Tcl status is returned.
 *
 * @ingroup expression_numerics
 *
 * Scales the quadratic terms used by atan2 and uses LogHypot for distances to plus/minus i. Returns
 * half the angle and half the log-distance difference. Poles at plus/minus i remain singular; caller
 * validates errno and finite components.
 */
static Rbc_Complex ComplexAtan(Rbc_Complex value) {
    Rbc_Complex result;
    double scale;
    double x;
    double y;
    double invScale;
    double numerator;
    double denominator;
    double logPlus;
    double logMinus;

    scale = FABS(value.real);
    if (FABS(value.imag) > scale) {
        scale = FABS(value.imag);
    }
    if (scale < 1.0) {
        scale = 1.0;
    }
    x = value.real / scale;
    y = value.imag / scale;
    invScale = 1.0 / scale;
    numerator = 2.0 * x * invScale;
    denominator = invScale * invScale - x * x - y * y;
    result.real = 0.5 * atan2(numerator, denominator);
    /*
     * |z+i| and |z-i|.  LogHypot avoids forming a magnitude that
     * can overflow before its logarithm is taken.
     */
    logPlus = LogHypot(value.real, value.imag + 1.0);
    logMinus = LogHypot(value.real, value.imag - 1.0);
    result.imag = 0.5 * (logPlus - logMinus);
    return result;
}

/**
 * @brief Compute complex powers with a separate integral-exponent path.
 * @param[in] interp Diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] base Complex base passed by value.
 * @param[in] exponent Complex exponent passed by value; finite integral real exponents use the separate
 *     squaring path.
 * @param[out] resultPtr Non-NULL caller-owned result storage, written on TCL_OK and left unchanged on
 *     TCL_ERROR. Success does not guarantee finite components.
 * @return TCL_OK after computation, or TCL_ERROR with a diagnostic for an unsupported power of zero.
 *
 * @ingroup expression_numerics
 *
 * Finite real integer exponents use exponentiation by squaring with the exponent kept as double. Zero
 * exponent returns one, including zero-to-zero; negative powers of zero fail. For zero base, positive
 * nonintegral real powers return zero and finite nonreal exponents fail. Other cases use
 * exp(exponent*Log(base)). Returns Tcl status; successful computation can still be nonfinite and
 * requires outer validation.
 */
static int ComplexPower(Tcl_Interp *interp, Rbc_Complex base, Rbc_Complex exponent, Rbc_Complex *resultPtr) {
    Rbc_Complex one;

    one = Rbc_ComplexFromReal(1.0);
    /*
     * A finite real integer exponent is branch-independent.  Use
     * exponentiation by squaring instead of exp(exponent * log(base)).
     *
     * Keep the exponent as a double.  This avoids converting very
     * large exactly-integral doubles to an integer type whose range
     * may be smaller.
     */
    if ((exponent.imag == 0.0) && FINITE(exponent.real) && (floor(exponent.real) == exponent.real)) {
        Rbc_Complex factor;
        Rbc_Complex result;
        double power;

        result = one;
        if (exponent.real == 0.0) {
            /*
             * Preserve the real expression convention: 0^0 == 1.
             */
            *resultPtr = result;
            return TCL_OK;
        }
        if (exponent.real < 0.0) {
            if (Rbc_ComplexIsZero(base)) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("divide by zero", -1));
                return TCL_ERROR;
            }
            factor = Rbc_ComplexDiv(one, base);
            power = -exponent.real;
        } else {
            factor = base;
            power = exponent.real;
        }
        while (power > 0.0) {
            if (fmod(power, 2.0) != 0.0) {
                result = Rbc_ComplexMul(result, factor);
            }
            power = floor(power * 0.5);
            if (power > 0.0) {
                factor = Rbc_ComplexMul(factor, factor);
            }
        }
        *resultPtr = result;
        return TCL_OK;
    }
    /*
     * Zero to a finite non-integral real power needs special handling:
     * log(0) cannot be used as an intermediate representation.
     */
    if (Rbc_ComplexIsZero(base) && FINITE(exponent.real) && FINITE(exponent.imag)) {
        if (exponent.imag != 0.0) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("zero cannot be raised to a complex power", -1));
            return TCL_ERROR;
        }
        if (exponent.real > 0.0) {
            *resultPtr = Rbc_ComplexFromReal(0.0);
            return TCL_OK;
        }
        /*
         * exponent == 0 was handled by the integer path above.
         */
        Tcl_SetObjResult(interp, Tcl_NewStringObj("divide by zero", -1));
        return TCL_ERROR;
    }
    /*
     * Principal value:
     *
     *     base^exponent = exp(exponent * Log(base))
     *
     * ComplexLog() supplies the principal logarithm.
     */
    {
        Rbc_Complex logBase;
        Rbc_Complex product;

        logBase = ComplexLog(base);
        product = Rbc_ComplexMul(exponent, logBase);
        *resultPtr = ComplexExp(product);
    }
    return TCL_OK;
}

/**
 * @brief Sum selected complex values having two finite components.
 * @param[in] vPtr Borrowed complex vector with valid inclusive first/last bounds. Samples with either
 *     component nonfinite are skipped; storage and selection remain unchanged.
 * @return Complex sum, or 0+0i when no sample qualifies.
 *
 * @ingroup expression_numerics
 *
 * Starts at 0+0i and skips the entire sample if either component is nonfinite. Uses ordinary complex
 * addition in storage order; empty input returns complex zero.
 */
static Rbc_Complex ComplexSum(VectorObject *vPtr) {
    Rbc_Complex sum;
    Tcl_Size i;

    sum.real = 0.0;
    sum.imag = 0.0;
    for (i = vPtr->first; i <= vPtr->last; i++) {
        Rbc_Complex value;

        value = vPtr->data.complex[i];
        if (!ComplexValueIsFinite(value)) {
            continue;
        }
        sum = Rbc_ComplexAdd(sum, value);
    }
    return sum;
}

/**
 * @brief Compute the componentwise mean of finite complex samples.
 * @param[in] vPtr Borrowed complex vector with valid inclusive first/last bounds. Samples with either
 *     component nonfinite are skipped; storage and selection remain unchanged.
 * @return Componentwise mean, or a pair of NaNs when no sample qualifies.
 *
 * @ingroup expression_numerics
 *
 * Both components must be finite for a sample to count. Divides real and imaginary sums by the same
 * selected sample count. No finite samples return NaN in both components; the evaluator rejects that
 * result.
 */
static Rbc_Complex ComplexMean(VectorObject *vPtr) {
    Rbc_Complex sum;
    Rbc_Complex result;
    Tcl_Size count;
    Tcl_Size i;

    sum.real = 0.0;
    sum.imag = 0.0;
    count = 0;
    for (i = vPtr->first; i <= vPtr->last; i++) {
        Rbc_Complex value;

        value = vPtr->data.complex[i];
        if (!ComplexValueIsFinite(value)) {
            continue;
        }
        sum = Rbc_ComplexAdd(sum, value);
        count++;
    }
    if (count == 0) {
        /*
         * Match the real Mean() behavior: the mean of no finite
         * samples is not a valid finite expression result.
         */
        result.real = rbcNaN;
        result.imag = rbcNaN;
        return result;
    }
    result.real = sum.real / (double)count;
    result.imag = sum.imag / (double)count;
    return result;
}

/**
 * @brief Multiply selected finite complex samples.
 * @param[in] vPtr Borrowed complex vector with valid inclusive first/last bounds. Samples with either
 *     component nonfinite are skipped; storage and selection remain unchanged.
 * @return Complex product, or 1+0i when no sample qualifies.
 *
 * @ingroup expression_numerics
 *
 * Starts at 1+0i and skips samples with either component nonfinite. Empty input returns complex one.
 * Uses Rbc_ComplexMul; does not promise overflow-free accumulation.
 */
static Rbc_Complex ComplexProduct(VectorObject *vPtr) {
    Rbc_Complex product;
    Tcl_Size i;

    product.real = 1.0;
    product.imag = 0.0;
    for (i = vPtr->first; i <= vPtr->last; i++) {
        Rbc_Complex value;

        value = vPtr->data.complex[i];
        if (!ComplexValueIsFinite(value)) {
            continue;
        }
        product = Rbc_ComplexMul(product, value);
    }
    return product;
}

/**
 * @brief Count selected complex samples with both components finite.
 * @param[in] vPtr Borrowed complex vector with valid inclusive first/last bounds. Samples with either
 *     component nonfinite are skipped; storage and selection remain unchanged.
 * @return Qualifying sample count as a real double.
 *
 * @ingroup expression_numerics
 *
 * Returns a double count. A sample with only one finite component is excluded entirely; this is not
 * raw vector length.
 */
static double ComplexLength(VectorObject *vPtr) {
    Tcl_Size count;
    Tcl_Size i;

    count = 0;
    for (i = vPtr->first; i <= vPtr->last; i++) {
        if (ComplexValueIsFinite(vPtr->data.complex[i])) {
            count++;
        }
    }
    return (double)count;
}

/**
 * @brief Count finite selected complex samples other than complex zero.
 * @param[in] vPtr Borrowed complex vector with valid inclusive first/last bounds. Samples with either
 *     component nonfinite are skipped; storage and selection remain unchanged.
 * @return Number of qualifying samples with either component nonzero, as a real double.
 *
 * @ingroup expression_numerics
 *
 * A sample counts when both components are finite and at least one is nonzero. No tolerance is used.
 */
static double ComplexNonzeros(VectorObject *vPtr) {
    Tcl_Size count;
    Tcl_Size i;

    count = 0;
    for (i = vPtr->first; i <= vPtr->last; i++) {
        Rbc_Complex value;

        value = vPtr->data.complex[i];
        if (!ComplexValueIsFinite(value)) {
            continue;
        }
        if (!Rbc_ComplexIsZero(value)) {
            count++;
        }
    }
    return (double)count;
}

/**
 * @brief Apply ceil independently to both complex components.
 * @param[in] value Complex value to round upward componentwise.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Does not round magnitude or phase. The adapter validates errno and finite results.
 */
static Rbc_Complex ComplexCeil(Rbc_Complex value) {
    value.real = ceil(value.real);
    value.imag = ceil(value.imag);
    return value;
}

/**
 * @brief Apply floor independently to both complex components.
 * @param[in] value Complex value to round downward componentwise.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Does not round magnitude or phase. The adapter validates errno and finite results.
 */
static Rbc_Complex ComplexFloor(Rbc_Complex value) {
    value.real = floor(value.real);
    value.imag = floor(value.imag);
    return value;
}

/**
 * @brief Round both components halfway away from zero.
 * @param[in] value Complex value to round componentwise.
 * The scalar is passed by value; no vector storage or ownership is transferred.
 *
 * @ingroup expression_numerics
 *
 * Delegates each component to Round. Does not change a complex temporary into real storage.
 */
static Rbc_Complex ComplexRound(Rbc_Complex value) {
    value.real = Round(value.real);
    value.imag = Round(value.imag);
    return value;
}

/**
 * @brief Compute mean complex-plane distance from the sample mean.
 * @param[in] vPtr Borrowed complex vector with valid inclusive first/last bounds. Samples with either
 *     component nonfinite are skipped; storage and selection remain unchanged.
 * @return Mean complex-plane distance, or zero for fewer than two qualifying samples.
 *
 * @ingroup expression_numerics
 *
 * Uses hypot(realDiff, imagDiff) for each finite selected sample and divides by count. Returns real
 * zero for fewer than two finite samples. Does not compute independent real/imaginary deviations.
 */
static double ComplexAvgDeviation(VectorObject *vPtr) {
    Rbc_Complex mean;
    double avg;
    Tcl_Size count;
    Tcl_Size i;

    mean = ComplexMean(vPtr);
    avg = 0.0;
    count = 0;
    for (i = vPtr->first; i <= vPtr->last; i++) {
        Rbc_Complex value;
        double realDiff;
        double imagDiff;

        value = vPtr->data.complex[i];
        if (!ComplexValueIsFinite(value)) {
            continue;
        }
        realDiff = value.real - mean.real;
        imagDiff = value.imag - mean.imag;
        avg += hypot(realDiff, imagDiff);
        count++;
    }
    /*
     * Preserve the real AvgDeviation() behavior.
     */
    if (count < 2) {
        return 0.0;
    }
    avg /= (double)count;
    return avg;
}

/**
 * @brief Compute real sample variance from squared complex magnitudes.
 * @param[in] vPtr Borrowed complex vector with valid inclusive first/last bounds. Samples with either
 *     component nonfinite are skipped; storage and selection remain unchanged.
 * @return Real squared-magnitude sample variance, or zero for fewer than two qualifying samples.
 *
 * @ingroup expression_numerics
 *
 * Computes the complex mean, accumulates hypot(realDiff, imagDiff) squared and divides by count-1.
 * Returns zero for fewer than two finite samples. This is squared-magnitude variance, not the possibly
 * complex mean of squared deviations; squaring can still overflow.
 */
static double ComplexVariance(VectorObject *vPtr) {
    Rbc_Complex mean;
    double var;
    Tcl_Size count;
    Tcl_Size i;

    mean = ComplexMean(vPtr);
    var = 0.0;
    count = 0;
    for (i = vPtr->first; i <= vPtr->last; i++) {
        Rbc_Complex value;
        double realDiff;
        double imagDiff;
        double magnitude;

        value = vPtr->data.complex[i];
        if (!ComplexValueIsFinite(value)) {
            continue;
        }
        realDiff = value.real - mean.real;
        imagDiff = value.imag - mean.imag;
        magnitude = hypot(realDiff, imagDiff);
        var += magnitude * magnitude;
        count++;
    }
    /*
     * Preserve the real Variance() behavior.
     */
    if (count < 2) {
        return 0.0;
    }
    var /= (double)(count - 1);
    return var;
}

/**
 * @brief Return the square root of positive complex sample variance.
 * @param[in] vPtr Borrowed complex vector with valid inclusive first/last bounds. Samples with either
 *     component nonfinite are skipped; storage and selection remain unchanged.
 * @return Square root of positive squared-magnitude variance, otherwise zero.
 *
 * @ingroup expression_numerics
 *
 * Returns a real scalar. Delegates to ComplexVariance and returns zero when variance is not greater
 * than zero; does not separately diagnose a nonfinite intermediate.
 */
static double ComplexStdDeviation(VectorObject *vPtr) {
    double var;

    var = ComplexVariance(vPtr);
    if (var > 0.0) {
        return sqrt(var);
    }
    return 0.0;
}

/**
 * @brief Return the legacy sign-test absolute value.
 * @param[in] value Real scalar tested against zero; negative zero is preserved.
 * @return Negated input when strictly negative, otherwise the unchanged input.
 *
 * @ingroup expression_numerics
 *
 * Negates values strictly below zero and otherwise returns the original value. Unlike fabs, negative
 * zero retains its sign.
 */
static double Fabs(double value) {
    if (value < 0.0) {
        return -value;
    }
    return value;
}

/**
 * @brief Round a real value halfway away from zero as a double.
 * @param[in] value Real scalar to round without narrowing to an integer type.
 * @return Rounded double using the implemented half-away-from-zero formula; the caller checks finiteness.
 *
 * @ingroup expression_numerics
 *
 * Uses ceil(value-0.5) for negative values and floor(value+0.5) otherwise. Performs no integer
 * conversion or independent finite check.
 */
static double Round(double value) {
    if (value < 0.0) {
        return ceil(value - 0.5);
    } else {
        return floor(value + 0.5);
    }
}

/**
 * @brief Copy an evaluated temporary into a fixed-type destination.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] destPtr Live writable fixed-type destination; caller protects lifetime across storage release.
 * @param[in] srcPtr Evaluated source temporary with a valid selected interval, kept alive through copying.
 * @return TCL_OK after copying, or TCL_ERROR for incompatible types or resize/copy failure.
 *
 * @ingroup expression_engine
 *
 * Matching types delegate to private duplication. Real results can populate a complex destination with
 * zero imaginary parts; complex-to-real assignment is rejected. Resizing/copying does not flush mapped
 * caches or notify. The caller checks destination lifetime and writability across expression
 * callbacks.
 */
static int CopyExpressionResult(Tcl_Interp *interp, VectorObject *destPtr, VectorObject *srcPtr) {
    Tcl_Size i;
    Tcl_Size j;
    Tcl_Size length;

    if (destPtr->type == srcPtr->type) {
        return Rbc_VectorDuplicate(destPtr, srcPtr);
    }
    /*
     * A real expression can be promoted into a complex target.
     */
    if ((destPtr->type == RBC_VECTOR_COMPLEX) && (srcPtr->type == RBC_VECTOR_REAL)) {
        length = srcPtr->last - srcPtr->first + 1;
        if (Rbc_VectorChangeLength(destPtr, length) != TCL_OK) {
            return TCL_ERROR;
        }
        for (i = 0, j = srcPtr->first; i < length; i++, j++) {
            destPtr->data.complex[i].real = srcPtr->data.real[j];
            destPtr->data.complex[i].imag = 0.0;
        }
        return TCL_OK;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("can't store complex expression result in real vector", -1));
    return TCL_ERROR;
}

/**
 * @brief Record destruction of the original assignment destination.
 * @param[in] interp Notification interpreter, unused here. See @ref rbc_interp_argument.
 * @param[in,out] clientData Caller-owned int destruction flag, valid for the temporary observer's lifetime.
 * @param[in] notify Notification kind; only RBC_VECTOR_NOTIFY_DESTROY sets the flag.
 *
 * @ingroup expression_engine
 *
 * clientData points to the caller's live stack flag. Only DESTROY sets it. The destruction path
 * releases the observer; the callback does not access the vector or keep an interpreter alive.
 */
static void ExpressionDestinationChanged(Tcl_Interp *interp, ClientData clientData, Rbc_VectorNotify notify) {
    (void)interp;
    if (notify == RBC_VECTOR_NOTIFY_DESTROY) {
        *(int *)clientData = TRUE;
    }
}

/* Internal evaluation contracts are documented in rbcVectorInt.h. */
int Rbc_VectorEvaluate(Tcl_Interp *interp, char *string, VectorObject **resultPtr) {
    Value value;

    *resultPtr = NULL;
    value.vPtr = Rbc_VectorNew(Rbc_VectorGetInterpData(interp));
    if (EvaluateExpression(interp, string, &value) != TCL_OK) {
        Rbc_VectorFree(value.vPtr);
        return TCL_ERROR;
    }
    *resultPtr = value.vPtr;
    return TCL_OK;
}

int Rbc_ExprVector(Tcl_Interp *interp, char *string, Rbc_Vector *vecPtr) {
    VectorInterpData *dataPtr;
    VectorObject *vPtr;
    Value value;
    int result;
    int destroyed = FALSE;
    VectorClient *clientPtr = NULL;

    vPtr = (VectorObject *)vecPtr;
    dataPtr = (vecPtr != NULL) ? vPtr->dataPtr : Rbc_VectorGetInterpData(interp);
    if ((vPtr != NULL) && (Rbc_VectorCheckWritable(interp, vecPtr) != TCL_OK)) {
        return TCL_ERROR;
    }
    value.vPtr = Rbc_VectorNew(dataPtr);
    if (vPtr != NULL) {
        clientPtr = RbcCalloc(1, sizeof(VectorClient));
        clientPtr->magic = VECTOR_MAGIC;
        clientPtr->serverPtr = vPtr;
        clientPtr->proc = ExpressionDestinationChanged;
        clientPtr->clientData = &destroyed;
        clientPtr->linkPtr = Rbc_ChainAppend(vPtr->chainPtr, clientPtr);
    }
    result = EvaluateExpression(interp, string, &value);
    if (result != TCL_OK) {
        goto done;
    }
    if (vPtr != NULL) {
        /*
         * Propagate an allocation/size failure instead of silently
         * reporting a successful vector expression.
         */
        if (destroyed) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("expression destination vector was destroyed", -1));
            result = TCL_ERROR;
            goto done;
        }
        /* Substitutions or map callbacks may have locked the destination. */
        result = Rbc_VectorCheckWritable(interp, (Rbc_Vector *)vPtr);
        if (result != TCL_OK) {
            goto done;
        }
        result = CopyExpressionResult(interp, vPtr, value.vPtr);
        if (result != TCL_OK) {
            goto done;
        }
    } else {
        Tcl_Size i;
        Tcl_Obj *resultObj;

        resultObj = Tcl_NewListObj(0, NULL);
        for (i = 0; i < value.vPtr->length; i++) {
            Tcl_ListObjAppendElement(NULL, resultObj, Rbc_NewVectorValueObj(value.vPtr, i));
        }
        Tcl_SetObjResult(interp, resultObj);
    }
    result = TCL_OK;

done:
    if ((clientPtr != NULL) && !destroyed) {
        Rbc_FreeVectorId((Rbc_VectorId)clientPtr);
    }
    Rbc_VectorFree(value.vPtr);
    return result;
}

/**
 * @brief Evaluate the complete writable expression into a caller-owned temporary.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] string NUL-terminated writable expression buffer, valid through evaluation; delimiters are
 *     restored.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return TCL_OK with a complete finite result, otherwise a non-OK evaluation status; side effects remain.
 *
 * @ingroup expression_engine
 *
 * Initializes parser state, calls NextValue and requires END. Rejects nonfinite components in the
 * final result through MathError. The caller frees the temporary on success or failure. Substitutions
 * and map callbacks execute Tcl; their side effects are not rolled back.
 */
static int EvaluateExpression(Tcl_Interp *interp, char *string, Value *valuePtr) {
    ParseInfo info;
    int result;
    VectorObject *vPtr;
    Tcl_Size i;

    info.expr = info.nextPtr = string;
    result = NextValue(interp, &info, -1, valuePtr);
    if (result != TCL_OK) {
        return result;
    }
    if (info.token != END) {
        Rbc_AppendResultStrings(interp, ": syntax error in expression \"", string, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    vPtr = valuePtr->vPtr;
    /* Check for NaN's and overflows. */
    for (i = 0; i < vPtr->length; i++) {
        switch (vPtr->type) {
        case RBC_VECTOR_REAL:
            if (!FINITE(vPtr->data.real[i])) {
                MathError(interp, vPtr->data.real[i]);
                return TCL_ERROR;
            }
            break;
        case RBC_VECTOR_COMPLEX:
            if (!FINITE(vPtr->data.complex[i].real)) {
                MathError(interp, vPtr->data.complex[i].real);
                return TCL_ERROR;
            }
            if (!FINITE(vPtr->data.complex[i].imag)) {
                MathError(interp, vPtr->data.complex[i].imag);
                return TCL_ERROR;
            }
            break;
        default:
            Tcl_Panic("bad vector type %d", (int)vPtr->type);
        }
    }
    return TCL_OK;
}

/**
 * @brief Evaluate one precedence-delimited expression into an existing temporary.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] parsePtr Live parser state borrowing the writable expression; updated through the scanned
 *     terminator.
 * @param[in] prec Precedence cutoff; -1 accepts a complete expression up to a delimiter.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return TCL_OK for the parsed subexpression, otherwise a non-OK status; temporary contents may be partial.
 *
 * @ingroup expression_engine
 *
 * Owns a second operand temporary for this recursive invocation and frees it on every exit. Stops at
 * END, CLOSE_PAREN, COMMA or an operator whose precedence is at most prec; token/nextPtr describe the
 * terminator already scanned. Evaluates both operands before applying operators, including logical
 * operators. Errors can leave partial temporary data and Tcl side effects.
 */
static int NextValue(Tcl_Interp *interp, ParseInfo *parsePtr, int prec, Value *valuePtr) {
    Value value2; /* Second operand for current operator.  */
    int operator; /* Current operator (either unary or binary). */
    /* Non-zero means already lexed the operator
     * (while picking up value for unary operator).
     * Don't lex again. */
    int gotOp;
    int result;
    VectorObject *vPtr, *v2Ptr;
    Tcl_Size i;

    /*
     * There are two phases to this procedure.  First, pick off an initial
     * value.  Then, parse (binary operator, value) pairs until done.
     */

    vPtr = valuePtr->vPtr;
    v2Ptr = Rbc_VectorNew(vPtr->dataPtr);
    gotOp = FALSE;
    value2.vPtr = v2Ptr;
    result = NextToken(interp, parsePtr, valuePtr);
    if (result != TCL_OK) {
        goto done;
    }
    if (parsePtr->token == OPEN_PAREN) {
        /* Parenthesized sub-expression. */
        result = NextValue(interp, parsePtr, -1, valuePtr);
        if (result != TCL_OK) {
            goto done;
        }
        if (parsePtr->token != CLOSE_PAREN) {
            Rbc_AppendResultStrings(interp, "unmatched parentheses in expression \"", parsePtr->expr, "\"", (char *)NULL);
            result = TCL_ERROR;
            goto done;
        }
    } else {
        if (parsePtr->token == MINUS) {
            parsePtr->token = UNARY_MINUS;
        }
        if (parsePtr->token >= UNARY_MINUS) {
            operator= parsePtr->token;
        result = NextValue(interp, parsePtr, precTable[operator], valuePtr);
        if (result != TCL_OK) {
            goto done;
        }
        gotOp = TRUE;
        /* Process unary operators. */
        switch (operator) {
        case UNARY_MINUS:
            if (vPtr->type == RBC_VECTOR_COMPLEX) {
                for (i = 0; i < vPtr->length; i++) {
                    vPtr->data.complex[i].real = -vPtr->data.complex[i].real;
                    vPtr->data.complex[i].imag = -vPtr->data.complex[i].imag;
                }
            } else {
                for (i = 0; i < vPtr->length; i++) {
                    vPtr->data.real[i] = -vPtr->data.real[i];
                }
            }
            break;
        case NOT:
            if (vPtr->type == RBC_VECTOR_COMPLEX) {
                if (ApplyComplexNotOperator(interp, vPtr) != TCL_OK) {
                    goto error;
                }
            } else {
                for (i = 0; i < vPtr->length; i++) {
                    vPtr->data.real[i] = (double)(!vPtr->data.real[i]);
                }
            }
            break;
        default:
            Rbc_AppendResultStrings(interp, "unknown operator", (char *)NULL);
            goto error;
        }
        } else if (parsePtr->token != VALUE) {
            Rbc_AppendResultStrings(interp, "missing operand", (char *)NULL);
            goto error;
        }
    }
    if (!gotOp) {
        result = NextToken(interp, parsePtr, &value2);
        if (result != TCL_OK) {
            goto done;
        }
    }
    /*
     * Got the first operand.  Now fetch (operator, operand) pairs.
     */
    for (;;) {
        operator= parsePtr->token;
        if ((operator<MULT) || (operator>= UNARY_MINUS)) {
            if ((operator== END) || (operator== CLOSE_PAREN) || (operator== COMMA)) {
                result = TCL_OK;
                goto done;
            } else {
                Rbc_AppendResultStrings(interp, "bad operator", (char *)NULL);
                goto error;
            }
        }
    if (precTable[operator] <= prec) {
        result = TCL_OK;
        goto done;
    }
    result = NextValue(interp, parsePtr, precTable[operator], &value2);
    if (result != TCL_OK) {
        goto done;
    }
    if ((parsePtr->token < MULT) && (parsePtr->token != VALUE) && (parsePtr->token != END) &&
        (parsePtr->token != CLOSE_PAREN) && (parsePtr->token != COMMA)) {
        Rbc_AppendResultStrings(interp, "unexpected token in expression", (char *)NULL);
        goto error;
    }
    /*
     * At this point we have two vectors and an operator.
     */
    if ((vPtr->type == RBC_VECTOR_COMPLEX) || (v2Ptr->type == RBC_VECTOR_COMPLEX)) {
        result = ApplyComplexBinaryOperator(interp, operator, vPtr, v2Ptr);
        if (result != TCL_OK) {
            goto done;
        }
    } else if (v2Ptr->length == 1) {
        double *opnd;
        double scalar;

        /*
         * 2nd operand is a scalar.
         */
        scalar = v2Ptr->data.real[0];
        opnd = vPtr->data.real;
        switch (operator) {
        case MULT:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] *= scalar;
            }
            break;
        case DIVIDE:
            if (scalar == 0.0) {
                Rbc_AppendResultStrings(interp, "divide by zero", (char *)NULL);
                goto error;
            }
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] /= scalar;
            }
            break;
        case PLUS:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] += scalar;
            }
            break;
        case MINUS:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] -= scalar;
            }
            break;
        case EXPONENT:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = pow(opnd[i], scalar);
            }
            break;
        case MOD:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = Fmod(opnd[i], scalar);
            }
            break;
        case LESS:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] < scalar);
            }
            break;
        case GREATER:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] > scalar);
            }
            break;
        case LEQ:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] <= scalar);
            }
            break;
        case GEQ:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] >= scalar);
            }
            break;
        case EQUAL:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] == scalar);
            }
            break;
        case NEQ:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] != scalar);
            }
            break;
        case AND:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] && scalar);
            }
            break;
        case OR:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] || scalar);
            }
            break;
        case LEFT_SHIFT:
        case RIGHT_SHIFT:
            if (RotateExpressionVector(interp, vPtr, operator, scalar) != TCL_OK) {
                goto error;
            }
            break;
        default:
            Rbc_AppendResultStrings(interp, "unknown operator in expression", (char *)NULL);
            goto error;
        }

    } else if (vPtr->length == 1) {
        double *opnd;
        double scalar;

        /*
         * 1st operand is a scalar.
         */
        scalar = vPtr->data.real[0];
        if (Rbc_VectorDuplicate(vPtr, v2Ptr) != TCL_OK) {
            return TCL_ERROR;
        }
        opnd = vPtr->data.real;
        switch (operator) {
        case MULT:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] *= scalar;
            }
            break;
        case PLUS:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] += scalar;
            }
            break;
        case DIVIDE:
            for (i = 0; i < vPtr->length; i++) {
                if (opnd[i] == 0.0) {
                    Rbc_AppendResultStrings(interp, "divide by zero", (char *)NULL);
                    goto error;
                }
                opnd[i] = (scalar / opnd[i]);
            }
            break;
        case MINUS:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = scalar - opnd[i];
            }
            break;
        case EXPONENT:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = pow(scalar, opnd[i]);
            }
            break;
        case MOD:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = Fmod(scalar, opnd[i]);
            }
            break;
        case LESS:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(scalar < opnd[i]);
            }
            break;
        case GREATER:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(scalar > opnd[i]);
            }
            break;
        case LEQ:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(scalar >= opnd[i]);
            }
            break;
        case GEQ:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(scalar <= opnd[i]);
            }
            break;
        case EQUAL:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] == scalar);
            }
            break;
        case NEQ:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] != scalar);
            }
            break;
        case AND:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] && scalar);
            }
            break;
        case OR:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] = (double)(opnd[i] || scalar);
            }
            break;

        case LEFT_SHIFT:
        case RIGHT_SHIFT:
            Rbc_AppendResultStrings(interp, "second shift operand must be scalar", (char *)NULL);
            goto error;
        default:
            Rbc_AppendResultStrings(interp, "unknown operator in expression", (char *)NULL);
            goto error;
        }
    } else {
        double *opnd1, *opnd2;
        /*
         * Carry out the function of the specified operator.
         */
        if (vPtr->length != v2Ptr->length) {
            Rbc_AppendResultStrings(interp, "vectors are different lengths", (char *)NULL);
            goto error;
        }
        opnd1 = vPtr->data.real, opnd2 = v2Ptr->data.real;
        switch (operator) {
        case MULT:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] *= opnd2[i];
            }
            break;
        case DIVIDE:
            for (i = 0; i < vPtr->length; i++) {
                if (opnd2[i] == 0.0) {
                    Rbc_AppendResultStrings(interp, "can't divide by 0.0 vector component", (char *)NULL);
                    goto error;
                }
                opnd1[i] /= opnd2[i];
            }
            break;
        case PLUS:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] += opnd2[i];
            }
            break;
        case MINUS:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] -= opnd2[i];
            }
            break;
        case MOD:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = Fmod(opnd1[i], opnd2[i]);
            }
            break;
        case EXPONENT:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = pow(opnd1[i], opnd2[i]);
            }
            break;
        case LESS:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = (double)(opnd1[i] < opnd2[i]);
            }
            break;
        case GREATER:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = (double)(opnd1[i] > opnd2[i]);
            }
            break;
        case LEQ:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = (double)(opnd1[i] <= opnd2[i]);
            }
            break;
        case GEQ:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = (double)(opnd1[i] >= opnd2[i]);
            }
            break;

        case EQUAL:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = (double)(opnd1[i] == opnd2[i]);
            }
            break;
        case NEQ:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = (double)(opnd1[i] != opnd2[i]);
            }
            break;
        case AND:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = (double)(opnd1[i] && opnd2[i]);
            }
            break;
        case OR:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] = (double)(opnd1[i] || opnd2[i]);
            }
            break;
        case LEFT_SHIFT:
        case RIGHT_SHIFT:
            Rbc_AppendResultStrings(interp, "second shift operand must be scalar", (char *)NULL);
            goto error;
        default:
            Rbc_AppendResultStrings(interp, "unknown operator in expression", (char *)NULL);
            goto error;
        }
    }
    }

done:
    Rbc_VectorFree(v2Ptr);
    return result;

error:
    Rbc_VectorFree(v2Ptr);
    return TCL_ERROR;
}

/**
 * @brief Set an arithmetic message and structured Tcl error code.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] className Borrowed NUL-terminated arithmetic error class, copied into the error-code list.
 * @param[in] messageObj Message object retained by the Tcl result and error code; a new reference-count-zero
 *     object is valid.
 *
 * @ingroup expression_engine
 *
 * Installs messageObj as result and as the third element of the ARITH/class/message error-code list.
 * Accepts a newly created message object; Tcl result/error-code references retain it. Does not raise a
 * completion code itself.
 */
static void SetMathError(Tcl_Interp *interp, const char *className, Tcl_Obj *messageObj) {
    Tcl_Obj *codeObj;
    Tcl_Obj *codeObjv[3];

    codeObjv[0] = Tcl_NewStringObj("ARITH", -1);
    codeObjv[1] = Tcl_NewStringObj(className, -1);
    codeObjv[2] = messageObj;
    codeObj = Tcl_NewListObj(3, codeObjv);
    Tcl_SetObjResult(interp, messageObj);
    Tcl_SetObjErrorCode(interp, codeObj);
}

/**
 * @brief Classify a floating-point failure using errno and the value.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] value Failed arithmetic value, interpreted together with the current errno; errno is not reset here.
 *
 * @ingroup expression_engine
 *
 * Reports DOMAIN for EDOM or NaN, UNDERFLOW/OVERFLOW for ERANGE or infinity, otherwise UNKNOWN. Reads
 * but does not clear errno. Callers that depend on errno must reset it before the operation.
 */
static void MathError(Tcl_Interp *interp, double value) {
    Tcl_Obj *messageObj;
    const char *className;

    if ((errno == EDOM) || (value != value)) {
        className = "DOMAIN";
        messageObj = Tcl_NewStringObj("domain error: "
                                      "argument not in valid range",
                                      -1);
    } else if ((errno == ERANGE) || IS_INF(value)) {
        if (value == 0.0) {
            className = "UNDERFLOW";
            messageObj = Tcl_NewStringObj("floating-point value "
                                          "too small to represent",
                                          -1);
        } else {
            className = "OVERFLOW";
            messageObj = Tcl_NewStringObj("floating-point value "
                                          "too large to represent",
                                          -1);
        }
    } else {
        className = "UNKNOWN";
        messageObj = Tcl_ObjPrintf("unknown floating-point error, "
                                   "errno = %d",
                                   errno);
    }
    SetMathError(interp, className, messageObj);
}

/**
 * @brief Convert a retained Tcl result into an expression value.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return Conversion status after clearing and parsing the retained interpreter result.
 *
 * @ingroup expression_engine
 *
 * Increments the result object reference before clearing the interpreter result, so conversion cannot
 * invalidate its borrowed string. Releases the reference afterward and returns ParseString status.
 * Does not retain arbitrary vector identities resolved by the conversion.
 */
static int ParseSubstitutionResult(Tcl_Interp *interp, Value *valuePtr) {
    Tcl_Obj *resultObj;
    const char *string;
    int result;

    /*
     * Keep the result object alive while ParseString() works.
     * ParseString() may replace the interpreter result itself.
     */
    resultObj = Tcl_GetObjResult(interp);
    Tcl_IncrRefCount(resultObj);
    string = Tcl_GetString(resultObj);
    Tcl_ResetResult(interp);
    result = ParseString(interp, string, valuePtr);
    Tcl_DecrRefCount(resultObj);
    return result;
}

/**
 * @brief Locate a matching close bracket and evaluate the enclosed Tcl script.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] string NUL-terminated buffer starting immediately after the opening bracket.
 * @param[out] termPtr Required borrowed cursor output after the closing bracket; may be set before script failure.
 * @return Parsing error or the unchanged Tcl_EvalEx completion code, not necessarily only TCL_OK/TCL_ERROR.
 *
 * @ingroup expression_engine
 *
 * Input begins after the opening bracket. Uses Tcl_ParseCommand repeatedly to respect command syntax,
 * writes termPtr after the closing bracket once found, then returns Tcl_EvalEx completion unchanged.
 * Missing terminator reports an error. The caller provides valid input and interpreter lifetime
 * through evaluation.
 */
static int ParseBracketScript(Tcl_Interp *interp, const char *string, const char **termPtr) {
    Tcl_Parse parse;
    const char *scanPtr;
    const char *commandEnd;
    const char *closePtr;
    Tcl_Size scriptLength;
    int result;

    scanPtr = string;
    closePtr = NULL;
    for (;;) {
        result = Tcl_ParseCommand(interp, scanPtr, -1, 1, &parse);
        if (result != TCL_OK) {
            return result;
        }
        if (parse.commandSize <= 0) {
            Tcl_FreeParse(&parse);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("missing close-bracket", -1));
            return TCL_ERROR;
        }
        commandEnd = parse.commandStart + parse.commandSize;
        if ((commandEnd > parse.commandStart) && (commandEnd[-1] == ']')) {
            closePtr = commandEnd - 1;
            Tcl_FreeParse(&parse);
            break;
        }
        Tcl_FreeParse(&parse);
        if (*commandEnd == '\0') {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("missing close-bracket", -1));
            return TCL_ERROR;
        }
        scanPtr = commandEnd;
    }
    *termPtr = closePtr + 1;
    scriptLength = (Tcl_Size)(closePtr - string);
    return Tcl_EvalEx(interp, string, scriptLength, 0);
}

/**
 * @brief Evaluate bracketed Tcl and convert its result to numeric storage.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] string NUL-terminated token buffer starting immediately after the opening bracket.
 * @param[out] termPtr Required borrowed cursor after the matching delimiter; may be written before later
 *     evaluation failure.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return Non-OK parsing/evaluation completion, or the numeric conversion status after successful evaluation.
 *
 * @ingroup expression_engine
 *
 * Delegates bracket parsing/evaluation and then ParseSubstitutionResult. Non-OK Tcl completion
 * propagates before numeric conversion.
 */
static int ParseBracketValue(Tcl_Interp *interp, const char *string, const char **termPtr, Value *valuePtr) {
    int result = ParseBracketScript(interp, string, termPtr);
    if (result != TCL_OK) {
        return result;
    }
    return ParseSubstitutionResult(interp, valuePtr);
}

/**
 * @brief Parse and evaluate a quoted Tcl token sequence.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] string NUL-terminated token buffer starting at the opening double quote.
 * @param[out] termPtr Required borrowed cursor after the matching delimiter; may be written before later
 *     evaluation failure.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return Non-OK parsing/evaluation completion, or the numeric conversion status after successful evaluation.
 *
 * @ingroup expression_engine
 *
 * Input includes the opening quote. Uses Tcl parsing/substitution rules, releases parse storage after
 * token evaluation, then converts the retained result. termPtr follows the closing quote on successful
 * parsing; substitutions may execute Tcl.
 */
static int ParseQuotedValue(Tcl_Interp *interp, const char *string, const char **termPtr, Value *valuePtr) {
    Tcl_Parse parse;
    int result;

    result = Tcl_ParseQuotedString(interp, string, -1, &parse, 0, termPtr);
    if (result != TCL_OK) {
        return result;
    }
    result = Tcl_EvalTokensStandard(interp, parse.tokenPtr, parse.numTokens);
    Tcl_FreeParse(&parse);
    if (result != TCL_OK) {
        return result;
    }
    return ParseSubstitutionResult(interp, valuePtr);
}

/**
 * @brief Parse a braced Tcl value and convert it to expression storage.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] string NUL-terminated token buffer starting at the opening brace.
 * @param[out] termPtr Required borrowed cursor after the matching delimiter; may be written before later
 *     evaluation failure.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return Non-OK parsing/evaluation completion, or the numeric conversion status after successful evaluation.
 *
 * @ingroup expression_engine
 *
 * Input includes the opening brace. Uses Tcl_ParseBraces and evaluates its tokens, preserving brace
 * substitution rules including backslash-newline handling. Converts through ParseSubstitutionResult
 * after freeing successful parse storage.
 */
static int ParseBracedValue(Tcl_Interp *interp, const char *string, const char **termPtr, Value *valuePtr) {
    Tcl_Parse parse;
    int result;

    result = Tcl_ParseBraces(interp, string, -1, &parse, 0, termPtr);
    if (result != TCL_OK) {
        return result;
    }
    result = Tcl_EvalTokensStandard(interp, parse.tokenPtr, parse.numTokens);
    Tcl_FreeParse(&parse);
    if (result != TCL_OK) {
        return result;
    }
    return ParseSubstitutionResult(interp, valuePtr);
}

/**
 * @brief Scan one operator, delimiter or numeric/vector value.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] parsePtr Parser cursor and token state borrowing a live writable expression buffer.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return TCL_OK after scanning; VALUE fills the temporary, while operators/delimiters need not change it.
 *
 * @ingroup expression_engine
 *
 * Advances nextPtr and records token; VALUE fills the supplied temporary. Bare numbers use
 * SetExpressionRealScalar. Substitutions use ParseString, while other names first try function lookup
 * then vector/range lookup and copying. Variable traces, commands and map functions can execute Tcl;
 * this scanner has no independent interpreter lifetime guard.
 */
static int NextToken(Tcl_Interp *interp, ParseInfo *parsePtr, Value *valuePtr) {
    char *p;
    const char *termPtr;    
    char *endPtr;
    const char *var;
    const char *vectorEndPtr;
    int result;

    p = parsePtr->nextPtr;
    while (isspace(UCHAR(*p))) {
        p++;
    }
    if (*p == '\0') {
        parsePtr->token = END;
        parsePtr->nextPtr = p;
        return TCL_OK;
    }
    /*
     * Try to parse the token as a floating-point number. But check
     * that the first character isn't a "-" or "+", which "strtod"
     * will happily accept as an unary operator.  Otherwise, we might
     * accidently treat a binary operator as unary by mistake, which
     * will eventually cause a syntax error.
     */
    if ((*p != '-') && (*p != '+')) {
        double value;

        errno = 0;
        value = strtod(p, &endPtr);
        if (endPtr != p) {
            if (errno != 0) {
                MathError(interp, value);
                return TCL_ERROR;
            }
            parsePtr->token = VALUE;
            parsePtr->nextPtr = endPtr;

            /*
             * Save the single floating-point value as an 1-component vector.
             */
            return SetExpressionRealScalar(valuePtr->vPtr, value);
        }
    }
    parsePtr->nextPtr = p + 1;
    switch (*p) {
    case '$':
        parsePtr->token = VALUE;
        /* (const char **) */
        var = Tcl_ParseVar(interp, p, (const char **)&endPtr);
        if (var == NULL) {
            return TCL_ERROR;
        }
        parsePtr->nextPtr = endPtr;
        Tcl_ResetResult(interp);
        result = ParseString(interp, var, valuePtr);
        return result;
    case '[':
        parsePtr->token = VALUE;
        result = ParseBracketValue(interp, p + 1, &termPtr, valuePtr);
        if (result != TCL_OK) {
            return result;
        }
        parsePtr->nextPtr = (char *)termPtr;
        return TCL_OK;
    case '"':
        parsePtr->token = VALUE;
        result = ParseQuotedValue(interp, p, &termPtr, valuePtr);
        if (result != TCL_OK) {
            return result;
        }
        parsePtr->nextPtr = (char *)termPtr;
        return TCL_OK;
    case '{':
        parsePtr->token = VALUE;
        result = ParseBracedValue(interp, p, &termPtr, valuePtr);
        if (result != TCL_OK) {
            return result;
        }
        parsePtr->nextPtr = (char *)termPtr;
        return TCL_OK;
    case '(':
        parsePtr->token = OPEN_PAREN;
        break;
    case ')':
        parsePtr->token = CLOSE_PAREN;
        break;
    case ',':
        parsePtr->token = COMMA;
        break;
    case '*':
        parsePtr->token = MULT;
        break;
    case '/':
        parsePtr->token = DIVIDE;
        break;
    case '%':
        parsePtr->token = MOD;
        break;
    case '+':
        parsePtr->token = PLUS;
        break;
    case '-':
        parsePtr->token = MINUS;
        break;
    case '^':
        parsePtr->token = EXPONENT;
        break;
    case '<':
        switch (*(p + 1)) {
        case '<':
            parsePtr->nextPtr = p + 2;
            parsePtr->token = LEFT_SHIFT;
            break;
        case '=':
            parsePtr->nextPtr = p + 2;
            parsePtr->token = LEQ;
            break;
        default:
            parsePtr->token = LESS;
            break;
        }
        break;
    case '>':
        switch (*(p + 1)) {
        case '>':
            parsePtr->nextPtr = p + 2;
            parsePtr->token = RIGHT_SHIFT;
            break;
        case '=':
            parsePtr->nextPtr = p + 2;
            parsePtr->token = GEQ;
            break;
        default:
            parsePtr->token = GREATER;
            break;
        }
        break;
    case '=':
        if (*(p + 1) == '=') {
            parsePtr->nextPtr = p + 2;
            parsePtr->token = EQUAL;
        } else {
            parsePtr->token = UNKNOWN;
        }
        break;
    case '&':
        if (*(p + 1) == '&') {
            parsePtr->nextPtr = p + 2;
            parsePtr->token = AND;
        } else {
            parsePtr->token = UNKNOWN;
        }
        break;
    case '|':
        if (*(p + 1) == '|') {
            parsePtr->nextPtr = p + 2;
            parsePtr->token = OR;
        } else {
            parsePtr->token = UNKNOWN;
        }
        break;
    case '!':
        if (*(p + 1) == '=') {
            parsePtr->nextPtr = p + 2;
            parsePtr->token = NEQ;
        } else {
            parsePtr->token = NOT;
        }
        break;
    default:
        parsePtr->token = VALUE;
        result = ParseMathFunction(interp, p, parsePtr, valuePtr);
        if ((result == TCL_OK) || (result == TCL_ERROR)) {
            return result;
        } else {
            VectorObject *vPtr;

            while (isspace(UCHAR(*p))) {
                p++; /* Skip spaces leading the vector name. */
            }
            vPtr = Rbc_VectorParseElement(interp, valuePtr->vPtr->dataPtr, p, &vectorEndPtr, NS_SEARCH_BOTH);
            if (vPtr == NULL) {
                return TCL_ERROR;
            }
            if (CopyExpressionVector(valuePtr->vPtr, vPtr) != TCL_OK) {
                return TCL_ERROR;
            }
            parsePtr->nextPtr = (char *)vectorEndPtr;
        }
    }
    return TCL_OK;
}

/*
 * Fmod --
 *     Returns x mod y
 */
/**
 * @brief Compute the legacy floor-based real remainder.
 * @param[in] x Real dividend.
 * @param[in] y Real divisor; zero is handled by returning zero.
 * @return x-floor(x/y)*y for nonzero y, otherwise zero; this is not C fmod semantics.
 *
 * @ingroup expression_engine
 *
 * Returns zero for a zero divisor; otherwise x-floor(x/y)*y. This differs from C fmod for negative
 * operands. Does not report an error or validate finite input.
 */
static double Fmod(double x, double y) {
    if (y == 0.0) {
        return 0.0;
    }
    return x - (floor(x / y) * y);
}

/**
 * @brief Interpret substitution text as a scalar, complex pair or vector reference.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] string Borrowed NUL-terminated substitution text, kept valid through conversion and lookup.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return TCL_OK with copied scalar/pair/vector values, otherwise TCL_ERROR; arbitrary numeric lists are not
 *     vectors.
 *
 * @ingroup expression_engine
 *
 * Tries a fully consumed strtod number, then a two-element complex list, then a fully consumed
 * vector/range reference. Does not accept arbitrary numeric lists as vectors. The current real-number
 * branch uses the same real scalar setter as bare numeric tokens, clearing any complex type and
 * imaginary component left in a reused operand temporary.
 */
static int ParseString(Tcl_Interp *interp, const char *string, Value *valuePtr) {
    char *numberEndPtr;
    const char *vectorEndPtr;
    double value;

    errno = 0;
    value = strtod(string, &numberEndPtr);
    if ((numberEndPtr != string) && (*numberEndPtr == '\0')) {
        if (errno != 0) {
            Tcl_ResetResult(interp);
            MathError(interp, value);
            return TCL_ERROR;
        }
        return SetExpressionRealScalar(valuePtr->vPtr, value);
    }
    while (isspace(UCHAR(*string))) {
        string++;
    }
    {
        Tcl_Obj *objPtr;
        Tcl_Obj **objv;
        Tcl_Size objc;

        objPtr = Tcl_NewStringObj(string, -1);
        Tcl_IncrRefCount(objPtr);
        if ((Tcl_ListObjGetElements(NULL, objPtr, &objc, &objv) == TCL_OK) && (objc == 2)) {
            Rbc_Complex value;
            int result;

            result = Rbc_GetComplex(interp, objPtr, &value);
            if (result == TCL_OK) {
                result = SetExpressionComplexScalar(valuePtr->vPtr, value);
            }
            Tcl_DecrRefCount(objPtr);
            return result;
        }
        Tcl_DecrRefCount(objPtr);
    }
    {
        VectorObject *vPtr;
        vPtr = Rbc_VectorParseElement(interp, valuePtr->vPtr->dataPtr, string, &vectorEndPtr, NS_SEARCH_BOTH);
        if (vPtr == NULL) {
            return TCL_ERROR;
        }
        if (*vectorEndPtr != '\0') {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("extra characters after vector", -1));
            return TCL_ERROR;
        }
        return CopyExpressionVector(valuePtr->vPtr, vPtr);
    }
}

/**
 * @brief Parse and retain one Tcl command-prefix value.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] nextPtr Required cursor into a live expression buffer, advanced after successful prefix parsing.
 * @return Tcl object with one retained reference, released by the caller with Tcl_DecrRefCount, or NULL on
 *     failure.
 *
 * @ingroup expression_engine
 *
 * Accepts braces, quotes, variable or bracket substitution, or a bare word ending at
 * whitespace/comma/parenthesis. Substitutions run once in the current frame. Returns a reference-
 * counted object and advances nextPtr, or NULL on failure. The caller decrements the reference;
 * MapVector validates list structure and nonemptiness.
 */
static Tcl_Obj *ParseMapPrefix(Tcl_Interp *interp, const char **nextPtr) {
    const char *p = *nextPtr, *endPtr;
    Tcl_Obj *prefix;
    Tcl_Parse parse;
    int result;

    while (isspace(UCHAR(*p))) {
        p++;
    }
    if ((*p == '{') || (*p == '"')) {
        result = (*p == '{') ? Tcl_ParseBraces(interp, p, -1, &parse, 0, &endPtr)
                             : Tcl_ParseQuotedString(interp, p, -1, &parse, 0, &endPtr);
        if (result == TCL_OK) {
            result = Tcl_EvalTokensStandard(interp, parse.tokenPtr, parse.numTokens);
        }
        Tcl_FreeParse(&parse);
        if (result != TCL_OK) {
            return NULL;
        }
        prefix = Tcl_GetObjResult(interp);
    } else if (*p == '$') {
        const char *text = Tcl_ParseVar(interp, p, &endPtr);
        if (text == NULL) {
            return NULL;
        }
        prefix = Tcl_NewStringObj(text, -1);
    } else if (*p == '[') {
        if (ParseBracketScript(interp, p + 1, &endPtr) != TCL_OK) {
            return NULL;
        }
        prefix = Tcl_GetObjResult(interp);
    } else {
        endPtr = p;
        while (*endPtr && !isspace(UCHAR(*endPtr)) && (*endPtr != ',') && (*endPtr != '(') && (*endPtr != ')')) {
            endPtr++;
        }
        prefix = Tcl_NewStringObj(p, endPtr - p);
    }
    Tcl_IncrRefCount(prefix);
    *nextPtr = endPtr;
    return prefix;
}

/**
 * @brief Apply a Tcl command prefix to every value of a detached temporary.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in] prefix Borrowed nonempty Tcl command-prefix list, kept alive through extraction of its words.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK after replacing the temporary with complete finite callback results, otherwise TCL_ERROR.
 * Callback side effects remain; callback completion codes other than TCL_OK are converted to errors.
 *
 * @ingroup expression_engine
 *
 * Retains individual prefix words against recursive shimmering and appends one real or complex-pair
 * argument per invocation. Builds a separate zero-initialized result, promoting on the first pair
 * return. Requires finite numeric results; empty input yields an empty real result with no calls. Tcl
 * errors gain an element index, other completion codes become MAP COMPLETION errors, and invalid
 * results become MAP VALUE errors. Frees argument references and partial results on failure; callback
 * side effects remain.
 */
static int MapVector(Tcl_Interp *interp, Tcl_Obj *prefix, VectorObject *vPtr) {
    Tcl_Obj **prefixObjv, **objv;
    Tcl_Size objc, i, j;
    VectorObject *resultPtr;
    int result = TCL_ERROR;

    if (Tcl_ListObjGetElements(interp, prefix, &objc, &prefixObjv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("map command prefix must not be empty", -1));
        return TCL_ERROR;
    }
    if ((size_t)objc >= SIZE_MAX / sizeof(Tcl_Obj *)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("map command prefix is too large", -1));
        return TCL_ERROR;
    }
    objv = Tcl_AttemptAlloc(((size_t)objc + 1) * sizeof(Tcl_Obj *));
    if (objv == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate map command arguments", -1));
        return TCL_ERROR;
    }
    /* Keep our own references: recursive Tcl evaluation can shimmer the prefix object. */
    for (j = 0; j < objc; j++) {
        objv[j] = prefixObjv[j];
        Tcl_IncrRefCount(objv[j]);
    }
    resultPtr = Rbc_VectorNew(vPtr->dataPtr);
    if (Rbc_VectorChangeLength(resultPtr, vPtr->length) != TCL_OK) {
        goto done;
    }
    /* Promotion copies the whole allocation, including elements not visited yet. */
    for (i = 0; i < resultPtr->length; i++) {
        resultPtr->data.real[i] = 0.0;
    }
    for (i = 0; i < vPtr->length; i++) {
        Tcl_Obj *value, **parts;
        Tcl_Size count;
        Rbc_Complex number;
        int pair = FALSE;
        int code;

        objv[objc] = Rbc_NewVectorValueObj(vPtr, i);
        Tcl_IncrRefCount(objv[objc]);
        code = Tcl_EvalObjv(interp, objc + 1, objv, 0);
        Tcl_DecrRefCount(objv[objc]);
        if (code != TCL_OK) {
            if (code != TCL_ERROR) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("map callback returned Tcl completion code %d", code));
                Tcl_SetErrorCode(interp, "RBC", "VECTOR", "MAP", "COMPLETION", NULL);
            }
            goto elementError;
        }
        value = Tcl_GetObjResult(interp);
        number.imag = 0.0;
        if (Tcl_GetDoubleFromObj(NULL, value, &number.real) != TCL_OK) {
            if ((Tcl_ListObjGetElements(NULL, value, &count, &parts) != TCL_OK) || (count != 2) ||
                (Tcl_GetDoubleFromObj(NULL, parts[0], &number.real) != TCL_OK) ||
                (Tcl_GetDoubleFromObj(NULL, parts[1], &number.imag) != TCL_OK)) {
                Tcl_SetObjResult(
                    interp,
                    Tcl_ObjPrintf(
                        "map callback must return a number or {real imag} pair at element %" TCL_SIZE_MODIFIER "d", i));
                Tcl_SetErrorCode(interp, "RBC", "VECTOR", "MAP", "VALUE", NULL);
                goto elementError;
            }
            pair = TRUE;
        }
        if (!ComplexValueIsFinite(number)) {
            Tcl_SetObjResult(
                interp,
                Tcl_ObjPrintf("map callback returned a non-finite number at element %" TCL_SIZE_MODIFIER "d", i));
            Tcl_SetErrorCode(interp, "RBC", "VECTOR", "MAP", "VALUE", NULL);
            goto elementError;
        }
        if (pair && (PromoteExpressionVectorToComplex(resultPtr) != TCL_OK)) {
            goto elementError;
        }
        if (resultPtr->type == RBC_VECTOR_COMPLEX) {
            resultPtr->data.complex[i] = number;
        } else {
            resultPtr->data.real[i] = number.real;
        }
        continue;

    elementError:
        Tcl_AppendObjToErrorInfo(interp,
                                 Tcl_ObjPrintf("\n    (vector map callback at element %" TCL_SIZE_MODIFIER "d)", i));
        goto done;
    }
    result = CopyExpressionVector(vPtr, resultPtr);
    if (result == TCL_OK) {
        Tcl_ResetResult(interp);
    }

done:
    Rbc_VectorFree(resultPtr);
    for (j = 0; j < objc; j++) {
        Tcl_DecrRefCount(objv[j]);
    }
    Tcl_Free(objv);
    return result;
}

/**
 * @brief Parse map(prefix, expression) before invoking element callbacks.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] parsePtr Parser positioned after map's opening parenthesis; success leaves token VALUE.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return TCL_OK after argument evaluation and mapping, otherwise TCL_ERROR.
 *
 * @ingroup expression_engine
 *
 * Retains the parsed prefix while evaluating the complete second argument. Requires comma and closing
 * parenthesis; successful MapVector changes token to VALUE. Releases the prefix on every exit. Prefix
 * and numeric-argument evaluation precede the per-element callbacks.
 */
static int ParseMapFunction(Tcl_Interp *interp, ParseInfo *parsePtr, Value *valuePtr) {
    const char *p = parsePtr->nextPtr;
    Tcl_Obj *prefix = ParseMapPrefix(interp, &p);
    int result = TCL_ERROR;

    if (prefix == NULL) {
        return TCL_ERROR;
    }
    while (isspace(UCHAR(*p))) {
        p++;
    }
    if (*p != ',') {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("expected map(commandPrefix, expression)", -1));
        goto done;
    }
    parsePtr->nextPtr = (char *)p + 1;
    if (NextValue(interp, parsePtr, -1, valuePtr) != TCL_OK) {
        goto done;
    }
    if (parsePtr->token != CLOSE_PAREN) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("expected closing parenthesis after map expression", -1));
        goto done;
    }
    result = MapVector(interp, prefix, valuePtr->vPtr);
    if (result == TCL_OK) {
        parsePtr->token = VALUE;
    }

done:
    Tcl_DecrRefCount(prefix);
    return result;
}

/**
 * @brief Try function syntax before falling back to vector-name parsing.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] start Writable expression span starting at a candidate function name; temporary NUL edits are
 *     restored.
 * @param[in,out] parsePtr Live parser state; success advances past the function argument and sets token VALUE.
 * @param[in,out] valuePtr Caller-owned Value containing a live evaluator temporary; caller releases it on every
 *     outcome.
 * @return TCL_OK for a completed function, TCL_RETURN for no recognized function, or TCL_ERROR on evaluation
 *     failure.
 *
 * @ingroup expression_engine
 *
 * Requires a writable input string: temporarily replaces the opening parenthesis with NUL during
 * lookup, then restores it. Recognizes map specially; ordinary names use the interpreter's math table.
 * TCL_RETURN means no recognized function, not a script return. Parses one expression argument and
 * invokes its adapter after the close parenthesis. The hash entry is borrowed across argument
 * evaluation; this helper does not guard registry teardown during callbacks.
 */
static int ParseMathFunction(Tcl_Interp *interp, char *start, ParseInfo *parsePtr, Value *valuePtr) {
    Tcl_HashEntry *hPtr;
    MathFunction *mathPtr; /* Info about math function. */
    char *p;
    VectorInterpData *dataPtr; /* Interpreter-specific data. */

    /*
     * Find the end of the math function's name and lookup the
     * record for the function.
     */
    p = start;
    while (isspace(UCHAR(*p))) {
        p++;
    }
    parsePtr->nextPtr = p;
    while (isalnum(UCHAR(*p)) || (*p == '_')) {
        p++;
    }
    if (*p != '(') {
        return TCL_RETURN; /* Must start with open parenthesis */
    }
    if (((p - parsePtr->nextPtr) == 3) && (strncmp(parsePtr->nextPtr, "map", 3) == 0)) {
        parsePtr->nextPtr = p + 1;
        return ParseMapFunction(interp, parsePtr, valuePtr);
    }
    dataPtr = valuePtr->vPtr->dataPtr;
    *p = '\0';
    hPtr = Tcl_FindHashEntry(&(dataPtr->mathProcTable), parsePtr->nextPtr);
    *p = '(';
    if (hPtr == NULL) {
        return TCL_RETURN; /* Name doesn't match any known function */
    }
    /* Pick up the single value as the argument to the function */
    parsePtr->token = OPEN_PAREN;
    parsePtr->nextPtr = p + 1;
    if (NextValue(interp, parsePtr, -1, valuePtr) != TCL_OK) {
        return TCL_ERROR; /* Parse error */
    }
    if (parsePtr->token != CLOSE_PAREN) {
        Rbc_AppendResultStrings(interp, "unmatched parentheses in expression \"", parsePtr->expr, "\"", (char *)NULL);
        return TCL_ERROR; /* Missing right parenthesis */
    }
    mathPtr = (MathFunction *)Tcl_GetHashValue(hPtr);
    if ((*mathPtr->proc)(mathPtr->clientData, interp, valuePtr->vPtr) != TCL_OK) {
        return TCL_ERROR; /* Function invocation error */
    }
    parsePtr->token = VALUE;
    return TCL_OK;
}

/**
 * @brief Apply a real component function to finite selected values.
 * @param[in] clientData Borrowed ComponentProc function pointer, valid throughout synchronous dispatch; never
 *     freed here.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK on completion, otherwise TCL_ERROR; the adapter-specific contract defines partial mutation and
 *     finite checks.
 *
 * @ingroup expression_engine
 *
 * Rejects complex input, resets errno and walks First/Next. Writes each result before errno/finite
 * validation; failure can leave a partially transformed temporary. Does not notify or commit named
 * output.
 */
static int ComponentFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr) {
    ComponentProc *procPtr = (ComponentProc *)clientData;
    Tcl_Size i;

    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("math function is not supported for complex vectors yet", -1));
        return TCL_ERROR;
    }
    errno = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        vPtr->data.real[i] = (*procPtr)(vPtr->data.real[i]);
        if (errno != 0) {
            MathError(interp, vPtr->data.real[i]);
            return TCL_ERROR;
        }
        if (!FINITE(vPtr->data.real[i])) {
            /*
             * IEEE floating-point error.
             */
            MathError(interp, vPtr->data.real[i]);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

/**
 * @brief Apply paired real/complex component implementations.
 * @param[in] clientData Borrowed ComplexComponentFunction descriptor, valid throughout synchronous dispatch; never
 *     freed here.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK on completion, otherwise TCL_ERROR; the adapter-specific contract defines partial mutation and
 *     finite checks.
 *
 * @ingroup expression_engine
 *
 * Real input delegates to ComponentFunc. Complex input visits all used values, resets errno for each
 * call, validates both components and stores each successful result. Errors can leave earlier elements
 * changed.
 */
static int ComplexComponentFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr) {
    ComplexComponentFunction *functionPtr;
    Tcl_Size i;

    functionPtr = (ComplexComponentFunction *)clientData;
    if (vPtr->type == RBC_VECTOR_REAL) {
        return ComponentFunc((ClientData)functionPtr->realProc, interp, vPtr);
    }
    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    for (i = 0; i < vPtr->length; i++) {
        Rbc_Complex value;

        errno = 0;
        value = (*functionPtr->complexProc)(vPtr->data.complex[i]);
        if (errno != 0) {
            double errorValue;

            if (!FINITE(value.real)) {
                errorValue = value.real;
            } else if (!FINITE(value.imag)) {
                errorValue = value.imag;
            } else {
                errorValue = value.real;
            }
            MathError(interp, errorValue);
            return TCL_ERROR;
        }
        if (!FINITE(value.real)) {
            MathError(interp, value.real);
            return TCL_ERROR;
        }
        if (!FINITE(value.imag)) {
            MathError(interp, value.imag);
            return TCL_ERROR;
        }
        vPtr->data.complex[i] = value;
    }
    return TCL_OK;
}

/**
 * @brief Reduce a real temporary to one real value.
 * @param[in] clientData Borrowed ScalarProc function pointer, valid throughout synchronous dispatch; never freed
 *     here.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK on completion, otherwise TCL_ERROR; the adapter-specific contract defines partial mutation and
 *     finite checks.
 *
 * @ingroup expression_engine
 *
 * Rejects complex input, computes the scalar before resizing and checks errno. Stores one value on
 * successful resize; final finiteness is checked by EvaluateExpression. No named-vector notification
 * is sent.
 */
static int ScalarFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr) {
    double value;
    
    ScalarProc *procPtr = (ScalarProc *)clientData;
    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("math function is not supported for complex vectors yet", -1));
        return TCL_ERROR;
    }
    errno = 0;
    value = (*procPtr)(vPtr);
    if (errno != 0) {
        MathError(interp, value);
        return TCL_ERROR;
    }
    if (Rbc_VectorChangeLength(vPtr, 1) != TCL_OK) {
        return TCL_ERROR;
    }
    vPtr->data.real[0] = value;
    return TCL_OK;
}

/**
 * @brief Reduce a real or complex temporary while retaining its type.
 * @param[in] clientData Borrowed ComplexScalarFunction descriptor, valid throughout synchronous dispatch; never
 *     freed here.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK on completion, otherwise TCL_ERROR; the adapter-specific contract defines partial mutation and
 *     finite checks.
 *
 * @ingroup expression_engine
 *
 * Selects the corresponding reducer, computes before resizing to one, and reports errno failures. A
 * complex result remains a pair. Final nonfinite results are rejected by the enclosing evaluator.
 */
static int ComplexScalarFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr) {
    ComplexScalarFunction *functionPtr;

    functionPtr = (ComplexScalarFunction *)clientData;
    if (vPtr->type == RBC_VECTOR_REAL) {
        double value;

        errno = 0;
        value = (*functionPtr->realProc)((Rbc_Vector *)vPtr);
        if (errno != 0) {
            MathError(interp, value);
            return TCL_ERROR;
        }
        if (Rbc_VectorChangeLength(vPtr, 1) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->data.real[0] = value;
        return TCL_OK;
    }
    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    {
        Rbc_Complex value;

        errno = 0;
        value = (*functionPtr->complexProc)(vPtr);
        if (errno != 0) {
            double errorValue;

            if (!FINITE(value.real)) {
                errorValue = value.real;
            } else if (!FINITE(value.imag)) {
                errorValue = value.imag;
            } else {
                errorValue = value.real;
            }
            MathError(interp, errorValue);
            return TCL_ERROR;
        }
        /*
         * Compute the reduction before resizing: the source values
         * still live in vPtr at this point.
         */
        if (Rbc_VectorChangeLength(vPtr, 1) != TCL_OK) {
            return TCL_ERROR;
        }
        vPtr->data.complex[0] = value;
    }
    return TCL_OK;
}

/**
 * @brief Reduce either numeric type to one real result.
 * @param[in] clientData Borrowed ComplexRealScalarFunction descriptor, valid throughout synchronous dispatch;
 *     never freed here.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK on completion, otherwise TCL_ERROR; the adapter-specific contract defines partial mutation and
 *     finite checks.
 *
 * @ingroup expression_engine
 *
 * Computes the reduction before any type change or release, checks errno, then uses
 * SetExpressionRealScalar. Operates on evaluator temporaries only; final finiteness belongs to
 * EvaluateExpression.
 */
static int ComplexRealScalarFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr) {
    ComplexRealScalarFunction *functionPtr;
    double value;

    functionPtr = (ComplexRealScalarFunction *)clientData;
    errno = 0;
    if (vPtr->type == RBC_VECTOR_REAL) {
        value = (*functionPtr->realProc)((Rbc_Vector *)vPtr);
    } else {
        assert(vPtr->type == RBC_VECTOR_COMPLEX);
        /*
         * Compute before changing the temporary from complex
         * storage to real storage.
         */
        value = (*functionPtr->complexProc)(vPtr);
    }
    if (errno != 0) {
        MathError(interp, value);
        return TCL_ERROR;
    }
    return SetExpressionRealScalar(vPtr, value);
}

/**
 * @brief Invoke a real-only whole-vector function adapter.
 * @param[in] clientData Borrowed VectorProc function pointer, valid throughout synchronous dispatch; never freed
 *     here.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK on completion, otherwise TCL_ERROR; the adapter-specific contract defines partial mutation and
 *     finite checks.
 *
 * @ingroup expression_engine
 *
 * Rejects complex input and returns the selected function's Tcl status. The function owns its
 * mutation/allocation policy; this wrapper neither checks errno nor publishes notifications.
 */
static int VectorFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr) {
    VectorProc *procPtr = (VectorProc *)clientData;
    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("math function is not supported for complex vectors yet", -1));
        return TCL_ERROR;
    }
    return (*procPtr)(vPtr);
}

/**
 * @brief Convert component results from complex input into real temporary storage.
 * @param[in] clientData Borrowed ComplexRealFunction descriptor, valid throughout synchronous dispatch; never
 *     freed here.
 * @param[in] interp Evaluation/diagnostic interpreter; see @ref rbc_interp_argument.
 * @param[in,out] vPtr Exclusively owned evaluator temporary, not a registered named vector.
 * @return TCL_OK on completion, otherwise TCL_ERROR; the adapter-specific contract defines partial mutation and
 *     finite checks.
 *
 * @ingroup expression_engine
 *
 * Real input delegates to ComponentFunc. Complex input allocates real capacity and validates each
 * transformed value before releasing old storage. Failure frees the new buffer and retains the old
 * one. On success preserves length/capacity/selection and changes only the temporary type and owned
 * data.
 */
static int ComplexRealFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr) {
    ComplexRealFunction *functionPtr;
    double *newArr;
    size_t byteCount;
    Tcl_Size i;

    functionPtr = (ComplexRealFunction *)clientData;
    if (vPtr->type == RBC_VECTOR_REAL) {
        return ComponentFunc((ClientData)functionPtr->realProc, interp, vPtr);
    }
    assert(vPtr->type == RBC_VECTOR_COMPLEX);
    if (GetDoubleArrayByteCount(interp, vPtr->size, &byteCount) != TCL_OK) {
        return TCL_ERROR;
    }
    newArr = NULL;
    if (byteCount > 0) {
        newArr = Tcl_AttemptAlloc(byteCount);
        if (newArr == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate real expression vector", -1));
            return TCL_ERROR;
        }
    }
    for (i = 0; i < vPtr->length; i++) {
        errno = 0;
        newArr[i] = (*functionPtr->complexProc)(vPtr->data.complex[i]);
        if (errno != 0) {
            MathError(interp, newArr[i]);
            goto error;
        }
        if (!FINITE(newArr[i])) {
            MathError(interp, newArr[i]);
            goto error;
        }
    }
    FreeExpressionData(vPtr);
    vPtr->data.real = newArr;
    vPtr->type = RBC_VECTOR_REAL;
    vPtr->freeProc = (newArr == NULL) ? TCL_STATIC : TCL_DYNAMIC;
    return TCL_OK;

error:
    if (newArr != NULL) {
        ckfree(newArr);
    }
    return TCL_ERROR;
}
