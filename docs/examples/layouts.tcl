# demonstration of different layouts of the graph
package require rbc
namespace import ::rbc::*

set graph [graph .g -width 600 -height 300]


.g element create horizontal -data {2 0 3 1 2.5 2} -xerror {0.2 0.4 0.3} -errorband x -errorbandopacity 0.25\
        -errorbandforeground steelblue 
.g element configure horizontal -xerror {} -xlow {1.7 2.5 2.1} -xhigh {2.4 3.2 3.0}
grid $graph -row 0 -column 0

$graph svg output line_x_errorband_asym.svg -decorations yes

