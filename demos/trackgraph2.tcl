# Shared X navigation and row-local Y interaction with graphtoolbar.
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

# Local legends follow their tracks and use the toolbar's active-legend controls.
$graph legend configure -hide yes
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
ttk::frame .controls
ttk::button .controls.reverse -text {Reverse tracks} -command {
    $::graph track order [lreverse [$::graph track order]]
}
ttk::button .controls.advance -text {Update vectors} -command AdvanceTrackSignals
ttk::checkbutton .controls.legends -text {Track legends} -variable trackLegends -command ToggleTrackLegends
pack .controls.reverse .controls.advance .controls.legends -side left -padx 4 -pady 4
ttk::label .help -text {Drag: zoom | Shift-drag: pan | Wheel: zoom | Drag a separator: resize rows
Right-click: controls | Click a legend entry: activate/hide | Click an axis: linear/log scale}
pack .help .controls -side bottom -fill x
pack .gtb -fill both -expand yes
