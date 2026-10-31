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
    return Tcl_PkgProvide(interp, "rbcnotifytest", "1.0");
}
