# Legacy graph interaction helpers, retained for backward compatibility.
# Loaded by package require rbc; no argparse or graphtoolbar dependency.
# Do not enable these bindings on a graph managed by rbc::graphtoolbar.
# See license.terms for redistribution terms.
namespace eval ::rbc {}

proc Rbc_ActiveLegend {graph} {
    rbc::LegacyAttach $graph
    $graph legend bind all <Enter> [list rbc::ActivateLegend $graph ]
    $graph legend bind all <Leave> [list rbc::DeactivateLegend $graph]
    $graph legend bind all <ButtonPress-1> [list rbc::HighlightLegend $graph]
}

proc Rbc_Crosshairs {graph} {
    rbc::Crosshairs $graph
}

proc Rbc_ResetCrosshairs {graph state} {
    rbc::Crosshairs $graph "Any-Motion" $state
}

proc Rbc_ZoomStack {graph} {
    rbc::ZoomStack $graph
}

# Disable legacy zoom and restore the limits saved before the first zoom.
proc Rbc_RemoveZoomStack {graph} {
    rbc::RemoveZoomStack $graph
}

proc Rbc_PrintKey {graph} {
    rbc::PrintKey $graph
}

proc Rbc_ClosestPoint {graph} {
    rbc::ClosestPoint $graph
}

proc Rbc_PostScriptDialog {graph} {
    rbc::PostScriptDialog $graph
}

#
# The following procedures that reside in the "rbc" namespace are
# supposed to be private.
#

proc rbc::ActivateLegend {graph} {
    set elem [$graph legend get current]
    if {$elem ne {}} {
        $graph legend activate $elem
    }
}
proc rbc::DeactivateLegend {graph} {
    set elem [$graph legend get current]
    if {$elem ne {}} {
        $graph legend deactivate $elem
    }
}

proc rbc::HighlightLegend {graph} {
    set elem [$graph legend get current]
    if {$elem eq {}} {
        return
    }
    set relief [$graph element cget $elem -labelrelief]
    if {$relief eq "flat"} {
        $graph element configure $elem -labelrelief raised
        $graph element activate $elem
    } else {
        $graph element configure $elem -labelrelief flat
        $graph element deactivate $elem
    }
}

proc rbc::Crosshairs {graph {event "Any-Motion"} {state "on"}} {
    $graph crosshairs $state
    bind crosshairs-$graph <$event> {
        %W crosshairs configure -position @%x,%y
    }
    bind crosshairs-$graph <Leave> {
        %W crosshairs off
    }
    bind crosshairs-$graph <Enter> {
        %W crosshairs on
    }
    $graph crosshairs configure -color red
    if {$state eq "on"} {
        rbc::AddBindTag $graph crosshairs-$graph
    } elseif {$state eq "off"} {
        rbc::RemoveBindTag $graph crosshairs-$graph
    }
}

proc rbc::InitStack {graph} {
    variable zoomInfo

    rbc::LegacyAttach $graph
    rbc::LegacyCancel $graph marching
    rbc::LegacyCancel $graph title
    rbc::LegacyClearZoom $graph
    rbc::RemoveBindTag $graph select-region-$graph
    set zoomInfo($graph,interval) 100
    set zoomInfo($graph,afterId) 0
    set zoomInfo($graph,A,x) {}
    set zoomInfo($graph,A,y) {}
    set zoomInfo($graph,B,x) {}
    set zoomInfo($graph,B,y) {}
    set zoomInfo($graph,stack) {}
    set zoomInfo($graph,corner) A
}

