/* Private temporary-image harness. Inject acquisition failure and count instance
 * releases without adding production commands or installed C API entries. */
#include "rbcInt.h"
#include "rbcImage.h"

static int failAcquire, instanceFrees;
static Tcl_Obj *createdName;

static Tk_Image TestGetImage(Tcl_Interp *interp, Tk_Window tkwin, const char *name,
                            Tk_ImageChangedProc *changeProc, ClientData data) {
    createdName = Tcl_NewStringObj(name, -1);
    Tcl_IncrRefCount(createdName);
    if (failAcquire) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("forced image acquisition failure", -1));
        Tcl_SetErrorCode(interp, "RBC", "TEST", "ACQUIRE", NULL);
        return NULL;
    }
    return Tk_GetImage(interp, tkwin, name, changeProc, data);
}
static void TestFreeImage(Tk_Image image) {
    instanceFrees++;
    Tk_FreeImage(image);
}
#undef Tk_GetImage
#undef Tk_FreeImage
#define Tk_GetImage TestGetImage
#define Tk_FreeImage TestFreeImage
#include "../generic/rbcImage.c"
#undef Tk_GetImage
#undef Tk_FreeImage

static int ImageTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_Image image = NULL;
    Tcl_Obj *result, *savedResult;
    const char *scenario;
    int code, exists, renamed = 0, blocked = 0;
    (void)data;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "scenario");
        return TCL_ERROR;
    }
    scenario = Tcl_GetString(objv[1]);
    createdName = NULL;
    instanceFrees = 0;
    failAcquire = strcmp(scenario, "acquire-fail") == 0;
    if (strcmp(scenario, "null") != 0) {
        image = Rbc_CreateTemporaryImage(interp, Tk_MainWindow(interp), NULL);
        if (image == NULL && !failAcquire) return TCL_ERROR;
    }
    if (failAcquire) {
        savedResult = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(savedResult);
        exists = Tk_FindPhoto(interp, Tcl_GetString(createdName)) != NULL;
        result = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(image != NULL));
        Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(exists));
        Tcl_ListObjAppendElement(NULL, result, savedResult);
        Tcl_DecrRefCount(savedResult);
        /* Also make the unfixed implementation safe to run repeatedly. */
        if (exists) Tk_DeleteImage(interp, Tcl_GetString(createdName));
    } else {
        if (strcmp(scenario, "deleted") == 0) Tk_DeleteImage(interp, Tcl_GetString(createdName));
        if (strcmp(scenario, "blocked") == 0 || strcmp(scenario, "renamed") == 0) {
            if (Tcl_EvalEx(interp, "rename ::image ::rbcTestOriginalImage", -1, TCL_EVAL_GLOBAL) != TCL_OK)
                goto error;
            renamed = 1;
            if (strcmp(scenario, "blocked") == 0) {
                if (Tcl_EvalEx(interp, "proc ::image {args} {error {image command blocked}}", -1,
                               TCL_EVAL_GLOBAL) != TCL_OK) goto error;
                blocked = 1;
            }
        }
        Tcl_SetObjResult(interp, Tcl_NewStringObj("retained result", -1));
        code = Rbc_DestroyTemporaryImage(interp, image);
        savedResult = Tcl_GetObjResult(interp);
        Tcl_IncrRefCount(savedResult);
        exists = createdName != NULL && Tk_FindPhoto(interp, Tcl_GetString(createdName)) != NULL;
        result = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(code));
        Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(instanceFrees));
        Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(exists));
        Tcl_ListObjAppendElement(NULL, result, savedResult);
        Tcl_DecrRefCount(savedResult);
        if (image != NULL && instanceFrees == 0) {
            if (exists) Tk_DeleteImage(interp, Tcl_GetString(createdName));
            TestFreeImage(image);
        }
        image = NULL;
    }
    Tcl_IncrRefCount(result);
    if (blocked) Tcl_EvalEx(interp, "rename ::image {}", -1, TCL_EVAL_GLOBAL);
    if (renamed) Tcl_EvalEx(interp, "rename ::rbcTestOriginalImage ::image", -1, TCL_EVAL_GLOBAL);
    if (createdName != NULL) Tcl_DecrRefCount(createdName);
    createdName = NULL;
    Tcl_SetObjResult(interp, result);
    Tcl_DecrRefCount(result);
    return TCL_OK;
error:
    if (blocked) Tcl_EvalEx(interp, "rename ::image {}", -1, TCL_EVAL_GLOBAL);
    if (renamed) Tcl_EvalEx(interp, "rename ::rbcTestOriginalImage ::image", -1, TCL_EVAL_GLOBAL);
    if (image != NULL) {
        Tk_DeleteImage(interp, Tcl_GetString(createdName));
        TestFreeImage(image);
    }
    if (createdName != NULL) Tcl_DecrRefCount(createdName);
    createdName = NULL;
    return TCL_ERROR;
}
DLLEXPORT int Rbcimagetest_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL || Tk_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
    Tcl_CreateObjCommand2(interp, "::rbcimagetest", ImageTestCmd, NULL, NULL);
    return Tcl_PkgProvide(interp, "rbcimagetest", "1.0");
}
