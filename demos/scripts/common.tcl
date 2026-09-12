# ------------------------------------------------------------------------------
#  RBC Demos - file demos/scripts/common.tcl
# ------------------------------------------------------------------------------
#  This file is a script fragment that is sourced by
#  several demo files.
# ------------------------------------------------------------------------------

# The text widget is being used as a large read-only label with word wrap.
# We need to set its -background and -font appropriately.
package require rbc
option add *Text.Background [. cget -bg]
option add *Text.Font TkDefaultFont

# This is true by default in X11, but not in Win32, where it is the same
# as TkDefaultFont!
font configure TkHeadingFont -weight bold

# Styling options
ttk::style configure TButton -highlightcolor grey -background grey80

# ------------------------------------------------------------------------------
#  Proc MakeLine
# ------------------------------------------------------------------------------
# Command to generate multi-line strings with long lines from readable
# indented code.
#
# Example:
#   set blurb [makeLine "
#            |This command allows multi-line strings with long lines to be
#            |created with readable code, and without breaking the rules for
#            |indentation.
#            |
#            |The command shifts the entire block of text to the left, omitting
#            |the pipe character and the spaces to its left. Then it replaces
#            |each newline with a space, with the exception that double
#            |newlines are preserved.
#   "]
# ------------------------------------------------------------------------------
proc MakeLine {in} {
    set halfway [regsub -all -line {^\s*\|} [string trim $in] {}]
    string map [list \n\n \n\n \n { }] $halfway
    # N.B. Implicit Return.
}

# ------------------------------------------------------------------------------
#  Proc CommonHeader
# ------------------------------------------------------------------------------
# Command to create a header window for an RBC demo.  The window is a text
# widget, and may have embedded buttons to request printing to a PostScript file
# or generation of a snapshot image.
#
# - The optional argument win is required if the buttons for
#   PostScript or image output are requested.
# - To request the Print button, also supply a non-empty value for psFile.
# - To request the snapshot button, also supply a non-empty value for imgFile.
#
# This command replaces an htext command in the BLT demos. RBC does not include
# BLT's htext.
#
# Arguments:
# w           - Tk window path requested for the new window
# txt         - text string to be shown in the window
# lines       - number of text display lines (allowing for word wrap and the
#               height of embedded buttons.
# DemoDir     - demo directory (used for loading button images)
#
# Return Value: Tk window path of the header window
# ------------------------------------------------------------------------------
proc CommonHeader {w txt lines DemoDir {win {}} args} {
    text $w -wrap word -width 1 -height 1 -relief flat -padx 15 -pady 5 -highlightthickness 0
    ### Main Message
    $w insert end $txt
    bind $w <Configure> {AdjustHeight %W 20}
    $w configure -state disabled
    return $w
}

# ------------------------------------------------------------------------------
#  Proc AdjustHeight
# ------------------------------------------------------------------------------
# Command to adjust the height of a text widget to fit its contents.
#
# Usage in this file: bound to <Configure> to ensure that the widget is large
# enough, even if the text or font changes.
#
# Arguments:
# w           - Tk window path of a text widget
# maxLines    - maximum permitted widget height (in text lines)
#
# Return Value: none
# ------------------------------------------------------------------------------
proc AdjustHeight {w maxLines} {
    # $w has no extra spacings per line.
    set lineSpace [font metrics [$w cget -font] -linespace]
    set yPixels [$w count -update -ypixels 1.0 end]
    set needLines [expr {($yPixels+$lineSpace-1)/$lineSpace}]
    if {$needLines <= $maxLines} {
        $w configure -height $needLines
    } else {
        # Ignore large demands during startup.
        # In 8.5 it's only the first call for each widget.
    }
    return
}

