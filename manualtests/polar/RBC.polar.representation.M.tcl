# RBC.polar.representation.M.tcl --
#
###Abstract
# Visual manual tests for the polar representation and its
# presentation options.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide polar.representation

package require rbc
namespace import rbc::*

namespace eval polar.representation {

    proc MakeSpiral {name} {
        vector create $name -type complex
        set values {}
        for {set i 0} {$i <= 120} {incr i} {
            set theta [expr {$i * 6.0 * acos(-1.0) / 180.0}]
            set radius [expr {0.12 + 0.0065 * $i}]
            lappend values [list [expr {$radius * cos($theta)}] [expr {$radius * sin($theta)}]]
        }
        $name set $values
    }

    proc AngleLabel {widget degrees} {
        return [format "%g deg" $degrees]
    }

    proc RadiusLabel {widget value} {
        if {$value < 0.0} {
            return {}
        }
        return [format "r=%.2g" $value]
    }

    # ------------------------------------------------------------------
    # Basic polar grid with a smooth complex spiral.
    # ------------------------------------------------------------------

    proc RBC.polar.representation.M.1.1.Setup {} {
        MakeSpiral z
        polar .polar1 -width 520 -height 520 -title "Polar: complex spiral"
        pack .polar1 -fill both -expand yes
        .polar1 axis configure x -min -1.1 -max 1.1
        .polar1 axis configure y -min -1.1 -max 1.1
        .polar1 grid configure -hide no
        .polar1 element create Spiral -cdata z -smooth natural -symbol none -linewidth 2
    }

    proc RBC.polar.representation.M.1.1.Body {} {
        .polar1 element configure Spiral -symbol circle -pixels 4
    }

    proc RBC.polar.representation.M.1.1.Cleanup {} {
        destroy .polar1
        catch {vector destroy z}
    }

    # ------------------------------------------------------------------
    # Custom angular ticks, angular formatter and radial formatter.
    # ------------------------------------------------------------------

    proc RBC.polar.representation.M.1.2.Setup {} {
        vector create z -type complex
        z set {{ 0.15 0.10} { 0.35 0.55} {-0.10 0.75} {-0.65 0.35} {-0.55 -0.35} { 0.10 -0.70} { 0.65 -0.25}}
        polar .polar1 -width 520 -height 520 -title "Polar: custom angular and radial labels"\
                -anglemajorticks {0 45 90 135 180 225 270 315}\
                -angleminorticks {22.5 67.5 112.5 157.5 202.5 247.5 292.5 337.5}\
                -anglecommand ::polar.representation::AngleLabel
        pack .polar1 -fill both -expand yes
        .polar1 axis configure x -min -1 -max 1 -majorticks {-1 -0.75 -0.5 -0.25 0  0.25  0.5  0.75 1}\
                -command ::polar.representation::RadiusLabel
        .polar1 axis configure y -min -1 -max 1
        .polar1 grid configure -hide no -minor yes
        .polar1 element create Curve -cdata z -smooth quadratic -symbol circle -pixels 5 -linewidth 2
    }

    proc RBC.polar.representation.M.1.2.Body {} {
        .polar1 configure -anglelabelanchor center -radiallabelanchor nw
    }

    proc RBC.polar.representation.M.1.2.Cleanup {} {
        destroy .polar1
        catch {vector destroy z}
    }


    # ------------------------------------------------------------------
    # Inverted axes.
    # ------------------------------------------------------------------

    proc RBC.polar.representation.M.1.3.Setup {} {
        vector create z -type complex
        z set {{-0.8 -0.2} {-0.2 0.7} { 0.4 0.9} { 0.8 0.1} { 0.3 -0.65}}
        polar .polar1 -width 520 -height 520 -title "Polar: normal axes"
        pack .polar1 -fill both -expand yes
        .polar1 axis configure x -min -1.2 -max 1.2
        .polar1 axis configure y -min -1.2 -max 1.2
        .polar1 grid configure -hide no
        .polar1 element create Curve -cdata z -smooth natural -symbol circle -pixels 5 -linewidth 2
    }

    proc RBC.polar.representation.M.1.3.Body {} {
        .polar1 configure -invertxy yes -title "Polar: -invertxy yes"
    }

    proc RBC.polar.representation.M.1.3.Cleanup {} {
        destroy .polar1
        catch {vector destroy z}
    }

    # ------------------------------------------------------------------
    # Grid and element mapped to alternate x2/y2 axes.
    # ------------------------------------------------------------------

    proc RBC.polar.representation.M.1.4.Setup {} {
        MakeSpiral z
        polar .polar1 -width 520 -height 520 -title "Polar: grid mapped to x2/y2"
        pack .polar1 -fill both -expand yes
        .polar1 axis configure x -min -10 -max 10
        .polar1 axis configure y -min -10 -max 10
        .polar1 axis configure x2 -min -1.1 -max 1.1
        .polar1 axis configure y2 -min -1.1 -max 1.1
        .polar1 grid configure -hide no -mapx x2 -mapy y2
        .polar1 element create Spiral -cdata z -mapx x2 -mapy y2 -smooth natural -symbol none -linewidth 2
    }

    proc RBC.polar.representation.M.1.4.Body {} {
        .polar1 axis configure x2 -min -1.5 -max 1.5
        .polar1 axis configure y2 -min -1.5 -max 1.5
    }

    proc RBC.polar.representation.M.1.4.Cleanup {} {
        destroy .polar1
        catch {vector destroy z}
    }
}
