set files {representationRunAll.tcl smoothingRunAll.tcl smithRunAll.tcl layoutRunAll.tcl}
package require Tk

foreach file $files {
    source $file
}
