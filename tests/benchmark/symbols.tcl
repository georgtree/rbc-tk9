#!/usr/bin/env tclsh

package require Tk
package require rbc

source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcSymbolsBenchmark {
    variable top .rbcSymbolsBenchmark
    variable graph .rbcSymbolsBenchmark.g
    variable xVector ::rbcSymbolsX
    variable yVector ::rbcSymbolsY
    variable options {}
}

proc ::rbcSymbolsBenchmark::Usage {} {
    puts {Usage: symbols.tcl ?options?

Options:

  -profile NAME
      smoke, standard, or stress.
      Default: standard

  -points LIST
      Comma-separated source-point counts.

  -sizes LIST
      Comma-separated WIDTHxHEIGHT values.

  -symbols LIST
      Comma-separated symbols.

  -pixels LIST
      Comma-separated symbol sizes in pixels.

  -trace
      Also benchmark line+circle cases.

  -no-trace
      Do not benchmark line+circle cases.

  -iterations N
      Measured iterations.

  -warmup N
      Warm-up iterations.

  -csv FILE
      Write long-format CSV.

  -help
      Show this message.

Examples:

  tclsh symbols.tcl

  tclsh symbols.tcl -profile stress

  tclsh symbols.tcl -points 10000,100000 -symbols circle,square -pixels 3,7,15
}
}

proc ::rbcSymbolsBenchmark::InitOptions {argv} {
    variable options
    set profile [::rbcBenchmark::FindProfile $argv standard]
    set options [dict merge [dict create profile $profile symbols {circle square diamond plus} trace 1 csv {}]\
                         [::rbcBenchmark::ProfileDefaults symbols $profile]]
}

proc ::rbcSymbolsBenchmark::ParseArgs {argv} {
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

            -trace {
                dict set options trace 1
            }

            -no-trace {
                dict set options trace 0
            }

            -profile -
            -points -
            -sizes -
            -symbols -
            -pixels -
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

                    -points {
                        dict set options points [::rbcBenchmark::ParseList $value]
                    }

                    -sizes {
                        dict set options sizes [::rbcBenchmark::ParseList $value]
                    }

                    -symbols {
                        dict set options symbols [::rbcBenchmark::ParseList $value]
                    }

                    -pixels {
                        dict set options pixels [::rbcBenchmark::ParseList $value]
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
                error "unknown option \"$arg\"; use -help"
            }
        }
    }

    dict set options points [::rbcBenchmark::ValidateCounts [dict get $options points] 2]
    dict set options sizes [::rbcBenchmark::ParseSizes [join [dict get $options sizes] ,]]
    ::rbcBenchmark::ValidateIterations [dict get $options iterations] [dict get $options warmup]
    set validSymbols {circle square diamond plus cross splus scross triangle arrow}
    foreach symbol [dict get $options symbols] {
        if {$symbol ni $validSymbols} {
            error "unknown symbol '$symbol'"
        }
    }
    set pixels {}
    foreach value [dict get $options pixels] {
        if {![string is integer -strict $value] ||
            $value < 1} {
            error "symbol size \"$value\" must be an integer >= 1"
        }
        lappend pixels $value
    }
    dict set options pixels $pixels
}

proc ::rbcSymbolsBenchmark::CreateVectors {n} {
    variable xVector
    variable yVector
    catch {::rbc::vector destroy $xVector}
    catch {::rbc::vector destroy $yVector}
    ::rbc::vector create [format "%s(%d)" $xVector $n] -variable {}
    ::rbc::vector create [format "%s(%d)" $yVector $n] -variable {}
    set step [expr {1.0/double($n-1)}]
    $xVector seq 0.0 end $step
    $yVector expr {0.65*sin(::rbcSymbolsX*217.0)+0.25*sin(::rbcSymbolsX*997.0)+0.08*sin(::rbcSymbolsX*8191.0)}
}

