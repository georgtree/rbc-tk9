# RBC.polar.smith.M.tcl --
#
###Abstract
# Visual manual tests for Smith-chart grids and impedance/admittance
# data formats.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide polar.smith

package require rbc
namespace import rbc::*

namespace eval polar.smith {

    proc SmithRealLabel {widget domain value} {
        if {$domain eq "impedance"} {
            return [format "r=%g" $value]
        }
        return [format "g=%g" $value]
    }

    proc SmithImagLabel {widget domain value} {
        if {$domain eq "impedance"} {
            return [format "x=%+g" $value]
        }
        return [format "b=%+g" $value]
    }

    proc ConfigureSmith {w title gridMode} {
        polar $w -width 520 -height 520 -representation smith -smithgrid $gridMode -title $title
        $w axis configure x -min -1.2 -max 1.2
        $w axis configure y -min -1.2 -max 1.2
        $w grid configure -hide no -minor yes
    }

    # ------------------------------------------------------------------
    # Physical impedance trajectory.
    # ------------------------------------------------------------------

    proc RBC.polar.smith.M.1.1.Setup {} {
        vector create z -type complex
        z set {{10 -35} {20 -15} {35 0} { 50 25} {75 50} {120 30} {180 0}}
        ConfigureSmith .polar1 "Smith: impedance data, Z0=50" impedance
        pack .polar1 -fill both -expand yes
        .polar1 element create Z -cdata z -cdataformat impedance -z0 50 -smooth natural -symbol circle -pixels 5\
                -linewidth 2
    }

    proc RBC.polar.smith.M.1.1.Body {} {
        .polar1 element configure Z -smooth quadratic
        .polar1 configure -title "Smith: impedance, quadratic smoothing"
    }

    proc RBC.polar.smith.M.1.1.Cleanup {} {
        destroy .polar1
        catch {vector destroy z}
    }

    # ------------------------------------------------------------------
    # Physical admittance trajectory.
    # ------------------------------------------------------------------

    proc RBC.polar.smith.M.1.2.Setup {} {
        vector create y -type complex
        y set {{0.004 -0.012} {0.008 -0.008} {0.012 -0.004} {0.020 0.000} {0.018 0.010} {0.012 0.020} {0.006 0.025}}
        ConfigureSmith .polar1 "Smith: admittance data, Z0=50" admittance
        pack .polar1 -fill both -expand yes
        .polar1 element create Y -cdata y -cdataformat admittance -z0 50 -smooth natural -symbol diamond -pixels 5\
                -linewidth 2
    }

    proc RBC.polar.smith.M.1.2.Body {} {
        .polar1 configure -smithgrid both -title "Smith: admittance data with both grids"
    }

    proc RBC.polar.smith.M.1.2.Cleanup {} {
        destroy .polar1
        catch {vector destroy y}
    }


    # ------------------------------------------------------------------
    # Custom Smith contour sets and formatter callbacks.
    # ------------------------------------------------------------------

    proc RBC.polar.smith.M.1.3.Setup {} {
        ConfigureSmith .polar1 "Smith: custom contours and labels" both
        pack .polar1 -fill both -expand yes
        .polar1 configure -smithrealmajorticks {0 0.5 1 2 5} -smithrealminorticks {0.2 0.75 1.5 3}\
            -smithimagmajorticks {0.5 1 2 5} -smithimagminorticks {0.2 0.75 1.5 3}\
            -smithrealcommand ::polar.smith::SmithRealLabel -smithimagcommand ::polar.smith::SmithImagLabel
    }

    proc RBC.polar.smith.M.1.3.Body {} {
        .polar1 configure -smithrealmajorticks {0 0.2 1 5 10} -smithimagmajorticks {0.2 1 5 10}
    }

    proc RBC.polar.smith.M.1.3.Cleanup {} {
        destroy .polar1
    }

    # ------------------------------------------------------------------
    # Same physical impedance trajectory with two reference impedances.
    #
    # The Body also demonstrates a live -z0 reconfiguration.
    # ------------------------------------------------------------------

    proc RBC.polar.smith.M.1.4.Setup {} {
        wm geometry . 760x390
        vector create z -type complex
        z set {{20 -30} {35 -10} {50 0} {75 25} {120 40} {200 0}}
        foreach {w z0} {.p1 50 .p2 75} {
            ConfigureSmith $w "Smith: Z0=$z0 ohm" impedance
            $w configure -width 370 -height 370
            $w element create Z -cdata z -cdataformat impedance -z0 $z0 -smooth natural -symbol circle -pixels 5\
                    -linewidth 2
        }
        grid .p1 .p2 -sticky news
        grid columnconfigure . 0 -weight 1
        grid columnconfigure . 1 -weight 1
        grid rowconfigure . 0 -weight 1
    }
    proc RBC.polar.smith.M.1.4.Body {} {
        .p1 element configure Z -z0 100
        .p1 configure -title "Smith: live Z0 change to 100 ohm"
    }

    proc RBC.polar.smith.M.1.4.Cleanup {} {
        catch {destroy .p1}
        catch {destroy .p2}
        catch {vector destroy z}
    }
}
