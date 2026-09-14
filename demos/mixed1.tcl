package require Tk
package require rbc
package require math::statistics
package require math::constants
namespace import ::math::statistics::*
namespace import rbc::*

::math::constants::constants radtodeg degtorad pi

proc createIntervals {data numOfIntervals} {
    set intervals [::math::statistics::minmax-histogram-limits [tcl::mathfunc::min {*}$data]\
                           [tcl::mathfunc::max {*}$data] $numOfIntervals]
    lappend intervalsStrings [format <=%.3f [expr {[lindex $intervals 0]/1e6}]]
    for {set i 0} {$i<[llength $intervals]-1} {incr i} {
        lappend intervalsStrings [format {%.3f - %.3f} [expr {[lindex $intervals $i]/1e6}]\
                                          [expr {[lindex $intervals [expr {$i+1}]]/1e6}]]
    }
    return [dict create intervals $intervals intervalsStr $intervalsStrings]
}

proc createDist {data intervals} {
    set dist [::math::statistics::histogram $intervals $data]
    return [lrange $dist 0 end-1]
}

proc formatLabel {intervals w value} {
    # Determine the element name from value (an integer index).
    set index [expr {int($value)}]
    return [lindex $intervals $index]
}

# The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
set HeaderText [MakeLine {
    |This is an example of the graph/graphtoolbar widget with mixed line and bar elements, showing statistical
    |distribution and normal probability density function of 1000 samples generated in Monte-Carlo simulation in
    |examples from SpiceGenTcl project (examples/ngspice/advanced/monte_carlo.tcl).
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
- Change crosshairs mode: select from toolbar menu;}

### create the graph and configure its options
set graph [graphtoolbar .g -width 800 -height 500 -type graph -controlmode toolbar -zoom -zoomtitle -zoommark\
                   -crosshairs -crosshairsmode closest -scaletoggle y -activelegend -zoomwheel -pan]
$graph graph grid on
### add new axes and configure them
$graph graph axis create bandwidth -hide yes
$graph graph axis create pdf
$graph graph xaxis use {x bandwidth}
$graph graph yaxis use y
$graph graph y2axis use pdf
$graph graph legend configure -font {TkFixedFont 8}
$graph setAxisActiveScale pdf

### read data from file
set data [ReadCsv [file join $DemoDir data bandwidth.csv]]
set data [lmap record $data {lindex $record 1}]

### find PDF parameters and limits
set min [tcl::mathfunc::min {*}$data]
set max [tcl::mathfunc::max {*}$data]
set mean [::math::statistics::mean $data]
set stdev [::math::statistics::stdev $data]

### get distribution of bandwidths with normal parameters distribution
set numOfIntervals 15
set normIntervals [createIntervals $data $numOfIntervals]
set normDist [createDist $data [dict get $normIntervals intervals]]

### create and fill vectors
vector create bins dist bandwidth pdf
dist set $normDist
bins seq 0 $numOfIntervals
bandwidth seq $min $max {($max-$min)/100.0}
pdf expr {1/($stdev*sqrt(2*$pi))*exp(-0.5*((bandwidth-$mean)/$stdev)^2)} 

### configure axes according to data
$graph graph axis configure x -command [list formatLabel [dict get $normIntervals intervalsStr]] -majorticks $bins(:)\
        -rotate 90 -title {Frequency, MHz}
$graph graph axis configure y -title {Bandwidths per interval}
$graph graph axis configure pdf -title {Probability density function}

### add elements on the graph
$graph graph element create bandwidth1 -label PDF -x bandwidth -y pdf -mapx bandwidth -mapy pdf -symbol {} -linewidth 2
$graph graph bar create bandwidth -label {1000 samples} -x bins -y dist -foreground skyblue

### map everything
grid .header -columnspan 2 -sticky ew
grid .details -sticky ew -padx 15
grid $graph -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 0
grid rowconfigure . 1 -weight 0
grid rowconfigure . 2 -weight 1