proc rbc::ZoomStack {graph {start "ButtonPress-1"} {reset "ButtonPress-3"}} {
    variable zoomInfo
    variable zoomMod

    # Re-enabling cancels a pending selection but retains completed zooms.
    set stack {}
    if {[info exists zoomInfo($graph,stack)]} {
        set stack $zoomInfo($graph,stack)
    }
    rbc::InitStack $graph
    set zoomInfo($graph,stack) $stack
    foreach event [bind zoom-$graph] {
        bind zoom-$graph $event {}
    }
    if {[info exists zoomMod]} {
        set modifier $zoomMod
    } else {
        set modifier ""
    }
    bind zoom-$graph <${modifier}${start}> {
        rbc::SetZoomPoint %W %x %y
    }
    bind zoom-$graph <${modifier}${reset}> {
        if {[%W inside %x %y]} {
            rbc::ResetZoom %W
        }
    }
    rbc::AddBindTag $graph zoom-$graph
}

# Remove only zoom-owned work. Keep other legacy features and their cleanup tag.
# The oldest stack entry contains the original explicit or automatic axis limits.
proc rbc::RemoveZoomStack {graph} {
    variable zoomInfo
    rbc::LegacyCancel $graph marching
    rbc::LegacyCancel $graph title
    if {[winfo exists $graph]} {
        if {[info exists zoomInfo($graph,stack)] && [llength $zoomInfo($graph,stack)] > 0} {
            foreach command [lindex $zoomInfo($graph,stack) end] { {*}$command }
        }
        rbc::LegacyClearZoom $graph
        rbc::RemoveBindTag $graph zoom-$graph
        rbc::RemoveBindTag $graph select-region-$graph
    }
    foreach prefix {zoom select-region} {
        set tag $prefix-$graph
        foreach event [bind $tag] {
            bind $tag $event {}
        }
    }
    rbc::LegacyForgetArray zoomInfo $graph,
}

proc rbc::PrintKey {graph {event "Shift-ButtonRelease-3"}} {
    bind print-$graph <$event> {
        Rbc_PostScriptDialog %W
    }
    rbc::AddBindTag $graph print-$graph
}

proc rbc::ClosestPoint {graph {event "Control-ButtonPress-2"}} {
    bind closest-point-$graph <$event> {
        rbc::FindElement %W %x %y
    }
    rbc::AddBindTag $graph closest-point-$graph
}

proc rbc::AddBindTag { widget tag } {
    rbc::LegacyAttach $widget
    set oldTagList [bindtags $widget]
    if {[lsearch -exact $oldTagList $tag] < 0} {
        bindtags $widget [linsert $oldTagList 0 $tag]
    }
}

proc rbc::RemoveBindTag {widget tag} {
    set oldTagList [bindtags $widget]
    set index [lsearch -exact $oldTagList $tag]
    if {$index >= 0} {
        bindtags $widget [lreplace $oldTagList $index $index]
    }
}

proc rbc::FindElement {graph x y} {
    if {![$graph element closest $x $y info -interpolate yes]} {
        bell
        return
    }
    # --------------------------------------------------------------
    # info(name)        - element Id
    # info(index)       - index of closest point
    # info(x) info(y)       - coordinates of closest point
    #                 or closest point on line segment.
    # info(dist)        - distance from sample coordinate
    # --------------------------------------------------------------
    rbc::LegacyAttach $graph
    rbc::LegacyClearClosest $graph
    set markerName "rbcClosest_$info(name)"
    catch {$graph marker delete $markerName}
    $graph marker create text -coords [list $info(x) $info(y)] \
            -name $markerName \
            -text "$info(name): $info(dist)\nindex $info(index)" \
            -font {Lucida 10} \
            -anchor center -justify left \
            -yoffset 0 -bg {}

    set coords [$graph invtransform $x $y]
    set nx [lindex $coords 0]
    set ny [lindex $coords 1]
    $graph marker create line -coords "$nx $ny $info(x) $info(y)" -name line.$markerName
    rbc::FlashPoint $graph $info(name) $info(index) 10
}

