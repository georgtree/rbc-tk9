# Shared X navigation, local Y scales, annotations and both legend types.
package require rbc::graphtoolbar

wm title . {RBC interactive stacked tracks}
rbc::graphtoolbar .gtb -type trackgraph -interactive -width 900 -height 620 \
    -crosshairsmode current -zoommark -zoomwheelmod {} -controlmode both
set graph [.gtb subwidget graph]
$graph configure -trackresize yes -title {Shared time, independent Y navigation}
$graph axis configure x -title {Time (s)} -min 0 -max 10 -loose no
$graph axis create samples -title Samples -min 0 -max 1000 -loose no
$graph x2axis use samples
$graph axis create voltage -title {Voltage (V)} -min -1.5 -max 1.5 -loose no
$graph axis create current -title {Current (mA)} -min -15 -max 15 -loose no
$graph axis create logic -title Enable -min -0.2 -max 1.2 -loose no
$graph track create voltage -yaxes voltage -weight 2
$graph track create current -yaxes current
$graph track create enable -yaxes logic

rbc::vector create trackTime trackVoltage trackCurrent trackEnable
for {set i 0} {$i<=1000} {incr i} {
    set t [expr {$i*0.01}]
    trackTime append $t
    trackVoltage append [expr {sin(2*acos(-1)*$t)}]
    trackCurrent append [expr {10*cos(2*acos(-1)*$t)}]
    trackEnable append [expr {int($t)%2}]
}
$graph line create voltage -track voltage -mapy voltage -xdata trackTime -ydata trackVoltage \
    -symbol none -color steelblue -linewidth 2 -label Voltage
$graph strip create current -track current -mapy current -xdata trackTime -ydata trackCurrent \
    -symbol none -color darkorange -linewidth 2 -label Current
$graph line create enable -track enable -mapy logic -xdata trackTime -ydata trackEnable \
    -symbol none -color seagreen -linewidth 2 -smooth step -label Enable
$graph grid configure -hide no
$graph marker create line -name threshold -track voltage -mapy voltage \
    -coords {-Inf 0.5 Inf 0.5} -outline firebrick -dashes {4 4}

$graph marker create text -name thresholdLabel -track voltage -mapy voltage \
    -coords {0.1 0.5} -text {Threshold: 0.5 V} -foreground firebrick -anchor sw
$graph marker create polygon -name interval -track current -mapy current \
    -coords {2 -Inf 3 -Inf 3 Inf 2 Inf} -fill lightyellow -outline {} -under yes

# Local legends follow their tracks and use the toolbar's active-legend controls.
$graph legend configure -hide no -position topmargin -rows 1
foreach track [$graph track names] {
    $graph track legend $track configure -hide no -position right -anchor ne -background white -borderwidth 1
}
set trackLegends 1
proc ToggleTrackLegends {} {
    foreach track [$::graph track names] {
        $::graph track legend $track configure -hide [expr {!$::trackLegends}]
    }
}

# The preset enables x/y at construction; include our custom axes after creating them.
.gtb setAxisActiveScale all
set phase 0
proc AdvanceTrackSignals {} {
    set ::phase [expr {$::phase+0.2}]
    trackVoltage expr {sin(2*acos(-1)*trackTime + $::phase)}
    trackCurrent expr {10*cos(2*acos(-1)*trackTime + $::phase)}
}
# Explicit shared-X controls work in both orientations.
proc TrackZoom {factor} {
    foreach axis {x samples} {
        lassign [$::graph axis limits $axis] lo hi
        set mid [expr {($lo+$hi)/2.0}]
        set half [expr {($hi-$lo)*$factor/2.0}]
        $::graph axis configure $axis -min [expr {$mid-$half}] -max [expr {$mid+$half}]
    }
}
proc TrackReset {} {
    $::graph axis configure x -min 0 -max 10
    $::graph axis configure samples -min 0 -max 1000
}
set voltageWeight 2
set inverted 0
set globalLegend 1
proc TrackWeightDisplay {} {
    set ::voltageWeight [format %.4g [$::graph track cget voltage -weight]]
}
bind $graph <<RbcGraphChanged>> TrackWeightDisplay
bind $graph <Motion> {
    set coordinate [expr {[%W cget -invertxy] ? %x : %y}]
    set track [%W track identify $coordinate]
    if {$track ne {} && [%W inside %x %y]} {
        lassign [%W track invtransform $track %x %y] time value
        .status configure -text [format {%s: time %.4g, value %.4g} $track $time $value]
    }
}
ttk::frame .controls
ttk::button .controls.in -text {Zoom X in} -command {TrackZoom 0.8}
ttk::button .controls.out -text {Zoom X out} -command {TrackZoom 1.25}
ttk::button .controls.reset -text {Reset X} -command TrackReset
ttk::button .controls.reverse -text {Reverse tracks} -command {
    $::graph track order [lreverse [$::graph track order]]
}
ttk::button .controls.advance -text {Update vectors} -command AdvanceTrackSignals
ttk::checkbutton .controls.legends -text {Local legends} -variable trackLegends -command ToggleTrackLegends
ttk::checkbutton .controls.global -text {Global legend} -variable globalLegend -command {
    $::graph legend configure -hide [expr {!$::globalLegend}]
}
ttk::checkbutton .controls.invert -text {Invert layout} -variable inverted -command {
    $::graph configure -invertxy $::inverted
}
ttk::label .controls.label -text {Voltage weight}
ttk::spinbox .controls.weight -from 0.1 -to 5 -increment 0.1 -width 6 -textvariable voltageWeight -command {
    $::graph track configure voltage -weight $::voltageWeight
}
grid .controls.in .controls.out .controls.reset .controls.reverse .controls.advance -padx 4 -pady 3 -sticky w
grid .controls.legends .controls.global .controls.invert .controls.label .controls.weight -padx 4 -pady 3 -sticky w
ttk::label .help -text {Drag: zoom | Shift-drag: pan | Wheel: zoom | Drag a separator: resize tracks
Right-click: controls | Click a legend entry: activate/hide | Click an axis: linear/log scale}
ttk::label .status -text {Move over a track to read values. Escape cancels separator dragging.}
pack .status .help .controls -side bottom -fill x
pack .gtb -fill both -expand yes
