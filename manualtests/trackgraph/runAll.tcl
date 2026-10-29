source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set files {layoutRunAll.tcl componentsRunAll.tcl barRunAll.tcl}

foreach file $files {
    source $file
}

source exportRunAll.tcl
}
