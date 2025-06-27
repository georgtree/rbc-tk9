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

### Load common commands (MakeSnapshot is used below).
source $DemoDir/scripts/common.tcl

### Create the graph.
set graph [graph .g]

### The configuration of the graph .g
####  (1) Set values for use as option defaults for the graph and its components
set configOptions {
    Axis.TickFont {Helvetica 14 bold}
    Axis.TitleFont {Helvetica 12 bold}
    Element.Pixels 8
    Element.ScaleSymbols true
    Element.Smooth cubic
    degrees.Command FormatAxisLabel
    degrees.LimitsFormat "Deg=%g"
    degrees.Subdivisions 0 
    degrees.Title Degrees 
    degrees.stepSize 90 
}
set resource [string trimleft $graph .]
foreach {option value} $configOptions {
    option add *$resource.$option $value
}
proc FormatAxisLabel {graph x} {
    ### FIXME rbc - on X11, "graph" does not render the degree sign correctly.
    ### This formula returns the integer followed by the Unicode character for the degree sign.
    format "%d%c" [expr int($x)] 0xB0
}

####  (2) Configure the graph
#####   (2a) create and configure graph pens and styles
set max -1.0
set step 0.2
set letters {A B C D E F G H I J K L}
set count 0
for {set level 30} {$level <= 100} {incr level 10} {
    set color [format "#dd0d%0.2x" [expr round($level*2.55)]]
    set pen pen$count
    ### No bitmap command in rbc - so
    ### use "-symbol circle" instead of "-symbol $symbol"
    ### set symbol "symbol$count"
    ### bitmap compose $symbol [lindex $letters $count]\
    ###	-font -*-helvetica-medium-r-*-*-34-*-*-*-*-*-*-*
    $graph pen create $pen -color $color -symbol circle -fill {} -pixels 13
    set min $max
    set max [expr {$max+$step}]
    lappend styles "$pen $min $max"
    incr count
}

#####   (2b) Create and configure graph axes
$graph axis create degrees -rotate 90
$graph xaxis use degrees

#####   (2c) Configure graph size and PostScript properties
$graph postscript configure -maxpect yes -landscape yes
$graph configure -width 600 -height 400

####  (3) Define and compute the vectors
set pi1_2 [expr {3.14159265358979323846/180.0}]
vector create w x sinX cosX radians
x seq -360.0 360.0 10.0
#x seq -360.0 -180.0 30.0
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
$graph element create line3 -color green4 -fill green -label cos(x) -mapx degrees -styles $styles -weights w -x x\
        -y cosX -yhigh yh -ylow yl
$graph element create line1 -color orange -outline black -fill orange -fill blue -label sin(x) -linewidth 3\
        -mapx degrees -pixels 6m -symbol [list @$bitmap @$mask] -x x -y sinX 

### Map everything, add Rbc_* commands.
grid .g -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 1
Rbc_ZoomStack $graph
Rbc_ActiveLegend $graph
Rbc_ClosestPoint $graph
Rbc_PrintKey $graph
set toolbar [Rbc_ToolbarCrosshair {} $graph]
grid $toolbar -sticky we
# FIXME rbc - On X11 the legend is not correctly sized for its
# text, possibly because it has an unexpected font.
# On X11 this code doesn't change the font, but it does
# size the legend correctly.
#
# Do this also for win32 (for which it does resize the font)
# because on win32 the legend font is too small.
if {[tk windowingsystem] in {x11 win32}} {
    $graph legend configure -font TkDefaultFont
}

### The code below is not executed and is not part of the demo.It remains available for experimentation.
####  (1) "Fill" experiment 1.
#     Choose "if 1" to add an area fill.
#     The original BLT demo did this, but the fill makes it hard to
#     see other graph features.
if 0 {
    $graph element configure line1 -areapattern solid -areaforeground green
    $graph element configure line3 -areapattern solid -areaforeground red
}

####  (2) "Fill" experiment 2.
#     Use the -areatile option to use the image $img.
if 0 {
    set data {
        R0lGODlhEAANAMIAAAAAAH9/f///////AL+/vwAA/wAAAAAAACH5BAEAAAUALAAAAAAQAA0A
        AAM8WBrM+rAEQWmIb5KxiWjNInCkV32AJHRlGQBgDA7vdN4vUa8tC78qlrCWmvRKsJTquHkp
        ZTKAsiCtWq0JADs=
    }
    set data {
        R0lGODlhEAANAMIAAAAAAH9/f///////AL+/vwAA/wAAAAAAACH5BAEAAAUALAAAAAAQAA0A
        AAM1WBrM+rAEMigJ8c3Kb3OSII6kGABhp1JnaK1VGwjwKwtvHqNzzd263M3H4n2OH1QBwGw6
        nQkAOw==
    }
    set img [image create photo -format gif -data $data]
    $graph element configure line1 -areapattern solid -areaforeground green -areatile $img 
    $graph element configure line3 -areapattern @$DemoDir/bitmaps/sharky.xbm -areaforeground red -areabackground {}\
            -areapattern solid
}

####  (3) Experiment with markers by placing a JPEG file at this location.
set fileName testImg.jpg
if { [file exists $fileName] } {
    set img2 [image create photo]
    winop readjpeg $fileName $img2
    if 1 { 
        puts stderr [time { 
            $graph marker create image -image $img2 -coords {-360.0 -1.0 360.0 1.0} -under yes -mapx degrees\
                    -name $fileName
        }]
    }
} 

####  (4) Experiment with image snapshots.
# Facilities for PostScript output and an image snapshot can be added to any
# demo by passing suitable arguments to the CommonHeader command.
#
# emf output (also to CLIPBOARD below) is available for Windows only.
if 0 {
    bind $graph <Control-ButtonPress-3> [list MakeSnapshot $graph demo2.ppm]
    bind $graph <Shift-ButtonPress-3> { 
        %W postscript output demo2.ps
        if {$tcl_platform(platform) eq {windows} } {
            %W snap demo2.emf -format emf
        }
    }
}
