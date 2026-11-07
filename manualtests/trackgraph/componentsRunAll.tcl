source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "components"

source trackgraph.$CommandName.tcl
source ../TrackgraphRunAllSupportMethods.tcl

ExecuteCommandSequenceNoCommand $CommandName
}
