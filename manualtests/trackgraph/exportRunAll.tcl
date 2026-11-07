source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "export"

source trackgraph.$CommandName.tcl
source ../TrackgraphRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
