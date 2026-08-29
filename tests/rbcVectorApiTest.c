/*
 * rbcVectorApiTest.c --
 *
 *      Consumer-side tests for the public Rbc vector C/stubs API.
 *
 *      This file deliberately includes only public Rbc headers.
 */

#ifdef RBCAPI
#undef RBCAPI
#endif

#define USE_TCL_STUBS 1
#define USE_RBC_STUBS 1

#include "rbc.h"
#include <string.h>

static int freeCount = 0;

static const char *VectorTypeName(Rbc_VectorType type) {
    switch (type) {
    case RBC_VECTOR_REAL:
        return "real";
    case RBC_VECTOR_COMPLEX:
        return "complex";
    }
    return "unknown";
}

static Tcl_Obj *NewComplexObj(Rbc_Complex value) {
    Tcl_Obj *objv[2];

    objv[0] = Tcl_NewDoubleObj(value.real);
    objv[1] = Tcl_NewDoubleObj(value.imag);
    return Tcl_NewListObj(2, objv);
}

static int GetVector(Tcl_Interp *interp, Tcl_Obj *nameObjPtr, Rbc_Vector **vecPtrPtr) {
    return Rbc_GetVector(interp, Tcl_GetString(nameObjPtr), vecPtrPtr);
}

static int PutDictValue(Tcl_Interp *interp, Tcl_Obj *dictObjPtr, const char *key, Tcl_Obj *valueObjPtr) {
    return Tcl_DictObjPut(interp, dictObjPtr, Tcl_NewStringObj(key, -1), valueObjPtr);
}

static int CreateCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    const char *name;
    const char *typeName;
    Tcl_Size length;
    Rbc_Vector *vecPtr;
    int result;

    if (objc != 5) {
        Tcl_WrongNumArgs(interp, 2, objv, "name real|complex|typed-real length");
        return TCL_ERROR;
    }
    if (Tcl_GetSizeIntFromObj(interp, objv[4], &length) != TCL_OK) {
        return TCL_ERROR;
    }
    name = Tcl_GetString(objv[2]);
    typeName = Tcl_GetString(objv[3]);
    if (strcmp(typeName, "real") == 0) {
        /*
         * Exercise the compatibility constructor.
         */
        result = Rbc_CreateVector(interp, name, length, &vecPtr);
    } else if (strcmp(typeName, "typed-real") == 0) {
        result = Rbc_CreateVectorWithType(interp, name, length, RBC_VECTOR_REAL, &vecPtr);
    } else if (strcmp(typeName, "complex") == 0) {
        result = Rbc_CreateVectorWithType(interp, name, length, RBC_VECTOR_COMPLEX, &vecPtr);
    } else {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("unknown vector type \"%s\"", typeName));
        return TCL_ERROR;
    }
    if (result != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(Rbc_NameOfVector(vecPtr), -1));
    return TCL_OK;
}

static int InspectCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *vecPtr;
    Tcl_Obj *dictObjPtr;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
    }
    if (GetVector(interp, objv[2], &vecPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    dictObjPtr = Tcl_NewDictObj();
    if ((PutDictValue(interp, dictObjPtr, "name", Tcl_NewStringObj(Rbc_NameOfVector(vecPtr), -1)) != TCL_OK) ||
        (PutDictValue(interp, dictObjPtr, "type", Tcl_NewStringObj(VectorTypeName(Rbc_VectorGetType(vecPtr)), -1)) !=
         TCL_OK) ||
        (PutDictValue(interp, dictObjPtr, "length", Tcl_NewWideIntObj((Tcl_WideInt)Rbc_VectorLength(vecPtr))) !=
         TCL_OK) ||
        (PutDictValue(interp, dictObjPtr, "size", Tcl_NewWideIntObj((Tcl_WideInt)Rbc_VectorSize(vecPtr))) != TCL_OK) ||
        (PutDictValue(interp, dictObjPtr, "dirty", Tcl_NewIntObj(Rbc_VectorDirty(vecPtr))) != TCL_OK) ||
        (PutDictValue(interp, dictObjPtr, "realData", Tcl_NewBooleanObj(Rbc_VectorData(vecPtr) != NULL)) != TCL_OK) ||
        (PutDictValue(interp, dictObjPtr, "complexData", Tcl_NewBooleanObj(Rbc_VectorComplexData(vecPtr) != NULL)) !=
         TCL_OK)) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, dictObjPtr);
    return TCL_OK;
}

static int ExistsCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int exists;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
    }
    exists = Rbc_VectorExists2(interp, Tcl_GetString(objv[2]));
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(exists));
    return TCL_OK;
}

static int FreeCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *vecPtr;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
    }
    if (GetVector(interp, objv[2], &vecPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    Rbc_FreeVector(vecPtr);
    return TCL_OK;
}

static int ResizeCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *vecPtr;
    Tcl_Size length;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 2, objv, "name length");
        return TCL_ERROR;
    }
    if ((GetVector(interp, objv[2], &vecPtr) != TCL_OK) ||
        (Tcl_GetSizeIntFromObj(interp, objv[3], &length) != TCL_OK)) {
        return TCL_ERROR;
    }
    if (Rbc_ResizeVector(vecPtr, length) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewWideIntObj((Tcl_WideInt)Rbc_VectorLength(vecPtr)));
    return TCL_OK;
}

