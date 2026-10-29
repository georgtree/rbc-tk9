source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "smith"

source RBC.polar.$CommandName.M.tcl
source ../PolarRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
