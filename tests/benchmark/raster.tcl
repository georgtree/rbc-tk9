#!/usr/bin/env tclsh
# Native bitmap/photo presentation; resource creation is outside timing.
package require Tk
package require rbc
source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcRasterBenchmark {
    variable top .rbcRasterBenchmark
    variable graph .rbcRasterBenchmark.g
    variable options {}
    variable files {}
    variable bitmap {}
    variable mask {}
    variable photo ::rbcRasterPhoto
    variable transparent ::rbcRasterTransparent
}

proc ::rbcRasterBenchmark::ParseArgs {argv} {
    variable options
    set options [argparse -inline -exact -long -help {Benchmark bitmap symbols and bitmap/photo markers.} {
        {-renderer= -enum {native cairo} -default native}
        {-antialias= -enum {default none gray} -default default}
        {-cases= -default bitmap-transparent,image-transparent -validate {[::rbcBenchmark::IsEnumList $arg {
            bitmap-transparent bitmap-opaque image-opaque image-transparent image-scaled image-scaled-transparent
            bitmap-symbol bitmap-mask-symbol}]}}
        {-counts= -default 100,1000 -validate {[::rbcBenchmark::IsCountList $arg 2]}}
        {-pixels= -default 16,64 -validate {[::rbcBenchmark::IsEnumList $arg {16 32 64}]}}
        {-rotations= -default 0,45,90 -validate {[::rbcBenchmark::IsEnumList $arg {0 45 90}]}}
        {-layouts= -default graph -validate {[::rbcBenchmark::IsEnumList $arg {graph fit scroll}]}}
        {-tracks= -type integer -default 4 -validate {$arg >= 2}}
        {-inverted= -default 0 -validate {[::rbcBenchmark::IsEnumList $arg {0 1}]}}
        {-sizes= -default 640x480,1280x720 -validate {[::rbcBenchmark::IsSizeList $arg]}}
        {-iterations= -type integer -default 20 -validate {$arg >= 1}}
        {-warmup= -type integer -default 5 -validate {$arg >= 0}}
        {-csv= -default {}}
    } $argv]
    ::rbcBenchmark::SetRendererOptions $options
    foreach key {cases counts pixels rotations layouts inverted} {
        dict set options $key [::rbcBenchmark::ParseList [dict get $options $key]]
    }
    dict set options sizes [::rbcBenchmark::ParseSizes [dict get $options sizes]]
}

proc ::rbcRasterBenchmark::CreateResources {pixels} {
    variable files
    variable bitmap
    variable mask
    variable photo
    variable transparent
    foreach image [list $photo $transparent] {catch {image delete $image}}
    foreach path $files {file delete $path}
    set files {}
    # Explicit XBM fixtures avoid platform-dependent built-in bitmap sizes.
    foreach kind {bitmap mask} {
        set data {}
        for {set y 0} {$y<$pixels} {incr y} {
            for {set x 0} {$x<$pixels} {incr x 8} {
                set byte 0
                for {set bit 0} {$bit<8} {incr bit} {
                    set xx [expr {$x+$bit}]
                    set inside [expr {$xx>=2 && $xx<$pixels-2 && $y>=2 && $y<$pixels-2}]
                    if {$inside && ($kind eq "mask" || (($xx/4+$y/4)%2)==0)} {
                        set byte [expr {$byte | (1<<$bit)}]
                    }
                }
                lappend data [format 0x%02x $byte]
            }
        }
        set channel [file tempfile path rbc-raster-]
        lappend files $path
        try {
            puts $channel "#define raster_width $pixels\n#define raster_height $pixels"
            puts $channel "static unsigned char raster_bits\[\] = { [join $data ,] };"
        } finally {close $channel}
        set $kind @$path
    }
    image create photo $photo -width $pixels -height $pixels
    $photo put #4060a0 -to 0 0 $pixels $pixels
    $photo put #e0b040 -to 2 2 [expr {$pixels-2}] [expr {$pixels-2}]
    image create photo $transparent -width $pixels -height $pixels
    $transparent copy $photo
    # Alternating transparent blocks exercise the native photo compositing path.
    for {set y 0} {$y<$pixels} {incr y} {
        for {set x 0} {$x<$pixels} {incr x} {
            if {(($x/4+$y/4)%2)==0} {$transparent transparency set $x $y 1}
        }
    }
}

