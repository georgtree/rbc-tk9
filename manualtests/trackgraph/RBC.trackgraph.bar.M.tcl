# RBC.trackgraph.bar.M.tcl --
#
###Abstract
# Visual manual tests for trackgraph bar.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide trackgraph.bar

package require rbc
namespace import rbc::*

namespace eval trackgraph.bar {
    proc Create {} {
        trackgraph .trackgraph1 -width 720 -height 520 -trackresize yes         -trackseparatorcolor gray50 -trackseparatorwidth 2
        pack .trackgraph1 -fill both -expand yes
        .trackgraph1 axis configure x -min 0 -max 10 -title Time
        .trackgraph1 axis create volts -min -1 -max 1 -title Voltage -scrollmin -4 -scrollmax 4
        .trackgraph1 axis create amps -min -10 -max 10 -title Current -scrollmin -40 -scrollmax 40
        .trackgraph1 track create voltage -yaxes volts -weight 2 -height 320
        .trackgraph1 track create current -yaxes amps -height 240
        .trackgraph1 line create v -track voltage -mapy volts -data {0 -0.8 5 0.8 10 -0.8} -color red -symbol none
        .trackgraph1 strip create i -track current -mapy amps -data {0 8 5 -8 10 8} -color blue -symbol none
        update
    }

    # Lines, strips and raised bars coexist. Tall bars and borders clip to voltage;
    # current remains untouched. After: invert; error caps remain perpendicular.
    proc RBC.trackgraph.bar.M.1.1.Setup {} {
        Create
        .trackgraph1 track configure voltage -barmode aligned
        .trackgraph1 bar create a -track voltage -mapy volts -data {2 2 5 -2 8 0.7} -foreground magenta     -barwidth 1 -borderwidth 3 -relief raised -yerror {0.2 0.2 0.2} -errorbarposition bar
        .trackgraph1 bar create b -track voltage -mapy volts -data {2 0.7 5 -0.7 8 0.6} -foreground cyan -barwidth 1
    }

    proc RBC.trackgraph.bar.M.1.1.Body {} {
        .trackgraph1 configure -invertxy yes
    }

    proc RBC.trackgraph.bar.M.1.1.Cleanup {} {
        destroy .trackgraph1
    }

    # Independent per-track bar modes: aligned voltage and stacked current. After:
    # voltage becomes overlap, current remains stacked; legend symbols stay intact.
    proc RBC.trackgraph.bar.M.1.2.Setup {} {
        Create
        foreach {track axis} {voltage volts current amps} {
            foreach {name color value} {a orange 0.5 b cyan 0.3} {
                .trackgraph1 bar create $track$name -track $track -mapy $axis -data [list 2 $value 5 $value 8 $value]             -foreground $color -barwidth 1 -fillopacity 0.5
            }
        }
        .trackgraph1 track configure voltage -barmode aligned
        .trackgraph1 track configure current -barmode stacked
    }

    proc RBC.trackgraph.bar.M.1.2.Body {} {
        .trackgraph1 track configure voltage -barmode overlap
    }

    proc RBC.trackgraph.bar.M.1.2.Cleanup {} {
        destroy .trackgraph1
    }

}
