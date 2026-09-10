#!/usr/bin/env tclsh

package require Tk
package require rbc

source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcMixedBenchmark {
    variable top .rbcMixedBenchmark
    variable graph .rbcMixedBenchmark.g
    variable options {}
    variable vectors {}
    variable resultReport {}
}

proc ::rbcMixedBenchmark::ParseArgs {argv} {
    variable options
    set parsed [argparse -inline -exact -long\
                -help {Benchmark a mixed engineering-style graph containing lines, scatter symbols, error bars, bars,\
                               and several marker types.} {
            {-renderer= -enum {native cairo} -default native -help {Select the graph renderer}}
            {-antialias= -enum {default none gray} -default default -help {Select Cairo antialiasing}}
            {-profile= -enum {smoke standard stress} -default standard -help {Select benchmark workload profile}}
            {-scales= -validate {[::rbcBenchmark::IsCountList $arg 1]} -errormsg {-scales must contain integers >= 1}}
            {-sizes= -validate {[::rbcBenchmark::IsSizeList $arg]} -errormsg {-sizes must contain WIDTHxHEIGHT values}}
            {-iterations= -type integer -validate {$arg >= 1} -errormsg {-iterations must be >= 1}}
            {-warmup= -type integer -validate {$arg >= 0} -errormsg {-warmup must be >= 0}}
            {-csv= -default {} -help {Write long-format CSV results}}
        } $argv]
    ::rbcBenchmark::SetRendererOptions $parsed
    set profile [dict get $parsed profile]
    set options [dict merge [dict create csv {}] [::rbcBenchmark::ProfileDefaults mixed $profile] $parsed]
    dict set options scales [::rbcBenchmark::ParseList [dict get $options scales]]
    dict set options sizes [::rbcBenchmark::ParseSizes [dict get $options sizes]]
}

proc ::rbcMixedBenchmark::NewVector {name n} {
    variable vectors
    catch {::rbc::vector destroy $name}
    ::rbc::vector create [format "%s(%d)" $name $n] -variable {}
    lappend vectors $name
    return $name
}

proc ::rbcMixedBenchmark::CreateData {scale} {
    variable vectors
    foreach vector $vectors {
        catch {::rbc::vector destroy $vector}
    }
    set vectors {}
    set linePoints [expr {50000 * $scale}]
    set scatterPoints [expr {20000 * $scale}]
    set errorPoints [expr {10000 * $scale}]
    set barPoints [expr {2000 * $scale}]
    set lineX [NewVector ::rbcMixedLineX $linePoints]
    set step [expr {1.0/double($linePoints-1)}]
    $lineX seq 0.0 end $step
    set lineYs {}
    foreach {name expression} {
        ::rbcMixedLineY0 {0.60*sin(::rbcMixedLineX*47.0)+0.15*sin(::rbcMixedLineX*601.0)}
        ::rbcMixedLineY1 {0.50*sin(::rbcMixedLineX*73.0+0.5)+0.20*sin(::rbcMixedLineX*911.0)}
        ::rbcMixedLineY2 {0.45*sin(::rbcMixedLineX*109.0+1.0)+0.18*sin(::rbcMixedLineX*1301.0)}
        ::rbcMixedLineY3 {0.40*sin(::rbcMixedLineX*151.0+1.5)+0.16*sin(::rbcMixedLineX*1709.0)}
    } {
        set y [NewVector $name $linePoints]
        $y expr $expression
        lappend lineYs $y
    }
    set scatterX [NewVector ::rbcMixedScatterX $scatterPoints]
    set scatterY [NewVector ::rbcMixedScatterY $scatterPoints]
    set step [expr {1.0/double($scatterPoints-1)}]
    $scatterX seq 0.0 end $step
    $scatterY expr {0.75*sin(::rbcMixedScatterX*389.0)+0.12*sin(::rbcMixedScatterX*3701.0)}
    set errorX [NewVector ::rbcMixedErrorX $errorPoints]
    set errorY [NewVector ::rbcMixedErrorY $errorPoints]
    set errorDY [NewVector ::rbcMixedErrorDY $errorPoints]
    set step [expr {1.0/double($errorPoints-1)}]
    $errorX seq 0.0 end $step
    $errorY expr {0.55*sin(::rbcMixedErrorX*83.0)}
    $errorDY expr {0.02+0.015*abs(sin(::rbcMixedErrorX*67.0))}
    set barX [NewVector ::rbcMixedBarX $barPoints]
    set barY [NewVector ::rbcMixedBarY $barPoints]
    set step [expr {1.0/double($barPoints)}]
    $barX seq [expr {$step*0.5}] end $step
    $barY expr {0.20+0.12*(1.0+sin(::rbcMixedBarX*31.0))}
    return [dict create line_points $linePoints line_x $lineX line_ys $lineYs scatter_points $scatterPoints scatter_x\
                    $scatterX scatter_y $scatterY error_points $errorPoints error_x $errorX error_y $errorY error_dy\
                    $errorDY bar_points $barPoints bar_x $barX bar_y $barY marker_count [expr {250 * $scale}]]
}

