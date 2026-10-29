# RBC.trackgraph.export.M.tcl --
#
###Abstract
# Visual manual tests for trackgraph export.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide trackgraph.export

package require rbc
namespace import rbc::*

namespace eval trackgraph.export {
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

    # Scrolled, inverted tracks with local legends and grids. After: inspect SVG,
    # PDF and PostScript; only the visible viewport is exported. Screen must not jump.
    proc RBC.trackgraph.export.M.1.1.Setup {} {
        Create
        .trackgraph1 configure -tracklayout scroll -invertxy yes
        foreach track {voltage current} {
            .trackgraph1 track legend $track configure -hide no -position right
            .trackgraph1 track grid $track configure -hide no
        }
        .trackgraph1 track view moveto 0.2
        update
    }

    proc RBC.trackgraph.export.M.1.1.Body {} {
        foreach format {svg pdf postscript} {
            set channel [file tempfile path rbc-manual-track-]
            close $channel
            file delete $path
            append path .[expr {$format eq "postscript" ? "ps" : $format}]
            .trackgraph1 $format output $path -width 900 -height 600
            puts "Inspect track $format: $path"
        }
    }

    proc RBC.trackgraph.export.M.1.1.Cleanup {} {
        destroy .trackgraph1
    }

}