# ------------------------------------------------------------------------------
#  Proc CommonFooter
# ------------------------------------------------------------------------------
# Command to create a footer window for an RBC demo.  The window is a text
# widget, and has an embedded "Quit" button to close the demo.
#
# This command replaces an htext command in the BLT demos. RBC does not include
# BLT's htext.
#
# Arguments:
# w           - Tk window path requested for the new window
# DemoDir     - demo directory (used for loading button images)
# style       - (optional) TXT (the default) for a text button; anything else
#               for an image button.
#
# Return Value: Tk window path of the footer window
# ------------------------------------------------------------------------------
proc CommonFooter {w DemoDir {style TXT}} {
    set visual [winfo screenvisual .]
    text $w -wrap word -width 0 -height 3 -relief flat -padx 15 -pady 5 -highlightthickness 0
    ttk::button $w.quit -command exit
    $w insert end {Hit the }
    $w window create end -window $w.quit
    $w insert end { button when you've seen enough.}
    if {$style eq {TXT}} {
        $w.quit configure -text Quit
    } else {
        set im [image create photo -file $DemoDir/images/stopsign.gif]
        $w.quit configure -image $im
    }
    bind $w <Configure> {AdjustHeight %W 20}
    $w configure -state disabled
    return $w
}

# ------------------------------------------------------------------------------
#  Proc CommonPrint
# ------------------------------------------------------------------------------
# Command to print an RBC widget to a PostScript file.
#
# Adapted from BLT demos graph1.tcl, barchart*.tcl.
#
# Arguments:
# graph       - the window to be printed (an RBC widget)
# psFile      - the filename for output
#
# Return Value: none
# ------------------------------------------------------------------------------
proc CommonPrint {graph psFile} {
    # If the demo's PostScript dialog has been loaded, use it.
    if {[info commands ::rbc::ps::psDialog] ne {}} {
        ::rbc::ps::psDialog $graph $psFile
        return
    }
    # This is a simple dialog that lets the user directly set the
    # "configure" options of the graph's postscript component.
    if 0 {
        Rbc_PostScriptDialog $graph
        return
    }
    ### FIXME rbc - rbc::busy segfaults, so instead use a grab.
    ### This gives a "busy" warning and also prevents the user+GUI
    ### from changing the graph while it is printing.
    ###rbc::busy hold .
    set lab .temporaryLabelInRBCDemoCommonPrint
    destroy $lab
    label $lab -text "Printing ..." -bg yellow -fg red
    place $lab -relx 0.5 -rely 0.0 -anchor n
    grab  $lab
    ### Catch so the grab is always released.
    set catchValue [catch {
        update idletasks
        $graph postscript output $psFile
        update idletasks
    } catchRes catchDict]
    ###rbc::busy release .
    grab release $lab
    destroy $lab
    update idletasks
    if {$catchValue == 1} {
        return -code error -errorinfo [dict get $catchDict -errorinfo] $catchRes
    }
    return
}

# ------------------------------------------------------------------------------
#  Proc MakeSnapshot
# ------------------------------------------------------------------------------
# Command to print an RBC widget to a PostScript file.
#
# Adapted (with dependencies) from BLT demos graph4.tcl
#
# Arguments:
# graph       - the window to be snapshotted (an RBC widget)
# filename    - the filename for output
#
# Return Value: none
# ------------------------------------------------------------------------------
proc MakeSnapshot {graph filename} {
    update idletasks
    global unique
    # Set to 1 to create a second thumbnail that tests the "winop snap" command.
    set TestWinop 0
    set top .snapshot[incr unique]
    set im1 [image create photo]
    set im2 [image create photo]
    $graph snap $im1
    set thumb1 [image create photo -width 210 -height 150 -gamma 1.8]
    winop resample $im1 $thumb1 sinc 
    #Sharpen $thumb1
    if {$TestWinop} {
        rbc::winop snap $graph $im2 
        set thumb2 [image create photo -width 210 -height 150 -gamma 1.8]
        winop resample $im2 $thumb2 sinc 
    }
    toplevel $top
    wm title $top "Snapshot \#$unique of \"[$graph cget -title]\""
    wm protocol $top WM_DELETE_WINDOW [list DestroySnapshot $top $im1 $im2]
    ttk::label $top.l1 -image $thumb1
    ttk::frame $top.bb
    if {$TestWinop} {
        ttk::label $top.l2 -image $thumb2
    }
    # Uses PNG format. Too many colors for GIF.
    # Tk has no other image formats unless img::* or Img is loaded.
    ttk::button $top.bb.png0 -text {Save Full-Size as PNG File} -command [list $im1 write $filename -format png]
    ttk::button $top.bb.png1 -text {Save Thumbnail as PNG File} -command [list $thumb1 write $filename -format png]
    ttk::button $top.bb.but -text Dismiss -command [list DestroySnapshot $top $im1 $im2]
    if {$TestWinop} {
        grid $top.l1 $top.l2
    } else {
        grid $top.l1 -columnspan 2
    }
    grid $top.bb -sticky ew -columnspan 2
    grid $top.bb.png0 $top.bb.png1 $top.bb.but -pady 4 -padx 10
    focus $top.bb.but
    return
}

proc DestroySnapshot {win im1 im2} {
    image delete $im1
    image delete $im2
    set thumb1 [$win.l1 cget -image]
    image delete $thumb1
    if {[winfo exists $win.l2]} {
        set thumb2 [$win.l2 cget -image]
        image delete $thumb2
    }
    destroy $win
    return
}

proc UpdateTextWrap {label width} {
    $label configure -wraplength [expr {max(1, $width - 16)}]
}

proc ExpandableText {win wraplength title text} {
    ttk::frame $win
    ttk::button $win.toggle -text "▶ $title" -command [list ToggleExpandableText $win $title] -style Toolbutton
    ttk::label $win.body -text $text -wraplength $wraplength -justify left
    grid $win.toggle -sticky w
    grid $win.body -sticky ew -padx 8
    grid remove $win.body
    grid columnconfigure $win 0 -weight 1
    bind $win <Configure> [list UpdateTextWrap $win.body %w]
    return $win
}

proc ToggleExpandableText {win title} {
    if {[winfo manager $win.body] eq "grid"} {
        grid remove $win.body
        $win.toggle configure -text "▶ $title"
    } else {
        grid $win.body
        $win.toggle configure -text "▼ $title"
    }
}

proc DemoThumbnail {file width height} {
    # Creates a resized photo. The caller owns the returned image.
    set source [image create photo -file $file]
    set target {}
    try {
        set target [image create photo -width $width -height $height]
        rbc::winop image resample $source $target sinc
    } on error {message options} {
        if {$target ne {}} {
            image delete $target
        }
        return -options $options $message
    } finally {
        image delete $source
    }
    return $target
}

proc ReadCsv {path} {
    package require csv
    set ch [open $path r]
    try {
        fconfigure $ch -encoding utf-8
        set header {}
        set rows {}
        set record {}
        set line 0
        while {[gets $ch part] >= 0} {
            incr line
            append record $part
            if {![::csv::iscomplete $record]} {
                append record \n
                continue
            }
            if {$record eq {}} {
                continue
            }
            set values [::csv::split $record]
            set record {}
            if {$header eq {}} {
                set header [lreplace $values 0 0 [string trimleft [lindex $values 0] \ufeff]]
                if {[llength [lsort -unique $header]] != [llength $header]} {
                    return -code error "$path: duplicate CSV columns"
                }
                continue
            }
            if {[llength $values] != [llength $header]} {
                return -code error "$path:$line: wrong column count"
            }
            set row {}
            foreach k $header v $values {
                dict set row $k $v
            }
            lappend rows $row
        }
        if {$record ne {}} {
            return -code error "$path: incomplete CSV record"
        }
        if {$header eq {}} {
            return -code error "$path: empty CSV"
        }
        return $rows
    } finally {close $ch}
}
