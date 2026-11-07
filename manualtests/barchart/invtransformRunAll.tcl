source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "invtransform"

source barchart.$CommandName.tcl
source ../BarchartRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
