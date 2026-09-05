# ------------------------------------------------------------------------------
#  RBC Demo graph6.tcl
#
#  This demo graph plots 39 different signals as monochrome curves.
# ------------------------------------------------------------------------------

package require Tk
package require rbc
namespace import rbc::*


### The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl

# To use the demo's "PostScript Options" dialog, source the file
# scripts/ps.tcl. If this is not done, the "Print" button will print to a
# file without offering an options dialog.  See command CommonPrint in
# scripts/common.tcl for choices, including the stock dialog
# Rbc_PostScriptDialog which is not used in these demos.
source $DemoDir/scripts/ps.tcl
set HeaderText {This is an example of displaying many waveforms curves but but as a monochrome curves.
Availible actions:
    - Zoom box selection: left mouse button press + motion + button release;
    - Reverse zoom/pan to the previous state:  middle mouse button click;
    - Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
    - Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
    - Panning: press and hold Shift + left mouse button press and hold + motion;
    - Toggle axive axis scale: left mouse button click over the selected axis;
    - Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
    - Change crosshairs mode: select from availible mods on toolbar;}
CommonHeader .header $HeaderText 5 $DemoDir .graph graph6.ps

### Set options for graph.
option add *Graph.Width 10i
option add *Graph.leftMargin .75i
option add *Graph.Height 6i
option add *Graph.plotBackground black
option add *Graph.x.hide yes
option add *Graph.x.title {}
option add *Graph.y.rotate 90
#option add *Graph.y.stepSize 2.0
option add *Graph.title {}
option add *graph.Title {Example s27}
option add *graph.x.hide no
option add *graph.topMargin 0
option add *graph.bottomMargin 0
option add *x.Title Time
option add *y.Title Signals
option add *Pixels 1
option add *Reduce 0.5
option add *bufferElements no
option add *Element.color green4
option add *Element.ScaleSymbols true
option add *Element.Color grey70
option add *Element.Symbol none
option add *Element.LineWidth 1
#option add *Element.Smooth natural
option add *Element.Smooth catrom
option add *activeLine.LineWidth 2
option add *activeLine.Color white
option add *activeLine.Color green1
#option add *Legend.Hide yes
option add *Legend.Position right
option add *Legend.Relief flat
option add *Legend.activeRelief sunken
option add *Legend.borderWidth 2
option add *Legend.Font -*-helvetica-medium-r-*-*-10-*-*-*-*-*-*-*
option add *Grid.hide no
option add *Grid.dashes {1 5}

### Define vectors x and v1 to v39; then load values into vectors.
source $DemoDir/scripts/graph46.tcl

### Define graph and its elements:
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode toolbar -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode closest -scaletoggle y -activelegend -zoomwheel -pan\
                   -zoomboxopts {-outline white} -pointeropts {-outline white} -zoomtitleopts {-foreground white}]
$graph graph legend configure -anchor nw -position .legend
for {set i 1} {$i<=39} {incr i} {
    $graph graph element create V$i -x x -y v$i
}

### Map everything, add Rbc_* commands and bindings.
wm min . 0 0 
grid .header -sticky ew
grid $graph .legend -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1

