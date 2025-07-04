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
 * How to make drag&drop work?
 *
 *  Right now we generate pseudo <Enter> <Leave> events within
 *  button grab on an object.  They're marked NotifyVirtual instead
 *  of NotifyAncestor.  A better solution: generate new-style
 *  virtual <<DragEnter>> <<DragMotion>> <<DragLeave>> events.
 *  These virtual events don't have to exist as "real" event
 *  sequences, like virtual events do now.
 */

/*
 *--------------------------------------------------------------
 *
 * DoEvent --
 *
 *      This procedure is called to invoke binding processing
 *      for a new event that is associated with the current item
 *      for a legend.
 *
 * Parameters:
 *      Rbc_BindTableStruct *bindPtr - Binding information for widget in which event occurred.
 *      XEvent *eventPtr - Real or simulated X event that is to be processed.
 *      ClientData item - Item picked.
 *      ClientData context - Context of item.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on the bindings for the legend.  A binding script
 *      could delete an entry, so callers should protect themselves
 *      with Tcl_Preserve and Tcl_Release.
 *
 *--------------------------------------------------------------
 */
static void DoEvent(Rbc_BindTableStruct *bindPtr, XEvent *eventPtr, ClientData item, ClientData context) {
    Rbc_List bindIds;
    int nIds;

    if ((bindPtr->tkwin == NULL) || (bindPtr->bindingTable == NULL)) {
        return;
    }
    /* Rbc doesn't set either focusItem or focusContext and both are NULL.
     * This results effectively in KeyPress/KeyRelease events to be ignored
     */
    if ((eventPtr->type == KeyPress) || (eventPtr->type == KeyRelease)) {
        item = bindPtr->focusItem;
        context = bindPtr->focusContext;
    }
    if (item == NULL) {
        return;
    }

    /*
     * Invoke the binding system.
     */
    bindIds = Rbc_ListCreate(TCL_ONE_WORD_KEYS);
    if (bindPtr->tagProc == NULL) {
        Rbc_ListAppend(bindIds, (char *)Tk_GetUid("all"), 0);
        Rbc_ListAppend(bindIds, (char *)item, 0);
    } else {
        (*bindPtr->tagProc)(bindPtr, item, context, bindIds);
    }
    nIds = Rbc_ListGetLength(bindIds);
    if (nIds > 0) {
        ClientData *idArray;
        ClientData tags[32];
        register Rbc_ListNode node;

        idArray = tags;
        if (nIds >= 32) {
            idArray = (ClientData *)ckalloc(sizeof(ClientData) * nIds);
        }
        nIds = 0;
        for (node = Rbc_ListFirstNode(bindIds); node != NULL; node = Rbc_ListNextNode(node)) {
            idArray[nIds++] = (ClientData)Rbc_ListGetKey(node);
        }
        Tk_BindEvent(bindPtr->bindingTable, eventPtr, bindPtr->tkwin, nIds, idArray);
        if (nIds >= 32) {
            ckfree((char *)idArray);
        }
    }
    Rbc_ListDestroy(bindIds);
}

