source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set files {controlsRunAll.tcl}

foreach file $files {
    source $file
}
}
