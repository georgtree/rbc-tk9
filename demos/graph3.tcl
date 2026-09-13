# ------------------------------------------------------------------------------
#  RBC Demo graph3.tcl
#
#  Sine and cosine functions as curves with data points, and a background image.
# ------------------------------------------------------------------------------

package require Tk
package require rbc
namespace import rbc::*


### The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
set HeaderText [MakeLine {
    |This is an example of graph in logariphmic scale with different functions plotted, each one has individual style.
}]
CommonHeader .header $HeaderText 4 $DemoDir .g
ExpandableText .details 800 {Availible actions} {
- Zoom box selection: left mouse button press + motion + button release;
- Reverse zoom/pan to the previous state:  middle mouse button click;
- Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
- Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
- Panning: press and hold Shift + left mouse button press and hold + motion;
- Toggle axive axis scale: left mouse button click over the selected axis;
- Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
- Change crosshairs mode: select from availible mods on toolbar;}

option add *HighlightThickness 0

### Define graph:
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode toolbar -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode closest -scaletoggle y -activelegend -zoomwheel -pan]

$graph graph legend configure -font {TkFixedFont 10} -position plotarea -anchor nw -background white
$graph graph axis configure y -logscale yes
$graph graph grid on

### The construction of the graph .g
####  (1) Set values for use as option defaults for graph elements
set configOptions {
    Element.Pixels 3
    Element.ScaleSymbols yes
}
set resName [string trimleft [$graph subwidget graph] .]
foreach {option value} $configOptions {
    option add *$resName.$option $value
}

####  (2) Define and compute the vectors
set pi1_2 [expr {3.14159265358979323846/180.0}]
vector create X Y1 Y2 Y3 Y4 Y5 Y6
X seq 0.1 4 [expr {3.9/double(100)}]
Y1 expr {sqrt(X)}
Y2 expr {X}
Y3 expr {X^2}
Y4 expr {10^X}
Y5 expr {X^X}
Y6 expr {10^(X^2)}

####  (3) Add elements to the graph
$graph graph element create line1 -label √x -dash {4 4} -color #F9A620 -x X -y Y1 -symbol {} -linewidth 2
$graph graph element create line2 -label x -color #5D2E8C -x X -y Y2 -symbol {} -linewidth 2
$graph graph element create line3 -label x² -color #A8D5E2 -fill #4A7856 -x X -y Y3
$graph graph element create line4 -label 10ˣ -color #F9A620 -fill #2EC4B6 -x X -y Y4
$graph graph element create line5 -label xˣ -color #A0B9BF -fill {} -x X -y Y5 -pixel 2 -linewidth 2
$graph graph element create line6 -label 10ˣ^² -color #2EC4B6 -dash {4 2 4} -x X -y Y6 -symbol {} -linewidth 2

### Map everything
grid .header -sticky ew -padx 4 -pady 4
grid .details -sticky ew -padx 15
grid $graph -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 0
grid rowconfigure . 1 -weight 0
grid rowconfigure . 2 -weight 1
