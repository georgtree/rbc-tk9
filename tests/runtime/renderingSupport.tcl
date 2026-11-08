# Shared snapshot fixtures and renderer availability for runtime tests.
# Each test file sources this support directly so it can run independently.
namespace eval ::rbcTest::rendering {
    namespace export PartialGrayPixels StrokeFixture CountPhotoColor PlotInk AreaInterior BarFixture LegendPixels ShowSampleLegend EmptyAxisLabel AxisFixture BitmapFixture BitmapSymbolFixture OpacityFixture OpacityPixel
    proc CheckCairo {} {
        ::rbc::graph .rbcCairoProbe -renderer native
        try {
            set code [catch {.rbcCairoProbe configure -renderer cairo} message]
            if {$code && $message ne {Cairo renderer is not available in this build}} {
                error $message
            }
            ::tcltest::testConstraint rbcCairo [expr {!$code}]
            ::tcltest::testConstraint rbcNoCairo $code
        } finally {
            destroy .rbcCairoProbe
        }
    }
    proc PartialGrayPixels {photo} {
        set count 0
        # Axes, symbols, borders and text are disabled in these fixtures.
        for {set y 21} {$y < [image height $photo]-21} {incr y} {
            for {set x 21} {$x < [image width $photo]-21} {incr x} {
                lassign [$photo get $x $y] r g b
                if {$r == $g && $g == $b && $r > 0 && $r < 255} {
                    incr count
                }
            }
        }
        return $count
    }
    proc StrokeFixture {kind {renderer cairo}} {
        ::rbc::$kind .renderer -renderer $renderer -width 240 -height 180 -background white -plotbackground white\
                -borderwidth 0 -plotborderwidth 0 -highlightthickness 0 -leftmargin 20 -rightmargin 20 -topmargin 20\
                -bottommargin 20
        pack .renderer
        .renderer legend configure -hide yes
        .renderer grid configure -hide yes
        foreach axis {x y x2 y2} {
            .renderer axis configure $axis -hide yes
        }
        .renderer axis configure x -min 0 -max 10
        .renderer axis configure y -min 0 -max 10
        .renderer element create signal -data {1 2 9 7} -symbol none -color black -linewidth 1
        update
    }
    proc CountPhotoColor {photo color} {
        set count 0
        for {set y 21} {$y < [image height $photo]-21} {incr y} {
            for {set x 21} {$x < [image width $photo]-21} {incr x} {
                if {[$photo get $x $y] eq $color} {
                    incr count
                }
            }
        }
        return $count
    }
    proc PlotInk {photo} {
        lassign [.renderer extents plotarea] left top width height
        set right [expr {$left+$width}]
        set bottom [expr {$top+$height}]
        set inside 0
        set outside 0
        for {set y 0} {$y < [image height $photo]} {incr y} {
            for {set x 0} {$x < [image width $photo]} {incr x} {
                if {[$photo get $x $y] eq {255 255 255}} {
                    continue
                }
                if {$x >= $left && $x < $right && $y >= $top && $y < $bottom} {
                    incr inside
                } else {
                    incr outside
                }
            }
        }
        list $inside $outside
    }
    proc AreaInterior {photo} {
        lassign [.renderer extents plotarea] left top width height
        set result {}
        # Safely inside the constant-height fill used by graph.cairo.34.
        for {set y [expr {$top+$height/2}]} {$y < $top+3*$height/4} {incr y} {
            for {set x [expr {$left+$width/3}]} {$x < $left+2*$width/3} {incr x} {
                lappend result [$photo get $x $y]
            }
        }
        return $result
    }
    proc BarFixture {} {
        ::rbc::barchart .renderer -renderer cairo -width 240 -height 180 -background white -plotbackground white\
                -borderwidth 0 -plotborderwidth 0 -highlightthickness 0 -leftmargin 20 -rightmargin 20 -topmargin 20\
                -bottommargin 20
        pack .renderer
        .renderer legend configure -hide yes
        .renderer grid configure -hide yes
        foreach axis {x y x2 y2} {.renderer axis configure $axis -hide yes}
        .renderer axis configure x -min 0 -max 10
        .renderer axis configure y -min -10 -max 10
        .renderer element create signal -data {2 5 5 -5 8 7} -foreground black -background {} -relief flat\
                -borderwidth 0 -showvalues no -showerrorbars none
        update
    }
    proc LegendPixels {photo {color {}}} {
        set ink 0
        set gray 0
        set matching 0
        for {set y 0} {$y < [image height $photo]} {incr y} {
            for {set x 0} {$x < [image width $photo]} {incr x} {
                set rgb [$photo get $x $y]
                lassign $rgb r g b
                incr ink [expr {$rgb ne {255 255 255}}]
                incr gray [expr {$r == $g && $g == $b && $r > 0 && $r < 255}]
                incr matching [expr {$rgb eq $color}]
            }
        }
        return [list $ink $gray $matching]
    }
    proc ShowSampleLegend {} {
        .renderer element configure signal -label Sample -hideplot yes
        .renderer legend configure -hide no -position rightmargin -background white -foreground white -borderwidth 0\
                -relief flat -font {Arial 18}
        update
    }
    proc EmptyAxisLabel {args} {
        return {}
    }
    proc AxisFixture {axis} {
        StrokeFixture graph
        .renderer element delete signal
        .renderer configure -leftmargin 40 -rightmargin 40 -topmargin 40 -bottommargin 40
        .renderer axis configure $axis -hide no -color black -linewidth 2 -showticks yes\
                -command ::rbcTest::rendering::EmptyAxisLabel -title {} -min 0 -max 10
        update
    }
    proc BitmapFixture {{renderer cairo}} {
        StrokeFixture graph $renderer
        .renderer element delete signal
        set bitmap [makeFile {
            #define sample_width 8
            #define sample_height 8
            static unsigned char sample_bits[] = {
                0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa
            };
        } renderer-bitmap.xbm]
        set mask [makeFile {
            #define sample_width 8
            #define sample_height 8
            static unsigned char sample_bits[] = {
                0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f
            };
        } renderer-mask.xbm]
        .renderer marker create bitmap -name m -coords {5 5} -bitmap @$bitmap -foreground black -background blue\
                -anchor center
        return [list $bitmap $mask]
    }
    proc BitmapSymbolFixture {kind} {
        lassign [BitmapFixture] bitmap mask
        destroy .renderer
        StrokeFixture $kind
        .renderer element configure signal -data {2 2 5 5 8 8} -linewidth 0 -symbol @$bitmap -pixels 17 -outline black\
                -fill blue
        return [list $bitmap $mask]
    }
    proc OpacityFixture {kind invert renderer} {
        ::rbc::$kind .opacity -width 400 -height 400 -plotbackground blue \
            -renderer $renderer -invertxy $invert
        pack .opacity
        .opacity axis configure x -min -3 -max 3
        .opacity axis configure y -min -3 -max 3
        .opacity grid configure -hide yes
        .opacity legend configure -hide yes
        .opacity element create signal -data {-2 -2 2 -2 2 2 -2 2 -2 -2} \
            -linewidth 0 -symbol none -areaclose chord -areapattern solid -areaforeground red
        update idletasks
    }
    proc OpacityPixel {photo x y expected} {
        lassign [.opacity transform $x $y] px py
        foreach actual [$photo get $px $py] target $expected {
            if {abs($actual-$target) > 1} {
                return 0
            }
        }
        return 1
    }
}
