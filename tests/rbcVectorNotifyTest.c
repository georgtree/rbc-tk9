/*
 * rbcVectorNotifyTest.c -- Internal notification regression harness.
 * Linked with RBC objects in a test-only library; no test command is installed
 * by the normal RBC library and no private function is added to public stubs.
 */
#include "rbcVectorInt.h"
#include <string.h>

typedef struct NotifyTest NotifyTest;
typedef struct {
    NotifyTest *test;
    int index;
} NotifyClient;
struct NotifyTest {
    VectorObject *vector;
    Rbc_VectorId ids[3];
    NotifyClient clients[3];
    Tcl_Obj *log;
    const char *action;
    int acted;
};

static void NotifyTestProc(Tcl_Interp *interp, ClientData data, Rbc_VectorNotify notify);

static void AddClient(Tcl_Interp *interp, NotifyTest *test, int index) {
    test->clients[index].test = test;
    test->clients[index].index = index;
    test->ids[index] = Rbc_AllocVectorId(interp, "::notifyTestVector");
    if (test->ids[index] == NULL) Tcl_Panic("notification test could not attach client");
    Rbc_SetVectorChangedProc(test->ids[index], NotifyTestProc, &test->clients[index]);
}

static void RemoveClient(NotifyTest *test, int index) {
    if (test->ids[index] != NULL) {
        Rbc_FreeVectorId(test->ids[index]);
        test->ids[index] = NULL;
    }
}

static void NotifyTestProc(Tcl_Interp *interp, ClientData data, Rbc_VectorNotify notify) {
    NotifyClient *client = data;
    NotifyTest *test = client->test;
    int index = client->index;
    Tcl_Size first = -1, last = -1;
    Tcl_Obj *entry[4];
    Rbc_VectorGetChangedRange(test->ids[index], &first, &last);
    entry[0] = Tcl_NewIntObj(index);
    entry[1] = Tcl_NewStringObj(notify == RBC_VECTOR_NOTIFY_DESTROY ? "destroy" : "update", -1);
    entry[2] = Tcl_NewWideIntObj(first);
    entry[3] = Tcl_NewWideIntObj(last);
    Tcl_ListObjAppendElement(NULL, test->log, Tcl_NewListObj(4, entry));

    if (index == 0 && !test->acted) {
        test->acted = 1;
        if (strcmp(test->action, "self") == 0 || strcmp(test->action, "destroy-self") == 0) {
            RemoveClient(test, 0);
        } else if (strcmp(test->action, "other") == 0 || strcmp(test->action, "destroy-other") == 0) {
            RemoveClient(test, 1);
        } else if (strcmp(test->action, "destroy") == 0) {
            VectorObject *vector = test->vector;
            test->vector = NULL;
            Rbc_VectorFree(vector);
        } else if (strcmp(test->action, "delete-interp") == 0) {
            Tcl_DeleteInterp(interp);
        } else if (strcmp(test->action, "nested") == 0) {
            Rbc_VectorUpdateClientsRange(test->vector, 1, 1);
            first = last = -1;
            Rbc_VectorGetChangedRange(test->ids[0], &first, &last);
            Tcl_ListObjAppendElement(NULL, test->log,
                Tcl_ObjPrintf("restored %" TCL_SIZE_MODIFIER "d %" TCL_SIZE_MODIFIER "d", first, last));
        } else if (strcmp(test->action, "add") == 0) {
            AddClient(interp, test, 2);
        } else if (strcmp(test->action, "recreate") == 0) {
            Rbc_Vector *replacement;
            if (Rbc_CreateVector(interp, "::notifyTestVector", 2, &replacement) != TCL_OK) {
                Tcl_Panic("notification test could not recreate vector");
            }
            test->vector = (VectorObject *)replacement;
        } else if (strcmp(test->action, "recursive-destroy") == 0) {
            Rbc_VectorFree(test->vector);
        }
    }
    if (notify == RBC_VECTOR_NOTIFY_DESTROY) {
        test->ids[index] = NULL;
        if (strcmp(test->action, "recreate") != 0) test->vector = NULL;
    }
}

