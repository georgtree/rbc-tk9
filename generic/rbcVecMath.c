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

#include "rbcVector.h"
#include <errno.h>
#include <math.h>

typedef int(GenericMathProc) ANYARGS;

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

#define TclParseBraces Rbc_ParseBraces
#define TclParseNestedCmd Rbc_ParseNestedCmd
#define TclParseQuotes Rbc_ParseQuotes
#define TclExpandParseValue Rbc_ExpandParseValue

int TclParseBraces(Tcl_Interp *interp, char *string, char **termPtr, ParseValue *pvPtr);
int TclParseNestedCmd(Tcl_Interp *interp, char *string, int flags, char **termPtr, ParseValue *pvPtr);
int TclParseQuotes(Tcl_Interp *interp, char *string, int termChar, int flags, char **termPtr, ParseValue *pvPtr);
void TclExpandParseValue(ParseValue *pvPtr, int needed);

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
static int First(VectorObject *vPtr);
static int Next(VectorObject *vPtr, int current);
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

static MathFunction mathFunctions[] = {
    {"abs", (GenericMathProc *)ComponentFunc, (ClientData)Fabs},
    {"acos", (GenericMathProc *)ComponentFunc, (ClientData)acos},
    {"asin", (GenericMathProc *)ComponentFunc, (ClientData)asin},
    {"atan", (GenericMathProc *)ComponentFunc, (ClientData)atan},
    {"adev", (GenericMathProc *)ScalarFunc, (ClientData)AvgDeviation},
    {"ceil", (GenericMathProc *)ComponentFunc, (ClientData)ceil},
    {"cos", (GenericMathProc *)ComponentFunc, (ClientData)cos},
    {"cosh", (GenericMathProc *)ComponentFunc, (ClientData)cosh},
    {"exp", (GenericMathProc *)ComponentFunc, (ClientData)exp},
    {"floor", (GenericMathProc *)ComponentFunc, (ClientData)floor},
    {"kurtosis", (GenericMathProc *)ScalarFunc, (ClientData)Kurtosis},
    {"length", (GenericMathProc *)ScalarFunc, (ClientData)Length},
    {"log", (GenericMathProc *)ComponentFunc, (ClientData)log},
    {"log10", (GenericMathProc *)ComponentFunc, (ClientData)log10},
    {"max", (GenericMathProc *)ScalarFunc, (ClientData)Rbc_VecMax},
    {"mean", (GenericMathProc *)ScalarFunc, (ClientData)Mean},
    {"median", (GenericMathProc *)ScalarFunc, (ClientData)Median},
    {"min", (GenericMathProc *)ScalarFunc, (ClientData)Rbc_VecMin},
    {"norm", (GenericMathProc *)VectorFunc, (ClientData)Norm},
    {"nz", (GenericMathProc *)ScalarFunc, (ClientData)Nonzeros},
    {"q1", (GenericMathProc *)ScalarFunc, (ClientData)Q1},
    {"q3", (GenericMathProc *)ScalarFunc, (ClientData)Q3},
    {"prod", (GenericMathProc *)ScalarFunc, (ClientData)Product},
    {"random", (GenericMathProc *)ComponentFunc, (ClientData)drand48},
    {"round", (GenericMathProc *)ComponentFunc, (ClientData)Round},
    {"sdev", (GenericMathProc *)ScalarFunc, (ClientData)StdDeviation},
    {"sin", (GenericMathProc *)ComponentFunc, (ClientData)sin},
    {"sinh", (GenericMathProc *)ComponentFunc, (ClientData)sinh},
    {"skew", (GenericMathProc *)ScalarFunc, (ClientData)Skew},
    {"sort", (GenericMathProc *)VectorFunc, (ClientData)Sort},
    {"sqrt", (GenericMathProc *)ComponentFunc, (ClientData)sqrt},
    {"sum", (GenericMathProc *)ScalarFunc, (ClientData)Sum},
    {"tan", (GenericMathProc *)ComponentFunc, (ClientData)tan},
    {"tanh", (GenericMathProc *)ComponentFunc, (ClientData)tanh},
    {"var", (GenericMathProc *)ScalarFunc, (ClientData)Variance},
    {
        (char *)NULL,
    },
};

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
static int First(VectorObject *vPtr) {
    register int i;

    for (i = vPtr->first; i <= vPtr->last; i++) {
        if (FINITE(vPtr->valueArr[i])) {
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
static int Next(VectorObject *vPtr, int current) {
    register int i;

    for (i = current + 1; i <= vPtr->last; i++) {
        if (FINITE(vPtr->valueArr[i])) {
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
    register int i;

    min = rbcNaN;
    for (i = 0; i < vPtr->length; i++) {
        if (FINITE(vPtr->valueArr[i])) {
            min = vPtr->valueArr[i];
            break;
        }
    }
    for (/* empty */; i < vPtr->length; i++) {
        if (FINITE(vPtr->valueArr[i])) {
            if (min > vPtr->valueArr[i]) {
                min = vPtr->valueArr[i];
            }
        }
    }
    vPtr->min = min;
    return vPtr->min;
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
    register int i;

    max = rbcNaN;
    for (i = 0; i < vPtr->length; i++) {
        if (FINITE(vPtr->valueArr[i])) {
            max = vPtr->valueArr[i];
            break;
        }
    }
    for (/* empty */; i < vPtr->length; i++) {
        if (FINITE(vPtr->valueArr[i])) {
            if (max < vPtr->valueArr[i]) {
                max = vPtr->valueArr[i];
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
    register int i;
    int count;
    double sum;

    sum = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        sum += vPtr->valueArr[i];
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
    register int i;
    double sum;

    sum = 0.0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        sum += vPtr->valueArr[i];
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
    register int i;
    register double prod;

    prod = 1.0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        prod *= vPtr->valueArr[i];
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
    int *indexArr;
    double *tempArr;
    register int i;

    indexArr = Rbc_VectorSortIndex(&vPtr, 1);
    tempArr = (double *)ckalloc(sizeof(double) * vPtr->length);
    for (i = vPtr->first; i <= vPtr->last; i++) {
        tempArr[i] = vPtr->valueArr[indexArr[i]];
    }
    ckfree((char *)indexArr);
    for (i = vPtr->first; i <= vPtr->last; i++) {
        vPtr->valueArr[i] = tempArr[i];
    }
    ckfree((char *)tempArr);
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
    int count;
    register int i;

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
    int *iArr;
    double q2;
    int mid;

    if (vPtr->length == 0) {
        return -DBL_MAX;
    }
    iArr = Rbc_VectorSortIndex(&vPtr, 1);
    mid = (vPtr->length - 1) / 2;

    /*
     * Determine Q2 by checking if the number of elements [0..n-1] is
     * odd or even.  If even, we must take the average of the two
     * middle values.
     */
    if (vPtr->length & 1) { /* Odd */
        q2 = vPtr->valueArr[iArr[mid]];
    } else { /* Even */
        q2 = (vPtr->valueArr[iArr[mid]] + vPtr->valueArr[iArr[mid + 1]]) * 0.5;
    }
    ckfree((char *)iArr);
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
    register int i;
    int count;

    mean = Mean(vecPtr);
    var = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        dx = vPtr->valueArr[i] - mean;
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
    register int i;
    int count;

    mean = Mean(vecPtr);
    var = skew = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        diff = vPtr->valueArr[i] - mean;
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
    register int i;
    int count;

    mean = Mean(vecPtr);
    avg = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        diff = vPtr->valueArr[i] - mean;
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
    register int i;
    int count;

    mean = Mean(vecPtr);
    var = kurt = 0.0;
    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        diff = vPtr->valueArr[i] - mean;
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
    double q1;
    int *iArr;

    if (vPtr->length == 0) {
        return -DBL_MAX;
    }
    iArr = Rbc_VectorSortIndex(&vPtr, 1);

    if (vPtr->length < 4) {
        q1 = vPtr->valueArr[iArr[0]];
    } else {
        int mid, q;

        mid = (vPtr->length - 1) / 2;
        q = mid / 2;

        /*
         * Determine Q1 by checking if the number of elements in the
         * bottom half [0..mid) is odd or even.   If even, we must
         * take the average of the two middle values.
         */
        if (mid & 1) { /* Odd */
            q1 = vPtr->valueArr[iArr[q]];
        } else { /* Even */
            q1 = (vPtr->valueArr[iArr[q]] + vPtr->valueArr[iArr[q + 1]]) * 0.5;
        }
    }
    ckfree((char *)iArr);
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
    double q3;
    int *iArr;

    if (vPtr->length == 0) {
        return -DBL_MAX;
    }

    iArr = Rbc_VectorSortIndex(&vPtr, 1);

    if (vPtr->length < 4) {
        q3 = vPtr->valueArr[iArr[vPtr->length - 1]];
    } else {
        int mid, q;

        mid = (vPtr->length - 1) / 2;
        q = (vPtr->length + mid) / 2;

        /*
         * Determine Q3 by checking if the number of elements in the
         * upper half (mid..n-1] is odd or even.   If even, we must
         * take the average of the two middle values.
         */
        if (mid & 1) { /* Odd */
            q3 = vPtr->valueArr[iArr[q]];
        } else { /* Even */
            q3 = (vPtr->valueArr[iArr[q]] + vPtr->valueArr[iArr[q + 1]]) * 0.5;
        }
    }
    ckfree((char *)iArr);
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
    register int i;

    min = Rbc_VecMin(vecPtr);
    max = Rbc_VecMax(vecPtr);
    range = max - min;
    for (i = 0; i < vPtr->length; i++) {
        norm = (vPtr->valueArr[i] - min) / range;
        vPtr->valueArr[i] = norm;
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
    register int i;
    int count;

    count = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        if (vPtr->valueArr[i] == 0.0) {
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
int Rbc_ExprVector(Tcl_Interp *interp, char *string, Rbc_Vector *vecPtr) {
    VectorInterpData *dataPtr; /* Interpreter-specific data. */
    VectorObject *vPtr = (VectorObject *)vecPtr;
    Value value;

    dataPtr = (vecPtr != NULL) ? vPtr->dataPtr : Rbc_VectorGetInterpData(interp);
    value.vPtr = Rbc_VectorNew(dataPtr);
    if (EvaluateExpression(interp, string, &value) != TCL_OK) {
        Rbc_VectorFree(value.vPtr);
        return TCL_ERROR;
    }

    if (vPtr != NULL) {
        Rbc_VectorDuplicate(vPtr, value.vPtr);
    } else {
        register int i;
        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
        /* No result vector.  Put values in the interpreter result.  */
        for (i = 0; i < value.vPtr->length; i++) {
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewDoubleObj(value.vPtr->valueArr[i]));
        }
        Tcl_SetObjResult(interp, resultObj);
    }
    Rbc_VectorFree(value.vPtr);
    return TCL_OK;
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
    register int i;

    info.expr = info.nextPtr = string;
    valuePtr->pv.buffer = valuePtr->pv.next = valuePtr->staticSpace;
    valuePtr->pv.end = valuePtr->pv.buffer + STATIC_STRING_SPACE - 1;
    valuePtr->pv.expandProc = TclExpandParseValue;
    valuePtr->pv.clientData = NULL;

    result = NextValue(interp, &info, -1, valuePtr);
    if (result != TCL_OK) {
        return result;
    }
    if (info.token != END) {
        Tcl_AppendResult(interp, ": syntax error in expression \"", string, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    vPtr = valuePtr->vPtr;

    /* Check for NaN's and overflows. */
    for (i = 0; i < vPtr->length; i++) {
        if (!FINITE(vPtr->valueArr[i])) {
            /*
             * IEEE floating-point error.
             */
            MathError(interp, vPtr->valueArr[i]);
            return TCL_ERROR;
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
    register int i;

    /*
     * There are two phases to this procedure.  First, pick off an initial
     * value.  Then, parse (binary operator, value) pairs until done.
     */

    vPtr = valuePtr->vPtr;
    v2Ptr = Rbc_VectorNew(vPtr->dataPtr);
    gotOp = FALSE;
    value2.vPtr = v2Ptr;
    value2.pv.buffer = value2.pv.next = value2.staticSpace;
    value2.pv.end = value2.pv.buffer + STATIC_STRING_SPACE - 1;
    value2.pv.expandProc = TclExpandParseValue;
    value2.pv.clientData = NULL;

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
            Tcl_AppendResult(interp, "unmatched parentheses in expression \"", parsePtr->expr, "\"", (char *)NULL);
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
            for (i = 0; i < vPtr->length; i++) {
                vPtr->valueArr[i] = -(vPtr->valueArr[i]);
            }
            break;
        case NOT:
            for (i = 0; i < vPtr->length; i++) {
                vPtr->valueArr[i] = (double)(!vPtr->valueArr[i]);
            }
            break;
        default:
            Tcl_AppendResult(interp, "unknown operator", (char *)NULL);
            goto error;
        }
        } else if (parsePtr->token != VALUE) {
            Tcl_AppendResult(interp, "missing operand", (char *)NULL);
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

        value2.pv.next = value2.pv.buffer;
        if ((operator<MULT) || (operator>= UNARY_MINUS)) {
            if ((operator== END) || (operator== CLOSE_PAREN) || (operator== COMMA)) {
                result = TCL_OK;
                goto done;
            } else {
                Tcl_AppendResult(interp, "bad operator", (char *)NULL);
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
        Tcl_AppendResult(interp, "unexpected token in expression", (char *)NULL);
        goto error;
    }
    /*
     * At this point we have two vectors and an operator.
     */

    if (v2Ptr->length == 1) {
        register double *opnd;
        register double scalar;

        /*
         * 2nd operand is a scalar.
         */
        scalar = v2Ptr->valueArr[0];
        opnd = vPtr->valueArr;
        switch (operator) {
        case MULT:
            for (i = 0; i < vPtr->length; i++) {
                opnd[i] *= scalar;
            }
            break;
        case DIVIDE:
            if (scalar == 0.0) {
                Tcl_AppendResult(interp, "divide by zero", (char *)NULL);
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
            int offset;

            offset = (int)scalar % vPtr->length;
            if (offset > 0) {
                double *hold;
                register int j;

                hold = (double *)ckalloc(sizeof(double) * offset);
                for (i = 0; i < offset; i++) {
                    hold[i] = opnd[i];
                }
                for (i = offset, j = 0; i < vPtr->length; i++, j++) {
                    opnd[j] = opnd[i];
                }
                for (i = 0, j = vPtr->length - offset; j < vPtr->length; i++, j++) {
                    opnd[j] = hold[i];
                }
                ckfree((char *)hold);
            }
        } break;
        case RIGHT_SHIFT: {
            int offset;

            offset = (int)scalar % vPtr->length;
            if (offset > 0) {
                double *hold;
                register int j;

                hold = (double *)ckalloc(sizeof(double) * offset);
                for (i = vPtr->length - offset, j = 0; i < vPtr->length; i++, j++) {
                    hold[j] = opnd[i];
                }
                for (i = vPtr->length - offset - 1, j = vPtr->length - 1; i >= 0; i--, j--) {
                    opnd[j] = opnd[i];
                }
                for (i = 0; i < offset; i++) {
                    opnd[i] = hold[i];
                }
                ckfree((char *)hold);
            }
        } break;
        default:
            Tcl_AppendResult(interp, "unknown operator in expression", (char *)NULL);
            goto error;
        }

    } else if (vPtr->length == 1) {
        register double *opnd;
        register double scalar;

        /*
         * 1st operand is a scalar.
         */
        scalar = vPtr->valueArr[0];
        Rbc_VectorDuplicate(vPtr, v2Ptr);
        opnd = vPtr->valueArr;
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
                    Tcl_AppendResult(interp, "divide by zero", (char *)NULL);
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
            Tcl_AppendResult(interp, "second shift operand must be scalar", (char *)NULL);
            goto error;
        default:
            Tcl_AppendResult(interp, "unknown operator in expression", (char *)NULL);
            goto error;
        }
    } else {
        register double *opnd1, *opnd2;
        /*
         * Carry out the function of the specified operator.
         */
        if (vPtr->length != v2Ptr->length) {
            Tcl_AppendResult(interp, "vectors are different lengths", (char *)NULL);
            goto error;
        }
        opnd1 = vPtr->valueArr, opnd2 = v2Ptr->valueArr;
        switch (operator) {
        case MULT:
            for (i = 0; i < vPtr->length; i++) {
                opnd1[i] *= opnd2[i];
            }
            break;
        case DIVIDE:
            for (i = 0; i < vPtr->length; i++) {
                if (opnd2[i] == 0.0) {
                    Tcl_AppendResult(interp, "can't divide by 0.0 vector component", (char *)NULL);
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
            Tcl_AppendResult(interp, "second shift operand must be scalar", (char *)NULL);
            goto error;
        default:
            Tcl_AppendResult(interp, "unknown operator in expression", (char *)NULL);
            goto error;
        }
    }
    }
done:
    if (value2.pv.buffer != value2.staticSpace) {
        ckfree((char *)value2.pv.buffer);
    }
    Rbc_VectorFree(v2Ptr);
    return result;

error:
    if (value2.pv.buffer != value2.staticSpace) {
        ckfree((char *)value2.pv.buffer);
    }
    Rbc_VectorFree(v2Ptr);
    return TCL_ERROR;
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
    if ((errno == EDOM) || (value != value)) {
        Tcl_AppendResult(interp, "domain error: argument not in valid range", (char *)NULL);
        Tcl_SetErrorCode(interp, "ARITH", "DOMAIN", Tcl_GetStringResult(interp), (char *)NULL);
    } else if ((errno == ERANGE) || IS_INF(value)) {
        if (value == 0.0) {
            Tcl_AppendResult(interp, "floating-point value too small to represent", (char *)NULL);
            Tcl_SetErrorCode(interp, "ARITH", "UNDERFLOW", Tcl_GetStringResult(interp), (char *)NULL);
        } else {
            Tcl_AppendResult(interp, "floating-point value too large to represent", (char *)NULL);
            Tcl_SetErrorCode(interp, "ARITH", "OVERFLOW", Tcl_GetStringResult(interp), (char *)NULL);
        }
    } else {
        char buf[20];

        sprintf(buf, "%d", errno);
        Tcl_AppendResult(interp, "unknown floating-point error, ", "errno = ", buf, (char *)NULL);
        Tcl_SetErrorCode(interp, "ARITH", "UNKNOWN", Tcl_GetStringResult(interp), (char *)NULL);
    }
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
 *      Value *valuePtr - Where to store value, if that is what's parsed from string.  Caller must have initialized pv 
 *                        field correctly.
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
    char *endPtr;
    const char *var;
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
            if (Rbc_VectorChangeLength(valuePtr->vPtr, 1) != TCL_OK) {
                return TCL_ERROR;
            }
            valuePtr->vPtr->valueArr[0] = value;
            return TCL_OK;
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
        result = TclParseNestedCmd(interp, p + 1, 0, &endPtr, &(valuePtr->pv));
        if (result != TCL_OK) {
            return result;
        }
        parsePtr->nextPtr = endPtr;
        Tcl_ResetResult(interp);
        result = ParseString(interp, valuePtr->pv.buffer, valuePtr);
        return result;
    case '"':
        parsePtr->token = VALUE;
        result = TclParseQuotes(interp, p + 1, '"', 0, &endPtr, &(valuePtr->pv));
        if (result != TCL_OK) {
            return result;
        }
        parsePtr->nextPtr = endPtr;
        Tcl_ResetResult(interp);
        result = ParseString(interp, valuePtr->pv.buffer, valuePtr);
        return result;
    case '{':
        parsePtr->token = VALUE;
        result = TclParseBraces(interp, p + 1, &endPtr, &valuePtr->pv);
        if (result != TCL_OK) {
            return result;
        }
        parsePtr->nextPtr = endPtr;
        Tcl_ResetResult(interp);
        result = ParseString(interp, valuePtr->pv.buffer, valuePtr);
        return result;
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
            vPtr = Rbc_VectorParseElement(interp, valuePtr->vPtr->dataPtr, p, &endPtr, NS_SEARCH_BOTH);
            if (vPtr == NULL) {
                return TCL_ERROR;
            }
            Rbc_VectorDuplicate(valuePtr->vPtr, vPtr);
            parsePtr->nextPtr = endPtr;
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
    char *endPtr;
    double value;

    errno = 0;

    /*
     * The string can be either a number or a vector.  First try to
     * convert the string to a number.  If that fails then see if
     * we can find a vector by that name.
     */

    value = strtod(string, &endPtr);
    if ((endPtr != string) && (*endPtr == '\0')) {
        if (errno != 0) {
            Tcl_ResetResult(interp);
            MathError(interp, value);
            return TCL_ERROR;
        }
        /* Numbers are stored as single element vectors. */
        if (Rbc_VectorChangeLength(valuePtr->vPtr, 1) != TCL_OK) {
            return TCL_ERROR;
        }
        valuePtr->vPtr->valueArr[0] = value;
        return TCL_OK;
    } else {
        VectorObject *vPtr;

        while (isspace(UCHAR(*string))) {
            string++; /* Skip spaces leading the vector name. */
        }
        vPtr = Rbc_VectorParseElement(interp, valuePtr->vPtr->dataPtr, string, &endPtr, NS_SEARCH_BOTH);
        if (vPtr == NULL) {
            return TCL_ERROR;
        }
        if (*endPtr != '\0') {
            Tcl_AppendResult(interp, "extra characters after vector", (char *)NULL);
            return TCL_ERROR;
        }
        /* Copy the designated vector to our temporary. */
        Rbc_VectorDuplicate(valuePtr->vPtr, vPtr);
    }
    return TCL_OK;
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
    valuePtr->pv.next = valuePtr->pv.buffer;
    if (NextValue(interp, parsePtr, -1, valuePtr) != TCL_OK) {
        return TCL_ERROR; /* Parse error */
    }
    if (parsePtr->token != CLOSE_PAREN) {
        Tcl_AppendResult(interp, "unmatched parentheses in expression \"", parsePtr->expr, "\"", (char *)NULL);
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
    register int i;

    errno = 0;
    for (i = First(vPtr); i >= 0; i = Next(vPtr, i)) {
        vPtr->valueArr[i] = (*procPtr)(vPtr->valueArr[i]);
        if (errno != 0) {
            MathError(interp, vPtr->valueArr[i]);
            return TCL_ERROR;
        }
        if (!FINITE(vPtr->valueArr[i])) {
            /*
             * IEEE floating-point error.
             */
            MathError(interp, vPtr->valueArr[i]);
            return TCL_ERROR;
        }
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

    errno = 0;
    value = (*procPtr)(vPtr);
    if (errno != 0) {
        MathError(interp, value);
        return TCL_ERROR;
    }
    if (Rbc_VectorChangeLength(vPtr, 1) != TCL_OK) {
        return TCL_ERROR;
    }
    vPtr->valueArr[0] = value;
    return TCL_OK;
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
    return (*procPtr)(vPtr);
}
