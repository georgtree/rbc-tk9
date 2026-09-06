#!/usr/bin/env tclsh

package require Tk
package require rbc

source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcBarsBenchmark {
    variable top .rbcBarsBenchmark
    variable graph .rbcBarsBenchmark.g
    variable xVector ::rbcBarsX
    variable yVector ::rbcBarsY
    variable yError ::rbcBarsDY
    variable options {}
}

proc ::rbcBarsBenchmark::Usage {} {
    puts {Usage: bars.tcl ?options?

Options:

  -profile NAME
      smoke, standard, or stress.
      Default: standard

  -points LIST
      Bars in one element.

  -element-counts LIST
      One-bar element counts for many-elements.

  -sizes LIST
      Comma-separated WIDTHxHEIGHT values.

  -cases LIST
      flat
      raised
      stipple
      yerror
      many-elements

  -iterations N
      Measured iterations.

  -warmup N
      Warm-up iterations.

  -csv FILE
      Write long-format CSV.

  -help
      Show this message.

Examples:

  tclsh bars.tcl

  tclsh bars.tcl -profile stress

  tclsh bars.tcl -points 1000,10000,100000 -cases flat,raised

  tclsh bars.tcl -element-counts 1000,5000,10000 -cases many-elements
}
}

proc ::rbcBarsBenchmark::InitOptions {argv} {
    variable options
    set profile [::rbcBenchmark::FindProfile $argv standard]
    set options [dict merge [dict create profile $profile cases {flat raised stipple yerror many-elements} csv {}]\
                         [::rbcBenchmark::ProfileDefaults bars $profile]]
}

