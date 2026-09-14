# ------------------------------------------------------------------------------
#  RBC Demo graph1.tcl
#
#  Demonstrates multiple features of the graphtoolbar widget.
# ------------------------------------------------------------------------------

package require Tk
package require rbc
namespace import rbc::*

# The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
set HeaderText [MakeLine {
    |This is an example of the graph/graphtoolbar widget with context control mode. It displays two-variable data
    |with assorted line attributes and symbols.
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

### Create the graph and configure its options
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode context -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode current -scaletoggle y -activelegend -zoomwheel -pan]
proc MultiplexView {widget args} { 
    $widget graph axis view y {*}$args
}
ttk::scrollbar .xbar -command [list $graph graph axis view x] -orient horizontal 
ttk::scrollbar .ybar -command [list MultiplexView $graph] -orient vertical 

####  Defines the data values (as vectors), options for graph elements, and the graph elements themselves.
#####  Define vectors of data to be plotted
set pi 3.14159265358979323846
vector create X Y1 Y2 Y3
X seq -$pi [expr {5*$pi}] [expr {$pi/100}]
Y1 expr {0.7*sin(X)+0.4*cos(5*X)}
Y2 expr {1.2*cos(X)}
Y3 expr 0.5*sin(8*X-$pi)
#####  Set option defaults for graph elements
set configOptions {
    Element.Pixels 10
    Element.Smooth catrom
    line1.Color red4
    line1.Fill red1
    line1.Symbol circle
    line2.Color purple4
    line2.Fill purple3
    line2.Symbol arrow
    line3.Symbol triangle
    line3.Fill green2
}
set resource [string trimleft [$graph subwidget graph] .]
foreach {option value} $configOptions {
    option add *$resource.$option $value
}

#####  Add elements to the graph
$graph graph element create line1 -x X -y Y1
$graph graph element create line2 -x X -y Y2
$graph graph element create line3 -x X -y Y3 

####  Configuration of .g (apart from its elements)
$graph graph grid on
$graph graph axis configure x -min -$pi -max $pi -scrollcommand {.xbar set} -scrollmax {5*$pi} -scrollmin -$pi -title X 
$graph graph axis configure y -scrollcommand {.ybar set} -title Y1 
$graph graph legend configure -activerelief flat -activeborderwidth 1 -position top -anchor ne -font {TkFixedFont 10}
$graph graph pen configure activeLine -showvalues y
$graph graph configure -title [pwd] -font {TkFixedFont 10} -plotpady {0.1i 0} 

####  Configure the "Fill" image for elements "line2"
set areaTile1 [image create photo -width 8 -height 8]
$areaTile1 put #e6eef8@0.5 -to 0 0 8 8
$areaTile1 put #cedef0@0.5 -to 0 0 4 8
$graph graph element configure line1 -areapattern solid -areaforeground purple -areaopacity 0.1
$graph graph element configure line2 -areatile $areaTile1 -areaclose baseline
$graph graph element configure line3
# set explicit order of elements shown
$graph graph element show {line3 line2 line1}

### Map everything
grid .header -columnspan 2 -sticky ew
grid .details -sticky ew -padx 15
grid $graph .ybar -sticky news
grid .xbar -sticky ew
grid .ybar -sticky ns
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 0
grid rowconfigure . 1 -weight 0
grid rowconfigure . 2 -weight 1

### add bindings that highlight element in the legend when hover mouse over it
$graph graph element bind all <Enter> {
    %W legend activate [%W element get current]
}
$graph graph element bind all <Leave> {
    %W legend deactivate [%W element get current]
}
