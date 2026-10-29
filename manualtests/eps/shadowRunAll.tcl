source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "shadow"

source RBC.eps.$CommandName.M.tcl
source ../EPSRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