proc rbc::FlashPoint {graph name index count} {
    rbc::LegacyAttach $graph
    if {![$graph element exists $name]} {
        rbc::LegacyClearClosest $graph
        return
    }
    if {$count & 1} {
        $graph element deactivate $name
    } else {
        # Data may have been shortened since the closest-point lookup.
        if {[catch {$graph element activate $name $index}]} {
            rbc::LegacyClearClosest $graph
            return
        }
    }
    incr count -1
    if {$count > 0} {
        rbc::LegacySchedule $graph [list flash $name $index] 200 [list ::rbc::FlashPoint $graph $name $index $count]
    } else {
        rbc::LegacyClearClosest $graph
    }
}

proc rbc::GetCoords {graph x y index} {
    variable zoomInfo
    if {[$graph cget -invertxy]} {
        set zoomInfo($graph,$index,x) $y
        set zoomInfo($graph,$index,y) $x
    } else {
        set zoomInfo($graph,$index,x) $x
        set zoomInfo($graph,$index,y) $y
    }
}

proc rbc::MarkPoint {graph index} {
    variable zoomInfo

    set x [$graph xaxis invtransform $zoomInfo($graph,$index,x)]
    set y [$graph yaxis invtransform $zoomInfo($graph,$index,y)]
    set marker "zoomText_$index"
    set text [format "x=%.4g\ny=%.4g" $x $y]
    if {[$graph marker exists $marker]} {
        $graph marker configure $marker -coords [list $x $y] -text $text
    } else {
        $graph marker create text -coords [list $x $y] -name $marker -font {Lucida 10} -text $text -anchor center\
                -bg {} -justify left
    }
}

proc rbc::DestroyZoomTitle {graph} {
    variable zoomInfo
    if {$zoomInfo($graph,corner) eq "A"} {
        catch {$graph marker delete "zoomTitle"}
    }
}

proc rbc::PopZoom {graph} {
    variable zoomInfo

    set zoomStack $zoomInfo($graph,stack)
    if {[llength $zoomStack] > 0} {
        set cmd [lindex $zoomStack 0]
        set zoomInfo($graph,stack) [lrange $zoomStack 1 end]
        foreach command $cmd {
            {*}$command
        }
        rbc::ZoomTitleLast $graph
        rbc::LegacySchedule $graph title 2000 [list ::rbc::DestroyZoomTitle $graph]
    } else {
        catch {$graph marker delete "zoomTitle"}
    }
}

# Push the old axis limits on the stack and set the new ones

proc rbc::PushZoom {graph} {
    variable zoomInfo

    rbc::LegacyClearZoom $graph
    rbc::LegacyCancel $graph marching
    set x1 $zoomInfo($graph,A,x)
    set y1 $zoomInfo($graph,A,y)
    set x2 $zoomInfo($graph,B,x)
    set y2 $zoomInfo($graph,B,y)
    if {($x1 == $x2) || ($y1 == $y2)} {
        # No delta, revert to start
        return
    }
    set cmd {}
    foreach margin {xaxis yaxis x2axis y2axis} {
        foreach axis [$graph $margin use] {
            set min [$graph axis cget $axis -min]
            set max [$graph axis cget $axis -max]
            set c [list $graph axis configure $axis -min $min -max $max]
            lappend cmd $c
        }
    }
    set zoomInfo($graph,stack) [linsert $zoomInfo($graph,stack) 0 $cmd]
    foreach margin {xaxis x2axis} {
        foreach axis [$graph $margin use] {
            set min [$graph axis invtransform $axis $x1]
            set max [$graph axis invtransform $axis $x2]
            if { $min > $max } {
                $graph axis configure $axis -min $max -max $min
            } else {
                $graph axis configure $axis -min $min -max $max
            }
        }
    }
    foreach margin {yaxis y2axis} {
        foreach axis [$graph $margin use] {
            set min [$graph axis invtransform $axis $y1]
            set max [$graph axis invtransform $axis $y2]
            if {$min > $max} {
                $graph axis configure $axis -min $max -max $min
            } else {
                $graph axis configure $axis -min $min -max $max
            }
        }
    }
}

#
# This routine terminates either an existing zoom, or pops back to
# the previous zoom level (if no zoom is in progress).
#

