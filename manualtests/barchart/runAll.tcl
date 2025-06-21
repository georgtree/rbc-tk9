set files {axisRunAll.tcl barRunAll.tcl configureRunAll.tcl crosshairsRunAll.tcl elementRunAll.tcl extentsRunAll.tcl\
                   gridRunAll.tcl insideRunAll.tcl insideRunAll.tcl invtransformRunAll.tcl legendRunAll.tcl\
                   markerRunAll.tcl penRunAll.tcl postscriptRunAll.tcl snapRunAll.tcl}
foreach file $files {
    source $file
}
