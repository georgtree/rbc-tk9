# ------------------------------------------------------------------------------
#  RBC Demo simplify1.tcl
# ------------------------------------------------------------------------------
#  Simplify paired real vectors with a tolerance in data coordinates.
# ------------------------------------------------------------------------------
package require Tk
package require rbc::graphtoolbar
namespace import rbc::graphtoolbar
namespace import rbc::vector

set DemoDir [file normalize [file dirname [info script]]]
source [file join $DemoDir scripts common.tcl]

proc Simplify {args} {
    variable tolerance
    variable status
    set kept [vector simplify x y xs ys $tolerance]
    set status [format {Tolerance: %.3f Original: %d points Simplified: %d points} $tolerance [x length] $kept]
}

set tolerance 0.1
set status {}
vector create x y xs ys
x seq -3.0 6.0 0.025
y expr {sin(x)+0.08*sin(43*x)+0.04*cos(71*x)}

set HeaderText [MakeLine {
    |Douglas-Peucker simplification of a noisy sine curve. Move the tolerance
    |slider to change how many original points are retained. Distances are
    |measured in data coordinates, not pixels; zooming does not change the result.
}]
CommonHeader .header $HeaderText 3 $DemoDir
ExpandableText .details 800 {Simplification and interactions} {
The first and last points are retained. Each removed point is within tolerance
of its replacement segment. This is geometric simplification, not smoothing:
retained values are copied exactly, and small peaks may disappear.

The output vectors already exist and stay attached to the graph throughout.
Use the left mouse button to select a zoom box, Ctrl + wheel to zoom, and
Shift + left drag to pan. Restore the view with the toolbar.
}

option add *HighlightThickness 0
set graph [graphtoolbar .g -width 900 -height 550 -type graph -controlmode toolbar -zoom -zoomtitle -zoommark -pan\
                   -zoomwheel -crosshairs -crosshairsmode closest -crosshairsclosestopts {-interpolate no}]
$graph graph configure -title {Polyline simplification}
$graph graph xaxis configure -title {X (radians)} -loose no
$graph graph yaxis configure -title Y
$graph graph legend configure -background {}
$graph graph grid on
$graph graph element create original -label Original -x x -y y -symbol none -color grey50 -linewidth 1 -dashes {3 3}
$graph graph element create simplified -label Simplified -x xs -y ys -symbol circle -pixels 4 -color firebrick\
        -linewidth 2

ttk::frame .controls
ttk::label .controls.label -text {Tolerance:}
ttk::scale .controls.scale -from 0 -to 0.5 -variable tolerance -command Simplify
ttk::label .status -textvariable status

grid .controls.label -row 0 -column 0 -padx 6
grid .controls.scale -row 0 -column 1 -sticky ew -padx 6
grid columnconfigure .controls 1 -weight 1
grid .header -row 0 -sticky ew
grid .details -row 1 -sticky ew -padx 12
grid $graph -row 2 -sticky nsew
grid .controls -row 3 -sticky ew -padx 12 -pady 6
grid .status -row 4 -sticky w -padx 12 -pady 6
grid columnconfigure . 0 -weight 1
grid rowconfigure . 2 -weight 1

Simplify
wm title . {RBC: vector simplification}
