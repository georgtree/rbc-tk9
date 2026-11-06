/**
 * @file rbc.h
 * @brief Public package entry point and client-side stubs initialization.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBC
#define _RBC

#define RBC_VERSION PACKAGE_VERSION /* Defined via command line C macro */

#include <tcl.h>

#ifndef EXPORT
#define EXPORT
#endif

#undef EXTERN

#ifdef __cplusplus
#   define EXTERN extern "C" EXPORT
#else
#   define EXTERN extern EXPORT
#endif

#include "rbcVector.h"

typedef char *Rbc_Uid;

/**
 * @brief Acquire a reference to an interned NUL-terminated name.
 *
 * Equal strings share a stable pointer while references remain. Do not modify
 * the returned characters despite the historical mutable typedef. Pair each
 * acquisition with Rbc_FreeUid. The process-static table is neither interpreter
 * local nor protected by locks; this is not a cross-thread sharing API.
 *
 * @param[in] string Borrowed non-NULL NUL-terminated spelling; acquires one reference to the interned result.
 */
EXTERN Rbc_Uid Rbc_GetUid (const char *string);
/**
 * @brief Release one acquired UID reference, deleting its key at zero.
 *
 * Require a still-valid UID acquired through Rbc_GetUid. Do not free the string
 * with ckfree or release a merely borrowed Rbc_FindUid result. Unknown valid
 * strings produce a stderr diagnostic; NULL or dangling pointers are not safe.
 *
 * @param[in,out] uid Live acquired UID reference to release; the string becomes invalid if this was its final
 *   reference.
 */
EXTERN void Rbc_FreeUid (Rbc_Uid uid);
/**
 * @brief Borrow an existing interned name without increasing its reference count.
 *
 * Return NULL when absent. The pointer remains valid only while an acquiring
 * owner keeps a reference. Lookup lazily initializes the shared table but does
 * not insert a missing name. Use Rbc_GetUid when independent ownership is needed.
 *
 * @param[in] string Borrowed non-NULL NUL-terminated lookup spelling; bytes are not modified and no reference
 *   is acquired.
 */
EXTERN Rbc_Uid Rbc_FindUid (char *string);

/**
 * Initialize the Tcl-only vector package in an interpreter.
 *
 * @param interp Interpreter receiving the commands and package registration. See @ref rbc_interp_argument.
 * @return TCL_OK on success; TCL_ERROR with an interpreter diagnostic on failure.
 *
 * Initializes Tcl stubs, creates interpreter-associated readiness state and the
 * rbc namespace, and registers vector commands plus the private _initTk command.
 * Provides rbc::vector with the shared RBC stubs table. Does not load Tk or
 * source the Tcl support scripts; the package index performs those later steps.
 * Completed initialization phases survive errors. Readiness flags prevent
 * repeating completed phases, but do not repair commands deleted by callers.
 */
EXTERN int Rbc_Init (Tcl_Interp *interp);

/**
 * Require the full RBC package and initialize the client's stubs pointer.
 *
 * @param interp Interpreter in which to require rbc; loading may execute Tcl. See @ref rbc_interp_argument.
 * @param version Required version, interpreted by Tcl_PkgRequireEx.
 * @param exact Nonzero requests an exact version match.
 * @return Borrowed actual package version on success; NULL if requiring the
 * package fails or its client data is NULL. The latter adds no diagnostic.
 *
 * Linked statically into the consumer from the RBC stub library. On success,
 * replaces that consumer's rbcStubsPtr with package client data. On failure,
 * leaves its previous value unchanged. Requires Tk through the full package
 * loader. Does not retain an interpreter or vector, validate table entries,
 * or provide synchronization for changing the pointer.
 */
const char *Rbc_InitStubs(Tcl_Interp *interp, const char *version, int exact);

/**
 * Require the Tcl-only vector package and initialize the client's stubs pointer.
 *
 * @param interp Interpreter in which to require rbc::vector; loading may execute Tcl. See @ref rbc_interp_argument.
 * @param version Required version, interpreted by Tcl_PkgRequireEx.
 * @param exact Nonzero requests an exact version match.
 * @return Borrowed actual package version on success; NULL if requiring the
 * package fails or its client data is NULL. The latter adds no diagnostic.
 *
 * Has the same pointer ownership and failure behavior as Rbc_InitStubs, but
 * does not require Tk. Both packages publish the same RBC stubs table. The
 * caller must request a version supporting every table slot it will use.
 */
const char *Rbc_VectorInitStubs(Tcl_Interp *interp, const char *version, int exact);


#include "rbcDecls.h"
#endif /* _RBC */
