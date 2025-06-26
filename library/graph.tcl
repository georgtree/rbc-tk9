namespace eval ::rbc {}
set ::rbc::library [file normalize [file dirname [info script]]]
package require tooltip
namespace import ::tooltip::*
### Public procedures
proc Rbc_ActiveLegend {graph} {
    # Make legend active for provided graph/stripchart/barchart
    #   graph - name of the graph/stripchart/barchart widget
    $graph legend bind all <Enter> [list rbc::ActivateLegend $graph]
    $graph legend bind all <Leave> [list rbc::DeactivateLegend $graph]
    $graph legend bind all <ButtonPress-1> [list rbc::HighlightLegend $graph]
}
proc Rbc_Crosshairs {graph} {
    # Initialize and enable crosshairs for provided graph/stripchart/barchart
    #   graph - name of the graph/stripchart/barchart widget
    rbc::Crosshairs $graph
}
proc Rbc_CrosshairsMarker {graph marker} {
    # Initialize and enable crosshairs for provided graph/stripchart/barchart with current coordinate marker
    #   graph - name of the graph/stripchart/barchart widget
    #   marker - id of the text marker that displays coordinates
    rbc::CrosshairsMarker $graph $marker
}
proc Rbc_ResetCrosshairs {graph state} {
    # Set crosshairs state (on/off) for provided graph/stripchart/barchart
    #   graph - name of the graph/stripchart/barchart widget
    #   state - state of crosshairs, must be on or off
    rbc::Crosshairs $graph Any-Motion $state
}
proc Rbc_ZoomStack {graph {start ButtonPress-1} {reset ButtonPress-3}} {
    # Initializes zoom stack and enables interactive zoom for provided graph/stripchart/barchart
    #   graph - name of the graph/stripchart/barchart widget
    #   start - bind event type that start zooming box, and then end the selection, default is ButtonPress-1
    #   reset - bind event type that start return zoom state to the previous one, default is ButtonPress-3
    rbc::ZoomStack $graph $start $reset
}
proc Rbc_PrintKey {graph {event Shift-ButtonRelease-3}} {
    # Specifies event that opens PostScript dialog for provided graph/stripchart/barchart
    #   graph - name of the graph/stripchart/barchart widget
    #   start - bind event type that open PostScript dialog, default is Shift-ButtonRelease-3
    rbc::PrintKey $graph $event
}
proc Rbc_ClosestPoint {graph {event Control-ButtonPress-2}} {
    # Enables highlightning of closest point on the closest line element with specified event for provided 
    # graph/stripchart/barchart
    #   graph - name of the graph/stripchart/barchart widget
    #   event - bind event type that activate highlightning, default is Control-ButtonPress-2
    rbc::ClosestPoint $graph
}
proc Rbc_PostScriptDialog {graph} {
    # Opens PostScript dialog for provided graph/stripchart/barchart
    #   graph - name of the graph/stripchart/barchart widget
    rbc::PostScriptDialog $graph
}
proc Rbc_AxisScaleActive {graph axis {event ButtonPress-1}} {
    # Makes provided axis active - toggle between linear/log scale by event
    #   graph - name of the graph/stripchart/barchart widget
    #   axis - name of the axis
    #   axis - bind event type that toggle axis scale, default is ButtonPress-1
    rbc::AxisScaleActive $graph $axis $event
}
proc Rbc_ToolbarCrosshair {w graph {state on}} {
    # Enables crosshair with coordinates printed in the toolbar, and return toolbar path for adding it to the graph
    #   w - parent window that holds toolbar
    #   graph - name of the graph/stripchart/barchart widget
    #   state - state of crosshairs, must be on or off, default is on
    return [rbc::ToolbarCrosshair $w $graph Any-Motion $state]
}
proc Rbc_MarkClosestPoint {graph {event ButtonPress-2} {interpolate yes}} {
    # Enables marking of closest point on the closest line element with specified event for provided
    # graph/stripchart/barchart
    #   graph - name of the graph/stripchart/barchart widget
    #   event - bind event type that triggers mark adding, default is ButtonPress-2
    #   interpolate - select if closest intepolated point on graph is marked, or only the existing one, default is yes
    rbc::MarkClosestPoint $graph $event $interpolate
}

