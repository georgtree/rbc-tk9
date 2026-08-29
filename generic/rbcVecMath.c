/*
 * rbcVecMath.c --
 *
 *      Collections of procedures and structures to perform
 *      math functions on vector objects.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcVectorInt.h"
#include <stdint.h>
#include <errno.h>
#include <math.h>

typedef int(GenericMathProc)(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr);

typedef Rbc_Complex(ComplexComponentProc)(Rbc_Complex value);
typedef double(ComplexRealProc)(Rbc_Complex value);
typedef Rbc_Complex(ComplexScalarProc)(VectorObject *vPtr);

typedef struct {
    ComponentProc *realProc;
    ComplexComponentProc *complexProc;
} ComplexComponentFunction;

typedef struct {
    ComponentProc *realProc;
    ComplexRealProc *complexProc;
} ComplexRealFunction;

typedef struct {
    Rbc_VectorIndexProc *realProc;
    ComplexScalarProc *complexProc;
} ComplexScalarFunction;

typedef double(ComplexRealScalarProc)(VectorObject *vPtr);

typedef struct {
    Rbc_VectorIndexProc *realProc;
    ComplexRealScalarProc *complexProc;
} ComplexRealScalarFunction;

/*
 *    Contains information about math functions that can be called
 *    for vectors.  The table of math functions is global within the
 *    application.  So you can't define two different "sqrt"
 *    functions.
 */
typedef struct {
    char *name;            /* Name of built-in math function.  If
                            * NULL, indicates that the function
                            * was user-defined and dynamically
                            * allocated.  Function names are
                            * global across all interpreters. */
    GenericMathProc *proc; /* Procedure that implements this math
                            * function. */
    ClientData clientData; /* Argument to pass when invoking the
                            * function. */
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
static int ApplyComplexEqualityOperator(Tcl_Interp *interp, int operator, VectorObject * vPtr, VectorObject *v2Ptr);
static double Random(double value);
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
static double Q1(Rbc_Vector *vecPtr);
static double Q3(Rbc_Vector *vecPtr);
static double Round(double value);
static double StdDeviation(Rbc_Vector *vecPtr);
static double Skew(Rbc_Vector *vecPtr);
static int Sort(VectorObject *vPtr);
static double Sum(Rbc_Vector *vecPtr);
static double Variance(Rbc_Vector *vecPtr);
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

static MathFunction mathFunctions[] = {
    {"abs", (GenericMathProc *)ComplexRealFunc, (ClientData)&absFunction},
    {"arg", (GenericMathProc *)ComplexRealFunc, (ClientData)&argFunction},
    {"conj", (GenericMathProc *)ComplexComponentFunc, (ClientData)&conjFunction},
    {"imag", (GenericMathProc *)ComplexRealFunc, (ClientData)&imagFunction},
    {"real", (GenericMathProc *)ComplexRealFunc, (ClientData)&realFunction},
    {"acos", (GenericMathProc *)ComplexComponentFunc, (ClientData)&acosFunction},
    {"asin", (GenericMathProc *)ComplexComponentFunc, (ClientData)&asinFunction},
    {"atan", (GenericMathProc *)ComplexComponentFunc, (ClientData)&atanFunction},
    {"adev", (GenericMathProc *)ScalarFunc, (ClientData)AvgDeviation},
    {"ceil", (GenericMathProc *)ComponentFunc, (ClientData)ceil},
    {"cos", (GenericMathProc *)ComplexComponentFunc, (ClientData)&cosFunction},
    {"cosh", (GenericMathProc *)ComplexComponentFunc, (ClientData)&coshFunction},
    {"exp", (GenericMathProc *)ComplexComponentFunc, (ClientData)&expFunction},
    {"floor", (GenericMathProc *)ComponentFunc, (ClientData)floor},
    {"kurtosis", (GenericMathProc *)ScalarFunc, (ClientData)Kurtosis},
    {"length", (GenericMathProc *)ComplexRealScalarFunc, (ClientData)&lengthFunction},
    {"log", (GenericMathProc *)ComplexComponentFunc, (ClientData)&logFunction},
    {"log10", (GenericMathProc *)ComplexComponentFunc, (ClientData)&log10Function},
    {"max", (GenericMathProc *)ScalarFunc, (ClientData)Rbc_VecMax},
    {"mean", (GenericMathProc *)ComplexScalarFunc, (ClientData)&meanFunction},
    {"median", (GenericMathProc *)ScalarFunc, (ClientData)Median},
    {"min", (GenericMathProc *)ScalarFunc, (ClientData)Rbc_VecMin},
    {"norm", (GenericMathProc *)VectorFunc, (ClientData)Norm},
    {"nz", (GenericMathProc *)ScalarFunc, (ClientData)Nonzeros},
    {"q1", (GenericMathProc *)ScalarFunc, (ClientData)Q1},
    {"q3", (GenericMathProc *)ScalarFunc, (ClientData)Q3},
    {"prod", (GenericMathProc *)ComplexScalarFunc, (ClientData)&productFunction},
    {"random", (GenericMathProc *)ComponentFunc, (ClientData)Random},
    {"round", (GenericMathProc *)ComponentFunc, (ClientData)Round},
    {"sdev", (GenericMathProc *)ScalarFunc, (ClientData)StdDeviation},
    {"sin", (GenericMathProc *)ComplexComponentFunc, (ClientData)&sinFunction},
    {"sinh", (GenericMathProc *)ComplexComponentFunc, (ClientData)&sinhFunction},
    {"skew", (GenericMathProc *)ScalarFunc, (ClientData)Skew},
    {"sort", (GenericMathProc *)VectorFunc, (ClientData)Sort},
    {"sqrt", (GenericMathProc *)ComplexComponentFunc, (ClientData)&sqrtFunction},
    {"sum", (GenericMathProc *)ComplexScalarFunc, (ClientData)&sumFunction},
    {"tan", (GenericMathProc *)ComplexComponentFunc, (ClientData)&tanFunction},
    {"tanh", (GenericMathProc *)ComplexComponentFunc, (ClientData)&tanhFunction},
    {"var", (GenericMathProc *)ScalarFunc, (ClientData)Variance},
    {
        (char *)NULL,
    },
};

static int GetDoubleArrayByteCount(Tcl_Interp *interp, Tcl_Size count, size_t *byteCountPtr) {
    if (count < 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("array size cannot be negative", -1));
        return TCL_ERROR;
    }
    if ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / sizeof(double))) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("array size is too large", -1));
        return TCL_ERROR;
    }
    *byteCountPtr = (size_t)count * sizeof(double);
    return TCL_OK;
}

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

