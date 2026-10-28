# RBC.trackgraph.support.tcl --
#
###Abstract
# Shared fixtures and renderer constraints for the trackgraph component tests.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
# Source inside the calling test file's component namespace. Helpers then use
# that namespace for vector names, callbacks and calls to other helpers.
# Each test file removes its namespace after cleanup to avoid shared variables.

proc Create {{inverted 0}} {
    ::rbc::trackgraph .tg -width 640 -height 480 -invertxy $inverted
    .tg legend configure -hide yes
    .tg axis configure x -min 0 -max 10 -loose no
    .tg axis create volts -min -1 -max 1 -loose no -title Voltage
    .tg axis create amps -min -10 -max 10 -loose no -title Current
    .tg track create voltage -yaxes volts -weight 2
    .tg track create current -yaxes amps -weight 1
    .tg line create v -track voltage -mapy volts -data {0 -1 5 1 10 -1} -color red -symbol none
    .tg strip create i -track current -mapy amps -data {0 10 5 -10 10 10} -color blue -symbol none
    pack .tg
    update
}

proc MarkerSetup {} {
    Create
    .tg element delete v i
    .tg grid configure -hide yes
}

proc ThirdTrack {} {
    .tg axis create aux -min -1 -max 1 -loose no
    .tg track create auxiliary -yaxes aux
    update
}

proc SeparatorPoint {} {
    lassign [.tg track bbox voltage] l t r b
    list [expr {($l+$r)/2}] [.tg track sashpos voltage]
}

proc FocusSetup {} {
    Create
    .tg configure -trackresize yes
    entry .trackFocus
    entry .trackOther
    pack .trackFocus .trackOther
    update
    focus -force .trackFocus
    update
}

proc LocalLegendSetup {} {
    Create
    .tg element configure v -label VoltageLegend
    .tg element configure i -label CurrentLegend
    foreach track {voltage current} {
        .tg track legend $track configure -hide no -position right -anchor ne -background white
    }
    update
}

proc LocalLegendPoint {track element} {
    lassign [.tg track legend $track bbox] left top width height
    for {set y $top} {$y<$top+$height} {incr y 2} {
        for {set x $left} {$x<$left+$width} {incr x 2} {
            if {[.tg track legend $track get @$x,$y] eq $element} {return [list $x $y]}
        }
    }
    error "legend entry $element not found in $track"
}

proc GridSetup {inverted renderer} {
    Create $inverted
    .tg element delete v i
    .tg configure -renderer $renderer -plotbackground white
    .tg axis configure x -majorticks {2 8}
    .tg axis create samples -min 0 -max 100 -loose no -majorticks {40 60}
    .tg x2axis use samples
    .tg axis create alternate -min 0 -max 100 -loose no -majorticks {25}
    .tg track configure voltage -yaxes {volts alternate}
    .tg grid configure -hide yes -minor no -dashes {} -linewidth 3
    .tg track grid voltage configure -hide no -mapx samples -mapy alternate -color #ff00ff
    .tg track grid current configure -hide no -mapx x -mapy {} -color #00ffff
    update
}

proc GridPixel {photo x y color} {
    for {set px [expr {int($x)-1}]} {$px<=int($x)+1} {incr px} {
        for {set py [expr {int($y)-1}]} {$py<=int($y)+1} {incr py} {
            if {[$photo get $px $py] eq $color} {return 1}
        }
    }
    return 0
}

proc ScrollSetup {{inverted 0}} {
    Create $inverted
    .tg axis create third -min -1 -max 1 -loose no
    .tg track create third -yaxes third -height 300
    .tg track configure voltage -height 300
    .tg track configure current -height 300
    .tg configure -tracklayout scroll
    update
}

proc BackgroundSetup {{inverted 0}} {
    Create $inverted
    .tg element delete v i
    update
    event generate .tg <Motion> -x 0 -y 0
}

proc BackgroundPoint {track} {
    lassign [.tg track bbox $track] l t r b
    return [list [expr {($l+$r)/2}] [expr {($t+$b)/2}]]
}

proc BackgroundMotion {track} {
    lassign [BackgroundPoint $track] x y
    event generate .tg <Motion> -x $x -y $y
}

proc BarsSetup {{inverted 0}} {
    Create $inverted
    .tg axis configure volts -min 0 -max 10 -loose no
    .tg axis configure amps -min 0 -max 40 -loose no
    .tg bar create a -track voltage -mapy volts -data {5 3} -barwidth 2 -foreground red -borderwidth 0
    .tg bar create b -track voltage -mapy volts -data {5 4} -barwidth 2 -foreground blue -borderwidth 0
    .tg bar create c -track current -mapy amps -data {5 10} -barwidth 2 -borderwidth 0
    .tg bar create d -track current -mapy amps -data {5 20} -barwidth 2 -borderwidth 0
    .tg element show {v i a b c d}
    update
}

foreach renderer {native cairo} {
    ::rbc::trackgraph .trackProbe
    testConstraint trackRenderer$renderer [expr {![catch {.trackProbe configure -renderer $renderer}]}]
    destroy .trackProbe
}
unset renderer
