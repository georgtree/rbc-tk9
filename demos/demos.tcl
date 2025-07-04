# ------------------------------------------------------------------------------
#  RBC Demo demos.tcl
#
#  This demo shows a snapshot and caption for each of the other demos.
# ------------------------------------------------------------------------------

package require Tk

proc EnableMouseWheelScrolling {canvas widget} {
    set bindScroll {
        if {[tk windowingsystem] eq "win32"} {
            bind %W <MouseWheel> [list %C yview scroll [expr {- (%D / 120)}] units]
        } elseif {[tk windowingsystem] eq "x11"} {
            bind %W <MouseWheel> "%C yview scroll \[expr {- (%D / 120)}\] units"
        } elseif {[tk windowingsystem] eq "aqua"} {
            bind %W <MouseWheel> [list %C yview scroll [expr {- (%D)}] units]
        }
    }
    # Bind to the frame and all its children
    foreach child [winfo children $widget] {
        set cmd [string map [list %W $child %C $canvas] $bindScroll]
        eval $cmd
        foreach childIn [winfo children $child] {
            set cmd [string map [list %W $childIn %C $canvas] $bindScroll]
            eval $cmd
        }
    }
    # Bind also to the frame itself
    set cmd [string map [list %W $widget %C $canvas] $bindScroll]
    eval $cmd
}

### The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]
set MyBinary [info nameofexecutable]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
set HeaderText [MakeLine {
    |There are several demo scripts for rbc.  Each demo can be launched from
    |this application by clicking the thumbnail image.
    |
    |Alternatively, the
    |demos can be run standalone from the command line or file manager.
}]
proc RunDemo {DemoDir fileTail} {
    set DemoPath [file join $DemoDir $fileTail]
    exec -ignorestderr -keepnewline -- $::MyBinary $DemoPath &
    return
}
proc MainWindow {win DemoDir} {
    # To test font resizing:
    # font configure TkDefaultFont -size -18
    # font configure TkHeadingFont -size -18
    set Caption(graph1.tcl) [MakeLine {
        |graph widget
        |
        |demo graph1.tcl
        |
        |Demonstrates multiple features of the graph widget: curves and points, fill images, scrolling, and legend.
    }]
    set Caption(graph2.tcl) [MakeLine {
        |graph widget
        |
        |demo graph2.tcl
        |
        |Sine and cosine functions showing different types of data points.
    }]
    set Caption(graph3.tcl) [MakeLine {
        |graph widget
        |
        |demo graph3.tcl
        |
        |Sine and cosine functions plotted as curves with different colored data points, and a bitmap image in the background.
    }]
    set Caption(graph4.tcl) [MakeLine {
        |graph widget
        |
        |demo graph4.tcl
        |
        |Graph that is similar to an oscilloscope
        |showing 39 different signals as colored curves with points.
        |Selectable traces, zoomable, legend identification.
    }]
    set Caption(graph5.tcl) [MakeLine {
        |graph widget
        |
        |demo graph5.tcl
        |
        |A simple plot of available symbol types, including user-specified bitmaps.
    }]
    set Caption(graph6.tcl) [MakeLine {
        |graph widget
        |
        |demo graph6.tcl
        |
        |Graph that is similar to an oscilloscope
        |showing 39 different signals as monochrome curves.
        |Selectable traces, zoomable.
    }]
    set Caption(graph7.tcl) [MakeLine {
        |graph widget
        |
        |demo graph7.tcl
        |
        |Zoomable scatter plot with 250,000 points.
    }]
    set Caption(barchart1.tcl) [MakeLine {
        |barchart widget
        |
        |demo barchart1.tcl
        |
        |Conventional barchart showing the available stipple patterns and error bars.  Zoomable.
    }]
    set Caption(barchart2.tcl) [MakeLine {
        |barchart widget
        |
        |demo barchart2.tcl
        |
        |Shows different ways of plotting multiple data sets with a common X axis: stacked, aligned, overlap and normal.
    }]
    set Caption(barchart3.tcl) [MakeLine {
        |barchart widget
        |
        |demo barchart3.tcl
        |
        |Conventional barchart showing colors and stipple patterns, with X as the value and Y as the independent variable.
    }]
    set Caption(barchart4.tcl) [MakeLine {
        |barchart widget
        |
        |demo barchart4.tcl
        |
        |A long series of values plotted with different colors and patterns.
    }]
    set Caption(barchart5.tcl) [MakeLine {
        |barchart widget
        |
        |demo barchart5.tcl
        |
        |Sine wave plotted as a barchart.
    }]
    set Caption(stripchart1.tcl) [MakeLine {
        |stripchart widget
        |
        |demo stripchart1.tcl
        |
        |The stripchart is useful for displaying one or more signals in real
        |time.  The demo includes multiple y axes, legend, and autoscaling.
    }]
    set Caption(winop1.tcl) [MakeLine {
        |winop command
        |
        |demo winop1.tcl
        |
        |Uses the winop image processing command to scale an image.
    }]
    set Caption(winop2.tcl) [MakeLine {
        |winop command
        |
        |demo winop2.tcl
        |
        |Uses the winop image processing command to rotate an image.
    }]
    set Caption(spline1.tcl) [MakeLine {
        |spline command
        |
        |demo spline1.tcl
        |
        |Uses built-in spline command to demonstrate approximating data points
    }]
    # create canvas with scrollbars
    ttk::frame $win
    canvas $win.c -width 500 -height 800 -xscrollcommand [list $win.xscroll set] -yscrollcommand [list $win.yscroll set]
    ttk::scrollbar $win.xscroll -orient horizontal -command [list $win.c xview]
    ttk::scrollbar $win.yscroll -command [list $win.c yview]
    pack $win.xscroll -side bottom -fill x
    pack $win.yscroll -side right -fill y
    pack $win.c -expand yes -fill both -side top
    ttk::frame $win.c.targetFrame
    set i 0
    foreach name {graph1 graph2 graph3 graph4 graph5 graph6 graph7 barchart1 barchart2 barchart3 barchart4 barchart5\
                          stripchart1 winop1 winop2 spline1} {
        set img $name.png
        set demo $name.tcl
        incr i
        ttk::labelframe $win.c.targetFrame.ins$i -text $name
        image create photo im$i -file [file normalize [file join $DemoDir thumbnails $img]]
        ttk::button $win.c.targetFrame.ins$i.pic$i -image im$i -command [list RunDemo $DemoDir $demo] 
        text $win.c.targetFrame.ins$i.caption$i -wrap word -width 20 -height 6 -relief flat -padx 15 -pady 5\
                -highlightthickness 0
        $win.c.targetFrame.ins$i.caption$i insert end [string map [list \n\n \n] $Caption($demo)]
        $win.c.targetFrame.ins$i.caption$i tag add bold 1.0 3.0
        $win.c.targetFrame.ins$i.caption$i tag configure bold -font TkHeadingFont
        $win.c.targetFrame.ins$i.caption$i configure -state disabled
        # This stops the text from exceeding its box if the system fonts are too large.
        bind $win.c.targetFrame.ins$i.caption$i <Configure> {AdjustFont %W 40}
        grid $win.c.targetFrame.ins$i -sticky nsew
        grid $win.c.targetFrame.ins$i.caption$i $win.c.targetFrame.ins$i.pic$i -sticky nsew
    }
    grid {*}[winfo children $win.c.targetFrame] -sticky nsew
    $win.c create window 0 0 -anchor nw -window $win.c.targetFrame
    bind $win.c.targetFrame <Configure> "
        set w $win.c.targetFrame
        $win.c configure -scrollregion \"0 0 \[winfo reqwidth \$w\] \[winfo reqheight \$w\]\"
    "
    EnableMouseWheelScrolling $win.c $win.c.targetFrame
    return $win
}

