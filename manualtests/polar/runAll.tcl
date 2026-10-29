source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set files {representationRunAll.tcl smoothingRunAll.tcl smithRunAll.tcl layoutRunAll.tcl}

foreach file $files {
    source $file
}

source areafillRunAll.tcl
}
