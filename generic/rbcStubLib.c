/**
 * @file rbcStubLib.c
 * @brief Client-linked stubs-table selection through Tcl package loading.
 *
 * Copyright (c) 2018 Ashok P. Nadkarni
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id$
 */

#ifndef USE_TCL_STUBS
#define USE_TCL_STUBS
#endif

#include "tcl.h"
#include "rbcVector.h"
#include "rbcDecls.h"

/**
 * @ingroup initialization_private
 * @brief Borrowed dispatch table selected by the consumer-linked stub object.
 *
 * Initially NULL. Successful initialization replaces it; failed initialization
 * leaves it unchanged. This is not interpreter-associated storage or an owned
 * allocation, and no lifetime reference or synchronization is supplied here.
 */
const RbcStubs *rbcStubsPtr;

/* Public initialization contracts are documented in rbc.h. */

/**
 * @ingroup initialization_private
 * @brief Require a package and select its published C dispatch table.
 * @param[in] interp See @ref rbc_interp_argument; package loading may execute Tcl.
 * @param[in] package Borrowed package name: rbc or rbc::vector.
 * @param[in] version Borrowed version requirement passed to Tcl.
 * @param[in] exact Nonzero requests an exact version match.
 * @return Borrowed actual version on success, or NULL on failure.
 *
 * Borrow interp, package and version; pass exact unchanged to Tcl_PkgRequireEx.
 * Package loading may execute Tcl. If require fails or returns NULL client data,
 * return NULL without changing rbcStubsPtr; missing client data adds no diagnostic.
 * Otherwise assign the borrowed table and return the borrowed actual version.
 * No table-magic/slot checks, copying, interpreter retention or locking occur.
 * Both public stubs initializers share this path; the package name chooses
 * whether Tk and full support scripts are required.
 */
static const char *InitPackageStubs(Tcl_Interp *interp, const char *package, const char *version, int exact) {
    const char *result;
    void *data;

    result = Tcl_PkgRequireEx(interp, package, version, exact, &data);
    if (!result || !data) {
        return NULL;
    }

    rbcStubsPtr = (const RbcStubs *)data;
    return result;
}

const char *Rbc_InitStubs(Tcl_Interp *interp, const char *version, int exact) {
    return InitPackageStubs(interp, "rbc", version, exact);
}

const char *Rbc_VectorInitStubs(Tcl_Interp *interp, const char *version, int exact) {
    return InitPackageStubs(interp, "rbc::vector", version, exact);
}
