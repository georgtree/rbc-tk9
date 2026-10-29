source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "readjpeg"

source RBC.winop.image.$CommandName.M.tcl
source ../WinopRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