proc ::rbcMixedBenchmark::CreateGraph {} {
    variable top
    variable graph
    set ::rbcBenchmark::top $top
    set ::rbcBenchmark::graph $graph
    ::rbcBenchmark::CreateBareGraph ::rbc::graph "RBC mixed rendering benchmark"
    $graph axis configure x -hide yes -min 0.0 -max 1.0
    $graph axis configure y -hide yes -min -1.1 -max 1.1
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcMixedBenchmark::CreateMarkers {count} {
    variable graph
    for {set i 0} {$i < $count} {incr i} {
        lassign [::rbcBenchmark::GridPosition $i $count] x y columns rows
        set dx [expr {0.20/double($columns)}]
        set dy [expr {0.20/double($rows)}]
        $graph marker create line -name lineMarker$i -coords [list [expr {$x-$dx}] [expr {$y-$dy}] [expr {$x+$dx}]\
                                                                      [expr {$y+$dy}]]\
                -outline black -linewidth 1 -under no
        $graph marker create polygon -name polygonMarker$i -coords [list $x [expr {$y-$dy}] [expr {$x+$dx}]\
                                                                            [expr {$y+$dy}] [expr {$x-$dx}]\
                                                                            [expr {$y+$dy}]]\
                -fill lightblue -outline {} -linewidth 0 -under no
        $graph marker create text -name textMarker$i -coords [list $x $y] -text M -font TkSmallCaptionFont -rotate 0\
                -anchor center -under no
    }
}

proc ::rbcMixedBenchmark::CreateScene {data} {
    variable graph
    ::rbcBenchmark::DeleteAllElements
    ::rbcBenchmark::DeleteAllMarkers
    return [::rbcBenchmark::Time {
        set i 0
        foreach y [dict get $data line_ys] {
            $graph line create line$i -xdata [dict get $data line_x] -ydata $y -linewidth 1 -symbol none -smooth linear\
                    -decimate none -reduce 0.0
            incr i
        }
        $graph line create scatter -xdata [dict get $data scatter_x] -ydata [dict get $data scatter_y] -linewidth 0\
                -symbol circle -pixels 5 -maxsymbols 0 -scalesymbols no -decimate none
        $graph line create errors -xdata [dict get $data error_x] -ydata [dict get $data error_y]\
                -yerror [dict get $data error_dy] -showerrorbars y -errorbarwidth 1 -errorbarcap 5 -linewidth 0\
                -symbol none -decimate none
        set barWidth [expr {0.8/double([dict get $data bar_points])}]
        $graph bar create bars -xdata [dict get $data bar_x] -ydata [dict get $data bar_y] -barwidth $barWidth\
                -relief flat -borderwidth 0 -foreground steelblue -showvalues no
        CreateMarkers [dict get $data marker_count]
        ::rbcBenchmark::SyncDisplay
    }]
}

proc ::rbcMixedBenchmark::RunCase {scale data width height csv} {
    variable options
    variable graph
    variable resultReport
    lassign [::rbcBenchmark::SetSize $width $height] actualWidth actualHeight
    $graph axis configure x -max 1.0
    ::rbcBenchmark::SyncDisplay
    set createMs [CreateScene $data]
    set warmup [dict get $options warmup]
    set iterations [dict get $options iterations]
    set redraw [::rbcBenchmark::Collect $warmup $iterations [list ::rbcBenchmark::MeasureRedraw]]
    set remap [::rbcBenchmark::CollectIndexed $warmup $iterations [list ::rbcBenchmark::MeasureAxisRemap x 1.0]]
    $graph axis configure x -max 1.0
    ::rbcBenchmark::SyncDisplay
    set primitiveCount [expr {4*[dict get $data line_points]+[dict get $data scatter_points]+\
                                      [dict get $data error_points]+[dict get $data bar_points]+\
                                      3*[dict get $data marker_count]}]
    set caseName scale-$scale
    ::rbcBenchmark::ReportAdd resultReport [list $caseName $primitiveCount "${width}x${height}"\
                                                    "${actualWidth}x${actualHeight}"\
                                                    [format %.3f $createMs] [format %.3f [dict get $redraw median]]\
                                                    [format %.3f [dict get $remap median]]]
    flush stdout
    ::rbcBenchmark::WriteStandardMetrics $csv mixed $caseName $primitiveCount $width $height $actualWidth $actualHeight\
            $createMs $redraw $remap
    ::rbcBenchmark::DeleteAllElements
    ::rbcBenchmark::DeleteAllMarkers
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcMixedBenchmark::PrintHeader {} {
    ::rbcBenchmark::PrintEnvironment "RBC mixed engineering-scene rendering benchmark"
    puts {
-bufferelements is disabled.

The mixed scene combines connected lines, scatter symbols, independent
error-bar segments, bars, line markers, filled polygon markers, and
text markers.

It is a whole-graph throughput benchmark.

Use the isolated workload scripts to identify which primitive causes
any regression or improvement.
}
}

proc ::rbcMixedBenchmark::InitResultReport {} {
    variable resultReport
    set resultReport [::rbcBenchmark::NewReport {case markers requested actual create-ms redraw-med axis-remap} {left\
    right right right right right right}]
}

proc ::rbcMixedBenchmark::Cleanup {} {
    variable top
    variable vectors
    catch {destroy $top}
    foreach vector $vectors {
        catch {::rbc::vector destroy $vector}
    }
    set vectors {}
}

proc ::rbcMixedBenchmark::Main {argv} {
    variable options
    variable resultReport
    ParseArgs $argv
    CreateGraph
    InitResultReport
    PrintHeader
    set csv [::rbcBenchmark::OpenLongCsv [dict get $options csv]]
    try {
        foreach scale [dict get $options scales] {
            set data [CreateData $scale]
            foreach size [dict get $options sizes] {
                lassign $size width height
                RunCase $scale $data $width $height $csv
            }
        }
        ::rbcBenchmark::PrintReport $resultReport
    } finally {
        if {$csv ne {}} {
            close $csv
        }
        Cleanup
    }
}

if {[catch { ::rbcMixedBenchmark::Main $argv } message opts]} {
    puts stderr "benchmark failed: $message"
    if {[dict exists $opts -errorinfo]} {
        puts stderr [dict get $opts -errorinfo]
    }
    ::rbcMixedBenchmark::Cleanup
    exit 1
}

exit 0