### Private procedures
####  General procedures
proc rbc::ClosestPoint {graph {event Control-ButtonPress-2}} {
    bind closest-point-$graph <$event> {
        rbc::FindElement %W %x %y
    }
    rbc::AddBindTag $graph closest-point-$graph
}
proc rbc::AddBindTag {widget tag} {
    set oldTagList [bindtags $widget]
    if {[lsearch $oldTagList $tag] < 0} {
        bindtags $widget [linsert $oldTagList 0 $tag]
    }
}
proc rbc::RemoveBindTag {widget tag} {
    set oldTagList [bindtags $widget]
    set index [lsearch $oldTagList $tag]
    if {$index >= 0} {
        bindtags $widget [lreplace $oldTagList $index $index]
    }
}
proc rbc::FindElement {graph x y} {
    if {![$graph element closest $x $y info -interpolate yes]} {
        return
    }
    # --------------------------------------------------------------
    # info(name)		- element Id
    # info(index)		- index of closest point
    # info(x) info(y)		- coordinates of closest point
    #				  or closest point on line segment.
    # info(dist)		- distance from sample coordinate
    # --------------------------------------------------------------
    set markerName "rbcClosest_$info(name)"
    catch {$graph marker delete $markerName}
    $graph marker create text -coords {$info(x) $info(y)} -name $markerName\
            -text "$info(name): $info(dist)\nindex $info(index)" -font {Lucida 10} -anchor center -justify left\
            -yoffset 0 -bg {}
    set coords [$graph invtransform $x $y]
    set nx [lindex $coords 0]
    set ny [lindex $coords 1]
    $graph marker create line -coords "$nx $ny $info(x) $info(y)" -name line.$markerName
    rbc::FlashPoint $graph $info(name) $info(index) 10
    rbc::FlashPoint $graph $info(name) [expr {$info(index)+1}] 10
}
proc rbc::FlashPoint {graph name index count} {
    if {$count & 1} {
        $graph element deactivate $name
    } else {
        $graph element activate $name $index
    }
    incr count -1
    if {$count > 0} {
        after 200 rbc::FlashPoint $graph $name $index $count
        update
    } else {
        $graph marker delete {*}[$graph marker names "rbcClosest_*"]
    }
}
proc rbc::FindElementMarkPoint {graph x y {interpolate yes}} {
    if {![$graph element closest $x $y info -interpolate $interpolate]} {
        return
    }
    rbc::AddLabelPoint $graph "$info(name): x=[format %.4g $info(x)] \ny=[format %.4g $info(y)]" "$info(x) $info(y)" n
    return
}

####  Legend procedures
proc rbc::ActivateLegend {graph} {
    set elem [$graph legend get current]
    $graph legend activate $elem
}
proc rbc::DeactivateLegend {graph} {
    set elem [$graph legend get current]
    $graph legend deactivate $elem
}
proc rbc::HighlightLegend {graph} {
    set elem [$graph legend get current]
    set relief [$graph element cget $elem -labelrelief]
    if {$relief eq {flat}} {
        $graph element configure $elem -labelrelief raised
        $graph element activate $elem
    } else {
        $graph element configure $elem -labelrelief flat
        $graph element deactivate $elem
    }
}

