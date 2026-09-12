# ------------------------------------------------------------------------------
#  RBC Demo stripchart1.tcl revised from Michael J. McLennan's BLT demo
# ------------------------------------------------------------------------------
#  EXAMPLE: simple driver for stripchart widget
# ------------------------------------------------------------------------------
#  Michael J. McLennan
#  mmclennan@lucent.com
#  Bell Labs Innovations for Lucent Technologies
# ==============================================================================
#               Copyright (c) 1996  Lucent Technologies
# ==============================================================================

package require Tk
package require rbc
namespace import rbc::*


# --------------------------------------
# Commands
# --------------------------------------
# random
# source_create
# source_delete
# source_event
# source_color
# RaiseAboveDot
# --------------------------------------
# ------------------------------------------------------------------------------
#  USAGE:  random ?<max>? ?<min>?
#
#  Returns a random number in the range <min> to <max>.
#  If <min> is not specified, the default is 0; if max is not
#  specified, the default is 1.
# ------------------------------------------------------------------------------
proc source_severity {name value} {
    # Returns 0 for normal, 1 for warning, or 2 for error.
    global sources
    if {$value < $sources($name-absmin) || $value > $sources($name-absmax)} {
        return 2
    }
    if {$value < $sources($name-min) || $value > $sources($name-max)} {
        return 1
    }
    return 0
}
proc source_limits {name absmin min max absmax} {
    # Validates and stores the source thresholds.
    global sources
    foreach value [list $absmin $min $max $absmax] {
        if {![string is double -strict $value]} {
            error "limits must be finite numbers"
        }
        if {[catch {expr {abs($value) < Inf}} finite] || !$finite} {
            error "limits must be finite numbers"
        }
    }
    if {!($absmin < $min && $min < $max && $max < $absmax)} {
        error "limits must satisfy: critical low < low < high < critical high"
    }
    foreach key {absmin min max absmax} value [list $absmin $min $max $absmax] {
        set sources($name-$key) $value
    }
}
proc source_status {name value} {
    # Updates the current-value indicator.
    global sources
    set severity [source_severity $name $value]
    set color [lindex [list $sources($name-color) darkorange3 red3] $severity]
    set state [lindex {Normal Warning Error} $severity]
    set win $sources($name-controls)
    $win.status configure -text [format "%s: %.5g" $state $value] -foreground $color
}
proc source_apply_limits {name} {
    # Applies edited thresholds and reclassifies the retained samples.
    global sources
    set win $sources($name-controls)
    if {[catch {
        set limits [$win.lims get]
        if {[llength $limits] != 4} {
            error "enter four numbers: critical low, low, high, critical high"
        }
        source_limits $name {*}$limits
    } message]} {
        tk_messageBox -parent $win -icon error -type ok -title {Invalid limits} -message $message
        return
    }
    set yv $sources($name-y)
    set wv $sources($name-w)
    $yv variable values
    set weights {}
    for {set i 0} {$i < [$yv length]} {incr i} {
        lappend weights [source_severity $name $values($i)]
    }
    $wv set $weights
    set sources($name-steady) [expr {0.5 * ($sources($name-min) + $sources($name-max))}]
    if {[$yv length] > 0} {
        source_status $name $values(end)
    }
}
proc random {{max 1.0} {min 0.0}} {
    global randomSeed
    set randomSeed [expr {(7141*$randomSeed+54773)%259200}]
    set num  [expr {$randomSeed/259200.0*($max-$min)+$min}]
    return $num
}
set randomSeed 14823
set useAxes y

