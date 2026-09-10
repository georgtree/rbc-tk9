# Compare native and Cairo solid-line rendering. Requires --enable-cairo.
package require Tk
package require rbc

wm title . {RBC: native and Cairo}
set data {}
for {set i 0} {$i <= 400} {incr i} {
    set x [expr {$i / 40.0}]
    lappend data $x [expr {sin($x) + 0.1*cos(7*$x)}]
}

foreach renderer {native cairo} {
    set g .$renderer
    ::rbc::graph $g -renderer $renderer -width 500 -height 350 \
        -title $renderer -plotbackground white
    pack $g -side left -fill both -expand yes
    $g legend configure -hide yes
    $g axis configure x -min 0 -max 10
    $g axis configure y -min -1.5 -max 1.5
    $g element create signal -data $data -symbol none -linewidth 1 -color navy
}
