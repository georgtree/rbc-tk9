#!/usr/bin/env tclsh
# Text presentation workloads; construction is outside timed intervals.
package require Tk
package require rbc
source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcTextBenchmark {
    variable top .rbcTextBenchmark
    variable graph .rbcTextBenchmark.g
    variable options {}
    variable resultReport {}
}

proc ::rbcTextBenchmark::ParseArgs {argv} {
    variable options
    set options [argparse -inline -exact -long -help {Benchmark text markers, axis labels and legends.} {
        {-renderer= -enum {native cairo} -default native}
        {-antialias= -enum {default none gray} -default default}
        {-cases= -default markers -validate {[::rbcBenchmark::IsEnumList $arg {markers axes legend tracklegend}]}}
        {-counts= -default 10,100 -validate {[::rbcBenchmark::IsCountList $arg 2]}}
        {-rotations= -default 0,45,90 -validate {[::rbcBenchmark::IsEnumList $arg {0 45 90}]}}
        {-sizes= -default 640x480,1280x720 -validate {[::rbcBenchmark::IsSizeList $arg]}}
        {-entries= -type integer -default 4 -validate {$arg >= 1} -help {Legend entries per track}}
        {-font= -default TkDefaultFont -help {Tk font used by all labels}}
        {-iterations= -type integer -default 20 -validate {$arg >= 1}}
        {-warmup= -type integer -default 5 -validate {$arg >= 0}}
        {-csv= -default {}}
    } $argv]
    ::rbcBenchmark::SetRendererOptions $options
    foreach key {cases counts rotations} {dict set options $key [::rbcBenchmark::ParseList [dict get $options $key]]}
    dict set options sizes [::rbcBenchmark::ParseSizes [dict get $options sizes]]
    # Validate and resolve the font before constructing workloads.
    font actual [dict get $options font]
}

proc ::rbcTextBenchmark::CreateGraph {caseName count rotation} {
    variable graph
    variable top
    variable options
    set ::rbcBenchmark::top $top
    set ::rbcBenchmark::graph $graph
    set command [expr {$caseName eq "tracklegend" ? "::rbc::trackgraph" : "::rbc::graph"}]
    ::rbcBenchmark::CreateBareGraph $command {RBC text rendering benchmark}
    # A cached graph background would hide axis and legend rendering costs.
    $graph configure -buffergraph no -bufferelements no
    $graph axis configure x -hide yes -min 0 -max 1 -loose no
    $graph axis configure y -hide yes -min 0 -max 1 -loose no
    set font [dict get $options font]
    switch -- $caseName {
        markers {
            for {set i 0} {$i<$count} {incr i} {
                lassign [::rbcBenchmark::GridPosition $i $count] x y
                # Include backgrounds and multiline labels in the same batch.
                set background [expr {$i%2 ? "lightyellow" : ""}]
                $graph marker create text -name m$i -coords [list $x $y] \
                    -text "Signal $i\nVoltage (mV)" -font $font -rotate $rotation -anchor center \
                    -foreground navy -background $background
            }
        }
        axes {
            set ticks {}
            for {set i 0} {$i<$count} {incr i} {lappend ticks [expr {$i/double($count-1)}]}
            # Explicit ticks keep the requested count independent of automatic spacing.
            $graph axis configure x -hide no -majorticks $ticks -minorticks {} -rotate $rotation \
                -tickfont $font -title {Time (s)} -titlefont $font
            $graph axis configure y -hide no -majorticks $ticks -minorticks {} -rotate $rotation \
                -tickfont $font -title {Voltage (V)} -titlefont $font
        }
        legend {
            for {set i 0} {$i<$count} {incr i} {
                $graph line create signal$i -data {0.5 0.5} -symbol none -linewidth 0 -label "Signal $i (mV)"
            }
            $graph legend configure -hide no -position plotarea -anchor nw -font $font -columns 4
        }
        tracklegend {
            $graph configure -tracklayout fit
            for {set i 0} {$i<$count} {incr i} {
                $graph axis create trackY$i -hide yes -min 0 -max 1 -loose no
                $graph track create t$i -yaxes trackY$i
                for {set j 0} {$j<[dict get $options entries]} {incr j} {
                    $graph line create s${i}_$j -track t$i -mapy trackY$i -data {0.5 0.5} \
                        -symbol none -linewidth 0 -label "Track $i signal $j"
                }
                $graph track legend t$i configure -hide no -position left -anchor nw -rows 1 -font $font
            }
        }
    }
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcTextBenchmark::Cleanup {} {
    variable top
    catch {destroy $top}
}

proc ::rbcTextBenchmark::Main {argv} {
    variable graph
    variable options
    variable resultReport
    ParseArgs $argv
    set resultReport [::rbcBenchmark::NewReport {case count actual redraw-med axis-remap}]
    set csv [::rbcBenchmark::OpenLongCsv [dict get $options csv]]
    # Include resolved font/scaling in the pairing key; comparisons must not
    # silently combine different display scales or font substitutions.
    set fontKey [list [font actual [dict get $options font]] [tk scaling]]
    try {
        foreach caseName [dict get $options cases] {
            set rotations [expr {$caseName in {legend tracklegend} ? {0} : [dict get $options rotations]}]
            foreach count [dict get $options counts] {
                foreach rotation $rotations {
                    CreateGraph $caseName $count $rotation
                    foreach size [dict get $options sizes] {
                        lassign $size width height
                        lassign [::rbcBenchmark::SetSize $width $height] aw ah
                        $graph axis configure x -max 1
                        ::rbcBenchmark::SyncDisplay
                        set label "$caseName/rotate=$rotation"
                        if {$caseName eq "tracklegend"} {append label "/entries=[dict get $options entries]"}
                        append label "/font=$fontKey"
                        set warmup [dict get $options warmup]
                        set iterations [dict get $options iterations]
                        set redraw [::rbcBenchmark::Collect $warmup $iterations ::rbcBenchmark::MeasureRedraw]
                        set remap [::rbcBenchmark::CollectIndexed $warmup $iterations \
                                           [list ::rbcBenchmark::MeasureAxisRemap x 1.0]]
                        foreach metric {redraw axis-remap} stats [list $redraw $remap] {
                            ::rbcBenchmark::WriteLongMetric $csv text $label $count $width $height $aw $ah $metric $stats
                        }
                        ::rbcBenchmark::ReportAdd resultReport [list "$caseName/rotate=$rotation" $count "${aw}x${ah}" \
                            [format %.3f [dict get $redraw median]] [format %.3f [dict get $remap median]]]
                        puts "Completed $caseName rotation=$rotation count=$count ${aw}x${ah}"
                    }
                }
            }
        }
        ::rbcBenchmark::PrintEnvironment {RBC text rendering benchmark}
        puts "Font and scaling: $fontKey"
        ::rbcBenchmark::PrintReport $resultReport
    } finally {
        if {$csv ne {}} {close $csv}
        Cleanup
    }
}

if {[catch {::rbcTextBenchmark::Main $argv} message opts]} {
    puts stderr [dict get $opts -errorinfo]
    ::rbcTextBenchmark::Cleanup
    exit 1
}
exit 0