proc source_create {name color min max {absmin {}} {absmax {}}} {
    global sources
    if {[info exists sources($name-controls)]} {
        error "source \"$name\" already exists"
    }
    if {$max<=$min} {
        error "bad range: $min - $max"
    }
    # Default critical limits extend the normal range by 10%.
    set span [expr {$max - $min}]
    if {$absmin eq {}} {
        set absmin [expr {$min - 0.1 * $span}]
    }
    if {$absmax eq {}} {
        set absmax [expr {$max + 0.1 * $span}]
    }
    source_limits $name $absmin $min $max $absmax
    set sources($name-color) $color
    set unique 0
    set win ".sources.nb.s[incr unique]"
    while {[winfo exists $win]} {
        set win ".sources.nb.s[incr unique]"
    }
    set xvname xvector$unique
    set yvname yvector$unique
    set wvname wvector$unique
    global $xvname $yvname $wvname
    #    catch { $xvname delete }
    #    catch { $yvname delete }
    #    catch { $wvname delete }
    vector create $xvname $yvname $wvname
    if {$xvname eq {xvector1}} {
        $xvname append 0
    } else {
        xvector1 variable thisVec
        $xvname append $thisVec(end)
    }
    $yvname append [random $max $min]
    $wvname append 0
    catch {.sc graph element delete $name}
    .sc graph element create $name -x $xvname -y $yvname -color $color -symbol none -linewidth 2 -weights $wvname\
            -styles {{warning 0.5 1.5} {error 1.5 2.5}}
    if {$name ne {default}} {
        .sc graph axis create $name -title $name -limitscolor $color -limitsformat "%4.4g" -titlecolor $color
        .sc graph element configure $name -mapy $name
        global useAxes
        lappend useAxes $name
        # Arrange y axes
        set count 0
        if 1 {
            # Share y axes between left and right
            set yUse {}
            set y2Use {}
            foreach axis $useAxes {
                if {$count & 1} {
                    lappend yUse $axis
                    .sc graph axis configure $axis -rotate 90
                } else {
                    lappend y2Use $axis
                    .sc graph axis configure $axis -rotate -90
                }
                incr count
            }
            .sc graph y2axis use $y2Use
            .sc graph yaxis use $yUse
        } else {
            # All y axes on the left
            .sc graph yaxis use $useAxes
        }
    }
    set cwin .sources.choices.rb$unique
    radiobutton $cwin -text $name -variable choices -value $win -command "
        foreach w \[pack slaves .sources.nb\] {
            pack forget \$w
        }
        pack $win -fill both
    "
    pack $cwin -anchor w
    frame $win
    pack $win -fill x
    label $win.limsl -text "Critical low / Low / High / Critical high:"
    entry $win.lims -width 32
    $win.lims insert 0 [list $absmin $min $max $absmax]
    bind $win.lims <Return> [list source_apply_limits $name]
    button $win.apply -text Apply -command [list source_apply_limits $name]
    label $win.smoothl -text Smooth:
    frame $win.smooth
    radiobutton $win.smooth.linear -text Linear -variable smooth -value linear -command "
            .sc graph element configure $name -smooth linear
        "
    pack $win.smooth.linear -side left
    radiobutton $win.smooth.step -text Step -variable smooth -value step -command "
            .sc graph element configure $name -smooth step
        "
    pack $win.smooth.step -side left
    radiobutton $win.smooth.natural -text Natural -variable smooth -value natural -command "
            .sc graph element configure $name -smooth natural
        "
    pack $win.smooth.natural -side left
    label $win.ratel -text {Sampling Rate:}
    scale $win.rate -orient horizontal -from 10 -to 1000
    grid $win.smoothl $win.smooth
    grid $win.limsl $win.lims
    grid $win.ratel $win.rate
    grid $win.apply -row 1 -column 2 -padx 4
    label $win.status -anchor w
    grid $win.status -row 3 -column 0 -columnspan 3 -sticky ew -padx 4 -pady 4
    grid columnconfigure $win 1 -weight 1
    grid configure $win.smoothl $win.limsl $win.ratel -sticky e
    grid configure $win.smooth $win.lims -sticky ew -padx 4
    grid configure $win.rate -sticky ew -padx 2
    if {$unique != 1} {
        button $win.del -text Delete -command [list source_delete $name]
        grid $win.del -sticky e -padx 4 -pady 4 -column 1
    }
    $win.rate set 100
    catch {$win.smooth.[.sc graph element cget $name -smooth] invoke} mesg
    set sources($name-choice) $cwin
    set sources($name-controls) $win
    set sources($name-stream) [after 100 [list source_event $name 100]]
    set sources($name-x) $xvname
    set sources($name-y) $yvname
    set sources($name-w) $wvname
    set sources($name-max) $max
    set sources($name-min) $min
    set sources($name-steady) [random $max $min]
    $yvname variable initialValues
    source_status $name $initialValues(end)
    $cwin invoke
}
proc source_delete {name} {
    global sources
    after cancel $sources($name-stream)
    destroy $sources($name-choice)
    destroy $sources($name-controls)
    unset sources($name-controls)
    set first [lindex [pack slaves .sources.choices] 0]
    $first invoke
}
proc source_event {name delay} {
    # Appends a sample and its severity, then schedules the next sample.
    global sources
    if {![info exists sources($name-controls)]} {
        return
    }
    set win $sources($name-controls)
    if {![winfo exists $win]} {
        return
    }
    set xv $sources($name-x)
    set yv $sources($name-y)
    set wv $sources($name-w)
    $xv variable x
    $yv variable y
    set x(++end) [expr {$x(end) + 0.001 * $delay}]
    if {[random] > 0.97} {
        # Generate occasional excursions beyond the critical limits.
        set span [expr {$sources($name-absmax) - $sources($name-absmin)}]
        set low [expr {$sources($name-absmin) - 0.25 * $span}]
        set high [expr {$sources($name-absmax) + 0.25 * $span}]
        set value [random $high $low]
    } else {
        set value [expr {$y(end) + 0.1 * ($sources($name-steady) - $y(end))}]
    }
    set y(++end) $value
    $wv append [source_severity $name $value]
    if {[$xv length] > 100} {
        $xv delete 0
        $yv delete 0
        $wv delete 0
    }
    source_status $name $value
    set delay [$win.rate get]
    set sources($name-stream) [after $delay [list source_event $name $delay]]
}
proc source_color {args} {
    set r [expr {round(2.55*[.addSource.color.r get])}]
    set g [expr {round(2.55*[.addSource.color.g get])}]
    set b [expr {round(2.55*[.addSource.color.b get])}]
    set color [format "#%2.2x%2.2x%2.2x" $r $g $b]
    .addSource.color.sample configure -background $color
}
proc RaiseAboveDot {window} {
    if {[winfo viewable .]} {
        wm transient $window .
    }
    raise $window
    wm deiconify $window
    focus $window
    wm transient $window {}
    return
}


