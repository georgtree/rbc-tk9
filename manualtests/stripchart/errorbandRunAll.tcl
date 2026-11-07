source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "errorband"

source stripchart.$CommandName.tcl
source ../StripchartRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
