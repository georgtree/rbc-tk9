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
    variable resultReport {}
}

proc ::rbcBarsBenchmark::ParseArgs {argv} {
    variable options
    set parsed [argparse -inline -exact -long\
                        -help {Benchmark RBC bar rendering using both large single bar elements and large numbers of\
                                       independent bar elements.} {
            {-profile= -enum {smoke standard stress} -default standard -help {Select benchmark workload profile}}
            {-points= -validate {[::rbcBenchmark::IsCountList $arg 1]} -errormsg {-points must contain integers >= 1}\
                     -help {Bar counts for single-element workloadso}}
            {-element-counts= -key element_counts -validate {[::rbcBenchmark::IsCountList $arg 1]}\
                     -errormsg {-element-counts must contain integers >= 1}\
                     -help {Element counts for many-elements workload}}
            {-sizes= -validate {[::rbcBenchmark::IsSizeList $arg]} -errormsg {-sizes must contain WIDTHxHEIGHT values}}
            {-cases= -validate {[::rbcBenchmark::IsEnumList $arg {flat raised stipple yerror many-elements}]}\
                     -errormsg {invalid bar benchmark case} -help {Comma-separated bar workloads}}
            {-iterations= -type integer -validate {$arg >= 1} -errormsg {-iterations must be >= 1}}
            {-warmup= -type integer -validate {$arg >= 0} -errormsg {-warmup must be >= 0}}
            {-csv= -default {} -help {Write long-format CSV results}}
        } $argv]
    set profile [dict get $parsed profile]
    set options [dict merge [dict create cases {flat raised stipple yerror many-elements} csv {}]\
                         [::rbcBenchmark::ProfileDefaults bars $profile] $parsed]
    foreach key {points element_counts cases} {
        dict set options $key [::rbcBenchmark::ParseList [dict get $options $key]]
    }
    dict set options sizes [::rbcBenchmark::ParseSizes [dict get $options sizes]]
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
    variable resultReport
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
    ::rbcBenchmark::ReportAdd resultReport [list $caseName $n "${width}x${height}" "${actualWidth}x${actualHeight}"\
                                                    [format %.3f $createMs] [format %.3f [dict get $redraw median]]\
                                                    [format %.3f [dict get $remap median]]]
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
}

proc ::rbcBarsBenchmark::InitResultReport {} {
    variable resultReport
    set resultReport [::rbcBenchmark::NewReport {case bars requested actual create-ms redraw-med axis-remap}\
                              {left right right right right right right}]
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
    variable resultReport
    ParseArgs $argv
    CreateGraph
    InitResultReport
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
        ::rbcBenchmark::PrintReport $resultReport
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
