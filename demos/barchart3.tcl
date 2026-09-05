# ------------------------------------------------------------------------------
#  RBC Demo barchart3.tcl
#
#  A conventional barchart with colors and stipple patterns; plotted against y.
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
set HeaderText {This is an example of the barchart widget.}
CommonHeader .header $HeaderText 5 $DemoDir .b barchart3.ps

### Create and configure barchart.
# Note that the plot is inverted (One to Eleven), and the bars are drawn from a baseline at 1.2, not zero.
proc FormatLabel {w value} {
    # Determine the element name from the value
    set displaylist [$w element show]
    set index [expr {round($value)-1}]
    set name [lindex $displaylist $index]
    if {$name eq {}} {
        return $name
    }
    # Return the element label
    set info [$w element configure $name -label]
    return [lindex $info 4]
}
set barchart [graphtoolbar .bc -width 800 -height 500 -type barchart -zoom -zoomtitle -zoommark -crosshairs\
                      -crosshairsmode closest -scaletoggle y -activelegend -zoomwheel]
$barchart graph configure -invert true -baseline 1.2
$barchart graph xaxis configure -command FormatLabel -descending true
$barchart graph legend configure -hide yes

### Define names, fgcolors, bgcolors, bitmaps - used to configure elements.
set visual [winfo screenvisual .]
set names {One Two Three Four Five Six Seven Eight}
if {($visual eq {staticgray}) || ($visual eq {grayscale})} {
    set fgcolors {white white white white white white white white}
    set bgcolors {black black black black black black black black}
} else {
    set fgcolors {red green blue purple orange brown cyan navy}
    set bgcolors {green blue purple orange brown cyan navy red}
}
set bitmaps {
    bdiagonal1 bdiagonal2 checker2 checker3 cross1 cross2 cross3 crossdiag
}

### Add elements to barchart.
# Use names, fgcolors, bgcolors, bitmaps to configure each element.
set numColors [llength $names]
for {set i 0} {$i<$numColors} {incr i} {
    $barchart graph element create [lindex $names $i] -data {$i+1 $i+1} -fg [lindex $fgcolors $i]\
            -bg [lindex $bgcolors $i] -stipple @$DemoDir/stipples/[lindex $bitmaps $i].xbm -relief raised -bd 2
}
$barchart graph element create Nine -data {9 -1.0} -fg red -relief sunken
$barchart graph element create Ten -data {10 2} -fg seagreen -stipple @$DemoDir/stipples/hobbes.xbm\
        -background palegreen
$barchart graph element create Eleven -data {11 3.3} -fg blue

### Map everything
grid .header -sticky ew -padx 15
grid $barchart -sticky news
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1
wm min . 0 0