static int NotifyTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    NotifyTest test;
    Rbc_Vector *vector;
    Tcl_Interp *target = interp;
    int immediate, destroy;
    (void)data;
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "scenario immediate");
        return TCL_ERROR;
    }
    if (Tcl_GetBooleanFromObj(interp, objv[2], &immediate) != TCL_OK) return TCL_ERROR;
    memset(&test, 0, sizeof(test));
    test.action = Tcl_GetString(objv[1]);
    test.log = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(test.log);
    if (strcmp(test.action, "delete-interp") == 0) {
        target = Tcl_CreateInterp();
        Tcl_CreateNamespace(target, "::rbc", NULL, NULL);
        if (Rbc_VectorInit(target) != TCL_OK) Tcl_Panic("notification test init failed");
    }
    if (Rbc_CreateVector(target, "::notifyTestVector", 2, &vector) != TCL_OK) {
        Tcl_DecrRefCount(test.log);
        return TCL_ERROR;
    }
    test.vector = (VectorObject *)vector;
    test.vector->notifyFlags = immediate ? NOTIFY_ALWAYS : NOTIFY_WHENIDLE;
    AddClient(target, &test, 0);
    AddClient(target, &test, 1);
    destroy = strncmp(test.action, "destroy-", 8) == 0 || strcmp(test.action, "recreate") == 0 ||
              strcmp(test.action, "recursive-destroy") == 0;
    if (destroy) {
        VectorObject *old = test.vector;
        /* Also exercise cancellation of a queued update before destruction. */
        if (!immediate) Rbc_VectorUpdateClientsRange(old, 0, 0);
        Rbc_VectorFree(old);
        if (strcmp(test.action, "recreate") != 0) test.vector = NULL;
    } else {
        Rbc_VectorUpdateClientsRange(test.vector, 0, 0);
    }
    while (Tcl_DoOneEvent(TCL_IDLE_EVENTS | TCL_DONT_WAIT)) {}
    if (strcmp(test.action, "add") == 0) {
        Rbc_VectorUpdateClientsRange(test.vector, 1, 1);
        while (Tcl_DoOneEvent(TCL_IDLE_EVENTS | TCL_DONT_WAIT)) {}
    }
    /* Detach test clients before cleanup so the log includes only the action. */
    for (int i = 0; i < 3; i++) RemoveClient(&test, i);
    if (test.vector != NULL) Rbc_VectorFree(test.vector);
    Tcl_SetObjResult(interp, test.log);
    Tcl_DecrRefCount(test.log);
    return TCL_OK;
}

/* FFT callbacks use this fixture only while the test command is active. */
typedef struct FftTest FftTest;
typedef struct {
    FftTest *test;
    int index;
} FftClient;
typedef struct {
    double values[2];
    FftTest *test;
    int index;
} FftStorage;
struct FftTest {
    Tcl_Interp *interp;
    VectorObject *dest[2];
    Rbc_VectorId ids[2];
    FftClient clients[2];
    const char *phase;
    const char *action;
    const char *operation;
    int acted;
    int valid;
    int releases;
    int updates[2];
    int count;
};

