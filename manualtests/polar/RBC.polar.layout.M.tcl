# RBC.polar.layout.M.tcl --
#
###Abstract
# Visual manual tests for polar/Smith geometry, aspect handling,
# inversion, off-centre ranges, and data outside the Smith unit circle.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide polar.layout

package require rbc
namespace import rbc::*

namespace eval polar.layout {

    # ------------------------------------------------------------------
    # Non-square window.
    #
    # The polar plot should remain circular rather than becoming an
    # ellipse.
    # ------------------------------------------------------------------

    proc RBC.polar.layout.M.1.1.Setup {} {
        vector create z -type complex
        set values {}
        for {set degree 0} {$degree <= 360} {incr degree 10} {
            set theta [expr {$degree * acos(-1.0) / 180.0}]
            lappend values [list [expr {0.8 * cos($theta)}] [expr {0.8 * sin($theta)}]]
        }
        z set $values
        polar .polar1 -width 700 -height 330 -title "Polar: non-square widget, aspect remains 1:1"
        pack .polar1 -fill both -expand yes
        .polar1 axis configure x -min -1.1 -max 1.1
        .polar1 axis configure y -min -1.1 -max 1.1
        .polar1 grid configure -hide no
        .polar1 element create Circle -cdata z -smooth natural -symbol none -linewidth 2
    }

    proc RBC.polar.layout.M.1.1.Body {} {
        .polar1 configure -width 330 -height 700 -title "Polar: tall widget, aspect remains 1:1"
    }

    proc RBC.polar.layout.M.1.1.Cleanup {} {
        destroy .polar1
        catch {vector destroy z}
    }


    # ------------------------------------------------------------------
    # Off-centre numerical ranges.
    #
    # The grid circle should remain centred on zero and use the largest
    # complete radius that fits within the four numerical boundaries.
    # ------------------------------------------------------------------

    proc RBC.polar.layout.M.1.2.Setup {} {
        vector create z -type complex
        z set {{-1.8 0.0} {-0.8 1.2} {0.8 1.6} {1.7 0.2} {0.6 -1.3}}
        polar .polar1 -width 560 -height 480 -title "Polar: off-centre axis ranges"
        pack .polar1 -fill both -expand yes
        .polar1 axis configure x -min -5 -max 2
        .polar1 axis configure y -min -3 -max 4
        .polar1 grid configure -hide no
        .polar1 element create Curve -cdata z -smooth natural -symbol circle -pixels 5 -linewidth 2
    }

    proc RBC.polar.layout.M.1.2.Body {} {
        .polar1 axis configure x -min -3 -max 2
        .polar1 axis configure y -min -2.5 -max 4
    }

    proc RBC.polar.layout.M.1.2.Cleanup {} {
        destroy .polar1
        catch {vector destroy z}
    }

    # ------------------------------------------------------------------
    # Side-by-side normal and inverted Smith axes.
    # ------------------------------------------------------------------

    proc RBC.polar.layout.M.1.3.Setup {} {
        wm geometry . 760x390
        vector create gamma -type complex
        gamma set {{-0.8 -0.2} {-0.4 0.5} {0.1 0.75} {0.65 0.35} {0.5 -0.45}}
        foreach {w title invert} {.p1 "Smith: normal axes" no .p2 "Smith: -invertxy yes" yes} {
            polar $w -width 370 -height 370 -representation smith -invertxy $invert -title $title
            $w axis configure x -min -1.2 -max 1.2
            $w axis configure y -min -1.2 -max 1.2
            $w grid configure -hide no
            $w element create Gamma -cdata gamma -smooth quadratic -symbol circle -pixels 5 -linewidth 2
        }
        grid .p1 .p2 -sticky news
        grid columnconfigure . 0 -weight 1
        grid columnconfigure . 1 -weight 1
        grid rowconfigure . 0 -weight 1
    }

    proc RBC.polar.layout.M.1.3.Body {} {
        .p1 configure -invertxy yes -title "Smith: live switch to inverted axes"
    }

    proc RBC.polar.layout.M.1.3.Cleanup {} {
        catch {destroy .p1}
        catch {destroy .p2}
        catch {vector destroy gamma}
    }


    # ------------------------------------------------------------------
    # Data outside the unit circle.
    #
    # Negative resistance may legitimately map outside |Gamma|=1.
    # The Smith grid stays unit-radius while the element remains visible.
    # ------------------------------------------------------------------

    proc RBC.polar.layout.M.1.4.Setup {} {
        vector create z -type complex
        z set {{-25 0} {-10 10} {10 20} {50 0} {150 -20}}
        polar .polar1 -width 650 -height 430 -representation smith -title "Smith: data outside unit circle"
        pack .polar1 -fill both -expand yes
        .polar1 axis configure x -min -3.5 -max 1.3
        .polar1 axis configure y -min -1.5 -max 1.5
        .polar1 grid configure -hide no
        .polar1 element create Z -cdata z -cdataformat impedance -z0 50 -smooth natural -symbol circle -pixels 6\
                -linewidth 2
    }

    proc RBC.polar.layout.M.1.4.Body {} {
        .polar1 element configure Z -smooth quadratic
        .polar1 configure -title "Smith: outside-circle data, quadratic smoothing"
    }

    proc RBC.polar.layout.M.1.4.Cleanup {} {
        destroy .polar1
        catch {vector destroy z}
    }
}