proc ::rbcSymbolsBenchmark::CreateGraph {} {
    variable top
    variable graph
    set ::rbcBenchmark::top $top
    set ::rbcBenchmark::graph $graph
    ::rbcBenchmark::CreateBareGraph ::rbc::graph "RBC symbol rendering benchmark"
    $graph axis configure x -hide yes -min 0.0 -max 1.0
    $graph axis configure y -hide yes -min -1.1 -max 1.1
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcSymbolsBenchmark::CreateElement {symbol pixels trace} {
    variable graph
    variable xVector
    variable yVector
    ::rbcBenchmark::DeleteAllElements
    set width [expr {$trace ? 1 : 0}]
    return [::rbcBenchmark::Time {
        $graph element create signal -xdata $xVector -ydata $yVector -linewidth $width -symbol $symbol -pixels $pixels\
                -maxsymbols 0 -scalesymbols no -showvalues no -smooth linear -decimate none -reduce 0.0
        ::rbcBenchmark::SyncDisplay
    }]
}

proc ::rbcSymbolsBenchmark::RunCase {caseName symbol pixels trace n width height csv} {
    variable options
    variable graph
    lassign [::rbcBenchmark::SetSize $width $height] actualWidth actualHeight
    $graph axis configure x -max 1.0
    ::rbcBenchmark::SyncDisplay
    set createMs [CreateElement $symbol $pixels $trace]
    set warmup [dict get $options warmup]
    set iterations [dict get $options iterations]
    set redraw [::rbcBenchmark::Collect $warmup $iterations [list ::rbcBenchmark::MeasureRedraw]]
    set remap [::rbcBenchmark::CollectIndexed $warmup $iterations [list ::rbcBenchmark::MeasureAxisRemap x 1.0]]
    $graph axis configure x -max 1.0
    ::rbcBenchmark::SyncDisplay
    puts [format "%-20s %10d %6dx%-6d %6dx%-6d %11.3f %11.3f %11.3f" $caseName $n $width $height $actualWidth\
                  $actualHeight $createMs [dict get $redraw median] [dict get $remap median]]
    flush stdout
    ::rbcBenchmark::WriteStandardMetrics $csv symbols $caseName $n $width $height $actualWidth $actualHeight $createMs\
            $redraw $remap
    ::rbcBenchmark::DeleteAllElements
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcSymbolsBenchmark::PrintHeader {} {
    ::rbcBenchmark::PrintEnvironment "RBC symbol/scatter rendering benchmark"
    puts {
-bufferelements is disabled.

create-ms:
    creates the element, maps all source points, and performs its first draw.

redraw-med:
    redraws already-mapped symbol geometry.  This is the primary
    renderer metric.

axis-remap:
    changes the X axis slightly, forcing mapping and drawing again.
}
    puts [format "%-20s %10s %13s %13s %11s %11s %11s" case points requested actual create-ms redraw-med axis-remap]
    puts [string repeat - 102]
}

proc ::rbcSymbolsBenchmark::Cleanup {} {
    variable top
    variable xVector
    variable yVector
    catch {destroy $top}
    catch {::rbc::vector destroy $xVector}
    catch {::rbc::vector destroy $yVector}
}

proc ::rbcSymbolsBenchmark::Main {argv} {
    variable options
    InitOptions $argv
    ParseArgs $argv
    CreateGraph
    PrintHeader
    set csv [::rbcBenchmark::OpenLongCsv [dict get $options csv]]
    try {
        foreach n [dict get $options points] {
            CreateVectors $n
            foreach pixels [dict get $options pixels] {
                foreach symbol [dict get $options symbols] {
                    set caseName [format "%s-%d" $symbol $pixels]
                    foreach size [dict get $options sizes] {
                        lassign $size width height
                        RunCase $caseName $symbol $pixels 0 $n $width $height $csv
                    }
                }
                if {[dict get $options trace]} {
                    set caseName [format "trace+circle-%d" $pixels]
                    foreach size [dict get $options sizes] {
                        lassign $size width height
                        RunCase $caseName circle $pixels 1 $n $width $height $csv
                    }
                }
            }
        }
    } finally {
        if {$csv ne {}} {
            close $csv
        }
        Cleanup
    }
}

if {[catch { ::rbcSymbolsBenchmark::Main $argv } message opts]} {
    puts stderr "benchmark failed: $message"
    if {[dict exists $opts -errorinfo]} {
        puts stderr [dict get $opts -errorinfo]
    }
    ::rbcSymbolsBenchmark::Cleanup
    exit 1
}

exit 0