static void FftTestAction(FftTest *test) {
    int k;
    if (test->acted) return;
    test->acted = 1;
    if (strstr(test->operation, "natural") != NULL || strstr(test->operation, "quadratic") != NULL) {
        VectorObject *v = test->dest[0];
        if (v == NULL || v->length != 5) test->valid = 0;
        else for (k = 0; k < 5; k++) {
            if (v->type == RBC_VECTOR_REAL) {
                if (fabs(v->data.real[k] - 0.5 * k) > 1e-12) test->valid = 0;
            } else if (fabs(v->data.complex[k].real - 0.5 * k) > 1e-6 ||
                       fabs(v->data.complex[k].imag - k) > 1e-6) test->valid = 0;
        }
    } else {
    /* All destination values must be installed before the first callback. */
    for (k = 0; k < test->count; k++) {
        double a = 1.0, b = 1.0;
        if (strcmp(test->operation, "ifft") == 0) a = b = 0.5;
        if (test->count == 2 && k == 1) a = b = 0.0;
        if (strcmp(test->operation, "fftfreq") == 0) { a = 0.0; b = -0.5; }
        if (test->dest[k] == NULL || test->dest[k]->length != 2 ||
            test->dest[k]->data.real[0] != a || test->dest[k]->data.real[1] != b) test->valid = 0;
    }
    }
    if (strcmp(test->action, "self") == 0) {
        Rbc_VectorFree(test->dest[0]);
    } else if (strcmp(test->action, "other") == 0) {
        Rbc_VectorFree(test->dest[1]);
    } else if (strcmp(test->action, "both") == 0 || strcmp(test->action, "recreate") == 0) {
        for (k = 0; k < test->count; k++) Rbc_VectorFree(test->dest[k]);
        if (strcmp(test->action, "recreate") == 0) {
            Rbc_Vector *replacement;
            if (Rbc_CreateVector(test->interp, "::fftTestReal", 1, &replacement) != TCL_OK) test->valid = 0;
            else Rbc_VectorData(replacement)[0] = 99.0;
        }
    } else if (strcmp(test->action, "interp") == 0) {
        Tcl_DeleteInterp(test->interp);
    } else if (strcmp(test->action, "nested") == 0) {
        if (Tcl_EvalEx(test->interp, "::fftTestSource fft -real ::fftTestReal -imag ::fftTestImag", -1,
                       TCL_EVAL_GLOBAL) != TCL_OK) test->valid = 0;
    } else if (strcmp(test->action, "rename") == 0) {
        if (Tcl_EvalEx(test->interp, "rbc::vector rename ::fftTestReal ::fftTestRenamed", -1,
                       TCL_EVAL_GLOBAL) != TCL_OK) test->valid = 0;
    }
}

static void FftTestNotify(Tcl_Interp *interp, ClientData data, Rbc_VectorNotify notify) {
    FftClient *client = data;
    FftTest *test = client->test;
    int k = client->index;
    (void)interp;
    if (notify == RBC_VECTOR_NOTIFY_DESTROY) {
        test->dest[k] = NULL;
        test->ids[k] = NULL;
    } else {
        test->updates[k]++;
        if (k == 0 && strcmp(test->phase, "notify") == 0) FftTestAction(test);
    }
}

static void FftTestRelease(void *data) {
    FftStorage *storage = (FftStorage *)data;
    FftTest *test = storage->test;
    test->releases++;
    if (storage->index == 0 && strcmp(test->phase, "release") == 0) FftTestAction(test);
    ckfree(storage);
}

static char *FftTestTrace(ClientData data, Tcl_Interp *interp, const char *part1,
                          const char *part2, int flags) {
    (void)interp;
    (void)part1;
    (void)part2;
    (void)flags;
    FftTestAction(data);
    return NULL;
}

