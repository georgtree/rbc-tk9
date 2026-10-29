source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "export"

source RBC.graph.$CommandName.M.tcl
source ../GraphRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
