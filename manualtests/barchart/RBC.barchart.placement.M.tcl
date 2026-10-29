# RBC.barchart.placement.M.tcl --
#
###Abstract
# Visual manual tests for barchart placement.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide barchart.placement

package require rbc
namespace import rbc::*

namespace eval barchart.placement {
    # Mixed left/right alignment in aligned groups. Error bars follow the actual
    # bar centres. After: offset one element; its explicit low/high errors move too.
    proc RBC.barchart.placement.M.1.1.Setup {} {
        barchart .barchart1 -width 720 -height 480 -plotbackground white
        pack .barchart1 -fill both -expand yes
        .barchart1 axis configure x -min 0 -max 8
        .barchart1 axis configure y -min -4 -max 8
        .barchart1 element create a -data {2 3 4 -2 6 5} -foreground lightblue -barwidth 1 -yerror {0.7 0.5 0.8}
        .barchart1 element create b -data {2 4 4 -1 6 3} -foreground orange -barwidth 1     -ylow {3 -2 2} -yhigh {5 0 4}
        .barchart1 element configure a b -errorbarposition bar
        .barchart1 configure -barmode aligned
        .barchart1 element configure a -baralign left
        .barchart1 element configure b -baralign right
    }

    proc RBC.barchart.placement.M.1.1.Body {} {
        .barchart1 element configure b -baroffset 0.5
    }

    proc RBC.barchart.placement.M.1.1.Cleanup {} {
        destroy .barchart1
    }

    # Overlap with translucent fills and mixed alignment. After: stacked mode;
    # inspect positive and negative bars, outlines and error caps.
    proc RBC.barchart.placement.M.1.2.Setup {} {
        barchart .barchart1 -width 720 -height 480 -plotbackground white
        pack .barchart1 -fill both -expand yes
        .barchart1 axis configure x -min 0 -max 8
        .barchart1 axis configure y -min -4 -max 8
        .barchart1 element create a -data {2 3 4 -2 6 5} -foreground lightblue -barwidth 1 -yerror {0.7 0.5 0.8}
        .barchart1 element create b -data {2 4 4 -1 6 3} -foreground orange -barwidth 1     -ylow {3 -2 2} -yhigh {5 0 4}
        .barchart1 element configure a b -errorbarposition bar
        .barchart1 configure -barmode overlap
        .barchart1 element configure a -baralign left -fillopacity 0.5
        .barchart1 element configure b -baralign right -fillopacity 0.5
    }

    proc RBC.barchart.placement.M.1.2.Body {} {
        .barchart1 configure -barmode stacked
    }

    proc RBC.barchart.placement.M.1.2.Cleanup {} {
        destroy .barchart1
    }

    # Inverted bars: symmetric and explicit errors have visible perpendicular caps.
    # After: legacy data positioning moves errors to source coordinates.
    proc RBC.barchart.placement.M.1.3.Setup {} {
        barchart .barchart1 -width 720 -height 480 -plotbackground white
        pack .barchart1 -fill both -expand yes
        .barchart1 axis configure x -min 0 -max 8
        .barchart1 axis configure y -min -4 -max 8
        .barchart1 element create a -data {2 3 4 -2 6 5} -foreground lightblue -barwidth 1 -yerror {0.7 0.5 0.8}
        .barchart1 element create b -data {2 4 4 -1 6 3} -foreground orange -barwidth 1     -ylow {3 -2 2} -yhigh {5 0 4}
        .barchart1 element configure a b -errorbarposition bar
        .barchart1 configure -barmode aligned -invertxy yes
    }

    proc RBC.barchart.placement.M.1.3.Body {} {
        .barchart1 element configure a b -errorbarposition data
    }

    proc RBC.barchart.placement.M.1.3.Cleanup {} {
        destroy .barchart1
    }

}
