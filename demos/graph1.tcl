# ------------------------------------------------------------------------------
#  RBC Demo graph1.tcl
#
#  Demonstrates multiple features of the graph widget.
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
set HeaderText {This is an example of the graph widget. It displays two-variable data with assorted line attributes and\
                        symbols.}
CommonHeader .header $HeaderText 6 $DemoDir .g graph1.ps
CommonFooter .footer $DemoDir
proc MultiplexView {args} { 
    eval .g axis view y $args
    eval .g axis view y2 $args
}
ttk::scrollbar .xbar -command {.g axis view x} -orient horizontal 
ttk::scrollbar .ybar -command MultiplexView -orient vertical 

### Set option defaults for $graph, related to the tile image.
if {[winfo screenvisual .] ne {staticgray}} {
    set image [image create photo -file $DemoDir/images/metal.png]
    option add *Graph.Tile $image
    option add *Label.Tile $image
    option add *Frame.Tile $image
    option add *TileOffset 0
}

### Create the graph and configure its options
set graph [graph .g -width 600 -height 400]

####  Defines the data values (as lists), options for graph elements, and the graph elements themselves.
#####   (1) Define lists of data to be plotted
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
#####   (2) Set option defaults for graph elements
set configOptions {
    Element.Pixels 6
    Element.Smooth catrom
    line1.Color red4
    line1.Fill red1
    line1.Symbol circle
    line2.Color purple4
    line2.Fill purple1
    line2.Symbol arrow
    line3.Color green4
    line3.Fill green1
    line3.Symbol triangle
}
set resource [string trimleft $graph .]
foreach {option value} $configOptions {
    option add *$resource.$option $value
}
#####   (3) Add elements to the graph
$graph element create line1 -x $X -y $Y2
$graph element create line2 -x $X -y $Y3
$graph element create line3 -x $X -y $Y1

####  Configuration of .g (apart from its elements)
.g postscript configure -center yes -maxpect yes -landscape no -preview yes
.g axis configure x -scrollcommand {.xbar set} -scrollmax 10 -scrollmin 2 
.g axis configure y -scrollcommand {.ybar set}
.g axis configure y2 -scrollmin 0.0 -scrollmax 1.0 -hide no -title Y2 
.g legend configure -activerelief flat -activeborderwidth 1 -position top -anchor ne
.g pen configure activeLine -showvalues y
.g configure -plotpady {0.5i 0} 
.g configure -title [pwd]

####  Configure the "Fill" images for elements "line2" and "line3" - the bees and sharks.
set image2 [image create photo -file $DemoDir/images/flowers.png]
.g element configure line2 -areapattern @$DemoDir/bitmaps/sharky.xbm
#	-areaforeground blue -areabackground ""
.g element configure line3 -areatile $image2


### Map everything, add Rbc_* commands and bindings.
grid .header -columnspan 2 -sticky ew
grid .g .ybar -sticky news
grid .xbar -sticky ew
grid .footer -columnspan 2 -sticky ew
grid .ybar -sticky ns
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1

Rbc_ZoomStack $graph
Rbc_Crosshairs $graph
Rbc_ActiveLegend $graph
Rbc_ClosestPoint $graph

.g element bind all <Enter> {
    %W legend activate [%W element get current]
}
.g element bind all <Leave> {
    %W legend deactivate [%W element get current]
}
.g axis bind all <Enter> {
    set axis [%W axis get current]
    %W axis configure $axis -background lightblue2
}
.g axis bind all <Leave> {
    set axis [%W axis get current]
    %W axis configure $axis -background {}
}

# FIXME rbc - On X11 the legend is not correctly sized for its
# text, possibly because it has an unexpected font.
# On X11 this code doesn't change the font, but it does
# size the legend correctly.
#
# Do this also for win32 (for which it does resize the font)
# because on win32 the legend font is too small.
if {[tk windowingsystem] in {x11 win32}} {
    .g legend configure -font TkDefaultFont
}

### The code below is not executed and is not part of the demo. It remains available for experimentation.
####  (1) The -leftvariable option appears not to work. See graph6.tcl for further discussion.
if 0 {
    .g configure -leftvariable left 
    trace variable left w {UpdateTable .g}
    proc UpdateTable {graph p1 p2 how} {
        table configure . c0 -width [$graph extents leftmargin]
        table configure . c2 -width [$graph extents rightmargin]
        table configure . r1 -height [$graph extents topmargin]
        table configure . r3 -height [$graph extents bottommargin]
    }
}

####  (2) Unused options.
if 0 {
    set configOptions {
        Axis.TitleFont {Times 18 bold}
        Legend.ActiveBackground	khaki2
        Legend.ActiveRelief sunken
        Legend.Background {}
        Title {A Simple X-Y Graph}
        activeLine.Color yellow4
        activeLine.Fill yellow
        background khaki3
        x.Descending no
        x.Loose no
        x.Title {X Axis Label}
        y.Rotate 90
        y.Title {Y Axis Label}
    }
}
