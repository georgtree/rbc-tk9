package require Tk
package require rbc

proc findGraphs {parent} {
    set result {}
    foreach win [winfo children $parent] {
        if {![catch {$win element names}] && ![catch {$win axis names}]} {
            lappend result $win
        }
        lappend result {*}[findGraphs $win]
    }
    return $result
}

proc saveSnapshots {demo outputDir} {
    file mkdir $outputDir
    set index 0
    foreach graph [findGraphs .] {
        set photo [image create photo]
        try {
            $graph snap $photo
            $photo write [file join $outputDir [file tail [file rootname $demo]].png] -format png
            incr index
        } finally {
            image delete $photo
        }
    }
    if {$index==0} {
        return -code error "No RBC graph widgets found"
    }
}

if {[llength $argv] != 2} {
    return -code error "usage: snapshotDemo.tcl demo.tcl output-directory"
}

set demo [file normalize [lindex $argv 0]]
set outputDir [file normalize [lindex $argv 1]]

# Let demos resolve their relative resources normally.
cd [file dirname $demo]

# Avoid exposing the wrapper's arguments to the demo.
set argv {}
set argc 0
set argv0 $demo

source $demo

# Give initial layout and scheduled drawing time to complete.
if {[file tail $demo] eq {stripchart1.tcl}} {
    set time 2000
} else {
    set time 500
}
after $time {
    if {[catch {saveSnapshots $demo $outputDir} message options]} {
        puts stderr [dict get $options -errorinfo]
        exit 1
    }
    exit 0
}
vwait ::snapshotFinished
