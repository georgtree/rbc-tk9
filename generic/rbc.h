/*
 * rbc.h --
 *
 *      This file constructs the basic functionality of the
 *      rbc commands.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @file rbc.h
 * Public package entry point and client-side stubs initialization.
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

EXTERN Rbc_Uid Rbc_GetUid (const char *string);
EXTERN void Rbc_FreeUid (Rbc_Uid uid);
EXTERN Rbc_Uid Rbc_FindUid (char *string);

/**
 * Initialize the Tcl-only vector package in an interpreter.
 *
 * @param interp Interpreter receiving the commands and package registration.
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
 * @param interp Interpreter in which to require rbc; loading may execute Tcl.
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
 * @param interp Interpreter in which to require rbc::vector; loading may execute Tcl.
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
