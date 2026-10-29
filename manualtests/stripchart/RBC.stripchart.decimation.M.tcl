# RBC.stripchart.decimation.M.tcl --
#
###Abstract
# Visual manual tests for stripchart decimation.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide stripchart.decimation

package require rbc
namespace import rbc::*

namespace eval stripchart.decimation {
    # Dense strip trace retains a narrow spike. After: append a new peak and scroll
    # the data range; both peaks remain visible without stray joins.
    proc RBC.stripchart.decimation.M.1.1.Setup {} {
        vector create stripX stripY
        stripX seq 0 10 0.001
        stripY expr {stripX * 0}
        stripY index 9000 1
        stripchart .stripchart1 -width 720 -height 480
        pack .stripchart1 -fill both -expand yes
        .stripchart1 axis configure y -min -1.2 -max 1.2
        .stripchart1 element create trace -xdata stripX -ydata stripY -symbol none -color red -decimate auto
    }

    proc RBC.stripchart.decimation.M.1.1.Body {} {
        stripX append {10.001 10.002 10.003}
        stripY append {0 -1 0}
        .stripchart1 axis configure x -min 8 -max 11
    }

    proc RBC.stripchart.decimation.M.1.1.Cleanup {} {
        destroy .stripchart1
        vector destroy stripX stripY
    }

}