proc rbc::ResetZoom {graph} {
    variable zoomInfo
    if {![info exists zoomInfo($graph,corner)]} {
        rbc::InitStack $graph
    }
    rbc::LegacyClearZoom $graph
    rbc::LegacyCancel $graph marching
    if {$zoomInfo($graph,corner) eq "A"} {
        # Reset the whole axis
        rbc::PopZoom $graph
    } else {
        set zoomInfo($graph,corner) A
        rbc::RemoveBindTag $graph select-region-$graph
    }
}


proc rbc::ZoomTitleNext {graph} {
    variable zoomInfo

    set level [expr {[llength $zoomInfo($graph,stack)] + 1}]
    if {[$graph cget -invertxy]} {
        set coords "-Inf -Inf"
    } else {
        set coords "-Inf Inf"
    }
    $graph marker create text -name "zoomTitle" -text "Zoom #$level" -coords $coords -bindtags "" -anchor nw\
            -font {Helvetica 18} -shadow yellow4 -foreground yellow1
}

proc rbc::ZoomTitleLast {graph} {
    variable zoomInfo
    set level [llength $zoomInfo($graph,stack)]
    if {$level > 0} {
        set coords [expr {[$graph cget -invertxy] ? {-Inf -Inf} : {-Inf Inf}}]
        $graph marker create text -name zoomTitle -anchor nw -coords $coords -text "Zoom #$level" -font {Helvetica 18}\
                -shadow yellow4 -foreground yellow1
    }
}

proc rbc::SetZoomPoint { graph x y } {
    variable zoomInfo
    variable zoomMod
    if {![info exists zoomInfo($graph,corner)]} {
        rbc::InitStack $graph
    }
    rbc::GetCoords $graph $x $y $zoomInfo($graph,corner)
    if {[info exists zoomMod]} {
        set modifier $zoomMod
    } else {
        set modifier "Any-"
    }
    bind select-region-$graph <${modifier}Motion> {
        rbc::GetCoords %W %x %y B
        #rbc::MarkPoint $graph B
        rbc::Box %W
    }
    if {$zoomInfo($graph,corner) eq "A"} {
        if {![$graph inside $x $y]} {
            return
        }
        # First corner selected, start watching motion events
        #rbc::MarkPoint $graph A
        rbc::ZoomTitleNext $graph
        rbc::AddBindTag $graph select-region-$graph
        set zoomInfo($graph,corner) B
    } else {
        # Delete the modal binding
        rbc::RemoveBindTag $graph select-region-$graph
        rbc::PushZoom $graph
        set zoomInfo($graph,corner) A
    }
}


proc rbc::MarchingAnts { graph offset } {
    variable zoomInfo
    incr offset
    if {[$graph marker exists zoomOutline]} {
        $graph marker configure zoomOutline -dashoffset $offset
        set interval $zoomInfo($graph,interval)
        set id [rbc::LegacySchedule $graph marching $interval [list ::rbc::MarchingAnts $graph $offset]]
        set zoomInfo($graph,afterId) $id
    }
}

proc rbc::Box {graph} {
    variable zoomInfo
    if {$zoomInfo($graph,A,x) > $zoomInfo($graph,B,x)} {
        set x1 [$graph xaxis invtransform $zoomInfo($graph,B,x)]
        set y1 [$graph yaxis invtransform $zoomInfo($graph,B,y)]
        set x2 [$graph xaxis invtransform $zoomInfo($graph,A,x)]
        set y2 [$graph yaxis invtransform $zoomInfo($graph,A,y)]
    } else {
        set x1 [$graph xaxis invtransform $zoomInfo($graph,A,x)]
        set y1 [$graph yaxis invtransform $zoomInfo($graph,A,y)]
        set x2 [$graph xaxis invtransform $zoomInfo($graph,B,x)]
        set y2 [$graph yaxis invtransform $zoomInfo($graph,B,y)]
    }
    set coords [list $x1 $y1 $x2 $y1 $x2 $y2 $x1 $y2 $x1 $y1]
    if {[$graph marker exists "zoomOutline"]} {
        $graph marker configure "zoomOutline" -coords $coords
    } else {
        set X [lindex [$graph xaxis use] 0]
        set Y [lindex [$graph yaxis use] 0]
        $graph marker create line -coords $coords -name "zoomOutline" -mapx $X -mapy $Y -dashes 4 -linewidth 2
        set interval $zoomInfo($graph,interval)
        set id [rbc::LegacySchedule $graph marching $interval [list ::rbc::MarchingAnts $graph 0]]
        set zoomInfo($graph,afterId) $id
    }
}

