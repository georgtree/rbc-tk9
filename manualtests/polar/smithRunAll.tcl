source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "smith"

source polar.$CommandName.tcl
source ../PolarRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