proc AdjustFont {w maxLines} {
    # $w has no extra spacings per line.
    set lineSpace1 [font metrics TkDefaultFont -linespace]
    set yPixels [$w count -update -ypixels 1.0 end]
    set needLines [expr {($yPixels+$lineSpace1-1)/$lineSpace1}]
    #puts "-ypixels $yPixels   $w"
    #puts "-ndlines $needLines $w"
    if {($needLines<=$maxLines) && ($yPixels>150)} {
        ReduceFont $w
    } else {
        # Ignore large demands during startup.
        # In 8.5 it's only the first call for each widget (and in this example, they do not occur at all).
    }
    return
}

proc ReduceFont {w} {
    # $w has no extra spacings per line.
    set size1 [font configure TkDefaultFont -size]
    set size2 [font configure TkHeadingFont -size]
    set lineSpace1 [font metrics TkDefaultFont -linespace]
    set lineSpace2 [font metrics TkHeadingFont -linespace]
    if {($lineSpace1!=$lineSpace2) || ($size1!=$size2)} {
        font configure TkHeadingFont {*}[font configure TkDefaultFont]
        font configure TkHeadingFont -weight bold
    }
    set lineSpace2a [font metrics TkHeadingFont -linespace]
    set yPixels [$w count -update -ypixels 1.0 end]
    set needLines [expr {($yPixels+$lineSpace1-1)/$lineSpace1}]
    if {$yPixels>150} {
        # Drop by one and try again.
        set newSize $size1
        if {$size1>0} {
            incr newSize -1
        } else {
            incr newSize
        }
        font configure TkDefaultFont -size $newSize
        font configure TkHeadingFont -size $newSize
    } else {
        # Ignore large demands during startup.
        # In 8.5 it's only the first call for each widget.
    }
    return
}

CommonHeader .header [string map [list \n\n \n] $HeaderText] 2 $DemoDir
.header configure -font TkHeadingFont
CommonFooter .footer $DemoDir
MainWindow .main $DemoDir

### Map everything, add Rbc_* commands and bindings.
grid .header -sticky ew
grid .main -sticky nsew
grid .footer -sticky ew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1

### For a quick exit.
menu .menu -tearoff 0
.menu add command -command {} -label Run -state disabled
.menu add command -command exit -label Quit
if {[tk windowingsystem] eq {aqua}} {
    bind . <ButtonPress-2> {tk_popup .menu %X %Y 0}
    bind . <Control-ButtonPress-1> {tk_popup .menu %X %Y 0}
} else {
    bind . <ButtonPress-3> {tk_popup .menu %X %Y 0}
}