static int RangeCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *vecPtr;
    Tcl_Obj *resultObjv[2];
    double min;
    double max;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
    }
    if (GetVector(interp, objv[2], &vecPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Rbc_VectorGetRange(vecPtr, &min, &max) != TCL_OK) {
        return TCL_ERROR;
    }
    resultObjv[0] = Tcl_NewDoubleObj(min);
    resultObjv[1] = Tcl_NewDoubleObj(max);
    Tcl_SetObjResult(interp, Tcl_NewListObj(2, resultObjv));
    return TCL_OK;
}

static int WriteCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *vecPtr;
    Rbc_VectorType type;
    Tcl_Size index;
    Tcl_Size length;
    Tcl_Obj *resultObjv[2];
    int before;
    int after;

    if ((objc != 5) && (objc != 6)) {
        Tcl_WrongNumArgs(interp, 2, objv, "name index real ?imag?");
        return TCL_ERROR;
    }
    if ((GetVector(interp, objv[2], &vecPtr) != TCL_OK) || (Tcl_GetSizeIntFromObj(interp, objv[3], &index) != TCL_OK)) {
        return TCL_ERROR;
    }
    length = Rbc_VectorLength(vecPtr);
    if ((index < 0) || (index >= length)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("index %" TCL_SIZE_MODIFIER "d is out of range", index));
        return TCL_ERROR;
    }
    type = Rbc_VectorGetType(vecPtr);
    before = Rbc_VectorDirty(vecPtr);
    switch (type) {
    case RBC_VECTOR_REAL: {
        double *dataArr;
        double value;

        if (objc != 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "name index value");
            return TCL_ERROR;
        }
        if (Tcl_GetDoubleFromObj(interp, objv[4], &value) != TCL_OK) {
            return TCL_ERROR;
        }
        dataArr = Rbc_VectorData(vecPtr);
        if (dataArr == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("real vector has no data storage", -1));
            return TCL_ERROR;
        }
        dataArr[index] = value;
        break;
    }
    case RBC_VECTOR_COMPLEX: {
        Rbc_Complex *dataArr;
        Rbc_Complex value;

        if (objc != 6) {
            Tcl_WrongNumArgs(interp, 2, objv, "name index real imag");
            return TCL_ERROR;
        }
        if ((Tcl_GetDoubleFromObj(interp, objv[4], &value.real) != TCL_OK) ||
            (Tcl_GetDoubleFromObj(interp, objv[5], &value.imag) != TCL_OK)) {
            return TCL_ERROR;
        }
        dataArr = Rbc_VectorComplexData(vecPtr);
        if (dataArr == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("complex vector has no data storage", -1));
            return TCL_ERROR;
        }
        dataArr[index] = value;
        break;
    }

    default:
        Tcl_SetObjResult(interp, Tcl_NewStringObj("bad vector type", -1));
        return TCL_ERROR;
    }

    Rbc_VectorChanged(vecPtr);
    after = Rbc_VectorDirty(vecPtr);
    resultObjv[0] = Tcl_NewIntObj(before);
    resultObjv[1] = Tcl_NewIntObj(after);
    Tcl_SetObjResult(interp, Tcl_NewListObj(2, resultObjv));
    return TCL_OK;
}

static int ResetVolatileCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *vecPtr;
    Rbc_VectorType type;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
    }
    if (GetVector(interp, objv[2], &vecPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    type = Rbc_VectorGetType(vecPtr);
    if (type == RBC_VECTOR_REAL) {
        double source[3] = {1.25, -2.5, 3.75};
        double *dataArr;
        Tcl_Obj *resultObjv[3];

        if (Rbc_ResetVector(vecPtr, source, 3, 3, TCL_VOLATILE) != TCL_OK) {
            return TCL_ERROR;
        }
        /*
         * If TCL_VOLATILE was honored, changing source must not
         * change the vector.
         */
        source[0] = 99.0;
        source[1] = 99.0;
        source[2] = 99.0;
        dataArr = Rbc_VectorData(vecPtr);
        resultObjv[0] = Tcl_NewDoubleObj(dataArr[0]);
        resultObjv[1] = Tcl_NewDoubleObj(dataArr[1]);
        resultObjv[2] = Tcl_NewDoubleObj(dataArr[2]);
        Tcl_SetObjResult(interp, Tcl_NewListObj(3, resultObjv));
        return TCL_OK;
    }
    if (type == RBC_VECTOR_COMPLEX) {
        Rbc_Complex source[2] = {{1.0, 2.0}, {3.0, -4.0}};
        Rbc_Complex *dataArr;
        Tcl_Obj *resultObjv[2];

        if (Rbc_ResetComplexVector(vecPtr, source, 2, 2, TCL_VOLATILE) != TCL_OK) {
            return TCL_ERROR;
        }
        source[0].real = 99.0;
        source[0].imag = 99.0;
        source[1].real = 99.0;
        source[1].imag = 99.0;
        dataArr = Rbc_VectorComplexData(vecPtr);
        resultObjv[0] = NewComplexObj(dataArr[0]);
        resultObjv[1] = NewComplexObj(dataArr[1]);
        Tcl_SetObjResult(interp, Tcl_NewListObj(2, resultObjv));
        return TCL_OK;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj("bad vector type", -1));
    return TCL_ERROR;
}

static int ResetWrongCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *vecPtr;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
    }
    if (GetVector(interp, objv[2], &vecPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Rbc_VectorGetType(vecPtr) == RBC_VECTOR_REAL) {
        Rbc_Complex value = {1.0, 2.0};

        return Rbc_ResetComplexVector(vecPtr, &value, 1, 1, TCL_VOLATILE);
    } else {
        double value = 1.0;

        return Rbc_ResetVector(vecPtr, &value, 1, 1, TCL_VOLATILE);
    }
}

