source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "bar"

source trackgraph.$CommandName.tcl
source ../TrackgraphRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