static int FftTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    FftTest test;
    Tcl_Interp *target;
    Rbc_Vector *source;
    Tcl_Obj *command, *answer;
    const char *names[] = {"::fftTestReal", "::fftTestImag"};
    int k, code, validName;
    (void)data;
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "operation phase action");
        return TCL_ERROR;
    }
    memset(&test, 0, sizeof(test));
    test.operation = Tcl_GetString(objv[1]);
    test.phase = Tcl_GetString(objv[2]);
    test.action = Tcl_GetString(objv[3]);
    test.count = (strcmp(test.operation, "fft") == 0 || strcmp(test.operation, "ifft") == 0) ? 2 : 1;
    test.valid = 1;
    target = test.interp = Tcl_CreateInterp();
    /* Keep the test interpreter available for checking its result after deletion. */
    Tcl_Preserve(target);
    Tcl_CreateNamespace(target, "::rbc", NULL, NULL);
    if (Rbc_VectorInit(target) != TCL_OK ||
        Rbc_CreateVector(target, "::fftTestSource", 2, &source) != TCL_OK) Tcl_Panic("FFT test init failed");
    Rbc_VectorData(source)[0] = 1.0;
    Rbc_VectorData(source)[1] = 0.0;
    for (k = 0; k < test.count; k++) {
        Rbc_Vector *vector;
        FftStorage *storage = ckalloc(sizeof(*storage));
        if (Rbc_CreateVector(target, names[k], 2, &vector) != TCL_OK) Tcl_Panic("FFT test create failed");
        test.dest[k] = (VectorObject *)vector;
        test.dest[k]->notifyFlags = NOTIFY_NEVER;
        test.dest[k]->flush = strcmp(test.phase, "trace") == 0;
        storage->values[0] = 3.0;
        storage->values[1] = 4.0;
        storage->test = &test;
        storage->index = k;
        if (Rbc_ResetVector(vector, storage->values, 2, 2, FftTestRelease) != TCL_OK)
            Tcl_Panic("FFT test reset failed");
        test.dest[k]->notifyFlags = NOTIFY_ALWAYS;
        test.clients[k].test = &test;
        test.clients[k].index = k;
        test.ids[k] = Rbc_AllocVectorId(target, names[k]);
        Rbc_SetVectorChangedProc(test.ids[k], FftTestNotify, &test.clients[k]);
    }
    if (strcmp(test.phase, "trace") == 0) {
        Tcl_TraceVar2(target, names[0], NULL, TCL_GLOBAL_ONLY | TCL_TRACE_UNSETS, FftTestTrace, &test);
    }
    if (test.count == 2) {
        command = Tcl_ObjPrintf("::fftTestSource %s -real ::fftTestReal -imag ::fftTestImag", test.operation);
    } else if (strcmp(test.operation, "window") == 0) {
        command = Tcl_NewStringObj("::fftTestReal window rectangular 2", -1);
    } else {
        command = Tcl_NewStringObj("::fftTestReal fftfreq 2", -1);
    }
    Tcl_IncrRefCount(command);
    code = Tcl_EvalObjEx(target, command, TCL_EVAL_GLOBAL);
    Tcl_DecrRefCount(command);
    validName = strcmp(Tcl_GetStringResult(target), test.count == 2 ?
                       "::fftTestReal ::fftTestImag" : "::fftTestReal") == 0;
    if (strcmp(test.action, "recreate") == 0) {
        Rbc_Vector *replacement;
        if (Rbc_GetVector(target, "::fftTestReal", &replacement) != TCL_OK ||
            Rbc_VectorLength(replacement) != 1 || Rbc_VectorData(replacement)[0] != 99.0) test.valid = 0;
    }
    answer = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(NULL, answer, Tcl_NewIntObj(code));
    Tcl_ListObjAppendElement(NULL, answer, Tcl_NewIntObj(test.acted && test.valid && validName));
    Tcl_ListObjAppendElement(NULL, answer, Tcl_NewIntObj(test.releases));
    Tcl_ListObjAppendElement(NULL, answer, Tcl_NewIntObj(test.updates[0]));
    Tcl_ListObjAppendElement(NULL, answer, Tcl_NewIntObj(test.updates[1]));
    Tcl_IncrRefCount(answer);
    /* Observers clear their IDs during interpreter cleanup. */
    if (!Tcl_InterpDeleted(target)) Tcl_DeleteInterp(target);
    Tcl_Release(target);
    Tcl_SetObjResult(interp, answer);
    Tcl_DecrRefCount(answer);
    return TCL_OK;
}

/* Spline outputs change length from two to five, so resize notifications
 * would expose incomplete data in the old implementation. */
