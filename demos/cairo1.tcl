# Compare native and Cairo line rendering. Requires --enable-cairo.
package require Tk
package require rbc

wm title . {RBC: native and Cairo}
ttk::frame .controls
ttk::label .controls.label -text {Cairo antialiasing:}
ttk::combobox .controls.mode -state readonly -width 10 \
    -values {default none gray fast good best}
.controls.mode set default
pack .controls.label .controls.mode -side left -padx 4 -pady 4
pack .controls -side top -fill x
bind .controls.mode <<ComboboxSelected>> {
    .cairo configure -antialias [.controls.mode get]
}
set data {}
set dashedData {}
for {set i 0} {$i <= 400} {incr i} {
    set x [expr {$i / 40.0}]
    set y [expr {sin($x) + 0.1*cos(7*$x)}]
    lappend data $x [expr {$y + 0.6}]
    lappend dashedData $x [expr {$y - 0.6}]
}

foreach renderer {native cairo} {
    set g .$renderer
    ::rbc::graph $g -renderer $renderer -width 500 -height 350 \
        -title $renderer -plotbackground white
    pack $g -side left -fill both -expand yes
    $g legend configure -hide yes
    $g axis configure x -min 0 -max 10
    $g axis configure y -min -2 -max 2
    $g element create signal -data $data -symbol none -linewidth 1 -color navy
    $g element create dashed -data $dashedData -symbol none -linewidth 2 \
        -color firebrick -dashes {8 4} -offdash steelblue
}
