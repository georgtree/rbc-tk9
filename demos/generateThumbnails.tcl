# Run each demo in its own process. Metadata is passed as one Tcl argument.
namespace eval ::snapshot {
    variable demoDir [file dirname [file normalize [info script]]]
    variable defaults {width 260 height 150 exclude {}}
    variable metadata {
        {name graph1}
        {name graph2}
        {name graph3}
        {name graph4}
        {name graph5}
        {name graph8}
        {name polar1}
        {name polar2}
        {name barchart1}
        {name barchart2}
        {name barchart3}
        {name barchart4}
        {name barchart5}
        {name stripchart1}
        {name spline1}
        {name graph6}
        {name graph7}
        {name winop1 width 260 windows {.}}
        {name winop2 width 260 windows {.}}
        {name cairoGraph1 exclude {.native}}
        {name cairoBar1 exclude {.native}}
        {name mixed1}
        {name combined1 height 150}
    }
}

proc ::snapshot::main {} {
    variable demoDir
    variable defaults
    variable metadata
    set failed 0
    foreach entry $metadata {
        set settings [dict merge $defaults $entry]
        set name [dict get $settings name]
        puts "Generating $name.png"
        if {[catch {exec [info nameofexecutable] [file join $demoDir snapshotDemo.tcl] [file join $demoDir ${name}.tcl]\
                            [file join $demoDir thumbnails] $settings >@ stdout 2>@ stderr} message]} {
            puts stderr "Snapshot $name failed: $message"
            set failed 1
        }
    }
    return $failed
}

exit [::snapshot::main]
