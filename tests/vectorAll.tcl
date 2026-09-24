# Run package-loading and public vector C API tests without initializing Tk.
package require tcltest 2
package require rbc::vector
if {[package provide Tk] ne {}} {
    error {vectorAll.tcl must start in a Tcl-only interpreter}
}
::tcltest::configure -testdir [file dirname [file normalize [info script]]]\
        -file {RBC.vector.load.A.test RBC.vector.capi.A.test RBC.vector.parentheses.A.test RBC.vector.namespace.A.test} -singleproc 1
::tcltest::configure {*}$argv
::tcltest::runAllTests
