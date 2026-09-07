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
    variable resultReport {}
}

proc ::rbcSymbolsBenchmark::ParseArgs {argv} {
    variable options
    set parsed [argparse -inline -exact -long\
        -help {Benchmark RBC scatter/symbol rendering. Data generation is excluded from timed rendering intervals.} {
            {-profile= -enum {smoke standard stress} -default standard -help {Select benchmark workload profile}}
            {-points= -validate {[::rbcBenchmark::IsCountList $arg 2]}\
                     -errormsg {-points must contain integers >= 2} -help {Comma-separated source-point counts}}
            {-sizes= -validate {[::rbcBenchmark::IsSizeList $arg]}\
                     -errormsg {-sizes must contain WIDTHxHEIGHT values} -help {Comma-separated graph sizes}}
            {-symbols= -validate {[::rbcBenchmark::IsEnumList $arg {circle square diamond plus cross splus scross\
                                                                            triangle arrow}]}\
                     -errormsg {invalid symbol list} -help {Comma-separated symbol types}}
            {-pixels= -validate {[::rbcBenchmark::IsCountList $arg 1]} -errormsg {-pixels must contain integers >= 1}\
                     -help {Comma-separated symbol sizes in pixels}}
            {-trace -key trace -value 1 -default 1 -help {Also benchmark trace+circle cases}}
            {-no-trace -key trace -value 0 -help {Disable trace+circle cases}}
            {-iterations= -type integer -validate {$arg >= 1}\
                     -errormsg {-iterations must be >= 1} -help {Number of measured iterations}}
            {-warmup= -type integer -validate {$arg >= 0} -errormsg {-warmup must be >= 0} -help {Number of warm-up\
                                                                                                          iterations}}
            {-csv= -default {} -help {Write long-format CSV results to this file}}
        } $argv]
    set profile [dict get $parsed profile]
    set options \
    [dict merge [dict create symbols {circle square diamond plus} trace 1 csv {}] [::rbcBenchmark::ProfileDefaults\
                                                                                           symbols $profile] $parsed]
    foreach key {points symbols pixels} {
        dict set options $key [::rbcBenchmark::ParseList [dict get $options $key]]
    }
    dict set options sizes [::rbcBenchmark::ParseSizes [dict get $options sizes]]
    
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
    variable resultReport
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
    ::rbcBenchmark::ReportAdd resultReport [list $caseName $n "${width}x${height}" "${actualWidth}x${actualHeight}"\
                                                    [format %.3f $createMs] [format %.3f [dict get $redraw median]]\
                                                    [format %.3f [dict get $remap median]]]
    flush stdout
    ::rbcBenchmark::WriteStandardMetrics $csv symbols $caseName $n $width $height $actualWidth $actualHeight $createMs\
            $redraw $remap
    ::rbcBenchmark::DeleteAllElements
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcSymbolsBenchmark::PrintHeader {} {
    variable resultReport
    ::rbcBenchmark::PrintEnvironment "RBC symbol/scatter rendering benchmark"

    puts {
-bufferelements is disabled.

create-ms:
    creates the element, maps all source points, and performs its first draw.

redraw-med:
    redraws already-mapped symbol geometry. This is the primary
    renderer metric.

axis-remap:
    changes the X axis slightly, forcing mapping and drawing again.
}
}

proc ::rbcSymbolsBenchmark::InitResultReport {} {
    variable resultReport
    set resultReport [::rbcBenchmark::NewReport {case points requested actual create-ms redraw-med axis-remap}\
                              {left right right right right right right}]
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
    variable resultReport
    ParseArgs $argv
    CreateGraph
    InitResultReport
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
        ::rbcBenchmark::PrintReport $resultReport
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
