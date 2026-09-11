# all.tcl --
#
# This file contains a top-level script to run all of the Tcl
# tests.  Execute it by invoking "make test" 
#

package require tcltest 2

package require Tk ;# This is for a Tk Widget; fail early if no Tk!

# Get RBC
package require rbc

# Legacy pixel tests use native rendering; explicit renderer options still win.
# Manual tests and demos do not source this runner and retain the build default.
foreach class {Graph Barchart Stripchart Polar} {
    option add *${class}.renderer native interactive
}
tcltest::testConstraint rbcNativeSuiteDefault 1

tcltest::configure {*}$argv
tcltest::configure -testdir [file normalize [file dirname [info script]]]

tcltest::configure -singleproc 1
tcltest::runAllTests