proc ::rbcRasterBenchmark::CreateGraph {caseName count pixels rotation layout inverted} {
    variable graph
    variable top
    variable options
    variable bitmap
    variable mask
    variable photo
    variable transparent
    set ::rbcBenchmark::top $top
    set ::rbcBenchmark::graph $graph
    set command [expr {$layout eq "graph" ? "::rbc::graph" : "::rbc::trackgraph"}]
    ::rbcBenchmark::CreateBareGraph $command {RBC bitmap and image benchmark}
    $graph configure -invertxy $inverted -buffergraph no -bufferelements no
    $graph axis configure x -hide yes -min 0 -max 1 -loose no
    $graph axis configure y -hide yes -min 0 -max 1 -loose no
    set tracks [expr {$layout eq "graph" ? 1 : [dict get $options tracks]}]
    if {$layout ne "graph"} {$graph configure -tracklayout $layout}
    for {set track 0} {$track<$tracks} {incr track} {
        set target {}
        if {$layout ne "graph"} {
            $graph axis create trackY$track -hide yes -min 0 -max 1 -loose no
            $graph track create t$track -yaxes trackY$track -height 240
            $graph track legend t$track configure -hide yes
            set target [list -track t$track -mapy trackY$track]
        }
        set data {}
        for {set i 0} {$i<$count} {incr i} {
            lassign [::rbcBenchmark::GridPosition $i $count] x y columns rows
            # Include each plot boundary, where native mask/clip handling differs.
            switch [expr {$i%8}] {
                0 {set x 0}
                1 {set x 1}
                2 {set y 0}
                3 {set y 1}
            }
            if {$caseName in {bitmap-symbol bitmap-mask-symbol}} {
                lappend data $x $y
            } elseif {[string match bitmap-* $caseName]} {
                set background [expr {$caseName eq "bitmap-opaque" ? "lightyellow" : ""}]
                $graph marker create bitmap {*}$target -coords [list $x $y] -bitmap $bitmap \
                    -rotate $rotation -foreground navy -background $background -anchor center -under no
            } else {
                set image [expr {[string match *transparent $caseName] ? $transparent : $photo}]
                set coords [list $x $y]
                if {[string match image-scaled* $caseName]} {
                    # Two graph-coordinate corners select RBC's image resampling path.
                    set dx [expr {0.4/$columns}]
                    set dy [expr {0.4/$rows}]
                    set coords [list [expr {$x-$dx}] [expr {$y-$dy}] [expr {$x+$dx}] [expr {$y+$dy}]]
                }
                $graph marker create image {*}$target -coords $coords -image $image -anchor center -under no
            }
        }
        if {$caseName in {bitmap-symbol bitmap-mask-symbol}} {
            set symbol [expr {$caseName eq "bitmap-mask-symbol" ? [list $bitmap $mask] : [list $bitmap]}]
            $graph line create s$track {*}$target -data $data -symbol $symbol -pixels $pixels \
                -color navy -fill lightyellow -outline navy -linewidth 0 -maxsymbols 0 \
                -scalesymbols no -decimate none -reduce 0 -showvalues no
        }
    }
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcRasterBenchmark::Cleanup {} {
    variable top
    variable files
    variable photo
    variable transparent
    catch {destroy $top}
    foreach name [list $photo $transparent] {catch {image delete $name}}
    foreach path $files {catch {file delete $path}}
    set files {}
}

proc ::rbcRasterBenchmark::Main {argv} {
    variable graph
    variable options
    variable top
    ParseArgs $argv
    set report [::rbcBenchmark::NewReport {case count actual redraw-med axis-remap}]
    set csv [::rbcBenchmark::OpenLongCsv [dict get $options csv]]
    try {
        foreach pixels [dict get $options pixels] {
            CreateResources $pixels
            foreach caseName [dict get $options cases] {
                set rotations {0}
                if {$caseName in {bitmap-transparent bitmap-opaque}} {set rotations [dict get $options rotations]}
                foreach count [dict get $options counts] {
                    foreach rotation $rotations {
                        foreach layout [dict get $options layouts] {
                            foreach inverted [dict get $options inverted] {
                                CreateGraph $caseName $count $pixels $rotation $layout $inverted
                                foreach size [dict get $options sizes] {
                                    lassign $size width height
                                    lassign [::rbcBenchmark::SetSize $width $height] aw ah
                                    if {$layout eq "scroll"} {$graph track view moveto 0.15}
                                    $graph axis configure x -max 1
                                    ::rbcBenchmark::SyncDisplay
                                    set tracks [expr {$layout eq "graph" ? 1 : [dict get $options tracks]}]
                                    set label "$caseName/pixels=$pixels/rotate=$rotation/layout=$layout/tracks=$tracks/inverted=$inverted"
                                    set warmup [dict get $options warmup]
                                    set iterations [dict get $options iterations]
                                    set redraw [::rbcBenchmark::Collect $warmup $iterations ::rbcBenchmark::MeasureRedraw]
                                    set remap [::rbcBenchmark::CollectIndexed $warmup $iterations \
                                        [list ::rbcBenchmark::MeasureAxisRemap x 1.0]]
                                    foreach metric {redraw axis-remap} stats [list $redraw $remap] {
                                        ::rbcBenchmark::WriteLongMetric $csv raster $label $count $width $height $aw $ah $metric $stats
                                    }
                                    ::rbcBenchmark::ReportAdd report [list $label $count "${aw}x${ah}" \
                                        [format %.3f [dict get $redraw median]] [format %.3f [dict get $remap median]]]
                                    puts "Completed $label count=$count ${aw}x${ah}"
                                }
                                destroy $top
                            }
                        }
                    }
                }
            }
        }
        ::rbcBenchmark::PrintEnvironment {RBC bitmap and image benchmark}
        ::rbcBenchmark::PrintReport $report
    } finally {
        if {$csv ne {}} {close $csv}
        Cleanup
    }
}

if {[catch {::rbcRasterBenchmark::Main $argv} message opts]} {
    puts stderr [dict get $opts -errorinfo]
    ::rbcRasterBenchmark::Cleanup
    exit 1
}
exit 0