# --------------------------------------
### Create non-rbc GUI elements 1 - Menus
# --------------------------------------
menu .mbar
. configure -menu .mbar
.mbar add cascade -label Main -menu .mbar.main
.mbar add cascade -label Preferences -menu .mbar.prefs
menu .mbar.main -tearoff 0
.mbar.main add command -label {Add Source...} -command {
    set x [expr [winfo rootx .]+50]
    set y [expr [winfo rooty .]+50]
    wm geometry .addSource +$x+$y
    RaiseAboveDot .addSource
}
.mbar.main add separator
.mbar.main add command -label Quit -command exit
menu .mbar.prefs -tearoff 0
.mbar.prefs add cascade -label {Warning Symbol} -menu .mbar.prefs.wm
menu .mbar.prefs.wm -tearoff 0
.mbar.prefs add cascade -label {Error Symbol} -menu .mbar.prefs.em
menu .mbar.prefs.em -tearoff 0
set warningsym circle
set errorsym cross
foreach sym {none square circle diamond plus cross triangle} {
    .mbar.prefs.wm add radiobutton -label $sym -variable warningsym -value $sym\
            -command {.sc graph pen configure warning -symbol $warningsym}
    .mbar.prefs.em add radiobutton -label $sym -variable errorsym -value $sym\
            -command {.sc graph pen configure error -symbol $errorsym}
}

