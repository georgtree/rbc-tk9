/*
 * rbcBind.c --
 *
 *      This module implements object binding procedures for the rbc
 *      toolkit.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/**
 * @file rbcBind.c
 * @brief Shared object event dispatch, current-item tracking and Tk bindings.
 */

/**
 * @defgroup binding_callbacks Binding dispatch callbacks
 * @brief Private event delivery and pointer transition machinery.
 */

#include "rbcInt.h"
#include "rbcBind.h"

/*
 * Binding table procedures.
 */
#define REPICK_IN_PROGRESS (1 << 0)
#define LEFT_GRABBED_ITEM (1 << 1)

#define ALL_BUTTONS_MASK (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)

#ifndef VirtualEventMask
#define VirtualEventMask (1L << 30)
#endif

#define ALL_VALID_EVENTS_MASK                                                                                          \
    (ButtonMotionMask | Button1MotionMask | Button2MotionMask | Button3MotionMask | Button4MotionMask |                \
     Button5MotionMask | ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask | KeyPressMask |      \
     KeyReleaseMask | PointerMotionMask | VirtualEventMask)

/*
 * Prototypes for procedures referenced only in this file:
 */

static Tk_EventProc BindProc;
static void DoEvent(Rbc_BindTableStruct *bindPtr, XEvent *eventPtr, ClientData item, ClientData context);
static void PickCurrentItem(Rbc_BindTableStruct *bindPtr, XEvent *eventPtr);

/*
 * The default path retains the current target during button grabs.
 * FULLY_SIMULATE_GRAB enables the historical extra crossing-event path below;
 * it does not introduce dedicated DragEnter/DragMotion/DragLeave virtual events.
 */

/**
 * @ingroup binding_callbacks
 * @brief Collect binding keys and synchronously dispatch one event through Tk.
 * @param[in,out] bindPtr Table retained by its owner for the call.
 * @param[in] eventPtr Borrowed real or synthesized event.
 * @param[in] item Borrowed picked object, or NULL to skip delivery.
 * @param[in] context Borrowed object type/owner discriminator.
 *
 * Key events prefer an explicit focus object. Skips absent/dead windows and
 * absent objects. Calls tagProc (or supplies all/item keys), copies keys to a
 * stack array of up to 32 entries or an allocated array, then calls Tk_BindEvent.
 * Releases only temporary list/array storage. Scripts can delete components;
 * this helper does not preserve the owner or binding table itself.
 */
static void DoEvent(Rbc_BindTableStruct *bindPtr, XEvent *eventPtr, ClientData item, ClientData context) {
    Rbc_List bindIds;
    Tcl_Size nIds;

    if ((bindPtr->tkwin == NULL) || (bindPtr->bindingTable == NULL)) {
        return;
    }
    /*
     * If a binding client has established an explicit focus item,
     * route key events to it.  Otherwise use the current picked item
     * supplied by BindProc.
     */
    if (((eventPtr->type == KeyPress) || (eventPtr->type == KeyRelease)) && (bindPtr->focusItem != NULL)) {
        item = bindPtr->focusItem;
        context = bindPtr->focusContext;
    }
    if (item == NULL || bindPtr->tkwin == NULL ||
        (((Tk_FakeWin *)bindPtr->tkwin)->flags & TK_ALREADY_DEAD)) {
        return;
    }
    bindIds = Rbc_ListCreate(TCL_ONE_WORD_KEYS);
    if (bindPtr->tagProc == NULL) {
        Rbc_ListAppend(bindIds, (const char *)Tk_GetUid("all"), NULL);
        Rbc_ListAppend(bindIds, (const char *)item, NULL);
    } else {
        (*bindPtr->tagProc)(bindPtr, item, context, bindIds);
    }
    nIds = Rbc_ListGetLength(bindIds);
    if (nIds > 0) {
        ClientData *idArray;
        ClientData tags[32];
        Rbc_ListNode node;
        Tcl_Size i;
        int dynamic;

        dynamic = (nIds > 32);
        if (dynamic) {
            idArray = RbcCalloc((size_t)nIds, sizeof(*idArray));
        } else {
            idArray = tags;
        }
        i = 0;
        for (node = Rbc_ListFirstNode(bindIds); node != NULL; node = Rbc_ListNextNode(node)) {
            assert(i < nIds);
            idArray[i++] = (ClientData)Rbc_ListGetKey(node);
        }
        assert(i == nIds);
        Tk_BindEvent(bindPtr->bindingTable, eventPtr, bindPtr->tkwin, nIds, idArray);
        if (dynamic) {
            ckfree(idArray);
        }
    }
    Rbc_ListDestroy(bindIds);
}

