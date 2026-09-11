# ------------------------------------------------------------------------------
#  RBC Demo graph2.tcl
#
#  Sine and cosine functions with different types of symbol at data points.
# ------------------------------------------------------------------------------

package require Tk
package require rbc
namespace import rbc::*


### The script can be run from any location.It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands
source $DemoDir/scripts/common.tcl
set HeaderText {This is an example of the graphtoolbar widget with toolbar control mode. In this example sine and\
cosine curves are displayed with applied different styles pens. Also demonstrated custom X-axis tick markers and limits\
labels.
Availible actions:
    - Zoom box selection: left mouse button press + motion + button release;
    - Reverse zoom/pan to the previous state:  middle mouse button click;
    - Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
    - Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
    - Panning: press and hold Shift + left mouse button press and hold + motion;
    - Toggle axive axis scale: left mouse button click over the selected axis;
    - Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
    - Change crosshairs mode: select from availible mods on toolbar;}
CommonHeader .header $HeaderText 6 $DemoDir

### Create the graph.
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode toolbar -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode current -scaletoggle y -activelegend -zoomwheel -pan]

### The configuration of the graph .g
####  (1) Set values for use as option defaults for the graph and its components
set configOptions {
    Element.Pixels 8
    Element.ScaleSymbols true
    Element.Smooth cubic
    degrees.Command FormatAxisLabel
    degrees.LimitsFormat "Deg=%g"
    degrees.Subdivisions 0
    degrees.Title Degrees
    degrees.stepSize 90
    degrees.tickFont {Helvetica 10 bold}
    degrees.limitsFont {Helvetica 10 bold}
}
set resource [string trimleft [$graph subwidget graph] .]
foreach {option value} $configOptions {
    option add *$resource.$option $value
}
proc FormatAxisLabel {graph x} {
    format "%d%c" [expr int($x)] 0xB0
}

####  (2) Configure the graph
#####   (2a) create and configure graph pens and styles
set max -1.0
set step 0.2
set letters {A B C D E F G H I J K L}
set count 0
for {set level 30} {$level <= 100} {incr level 10} {
    set color [format "#E07C%0.2x" [expr round($level*2.55)]]
    set pen pen$count
    ### No bitmap command in rbc - so
    ### use "-symbol circle" instead of "-symbol $symbol"
    ### set symbol "symbol$count"
    ### bitmap compose $symbol [lindex $letters $count]\
    ###	-font -*-helvetica-medium-r-*-*-34-*-*-*-*-*-*-*
    $graph graph pen create $pen -color $color -symbol circle -fill {} -pixels 13
    set min $max
    set max [expr {$max+$step}]
    lappend styles "$pen $min $max"
    incr count
}

#####  (2b) Create and configure graph axes
$graph graph axis create degrees -rotate 90
$graph graph xaxis use degrees
$graph graph axis configure y -tickfont {Helvetica 10 bold}
$graph graph legend configure -font {TkFixedFont 10}
$graph graph grid on

#####  (2c) Configure graph size and PostScript properties
$graph graph postscript configure -maxpect yes -landscape yes

####  (3) Define and compute the vectors
set pi1_2 [expr {3.14159265358979323846/180.0}]
vector create w x sinX cosX radians
x seq -360.0 360.0 10.0
radians expr {x*$pi1_2}
sinX expr sin(radians)
cosX expr cos(radians)
cosX dup w
vector destroy radians
vector create xh xl yh yl
set pct [expr {($cosX(max)-$cosX(min))*0.025}]
yh expr {cosX+$pct}
yl expr {cosX-$pct}
set pct [expr {($x(max)-$x(min))*0.025}]
xh expr {x+$pct}
xl expr {x-$pct}

####  (4) Add elements to the graph
set bitmap [file join $DemoDir bitmaps spiral.xbm]
set mask [file join $DemoDir bitmaps spiral_mask.xbm]
$graph graph element create line3 -color green4 -fill #90E07C -label cos(x) -mapx degrees -styles $styles -weights w -x x\
        -y cosX -yhigh yh -ylow yl
$graph graph element create line1 -color orange -outline black -fill orange -fill #7D7CE0 -label sin(x) -linewidth 3\
        -mapx degrees -pixels 6m -symbol [list @$bitmap @$mask] -x x -y sinX 

### Map everything
grid .header -columnspan 1 -sticky ew
grid $graph -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 1
