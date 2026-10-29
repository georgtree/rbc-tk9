source [file join [file dirname [info script]] ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
# Run from any working directory and restore it when finished.
set manualTestRoot [pwd]
set manualTestStart [::rbcManual::Counts]
set manualTestDirectory [pwd]
try {
    foreach component {graph barchart stripchart winop eps polar trackgraph graphtoolbar} {
        puts "\nCOMPONENT $component"
        cd [file join $manualTestRoot $component]
        if {[catch {source runAll.tcl} message]} {
            incr ::rbcManual::attempted
            incr ::rbcManual::failed
            puts stderr "ERROR loading $component suite: $message"
        }
    }
} finally {
    cd $manualTestDirectory
    ::rbcManual::Report all $manualTestStart
}
if {[lindex [::rbcManual::Counts] 1] > [lindex $manualTestStart 1]} {
    error "Manual suite had execution errors; see the labelled ERROR lines above."
}
}
