source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "legend"

proc commandList {} {
	list activate bind configure deactivate get
}

source ../GraphRunAllSupportMethods.tcl

LoadSources [commandList] $CommandName

ExecuteCommandSequenceCommand [commandList] $CommandName
}
