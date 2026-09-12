# Compare native and Cairo geometry and photo markers. Requires --enable-cairo.
package require Tk
package require rbc

proc CairoDemoAreas {} {
    global showAreas usePhotoTile areaTile
    foreach g {.native .cairo} {
        $g element configure signal -areapattern [expr {$showAreas ? "solid" : {}}]
        $g element configure dashed -areapattern [expr {$showAreas ? "gray50" : {}}]
        $g element configure signal -areatile [expr {$showAreas && $usePhotoTile ? $areaTile : {}}]
    }
}

### fill and tile options
set showAreas 0
set usePhotoTile 0
set tileVariant 0
set areaTile [image create photo -width 8 -height 8]
$areaTile put #e6eef8 -to 0 0 8 8
$areaTile put #cedef0 -to 0 0 4 8

### create top window and control elements
wm title . {RBC: native and Cairo}
ttk::frame .controls
ttk::label .controls.label -text {Cairo antialiasing:}
ttk::combobox .controls.mode -state readonly -width 10 -values {default none gray fast good best}
.controls.mode set default
pack .controls.label .controls.mode -side left -padx 4 -pady 4
ttk::checkbutton .controls.areas -text {Area fills} -variable showAreas -command CairoDemoAreas
pack .controls.areas -side left -padx 12
ttk::checkbutton .controls.tile -text {Photo tile} -variable usePhotoTile -command CairoDemoAreas
ttk::button .controls.recolor -text {Recolor tile} -command {
    set tileVariant [expr {!$tileVariant}]
    $areaTile put [expr {$tileVariant ? "#e4c7b3" : "#cedef0"}] -to 0 0 4 8
}
pack .controls.tile .controls.recolor -side left -padx 4
set axisWidth 2
ttk::label .controls.axisLabel -text {Axis width:}
ttk::combobox .controls.axisWidth -state readonly -width 2 -textvariable axisWidth -values {0 1 2 3}
bind .controls.axisWidth <<ComboboxSelected>> {
    foreach g {.native .cairo} {
        foreach axis {x y} {$g axis configure $axis -linewidth $axisWidth}
    }
}
pack .controls.axisLabel .controls.axisWidth -side left -padx 4
set showLegend 1
ttk::checkbutton .controls.legend -text {Legends} -variable showLegend -command {
    foreach g {.native .cairo} {$g legend configure -hide [expr {!$showLegend}]}
}
pack .controls.legend -side left -padx 4
set showGrid 1
set showMarkers 1
ttk::checkbutton .controls.grid -text {Grid} -variable showGrid -command {
    foreach g {.native .cairo} {$g grid configure -hide [expr {!$showGrid}]}
}
ttk::checkbutton .controls.markers -text {Markers} -variable showMarkers -command {
    foreach g {.native .cairo} {
        foreach marker {demoArrow demoPolygon demoText demoPhoto demoBitmap} {
            $g marker configure $marker -hide [expr {!$showMarkers}]
        }
    }
}
pack .controls.grid .controls.markers -side left -padx 4
pack .controls -side top -fill x
ttk::label .description -text {Top: error bars. Middle: strokes and areas. Bottom: symbols and edge clipping.}
pack .description -side top -pady 4
bind .controls.mode <<ComboboxSelected>> {
    foreach g {.cairo} {$g configure -antialias [.controls.mode get]}
}

### create graph windows and displayed elements

# Enlarge RGBA pixels to show opaque, transparent and half-alpha samples.
set markerSource [image create photo -data {iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAE0lEQVR4nGP4z8AAQUDcACT+AwA2XgZ7cJWRwAAAAABJRU5ErkJggg==}]
set markerPhoto [image create photo]
$markerPhoto copy $markerSource -zoom 12 12
image delete $markerSource
set markerBitmap @[file join [file dirname [info script]] cairo-bitmap.xbm]
set data {}
set dashedData {}
for {set i 0} {$i<=400} {incr i} {
    set x [expr {$i/40.0}]
    set y [expr {sin($x)+0.1*cos(7*$x)}]
    lappend data $x [expr {$y+0.6}]
    lappend dashedData $x [expr {$y-0.6}]
}

ttk::frame .traces
pack .traces -side top -fill both -expand yes

foreach renderer {native cairo} {
    set g .$renderer
    ::rbc::graph $g -renderer $renderer -width 500 -height 350 -title $renderer -plotbackground white
    pack $g -in .traces -side left -fill both -expand yes
    $g legend configure -hide no -position rightmargin -font {Arial 10}
    foreach axis {x y} {$g axis configure $axis -linewidth $axisWidth}
    $g axis configure x -min 0 -max 10
    $g axis configure y -min -3 -max 3
    $g grid configure -hide no -color grey85 -dashes dot -linewidth 1 -minor no
    $g element create signal -label Signal -data $data -symbol none -linewidth 1 -color navy\
        -areapattern [expr {$showAreas ? "solid" : ""}] -areaforeground #e6eef8
    $g element create dashed -label Dashed -data $dashedData -symbol none -linewidth 2 -color firebrick -dashes {8 4}\
            -offdash steelblue -areapattern [expr {$showAreas ? "gray50" : ""}] -areaforeground #e3beb0\
            -areabackground {}
    $g element create errors -label {} -data {1 2.3 3 2.3 5 2.3 7 2.3 9 2.3} -linewidth 0 -symbol circle -pixels 7\
            -color darkgreen -xerror {0.3 0.3 0.3 0.3 0.3} -yerror {0.25 0.25 0.25 0.25 0.25} -errorbarwidth 2\
            -errorbarcap 8 -showerrorbars both
    set x 0
    foreach symbol {circle square diamond plus cross splus scross triangle arrow} {
        incr x
        $g element create $symbol -label [expr {$symbol in {circle triangle} ? $symbol : ""}] -data [list $x -2.4]\
                -linewidth 0 -symbol $symbol -pixels 15 -fill lightsteelblue -outline navy -outlinewidth 1
    }
    $g element create clipped -label {} -data {0 -2.4 10 -2.4} -linewidth 0 -symbol circle -pixels 21 -fill salmon\
            -outline firebrick
    $g element create tinySample -label {} -data {6.5 -1.7 6.7 -1.7 6.9 -1.7 7.1 -1.7 7.3 -1.7} -linewidth 0\
            -symbol circle -pixels 1 -fill navy
    $g element create bitmapSample -label Bitmap -data {2.7 -1.7 5.5 -1.7} -linewidth 0 -symbol $markerBitmap\
            -pixels 17 -outline #885577 -fill {}
    $g marker create line -name demoArrow -coords {1.2 2.7 2 1.8} -outline #885577 -linewidth 2 -arrow last -cap round
    $g marker create polygon -name demoPolygon -coords {7.5 -1 8.6 -0.6 9 -1.4} -fill #f1ddc6 -stipple gray50\
            -outline #a56c32 -linewidth 2 -dashes {6 3}
    $g marker create bitmap -name demoBitmap -bitmap $markerBitmap -coords {3.6 -1.8 4.2 -1.2} -foreground #885577\
            -background #f1ddc6 -rotate 27
    $g marker create image -name demoPhoto -coords {6.2 -1.5} -image $markerPhoto -anchor center
    $g marker create text -name demoText -coords {4.8 2.75} -text {Native text} -foreground grey30\
            -background lightsteelblue -rotate 17
}