static void FreeExpressionData(VectorObject *vPtr) {
    if ((vPtr->data.raw != NULL) && (vPtr->freeProc != TCL_STATIC)) {
        if (vPtr->freeProc == TCL_DYNAMIC) {
            ckfree(vPtr->data.raw);
        } else {
            vPtr->freeProc(vPtr->data.raw);
        }
    }
}

static void ReleaseExpressionStorage(VectorObject *vPtr) {
    FreeExpressionData(vPtr);
    vPtr->data.raw = NULL;
    vPtr->length = 0;
    vPtr->size = 0;
    vPtr->first = 0;
    vPtr->last = -1;
    vPtr->freeProc = TCL_STATIC;
}

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

static int CopyExpressionVector(VectorObject *destPtr, VectorObject *srcPtr) {
    SetExpressionVectorType(destPtr, srcPtr->type);
    return Rbc_VectorDuplicate(destPtr, srcPtr);
}

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

static int SetExpressionRealScalar(VectorObject *vPtr, double value) {
    SetExpressionVectorType(vPtr, RBC_VECTOR_REAL);
    if (Rbc_VectorChangeLength(vPtr, 1) != TCL_OK) {
        return TCL_ERROR;
    }
    vPtr->data.real[0] = value;
    return TCL_OK;
}

static int SetExpressionComplexScalar(VectorObject *vPtr, Rbc_Complex value) {
    SetExpressionVectorType(vPtr, RBC_VECTOR_COMPLEX);
    if (Rbc_VectorChangeLength(vPtr, 1) != TCL_OK) {
        return TCL_ERROR;
    }
    vPtr->data.complex[0] = value;
    return TCL_OK;
}

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

