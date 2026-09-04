# ------------------------------------------------------------------------------
#  RBC Demo barchart1.tcl
#
#  A conventional barchart with stipple patterns and error bars.
# ------------------------------------------------------------------------------

package require Tk
package require rbc
namespace import rbc::*


### The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Set option defaults for the barchart.
option add *Barchart.title {A Simple Barchart}
option add *Barchart.font {Helvetica 12 bold}
option add *Axis.tickFont {TkFixedFont 10}
option add *Axis.titleFont {Helvetica 12 bold}
option add *x.Rotate 90
option add *x.Command FormatLabel
option add *x.Title {Stipple type}
option add *y.Title {Y Axis Label}
option add *Element.Background white
option add *Element.Relief solid
option add *Element.BorderWidth 1
option add *Grid.dashes {2 4}
option add *Grid.hide no
option add *Grid.mapX {}
option add *Legend.hide no
option add *Legend.Font {TkFixedFont 10}

proc FormatLabel {w value} {
    # Determine the element name from value (an integer index).
    set names [$w element show]
    set index [expr round($value)]
    if {$index!=$value} {
        return $value 
    }
    global elemLabels
    if {[info exists elemLabels($index)]} {
        # In the present example, this text label is returned.
        return $elemLabels($index)
    }
    return $value
}

### Create the barchart.
set barchart [graphtoolbar .bc -width 1200 -height 600 -type barchart -zoom -zoomtitle -zoommark -crosshairs\
                      -crosshairsmode closest -scaletoggle y -activelegend -zoomwheel]

### Add a bar to .bc for each bitmap in the list.
proc random {{max 1.0} {min 0.0}} {
    global randomSeed
    set randomSeed [expr {(7141*$randomSeed+54773)%259200}]
    set num [expr {$randomSeed/259200.0*($max-$min)+$min}]
    return $num
}
set randomSeed 148230
set bitmaps {bdiagonal1 bdiagonal2 checker2 checker3 cross1 cross2 cross3 crossdiag dot1 dot2 dot3 dot4 fdiagonal1\
                      fdiagonal2 hline1 hline2 lbottom ltop rbottom rtop vline1 vline2}
set count 1
foreach stipple $bitmaps {
    set label [file tail $stipple]
    set label [file root $label] 
    set y [random -2 10]
    set yhigh [expr {$y+0.5}]
    set ylow [expr {$y-0.5}]
    $barchart graph element create $label -y $y -x $count -fg brown -bg orange -stipple\
            @$DemoDir/stipples/${stipple}.xbm -yhigh $yhigh -ylow $ylow
    set elemLabels($count) $label
    incr count
}

### Map everything
grid $barchart -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 1
