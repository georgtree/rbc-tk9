/* Private image harness. Inject acquisition failures and photo-block layouts,
 * and count releases without adding production commands or installed C API. */
#include "rbcInt.h"
#include "rbcImage.h"

static int failAcquire, instanceFrees;
static Tk_PhotoImageBlock *testPhotoBlock;
static Tcl_Obj *createdName;

/* Inject block layouts that Tk's native photo storage does not normally expose. */
static int TestPhotoGetImage(Tk_PhotoHandle photo, Tk_PhotoImageBlock *blockPtr) {
    if (testPhotoBlock != NULL) {
        *blockPtr = *testPhotoBlock;
        return 1;
    }
    return Tk_PhotoGetImage(photo, blockPtr);
}

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
#undef Tk_PhotoGetImage
#define Tk_PhotoGetImage TestPhotoGetImage
#include "../generic/rbcImage.c"
#undef Tk_PhotoGetImage
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
/* Return complete and cropped RGBA snapshots from a padded synthetic block. */
static int PhotoBlockTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_PhotoImageBlock block;
    unsigned char pixels[2 * (3 * 4 + 5)];
    Rbc_ColorImage full, crop;
    Tcl_Obj *result;
    int pixelSize, x, y, i, j;
    (void)data;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "pixelSize");
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &pixelSize) != TCL_OK) return TCL_ERROR;
    if (pixelSize < 1 || pixelSize > 4) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("pixelSize must be 1 through 4", -1));
        return TCL_ERROR;
    }
    memset(pixels, 0xEE, sizeof(pixels));
    memset(&block, 0, sizeof(block));
    block.pixelPtr = pixels;
    block.width = 3;
    block.height = 2;
    block.pixelSize = pixelSize;
    block.pitch = 3 * pixelSize + 5;
    if (pixelSize == 2) {
        block.offset[0] = block.offset[1] = block.offset[2] = 1;
    } else if (pixelSize == 3) {
        block.offset[0] = 2;
        block.offset[1] = 0;
        block.offset[2] = 1;
    } else if (pixelSize == 4) {
        block.offset[0] = 2;
        block.offset[1] = 1;
        block.offset[2] = 0;
        block.offset[3] = 3;
    }
    for (y = 0; y < 2; y++) {
        for (x = 0; x < 3; x++) {
            unsigned char *pixel = pixels + y * block.pitch + x * pixelSize;
            int n = y * 3 + x;
            pixel[block.offset[0]] = 10 + n;
            if (pixelSize >= 3) {
                pixel[block.offset[1]] = 30 + n;
                pixel[block.offset[2]] = 50 + n;
            }
            if (pixelSize == 2) pixel[block.offset[3]] = 100 + n;
            if (pixelSize == 4) pixel[block.offset[3]] = 200 + n;
        }
    }
    testPhotoBlock = &block;
    full = Rbc_PhotoToColorImage(NULL);
    crop = Rbc_PhotoRegionToColorImage(NULL, 1, 0, 2, 2);
    testPhotoBlock = NULL;
    result = Tcl_NewListObj(0, NULL);
    for (j = 0; j < 2; j++) {
        Rbc_ColorImage image = j == 0 ? full : crop;
        Tcl_Obj *values = Tcl_NewListObj(0, NULL);
        for (i = 0; i < image->width * image->height; i++) {
            Pix32 *pixel = image->bits + i;
            Tcl_Obj *rgba[4];
            rgba[0] = Tcl_NewIntObj(pixel->rgba.red);
            rgba[1] = Tcl_NewIntObj(pixel->rgba.green);
            rgba[2] = Tcl_NewIntObj(pixel->rgba.blue);
            rgba[3] = Tcl_NewIntObj(pixel->rgba.alpha);
            Tcl_ListObjAppendElement(NULL, values, Tcl_NewListObj(4, rgba));
        }
        Tcl_ListObjAppendElement(NULL, result, values);
    }
    Rbc_FreeColorImage(full);
    Rbc_FreeColorImage(crop);
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

DLLEXPORT int Rbcimagetest_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL || Tk_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
    Tcl_CreateObjCommand2(interp, "::rbcimagetest", ImageTestCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::rbcphotoblocktest", PhotoBlockTestCmd, NULL, NULL);
    return Tcl_PkgProvide(interp, "rbcimagetest", "1.0");
}
