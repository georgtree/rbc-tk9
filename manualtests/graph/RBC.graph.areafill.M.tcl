# RBC.graph.areafill.M.tcl --
#
###Abstract
# Visual manual tests for graph areafill.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide graph.areafill

package require rbc
namespace import rbc::*

namespace eval graph.areafill {
    # baseline closure before; chord after. Inspect the closed interior, not just
    # the outline. Baseline reaches the plot edge; origin joins data coordinate (0,0).
    proc RBC.graph.areafill.M.1.1.Setup {} {
        graph .fill -width 700 -height 500 -plotbackground white
        pack .fill -fill both -expand yes
        .fill axis configure x y -min -3 -max 3
        .fill grid configure -hide no
        .fill element create trace -data {2 0 2 2 0 2} -symbol circle -pixels 5 -color navy     -areapattern solid -areaforeground orange -areaopacity 0.65
        .fill element configure trace -areaclose baseline
    }

    proc RBC.graph.areafill.M.1.1.Body {} {
        .fill element configure trace -areaclose chord
    }

    proc RBC.graph.areafill.M.1.1.Cleanup {} {
        destroy .fill
    }

    # chord closure before; origin after. Inspect the closed interior, not just
    # the outline. Baseline reaches the plot edge; origin joins data coordinate (0,0).
    proc RBC.graph.areafill.M.1.2.Setup {} {
        graph .fill -width 700 -height 500 -plotbackground white
        pack .fill -fill both -expand yes
        .fill axis configure x y -min -3 -max 3
        .fill grid configure -hide no
        .fill element create trace -data {2 0 2 2 0 2} -symbol circle -pixels 5 -color navy     -areapattern solid -areaforeground orange -areaopacity 0.65
        .fill element configure trace -areaclose chord
    }

    proc RBC.graph.areafill.M.1.2.Body {} {
        .fill element configure trace -areaclose origin
    }

    proc RBC.graph.areafill.M.1.2.Cleanup {} {
        destroy .fill
    }

    # origin closure before; baseline after. Inspect the closed interior, not just
    # the outline. Baseline reaches the plot edge; origin joins data coordinate (0,0).
    proc RBC.graph.areafill.M.1.3.Setup {} {
        graph .fill -width 700 -height 500 -plotbackground white
        pack .fill -fill both -expand yes
        .fill axis configure x y -min -3 -max 3
        .fill grid configure -hide no
        .fill element create trace -data {2 0 2 2 0 2} -symbol circle -pixels 5 -color navy     -areapattern solid -areaforeground orange -areaopacity 0.65
        .fill element configure trace -areaclose origin
    }

    proc RBC.graph.areafill.M.1.3.Body {} {
        .fill element configure trace -areaclose baseline
    }

    proc RBC.graph.areafill.M.1.3.Cleanup {} {
        destroy .fill
    }

    # Transparent stipple leaves the grid visible through its holes. After: opaque
    # stipple background fills those holes; the closure remains unchanged.
    proc RBC.graph.areafill.M.1.4.Setup {} {
        graph .fill -width 700 -height 500 -plotbackground white
        pack .fill -fill both -expand yes
        .fill axis configure x y -min -3 -max 3
        .fill grid configure -hide no
        .fill element create trace -data {2 0 2 2 0 2} -symbol circle -pixels 5 -color navy     -areapattern solid -areaforeground orange -areaopacity 0.65
        .fill element configure trace -areaclose origin -areapattern gray25 -areaopacity 1 -areabackground {}
    }

    proc RBC.graph.areafill.M.1.4.Body {} {
        .fill element configure trace -areabackground lightblue
    }

    proc RBC.graph.areafill.M.1.4.Cleanup {} {
        destroy .fill
    }

    # Image-tiled area follows the chosen closure. After: invert and resize; the
    # pattern stays inside the fill and does not leak beyond the plot rectangle.
    proc RBC.graph.areafill.M.1.5.Setup {} {
        graph .fill -width 700 -height 500 -plotbackground white
        pack .fill -fill both -expand yes
        .fill axis configure x y -min -3 -max 3
        .fill grid configure -hide no
        .fill element create trace -data {2 0 2 2 0 2} -symbol circle -pixels 5 -color navy     -areapattern solid -areaforeground orange -areaopacity 0.65
        variable tile
        set tile [image create photo -width 8 -height 8]
        $tile put lightblue -to 0 0 8 8
        $tile put navy -to 0 0 4 4
        .fill element configure trace -areaclose origin -areatile $tile -areaopacity 0.6
    }

    proc RBC.graph.areafill.M.1.5.Body {} {
        .fill configure -invertxy yes -width 420 -height 600
    }

    proc RBC.graph.areafill.M.1.5.Cleanup {} {
        destroy .fill
        variable tile
        image delete $tile
        unset tile
    }

    # Solid translucent area overlays the grid. After: zero opacity removes only
    # the fill; line and symbols remain. No old fill pixels should remain.
    proc RBC.graph.areafill.M.1.6.Setup {} {
        graph .fill -width 700 -height 500 -plotbackground white
        pack .fill -fill both -expand yes
        .fill axis configure x y -min -3 -max 3
        .fill grid configure -hide no
        .fill element create trace -data {2 0 2 2 0 2} -symbol circle -pixels 5 -color navy     -areapattern solid -areaforeground orange -areaopacity 0.65
        .fill element configure trace -areaclose origin
    }

    proc RBC.graph.areafill.M.1.6.Body {} {
        .fill element configure trace -areaopacity 0
    }

    proc RBC.graph.areafill.M.1.6.Cleanup {} {
        destroy .fill
    }

}
