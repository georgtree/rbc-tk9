# RBC.polar.smoothing.M.tcl --
#
###Abstract
# Visual manual tests comparing line smoothing modes on polar
# trajectories.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide polar.smoothing

package require rbc
namespace import rbc::*

namespace eval polar.smoothing {

    proc ConfigurePanel {w title smooth vector} {
        polar $w -width 280 -height 280 -title $title
        $w axis configure x -min -1.5 -max 1.5
        $w axis configure y -min -1.5 -max 1.5
        $w grid configure -hide no
        $w element create Curve -cdata $vector -smooth $smooth -symbol circle -pixels 4 -linewidth 2
    }

    # ------------------------------------------------------------------
    # Compare all smoothing modes on the same coarse closed loop.
    # ------------------------------------------------------------------

    proc RBC.polar.smoothing.M.1.1.Setup {} {
        wm geometry . 600x600
        vector create z -type complex
        z set {{ 1.00 0.00} { 0.35 0.95} {-0.75 0.60} {-0.85 -0.45} { 0.35 -0.95} { 1.00 0.00}}
        ConfigurePanel .p1 "linear" linear z
        ConfigurePanel .p2 "natural" natural z
        ConfigurePanel .p3 "quadratic" quadratic z
        ConfigurePanel .p4 "catrom" catrom z
        grid .p1 -row 0 -column 0 -sticky news
        grid .p2 -row 0 -column 1 -sticky news
        grid .p3 -row 1 -column 0 -sticky news
        grid .p4 -row 1 -column 1 -sticky news
        grid rowconfigure . 0 -weight 1
        grid rowconfigure . 1 -weight 1
        grid columnconfigure . 0 -weight 1
        grid columnconfigure . 1 -weight 1
    }

    proc RBC.polar.smoothing.M.1.1.Body {} {
        foreach w {.p1 .p2 .p3 .p4} {
            $w element configure Curve -pixels 7
        }
    }

    proc RBC.polar.smoothing.M.1.1.Cleanup {} {
        foreach w {.p1 .p2 .p3 .p4} {
            catch {destroy $w}
        }
        catch {vector destroy z}
    }


    # ------------------------------------------------------------------
    # Vertical trajectory.
    #
    # This is intentionally not y=f(x), so natural and quadratic
    # smoothing visibly exercise the parametric implementation.
    # ------------------------------------------------------------------

    proc RBC.polar.smoothing.M.1.2.Setup {} {
        wm geometry . 860x330
        vector create z -type complex
        z set {{0.35 -1.00} {0.35 -0.35} {0.35 0.35} {0.35 1.00}}
        ConfigurePanel .p1 "vertical: linear" linear z
        ConfigurePanel .p2 "vertical: natural" natural z
        ConfigurePanel .p3 "vertical: quadratic" quadratic z
        grid .p1 .p2 .p3 -sticky news
        foreach col {0 1 2} {
            grid columnconfigure . $col -weight 1
        }
        grid rowconfigure . 0 -weight 1
    }

    proc RBC.polar.smoothing.M.1.2.Body {} {
        foreach w {.p1 .p2 .p3} {
            $w element configure Curve -symbol diamond -pixels 7
        }
    }

    proc RBC.polar.smoothing.M.1.2.Cleanup {} {
        foreach w {.p1 .p2 .p3} {
            catch {destroy $w}
        }
        catch {vector destroy z}
    }

    # ------------------------------------------------------------------
    # Singular Smith impedance point.
    #
    # Z=-Z0 is singular and therefore produces a break between two
    # valid runs.  The right-hand plot demonstrates per-run smoothing:
    # both valid portions should be smooth, but there must never be a
    # connection through the singular point.
    # ------------------------------------------------------------------

    proc RBC.polar.smoothing.M.1.3.Setup {} {
        wm geometry . 760x390
        vector create z -type complex
        z set {{ 50 0} {50 100} {0 50} {-50 0} {0 0} {10 -20} {0 -50}}

        foreach {w title smooth} {.p1 "Smith gap: linear" linear .p2 "Smith gap: natural per-run" natural} {
            polar $w -width 370 -height 370 -representation smith -title $title
            $w axis configure x -min -1.2 -max 1.2
            $w axis configure y -min -1.2 -max 1.2
            $w grid configure -hide no
            $w element create Curve -cdata z -cdataformat impedance -z0 50 -smooth $smooth -symbol circle -pixels 5\
                    -linewidth 2
        }
        grid .p1 .p2 -sticky news
        grid columnconfigure . 0 -weight 1
        grid columnconfigure . 1 -weight 1
        grid rowconfigure . 0 -weight 1
    }

    proc RBC.polar.smoothing.M.1.3.Body {} {
        .p2 element configure Curve -smooth quadratic
        .p2 configure -title "Smith gap: quadratic per-run"
    }


    proc RBC.polar.smoothing.M.1.3.Cleanup {} {
        catch {destroy .p1}
        catch {destroy .p2}
        catch {vector destroy z}
    }
}
