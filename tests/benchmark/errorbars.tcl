#!/usr/bin/env tclsh

package require Tk
package require rbc

source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcErrorBenchmark {
    variable top .rbcErrorBenchmark
    variable graph .rbcErrorBenchmark.g
    variable xVector ::rbcErrorX
    variable yVector ::rbcErrorY
    variable xError ::rbcErrorDX
    variable yError ::rbcErrorDY
    variable options {}
}

proc ::rbcErrorBenchmark::Usage {} {
    puts {Usage: errorbars.tcl ?options?

Options:

  -profile NAME
      smoke, standard, or stress.
      Default: standard

  -points LIST
      Comma-separated source-point counts.

  -sizes LIST
      Comma-separated WIDTHxHEIGHT values.

  -cases LIST
      Comma-separated cases:
          y
          xy
          y+circle

  -iterations N
      Measured iterations.

  -warmup N
      Warm-up iterations.

  -csv FILE
      Write long-format CSV.

  -help
      Show this message.

Examples:

  tclsh errorbars.tcl

  tclsh errorbars.tcl -profile stress

  tclsh errorbars.tcl -points 10000,100000 -cases y,xy
}
}

proc ::rbcErrorBenchmark::InitOptions {argv} {
    variable options
    set profile [::rbcBenchmark::FindProfile $argv standard]
    set options [dict merge [dict create profile $profile cases {y xy y+circle} csv {}]\
                         [::rbcBenchmark::ProfileDefaults errorbars $profile]]
}

proc ::rbcErrorBenchmark::ParseArgs {argv} {
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
            -points -
            -sizes -
            -cases -
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
                    -cases {
                        dict set options cases [::rbcBenchmark::ParseList $value]
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
    foreach case [dict get $options cases] {
        if {$case ni {y xy y+circle}} {
            error "unknown error-bar case '$case'"
        }
    }
}

proc ::rbcErrorBenchmark::CreateVectors {n} {
    variable xVector
    variable yVector
    variable xError
    variable yError
    foreach vector [list $xVector $yVector $xError $yError] {
        catch {::rbc::vector destroy $vector}
        ::rbc::vector create [format "%s(%d)" $vector $n] -variable {}
    }
    set step [expr {1.0/double($n-1)}]
    $xVector seq 0.0 end $step
    $yVector expr {0.65*sin(::rbcErrorX*71.0)+0.25*sin(::rbcErrorX*613.0)}
    $xError expr {0.0005+0.0002*abs(sin(::rbcErrorX*31.0))}
    $yError expr {0.025+0.015*abs(sin(::rbcErrorX*47.0))}
}

proc ::rbcErrorBenchmark::CreateGraph {} {
    variable top
    variable graph
    set ::rbcBenchmark::top $top
    set ::rbcBenchmark::graph $graph
    ::rbcBenchmark::CreateBareGraph ::rbc::graph "RBC error-bar rendering benchmark"
    $graph axis configure x -hide yes -min 0.0 -max 1.0
    $graph axis configure y -hide yes -min -1.1 -max 1.1
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcErrorBenchmark::CreateElement {caseName} {
    variable graph
    variable xVector
    variable yVector
    variable xError
    variable yError
    ::rbcBenchmark::DeleteAllElements
    set elementOptions [list -xdata $xVector -ydata $yVector -linewidth 0 -symbol none -showvalues no -smooth linear\
                                -decimate none -reduce 0.0 -errorbarwidth 1 -errorbarcap 5]
    switch -- $caseName {
        y {
            lappend elementOptions -yerror $yError -showerrorbars y
        }
        xy {
            lappend elementOptions -xerror $xError -yerror $yError -showerrorbars both
        }
        y+circle {
            lappend elementOptions -yerror $yError -showerrorbars y -symbol circle -pixels 5 -maxsymbols 0\
                    -scalesymbols no
        }
    }
    return [::rbcBenchmark::Time {
        $graph element create signal {*}$elementOptions
        ::rbcBenchmark::SyncDisplay
    }]
}

proc ::rbcErrorBenchmark::RunCase {caseName n width height csv} {
    variable options
    variable graph
    lassign [::rbcBenchmark::SetSize $width $height] actualWidth actualHeight
    $graph axis configure x -max 1.0
    ::rbcBenchmark::SyncDisplay
    set createMs [CreateElement $caseName]
    set warmup [dict get $options warmup]
    set iterations [dict get $options iterations]
    set redraw [::rbcBenchmark::Collect $warmup $iterations [list ::rbcBenchmark::MeasureRedraw]]
    set remap [::rbcBenchmark::CollectIndexed $warmup $iterations [list ::rbcBenchmark::MeasureAxisRemap x 1.0]]
    $graph axis configure x -max 1.0
    ::rbcBenchmark::SyncDisplay
    puts [format "%-14s %10d %6dx%-6d %6dx%-6d %11.3f %11.3f %11.3f" $caseName $n $width $height $actualWidth\
                  $actualHeight $createMs [dict get $redraw median] [dict get $remap median]]
    flush stdout
    ::rbcBenchmark::WriteStandardMetrics $csv errorbars $caseName $n $width $height $actualWidth $actualHeight\
            $createMs $redraw $remap
    ::rbcBenchmark::DeleteAllElements
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcErrorBenchmark::PrintHeader {} {
    ::rbcBenchmark::PrintEnvironment "RBC error-bar rendering benchmark"

    puts {
-bufferelements is disabled.

The connecting trace is suppressed so the y and xy cases primarily
stress independent error-bar segments and caps.

y+circle additionally stresses symbol rendering.

redraw-med is the primary renderer metric.

axis-remap includes mapping plus drawing.
}

    puts [format "%-14s %10s %13s %13s %11s %11s %11s" case points requested actual create-ms redraw-med axis-remap]
    puts [string repeat - 96]
}

proc ::rbcErrorBenchmark::Cleanup {} {
    variable top
    variable xVector
    variable yVector
    variable xError
    variable yError
    catch {destroy $top}
    foreach vector [list $xVector $yVector $xError $yError] {
        catch {::rbc::vector destroy $vector}
    }
}

proc ::rbcErrorBenchmark::Main {argv} {
    variable options
    InitOptions $argv
    ParseArgs $argv
    CreateGraph
    PrintHeader
    set csv [::rbcBenchmark::OpenLongCsv [dict get $options csv]]
    try {
        foreach n [dict get $options points] {
            CreateVectors $n
            foreach caseName [dict get $options cases] {
                foreach size [dict get $options sizes] {
                    lassign $size width height
                    RunCase $caseName $n $width $height $csv
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

if {[catch { ::rbcErrorBenchmark::Main $argv } message opts]} {
    puts stderr "benchmark failed: $message"
    if {[dict exists $opts -errorinfo]} {
        puts stderr [dict get $opts -errorinfo]
    }
    ::rbcErrorBenchmark::Cleanup
    exit 1
}

exit 0