/**
 * @ingroup binding_callbacks
 * @brief Update the current object/context and synthesize crossing events.
 * @param[in,out] bindPtr Table retained by the caller across callback execution.
 * @param[in] eventPtr Pointer-location event, or the table's saved pickEvent.
 *
 * Saves pointer state for later repicks. LeaveNotify clears the hit without
 * calling pickProc. Both object and context participate in target comparison.
 * Publishes the pending target before Leave so Rbc_DeleteBindings can cancel
 * its subsequent Enter. Recursive repick during Leave saves the event but
 * returns without another transition. Button state controls grab behavior.
 */
static void PickCurrentItem(Rbc_BindTableStruct *bindPtr, XEvent *eventPtr) {
    unsigned int buttonDown;
    ClientData newItem;
    ClientData newContext;

    /*
     * Check whether or not a button is down.  If so, we'll log entry
     * and exit into and out of the current item, but not entry into
     * any other item.  This implements a form of grabbing equivalent
     * to what the X server does for windows.
     */
    buttonDown = (bindPtr->state & ALL_BUTTONS_MASK);
    if (!buttonDown) {
        bindPtr->flags &= ~LEFT_GRABBED_ITEM;
    }
    /*
     * Save information about this event in the widget.  The event in
     * the widget is used for two purposes:
     *
     * 1. Event bindings: if the current item changes, fake events are
     *    generated to allow item-enter and item-leave bindings to trigger.
     * 2. Reselection: if the current item gets deleted, can use the
     *    saved event to find a new current item.
     * Translate MotionNotify events into EnterNotify events, since that's
     * what gets reported to item handlers.
     */
    if (eventPtr != &bindPtr->pickEvent) {
        if ((eventPtr->type == MotionNotify) || (eventPtr->type == ButtonRelease)) {
            bindPtr->pickEvent.xcrossing.type = EnterNotify;
            bindPtr->pickEvent.xcrossing.serial = eventPtr->xmotion.serial;
            bindPtr->pickEvent.xcrossing.send_event = eventPtr->xmotion.send_event;
            bindPtr->pickEvent.xcrossing.display = eventPtr->xmotion.display;
            bindPtr->pickEvent.xcrossing.window = eventPtr->xmotion.window;
            bindPtr->pickEvent.xcrossing.root = eventPtr->xmotion.root;
            bindPtr->pickEvent.xcrossing.subwindow = None;
            bindPtr->pickEvent.xcrossing.time = eventPtr->xmotion.time;
            bindPtr->pickEvent.xcrossing.x = eventPtr->xmotion.x;
            bindPtr->pickEvent.xcrossing.y = eventPtr->xmotion.y;
            bindPtr->pickEvent.xcrossing.x_root = eventPtr->xmotion.x_root;
            bindPtr->pickEvent.xcrossing.y_root = eventPtr->xmotion.y_root;
            bindPtr->pickEvent.xcrossing.mode = NotifyNormal;
            bindPtr->pickEvent.xcrossing.detail = NotifyNonlinear;
            bindPtr->pickEvent.xcrossing.same_screen = eventPtr->xmotion.same_screen;
            bindPtr->pickEvent.xcrossing.focus = False;
            bindPtr->pickEvent.xcrossing.state = eventPtr->xmotion.state;
        } else {
            bindPtr->pickEvent = *eventPtr;
        }
    }
    bindPtr->activePick = TRUE;
    /*
     * If this is a recursive call (there's already a partially completed
     * call pending on the stack;  it's in the middle of processing a
     * Leave event handler for the old current item) then just return;
     * the pending call will do everything that's needed.
     */
    if (bindPtr->flags & REPICK_IN_PROGRESS) {
        return;
    }
    /*
     * A LeaveNotify event automatically means that there's no current
     * item, so the check for closest item can be skipped.
     */
    newContext = NULL;
    if (bindPtr->pickEvent.type != LeaveNotify) {
        int x, y;

        x = bindPtr->pickEvent.xcrossing.x;
        y = bindPtr->pickEvent.xcrossing.y;
        newItem = (*bindPtr->pickProc)(bindPtr->clientData, x, y, &newContext);
    } else {
        newItem = NULL;
    }
    if (((newItem == bindPtr->currentItem) && (newContext == bindPtr->currentContext)) &&
        (!(bindPtr->flags & LEFT_GRABBED_ITEM))) {
        /*
         * Nothing to do:  the current item hasn't changed.
         */
        return;
    }
#ifndef FULLY_SIMULATE_GRAB
    if (((newItem != bindPtr->currentItem) || (newContext != bindPtr->currentContext)) && (buttonDown)) {
        bindPtr->flags |= LEFT_GRABBED_ITEM;
        return;
    }
#endif
    /*
     * Simulate a LeaveNotify event on the previous current item and
     * an EnterNotify event on the new current item.  Remove the "current"
     * tag from the previous current item and place it on the new current
     * item.
     */
    if ((bindPtr->currentItem != NULL) &&
        ((newItem != bindPtr->currentItem) || (newContext != bindPtr->currentContext)) &&
        !(bindPtr->flags & LEFT_GRABBED_ITEM)) {
        XEvent event;

        event = bindPtr->pickEvent;
        event.type = LeaveNotify;
        /*
         * If the event's detail happens to be NotifyInferior the
         * binding mechanism will discard the event.  To be consistent,
         * always use NotifyAncestor.
         */
        event.xcrossing.detail = NotifyAncestor;
        /* Publish the pending target so deletion from a Leave script can
         * clear it through Rbc_DeleteBindings before we deliver Enter. */
        if (!buttonDown) {
            bindPtr->newItem = newItem;
            bindPtr->newContext = newContext;
        }
        bindPtr->flags |= REPICK_IN_PROGRESS;
        DoEvent(bindPtr, &event, bindPtr->currentItem, bindPtr->currentContext);
        bindPtr->flags &= ~REPICK_IN_PROGRESS;
        if (!buttonDown) {
            newItem = bindPtr->newItem;
            newContext = bindPtr->newContext;
        }
        /*
         * Note:  during DoEvent above, it's possible that
         * bindPtr->newItem got reset to NULL because the
         * item was deleted.
         */
    }
    if (((newItem != bindPtr->currentItem) || (newContext != bindPtr->currentContext)) && (buttonDown)) {
        XEvent event;

        bindPtr->flags |= LEFT_GRABBED_ITEM;
        event = bindPtr->pickEvent;
        if ((newItem != bindPtr->newItem) || (newContext != bindPtr->newContext)) {
            ClientData savedItem;
            ClientData savedContext;

            /*
             * Generate <Enter> and <Leave> events for objects during
             * button grabs.  This isn't standard. But for example, it
             * allows one to provide balloon help on the individual
             * entries of the Hierbox widget.
             */
            savedItem = bindPtr->currentItem;
            savedContext = bindPtr->currentContext;
            if (bindPtr->newItem != NULL) {
                event.type = LeaveNotify;
                event.xcrossing.detail = NotifyVirtual /* Ancestor */;
                bindPtr->currentItem = bindPtr->newItem;
                DoEvent(bindPtr, &event, bindPtr->newItem, bindPtr->newContext);
            }
            bindPtr->newItem = newItem;
            bindPtr->newContext = newContext;
            if (newItem != NULL) {
                event.type = EnterNotify;
                event.xcrossing.detail = NotifyVirtual /* Ancestor */;
                bindPtr->currentItem = newItem;
                DoEvent(bindPtr, &event, newItem, newContext);
            }
            bindPtr->currentItem = savedItem;
            bindPtr->currentContext = savedContext;
        }
        return;
    }
    /*
     * Special note:  it's possible that
     *   bindPtr->newItem == bindPtr->currentItem
     * here.  This can happen, for example, if LEFT_GRABBED_ITEM was set.
     */

    bindPtr->flags &= ~LEFT_GRABBED_ITEM;
    bindPtr->currentItem = bindPtr->newItem = newItem;
    bindPtr->currentContext = bindPtr->newContext = newContext;
    if (bindPtr->currentItem != NULL) {
        XEvent event;

        event = bindPtr->pickEvent;
        event.type = EnterNotify;
        event.xcrossing.detail = NotifyAncestor;
        DoEvent(bindPtr, &event, newItem, newContext);
    }
}