static int SplineTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    FftTest test;
    Tcl_Interp *target;
    Rbc_Vector *vector;
    Tcl_Obj *command, *answer;
    int code, complex;
    (void)data;
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "operation phase action");
        return TCL_ERROR;
    }
    memset(&test, 0, sizeof(test));
    test.operation = Tcl_GetString(objv[1]);
    test.phase = Tcl_GetString(objv[2]);
    test.action = Tcl_GetString(objv[3]);
    test.count = test.valid = 1;
    complex = test.operation[0] == 'p';
    target = test.interp = Tcl_CreateInterp();
    Tcl_Preserve(target);
    Tcl_CreateNamespace(target, "::rbc", NULL, NULL);
    Rbc_VectorInit(target);
    Rbc_SplineInit(target);
    if (Tcl_EvalEx(target, "rbc::vector create sx sy sq; sx set {0 1 2}; sy set {0 1 2}; "
                   "sq set {0 .5 1 1.5 2}; rbc::vector create sz -type complex; sz set {{0 0} {1 2} {2 4}}",
                   -1, TCL_EVAL_GLOBAL) != TCL_OK) Tcl_Panic("spline test init failed");
    if (Rbc_CreateVectorWithType(target, "::fftTestReal", 2,
                                 complex ? RBC_VECTOR_COMPLEX : RBC_VECTOR_REAL, &vector) != TCL_OK)
        Tcl_Panic("spline destination create failed");
    test.dest[0] = (VectorObject *)vector;
    test.dest[0]->notifyFlags = NOTIFY_NEVER;
    test.dest[0]->flush = strcmp(test.phase, "trace") == 0;
    if (!complex) {
        FftStorage *storage = ckalloc(sizeof(*storage));
        storage->values[0] = 3; storage->values[1] = 4;
        storage->test = &test; storage->index = 0;
        Rbc_ResetVector(vector, storage->values, 2, 2, FftTestRelease);
    }
    test.dest[0]->notifyFlags = NOTIFY_ALWAYS;
    test.clients[0].test = &test;
    test.ids[0] = Rbc_AllocVectorId(target, "::fftTestReal");
    Rbc_SetVectorChangedProc(test.ids[0], FftTestNotify, &test.clients[0]);
    if (strcmp(test.phase, "trace") == 0)
        Tcl_TraceVar2(target, "::fftTestReal", NULL, TCL_GLOBAL_ONLY | TCL_TRACE_UNSETS, FftTestTrace, &test);
    command = complex ? Tcl_ObjPrintf("rbc::spline parametric %s sz ::fftTestReal 5", test.operation + 1) :
                        Tcl_ObjPrintf("rbc::spline %s sx sy sq ::fftTestReal", test.operation);
    Tcl_IncrRefCount(command);
    code = Tcl_EvalObjEx(target, command, TCL_EVAL_GLOBAL);
    Tcl_DecrRefCount(command);
    if (strcmp(test.action, "recreate") == 0) {
        if (Rbc_GetVector(target, "::fftTestReal", &vector) != TCL_OK ||
            Rbc_VectorLength(vector) != 1 || Rbc_VectorData(vector)[0] != 99.0) test.valid = 0;
    }
    answer = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(NULL, answer, Tcl_NewIntObj(code));
    Tcl_ListObjAppendElement(NULL, answer, Tcl_NewIntObj(test.acted && test.valid));
    Tcl_ListObjAppendElement(NULL, answer, Tcl_NewIntObj(test.updates[0]));
    Tcl_ListObjAppendElement(NULL, answer, Tcl_NewIntObj(test.releases));
    Tcl_IncrRefCount(answer);
    if (!Tcl_InterpDeleted(target)) Tcl_DeleteInterp(target);
    Tcl_Release(target);
    Tcl_SetObjResult(interp, answer);
    Tcl_DecrRefCount(answer);
    return TCL_OK;
}

DLLEXPORT int Rbcnotifytest_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
    if (Tcl_PkgPresent(interp, "rbc::vector", NULL, 0) != NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("load notification harness in a fresh interpreter", -1));
        return TCL_ERROR;
    }
    Tcl_ResetResult(interp);
    if (Tcl_FindNamespace(interp, "::rbc", NULL, 0) == NULL &&
        Tcl_CreateNamespace(interp, "::rbc", NULL, NULL) == NULL) return TCL_ERROR;
    if (Rbc_VectorInit(interp) != TCL_OK) return TCL_ERROR;
    Tcl_CreateObjCommand2(interp, "::rbcnotifytest", NotifyTestCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::rbcffttest", FftTestCmd, NULL, NULL);
    Rbc_SplineInit(interp);
    Tcl_CreateObjCommand2(interp, "::rbcsplinetest", SplineTestCmd, NULL, NULL);
    return Tcl_PkgProvide(interp, "rbcnotifytest", "1.0");
}