proc rbc::PostScriptDialog { graph } {
    variable psdata

    rbc::LegacyAttach $graph
    set top $graph.top
    if {[winfo exists $top]} {
        raise $top
        return
    }
    toplevel $top
    foreach var {center landscape maxpect preview decorations padx pady paperwidth paperheight width height colormode} {
        set psdata($graph.$var) [$graph postscript cget -$var]
    }
    set var [namespace current]::psdata
    set row 1
    set col 0
    label $top.title -text "PostScript Options"
    grid $top.title -columnspan 7
    foreach bool {center landscape maxpect preview decorations} {
        set w $top.$bool-label
        label $w -text "-$bool" -font {Courier 12}
        grid $w -row $row -column $col -sticky e -pady {2 0} -padx {0 4}
        set w $top.$bool-yes
        radiobutton $w -text "yes" -variable ${var}($graph.$bool) -value 1
        grid $w -row $row -column [expr {$col + 1}] -sticky w
        set w $top.$bool-no
        radiobutton $w -text "no" -variable ${var}($graph.$bool) -value 0
        grid $w -row $row -column [expr {$col + 2}] -sticky w
        incr row
    }
    label $top.modes -text "-colormode" -font {Courier 12}
    grid $top.modes -row $row -column 0 -sticky e -pady {2 0} -padx {0 4}
    set col 1
    foreach m {color greyscale} {
        set w $top.$m
        radiobutton $w -text $m -variable ${var}($graph.colormode) -value $m
        grid $w -row $row -column $col -sticky w
        incr col
    }
    set row 1
    frame $top.sep -width 2 -bd 1 -relief sunken
    grid $top.sep -row $row -column 3 -sticky ns -rowspan 6
    set col 4
    foreach value {padx pady paperwidth paperheight width height} {
        set w $top.$value-label
        label $w -text "-$value" -font {Courier 12}
        grid $w -row $row -column $col -sticky e -pady {2 0} -padx {0 4}
        set w $top.$value-entry
        global $graph.$value
        entry $w -textvariable ${var}($graph.$value) -width 8
        grid $w -row $row -column [expr {$col + 1}] -columnspan 2 -sticky w -padx 8
        incr row
    }
    #    table configure $top c3 -width .125i
    grid columnconfigure $top 3 -minsize .125i
    button $top.cancel -text "Cancel" -command [list destroy $top]
    #    table $top $row,0 $top.cancel  -width 1i -pady 2 -cspan 3
    grid $top.cancel -row $row -column 0 -pady 2 -columnspan 3
    button $top.reset -text "Reset" -command [list ::rbc::LegacyResetDialog $graph]
    grid $top.reset -row $row -column 3 -pady 2
    button $top.print -text "Print" -command [list ::rbc::ResetPostScript $graph]
    #    table $top $row,4 $top.print  -width 1i -pady 2 -cspan 2
    grid $top.print -row $row -column 4 -pady 2 -columnspan 2
    bind $top <Destroy> [list ::rbc::LegacyDialogDestroyed $graph $top %W]
}