/**
 * @ingroup binding_callbacks
 * @brief Handle Tk key, button, crossing and motion events for object bindings.
 * @param[in] clientData Binding table registered as the Tk event-handler data.
 * @param[in,out] eventPtr Borrowed event; release-state edits are restored.
 *
 * Preserves the owner at bindPtr->clientData, not the binding table separately.
 * Press repicks before dispatch; release dispatches before repicking with the
 * post-release button state. Motion repicks then dispatches; key events use
 * current/focus state. Owner cleanup must defer table destruction until return.
 */
static void BindProc(ClientData clientData, XEvent *eventPtr) {
    Rbc_BindTableStruct *bindPtr = clientData;
    unsigned int mask;

    Tcl_Preserve(bindPtr->clientData);
    /*
     * This code below keeps track of the current modifier state in
     * bindPtr->state.  This information is used to defer repicks of
     * the current item while buttons are down.
     */
    switch (eventPtr->type) {
    case ButtonPress:
    case ButtonRelease:
        mask = Tk_GetButtonMask(eventPtr->xbutton.button);
        /*
         * For button press events, repick the current item using the
         * button state before the event, then process the event.  For
         * button release events, first process the event, then repick
         * the current item using the button state *after* the event
         * (the button has logically gone up before we change the
         * current item).
         */
        if (eventPtr->type == ButtonPress) {
            /*
             * On a button press, first repick the current item using
             * the button state before the event, the process the event.
             */
            bindPtr->state = eventPtr->xbutton.state;
            PickCurrentItem(bindPtr, eventPtr);
            bindPtr->state ^= mask;
            DoEvent(bindPtr, eventPtr, bindPtr->currentItem, bindPtr->currentContext);
        } else {
            /*
             * Button release: first process the event, with the button
             * still considered to be down.  Then repick the current
             * item under the assumption that the button is no longer down.
             */
            bindPtr->state = eventPtr->xbutton.state;
            DoEvent(bindPtr, eventPtr, bindPtr->currentItem, bindPtr->currentContext);
            eventPtr->xbutton.state ^= mask;
            bindPtr->state = eventPtr->xbutton.state;
            PickCurrentItem(bindPtr, eventPtr);
            eventPtr->xbutton.state ^= mask;
        }
        break;
    case EnterNotify:
    case LeaveNotify:
        bindPtr->state = eventPtr->xcrossing.state;
        PickCurrentItem(bindPtr, eventPtr);
        break;
    case MotionNotify:
        bindPtr->state = eventPtr->xmotion.state;
        PickCurrentItem(bindPtr, eventPtr);
        DoEvent(bindPtr, eventPtr, bindPtr->currentItem, bindPtr->currentContext);
        break;
    case KeyPress:
    case KeyRelease:
        bindPtr->state = eventPtr->xkey.state;
        DoEvent(bindPtr, eventPtr, bindPtr->currentItem, bindPtr->currentContext);
        break;
    }
    Tcl_Release(bindPtr->clientData);
}