# --------------------------------------
### Create non-rbc GUI elements 2 - the "Add Source" toplevel window
# --------------------------------------
toplevel .addSource
wm title .addSource {Add Source}
wm group .addSource .
wm withdraw .addSource
wm protocol .addSource WM_DELETE_WINDOW {.addSource.controls.cancel invoke}
frame .addSource.info
pack .addSource.info -expand yes -fill both -padx 4 -pady 4
label .addSource.info.namel -text Name:
entry .addSource.info.name
label .addSource.info.maxl -text Maximum:
entry .addSource.info.max
label .addSource.info.minl -text Minimum:
entry .addSource.info.min
label .addSource.info.absminl -text {Critical low:}
entry .addSource.info.absmin
label .addSource.info.absmaxl -text {Critical high:}
entry .addSource.info.absmax
grid .addSource.info.namel .addSource.info.name
grid .addSource.info.maxl .addSource.info.max
grid .addSource.info.absminl .addSource.info.absmin
grid .addSource.info.absmaxl .addSource.info.absmax
grid .addSource.info.minl .addSource.info.min
grid configure .addSource.info.namel .addSource.info.maxl .addSource.info.minl -sticky e
grid configure .addSource.info.name .addSource.info.max .addSource.info.min -sticky ew
grid configure .addSource.info.absminl .addSource.info.absmaxl -sticky e
grid configure .addSource.info.absmin .addSource.info.absmax -sticky ew
frame .addSource.color
pack .addSource.color -padx 8 -pady 4
frame .addSource.color.sample -width 30 -height 30 -borderwidth 2 -relief raised
pack .addSource.color.sample -side top -fill both
scale .addSource.color.r -label Red -orient vertical -from 100 -to 0 -command source_color
pack .addSource.color.r -side left -fill y
scale .addSource.color.g -label Green -orient vertical -from 100 -to 0 -command source_color
pack .addSource.color.g -side left -fill y
scale .addSource.color.b -label Blue -orient vertical -from 100 -to 0 -command source_color
pack .addSource.color.b -side left -fill y
source_color
frame .addSource.sep -borderwidth 1 -height 2 -relief sunken
pack .addSource.sep -fill x -pady 4
frame .addSource.controls
pack .addSource.controls -fill x -padx 4 -pady 4
button .addSource.controls.ok -text Ok -command {
    if {[catch {
        source_create [.addSource.info.name get] [.addSource.color.sample cget -background] [.addSource.info.min get]\
                [.addSource.info.max get] [.addSource.info.absmin get] [.addSource.info.absmax get]
    } message]} {
        tk_messageBox -parent .addSource -icon error -type ok -title {Cannot add source} -message $message
    } else {
        wm withdraw .addSource
    }
}
pack .addSource.controls.ok -side left -expand yes -padx 4
button .addSource.controls.cancel -text Cancel -command {
    wm withdraw .addSource
}
pack .addSource.controls.cancel -side left -expand yes -padx 4
set showSourceControls 0
.mbar.prefs add separator
.mbar.prefs add checkbutton -label {Source controls} -variable showSourceControls -command {
    if {$showSourceControls} {
        pack .sources -side left -fill y -before .sc
    } else {
        pack forget .sources
    }
}

# --------------------------------------
### Create non-rbc GUI elements 3 - the "Sources" frame.  
# --------------------------------------
frame .sources
frame .sources.nb -borderwidth 2 -relief sunken
label .sources.title -text Sources:
frame .sources.choices -borderwidth 2 -relief groove
pack .sources.title -anchor w -padx 4 -pady 4
pack .sources.choices -fill x -padx 4
pack .sources.nb -fill both -expand yes -padx 4 -pady 4

# --------------------------------------
### The Stripchart
# --------------------------------------

####  Set option defaults for the stripchart.
option add *x.range 20.0
option add *x.shiftBy 15.0
option add *bufferElements no
option add *bufferGraph yes
option add *symbol triangle
option add *Axis.lineWidth 1
option add *Axis*Rotate 90
option add *pixels 1.25m
option add *Stripchart.width 6i

### Create and configure the stripchart; add the sources.
set stripchart [graphtoolbar .sc -width 800 -height 500 -type stripchart -controlmode context -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode current -scaletoggle y -activelegend -zoomwheel -pan]
$stripchart graph configure -title Stripchart -width 600 -height 400
$stripchart graph xaxis configure -title {Time (s)} -autorange 2.0 -shiftby 0.5
$stripchart graph yaxis configure -title Samples
$stripchart graph legend configure -font TkTooltipFont -position topmargin -rows 1

.sc graph pen create warning -color darkorange3 -linewidth 2 -symbol $warningsym -pixels 7 -fill darkorange3\
        -outline darkorange3
.sc graph pen create error -color red3 -linewidth 2 -symbol $errorsym -pixels 8 -fill red3 -outline red3

# The default font is too small on win32.
source_create default red 0 10
source_create temp blue3 0 10
source_create pressure green3 0 200
source_create volume orange3 0 1020
source_create power yellow3 0 0.01999
source_create work magenta3 0 10

### Map everything
pack $stripchart -expand yes -fill both



