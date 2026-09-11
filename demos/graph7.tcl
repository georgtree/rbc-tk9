# ------------------------------------------------------------------------------
#  RBC Demo graph7.tcl
#
#  This demo graph displays a scatter plot with a large number of points.
# ------------------------------------------------------------------------------

package require Tk
package require rbc
namespace import rbc::*


### The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
set HeaderText {At this scale the 250,000 plotted points overlap, zoom closer to see individual points.
Availible actions:
    - Zoom box selection: left mouse button press + motion + button release;
    - Reverse zoom/pan to the previous state:  middle mouse button click;
    - Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
    - Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
    - Panning: press and hold Shift + left mouse button press and hold + motion;
    - Toggle axive axis scale: left mouse button click over the selected axis;
    - Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
    - Change crosshairs mode: select from availible mods on toolbar;}

CommonHeader .header $HeaderText 7 $DemoDir .graph

### Colors and other options for the graph:
option add *HighlightThickness 0
option add *Element.ScaleSymbols no
option add *activeLine.Color yellow4
option add *activeLine.Fill yellow
option add *activeLine.LineWidth 0

### Define graph and its elements:
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode toolbar -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode closest -crosshairsclosestopts {-interpolate no} -scaletoggle all\
                   -activelegend -zoomwheel -pan]
set length 250000
$graph graph configure -title "Scatter Plot\n$length points"
$graph graph xaxis configure -loose no -title {X Axis Label}
$graph graph yaxis configure -title {Y Axis Label} 
$graph graph legend configure -activerelief sunken -background {}
$graph graph element create line3 -symbol square -color green4 -fill green2 -linewidth 0 -outlinewidth 1 -pixels 4

### Map everything
grid .header -sticky ew
grid $graph -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1
wm min . 0 0

### Warn of delay calculating and drawing points:
label .lab7 -text {Calculating ...} -bg yellow -fg red
place .lab7 -relx 0.5 -rely 0.0 -anchor n
update

### Now add the data points.
vector create x($length) y($length)
x expr random(x)
y expr random(y)
x sort y
$graph graph element configure line3 -x x -y y

### Disable the GUI while the points are being drawn.

### FIXME rbc - rbc::busy segfaults ...
#::rbc::busy hold $graph
#update
#::rbc::busy release $graph

### ... so instead use a grab.
### Catch so the grab is always released.
grab .lab7
catch update
grab release .lab7
destroy .lab7
