# RBC.stripchart.errorband.M.tcl --
#
###Abstract
# Visual manual tests for stripchart errorband.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide stripchart.errorband

package require rbc
namespace import rbc::*

namespace eval stripchart.errorband {
    # Symmetric Y band surrounds the trace. After: explicit asymmetric low/high
    # bounds replace it; line remains within the envelope.
    proc RBC.stripchart.errorband.M.1.1.Setup {} {
        stripchart .band -width 700 -height 500 -plotbackground white
        pack .band -fill both -expand yes
        .band axis configure x y -min 0 -max 10
        .band grid configure -hide no
        .band element create signal -data {1 3 3 6 5 4 7 7 9 5} -symbol circle -pixels 5 -color navy     -showerrorbars none -errorbandforeground orange -errorbandopacity 0.5
        .band element configure signal -errorband y -yerror {1 0.5 1.5 0.5 1}
    }

    proc RBC.stripchart.errorband.M.1.1.Body {} {
        .band element configure signal -yerror {} -ylow {2 4 3 5 4} -yhigh {5 7 6 9 8}
    }

    proc RBC.stripchart.errorband.M.1.1.Cleanup {} {
        destroy .band
    }

    # X uncertainty forms a horizontal envelope. After: invert the axes; band
    # rotates with the data, without changing the meaning of the X errors.
    proc RBC.stripchart.errorband.M.1.2.Setup {} {
        stripchart .band -width 700 -height 500 -plotbackground white
        pack .band -fill both -expand yes
        .band axis configure x y -min 0 -max 10
        .band grid configure -hide no
        .band element create signal -data {1 3 3 6 5 4 7 7 9 5} -symbol circle -pixels 5 -color navy     -showerrorbars none -errorbandforeground orange -errorbandopacity 0.5
        .band element configure signal -errorband x -xerror {0.5 1 0.5 1 0.5}
    }

    proc RBC.stripchart.errorband.M.1.2.Body {} {
        .band configure -invertxy yes
    }

    proc RBC.stripchart.errorband.M.1.2.Cleanup {} {
        destroy .band
    }

    # Stippled Y band and step trace follow the same transitions. After: zoom
    # clips the envelope and trace together at the plot edges.
    proc RBC.stripchart.errorband.M.1.3.Setup {} {
        stripchart .band -width 700 -height 500 -plotbackground white
        pack .band -fill both -expand yes
        .band axis configure x y -min 0 -max 10
        .band grid configure -hide no
        .band element create signal -data {1 3 3 6 5 4 7 7 9 5} -symbol circle -pixels 5 -color navy     -showerrorbars none -errorbandforeground orange -errorbandopacity 0.5
        .band element configure signal -errorband y -yerror {2 2 2 2 2} -smooth step     -errorbandpattern gray50 -errorbandbackground {} -errorbandopacity 1
    }

    proc RBC.stripchart.errorband.M.1.3.Body {} {
        .band axis configure x -min 2 -max 8
        .band axis configure y -min 3 -max 6
    }

    proc RBC.stripchart.errorband.M.1.3.Cleanup {} {
        destroy .band
    }

    # Image-tiled Y band uses its own fill, independent of the trace. After: disable
    # the band; no tiles remain and line/symbols are still visible.
    proc RBC.stripchart.errorband.M.1.4.Setup {} {
        stripchart .band -width 700 -height 500 -plotbackground white
        pack .band -fill both -expand yes
        .band axis configure x y -min 0 -max 10
        .band grid configure -hide no
        .band element create signal -data {1 3 3 6 5 4 7 7 9 5} -symbol circle -pixels 5 -color navy     -showerrorbars none -errorbandforeground orange -errorbandopacity 0.5
        variable tile
        set tile [image create photo -width 8 -height 8]
        $tile put lightblue -to 0 0 8 8
        $tile put navy -to 0 0 4 4
        .band element configure signal -errorband y -yerror {1 1 1 1 1} -errorbandtile $tile
    }

    proc RBC.stripchart.errorband.M.1.4.Body {} {
        .band element configure signal -errorband none
    }

    proc RBC.stripchart.errorband.M.1.4.Cleanup {} {
        destroy .band
        variable tile
        image delete $tile
        unset tile
    }

}
