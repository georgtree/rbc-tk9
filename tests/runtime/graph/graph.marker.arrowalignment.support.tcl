# Shared fixtures for graph.marker.arrowalignment and Cairo regressions.
namespace eval ::rbcTest::arrow {
    namespace export Setup Coordinates AddArrow Snapshot Region
    proc Setup {renderer} {
        rbc::graph .arrowAlign -width 240 -height 240 -renderer $renderer -antialias gray \
            -plotbackground white -background white -plotborderwidth 0 -borderwidth 0 \
            -highlightthickness 0 -leftmargin 20 -rightmargin 20 -topmargin 20 -bottommargin 20
        .arrowAlign legend configure -hide yes
        .arrowAlign grid configure -hide yes
        foreach axis {x y x2 y2} {
            .arrowAlign axis configure $axis -hide yes -min 0 -max 1 -loose no
        }
        pack .arrowAlign
        update
    }
    proc Coordinates {pixels} {
        set coords {}
        foreach {x y} $pixels {
            lappend coords {*}[.arrowAlign invtransform $x $y]
        }
        return $coords
    }
    proc AddArrow {pixels arrow width} {
        .arrowAlign marker create line -name direction -coords [Coordinates $pixels] \
            -arrow $arrow -arrowshape {20 26 10} -outline red -linewidth $width
    }
    proc Snapshot {} {
        update idletasks
        set photo [image create photo]
        .arrowAlign snap $photo
        return $photo
    }
    proc Region {photo vertical} {
        set pixels {}
        for {set along 80} {$along <= 110} {incr along} {
            for {set across 76} {$across <= 85} {incr across} {
                if {$vertical} {
                    lappend pixels [$photo get $across $along]
                } else {
                    lappend pixels [$photo get $along $across]
                }
            }
        }
        return $pixels
    }
}
