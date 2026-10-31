/*
 * rbcStubLib.c --
 *
 *  Stub object that will be statically linked into extensions that wish
 *  to access the RBC API.
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

const RbcStubs *rbcStubsPtr;

/* Public initialization contracts are documented in rbc.h. */

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
