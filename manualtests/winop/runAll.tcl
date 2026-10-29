source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set files {convolveRunAll.tcl imageRunAll.tcl lowerRunAll.tcl mapRunAll.tcl moveRunAll.tcl quantizeRunAll.tcl\
               queryRunAll.tcl raiseRunAll.tcl readjpegRunAll.tcl resampleRunAll.tcl snapRunAll.tcl subsampleRunAll.tcl\
               unmapRunAll.tcl warptoRunAll.tcl}
foreach file $files {
    source $file
}
}
