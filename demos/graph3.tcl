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
set HeaderText {This is an example of a bitmap marker. Try zooming in on a region by clicking and holding the left\
button, moving the pointer, and release the button. Notice that the bitmap scales too. To restore the last view,\
click on the middle mouse button (wheel button).
Availible actions:
    - Zoom box selection: left mouse button press + motion + button release;
    - Reverse zoom/pan to the previous state:  middle mouse button click;
    - Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
    - Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
    - Panning: press and hold Shift + left mouse button press and hold + motion;
    - Toggle axive axis scale: left mouse button click over the selected axis;
    - Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
    - Change crosshairs mode: select from availible mods on toolbar;}
CommonHeader .header $HeaderText 4 $DemoDir .g

option add *HighlightThickness 0

### Define graph:
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode toolbar -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode closest -scaletoggle y -activelegend -zoomwheel -pan]

$graph graph legend configure -font {TkFixedFont 10}

### The construction of the graph .g
####  (1) Set values for use as option defaults for graph elements
set configOptions {
    Element.Pixels 1.75m
    Element.ScaleSymbols yes
}
set resName [string trimleft [$graph subwidget graph] .]
foreach {option value} $configOptions {
    option add *$resName.$option $value
}

####  (2) Define and compute the vectors
set pi1_2 [expr {3.14159265358979323846/180.0}]
vector create x sinX cosX -variable {}
x seq -360 360 5
sinX expr {sin(x*$pi1_2)}
cosX expr {cos(x*$pi1_2)}

####  (3) Add elements to the graph
$graph graph element create line1 -label sin(x) -fill #7D7CE0 -color black -x x -y sinX
$graph graph element create line2 -label cos(x) -color green4  -fill #90E07C -x x -y cosX

### Now add a bitmap marker (background image) and configure the postscript component.
$graph graph marker create bitmap -name bg -coords {-360 -0.75 360 0.75}\
    -bitmap @$DemoDir/bitmaps/Tcl9LogoMonochromeText.xbm -bg white -fg #7CDFE0 -under yes -rotate 0
set textMarker [$graph graph marker create text -text {} -anchor sw -justify left]
$graph graph marker configure $textMarker -coords {-Inf -Inf}
$graph graph postscript configure -maxpect yes -landscape yes

### Map everything
grid .header -sticky ew -padx 4 -pady 4
grid $graph -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1
