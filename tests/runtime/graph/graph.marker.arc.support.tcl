# Shared fixtures for graph.marker.arc and Cairo regressions.
namespace eval ::rbcTest::arc {
    namespace export Setup Point Find Pixel
    proc Setup {{renderer native}} {
        ::rbc::graph .arc -width 420 -height 320 -plotbackground white -renderer $renderer
        .arc axis configure x y -min 0 -max 10
        pack .arc
        update
        .arc marker create arc -name a -coords {2 2 8 8} -outline black -fill red
    }
    proc Point {u v} {
        lassign [.arc transform 2 2] x1 y1
        lassign [.arc transform 8 8] x2 y2
        list [expr {round(($x1+$x2)/2.0+$u*abs($x2-$x1)/2.0)}] [expr {round(($y1+$y2)/2.0-$v*abs($y2-$y1)/2.0)}]
    }
    proc Find {u v} {
        update
        lassign [Point $u $v] x y
        .arc marker find overlapping [expr {$x-2}] [expr {$y-2}] [expr {$x+2}] [expr {$y+2}]
    }
    proc Pixel {photo u v} {
        lassign [Point $u $v] x y
        $photo get $x $y
    }
}