proc ::rbcBarsBenchmark::ParseArgs {argv} {
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
            -element-counts -
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
                    -element-counts {
                        dict set options element_counts [::rbcBenchmark::ParseList $value]
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
    dict set options points [::rbcBenchmark::ValidateCounts [dict get $options points] 1]
    dict set options element_counts [::rbcBenchmark::ValidateCounts [dict get $options element_counts] 1]
    dict set options sizes [::rbcBenchmark::ParseSizes [join [dict get $options sizes] ,]]
    ::rbcBenchmark::ValidateIterations [dict get $options iterations] [dict get $options warmup]
    foreach case [dict get $options cases] {
        if {$case ni {flat raised stipple yerror many-elements}} {
            error "unknown bar case \"$case\""
        }
    }
}

proc ::rbcBarsBenchmark::CreateVectors {n} {
    variable xVector
    variable yVector
    variable yError
    foreach vector [list $xVector $yVector $yError] {
        catch {::rbc::vector destroy $vector}
        ::rbc::vector create [format "%s(%d)" $vector $n] -variable {}
    }
    $xVector seq 0.5 end 1.0
    $yVector expr {0.55+0.35*sin(::rbcBarsX*0.031)+0.08*sin(::rbcBarsX*0.173)}
    $yError expr {0.02+0.01*abs(sin(::rbcBarsX*0.071))}
}

proc ::rbcBarsBenchmark::CreateGraph {} {
    variable top
    variable graph
    set ::rbcBenchmark::top $top
    set ::rbcBenchmark::graph $graph
    ::rbcBenchmark::CreateBareGraph ::rbc::barchart "RBC bar rendering benchmark"
    $graph axis configure x -hide yes -min 0.0 -max 1.0
    $graph axis configure y -hide yes -min 0.0 -max 1.05
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcBarsBenchmark::CreateSingleElement {caseName} {
    variable graph
    variable xVector
    variable yVector
    variable yError
    ::rbcBenchmark::DeleteAllElements
    set elementOptions [list -xdata $xVector -ydata $yVector -barwidth 0.8 -showvalues no]
    switch -- $caseName {
        flat {
            lappend elementOptions -relief flat -borderwidth 0 -foreground steelblue
        }
        raised {
            lappend elementOptions -relief raised -borderwidth 2 -foreground steelblue -background navy
        }
        stipple {
            lappend elementOptions -relief flat -borderwidth 0 -foreground steelblue -stipple gray50
        }
        yerror {
            lappend elementOptions -relief flat -borderwidth 0 -foreground steelblue -yerror $yError -showerrorbars y\
                    -errorbarwidth 1 -errorbarcap 5
        }
    }
    return [::rbcBenchmark::Time {
        $graph element create bars {*}$elementOptions
        ::rbcBenchmark::SyncDisplay
    }]
}

proc ::rbcBarsBenchmark::ManyElementValue {index} {
    return [expr {0.55+0.35*sin((double($index)+0.5)*0.031)+0.08*sin((double($index)+0.5)*0.173)}]
}

proc ::rbcBarsBenchmark::CreateManyElements {n} {
    variable graph
    ::rbcBenchmark::DeleteAllElements
    return [::rbcBenchmark::Time {
        for {set i 0} {$i < $n} {incr i} {
            set x [expr {double($i)+0.5}]
            set y [ManyElementValue $i]
            $graph element create e$i -x $x -y $y -barwidth 0.8 -relief flat -borderwidth 0 -foreground steelblue\
                    -showvalues no
        }
        ::rbcBenchmark::SyncDisplay
    }]
}

proc ::rbcBarsBenchmark::RunCase {caseName n width height csv manyElements} {
    variable options
    variable graph
    lassign [::rbcBenchmark::SetSize $width $height] actualWidth actualHeight
    set xmax [expr {double($n)}]
    $graph axis configure x -min 0.0 -max $xmax
    ::rbcBenchmark::SyncDisplay
    if {$manyElements} {
        set createMs [CreateManyElements $n]
    } else {
        set createMs [CreateSingleElement $caseName]
    }
    set warmup [dict get $options warmup]
    set iterations [dict get $options iterations]
    set redraw [::rbcBenchmark::Collect $warmup $iterations [list ::rbcBenchmark::MeasureRedraw]]
    set remap [::rbcBenchmark::CollectIndexed $warmup $iterations [list ::rbcBenchmark::MeasureAxisRemap x $xmax]]
    $graph axis configure x -max $xmax
    ::rbcBenchmark::SyncDisplay
    puts [format "%-16s %10d %6dx%-6d %6dx%-6d %11.3f %11.3f %11.3f" $caseName $n $width $height $actualWidth\
                  $actualHeight $createMs [dict get $redraw median] [dict get $remap median]]
    flush stdout
    ::rbcBenchmark::WriteStandardMetrics $csv bars $caseName $n $width $height $actualWidth $actualHeight $createMs\
            $redraw $remap
    ::rbcBenchmark::DeleteAllElements
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcBarsBenchmark::PrintHeader {} {
    ::rbcBenchmark::PrintEnvironment "RBC bar rendering benchmark"
    puts {
-bufferelements is disabled.

flat/raised/stipple/yerror use one bar element containing many bars.

many-elements creates one bar per element, stressing element traversal
and per-element renderer setup as well as rectangle drawing.

redraw-med is the primary renderer metric.

axis-remap includes mapping plus drawing.
}

    puts [format "%-16s %10s %13s %13s %11s %11s %11s" case bars requested actual create-ms redraw-med axis-remap]
    puts [string repeat - 98]
}

proc ::rbcBarsBenchmark::Cleanup {} {
    variable top
    variable xVector
    variable yVector
    variable yError
    catch {destroy $top}
    foreach vector [list $xVector $yVector $yError] {
        catch {::rbc::vector destroy $vector}
    }
}

proc ::rbcBarsBenchmark::Main {argv} {
    variable options
    InitOptions $argv
    ParseArgs $argv
    CreateGraph
    PrintHeader
    set csv [::rbcBenchmark::OpenLongCsv [dict get $options csv]]
    try {
        set ordinaryCases {}
        if {"flat" in [dict get $options cases]} {
            lappend ordinaryCases flat
        }
        if {"raised" in [dict get $options cases]} {
            lappend ordinaryCases raised
        }
        if {"stipple" in [dict get $options cases]} {
            lappend ordinaryCases stipple
        }
        if {"yerror" in [dict get $options cases]} {
            lappend ordinaryCases yerror
        }
        foreach n [dict get $options points] {
            if {[llength $ordinaryCases] == 0} {
                break
            }
            CreateVectors $n
            foreach caseName $ordinaryCases {
                foreach size [dict get $options sizes] {
                    lassign $size width height
                    RunCase $caseName $n $width $height $csv 0
                }
            }
        }
        if {"many-elements" in [dict get $options cases]} {
            foreach n [dict get $options element_counts] {
                foreach size [dict get $options sizes] {
                    lassign $size width height
                    RunCase many-elements $n $width $height $csv 1
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

if {[catch { ::rbcBarsBenchmark::Main $argv } message opts]} {
    puts stderr "benchmark failed: $message"
    if {[dict exists $opts -errorinfo]} {
        puts stderr [dict get $opts -errorinfo]
    }
    ::rbcBarsBenchmark::Cleanup
    exit 1
}

exit 0
