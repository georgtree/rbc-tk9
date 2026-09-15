package require Tk
package require rbc
namespace import rbc::*


proc ElementNameLabel {elementMapping graph element index x y} {
    set record [dict filter $elementMapping script {key value} {
        set density [dict get $value density_g_cm3]
        set nucleons [dict get $value nucleons]
        expr {($x==$nucleons) && ($y==$density)}
    }]
    return [dict get {*}[dict values $record] isotope]
}
proc ElementInfo {elementMapping element x y info} {
    set record [dict filter $elementMapping script {key value} {
        set density [dict get $value density_g_cm3]
        set nucleons [dict get $value nucleons]
        expr {($x==$nucleons) && ($y==$density)}
    }]
    set value {*}[dict values $record]
    set elementName [dict keys $record]
    dict with value {}
    return [format "Element: %s\nIsotope: %s\nProtons: %d\nNeutrons:\
                            %d\nDensity, g/cm^3: %.2f\nIsotope selection basis: %s"\
                    $elementName $isotope $protons $neutrons $density_g_cm3 $selection_basis]
}

### The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands
source $DemoDir/scripts/common.tcl
set HeaderText [MakeLine {
    |This is an example of using custom closest point marker from graphtoolbar and custom labels for symbol values.
    |As a dataset the information about periodic elements used, where the most naturally abdundant isotope or
    |isotope with longest half-life selected. On the X axis the total number of nucleons plotted, Y axis mark
    |the density of isotope in g/cm^3. Color of element corresponds to the value of melting point in Celsius degrees 
    |in certain range.
}]

CommonHeader .header $HeaderText 6 $DemoDir
ExpandableText .details 800 {Availible actions} {
- Zoom box selection: left mouse button press + motion + button release;
- Reverse zoom/pan to the previous state:  middle mouse button click;
- Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
- Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
- Panning: press and hold Shift + left mouse button press and hold + motion;
- Toggle axive axis scale: left mouse button click over the selected axis;
- Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
- Change crosshairs mode: select from availible mods on toolbar;}

### Create and configure the graph
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode toolbar -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode closest -crosshairsclosestopts {-interpolate no} -scaletoggle y\
                   -activelegend -zoomwheel -pan]
$graph graph grid on
$graph graph legend configure -hide yes
$graph graph axis configure y -title {density, g/cm^3} -min -1 -max 25
$graph graph axis configure x -title {Nucleons} -min -10 -max 250

### Load data
set data [ReadCsv [file join $DemoDir data periodic_elements.csv]]
vector create nucleonsVec densityVec meltingPointVec
foreach record $data {
    set nucleons [expr {[dict get $record protons]+[dict get $record neutrons]}]
    set density [dict get $record density_g_cm3]
    if {$density eq {}} {
        continue
    }
    nucleonsVec append $nucleons
    densityVec append $density
    meltingPointVec append [dict get $record melting_point_C]
    dict append record nucleons $nucleons
    dict append elementMapping [dict get $record element] [dict remove $record element]
}

# create and configure graph pens and styles
set min [vector expr {min(meltingPointVec)}]
set max [vector expr {max(meltingPointVec)}]
set tempRangesNum 10
set steps [lseq $min $max [expr {($max-$min)/double($tempRangesNum)}] ]
set i 0
for {set i 1} {$i <= 10} {incr i 1} {
    # generate colors for each temperature range
    set color [format "#%02X%02X%02X" {*}[lmap a {0 0 139} b {255 0 0}\
                                                  {expr {round($a + ($b-$a)*($i-1.0)/max(1,$tempRangesNum-1))}}]]
    set pen pen$i
    $graph graph pen create $pen -color $color -symbol circle -pixels 10 -valueoffset {0 -5}\
            -valuecommand [list ::ElementNameLabel $elementMapping] -showvalues y -outline black
    lassign [lrange $steps [expr {$i-1}] $i] min max
    lappend styles [list $pen $min $max]
}

# create graph element and configure it
$graph configure -closestcommand [list ElementInfo $elementMapping] -coordclosestmark custom
$graph graph element create periodic-elements -x nucleonsVec -y densityVec -w meltingPointVec\
        -scalesymbols no -valuefont {Arial 7} -pixels 10 -valuecommand [list ::ElementNameLabel $elementMapping]\
        -showvalues y -linewidth 0 -styles $styles -color black -valueoffset {0 -5} -outline black

# map everything
grid .header -columnspan 1 -sticky ew
grid .details -sticky ew -padx 15
grid $graph -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 0
grid rowconfigure . 1 -weight 0
grid rowconfigure . 2 -weight 1
