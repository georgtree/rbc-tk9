package require Tk
package require rbc
namespace import rbc::*

# The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
set HeaderText [MakeLine {
    |This is an example of the polar graph widget with Smith chart representation with impedance grid. As an example,
    |the S11 curve was taken of pass-band filter from SpiceGenTcl project (examples/ngspice/sp/filter.tcl)
}]
CommonHeader .header $HeaderText 6 $DemoDir
ExpandableText .details 600 {Availible actions} {
- Zoom box selection: left mouse button press + motion + button release;
- Reverse zoom/pan to the previous state:  middle mouse button click;
- Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
- Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
- Panning: press and hold Shift + left mouse button press and hold + motion;
- Highlight/hide certain plot: left mouse button click of legend, toggle between normal-active-hide state;
- Change crosshairs mode: right mouse button click, and select from four availible modes;}

### Create the graph and configure its options
set graph [graphtoolbar .g -width 700 -height 600 -type polar -representation smith -controlmode context -zoom\
                   -zoomtitle -zoommark -crosshairs -crosshairsmode closest -activelegend -zoomwheel -pan]
$graph graph grid on

### read data from file
set data [ReadCsv [file join $DemoDir data filter_s11.csv]]
foreach record $data {
    lappend frequency [lindex [dict get $record frequency] 0]
    lappend s11 [dict get $record s11]
}

# coordinates provided in form of the complex vector
vector create s11Vec -type complex
vector create frequencyVec
s11Vec set $s11
frequencyVec set $frequency
set rangeIndexes [frequencyVec search 70e6 130e6]
set startIndex [lindex $rangeIndexes 0]
set endIndex [lindex $rangeIndexes end]
frequencyVec set [frequencyVec range $startIndex $endIndex]
s11Vec set [s11Vec range $startIndex $endIndex]

$graph graph element create s11 -cdata s11Vec -symbol {}  -linewidth 2 -param frequencyVec


### Map everything
grid .header -columnspan 1 -sticky ew
grid .details -sticky ew -padx 15
grid $graph -sticky news
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 0
grid rowconfigure . 1 -weight 0
grid rowconfigure . 2 -weight 1
