#!/usr/bin/env tclsh
# Repeated clip changes, boundary crossings and partially visible tracks.
package require Tk
package require rbc
source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcTrackClipBenchmark {
    variable top .rbcTrackClipBenchmark
    variable graph .rbcTrackClipBenchmark.g
    variable options {}
    variable resultReport {}
}

proc ::rbcTrackClipBenchmark::ParseArgs {argv} {
    variable options
    set options [argparse -inline -exact -long -help {Benchmark track-local rendering and clipping.} {
        {-renderer= -enum {native cairo} -default native}
        {-antialias= -enum {default none gray} -default default}
        {-tracks= -default 2,8,32 -validate {[::rbcBenchmark::IsCountList $arg 1]}}
        {-points= -default 1000 -validate {[::rbcBenchmark::IsCountList $arg 2]}}
        {-sizes= -default 640x480,1280x720 -validate {[::rbcBenchmark::IsSizeList $arg]}}
        {-cases= -default line,mixed -validate {[::rbcBenchmark::IsEnumList $arg {line mixed}]}}
        {-layouts= -default fit,scroll -validate {[::rbcBenchmark::IsEnumList $arg {fit scroll}]}}
        {-inverted= -default 0,1 -validate {[::rbcBenchmark::IsEnumList $arg {0 1}]}}
        {-iterations= -type integer -default 15 -validate {$arg >= 1}}
        {-warmup= -type integer -default 3 -validate {$arg >= 0}}
        {-csv= -default {}}
    } $argv]
    ::rbcBenchmark::SetRendererOptions $options
    foreach key {tracks points cases layouts inverted} {
        dict set options $key [::rbcBenchmark::ParseList [dict get $options $key]]
    }
    dict set options sizes [::rbcBenchmark::ParseSizes [dict get $options sizes]]
}

proc ::rbcTrackClipBenchmark::CreateData {n} {
    foreach name {X Y E} {
        set vector ::rbcTrackClip$name
        catch {::rbc::vector destroy $vector}
        ::rbc::vector create [format {%s(%d)} $vector $n] -variable {}
    }
    ::rbcTrackClipX seq -0.1 end [expr {1.2/double($n-1)}]
    ::rbcTrackClipY expr {1.25*sin(::rbcTrackClipX*80.0)}
    ::rbcTrackClipE expr {0.15+0.0*::rbcTrackClipX}
}

proc ::rbcTrackClipBenchmark::CreateGraph {count caseName layout inverted} {
    variable top
    variable graph
    set ::rbcBenchmark::top $top
    set ::rbcBenchmark::graph $graph
    ::rbcBenchmark::CreateBareGraph ::rbc::trackgraph {RBC track clipping benchmark}
    $graph configure -invertxy $inverted -tracklayout $layout
    $graph axis configure x -hide yes -min 0 -max 1 -loose no
    for {set i 0} {$i<$count} {incr i} {
        set axis trackY$i
        set track t$i
        $graph axis create $axis -hide yes -min -1 -max 1 -loose no
        $graph track create $track -yaxes $axis -height 160
        set style [list -symbol none -linewidth 2]
        if {$caseName eq "mixed"} {
            set style [list -symbol circle -pixels 9 -scalesymbols no -maxsymbols 0 -linewidth 2 \
                           -yerror ::rbcTrackClipE -showerrorbars y -errorbarwidth 2 -errorbarcap 7]
        }
        $graph line create trace$i -track $track -mapy $axis -xdata ::rbcTrackClipX -ydata ::rbcTrackClipY \
                -decimate none -reduce 0 -color steelblue {*}$style
        if {$caseName eq "mixed"} {
            $graph bar create bar$i -track $track -mapy $axis -data {-0.02 1.3 0.4 -1.3 1.02 1.3} \
                    -barwidth 0.12 -foreground orange -borderwidth 3 -relief raised
            $graph marker create polygon -track $track -mapy $axis -coords {-0.05 -1.2 0.2 -1.2 0.2 1.2 -0.05 1.2} \
                    -fill lightyellow -outline purple -linewidth 4 -under yes
            $graph marker create arc -track $track -mapy $axis -coords {0.75 -1.3 1.1 1.3} \
                    -style arc -start 0 -extent 300 -outline red -linewidth 4
        }
    }
}

proc ::rbcTrackClipBenchmark::MeasureScroll {iteration} {
    variable graph
    return [::rbcBenchmark::Time {
        $graph track view moveto [expr {($iteration & 1) ? 0.20 : 0.15}]
        ::rbcBenchmark::SyncDisplay
    }]
}

proc ::rbcTrackClipBenchmark::Cleanup {} {
    variable top
    catch {destroy $top}
    foreach name {X Y E} {catch {::rbc::vector destroy ::rbcTrackClip$name}}
}

proc ::rbcTrackClipBenchmark::Main {argv} {
    variable graph
    variable options
    variable resultReport
    ParseArgs $argv
    set resultReport [::rbcBenchmark::NewReport {case tracks actual redraw-med axis-remap}]
    set csv [::rbcBenchmark::OpenLongCsv [dict get $options csv]]
    try {
        foreach n [dict get $options points] {
            CreateData $n
            foreach count [dict get $options tracks] {
                foreach caseName [dict get $options cases] {
                    foreach layout [dict get $options layouts] {
                        foreach inverted [dict get $options inverted] {
                            CreateGraph $count $caseName $layout $inverted
                            foreach size [dict get $options sizes] {
                                lassign $size width height
                                lassign [::rbcBenchmark::SetSize $width $height] aw ah
                                if {$layout eq "scroll"} {$graph track view moveto 0.15}
                                $graph axis configure x -max 1
                                ::rbcBenchmark::SyncDisplay
                                set label "$caseName/layout=$layout/inverted=$inverted/points=$n"
                                set warmup [dict get $options warmup]
                                set iterations [dict get $options iterations]
                                set redraw [::rbcBenchmark::Collect $warmup $iterations ::rbcBenchmark::MeasureRedraw]
                                set remap [::rbcBenchmark::CollectIndexed $warmup $iterations \
                                                   [list ::rbcBenchmark::MeasureAxisRemap x 1.0]]
                                foreach metric {redraw axis-remap} stats [list $redraw $remap] {
                                    ::rbcBenchmark::WriteLongMetric $csv trackclip $label $count $width $height $aw $ah $metric $stats
                                }
                                if {$layout eq "scroll"} {
                                    lassign [$graph track view] first last
                                    if {$last-$first < 1.0} {
                                        set scroll [::rbcBenchmark::CollectIndexed $warmup $iterations \
                                                            ::rbcTrackClipBenchmark::MeasureScroll]
                                        ::rbcBenchmark::WriteLongMetric $csv trackclip $label $count $width $height $aw $ah track-scroll $scroll
                                    }
                                }
                                ::rbcBenchmark::ReportAdd resultReport [list $label $count "${aw}x${ah}" \
                                    [format %.3f [dict get $redraw median]] [format %.3f [dict get $remap median]]]
                                puts "Completed $label tracks=$count ${aw}x${ah}"
                            }
                        }
                    }
                }
            }
        }
        ::rbcBenchmark::PrintEnvironment {RBC track clipping benchmark}
        ::rbcBenchmark::PrintReport $resultReport
    } finally {
        if {$csv ne {}} {close $csv}
        Cleanup
    }
}

if {[catch {::rbcTrackClipBenchmark::Main $argv} message opts]} {
    puts stderr [dict get $opts -errorinfo]
    ::rbcTrackClipBenchmark::Cleanup
    exit 1
}
exit 0
