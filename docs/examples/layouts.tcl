# demonstration of different layouts of the graph
package require rbc
namespace import ::rbc::*

set graph [barchart .b -width 600 -height 300]


.b configure -baseline 0
.b element create values -x {1 2 3} -y {4 7 2} 
.b element configure values -yerror {0.5 0.8 0.4 1.0} -errorbarcap 10 -errorbarcolor black -fillopacity 0.5
#.b element configure values -y {-4 7 -2}
.b element configure values -barwidth 0.5
grid $graph -row 0 -column 0

$graph svg output barchart_errorbars.svg -decorations yes

