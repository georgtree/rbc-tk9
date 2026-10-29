source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "fill"

source RBC.eps.$CommandName.M.tcl
source ../EPSRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
