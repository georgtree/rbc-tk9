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


/**
 * @file rbcVector.h
 * @brief Opaque vector types and client notification declarations.
 *
 * A vector and its storage belong to one interpreter. Client IDs observe
 * lifetime; they neither own nor preserve the vector. Use these interfaces
 * on the interpreter's owning thread. The client notification declarations
 * here are not entries in the generated public stubs table.
 */

#ifndef _RBCVECTOR
#define _RBCVECTOR

#include <tcl.h>

/** @brief Opaque vector record; access values and metadata through the API. */
typedef struct Rbc_Vector_s Rbc_Vector;

/** @brief Storage element type; real and complex buffers are distinct. */
typedef enum {
    RBC_VECTOR_REAL = 0,
    RBC_VECTOR_COMPLEX
} Rbc_VectorType;

/**
 * @brief Portable complex value, independent of native C complex support.
 */
typedef struct {
    double real; /**< Real component. */
    double imag; /**< Imaginary component. */
} Rbc_Complex;

/**
 * @brief Observer token attached to one vector, not a vector reference count.
 *
 * Survives vector rename. Free it when detaching, including from a callback.
 * Vector destruction frees remaining tokens after
 * delivering DESTROY; clients must discard their saved token at that point.
 */
typedef struct Rbc_VectorIdStruct *Rbc_VectorId;

/** @brief Client notification reason: changed data or imminent destruction. */
typedef enum { RBC_VECTOR_NOTIFY_UPDATE = 1, RBC_VECTOR_NOTIFY_DESTROY } Rbc_VectorNotify;

/**
 * @brief Synchronous notification callback on the interpreter's owning thread.
 *
 * clientData is borrowed and must remain valid while registered. On UPDATE,
 * reacquire storage rather than retaining an old data pointer. On DESTROY,
 * discard borrowed vector/storage pointers and the client ID; RBC frees the
 * remaining IDs after delivery. A callback may free its own or another ID,
 * add an observer for a subsequent delivery, or destroy the notifying vector.
 * Destruction interrupts the outer UPDATE pass. Nested immediate updates
 * restore the outer callback's changed-range snapshot on return.
 *
 * RBC preserves its records, not caller-owned clientData. A nested callback
 * can destroy the client's owner; the caller must manage that lifetime.
 */
typedef void(Rbc_VectorChangedProc)(Tcl_Interp *interp, ClientData clientData, Rbc_VectorNotify notify);

/*
 * Vector client API.
 *
 * These aren't part of the vector representation and therefore remain
 * public even though Rbc_Vector itself is opaque.
 */
/**
 * @brief Register an observer of an existing vector without copying its values.
 * @param[in] interp Interpreter containing the vector registry.
 * @param[in] vecName Borrowed name resolved by the vector name lookup rules.
 * @return New token, initially without a callback, or NULL with a diagnostic.
 * @see Rbc_SetVectorChangedProc
 * @see Rbc_FreeVectorId
 */
Rbc_VectorId Rbc_AllocVectorId(Tcl_Interp *interp, const char *vecName);

/**
 * @brief Detach an observer, deferring reclamation while delivery retains it.
 * @param[in] clientId Allocated token, or NULL; invalid after this call.
 *
 * Does not destroy the vector. May be called from UPDATE or DESTROY callbacks.
 * Destruction itself frees remaining IDs. A magic-field check is not permission
 * to pass freed or arbitrary pointers. NULL is accepted.
 */
void Rbc_FreeVectorId(Rbc_VectorId clientId);

/**
 * @brief Borrow the vector associated with a client ID and refresh its range.
 * @param[in] interp Interpreter used for error reporting.
 * @param[in] clientId Non-NULL live token.
 * @param[out] vecPtrPtr Receives the borrowed vector on success only.
 * @return TCL_OK, or TCL_ERROR with a diagnostic for a bad/disconnected token.
 *
 * Neither the returned pointer nor the ID extends vector lifetime. Never call
 * this with a token left over after vector destruction. Cached extrema are
 * recomputed when invalidated; this is not always a constant-time lookup.
 */
int Rbc_GetVectorById(Tcl_Interp *interp, Rbc_VectorId clientId, Rbc_Vector **vecPtrPtr);

/**
 * @brief Replace an observer's callback and borrowed callback data.
 * @param[in] clientId Non-NULL live token.
 * @param[in] proc Callback, or NULL to disable calls without detaching the ID.
 * @param[in] clientData Borrowed data passed to proc; never freed by RBC.
 *
 * Does not invoke the callback immediately or transfer ownership of clientData.
 */
void Rbc_SetVectorChangedProc(Rbc_VectorId clientId, Rbc_VectorChangedProc *proc, ClientData clientData);

/**
 * @brief Return the vector's current registered name, not its array mapping.
 * @param[in] clientId Non-NULL live token.
 * @return Borrowed name until rename/destruction, or NULL if disconnected.
 *
 * Do not modify or free the returned string. Copy it if it must survive rename.
 * A token already freed by destruction cannot be queried.
 */
char *Rbc_NameOfVectorId(Rbc_VectorId clientId);

/**
 * @brief Test whether an idle update notification is queued.
 * @param[in] clientId Live token, or NULL.
 * @return Nonzero for a pending notification; zero for NULL/disconnected IDs.
 *
 * The nonzero result is a flag mask, not necessarily 1. It is not a test for
 * all possible data changes, and cannot validate a freed token.
 */
int Rbc_VectorNotifyPending(Rbc_VectorId clientId);

/**
 * @brief Get cached finite extrema of a real vector, recomputing if necessary.
 * @param[in,out] vecPtr Live borrowed vector; its extrema cache may be updated.
 * @param[out] minPtr Receives minimum on success; must not be NULL.
 * @param[out] maxPtr Receives maximum on success; must not be NULL.
 * @return TCL_OK for a real vector; TCL_ERROR for complex storage, without
 * an interpreter diagnostic or changes to the output parameters.
 *
 * Empty vectors and vectors without finite values return NaN extrema.
 */
int Rbc_VectorGetRange(Rbc_Vector *vecPtr, double *minPtr, double *maxPtr);

#endif /* _RBCVECTOR */
