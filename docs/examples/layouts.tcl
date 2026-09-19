# demonstration of different layouts of the graph
package require rbc
namespace import ::rbc::*

set graph [graph .g -width 600 -height 600]

$graph configure -plotpadx 20 -plotpady 20 -title {Title example} -plotborderwidth 5 -plotrelief sunken

$graph axis configure x -hide no -title {x axis}
$graph axis configure y -hide no -title {y axis}
$graph axis configure x2 -hide no -title {x2 axis}
$graph axis configure y2 -hide no  -title {y2 axis}

grid $graph -row 0 -column 0
set file [open layout.svg w+]
puts $file [$graph svg output -decorations yes]
close $file
