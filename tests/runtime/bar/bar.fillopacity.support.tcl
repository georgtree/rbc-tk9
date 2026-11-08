# Shared fixtures for bar.fillopacity and Cairo regressions.
namespace eval ::rbcTest::barOpacity {
    namespace export Create Pixel
    proc Create {{renderer native}} {
        ::rbc::barchart .bo -width 400 -height 300 -renderer $renderer -plotbackground white
        .bo axis configure x -min 0 -max 2 -loose no
        .bo axis configure y -min 0 -max 6 -loose no
        .bo grid configure -hide yes
        .bo legend configure -hide yes
        .bo element create e -data {1 4} -barwidth 1 -foreground red -background red \
            -borderwidth 0 -label bar
        pack .bo
        update
    }
    proc Pixel {photo x y} {
        .bo snap $photo
        lassign [.bo transform $x $y] px py
        return [$photo get [expr {round($px)}] [expr {round($py)}]]
    }
}
