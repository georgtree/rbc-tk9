# RBC.graph.export.M.tcl --
#
###Abstract
# Visual manual tests for graph export.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide graph.export

package require rbc
namespace import rbc::*

namespace eval graph.export {
    # Math glyphs, arcs, and a mapped window are visible. After: inspect the printed
    # SVG/PDF paths in external viewers; compare embedded and unembedded fonts.
    # Export files are retained for inspection; screen geometry must be restored.
    proc RBC.graph.export.M.1.1.Setup {} {
        graph .graph1 -width 720 -height 480 -plotbackground white
        pack .graph1 -fill both -expand yes
        .graph1 axis configure x y -min 0 -max 10
        .graph1 marker create text -name formula -coords {5 8} -text {V₁ = α² + β₃ → ∞} -font {Arial 18}
        .graph1 marker create arc -coords {2 2 8 7} -extent 250 -fill lightblue -outline navy
        button .graph1.info -text Info
        .graph1 marker create window -coords {5 5} -window .graph1.info
        update
    }

    proc RBC.graph.export.M.1.1.Body {} {
        # The nonembedded PDF font intentionally supports Windows-1252 only.
        if {![catch {.graph1 pdf output -embedfonts no} message] ||
                ![string match {*outside Windows-1252*} $message]} {
            error "Expected an unsupported-glyph diagnostic: $message"
        }
        foreach format {svg pdf} {
            foreach embed {0 1} {
                set channel [file tempfile path rbc-manual-export-]
                close $channel
                file delete $path
                append path .$format
                if {$format eq "pdf" && !$embed} {
                    .graph1 marker configure formula -text {V1 = a*a + b3 (unembedded PDF)}
                } else {
                    .graph1 marker configure formula -text {V₁ = α² + β₃ → ∞}
                }
                .graph1 $format output $path -width 900 -height 600 -embedfonts $embed
                puts "Inspect $format (embedfonts=$embed): $path"
            }
        }
    }

    proc RBC.graph.export.M.1.1.Cleanup {} {
        destroy .graph1
    }

}
