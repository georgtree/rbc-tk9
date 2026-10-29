source [file join [file dirname [info script]] ../ManualRunAllSupportMethods.tcl]

::rbcManual::RunRunner [info script] {
set CommandName "image"

proc commandList {} {
	list convolve gradient readjpeg resample rotate snap subsample
}

source ../WinopRunAllSupportMethods.tcl

LoadSources [commandList] $CommandName

ExecuteCommandSequenceCommand [commandList] $CommandName
}
