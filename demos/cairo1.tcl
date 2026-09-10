# Compare native and Cairo areas, strokes, symbols and error bars. Requires --enable-cairo.
package require Tk
package require rbc

wm title . {RBC: native and Cairo}
ttk::frame .controls
ttk::label .controls.label -text {Cairo antialiasing:}
ttk::combobox .controls.mode -state readonly -width 10 \
    -values {default none gray fast good best}
.controls.mode set default
pack .controls.label .controls.mode -side left -padx 4 -pady 4
set showAreas 1
set usePhotoTile 0
set tileVariant 0
set areaTile [image create photo -width 8 -height 8]
$areaTile put #e6eef8 -to 0 0 8 8
$areaTile put #cedef0 -to 0 0 4 8
proc CairoDemoAreas {} {
    global showAreas usePhotoTile areaTile
    foreach g {.native .cairo} {
        $g element configure signal -areapattern [expr {$showAreas ? "solid" : ""}]
        $g element configure dashed -areapattern [expr {$showAreas ? "gray50" : ""}]
        $g element configure signal -areatile [expr {$showAreas && $usePhotoTile ? $areaTile : ""}]
    }
}
ttk::checkbutton .controls.areas -text {Area fills} -variable showAreas -command CairoDemoAreas
pack .controls.areas -side left -padx 12
ttk::checkbutton .controls.tile -text {Photo tile} -variable usePhotoTile -command CairoDemoAreas
ttk::button .controls.recolor -text {Recolor tile} -command {
    set tileVariant [expr {!$tileVariant}]
    $areaTile put [expr {$tileVariant ? "#e4c7b3" : "#cedef0"}] -to 0 0 4 8
}
pack .controls.tile .controls.recolor -side left -padx 4
pack .controls -side top -fill x
ttk::label .description -text {Top: error bars. Middle: strokes and areas. Bottom: symbols and edge clipping.}
pack .description -side top -pady 4
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
    $g axis configure y -min -3 -max 3
    $g element create signal -data $data -symbol none -linewidth 1 -color navy \
        -areapattern solid -areaforeground #e6eef8
    $g element create dashed -data $dashedData -symbol none -linewidth 2 \
        -color firebrick -dashes {8 4} -offdash steelblue \
        -areapattern gray50 -areaforeground #e3beb0 -areabackground {}
    $g element create errors -data {1 2.3 3 2.3 5 2.3 7 2.3 9 2.3} \
        -linewidth 0 -symbol circle -pixels 7 -color darkgreen \
        -xerror {0.3 0.3 0.3 0.3 0.3} -yerror {0.25 0.25 0.25 0.25 0.25} \
        -errorbarwidth 2 -errorbarcap 8 -showerrorbars both
    set x 0
    foreach symbol {circle square diamond plus cross splus scross triangle arrow} {
        incr x
        $g element create $symbol -data [list $x -2.4] -linewidth 0 \
            -symbol $symbol -pixels 15 -fill lightsteelblue -outline navy -outlinewidth 1
    }
    $g element create clipped -data {0 -2.4 10 -2.4} -linewidth 0 \
        -symbol circle -pixels 21 -fill salmon -outline firebrick
}
