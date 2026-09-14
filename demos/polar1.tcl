package require Tk
package require rbc
namespace import rbc::*

proc spiral {name} {
    vector create $name -type complex
    set values {}
    for {set i 0} {$i <= 120} {incr i} {
        set theta [expr {$i * 6.0 * acos(-1.0) / 180.0}]
        set radius [expr {0.12 + 0.0065 * $i}]
        lappend values [list [expr {$radius * cos($theta)}] [expr {$radius * sin($theta)}]]
    }
    $name set $values
}

proc rose {aName bName} {
    vector create $aName $bName
    for {set i 0} {$i <= 360} {incr i} {
        set theta [expr {$i * 1.0 * acos(-1.0) / 180.0}]
        set radius [expr {sin(2*$theta)*cos(2*$theta)}]
        $aName append [expr {$radius * cos($theta)}]
        $bName append [expr {$radius * sin($theta)}]
    }
}

proc sin {thetaName radiusName} {
    vector create $thetaName $radiusName
    $thetaName seq 0.0 [expr {acos(-1.0)}] [expr {acos(-1.0)/180.0}]
    $radiusName expr sin($thetaName)
}

# The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
set HeaderText [MakeLine {
    |This is an example of the polar graph widget with context control mode. It displays complex vector data
    |on a graph with polar coordinate system, and demonstrate different form of provided coordinates for polar element.
}]
CommonHeader .header $HeaderText 6 $DemoDir
ExpandableText .details 600 {Availible actions} {
- Zoom box selection: left mouse button press + motion + button release;
- Reverse zoom/pan to the previous state:  middle mouse button click;
- Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
- Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
- Panning: press and hold Shift + left mouse button press and hold + motion;
- Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
- Change crosshairs mode: right mouse button click, and select from four availible modes;}

### Create the graph and configure its options
set graph [graphtoolbar .g -width 700 -height 600 -type polar -controlmode context -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode closest -activelegend -zoomwheel -pan]
$graph graph grid on

set areaTile1 [image create photo -width 8 -height 8]
$areaTile1 put #e6eef8@0.8 -to 0 0 8 8
set areaTile2 [image create photo -width 8 -height 8]
$areaTile2 put red@0.2 -to 0 0 8 8

set areaTile3 [image create photo -width 8 -height 8]
$areaTile3 put green@0.1 -to 0 0 8 8

# coordinates provided in form of the complex vector
spiral spiralVec
$graph graph element create spiral -cdata spiralVec -symbol {} -color green -linewidth 2  -areatile $areaTile1 -areaclose origin
# coordinates provided in form of two real vectors representing a and b in a+b*i
rose roseAVec roseBVec
$graph graph element create rose -x roseAVec -y roseBVec -symbol {} -color purple -linewidth 2 -areatile $areaTile2 
# coordinates provided in form of two real vectors representing radius and degree in radians
sin thetaVec radiusVec
$graph graph element create sin -datacoordinates polar -x thetaVec -y radiusVec -symbol {} -color blue -linewidth 2  -areatile $areaTile3

### Map everything
grid .header -columnspan 1 -sticky ew
grid .details -sticky ew -padx 15
grid $graph -sticky news
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 0
grid rowconfigure . 1 -weight 0
grid rowconfigure . 2 -weight 1
