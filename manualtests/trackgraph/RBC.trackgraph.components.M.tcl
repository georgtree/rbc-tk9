# RBC.trackgraph.components.M.tcl --
#
###Abstract
# Visual manual tests for trackgraph components.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide trackgraph.components

package require rbc
namespace import rbc::*

namespace eval trackgraph.components {
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

    # Each local legend contains only its track elements; the global legend has
    # both. After: reorder, and inspect that legends move with their tracks.
    proc RBC.trackgraph.components.M.1.1.Setup {} {
        Create
        .trackgraph1 legend configure -position topmargin -rows 1
        foreach track {voltage current} {
            .trackgraph1 track legend $track configure -hide no -position right
        }
    }

    proc RBC.trackgraph.components.M.1.1.Body {} {
        .trackgraph1 track order {current voltage}
    }

    proc RBC.trackgraph.components.M.1.1.Cleanup {} {
        destroy .trackgraph1
    }

    # Different tracks select different X grids. After: hide global grid; both
    # explicit local grids remain visible and clipped to their track.
    proc RBC.trackgraph.components.M.1.2.Setup {} {
        Create
        .trackgraph1 axis create samples -min 0 -max 100 -stepsize 25
        .trackgraph1 x2axis use samples
        .trackgraph1 track grid voltage configure -hide no -mapx x -mapy volts -color lightblue
        .trackgraph1 track grid current configure -hide no -mapx samples -mapy amps -color orange
    }

    proc RBC.trackgraph.components.M.1.2.Body {} {
        .trackgraph1 grid configure -hide yes
    }

    proc RBC.trackgraph.components.M.1.2.Cleanup {} {
        destroy .trackgraph1
    }

    # Window and XOR markers belong to voltage. After: reorder and invert; neither
    # leaves stale pixels. Windows use ordinary graph visibility, not child clipping.
    proc RBC.trackgraph.components.M.1.3.Setup {} {
        Create
        button .trackgraph1.info -text Voltage
        .trackgraph1 marker create window -track voltage -mapy volts -coords {5 0} -window .trackgraph1.info
        .trackgraph1 marker create line -name selection -track voltage -mapy volts -coords {-2 -0.5 12 0.5} -xor yes -outline green -linewidth 3
    }

    proc RBC.trackgraph.components.M.1.3.Body {} {
        .trackgraph1 track order {current voltage}
        .trackgraph1 configure -invertxy yes
        .trackgraph1 marker configure selection -coords {-2 0.5 12 -0.5}
    }

    proc RBC.trackgraph.components.M.1.3.Cleanup {} {
        destroy .trackgraph1
    }

    # Move over track backgrounds: title reports the entered track. After: reverse
    # order; binding targets still identify the track under the pointer.
    proc RBC.trackgraph.components.M.1.4.Setup {} {
        Create
        .trackgraph1 track bind all <Enter> {%W configure -title "Track: [%W track get current]"}
    }

    proc RBC.trackgraph.components.M.1.4.Body {} {
        .trackgraph1 track order {current voltage}
    }

    proc RBC.trackgraph.components.M.1.4.Cleanup {} {
        destroy .trackgraph1
    }

}
