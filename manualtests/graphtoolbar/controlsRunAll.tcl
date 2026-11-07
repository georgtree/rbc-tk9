source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "controls"

source graphtoolbar.$CommandName.tcl
source ../GraphtoolbarRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
