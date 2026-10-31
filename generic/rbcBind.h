/*
 * rbcBind.h --
 *
 *      Private object binding tables shared by graphs and legends.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/**
 * @file rbcBind.h
 * @brief Private object picking and Tk binding dispatch interfaces.
 *
 * Tables borrow the window, owner and picked objects. They own their Tk binding
 * table and event-handler registration. Call on the owning Tk thread; the
 * owner must keep the table alive throughout binding-script execution.
 */

#ifndef _RBCBIND
#define _RBCBIND

#include "rbcList.h"
#include <tcl.h>
#include <tk.h>

/** @brief Owner-managed binding table; destroy with Rbc_DestroyBindingTable(). */
typedef struct Rbc_BindTableStruct *Rbc_BindTable;

/**
 * @brief Return a borrowed object at window-relative pixel coordinates.
 *
 * clientData is the owner supplied at construction. Set contextPtr to the
 * object's type or owner context; object and context together identify the
 * target. Return NULL for no hit. The callback must not destroy the binding
 * table. Any callback-capable layout it performs needs owner lifetime protection.
 */
typedef ClientData(Rbc_BindPickProc)(ClientData clientData, int x, int y, ClientData *contextPtr);

/**
 * @brief Append ordered binding-key pointers for a borrowed object/context.
 *
 * list is a temporary one-word-key list owned by dispatch. Append keys without
 * destroying the list or transferring ownership of the key storage. Keys must
 * remain valid for Tk_BindEvent; this callback should not evaluate Tcl scripts.
 */
typedef void(Rbc_BindTagProc)(Rbc_BindTable bindTable, ClientData object, ClientData context, Rbc_List list);

/*
 * Private state. Item pointers and context values are borrowed, not references
 * that retain components. Owners clear them with Rbc_DeleteBindings before
 * freeing an object. newItem exposes the pending Enter target during Leave.
 */

typedef struct Rbc_BindTableStruct {
    unsigned int flags;
    Tk_BindingTable bindingTable; /* Owned Tk table, created eagerly. Keys are
                                   * pointer identities supplied by tagProc,
                                   * not necessarily object addresses. */
    ClientData currentItem;       /* The item currently containing the mouse
                                   * pointer, or NULL if none. */
    ClientData currentContext;    /* One word indicating what kind of object
                                   * was picked. */
    ClientData newItem;           /* The item that is about to become the
                                   * current one, or NULL.  This field is
                                   * used to detect deletions of the new
                                   * current item pointer that occur during
                                   * Leave processing of the previous current
                                   * item.  */
    ClientData newContext;        /* One-word indicating what kind of object
                                   * was just picked. */
    ClientData focusItem;
    ClientData focusContext;
    XEvent pickEvent; /* The event upon which the current choice
                       * of the current item is based.  Must be saved
                       * so that if the current item is deleted,
                       * we can pick another. */
    int activePick;   /* The pick event has been initialized so
                       * that we can repick it */
    int state;        /* Last known modifier state.  Used to
                       * defer picking a new current object
                       * while buttons are down. */
    ClientData clientData;
    Tk_Window tkwin;
    Rbc_BindPickProc *pickProc; /* Routine to report the item the mouse is
                                 * currently over. */
    Rbc_BindTagProc *tagProc;   /* Routine to report tags picked items. */
} Rbc_BindTableStruct;

/**
 * @brief Delete the Tk bindings, unregister the event handler and free a table.
 * @param[in] table Non-NULL live table, with its registered window still valid
 * for Tk_DeleteEventHandler.
 *
 * Does not free the owner, objects or tag-key storage. Frees immediately, so
 * the owner must defer this call until active dispatch/repick calls finish.
 */
void Rbc_DestroyBindingTable(Rbc_BindTable table);

