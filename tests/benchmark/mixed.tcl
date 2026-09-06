#!/usr/bin/env tclsh

package require Tk
package require rbc

source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcMixedBenchmark {
    variable top .rbcMixedBenchmark
    variable graph .rbcMixedBenchmark.g
    variable options {}
    variable vectors {}
}

proc ::rbcMixedBenchmark::Usage {} {
    puts {Usage: mixed.tcl ?options?

Options:

  -profile NAME
      smoke, standard, or stress.
      Default: standard

  -scales LIST
      Comma-separated scene scale factors.

  -sizes LIST
      Comma-separated WIDTHxHEIGHT values.

  -iterations N
      Measured iterations.

  -warmup N
      Warm-up iterations.

  -csv FILE
      Write long-format CSV.

  -help
      Show this message.

Scale 1 contains approximately:

    4 x 50000-point connected line traces
    20000 scatter symbols
    10000 Y error bars
    2000 bars
    250 line markers
    250 polygon markers
    250 text markers
}
}

proc ::rbcMixedBenchmark::InitOptions {argv} {
    variable options
    set profile [::rbcBenchmark::FindProfile $argv standard]
    set options [dict merge [dict create profile $profile csv {}] [::rbcBenchmark::ProfileDefaults mixed $profile]]
}

proc ::rbcMixedBenchmark::ParseArgs {argv} {
    variable options
    for {set i 0} {$i < [llength $argv]} {incr i} {
        set arg [lindex $argv $i]
        switch -- $arg {
            -help -
            --help -
            -h {
                Usage
                exit 0
            }

            -profile -
            -scales -
            -sizes -
            -iterations -
            -warmup -
            -csv {
                incr i
                if {$i >= [llength $argv]} {
                    error "missing value for $arg"
                }
                set value [lindex $argv $i]
                switch -- $arg {
                    -profile {
                    }
                    -scales {
                        dict set options scales [::rbcBenchmark::ParseList $value]
                    }
                    -sizes {
                        dict set options sizes [::rbcBenchmark::ParseList $value]
                    }
                    -iterations {
                        dict set options iterations $value
                    }
                    -warmup {
                        dict set options warmup $value
                    }
                    -csv {
                        dict set options csv $value
                    }
                }
            }
            default {
                error "unknown option '$arg'; use -help"
            }
        }
    }
    dict set options scales [::rbcBenchmark::ValidateCounts [dict get $options scales] 1]
    dict set options sizes [::rbcBenchmark::ParseSizes [join [dict get $options sizes] ,]]
    ::rbcBenchmark::ValidateIterations [dict get $options iterations] [dict get $options warmup]
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
    puts [format "%-10s %12d %6dx%-6d %6dx%-6d %11.3f %11.3f %11.3f" $caseName $primitiveCount $width $height\
                  $actualWidth $actualHeight $createMs [dict get $redraw median] [dict get $remap median]]
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

    puts [format "%-10s %12s %13s %13s %11s %11s %11s" case primitives requested actual create-ms redraw-med\
                  axis-remap]
    puts [string repeat - 98]
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
    InitOptions $argv
    ParseArgs $argv
    CreateGraph
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
