# Compare native and Cairo bar elements. Requires --enable-cairo.
package require Tk
package require rbc

wm title . {RBC bars: native and Cairo}
set barMode aligned
set stipple {}
set relief flat
set invert 0
set active 0
set antialias default
proc UpdateBars {} {
    global barMode stipple relief invert active antialias
    foreach g {.native .cairo} {
        $g configure -barmode $barMode -invertxy $invert -antialias $antialias
        foreach element {first second} {
            $g element configure $element -stipple $stipple -relief $relief
        }
        $g element deactivate first
        if {$active} {$g element activate first 1}
    }
}
ttk::frame .controls
foreach {name variable values} {
    layout barMode {normal aligned overlap stacked}
    stipple stipple {{} gray25 gray50 gray75}
    relief relief {flat raised sunken ridge groove solid}
    antialias antialias {default none gray fast good best}
} {
    ttk::label .controls.${name}Label -text $name
    ttk::combobox .controls.$name -state readonly -width 9 -textvariable $variable -values $values
    bind .controls.$name <<ComboboxSelected>> {UpdateBars}
    pack .controls.${name}Label .controls.$name -side left -padx 3 -pady 5
}
ttk::checkbutton .controls.invert -text {Invert axes} -variable invert -command UpdateBars
ttk::checkbutton .controls.active -text {Active bar} -variable active -command UpdateBars
pack .controls.invert .controls.active -side left -padx 4
pack .controls -fill x
ttk::label .description -text {Bar fills and error strokes use the selected renderer. Relief borders and labels use Tk.}
pack .description -pady 4
foreach renderer {native cairo} {
    set g .$renderer
    ::rbc::barchart $g -renderer $renderer -width 500 -height 360 -title $renderer \
        -plotbackground white -barmode aligned
    pack $g -side left -fill both -expand yes
    $g grid configure -hide yes
    $g axis configure x -min 0 -max 5
    $g axis configure y -min -8 -max 12
    $g element create first -data {1 5 2 -4 3 7 4 3} -foreground steelblue -background navy \
        -relief flat -borderwidth 2 -yerror {1 0.8 1.2 0.6} -errorbarcolor black \
        -errorbarwidth 2 -errorbarcap 8 -showvalues y
    $g element create second -data {1 3 2 -2 3 4 4 5} -foreground salmon -background firebrick \
        -relief flat -borderwidth 2 -yerror {0.6 0.5 0.8 0.7} -errorbarcolor black \
        -errorbarwidth 2 -errorbarcap 8 -showvalues y
    $g pen configure activeBar -foreground gold -background darkgoldenrod -borderwidth 2 -relief raised
}
