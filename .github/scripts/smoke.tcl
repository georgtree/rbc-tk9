# Fail early if dependencies or the optional renderer were silently omitted.
package require Tk 9.1
package require argparse
package require rbc::graphtoolbar
set variant static-cairo
if {[info exists ::env(RBC_CI_RENDERER)]} {
    set variant $::env(RBC_CI_RENDERER)
}
switch -- $variant {
    static-cairo {set renderer cairo}
    native {set renderer native}
    default {error "Unknown renderer variant: $variant"}
}
set graph [::rbc::graph .ciGraph -renderer $renderer]
pack $graph
$graph element create line -xdata {0 1 2} -ydata {1 0 1}
update
if {[$graph cget -renderer] ne $renderer} {error "$renderer renderer was not enabled"}
if {$variant eq "native"} {
    if {![catch {$graph configure -renderer cairo} message] ||
        $message ne "Cairo renderer is not available in this build"} {
        error "Native-only build did not reject Cairo: $message"
    }
}
puts "Loaded Tcl [package provide Tcl], Tk [package provide Tk], RBC [package provide rbc], argparse [package provide argparse]"
destroy .
