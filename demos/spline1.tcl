# --------------------------------------------------------------------------
# Starting with Tcl 8.x, the BLT commands are stored in their own
# namespace called "blt".  The idea is to prevent name clashes with
# Tcl commands and variables from other packages, such as a "table"
# command in two different packages.
#
# You can access the BLT commands in a couple of ways.  You can prefix
# all the BLT commands with the namespace qualifier "blt::"
#
#    blt::graph .g
#    blt::table . .g -resize both
#
# or you can import all the command into the global namespace.
#
#    namespace import blt::*
#    graph .g
#    table . .g -resize both
#
# --------------------------------------------------------------------------
# restart using wish \
exec wish "$0" "$@"

package require rbc

### The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
option add *graph.Element.ScaleSymbols true

### Make and fill small vectors
rbc::vector create x y
x seq 10 0 -0.5
y expr sin(x^3)
x expr x*x
x sort y
rbc::vector create x2 y1 y2 y3

### make and fill (x only) large vectors
x populate x2 10

### natural spline interpolation
rbc::spline natural x y x2 y1

### quadratic spline interpolation
rbc::spline quadratic x y x2 y2

### make plot
rbc::graph .graph -width 600 -height 400
.graph xaxis configure -title "x\u00b2"
.graph yaxis configure -title "sin(y\u00b3)"
.graph pen configure activeLine -pixels 5
.graph element create Original -x x -y y -color red4 -fill red -pixels 5 -symbol circle
.graph element create Natural -x x2 -y y1 -color green4 -fill green -pixels 3 -symbol triangle
.graph element create Quadratic -x x2 -y y2 -color blue4 -fill orange2 -pixels 3 -symbol arrow
pack .graph -fill both -expand 1
Rbc_ZoomStack .graph
Rbc_Crosshairs .graph
Rbc_ActiveLegend .graph
Rbc_ClosestPoint .graph
Rbc_PrintKey .graph
.graph grid on