static void TestFreeProc(void *blockPtr) {
    freeCount++;
    Tcl_Free(blockPtr);
}

static int OwnershipCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *vecPtr;
    Rbc_VectorType type;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 2, objv, "name");
        return TCL_ERROR;
    }
    if (GetVector(interp, objv[2], &vecPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    type = Rbc_VectorGetType(vecPtr);
    freeCount = 0;
    if (type == RBC_VECTOR_REAL) {
        double *ownedArr;
        double replacement[1] = {7.0};

        ownedArr = (double *)Tcl_Alloc(2 * sizeof(*ownedArr));
        ownedArr[0] = 1.0;
        ownedArr[1] = 2.0;
        if (Rbc_ResetVector(vecPtr, ownedArr, 2, 2, TestFreeProc) != TCL_OK) {
            Tcl_Free((char *)ownedArr);
            return TCL_ERROR;
        }
        /*
         * Replacing the storage must invoke TestFreeProc exactly once.
         */
        if (Rbc_ResetVector(vecPtr, replacement, 1, 1, TCL_VOLATILE) != TCL_OK) {
            return TCL_ERROR;
        }
    } else if (type == RBC_VECTOR_COMPLEX) {
        Rbc_Complex *ownedArr;
        Rbc_Complex replacement[1] = {{7.0, -7.0}};

        ownedArr = (Rbc_Complex *)Tcl_Alloc(2 * sizeof(*ownedArr));
        ownedArr[0].real = 1.0;
        ownedArr[0].imag = 2.0;
        ownedArr[1].real = 3.0;
        ownedArr[1].imag = 4.0;
        if (Rbc_ResetComplexVector(vecPtr, ownedArr, 2, 2, TestFreeProc) != TCL_OK) {
            Tcl_Free((char *)ownedArr);
            return TCL_ERROR;
        }
        if (Rbc_ResetComplexVector(vecPtr, replacement, 1, 1, TCL_VOLATILE) != TCL_OK) {
            return TCL_ERROR;
        }
    } else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("bad vector type", -1));
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(freeCount));
    return TCL_OK;
}

static int RbcCapiTestObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *const subcommands[] = {"create",        "exists",     "free",   "inspect", "ownership", "range",
                                              "resetvolatile", "resetwrong", "resize", "write",   NULL};
    enum {
        CMD_CREATE,
        CMD_EXISTS,
        CMD_FREE,
        CMD_INSPECT,
        CMD_OWNERSHIP,
        CMD_RANGE,
        CMD_RESETVOLATILE,
        CMD_RESETWRONG,
        CMD_RESIZE,
        CMD_WRITE
    };
    int index;

    (void)clientData;
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "subcommand ?arg ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], subcommands, "subcommand", 0, &index) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (index) {
    case CMD_CREATE:
        return CreateCmd(interp, objc, objv);
    case CMD_EXISTS:
        return ExistsCmd(interp, objc, objv);
    case CMD_FREE:
        return FreeCmd(interp, objc, objv);
    case CMD_INSPECT:
        return InspectCmd(interp, objc, objv);
    case CMD_OWNERSHIP:
        return OwnershipCmd(interp, objc, objv);
    case CMD_RANGE:
        return RangeCmd(interp, objc, objv);
    case CMD_RESETVOLATILE:
        return ResetVolatileCmd(interp, objc, objv);
    case CMD_RESETWRONG:
        return ResetWrongCmd(interp, objc, objv);
    case CMD_RESIZE:
        return ResizeCmd(interp, objc, objv);
    case CMD_WRITE:
        return WriteCmd(interp, objc, objv);
    }
    Tcl_Panic("bad rbccapitest subcommand");
    return TCL_ERROR;
}

DLLEXPORT int Rbccapitest_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL) {
        return TCL_ERROR;
    }
    if (Rbc_InitStubs(interp, RBC_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }
    Tcl_CreateObjCommand2(interp, "::rbccapitest", RbcCapiTestObjCmd, NULL, NULL);
    return Tcl_PkgProvide(interp, "rbccapitest", "1.0");
}
