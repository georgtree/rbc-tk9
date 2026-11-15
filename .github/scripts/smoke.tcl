# Fail early if dependencies or the optional renderer were silently omitted.
package require Tk 9.1
package require argparse
package require rbc::graphtoolbar
set graph [::rbc::graph .ciGraph -renderer cairo]
pack $graph
$graph element create line -xdata {0 1 2} -ydata {1 0 1}
update
if {[$graph cget -renderer] ne "cairo"} {error "Cairo renderer was not enabled"}
puts "Loaded Tcl [package provide Tcl], Tk [package provide Tk], RBC [package provide rbc], argparse [package provide argparse]"
destroy .
