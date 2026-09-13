package require rbc

set demoDir [file normalize [file dirname [info script]]]

set demos {graph1.tcl graph2.tcl}

foreach demo $demos {
    exec [info nameofexecutable] snapshotDemo.tcl [file join $demoDir $demo] [file join $demoDir thumbnails]
}
