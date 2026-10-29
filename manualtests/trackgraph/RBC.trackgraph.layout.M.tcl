# RBC.trackgraph.layout.M.tcl --
#
###Abstract
# Visual manual tests for trackgraph layout.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide trackgraph.layout

package require rbc
namespace import rbc::*

namespace eval trackgraph.layout {
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

    # Tracks share aligned horizontal coordinates. Drag the divider to change
    # weights. After: invert and reorder; dividers and axes follow their tracks.
    proc RBC.trackgraph.layout.M.1.1.Setup {} {
        Create
    }

    proc RBC.trackgraph.layout.M.1.1.Body {} {
        .trackgraph1 configure -invertxy yes
        .trackgraph1 track order {current voltage}
    }

    proc RBC.trackgraph.layout.M.1.1.Cleanup {} {
        destroy .trackgraph1
    }

    # Scrollable tracks retain their data scale. After: scroll partially past the
    # first track; traces, grids and labels must stay in their visible track area.
    proc RBC.trackgraph.layout.M.1.2.Setup {} {
        Create
        .trackgraph1 configure -tracklayout scroll
        scrollbar .scroll -command {.trackgraph1 track view}
        .trackgraph1 configure -trackscrollcommand {.scroll set}
        pack .scroll -side right -fill y
    }

    proc RBC.trackgraph.layout.M.1.2.Body {} {
        .trackgraph1 track view moveto 0.35
    }

    proc RBC.trackgraph.layout.M.1.2.Cleanup {} {
        destroy .trackgraph1 .scroll
    }

    # Data scrolling changes only the voltage range. Threshold follows its data
    # value; infinite endpoints span the visible track, not the entire widget.
    proc RBC.trackgraph.layout.M.1.3.Setup {} {
        Create
        .trackgraph1 marker create line -name threshold -track voltage -mapy volts -coords {-Inf 0.5 Inf 0.5} -outline green
        scrollbar .scroll -command {.trackgraph1 track yview voltage -mapy volts}
        .trackgraph1 axis configure volts -scrollcommand {.scroll set}
        pack .scroll -side right -fill y
    }

    proc RBC.trackgraph.layout.M.1.3.Body {} {
        .trackgraph1 track yview voltage -mapy volts scroll 2 units
    }

    proc RBC.trackgraph.layout.M.1.3.Cleanup {} {
        destroy .trackgraph1 .scroll
    }

}
