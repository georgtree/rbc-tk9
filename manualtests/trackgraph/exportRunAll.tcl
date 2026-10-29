source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "export"

source RBC.trackgraph.$CommandName.M.tcl
source ../TrackgraphRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
