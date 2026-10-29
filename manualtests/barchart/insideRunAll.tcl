source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "inside"

source RBC.barchart.$CommandName.M.tcl
source ../BarchartRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