/**
 * @brief Create an empty binding table and register its Tk event handler.
 * @param[in] interp Interpreter used by Tk's binding table.
 * @param[in] tkwin Borrowed live window for key, button, crossing and motion events.
 * @param[in] clientData Borrowed owner; event dispatch preserves this address.
 * @param[in] pickProc Required hit-test callback.
 * @param[in] tagProc Optional tag collector; NULL uses the all UID then the item.
 * @return Owner-managed table; allocation failure is not a recoverable result.
 *
 * Owner destruction must cooperate with Tcl_Preserve/Tcl_Release and defer
 * freeing this table while dispatch is active. Construction does not select
 * an initial current item or retain the owner/window.
 */
Rbc_BindTable Rbc_CreateBindingTable(Tcl_Interp *interp, Tk_Window tkwin, ClientData clientData,
                                     Rbc_BindPickProc *pickProc, Rbc_BindTagProc *tagProc);

/**
 * @brief Query or change bindings for one pointer-identity binding key.
 * @param[in] interp Interpreter receiving the result or diagnostic.
 * @param[in,out] table Live binding table.
 * @param[in] item Stable key, which can be a tag token rather than an object.
 * @param[in] objc Number of arguments; caller validates the supported range 0..2.
 * @param[in] objv Borrowed sequence and optional script objects.
 * @return TCL_OK or TCL_ERROR with interpreter result from this operation/Tk.
 *
 * Zero arguments lists sequences, one queries a sequence, two set it. An empty
 * script deletes the binding; leading '+' appends. Unsupported event masks
 * remove the newly created binding and return an error, without rollback of
 * an earlier binding replaced by that call. No binding script is evaluated here.
 */
int Rbc_ConfigureBindingsFromObj(Tcl_Interp *interp, Rbc_BindTable table, ClientData item, Tcl_Size objc,
                                 Tcl_Obj *const *objv);

/**
 * @brief Repick using the last saved pointer event, if one exists.
 * @param[in,out] table Live table retained by its owner throughout this call.
 *
 * Can synchronously run Leave and Enter scripts. Does not query the pointer,
 * schedule idle work, or acquire a preserve scope. The caller must retain the
 * owner/table and revalidate component/window state after callback execution.
 */
void Rbc_PickCurrentItem(Rbc_BindTable table);

/**
 * @brief Delete one key's bindings and clear matching saved object pointers.
 * @param[in,out] table Live table.
 * @param[in] object Binding key and/or object identity to remove.
 *
 * Clears matching current, pending and focus objects together with their
 * contexts. Does not free the object, evaluate scripts or immediately repick.
 * Distinct name/class/tag keys are not removed merely because an object using
 * those keys is deleted; the owning component manages that tag policy.
 */
void Rbc_DeleteBindings(Rbc_BindTable table, ClientData object);

/**
 * @brief Move the event-handler registration to another live window.
 * @param[in,out] table Live table; any old registered window must still be valid.
 * @param[in] tkwin Borrowed non-NULL destination window.
 *
 * Preserves Tk bindings, owner, current/focus targets and the saved pick event.
 * Does not remap coordinates or repick. The owner manages geometry and lifetime
 * when moving between graph and external-legend windows.
 */
void Rbc_MoveBindingTable(Rbc_BindTable table, Tk_Window tkwin);

#define Rbc_SetFocusItem(bindPtr, object, context)                                                                     \
    ((bindPtr)->focusItem = (ClientData)(object), (bindPtr)->focusContext = (ClientData)(context))

#define Rbc_SetCurrentItem(bindPtr, object, context)                                                                   \
    ((bindPtr)->currentItem = (ClientData)(object), (bindPtr)->currentContext = (ClientData)(context))

#define Rbc_GetCurrentItem(bindPtr) ((bindPtr)->currentItem)
#define Rbc_GetCurrentContext(bindPtr) ((bindPtr)->currentContext)
#define Rbc_GetLatestItem(bindPtr) ((bindPtr)->newItem)

#define Rbc_GetBindingData(bindPtr) ((bindPtr)->clientData)

#endif /* _RBCBIND */
