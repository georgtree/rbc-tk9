# Winop

  - [Name](#name)
  - [Synopsis](#synopsis)
  - [Description](#description)
  - [Introduction](#introduction)
  - [Operations](#operations)

## Name

winop − Perform assorted window operations

## Synopsis

**winop lower** ?*window*? ...

**winop map** ?*window*? ...

**winop move** *window x y*

**winop raise** ?*window*? ...

**winop snap** *window photoName*

**winop unmap** ?*window*? ...

**winop warpto** ?*window*?

## Description

The **winop** command performs various window operations on Tk windows using low-level Xlib function calls to work
around window manager pecularities.

## Introduction

Tk has several commands for manipulating its windows: **raise**, **lower**, **wm**, etc. These commands ask the window
manager to perform operations on Tk windows. In some cases, a particular window manager won't perform the operation as
expected.

For example, if you positioned a toplevel window using **wm geometry**, the window may not actually be at those
particular coordinates. The position of the window may be offset by dimensions of the title bar added by the window
manager.

In situations like these, the **winop** command can be used to workaround these difficulties. Instead, it makes
low-level Xlib (such **XRaiseWindow** and **XMapWindow**) calls to perform these operations.

```tcl
toplevel .top
wm withdraw .top

# Set the geometry to make the window manager
# place the window.
wm geometry .top +100+100

# Move the window to the desired location
# and "update" to force the window manager
# to recognize it.
winop move .top 100 100
update

wm deiconify .top
winop move .top 100 100
```

## Operations

The following operations are available for the **winop** command:

- **winop lower** ?*window*? ...

  Lowers *window* to the bottom of the X window stack. *Window* is the path name of a Tk window.

- **winop map** ?*window*? ...

  Maps *window* on the screen. *Window* is the path name of a Tk window.  If *window* is already mapped, this command
  has no effect.

- **winop move** *window x y*

  Move *window* to the screen location specified by *x* and *y*. *Window* is the path name of a Tk window, while *x* and
  *y* are screen coordinates. This command returns the empty string.

- **winop raise** ?*window*? ...

  Raises *window* to the top of the X window stack. *Window* must be a valid path name of a Tk window. This command
  returns the empty string.

- **winop snap** *window photoName*

  Takes a snapshot of the *window* and stores the contents in the photo image *photoName*. *Window* is the valid path
  name of a Tk window which must be totally visible (unobscured). *PhotoName* is the name of a Tk photo image which must
  already exist. This command can fail if the window is obscured in any fashion, such as covered by another window or
  partially offscreen. In that case, an error message is returned.

- **winop unmap** ?*window*? ...

  Unmaps *window* from the screen. *Window* is the path name of a Tk window.

- **winop warpto** ?*window*?

  Warps the pointer to *window*. *Window* is the path name of a Tk window which must be mapped. If *window* is in the
  form *@x,y*, where *x* and *y* are root screen coordinates, the pointer is warped to that location on the screen.

  If no *window* argument is present the current location of the pointer is returned. The location is returned as a list
  in the form "*x y*", where *x* and *y* are the current coordinates of the pointer.