int Rbc_ConfigureBindingsFromObj(Tcl_Interp *interp, Rbc_BindTableStruct *bindPtr, ClientData item, Tcl_Size objc,
                                 Tcl_Obj *const objv[]) {
    const char *command;
    const char *seq;
    const char *string;
    unsigned long mask;

    if (objc == 0) {
        Tk_GetAllBindings(interp, bindPtr->bindingTable, item);
        return TCL_OK;
    }
    string = Tcl_GetString(objv[0]);
    if (objc == 1) {
        command = Tk_GetBinding(interp, bindPtr->bindingTable, item, string);
        if (command == NULL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("invalid binding event \"%s\"", string));
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interp, Tcl_NewStringObj(command, -1));
        return TCL_OK;
    }
    seq = string;
    command = Tcl_GetString(objv[1]);
    if (command[0] == '\0') {
        return Tk_DeleteBinding(interp, bindPtr->bindingTable, item, seq);
    }
    if (command[0] == '+') {
        mask = Tk_CreateBinding(interp, bindPtr->bindingTable, item, seq, command + 1, TRUE);
    } else {
        mask = Tk_CreateBinding(interp, bindPtr->bindingTable, item, seq, command, FALSE);
    }
    if (mask == 0) {
        return TCL_ERROR;
    }
    if (mask & (unsigned)~ALL_VALID_EVENTS_MASK) {
        Tk_DeleteBinding(interp, bindPtr->bindingTable, item, seq);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("requested illegal events; only key, button, "
                                                  "motion, enter, leave, and virtual events may "
                                                  "be used",
                                                  -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

Rbc_BindTable Rbc_CreateBindingTable(Tcl_Interp *interp, Tk_Window tkwin, ClientData clientData,
                                     Rbc_BindPickProc *pickProc, Rbc_BindTagProc *tagProc) {
    unsigned int mask;
    Rbc_BindTableStruct *bindPtr;

    bindPtr = RbcCalloc(1, sizeof(Rbc_BindTableStruct));
    assert(bindPtr);
    bindPtr->clientData = clientData;
    bindPtr->pickProc = pickProc;
    bindPtr->tagProc = tagProc;
    bindPtr->tkwin = tkwin;
    bindPtr->bindingTable = Tk_CreateBindingTable(interp);
    mask = (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask |
            PointerMotionMask);
    Tk_CreateEventHandler(tkwin, mask, BindProc, bindPtr);
    return bindPtr;
}

void Rbc_DestroyBindingTable(Rbc_BindTableStruct *bindPtr) {
    unsigned int mask;

    Tk_DeleteBindingTable(bindPtr->bindingTable);
    mask = (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask |
            PointerMotionMask);
    Tk_DeleteEventHandler(bindPtr->tkwin, mask, BindProc, bindPtr);
    ckfree((char *)bindPtr);
}

void Rbc_PickCurrentItem(Rbc_BindTableStruct *bindPtr) {
    if (bindPtr->activePick) {
        PickCurrentItem(bindPtr, &(bindPtr->pickEvent));
    }
}

void Rbc_DeleteBindings(Rbc_BindTableStruct *bindPtr, ClientData object) {
    Tk_DeleteAllBindings(bindPtr->bindingTable, object);
    /*
     * Invalidate saved targets now; the owner or a later pointer event
     * performs any required repick after component removal is complete.
     */
    if (bindPtr->currentItem == object) {
        bindPtr->currentItem = NULL;
        bindPtr->currentContext = NULL;
    }
    if (bindPtr->newItem == object) {
        bindPtr->newItem = NULL;
        bindPtr->newContext = NULL;
    }
    if (bindPtr->focusItem == object) {
        bindPtr->focusItem = NULL;
        bindPtr->focusContext = NULL;
    }
}

void Rbc_MoveBindingTable(Rbc_BindTableStruct *bindPtr, Tk_Window tkwin) {
    unsigned int mask;

    mask = (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask |
            PointerMotionMask);
    if (bindPtr->tkwin != NULL) {
        Tk_DeleteEventHandler(bindPtr->tkwin, mask, BindProc, bindPtr);
    }
    Tk_CreateEventHandler(tkwin, mask, BindProc, bindPtr);
    bindPtr->tkwin = tkwin;
}
