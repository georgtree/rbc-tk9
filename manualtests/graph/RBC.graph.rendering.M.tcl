# RBC.graph.rendering.M.tcl --
#
###Abstract
# Visual manual tests for graph rendering.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide graph.rendering

package require rbc
namespace import rbc::*

namespace eval graph.rendering {
    # Repeat with -renderer native and -renderer cairo. Stipple, filled markers and symbols
    # should retain their geometry. After: a smaller plot clips thick strokes cleanly.
    proc RBC.graph.rendering.M.1.1.Setup {} {
        graph .graph1 -width 720 -height 480 -plotbackground white
        pack .graph1 -fill both -expand yes
        .graph1 axis configure x y -min 0 -max 10
        .graph1 line create curve -data {0 1 2 8 4 2 6 9 8 3 10 7} -color blue -linewidth 3 -symbol diamond
        .graph1 marker create polygon -coords {-2 2 6 2 6 8 -2 8} -fill orange -stipple gray50 -outline red -linewidth 8
        .graph1 marker create arc -coords {6 3 12 9} -fill cyan -outline navy -linewidth 8
    }

    proc RBC.graph.rendering.M.1.1.Body {} {
        .graph1 configure -width 480 -height 330
        puts "Inspect resized plot with renderer=[.graph1 cget -renderer]"
    }

    proc RBC.graph.rendering.M.1.1.Cleanup {} {
        destroy .graph1
    }

    # A single-row legend uses each label width. After: a two-row legend uses cells;
    # labels must not overlap at either size.
    proc RBC.graph.rendering.M.1.2.Setup {} {
        graph .graph1 -width 720 -height 480 -plotbackground white
        pack .graph1 -fill both -expand yes
        .graph1 axis configure x y -min 0 -max 10
        foreach {name label y} {a A 2 b {A much longer legend label} 5 c Medium 8} {
            .graph1 line create $name -data [list 0 $y 10 $y] -label $label
        }
        .graph1 legend configure -position topmargin -rows 1
    }

    proc RBC.graph.rendering.M.1.2.Body {} {
        .graph1 legend configure -rows 2
    }

    proc RBC.graph.rendering.M.1.2.Cleanup {} {
        destroy .graph1
    }

}
