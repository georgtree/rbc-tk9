source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "lower"

source RBC.winop.$CommandName.M.tcl
source ../WinopRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
