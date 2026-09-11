# ------------------------------------------------------------------------------
#  RBC Demo barchart5.tcl
#
#  A sine wave plotted as a barchart.
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
set graph .graph
set HeaderText [MakeLine {
    |This is an example of the barchart widget.
    |The barchart has many components; x and y axis, legend, crosshairs,
    |elements, etc.
}]
CommonHeader .header $HeaderText 6 $DemoDir $graph barchart5.ps

### Set option defaults for the graph.
option add *graph.title {A Simple Barchart}
option add *graph.x.Font {Times 10}
option add *graph.x.Title {X Axis Label}
option add *graph.y.Title {Y Axis Label}
option add *graph.Element.Relief raised
set visual [winfo screenvisual .] 
if {($visual ne {staticgray}) && ($visual ne {grayscale})} {
    option add *graph.Element.Background white
    option add *graph.Legend.activeForeground pink
    option add *graph.background khaki
    option add *graph.plotBackground lightblue
}

### Create and configure barchart.
set barchart [graphtoolbar .bc -width 800 -height 500 -type barchart -zoom -zoomtitle -zoommark -crosshairs\
                      -crosshairsmode closest -scaletoggle y -activelegend -zoomwheel]
$barchart graph configure -relief raised -bd 2
$barchart graph xaxis configure -rotate 90 -stepsize 0 

### Define and compute vectors.
vector create x
vector create y
x seq -5.0 5.0 0.2 
y expr sin(x)
set barWidth 0.19

### Add barchart element.
$barchart graph element create sin -relief raised -bd 1 -x x -y y  -barwidth $barWidth

### Map everything, add Rbc_* commands.
grid .header -sticky ew
grid $barchart -sticky nsew -padx 4
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1
wm min . 0 0
