source [file join [file dirname [info script]] ManualRunAllSupportMethods.tcl]

proc uniqkey { } {
	set key   [ expr { pow(2,31) + [ clock clicks ] } ]
    set key   [ string range $key end-8 end-3 ]
    set key   [ clock seconds ]$key
    return $key
}

proc sleep { ms } {
    set uniq [ uniqkey ]
    set ::__sleep__tmp__$uniq 0
    after $ms set ::__sleep__tmp__$uniq 1
    vwait ::__sleep__tmp__$uniq
    unset ::__sleep__tmp__$uniq
}

proc LoadSources {CommandList CommandName} {
	foreach command $CommandList {
		source "RBC.barchart.$CommandName.$command.M.tcl"
	}
}

proc ProcNameGenCommand {commandName command major minor} {
	return "barchart.$commandName\::RBC.barchart.$commandName.$command.M.$major.$minor"
}

proc ProcNameGenNoCommand {commandName major minor} {
	return "barchart.$commandName\::RBC.barchart.$commandName.M.$major.$minor"
}

proc ExecuteCommandSequenceCommand {CommandList CommandName} {
    set before [::rbcManual::Counts]
	foreach command $CommandList {
		set minor 1
		set major 1
		set delay [expr {[info exists ::manualTestDelay] ? $::manualTestDelay : 200}]
		while {1 == 1} {
			if {[info commands [ProcNameGenCommand $CommandName $command $major $minor].Setup] != ""} {
				::rbcManual::RunCase [ProcNameGenCommand $CommandName $command $major $minor] $delay
				incr minor
			} else {
				incr minor
				if {[info commands [ProcNameGenCommand $CommandName $command $major $minor].Setup] == ""} {
					incr major
					set minor 1
					if {[info commands [ProcNameGenCommand $CommandName $command $major $minor].Setup] == ""} {
						break;
					}
				}
			}
		}
	}

    ::rbcManual::Report barchart.$CommandName $before
}

proc ExecuteCommandSequenceNoCommand {CommandName} {
    set before [::rbcManual::Counts]
	set minor 1
	set major 1
	set delay [expr {[info exists ::manualTestDelay] ? $::manualTestDelay : 200}]
	while {1 == 1} {
		if {[info commands [ProcNameGenNoCommand $CommandName $major $minor].Setup] != ""} {
			::rbcManual::RunCase [ProcNameGenNoCommand $CommandName $major $minor] $delay
			incr minor
		} else {
			incr minor
			if {[info commands [ProcNameGenNoCommand $CommandName $major $minor].Setup] == ""} {
				incr major
				set minor 1
				if {[info commands [ProcNameGenNoCommand $CommandName $major $minor].Setup] == ""} {
					break;
				}
			}
		}
	}
    ::rbcManual::Report barchart.$CommandName $before
}
