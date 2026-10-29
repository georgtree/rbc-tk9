source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set files {axisRunAll.tcl barRunAll.tcl configureRunAll.tcl crosshairsRunAll.tcl elementRunAll.tcl extentsRunAll.tcl gridRunAll.tcl insideRunAll.tcl invtransformRunAll.tcl legendRunAll.tcl markerRunAll.tcl penRunAll.tcl postscriptRunAll.tcl snapRunAll.tcl}
foreach file $files {
    source $file
}

# Visual coverage for newer features.
foreach file {placementRunAll.tcl} {
    source $file
}
}