/*
 *--------------------------------------------------------------
 *
 * PickCurrentItem --
 *
 *      Find the topmost item in a legend that contains a given
 *      location and mark the the current item.  If the current
 *      item has changed, generate a fake exit event on the old
 *      current item and a fake enter event on the new current
 *      item.
 *
 * Parameters:
 *      Rbc_BindTableStruct *bindPtr - Binding table information.
 *      XEvent *eventPtr - Event describing location of mouse cursor.  Must be EnterWindow, LeaveWindow, ButtonRelease,
 *          or MotionNotify
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The current item may change.  If it does, then the commands
 *      associated with item entry and exit could do just about
 *      anything.  A binding script could delete the legend, so
 *      callers should protect themselves with Tcl_Preserve and
 *      Tcl_Release.
 *
 *--------------------------------------------------------------
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

        bindPtr->flags |= REPICK_IN_PROGRESS;
        DoEvent(bindPtr, &event, bindPtr->currentItem, bindPtr->currentContext);
        bindPtr->flags &= ~REPICK_IN_PROGRESS;

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

/*
 *--------------------------------------------------------------
 *
 * BindProc --
 *
 *      This procedure is invoked by the Tk dispatcher to handle
 *      events associated with bindings on items.
 *
 * Parameters:
 *      ClientData clientData - Pointer to widget structure.
 *      XEvent *eventPtr - Pointer to X event that just happened.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Depends on the command invoked as part of the binding
 *      (if there was any).
 *
 *--------------------------------------------------------------
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
        PickCurrentItem(bindPtr, eventPtr);
        DoEvent(bindPtr, eventPtr, bindPtr->currentItem, bindPtr->currentContext);
        break;
    }
    Tcl_Release(bindPtr->clientData);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ConfigureBindings --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Rbc_BindTableStruct *bindPtr
 *      ClientData item
 *      int argc
 *      char **argv
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_ConfigureBindings(Tcl_Interp *interp, Rbc_BindTableStruct *bindPtr, ClientData item, int argc,
                          char **argv) {
    const char *command;
    unsigned long mask;
    char *seq;

    if (argc == 0) {
        Tk_GetAllBindings(interp, bindPtr->bindingTable, item);
        return TCL_OK;
    }
    if (argc == 1) {
        command = Tk_GetBinding(interp, bindPtr->bindingTable, item, argv[0]);
        if (command == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetResult(interp, command, TCL_VOLATILE);
        return TCL_OK;
    }

    seq = argv[0];
    command = argv[1];

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
        Tcl_ResetResult(interp);
        Tcl_AppendResult(interp, "requested illegal events; ", "only key, button, motion, enter, leave, and virtual ",
                         "events may be used", (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

#if (TCL_MAJOR_VERSION >= 8)

/*
 *--------------------------------------------------------------
 *
 * Rbc_ConfigureBindingsFromObj --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Rbc_BindTableStruct *bindPtr
 *      ClientData item
 *      int argc
 *      Tcl_Obj *const *objv
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_ConfigureBindingsFromObj(Tcl_Interp *interp, Rbc_BindTableStruct *bindPtr, ClientData item, int objc,
                                 Tcl_Obj *const *objv) {
    const char *command;
    unsigned long mask;
    char *seq;
    char *string;

    if (objc == 0) {
        Tk_GetAllBindings(interp, bindPtr->bindingTable, item);
        return TCL_OK;
    }
    string = Tcl_GetString(objv[0]);
    if (objc == 1) {
        command = Tk_GetBinding(interp, bindPtr->bindingTable, item, string);
        if (command == NULL) {
            Tcl_ResetResult(interp);
            Tcl_AppendResult(interp, "invalid binding event \"", string, "\"", (char *)NULL);
            return TCL_ERROR;
        }
        Tcl_SetResult(interp, command, TCL_VOLATILE);
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
        Tcl_ResetResult(interp);
        Tcl_AppendResult(interp, "requested illegal events; ", "only key, button, motion, enter, leave, and virtual ",
                         "events may be used", (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}
#endif

/*
 *--------------------------------------------------------------
 *
 * Rbc_CreateBindingTable --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      ClientData clientData
 *      Rbc_BindPickProc *pickProc
 *      Rbc_BindTagProc *tagProc
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
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

/*
 *--------------------------------------------------------------
 *
 * Rbc_DestroyBindingTable --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_BindTableStruct *bindPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_DestroyBindingTable(Rbc_BindTableStruct *bindPtr) {
    unsigned int mask;

    Tk_DeleteBindingTable(bindPtr->bindingTable);
    mask = (KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | EnterWindowMask | LeaveWindowMask |
            PointerMotionMask);
    Tk_DeleteEventHandler(bindPtr->tkwin, mask, BindProc, bindPtr);
    ckfree((char *)bindPtr);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_PickCurrentItem --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_BindTableStruct *bindPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_PickCurrentItem(Rbc_BindTableStruct *bindPtr) {
    if (bindPtr->activePick) {
        PickCurrentItem(bindPtr, &(bindPtr->pickEvent));
    }
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_DeleteBindings --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_BindTableStruct *bindPtr
 *      ClientData object
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_DeleteBindings(Rbc_BindTableStruct *bindPtr, ClientData object) {
    Tk_DeleteAllBindings(bindPtr->bindingTable, object);

    /*
     * If this is the object currently picked, we need to repick one.
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

/*
 *--------------------------------------------------------------
 *
 * Rbc_MoveBindingTable --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_BindTableStruct *bindPtr
 *      Tk_Window tkwin
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
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
