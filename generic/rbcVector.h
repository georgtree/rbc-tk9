/*
 * rbcVector.h --
 *
 *      Public definitions for RBC vectors.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */


#ifndef _RBCVECTOR
#define _RBCVECTOR

#include <tcl.h>

/*
 * Rbc_Vector is deliberately opaque.  Clients must use the vector API
 * rather than access the vector representation directly.
 */
typedef struct Rbc_Vector_s Rbc_Vector;

/*
 * Numeric type of a vector.
 */
typedef enum {
    RBC_VECTOR_REAL = 0,
    RBC_VECTOR_COMPLEX
} Rbc_VectorType;

/*
 * Portable representation of a complex value.
 *
 * Do not use the native C complex type here.  This representation is
 * independent of compiler complex-number support.
 */
typedef struct {
    double real;
    double imag;
} Rbc_Complex;

/*
 * Vector client interface.
 *
 * This is used, among other things, by graph elements to follow named
 * vectors and receive notification when their contents change.
 */
typedef struct Rbc_VectorIdStruct *Rbc_VectorId;

typedef enum { RBC_VECTOR_NOTIFY_UPDATE = 1, RBC_VECTOR_NOTIFY_DESTROY } Rbc_VectorNotify;

typedef void(Rbc_VectorChangedProc)(Tcl_Interp *interp, ClientData clientData, Rbc_VectorNotify notify);

/*
 * Vector client API.
 *
 * These aren't part of the vector representation and therefore remain
 * public even though Rbc_Vector itself is opaque.
 */
Rbc_VectorId Rbc_AllocVectorId(Tcl_Interp *interp, const char *vecName);
void Rbc_FreeVectorId(Rbc_VectorId clientId);
int Rbc_GetVectorById(Tcl_Interp *interp, Rbc_VectorId clientId, Rbc_Vector **vecPtrPtr);
void Rbc_SetVectorChangedProc(Rbc_VectorId clientId, Rbc_VectorChangedProc *proc, ClientData clientData);
char *Rbc_NameOfVectorId(Rbc_VectorId clientId);
int Rbc_VectorNotifyPending(Rbc_VectorId clientId);

int Rbc_VectorGetRange(Rbc_Vector *vecPtr, double *minPtr, double *maxPtr);

#endif /* _RBCVECTOR */
