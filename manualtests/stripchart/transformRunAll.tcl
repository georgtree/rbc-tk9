source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "transform"

source RBC.stripchart.$CommandName.M.tcl
source ../StripchartRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
