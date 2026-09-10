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
    variable resultReport {}
}


proc ::rbcErrorBenchmark::ParseArgs {argv} {
    variable options
    set parsed [argparse -inline -exact -long\
        -help {Benchmark RBC error-bar rendering, including workloads dominated by large numbers of independent line\
                       segments.} {
            {-renderer= -enum {native cairo} -default native -help {Select the graph renderer}}
            {-antialias= -enum {default none gray} -default default -help {Select Cairo antialiasing}}
            {-profile= -enum {smoke standard stress} -default standard -help {Select benchmark workload profile}}
            {-points= -validate {[::rbcBenchmark::IsCountList $arg 2]} -errormsg {-points must contain integers >= 2}\
                     -help {Comma-separated source-point counts}}
            {-sizes= -validate {[::rbcBenchmark::IsSizeList $arg]} -errormsg {-sizes must contain WIDTHxHEIGHT values}\
                     -help {Comma-separated graph sizes}}
            {-cases= -validate {[::rbcBenchmark::IsEnumList $arg {y xy y+circle}]} -errormsg {-cases must contain y,\
                                                                                                      xy, or y+circle}\
                     -help {Comma-separated error-bar workloads}}
            {-iterations= -type integer -validate {$arg >= 1} -errormsg {-iterations must be >= 1}}
            {-warmup= -type integer -validate {$arg >= 0} -errormsg {-warmup must be >= 0}}\
            {-csv= -default {} -help {Write long-format CSV results}}
        } $argv]
    ::rbcBenchmark::SetRendererOptions $parsed
    set profile [dict get $parsed profile]
    set options [dict merge [dict create cases {y xy y+circle} csv {}] [::rbcBenchmark::ProfileDefaults errorbars\
                                                                                $profile] $parsed]
    foreach key {points cases} {
        dict set options $key [::rbcBenchmark::ParseList [dict get $options $key]]
    }
    dict set options sizes [::rbcBenchmark::ParseSizes [dict get $options sizes]]
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
    variable resultReport
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
    ::rbcBenchmark::ReportAdd resultReport [list $caseName $n "${width}x${height}" "${actualWidth}x${actualHeight}"\
                                                    [format %.3f $createMs] [format %.3f [dict get $redraw median]]\
                                                    [format %.3f [dict get $remap median]]]
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
}

proc ::rbcErrorBenchmark::InitResultReport {} {
    variable resultReport
    set resultReport [::rbcBenchmark::NewReport {case points requested actual create-ms redraw-med axis-remap}\
                              {left right right right right right right}]
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
    variable resultReport
    ParseArgs $argv
    CreateGraph
    InitResultReport
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
        ::rbcBenchmark::PrintReport $resultReport
    } finally {
        if {$csv ne {}} {
            close $csv
        }
        Cleanup
    }
}

if {[catch {::rbcErrorBenchmark::Main $argv} message opts]} {
    puts stderr "benchmark failed: $message"
    if {[dict exists $opts -errorinfo]} {
        puts stderr [dict get $opts -errorinfo]
    }
    ::rbcErrorBenchmark::Cleanup
    exit 1
}

exit 0
