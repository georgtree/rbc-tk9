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
set HeaderText {This is an example of the graphtoolbar widget with context control mode. It displays two-variable data\
with assorted line attributes and symbols.
Availible actions:
    - Zoom box selection: left mouse button press + motion + button release;
    - Reverse zoom/pan to the previous state:  middle mouse button click;
    - Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
    - Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
    - Panning: press and hold Shift + left mouse button press and hold + motion;
    - Toggle axive axis scale: left mouse button click over the selected axis;
    - Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
    - Change crosshairs mode: right mouse button click, and select from four availible modes;}
CommonHeader .header $HeaderText 6 $DemoDir

### Create the graph and configure its options
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode context -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode current -scaletoggle y -activelegend -zoomwheel -pan]
proc MultiplexView {widget args} { 
    $widget graph axis view y {*}$args
}
ttk::scrollbar .xbar -command [list $graph graph axis view x] -orient horizontal 
ttk::scrollbar .ybar -command [list MultiplexView $graph] -orient vertical 

####  Defines the data values (as lists), options for graph elements, and the graph elements themselves.
#####  (1) Define lists of data to be plotted
# Data are not defined as vectors or arrays in this example.
set X { 
    2.00000e-01 4.00000e-01 6.00000e-01 8.00000e-01 1.00000e+00 
    1.20000e+00 1.40000e+00 1.60000e+00 1.80000e+00 2.00000e+00 
    2.20000e+00 2.40000e+00 2.60000e+00 2.80000e+00 3.00000e+00 
    3.20000e+00 3.40000e+00 3.60000e+00 3.80000e+00 4.00000e+00 
    4.20000e+00 4.40000e+00 4.60000e+00 4.80000e+00 5.00000e+00 
} 
set Y1 { 
    4.07008e+01 7.95658e+01 1.16585e+02 1.51750e+02 1.85051e+02 
    2.16479e+02 2.46024e+02 2.73676e+02 2.99427e+02 3.23267e+02 
    3.45187e+02 3.65177e+02 3.83228e+02 3.99331e+02 4.13476e+02 
    4.25655e+02 4.35856e+02 4.44073e+02 4.50294e+02 4.54512e+02 
    4.56716e+02 4.57596e+02 4.58448e+02 4.59299e+02 4.60151e+02 
}
set Y2 { 
    5.14471e-00 2.09373e+01 2.84608e+01 3.40080e+01 3.75691e+01
    3.91345e+01 3.92706e+01 3.93474e+01 3.94242e+01 3.95010e+01 
    3.95778e+01 3.96545e+01 3.97313e+01 3.98081e+01 3.98849e+01 
    3.99617e+01 4.00384e+01 4.01152e+01 4.01920e+01 4.02688e+01 
    4.03455e+01 4.04223e+01 4.04990e+01 4.05758e+01 4.06526e+01 
}
set Y3 { 
    2.61825e+01 5.04696e+01 7.28517e+01 9.33192e+01 1.11863e+02 
    1.28473e+02 1.43140e+02 1.55854e+02 1.66606e+02 1.75386e+02 
    1.82185e+02 1.86994e+02 1.89802e+02 1.90683e+02 1.91047e+02 
    1.91411e+02 1.91775e+02 1.92139e+02 1.92503e+02 1.92867e+02 
    1.93231e+02 1.93595e+02 1.93958e+02 1.94322e+02 1.94686e+02 
}
#####  (2) Set option defaults for graph elements
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

#####  (3) Add elements to the graph
$graph graph element create line1 -x $X -y $Y2
$graph graph element create line2 -x $X -y $Y3
$graph graph element create line3 -x $X -y $Y1 

####  Configuration of .g (apart from its elements)
$graph graph axis configure x -scrollcommand {.xbar set} -scrollmax 10 -scrollmin 2 -title X
$graph graph axis configure y -scrollcommand {.ybar set} -title Y1 
$graph graph legend configure -activerelief flat -activeborderwidth 1 -position top -anchor ne -font {TkFixedFont 10}
$graph graph pen configure activeLine -showvalues y
$graph graph configure -title [pwd] -font {TkFixedFont 10} -plotpady {0.1i 0} 

####  Configure the "Fill" images for elements "line2" and "line3" - the flowers and sharks.
set image2 [image create photo -file $DemoDir/images/flowers.png]
$graph graph element configure line2 -areapattern @$DemoDir/bitmaps/sharky.xbm
$graph graph element configure line3 -areatile $image2

### Map everything, add Rbc_* commands and bindings.
grid .header -columnspan 2 -sticky ew
grid $graph .ybar -sticky news
grid .xbar -sticky ew
grid .ybar -sticky ns
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1

#### add bindings that highlight element in the legend when hover mouse over it
$graph graph element bind all <Enter> {
    %W legend activate [%W element get current]
}
$graph graph element bind all <Leave> {
    %W legend deactivate [%W element get current]
}
