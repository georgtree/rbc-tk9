source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "marker"

proc commandList {} {
	list bind configure.bitmap configure.common configure.image configure.line configure.polygon configure.text configure.window create delete
}

source ../StripchartRunAllSupportMethods.tcl

LoadSources [commandList] $CommandName

ExecuteCommandSequenceCommand [commandList] $CommandName
}
