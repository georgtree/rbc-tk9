set files {axisRunAll.tcl barRunAll.tcl configureRunAll.tcl configureRunAll.tcl crosshairsRunAll.tcl elementRunAll.tcl\
                   extentsRunAll.tcl gridRunAll.tcl insideRunAll.tcl invtransformRunAll.tcl legendRunAll.tcl\
                   markerRunAll.tcl penRunAll.tcl postscriptRunAll.tcl snapRunAll.tcl transformRunAll.tcl}
foreach file $files {
    source $file
}
