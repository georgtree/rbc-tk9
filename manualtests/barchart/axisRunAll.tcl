source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "axis"

proc commandList {} {
	list bind configure invtransform transform
}

source ../BarchartRunAllSupportMethods.tcl

LoadSources [commandList] $CommandName

ExecuteCommandSequenceCommand [commandList] $CommandName
}
