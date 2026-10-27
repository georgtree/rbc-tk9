package require Tk
package require rbc::graphtoolbar
namespace import rbc::*

# The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
set HeaderText [MakeLine {
    |This is an example of the graph/graphtoolbar widget with trackgraph element.
}]
CommonHeader .header $HeaderText 6 $DemoDir
ExpandableText .details 800 {Availible actions} {
- Zoom box selection: left mouse button press + motion + button release;
- Reverse zoom/pan to the previous state:  middle mouse button click;
- Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
- Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
- Panning: press and hold Shift + left mouse button press and hold + motion;
- Toggle axive axis scale: left mouse button click over the selected axis;
- Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
- Change crosshairs mode: right mouse button click, and select from four availible modes;}


rbc::graphtoolbar .g -type trackgraph -interactive -width 800 -height 500 -crosshairsmode closest -zoommark\
        -controlmode context
set graph [.g subwidget graph]
set configOptions {
    Element.Symbol none
    Element.LineWidth 2
}
set resource [string trimleft $graph .]
foreach {option value} $configOptions {
    option add *$resource.$option $value
}
$graph configure -trackresize yes -trackseparatorcolor gray60 -trackseparatorwidth 2
$graph axis configure x -title {Time (s)} -min 0 -max 10 -loose no
$graph axis create samples -title Samples -min 0 -max 1000 -loose no
$graph x2axis use samples
$graph axis create voltage -title {Voltage (V)} -min -1.5 -max 1.5 -loose no
$graph axis create current -title {Current (mA)} -min -15 -max 15 -loose no
$graph axis create logic -title Enable -min -0.2 -max 1.2 -loose no
$graph track create voltage -yaxes voltage -weight 2
$graph track create current -yaxes current
$graph track create enable -yaxes logic

vector create trackTime trackVoltage trackCurrent trackEnable
for {set i 0} {$i<=1000} {incr i} {
    set t [expr {$i*0.01}]
    trackTime append $t
    trackVoltage append [expr {sin(2*acos(-1)*$t)}]
    trackCurrent append [expr {10*cos(2*acos(-1)*$t)}]
    trackEnable append [expr {int($t)%2}]
}
$graph line create voltage -track voltage -mapy voltage -xdata trackTime -ydata trackVoltage -color steelblue\
        -label Voltage
$graph strip create current -track current -mapy current -xdata trackTime -ydata trackCurrent -color darkorange\
        -label Current
$graph line create enable -track enable -mapy logic -xdata trackTime -ydata trackEnable -color seagreen -smooth step\
        -label Enable
$graph grid configure -hide no
$graph marker create line -name threshold -track voltage -mapy voltage -coords {-Inf 0.5 Inf 0.5} -outline firebrick\
        -dashes {4 4}

$graph marker create text -name thresholdLabel -track voltage -mapy voltage -coords {0.1 0.5} -text {Threshold: 0.5 V}\
        -foreground firebrick -anchor sw
$graph marker create polygon -name interval -track current -mapy current -coords {2 -Inf 3 -Inf 3 Inf 2 Inf}\
        -fill lightyellow -outline {} -under yes

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
.g setAxisActiveScale all
set phase 0
proc AdvanceTrackSignals {} {
    set ::phase [expr {$::phase+0.2}]
    trackVoltage expr {sin(2*acos(-1)*trackTime + $::phase)}
    trackCurrent expr {10*cos(2*acos(-1)*trackTime + $::phase)}
}
set voltageWeight 2
set inverted 0
set globalLegend 1
set trackDividers 1
proc TrackWeightDisplay {} {
    set ::voltageWeight [format %.4g [$::graph track cget voltage -weight]]
}
bind $graph <<RbcGraphChanged>> TrackWeightDisplay
bind $graph <Motion> {
    set coordinate [expr {[%W cget -invertxy] ? %x : %y}]
    set track [%W track identify $coordinate]
    if {$track ne {} && [%W inside %x %y]} {
        lassign [%W track invtransform $track %x %y] time value
    }
}
ttk::frame .controls
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
ttk::checkbutton .controls.dividers -text Dividers -variable trackDividers -command {
    $::graph configure -trackseparatorcolor [expr {$::trackDividers ? "gray60" : ""}]
}
ttk::label .controls.label -text {Voltage weight}
ttk::spinbox .controls.weight -from 0.1 -to 5 -increment 0.1 -width 6 -textvariable voltageWeight -command {
    $::graph track configure voltage -weight $::voltageWeight
}
grid .controls.reverse .controls.advance -padx 4 -pady 3 -sticky w
grid .controls.legends .controls.global .controls.invert .controls.label .controls.weight .controls.dividers -padx 4\
        -pady 3 -sticky w

grid .header -sticky ew
grid .details -sticky ew
grid .g -sticky nsew
grid .controls -sticky ew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 0
grid rowconfigure . 1 -weight 0
grid rowconfigure . 2 -weight 1
grid rowconfigure . 3 -weight 0
