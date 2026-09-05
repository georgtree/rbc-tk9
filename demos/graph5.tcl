# ------------------------------------------------------------------------------
#  RBC Demo graph5.tcl
#
#  This demo displays the available symbol types.
# ------------------------------------------------------------------------------

package require Tk
package require rbc
namespace import rbc::*

### The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl

### Set option defaults for $graph
option add *Element.ScaleSymbols true
option add *Axis.loose true
option add *Pixels .8c
option add *Element.lineWidth 0
option add *Legend.ActiveRelief raised
option add *Legend.padY 0
option add *Button*Font {Courier 14} widgetDefault
option add *Legend*Font {Courier 14 bold} widgetDefault
option add *Graph.Font {Courier 18 bold} widgetDefault
option add *Graph.title {Element Symbol Types}
option add *Graph.width 8i
option add *Graph.height 6i
option add *Graph.plotPadY .25i
option add *Graph.plotPadX .25i

### Define vectors x and y0 to y10; then create data.
vector create x -variable {}
x set {0.0 0.1 0.2 0.3 0.4 0.5 0.6 0.7 0.8 0.9 1.0}
for {set i 0} {$i<11} {incr i} {
    set vecName y$i
    vector create ${vecName}
    $vecName length 11
    $vecName variable y
    set y(:) [expr {$i*100.0}]
}

### Define graph and its elements:
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode toolbar -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode current -scaletoggle all -activelegend -zoomwheel -pan]
set Dem [file join $DemoDir bitmaps hobbes]
set attributes [string map [list @ @$Dem] {
    none     None     #264653 red4     y0
    arrow    Arrow    #287271 brown4   y10
    circle   Circle   #2a9d8f yellow4  y2
    cross    Cross    #8ab17d cyan4    y6
    diamond  Diamond  #babb74 green4   y3
    plus     Plus     #e9c46a magenta4 y9
    splus    Splus    #efb366 purple4  y7
    scross   Scross   #f4a261 red4     y8
    square   Square   #ee8959 orange4  y1
    triangle Triangle #e76f51 blue4    y4
    {{@.xbm} {@_mask.xbm}}
             Bitmap   yellow  black    y5
}]

set count 0
foreach {symbol label fill color yVec} $attributes {
    $graph graph element create line${count} -label $label -symbol $symbol -color $color -fill $fill -x x -y $yVec
    incr count
}
$graph graph element configure line0 -dashes {2 4 2} -linewidth 2

### Map everything, add Rbc_* commands.
grid $graph -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 1