static int ApplyComplexEqualityOperator(Tcl_Interp *interp, int operator, VectorObject * vPtr, VectorObject *v2Ptr) {
    double *newArr;
    size_t byteCount;
    Tcl_Size length;
    Tcl_Size i;

    assert((operator== EQUAL) || (operator== NEQ));
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
     * Compute the complete result before replacing vPtr's storage,
     * since vPtr is also one of the operands.
     */
    for (i = 0; i < length; i++) {
        Rbc_Complex a;
        Rbc_Complex b;
        int equal;

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
        equal = (a.real == b.real) && (a.imag == b.imag);
        newArr[i] = (double)((operator== EQUAL) ? equal : !equal);
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

static int ApplyComplexBinaryOperator(Tcl_Interp *interp, int operator, VectorObject * vPtr, VectorObject *v2Ptr) {
    Tcl_Size i;

    switch (operator) {
    case EQUAL:
    case NEQ:
        return ApplyComplexEqualityOperator(interp, operator, vPtr, v2Ptr);
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

static int ComplexValueIsFinite(Rbc_Complex value) { return FINITE(value.real) && FINITE(value.imag); }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_VectorInstallMathFunctions --
 *
 *      Creates a hash entry for every math function
 *      and sets the value to the function.
 *
 * Parameters:
 *      Tcl_HashTable *tablePtr - Pointer to the hash where the math functions should be installed to.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Adds to the hash entry the math functions.
 *
 *----------------------------------------------------------------------
 */
void Rbc_VectorInstallMathFunctions(Tcl_HashTable *tablePtr) {
    Tcl_HashEntry *hPtr;
    register MathFunction *mathPtr;
    int isNew;

    for (mathPtr = mathFunctions; mathPtr->name != NULL; mathPtr++) {
        hPtr = Tcl_CreateHashEntry(tablePtr, mathPtr->name, &isNew);
        Tcl_SetHashValue(hPtr, (ClientData)mathPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_VectorInstallMathFunctions --
 *
 *      Creates a hash entry for every index
 *      and sets the value to the function.
 *
 * Parameters:
 *      Tcl_HashTable *tablePtr - Pointer to the hash where the special indices should be added to.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Adds to the hash entry the special indicies
 *
 *----------------------------------------------------------------------
 */
void Rbc_VectorInstallSpecialIndices(Tcl_HashTable *tablePtr) {
    InstallIndexProc(tablePtr, "min", Rbc_VecMin);
    InstallIndexProc(tablePtr, "max", Rbc_VecMax);
    InstallIndexProc(tablePtr, "mean", Mean);
    InstallIndexProc(tablePtr, "sum", Sum);
    InstallIndexProc(tablePtr, "prod", Product);
}

/*
 *----------------------------------------------------------------------
 *
 * InstallIndexProc --
 *
 *      Creates a hash entry for every index
 *      and sets the value to the function.
 *
 * Parameters:
 *      Tcl_HashTable *tablePtr
 *      char *string
 *      Rbc_VectorIndexProc *procPtr - Pointer to function to be called when the vector finds the named index. If NULL,
 *                                     this indicates to remove the index from the table.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Adds to the hash entry the special indicies
 *
 *----------------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * First --
 *
 *      Gets the first index of the designated interval.  The interval
 *      is between vPtr->first and vPtr->last.  But the range may
 *      NaN or Inf values that should be ignored.
 *
 * Parameters:
 *      VectorObject *vPtr - The vector to retrieve the first index from 
 *
 * Results:
 *      Returns the index of the first finite value in the designated
 *      interval.  If no finite values exists in the range, then -1 is
 *      returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * Next --
 *
 *      Gets the next index of the designated interval.  The interval
 *      is between vPtr->first and vPtr->last.  Ignore NaN or Inf
 *      values.
 *
 * Parameters:
 *      VectorObject *vPtr - The vector to retrieve the next index for
 *      int current - The current index
 *
 * Results:
 *      Returns the index of the next finite value in the designated
 *      interval.  If no more finite values exists in the range,
 *      then -1 is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_VecMin --
 *
 *      Calculates the minimum value of all the indexes in the
 *      vector.
 *
 * Parameters:
 *      Rbc_Vector *vecPtr - The vector to calculate the min for
 *
 * Results:
 *      The minimum value in the vector
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
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

static double Random(double value) {
    (void)value;

    return Rbc_RandomDouble();
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_VecMax --
 *
 *      Calculates the minimum value of all the indexes in the
 *      vector.
 *
 * Parameters:
 *      Rbc_Vector *vecPtr - The vector to calculate the max for
 *
 * Results:
 *      The minimum value in the vector
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
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

/*
 *----------------------------------------------------------------------
 *
 * Mean --
 *
 *      Calculates the mean of all the value in the
 *      vector.
 *
 * Parameters:
 *      Rbc_Vector *vecPtr - The vector to calculate the mean of
 *
 * Results:
 *      The mean value of the vector
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * Sum --
 *
 *      Calculates the sum of all the value in the
 *      vector.
 *
 * Parameters:
 *      Rbc_Vector *vecPtr - The vector to calculate the sum for
 *
 * Results:
 *      The sum value of the vector
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * Product --
 *
 *      Calculates the product of all the value in the
 *      vector.
 *
 * Parameters:
 *      Rbc_Vector *vecPtr - The Vector to calculate product for
 *
 * Results:
 *      The product value of the vector
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static double Product(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size i;
    register double prod;

    prod = 1.0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        prod *= vPtr->data.real[i];
    }
    return prod;
}

/*
 *--------------------------------------------------------------
 *
 * Sort --
 *
 *      A vector math function.  Sorts the values of the given
 *      vector.
 *
 * Parameters:
 *      VectorObject *vPtr
 *
 * Results:
 *      Always TCL_OK.
 *
 * Side Effects:
 *      The vector is sorted.
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * Length --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * Median --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * Variance --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double Variance(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    register double dx, var, mean;
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

/*
 *--------------------------------------------------------------
 *
 * Skew --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double Skew(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    register double diff, var, skew, mean, diffsq;
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

/*
 *--------------------------------------------------------------
 *
 * StdDeviation --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double StdDeviation(Rbc_Vector *vecPtr) {
    double var;

    var = Variance(vecPtr);
    if (var > 0.0) {
        return sqrt(var);
    }
    return 0.0;
}

/*
 *--------------------------------------------------------------
 *
 * AvgDeviation --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double AvgDeviation(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    register double diff, avg, mean;
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

/*
 *--------------------------------------------------------------
 *
 * Kurtosis --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double Kurtosis(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    register double diff, diffsq, kurt, var, mean;
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

/*
 *--------------------------------------------------------------
 *
 * Q1 --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * Q3 --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * Norm --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * Nonzeros --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double Nonzeros(Rbc_Vector *vecPtr) {
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Tcl_Size i;
    Tcl_Size count;

    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        if (vPtr->data.real[i] == 0.0) {
            count++;
        }
    }
    return (double)count;
}

static double Identity(double value) { return value; }

static double Zero(double value) {
    (void)value;

    return 0.0;
}

static double Arg(double value) { return atan2(0.0, value); }

static double ComplexAbs(Rbc_Complex value) { return hypot(value.real, value.imag); }

static double ComplexArg(Rbc_Complex value) { return atan2(value.imag, value.real); }

static double ComplexReal(Rbc_Complex value) { return value.real; }

static double ComplexImag(Rbc_Complex value) { return value.imag; }

static Rbc_Complex ComplexConj(Rbc_Complex value) {
    value.imag = -value.imag;
    return value;
}

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

static Rbc_Complex ComplexExp(Rbc_Complex value) {
    Rbc_Complex result;
    double scale;

    scale = exp(value.real);
    result.real = scale * cos(value.imag);
    result.imag = scale * sin(value.imag);
    return result;
}

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

static Rbc_Complex ComplexLog(Rbc_Complex value) {
    Rbc_Complex result;

    result.real = LogHypot(value.real, value.imag);
    result.imag = atan2(value.imag, value.real);
    return result;
}

static Rbc_Complex ComplexLog10(Rbc_Complex value) {
    Rbc_Complex result;
    const double invLn10 = 0.43429448190325182765;

    result = ComplexLog(value);
    result.real *= invLn10;
    result.imag *= invLn10;
    return result;
}

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

static double RealAcosh(double value) {
    const double ln2 = 0.69314718055994530942;

    if (value > DBL_MAX * 0.5) {
        return log(value) + ln2;
    }
    return log(value + sqrt(value - 1.0) * sqrt(value + 1.0));
}

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

static Rbc_Complex ComplexAcos(Rbc_Complex value) {
    Rbc_Complex result;
    Rbc_Complex asinValue;

    asinValue = ComplexAsin(value);
    result.real = M_PI_2 - asinValue.real;
    result.imag = -asinValue.imag;
    return result;
}

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

/*
 *--------------------------------------------------------------
 *
 * Fabs --
 *
 *      TODO: Description
 *
 * Parameters:
 *      double value
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double Fabs(double value) {
    if (value < 0.0) {
        return -value;
    }
    return value;
}

/*
 *--------------------------------------------------------------
 *
 * Round --
 *
 *      TODO: Description
 *
 * Parameters:
 *      double value
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double Round(double value) {
    if (value < 0.0) {
        return ceil(value - 0.5);
    } else {
        return floor(value + 0.5);
    }
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ExprVector --
 *
 *      Evaluates an vector expression and returns its value(s).
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      char *string
 *      Rbc_Vector *vecPtr
 *
 * Results:
 *      Each of the procedures below returns a standard Tcl result.
 *      If an error occurs then an error message is left in
 *      the interpreter result.  Otherwise the value of the expression,
 *      in the appropriate form, is stored at the interpreter result.
 *      If the expression had a result that was incompatible with the
 *      desired form then an error is returned.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------
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

int Rbc_ExprVector(Tcl_Interp *interp, char *string, Rbc_Vector *vecPtr) {
    VectorInterpData *dataPtr;
    VectorObject *vPtr;
    Value value;
    int result;

    vPtr = (VectorObject *)vecPtr;
    dataPtr = (vecPtr != NULL) ? vPtr->dataPtr : Rbc_VectorGetInterpData(interp);
    value.vPtr = Rbc_VectorNew(dataPtr);
    result = EvaluateExpression(interp, string, &value);
    if (result != TCL_OK) {
        goto done;
    }
    if (vPtr != NULL) {
        /*
         * Propagate an allocation/size failure instead of silently
         * reporting a successful vector expression.
         */
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
    Rbc_VectorFree(value.vPtr);
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * EvaluateExpression --
 *
 *      This procedure provides top-level functionality shared by
 *      procedures like Tcl_ExprInt, Tcl_ExprDouble, etc.
 *
 * Parameters:
 *      Tcl_Interp *interp - Context in which to evaluate the expression.
 *      char *string - Expression to evaluate.
 *      Value *valuePtr - Where to store result. Should not be initialized by caller.
 *
 * Results:
 *      The result is a standard Tcl return value.  If an error
 *      occurs then an error message is left in the interpreter result.
 *      The value of the expression is returned in *valuePtr, in
 *      whatever form it ends up in (could be string or integer
 *      or double).  Caller may need to convert result.  Caller
 *      is also responsible for freeing string memory in *valuePtr,
 *      if any was allocated.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * NextValue --
 *
 *      Parse a "value" from the remainder of the expression in parsePtr.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter to use for error reporting.
 *      ParseInfo *parsePtr - Describes the state of the parse just before the value (i.e. NextToken will be called to
 *                            get first token of value).
 *      int prec - Treat any un-parenthesized operator with precedence <= this as the end of the expression.
 *      Value *valuePtr - Where to store the value of the expression. Caller must have initialized pv field.
 *
 * Results:
 *      Normally TCL_OK is returned.  The value of the expression is
 *      returned in *valuePtr.  If an error occurred, then the interpreter
 *      result contains an error message and TCL_ERROR is returned.
 *      InfoPtr->token will be left pointing to the token AFTER the
 *      expression, and parsePtr->nextPtr will point to the character just
 *      after the terminating token.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
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
                Tcl_SetObjResult(interp, Tcl_NewStringObj("operator \"!\" is not supported for complex vectors", -1));
                goto error;
            }

            for (i = 0; i < vPtr->length; i++) {
                vPtr->data.real[i] = (double)(!vPtr->data.real[i]);
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
        register double *opnd;
        register double scalar;

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
        case LEFT_SHIFT: {
            Tcl_Size offset;
            Tcl_Size remaining;
            size_t holdBytes;
            size_t remainingBytes;
            double *hold;

            if (GetRotationOffset(interp, scalar, vPtr->length, &offset) != TCL_OK) {
                goto error;
            }
            if (offset == 0) {
                break;
            }
            remaining = vPtr->length - offset;
            if (GetDoubleArrayByteCount(interp, offset, &holdBytes) != TCL_OK) {
                goto error;
            }
            if (GetDoubleArrayByteCount(interp, remaining, &remainingBytes) != TCL_OK) {
                goto error;
            }
            hold = Tcl_AttemptAlloc(holdBytes);
            if (hold == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate vector rotation buffer", -1));
                goto error;
            }
            memcpy(hold, opnd, holdBytes);
            memmove(opnd, opnd + offset, remainingBytes);
            memcpy(opnd + remaining, hold, holdBytes);
            ckfree(hold);
            break;
        }
        case RIGHT_SHIFT: {
            Tcl_Size offset;
            Tcl_Size remaining;
            size_t holdBytes;
            size_t remainingBytes;
            double *hold;

            if (GetRotationOffset(interp, scalar, vPtr->length, &offset) != TCL_OK) {
                goto error;
            }
            if (offset == 0) {
                break;
            }
            remaining = vPtr->length - offset;
            if (GetDoubleArrayByteCount(interp, offset, &holdBytes) != TCL_OK) {
                goto error;
            }
            if (GetDoubleArrayByteCount(interp, remaining, &remainingBytes) != TCL_OK) {
                goto error;
            }
            hold = Tcl_AttemptAlloc(holdBytes);
            if (hold == NULL) {
                Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate vector rotation buffer", -1));
                goto error;
            }
            memcpy(hold, opnd + remaining, holdBytes);
            memmove(opnd + offset, opnd, remainingBytes);
            memcpy(opnd, hold, holdBytes);
            ckfree(hold);
            break;
        }
        default:
            Rbc_AppendResultStrings(interp, "unknown operator in expression", (char *)NULL);
            goto error;
        }

    } else if (vPtr->length == 1) {
        register double *opnd;
        register double scalar;

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
        register double *opnd1, *opnd2;
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

/*
 *----------------------------------------------------------------------
 *
 * MathError --
 *
 *      This procedure is called when an error occurs during a
 *      floating-point operation.  It reads errno and sets
 *      the interpreter result accordingly.
 *
 * Parameters:
 *      Tcl_Interp *interp - Where to store error message.
 *      double value - Value returned after error; used to distinguish underflows from overflows.
 *
 * Results:
 *      The Interpreter result is set to hold an error message.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
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

static int ParseBracketValue(Tcl_Interp *interp, const char *string, const char **termPtr, Value *valuePtr) {
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
    result = Tcl_EvalEx(interp, string, scriptLength, 0);
    if (result != TCL_OK) {
        return result;
    }
    return ParseSubstitutionResult(interp, valuePtr);
}

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

/*
 *----------------------------------------------------------------------
 *
 * NextToken --
 *
 *      Lexical analyzer for expression parser:  parses a single value,
 *      operator, or other syntactic element from an expression string.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter to use for error reporting.
 *      ParseInfo *parsePtr - Describes the state of the parse.
 *      Value *valuePtr - Where to store a parsed value.
 *
 * Results:
 *      TCL_OK is returned unless an error occurred while doing lexical
 *      analysis or executing an embedded command.  In that case a
 *      standard Tcl error is returned, using the interpreter result to hold
 *      an error message.  In the event of a successful return, the token
 *      and field in parsePtr is updated to refer to the next symbol in
 *      the expression string, and the expr field is advanced past that
 *      token;  if the token is a value, then the value is stored at
 *      valuePtr.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
static int NextToken(Tcl_Interp *interp, ParseInfo *parsePtr, Value *valuePtr) {
    register char *p;
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
/*
 *--------------------------------------------------------------
 *
 * Fmod --
 *
 *      Returns the remainder after performing x divided by y.
 *
 * Parameters:
 *      double x
 *      double y
 *
 * Results:
 *      x mod y
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static double Fmod(double x, double y) {
    if (y == 0.0) {
        return 0.0;
    }
    return x - (floor(x / y) * y);
}

/*
 *--------------------------------------------------------------
 *
 * ParseString --
 *
 *      Given a string (such as one coming from command or variable
 *      substitution), make a Value based on the string.  The value
 *      will be a floating-point or integer, if possible, or else it
 *      will just be a copy of the string.
 *
 * Parameters:
 *      Tcl_Interp *interp - Where to store error message.
 *      const char *string - String to turn into value.
 *      Value *valuePtr - Where to store value information. Caller must have initialized pv field.
 *
 * Results:
 *      TCL_OK is returned under normal circumstances, and TCL_ERROR
 *      is returned if a floating-point overflow or underflow occurred
 *      while reading in a number.  The value at *valuePtr is modified
 *      to hold a number, if possible.
 *
 * Side effects:
 *      None.
 *
 *--------------------------------------------------------------
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
        if (Rbc_VectorChangeLength(valuePtr->vPtr, 1) != TCL_OK) {
            return TCL_ERROR;
        }
        valuePtr->vPtr->data.real[0] = value;
        return TCL_OK;
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

/*
 *----------------------------------------------------------------------
 *
 * ParseMathFunction --
 *
 *      This procedure is invoked to parse a math function from an
 *      expression string, carry out the function, and return the
 *      value computed.
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter to use for error reporting.
 *      char *start - Start of string to parse
 *      ParseInfo *parsePtr - Describes the state of the parse. parsePtr->nextPtr must point to the first character of 
 *                            the function's name.
 *      Value *valuePtr - Where to store value, if that is what's parsed from string.  Caller must have initialized pv 
 *                        field correctly.
 *
 * Results:
 *      TCL_OK is returned if all went well and the function's value
 *      was computed successfully.  If the name doesn't match any
 *      known math function, returns TCL_RETURN. And if a format error
 *      was found, TCL_ERROR is returned and an error message is left
 *      in the interpreter result.
 *
 *      After a successful return parsePtr will be updated to point to
 *      the character just after the function call, the token is set
 *      to VALUE, and the value is stored in valuePtr.
 *
 * Side effects:
 *      Embedded commands could have arbitrary side-effects.
 *
 *----------------------------------------------------------------------
 */
static int ParseMathFunction(Tcl_Interp *interp, char *start, ParseInfo *parsePtr, Value *valuePtr) {
    Tcl_HashEntry *hPtr;
    MathFunction *mathPtr; /* Info about math function. */
    register char *p;
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

/*
 *----------------------------------------------------------------------
 *
 * Math Functions --
 *
 *      This page contains the procedures that implement all of the
 *      built-in math functions for expressions.
 *
 * Parameters:
 *      ClientData clientData - Contains address of procedure that takes one double argument and returns a double result.
 *      Tcl_Interp *interp
 *      VectorObject *vPtr
 *
 * Results:
 *      Each procedure returns TCL_OK if it succeeds and places result
 *      information at *resultPtr.  If it fails it returns TCL_ERROR
 *      and leaves an error message in the interpreter result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * ScalarFunc --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      VectorObject *vPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * VectorFunc --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      VectorObject *vPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int VectorFunc(ClientData clientData, Tcl_Interp *interp, VectorObject *vPtr) {
    VectorProc *procPtr = (VectorProc *)clientData;
    if (vPtr->type == RBC_VECTOR_COMPLEX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("math function is not supported for complex vectors yet", -1));
        return TCL_ERROR;
    }
    return (*procPtr)(vPtr);
}

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
