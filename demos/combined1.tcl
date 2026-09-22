package require Tk
package require rbc
package require math::statistics
package require math::constants
namespace import ::math::statistics::*
namespace import rbc::*

proc createIntervals {data numOfIntervals} {
    return [::math::statistics::minmax-histogram-limits [tcl::mathfunc::min {*}$data]\
                           [tcl::mathfunc::max {*}$data] $numOfIntervals]
}

proc createDist {data intervals} {
    set dist [::math::statistics::histogram $intervals $data]
    lset dist 1 [expr {[lindex $dist 1]+[lindex $dist 0]}]
    lset dist end-1 [expr {[lindex $dist end-1]+[lindex $dist end]}]
    return [lrange $dist 1 end-1]
}

proc syncAxes {graph axes masterAxis slaveGraph slaveAxis} {
    # Apply changes in the master axis to the slave axis in slave graph.
    # graph - graph name of axes which limits are changed
    # axes - list of axes changed
    # masterAxis - name of the axis from graph that governs the change in slave axis
    # slaveGraph - name of the graph widget containing slave axis
    # slaveAxis - name of the slave axis following changes in master axis
    foreach axis $axes {
        if {$axis ni [$graph axis names]} {
            continue
        }
        # if the source changes from [a,b] to [c,d], and target is [v,u], (c-a)/(b-a)*(u-v)+v and (d-b)/(b-a)*(u-v)+u
        if {$axis eq $masterAxis} {
            lassign [$graph axis limits $masterAxis] newMin newMax
            lassign [dict get $::axesStates $graph $masterAxis] oldMin oldMax
            lassign [$slaveGraph axis limits $slaveAxis] slaveMin slaveMax
            set span [expr {$slaveMax-$slaveMin}]
            $slaveGraph axis configure $slaveAxis -min {($newMin-$oldMin)/($oldMax-$oldMin)*$span+$slaveMin}\
                    -max {($newMax-$oldMax)/($oldMax-$oldMin)*$span+$slaveMax}
            dict set ::axesStates $graph $masterAxis [list $newMin $newMax]
        }
    }
}

# The script can be run from any location. It loads the files it needs from the demo directory.
set DemoDir [file normalize [file dirname [info script]]]

### Load common commands and create non-rbc GUI elements.
source $DemoDir/scripts/common.tcl
set HeaderText [MakeLine {
    |This is an example of a few graph/graphtoolbar widgets combined together, showing scatter plot together
    |with statistical distribution together with over X and Y axes. Scatter plot can be zoomed/panned and the 
    |statistical distribution plots follow these changes.
}]
CommonHeader .header $HeaderText 6 $DemoDir
ExpandableText .details 800 {Availible actions for scatter plot} {
- Zoom box selection: left mouse button press + motion + button release;
- Reverse zoom/pan to the previous state:  middle mouse button click;
- Zoom with mouse wheel: press and hold Ctrl + wheel scroll;
- Selected axis zoom: put mouse pointer over axis + press and hold Ctrl + wheel scroll;
- Panning: press and hold Shift + left mouse button press and hold + motion;
- Change crosshairs mode: select from toolbar menu;}

### create the graph and configure its options
. configure -background white
set scatterGraph [graphtoolbar .gScatter -width 500 -height 500 -type graph -controlmode context -zoom -crosshairs\
                          -crosshairsmode closest -crosshairsclosestopts {-interpolate no} -pan -zoomwheel]
$scatterGraph graph legend configure -hide yes 
$scatterGraph graph configure -plotpadx 0 -plotpady 0 -background white
$scatterGraph graph grid on
$scatterGraph graph configure -bottommargin 50 -leftmargin 50 -topmargin 1 -rightmargin 1

set topHistGraph [graphtoolbar .gTopHist -width 500 -height 200 -type barchart -controlmode context -crosshairs\
                          -crosshairsmode closest]
$topHistGraph graph legend configure -hide yes
$topHistGraph graph configure -barwidth 1 -plotpadx 0 -plotpady 1 -background white
$topHistGraph graph axis configure x -hide yes
$topHistGraph graph configure -rightmargin 1 -leftmargin 50 -bottommargin 1

set rightHistGraph [graphtoolbar .gRightHist -width 200 -height 500 -type barchart -controlmode context -crosshairs\
                            -crosshairsmode closest]
$rightHistGraph graph legend configure -hide yes
$rightHistGraph graph configure -barwidth 1 -plotpadx 0 -plotpady 0 -background white
$rightHistGraph graph axis configure x -hide yes 
$rightHistGraph graph configure -invertxy yes -leftmargin 1 -topmargin 1 -bottommargin 50 

### create vectors and generate sets of two normal distributed samples
vector create x y
expr {srand(10)}
x set [::math::statistics::random-normal 0 3 2000]
y set [::math::statistics::random-normal 0 3 2000]

### calculate intervals
set numOfIntervals 20
vector create xDistVec yDistVec bins
set xIntervals [createIntervals $x(:) $numOfIntervals]
xDistVec set [createDist $x(:) $xIntervals]
set yIntervals [createIntervals $y(:) $numOfIntervals]
yDistVec set [createDist $y(:) $xIntervals]
bins seq 0 {$numOfIntervals-2}

### configure axes according to calculated intervals
foreach limitX [lrange $xIntervals 0 end] limitY [lrange $yIntervals 0 end] {
    lappend majorticksX [format %.4f $limitX]
    lappend majorticksY [format %.4f $limitY]
}
$scatterGraph graph axis configure x -majorticks $majorticksX -rotate 90
$scatterGraph graph axis configure y -majorticks $majorticksY
$topHistGraph graph axis configure y -max {$xDistVec(max)+10}
$rightHistGraph graph axis configure y -max {$yDistVec(max)+10}

### create elements
$scatterGraph graph element create scatter -x x -y y -symbol circle -pixels 2 -linewidth 0 
$topHistGraph graph element create xHist -x bins -y xDistVec -borderwidth 0 -foreground skyblue
$rightHistGraph graph element create yHist -x bins -y yDistVec -borderwidth 0 -foreground skyblue

### add bindings for axes synchronization
dict set ::axesStates [$scatterGraph subwidget graph] x [$scatterGraph graph axis limits x]
dict set ::axesStates [$scatterGraph subwidget graph] y [$scatterGraph graph axis limits y]
bind [$scatterGraph subwidget graph] <<RbcAxisChanged>> [list syncAxes %W %d x [$topHistGraph subwidget graph] x]
bind [$scatterGraph subwidget graph] <<RbcAxisChanged>> +[list syncAxes %W %d y [$rightHistGraph subwidget graph] x]

### map everything
grid .header -row 0 -columnspan 2 -sticky ew
grid .details -row 1 -columnspan 2 -sticky ew
grid $scatterGraph -row 3 -column 0 -sticky nsew
grid $topHistGraph -row 2 -column 0 -sticky nsew
grid $rightHistGraph -row 3 -column 1 -sticky nsew
grid columnconfigure . 0 -weight 1
grid columnconfigure . 1 -weight 1
grid rowconfigure . 0 -weight 0
grid rowconfigure . 1 -weight 0
grid rowconfigure . 2 -weight 1
grid rowconfigure . 3 -weight 1
