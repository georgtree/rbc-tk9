# RBC.graph.arc.M.tcl --
#
###Abstract
# Visual manual tests for graph arc.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide graph.arc

package require rbc
namespace import rbc::*

namespace eval graph.arc {
    # Ellipse, sector, chord and open arc: only the open arc ignores its fill.
    # After: negative sweeps reverse direction; ellipse remains closed.
    proc RBC.graph.arc.M.1.1.Setup {} {
        graph .graph1 -width 720 -height 480 -plotbackground white
        pack .graph1 -fill both -expand yes
        .graph1 axis configure x y -min 0 -max 10
        foreach {name coords style extent} {
            ellipse {1 6 4 9} chord 360
            sector {6 6 9 9} pieslice 120
            segment {1 1 4 4} chord 120
            open {6 1 9 4} arc 120
        } {
            .graph1 marker create arc -name $name -coords $coords -style $style -start 30 -extent $extent         -fill lightblue -outline navy -linewidth 3
        }
    }

    proc RBC.graph.arc.M.1.1.Body {} {
        foreach name {sector segment open} {
            .graph1 marker configure $name -extent -120
        }
    }

    proc RBC.graph.arc.M.1.1.Cleanup {} {
        destroy .graph1
    }

    # Arrows should meet their shafts at diagonal angles without a sideways kink.
    # After: resize and invert; arrowheads and arc outlines remain aligned.
    proc RBC.graph.arc.M.1.2.Setup {} {
        graph .graph1 -width 720 -height 480 -plotbackground white
        pack .graph1 -fill both -expand yes
        .graph1 axis configure x y -min 0 -max 10
        .graph1 marker create line -coords {1 1 9 9} -arrow both -linewidth 3 -outline red
        .graph1 marker create line -coords {1 9 9 1} -arrow both -linewidth 3 -outline blue
        .graph1 marker create arc -coords {2 2 8 8} -style pieslice -extent 240 -fill {} -outline black
    }

    proc RBC.graph.arc.M.1.2.Body {} {
        .graph1 configure -invertxy yes -width 480 -height 650
    }

    proc RBC.graph.arc.M.1.2.Cleanup {} {
        destroy .graph1
    }

}