####  Markers procedures
proc rbc::MarkClosestPoint {graph {event Control-ButtonPress-2} {interpolate yes}} {
    bind closest-point-$graph <$event> "
        rbc::FindElementMarkPoint %W %x %y $interpolate
    "
    rbc::AddBindTag $graph closest-point-$graph
}
proc rbc::AddLabelPoint {graph text coords {anchor w}} {
    $graph marker create text -anchor $anchor -text $text -coords $coords
    $graph marker create bitmap -coords $coords\
            -bitmap "@[file dirname [dict get [info frame 0] file]]/circle.xbm" -under no
}
proc rbc::RemoveAllMarkers {graph} {
    set markersNames [$graph marker names]
    $graph marker delete {*}$markersNames
    return
}

####  Axes procedures
proc rbc::AxisScaleActive {graph axis {event ButtonPress-1}} {
    $graph axis bind $axis <Enter> {
        set axis [%W axis get current]
        set detail [%W axis get detail]
        if {$detail eq {line}} {
            %W axis configure $axis -background grey 
        }
    }
    $graph axis bind $axis <Leave> {
        set axis [%W axis get current]
        %W axis configure $axis -background {}
    }
    $graph axis bind $axis <$event> {
        set axis [%W axis get current]
        if {[lindex [%W axis configure $axis -logscale] end]} {
            %W axis configure $axis -logscale no
        } else {
            %W axis configure $axis -logscale yes
        }
    }
}
proc rbc::ToggleAxisScale {graph axis} {
    if {[lindex [$graph axis configure $axis -logscale] end]} {
        $graph axis configure $axis -logscale no
    } else {
        if {[catch {$graph axis configure $axis -logscale yes}]} {
            $graph axis configure $axis -logscale yes
        }
    }
    return
}
####  Crosshair procedures
proc rbc::Crosshairs {graph {event Any-Motion} {state on}} {
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
    if {$state eq {on}} {
        rbc::AddBindTag $graph crosshairs-$graph
    } elseif {$state eq {off}} {
        rbc::RemoveBindTag $graph crosshairs-$graph
    }
}
proc rbc::CrosshairsMarker {graph marker {event Any-Motion} {state on}} {
    $graph crosshairs $state
    bind crosshairs-$graph <$event> "
        %W crosshairs configure -position @%x,%y
        %W marker configure $marker -text \[format \"x=%%.4g\ny=%%.4g\" \{*\}\[%W invtransform %x %y\]\]
    "
    bind crosshairs-$graph <Leave> {
        %W crosshairs off
    }
    bind crosshairs-$graph <Enter> {
        %W crosshairs on
    }
    $graph crosshairs configure -color red
    if {$state eq {on}} {
        rbc::AddBindTag $graph crosshairs-$graph
    } elseif {$state eq {off}} {
        rbc::RemoveBindTag $graph crosshairs-$graph
    }
}

####  Zoom procedures
option add *zoomTitle.font {Helvetica 18}
option add *zoomTitle.shadow yellow4
option add *zoomTitle.foreground yellow1
option add *zoomTitle.coords {-Inf Inf}
proc rbc::InitStack {graph} {
    variable zoomInfo
    set zoomInfo($graph,interval) 100
    set zoomInfo($graph,afterId) 0
    set zoomInfo($graph,A,x) {}
    set zoomInfo($graph,A,y) {}
    set zoomInfo($graph,B,x) {}
    set zoomInfo($graph,B,y) {}
    set zoomInfo($graph,stack) {}
    set zoomInfo($graph,corner) A
}
proc rbc::ZoomStack {graph {start ButtonPress-1} {reset ButtonPress-3}} {
    variable zoomInfo
    variable zoomMod
    rbc::InitStack $graph
    if {[info exists zoomMod]} {
        set modifier $zoomMod
    } else {
        set modifier {}
    }
    bind zoom-$graph <${modifier}${start}> {rbc::SetZoomPoint %W %x %y}
    bind zoom-$graph <${modifier}${reset}> {
        if {[%W inside %x %y]} {
            rbc::ResetZoom %W
        }
    }
    rbc::AddBindTag $graph zoom-$graph
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
    set marker zoomText_$index
    set text [format "x=%.4g\ny=%.4g" $x $y]
    if [$graph marker exists $marker] {
        $graph marker configure $marker -coords {$x $y} -text $text
    } else {
        $graph marker create text -coords {$x $y} -name $marker -font {Lucida 10} -text $text -anchor center -bg {}\
                -justify left
    }
}
proc rbc::DestroyZoomTitle {graph} {
    variable zoomInfo
    if {$zoomInfo($graph,corner) eq {A}} {
        catch {$graph marker delete zoomTitle}
    }
}
proc rbc::PopZoom {graph} {
    variable zoomInfo
    set zoomStack $zoomInfo($graph,stack)
    if {[llength $zoomStack] > 0} {
        set cmd [lindex $zoomStack 0]
        set zoomInfo($graph,stack) [lrange $zoomStack 1 end]
        eval $cmd
        rbc::ZoomTitleLast $graph
        tk busy hold $graph
        event generate $graph <Configure>
        update idletasks
        tk busy forget $graph
        after 2000 "rbc::DestroyZoomTitle $graph"
    } else {
        catch {$graph marker delete zoomTitle}
    }
}
# Push the old axis limits on the stack and set the new ones
proc rbc::PushZoom {graph} {
    variable zoomInfo
    eval $graph marker delete [$graph marker names "zoom*"]
    if {[info exists zoomInfo($graph,afterId)]} {
        after cancel $zoomInfo($graph,afterId)
    }
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
            append cmd "$c\n"
        }
    }
    set zoomInfo($graph,stack) [linsert $zoomInfo($graph,stack) 0 $cmd]
    foreach margin {xaxis x2axis} {
        foreach axis [$graph $margin use] {
            set min [$graph axis invtransform $axis $x1]
            set max [$graph axis invtransform $axis $x2]
            if {$min > $max} {
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
    tk busy hold $graph
    event generate $graph <Configure>
    update idletasks
    tk busy forget $graph
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
    eval $graph marker delete [$graph marker names zoom*]
    if {$zoomInfo($graph,corner) eq {A}} {
        # Reset the whole axis
        rbc::PopZoom $graph
    } else {
        global zoomMod
        if {[info exists zoomMod]} {
            set modifier $zoomMod
        } else {
            set modifier Any-
        }
        set zoomInfo($graph,corner) A
        rbc::RemoveBindTag $graph select-region-$graph
    }
}
proc rbc::ZoomTitleNext {graph} {
    variable zoomInfo
    set level [expr {[llength $zoomInfo($graph,stack)] + 1}]
    if {[$graph cget -invertxy]} {
        set coords {-Inf -Inf}
    } else {
        set coords {-Inf Inf}
    }
    $graph marker create text -name zoomTitle -text "Zoom #$level" -coords $coords -bindtags {} -anchor nw
}
proc rbc::ZoomTitleLast {graph} {
    variable zoomInfo
    set level [llength $zoomInfo($graph,stack)]
    if {$level > 0} {
     	$graph marker create text -name zoomTitle -anchor nw -text "Zoom #$level"
    }
}
proc rbc::SetZoomPoint {graph x y} {
    variable zoomInfo
    variable zoomMod
    if {![info exists zoomInfo($graph,corner)]} {
        rbc::InitStack $graph
    }
    rbc::GetCoords $graph $x $y $zoomInfo($graph,corner)
    if {[info exists zoomMod]} {
        set modifier $zoomMod
    } else {
        set modifier Any-
    }
    bind select-region-$graph <${modifier}Motion> {
        rbc::GetCoords %W %x %y B
        #rbc::MarkPoint $graph B
        rbc::Box %W
    }
    if {$zoomInfo($graph,corner) eq {A}} {
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
option add *zoomOutline.dashes 4
option add *zoomTitle.anchor nw
option add *zoomOutline.lineWidth 2
option add *zoomOutline.xor yes
proc rbc::MarchingAnts {graph offset} {
    variable zoomInfo
    incr offset
    if {[$graph marker exists zoomOutline]} {
        $graph marker configure zoomOutline -dashoffset $offset
        set interval $zoomInfo($graph,interval)
        set id [after $interval [list rbc::MarchingAnts $graph $offset]]
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
    set coords {$x1 $y1 $x2 $y1 $x2 $y2 $x1 $y2 $x1 $y1}
    if {[$graph marker exists zoomOutline]} {
        $graph marker configure zoomOutline -coords $coords
    } else {
        set X [lindex [$graph xaxis use] 0]
        set Y [lindex [$graph yaxis use] 0]
        $graph marker create line -coords $coords -name zoomOutline -mapx $X -mapy $Y
        set interval $zoomInfo($graph,interval)
        set id [after $interval [list rbc::MarchingAnts $graph 0]]
        set zoomInfo($graph,afterId) $id
    }
}
proc rbc::ResetAllZoom {graph} {
    variable zoomInfo
    foreach state [set zoomInfo($graph,stack)] {
        rbc::PopZoom $graph
    }
    rbc::InitStack $graph
    eval $graph marker delete [$graph marker names zoom*]
}

####  Toolbars procedures
proc rbc::ToolbarCrosshair {w graph {event Any-Motion} {state on}} {
    set topFrame [ttk::frame $w.fra]
    set leftFrame [ttk::frame $topFrame.left]
    set rightFrame [ttk::frame $topFrame.right]
    set ::rbc::scaleStateX($graph) [lindex [$graph axis configure x -logscale] end]
    set ::rbc::scaleStateY($graph) [lindex [$graph axis configure y -logscale] end]
    ttk::checkbutton $leftFrame.b1 -text {Log Y} -variable ::rbc::scaleStateY($graph)\
            -command [list rbc::ToggleAxisScale $graph y]
    ttk::checkbutton $leftFrame.b2 -text {Log X} -variable ::rbc::scaleStateX($graph)\
            -command [list rbc::ToggleAxisScale $graph x]
    ttk::button $leftFrame.b3 -command [list rbc::ResetZoom $graph] -width 14 -image revertZoom
    ttk::button $leftFrame.b4 -command [list rbc::ResetAllZoom $graph] -width 14 -image resetZoom
    ttk::button $leftFrame.b5 -command [list rbc::RemoveAllMarkers $graph] -width 14 -image removeAllMarkers
    ttk::button $leftFrame.b6 -command [list rbc::MakeSnapshot $graph] -width 14 -image saveSnapshot
    ttk::button $leftFrame.b7 -command [list rbc::PostScriptDialog $graph] -width 14 -image postscriptPrint
    ttk::label $rightFrame.lx -text x=
    ttk::label $rightFrame.entryx -text 0.0 -width 10
    ttk::label $rightFrame.ly -text y=
    ttk::label $rightFrame.entryy -text 0.0 -width 10
    pack $leftFrame -side left -fill both -expand true
    pack $rightFrame -side right -fill y -anchor e
    pack $leftFrame.b1 $leftFrame.b2 $leftFrame.b3 $leftFrame.b4 $leftFrame.b5 $leftFrame.b6 $leftFrame.b7 -side left\
            -fill y  
    pack $rightFrame.entryy $rightFrame.ly $rightFrame.entryx $rightFrame.lx -side right -fill y
    $graph crosshairs $state
    bind crosshairs-$graph <$event> "
        %W crosshairs configure -position @%x,%y
        lassign \[%W invtransform %x %y\] x y
        $rightFrame.entryx configure -text \[format \"%%.4g\" \$x\]
        $rightFrame.entryy configure -text \[format \"%%.4g\" \$y\]
    "
    bind crosshairs-$graph <Leave> {
        %W crosshairs off
    }
    bind crosshairs-$graph <Enter> {
        %W crosshairs on
    }
    $graph crosshairs configure -color red
    if {$state eq {on}} {
        rbc::AddBindTag $graph crosshairs-$graph
    } elseif {$state eq {off}} {
        rbc::RemoveBindTag $graph crosshairs-$graph
    }
    return $topFrame
}

####  PostScript procedures
proc rbc::PrintKey {graph {event Shift-ButtonRelease-3}} {
    bind print-$graph <$event> {Rbc_PostScriptDialog %W}
    rbc::AddBindTag $graph print-$graph
}
proc rbc::PostScriptDialog {graph} {
    variable psdata
    set nsp [namespace path]
    namespace path ::tcl::mathop
    set top $graph.top
    toplevel $top
    wm title $top {Postscript dialog}
    foreach var {center landscape maxpect preview decorations padx
	    pady paperwidth paperheight width height colormode} {
        set psdata($graph.$var) [$graph postscript cget -$var]
    }
    set var [namespace current]::psdata
    set row 1
    set col 0
    ttk::label $top.title -text {PostScript Options}
    grid $top.title -columnspan 7
    foreach bool {center landscape maxpect preview decorations} {
        set w $top.$bool-label
        ttk::label $w -text -$bool -font {Courier 12}
        grid $w -row $row -column $col -sticky e -pady {2 0} -padx {0 4}
        set w $top.$bool-yes
        ttk::radiobutton $w -text yes -variable ${var}($graph.$bool) -value 1
        grid $w -row $row -column [+ $col 1] -sticky w
        set w $top.$bool-no
        ttk::radiobutton $w -text no -variable ${var}($graph.$bool) -value 0
        grid $w -row $row -column [+ $col 2] -sticky w
        incr row
    }
    ttk::label $top.modes -text -colormode -font {Courier 12}
    grid $top.modes -row $row -column 0 -sticky e -pady {2 0} -padx {0 4}
    set col 1
    foreach m {color greyscale} {
        set w $top.$m
        ttk::radiobutton $w -text $m -variable ${var}($graph.colormode) -value $m
        grid $w -row $row -column $col -sticky w
        incr col
    }
    set row 1
    ttk::frame $top.sep -width 2
    grid $top.sep -row $row -column 3 -sticky ns -rowspan 6
    set col 4
    foreach value {padx pady paperwidth paperheight width height} {
        set w $top.$value-label
        ttk::label $w -text -$value -font {Courier 12}
        grid $w -row $row -column $col -sticky e -pady {2 0} -padx {0 4}
        set w $top.$value-entry
        global $graph.$value
        ttk::entry $w -textvariable ${var}($graph.$value) -width 8
        grid $w -row $row -column [+ $col 1] -columnspan 2 -sticky w -padx 8
        incr row
    }
    grid columnconfigure $top 3 -minsize .125i
    ttk::button $top.cancel -text Cancel -command [list destroy $top]
    grid $top.cancel -row $row -column 0 -pady 2 -columnspan 3
    ttk::button $top.reset -text Reset -command [list destroy $top]
    ttk::button $top.print -text Print -command [list rbc::ResetPostScript $graph]
    grid $top.print -row $row -column 4 -pady 2 -columnspan 2
    namespace path $nsp
    bind $top.print <Destroy> [list array unset $var $graph*]
}
proc rbc::ResetPostScript {graph} {
    variable psdata
    foreach var {center landscape maxpect preview decorations padx
	    pady paperwidth paperheight width height colormode} {
        set old [$graph postscript cget -$var]
        if {[catch {$graph postscript configure -$var $psdata($graph.$var)}]} {
            $graph postscript configure -$var $old
            set psdata($graph.$var) $old
        }
    }
    $graph postscript output out.ps
    catch {
        puts stdout "wrote file \"out.ps\"."
        flush stdout
    }
}

####  Output procedures
proc rbc::MakeSnapshot {graph} {
    if {![catch {set savePath [tk_getSaveFile -initialfile snapshot.png]}] && ($savePath ne {})} {
        set ::baseImg [image create photo]
        $graph snap $::baseImg
        $::baseImg write [list $savePath] -format png
        unset ::baseImg
    } else {
        return
    }
}

####  Icons
image create photo removeAllMarkers -data {
    R0lGODlhEAAQAIIAAASC/PwCBMQCBEQCBIQCBAAAAAAAAAAAACH5BAEAAAAA
    LAAAAAAQABAAAAMuCLrc/hCGFyYLQjQsquLDQ2ScEEJjZkYfyQKlJa2j7AQn
    MM7NfucLze1FLD78CQAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJz
    aW9uIDIuNQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVz
    ZXJ2ZWQuDQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7
}
image create photo revertZoom -data {
    R0lGODlhEAAQAIUAAPwCBBxSHBxOHMTSzNzu3KzCtBRGHCSKFIzCjLzSxBQ2
    FAxGHDzCLCyeHBQ+FHSmfAwuFBxKLDSCNMzizISyjJzOnDSyLAw+FAQSDAQe
    DBxWJAwmDAQOBKzWrDymNAQaDAQODAwaDDyKTFSyXFTGTEy6TAQCBAQKDAwi
    FBQyHAwSFAwmHAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
    AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAALAAAAAAQABAAAAZ1
    QIBwSCwaj0hiQCBICpcDQsFgGAaIguhhi0gohIsrQEDYMhiNrRfgeAQC5fMC
    AolIDhD2hFI5WC4YRBkaBxsOE2l/RxsHHA4dHmkfRyAbIQ4iIyQlB5NFGCAA
    CiakpSZEJyinTgAcKSesACorgU4mJ6uxR35BACH+aENyZWF0ZWQgYnkgQk1Q
    VG9HSUYgUHJvIHZlcnNpb24gMi41DQqpIERldmVsQ29yIDE5OTcsMTk5OC4g
    QWxsIHJpZ2h0cyByZXNlcnZlZC4NCmh0dHA6Ly93d3cuZGV2ZWxjb3IuY29t
    ADs=
}
image create photo resetZoom -data {
    iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAAXNSR0IArs4c
    6QAAAXRJREFUOI2VkyFPw1AQx397ObGQF1Ix8eREQxATyIpmqZjYR+AjoAkf
    AMEHQCCQSCRyAtGQigoEgpCFTEwQUjFRgahodgjapS2jCefu3f1/d7m7Bx3z
    j3wNp6HaQ6vdWK+F01DFiD6/r/T180OTt1cVIxpOw16QqcXRbM46z3HOUciQ
    bOhx/5ERzebUkNLIL9gAQIzo+ivHWQ+AeJOxzHJeNjmnkzHzkaMA0rrbbTnY
    dWAPrS6WS5z1SKukaOQ4mxzjCcSrrCW+7HbgH/n6EMfkMqQQSEeOqAoGDWEt
    fmxUBxjYA6t3aQrWY10AAsXxeAfpE7dmsNhkLFYZRVkAQ67Dk131AJA9Yqi2
    EIQB8c0tc98x9lxLXJtsy/67qO+gNKKJEZ0ZUTGiV5VfVn4vBH52PWsky1Oy
    gyRPST+krt59b3bSC6mr74tJBbg4v2hBpJu4b1UA5bYcREY0eP4ZbxzHRFGk
    LcBfq2pC0iT93y/dZ9IY8jcCl7dntGRDqgAAAABJRU5ErkJggg==
}
image create photo saveSnapshot -data {
   R0lGODlhEAAQAIYAAPz+BFxaXNze3Nza3Nza1LS2tJyanPz+/PTu5GRqZOTi
   3HRydMzOzDQyNJQOFDxKfKTW5OTy/OT23MTaxISKhGxmZERCRCwuLJQSFPzG
   pLweHJTO3ESu3DRilNz61FymXCxKRMTCxPTy7Ly2rIyKhDwCBORiRKQWFEy2
   3CyKvJTWhDSOPCRONDQaHNROPNQ2JLRubHTC5DR6rHR+jFSqRCRyLGxqbOzm
   3LyulCwqLMySjMQ2JMyqrERijJSenFSGXGRmXMS6tPTq3Ozi1MS+vMxCNKyq
   rHR2dPTm1OzezOSKfNTe5KSmpIyGjLy2pOzWvLyqjIRqZIyKjHR2fExKTHx2
   bLyynOzexLymhERGRGRmZKSWhMS6pOTOpLyedMTCtMS6rMSulLymfAAAAAAA
   AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
   AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAA
   LAAAAAAQABAAAAfcgACCAAGFhoaDiQECAwICBAUBBgGJggEEB5kHCAkHk5UB
   CpqZCwyeDYIOAA8QERITmRQLFRYXABgZGhscHR4fICEiAiMkDSUmJicoKQ8q
   KywLAwgKIxctLi8wMTIzNDUJNiEINzgNOTo7PBA9Pj/QQEFCQzg5F0RFEQQ+
   RhRHswVIkpS7oEMJgiUhCjBpcqSCkyFPoNjKEYVJCCNSpgSgUsXKlSdYUAG4
   kCOLlgpAgFTZwuVJFyy2Bi0gEsSJzZYuvYi09OULGC44wmAR48VLDkU5zClt
   0OACUz+BAAAh/mhDcmVhdGVkIGJ5IEJNUFRvR0lGIFBybyB2ZXJzaW9uIDIu
   NQ0KqSBEZXZlbENvciAxOTk3LDE5OTguIEFsbCByaWdodHMgcmVzZXJ2ZWQu
   DQpodHRwOi8vd3d3LmRldmVsY29yLmNvbQA7
}
image create photo postscriptPrint -data {
   R0lGODlhEAAQAIYAAPwCBFxWVFRWVFxaXOTi3Nze3LS2tKSajPT29Pz+/Ozq
   7GxqbJyanISChPz6/HRydNTS1CwqLMTCxISGhOzu7ERCRDQyNERGRLy2rIyK
   hExOTAQCBKyqrOTm5KSmpHR2dNza3AQGBNzSxOzm3LyulBweHBQSFKSilOza
   xAwKDOzezDw6PJSSlDw+PKSelNzOvLymhAwODNzGrOTStLyqjExKRHRqZLym
   fMTCvMS+tMS6rLyynMS6pOTOpLyifMTCtMS+rMS2pMSynLyidLyedAAAAAAA
   AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
   AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
   AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA
   AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACH5BAEAAAAA
   LAAAAAAQABAAAAfPgACCAAECA4aIA4OLAgSOBQQGAgcBi4KNCAmaCgsJDAKW
   jQkNCA4JDxCeEYwSDQ4TFKMPCxUWAA0BEQ0GDggQDxcIBBgZthobExwICB0e
   FRsSHcOrER8QCwYIIA8bIRwiIyS2JSYVHgsQzhsbJygEJKsl6ykeH9zrJyIq
   4gAp6xsrWLT45+IFChi2YqwLwcLEvxgHZMygsarBOgsCBGioUcNGxBk3bA0S
   gCOHjh0oeczo4WPVoAA/fgAJIoQGjSFEiIgUNCCCBZ8Wggq14CcQACH+aENy
   ZWF0ZWQgYnkgQk1QVG9HSUYgUHJvIHZlcnNpb24gMi41DQqpIERldmVsQ29y
   IDE5OTcsMTk5OC4gQWxsIHJpZ2h0cyByZXNlcnZlZC4NCmh0dHA6Ly93d3cu
   ZGV2ZWxjb3IuY29tADs=
}