proc rbc::ResetPostScript { graph } {
    variable psdata
    foreach var {center landscape maxpect preview decorations padx pady paperwidth paperheight width height colormode} {
        set old [$graph postscript cget -$var]
        if {[catch {$graph postscript configure -$var $psdata($graph.$var)}]} {
            $graph postscript configure -$var $old
            set psdata($graph.$var) $old
        }
    }
    $graph postscript output "out.ps"
    catch {
        puts stdout "wrote file \"out.ps\"."
        flush stdout
    }
}

# Each legacy graph has one cleanup bindtag. Timers are keyed by widget and job,
# so repeated setup replaces jobs and destroying/reusing a pathname cannot run old work.
proc rbc::LegacyAttach {graph} {
    if {![winfo exists $graph]} {
        error "bad window path name \"$graph\""
    }
    set tag legacy-rbc-$graph
    set tags [bindtags $graph]
    if {[lsearch -exact $tags $tag] < 0} {
        bindtags $graph [linsert $tags 0 $tag]
        bind $tag <Destroy> [list ::rbc::LegacyDestroy $graph %W]
    }
}

proc rbc::LegacyCancel {graph job} {
    variable legacyTimers
    set key [list $graph $job]
    if {[info exists legacyTimers($key)]} {
        after cancel $legacyTimers($key)
        unset legacyTimers($key)
    }
}

proc rbc::LegacySchedule {graph job delay script} {
    variable legacyTimers
    rbc::LegacyAttach $graph
    rbc::LegacyCancel $graph $job
    set key [list $graph $job]
    set legacyTimers($key) [after $delay [list ::rbc::LegacyRun $graph $job $script]]
}

proc rbc::LegacyRun {graph job script} {
    variable legacyTimers
    unset -nocomplain legacyTimers([list $graph $job])
    if {[winfo exists $graph]} {
        {*}$script
    }
}

proc rbc::LegacyClearZoom {graph} {
    foreach marker {zoomTitle zoomOutline zoomText_A zoomText_B} {
        if {[$graph marker exists $marker]} {
            $graph marker delete $marker
        }
    }
}

proc rbc::LegacyClearClosest {graph} {
    variable legacyTimers
    foreach key [array names legacyTimers] {
        if {[lindex $key 0] eq $graph && [lindex $key 1 0] eq "flash"} {
            after cancel $legacyTimers($key)
            unset legacyTimers($key)
            set element [lindex $key 1 1]
            if {[$graph element exists $element]} {
                $graph element deactivate $element
            }
        }
    }
    set markers [concat [$graph marker names rbcClosest_*] [$graph marker names line.rbcClosest_*]]
    if {[llength $markers]} {
        $graph marker delete {*}$markers
    }
}

# Array indices are compared as strings rather than glob patterns: widget names
# may contain characters that are special to string match or array unset.
proc rbc::LegacyForgetArray {arrayName prefix} {
    upvar #0 ::rbc::$arrayName data
    foreach key [array names data] {
        if {[string first $prefix $key] == 0} {
            unset data($key)
        }
    }
}

proc rbc::LegacyDestroy {graph window} {
    if {$window ne $graph} { return }
    variable legacyTimers
    foreach key [array names legacyTimers] {
        if {[lindex $key 0] eq $graph} {
            after cancel $legacyTimers($key)
            unset legacyTimers($key)
        }
    }
    rbc::LegacyForgetArray zoomInfo $graph,
    rbc::LegacyForgetArray psdata $graph.
    foreach prefix {legacy-rbc crosshairs zoom print closest-point select-region} {
        set tag $prefix-$graph
        foreach event [bind $tag] {
            bind $tag $event {}
        }
    }
}

proc rbc::LegacyDialogDestroyed {graph top window} {
    if {$window eq $top} {
        rbc::LegacyForgetArray psdata $graph.
    }
}

proc rbc::LegacyResetDialog {graph} {
    variable psdata
    foreach option {center landscape maxpect preview decorations padx pady paperwidth paperheight width height\
                            colormode} {
        set psdata($graph.$option) [$graph postscript cget -$option]
    }
}
