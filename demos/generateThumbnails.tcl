package require rbc

set demoDir [file normalize [file dirname [info script]]]

set demos {graph1 graph2 graph3 graph4 graph5 graph8 polar1 barchart1 barchart2 barchart3 barchart4 barchart5\
                          stripchart1 spline1 graph6 graph7 cairoGraph1 cairoBar1}

foreach demo $demos {
    puts $demo
    exec [info nameofexecutable] [file join $demoDir snapshotDemo.tcl] [file join $demoDir $demo.tcl]\
            [file join $demoDir thumbnails]
}
