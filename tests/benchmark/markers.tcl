#!/usr/bin/env tclsh

package require Tk
package require rbc

source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcMarkersBenchmark {
    variable top .rbcMarkersBenchmark
    variable graph .rbcMarkersBenchmark.g
    variable imageName ::rbcMarkerBenchmarkImage
    variable options {}
}

proc ::rbcMarkersBenchmark::Usage {} {
    puts {Usage: markers.tcl ?options?

Options:

  -profile NAME
      smoke, standard, or stress.
      Default: standard

  -counts LIST
      Comma-separated marker counts.

  -sizes LIST
      Comma-separated WIDTHxHEIGHT values.

  -cases LIST
      line
      polygon-fill
      polygon-outline
      text-0
      text-45
      bitmap
      image

  -iterations N
      Measured iterations.

  -warmup N
      Warm-up iterations.

  -csv FILE
      Write long-format CSV.

  -help
      Show this message.

Examples:

  tclsh markers.tcl

  tclsh markers.tcl -profile stress

  tclsh markers.tcl -counts 1000,5000,10000 -cases line,polygon-fill,text-45
}
}

proc ::rbcMarkersBenchmark::InitOptions {argv} {
    variable options

    set profile [::rbcBenchmark::FindProfile $argv standard]

    set options [dict merge [dict create profile $profile\
                                     cases {line polygon-fill polygon-outline text-0 text-45 bitmap image} csv {}]\
                         [::rbcBenchmark::ProfileDefaults markers $profile]]
}

proc ::rbcMarkersBenchmark::ParseArgs {argv} {
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
            -counts -
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
                    -counts {
                        dict set options counts [::rbcBenchmark::ParseList $value]
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
                error "unknown option '$arg'; use -help"
            }
        }
    }
    dict set options counts [::rbcBenchmark::ValidateCounts [dict get $options counts] 1]
    dict set options sizes [::rbcBenchmark::ParseSizes [join [dict get $options sizes] ,]]
    ::rbcBenchmark::ValidateIterations [dict get $options iterations] [dict get $options warmup]
    foreach case [dict get $options cases] {
        if {$case ni {line polygon-fill polygon-outline text-0 text-45 bitmap image}} {
            error "unknown marker case '$case'"
        }
    }
}

proc ::rbcMarkersBenchmark::CreateGraph {} {
    variable top
    variable graph
    set ::rbcBenchmark::top $top
    set ::rbcBenchmark::graph $graph
    ::rbcBenchmark::CreateBareGraph ::rbc::graph "RBC marker rendering benchmark"
    $graph axis configure x -hide yes -min 0.0 -max 1.0
    $graph axis configure y -hide yes -min 0.0 -max 1.0
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcMarkersBenchmark::CreateImage {} {
    variable imageName
    catch { image delete $imageName }
    image create photo $imageName -width 8 -height 8
    $imageName put #4060a0 -to 0 0 8 8
    $imageName put #d0d8f0 -to 2 2 6 6
}

proc ::rbcMarkersBenchmark::CreateMarkers {caseName n} {
    variable graph
    variable imageName
    ::rbcBenchmark::DeleteAllMarkers
    return [::rbcBenchmark::Time {
        for {set i 0} {$i < $n} {incr i} {
            lassign [::rbcBenchmark::GridPosition $i $n] x y columns rows
            set dx [expr {0.30/double($columns)}]
            set dy [expr {0.30/double($rows)}]
            set name m$i
            switch -- $caseName {
                line {
                    $graph marker create line -name $name -coords [list [expr {$x-$dx}] [expr {$y-$dy}] [expr {$x+$dx}]\
                                                                           [expr {$y+$dy}]]\
                            -outline black -linewidth 1 -under no
                }
                polygon-fill {
                    $graph marker create polygon -name $name -coords [list $x [expr {$y-$dy}] [expr {$x+$dx}]\
                                                                              [expr {$y+$dy}] [expr {$x-$dx}]\
                                                                              [expr {$y+$dy}]]\
                            -fill steelblue -outline {} -linewidth 0 -under no
                }
                polygon-outline {
                    $graph marker create polygon -name $name -coords [list $x [expr {$y-$dy}] [expr {$x+$dx}]\
                                                                              [expr {$y+$dy}] [expr {$x-$dx}]\
                                                                              [expr {$y+$dy}]]\
                            -fill {} -outline black -linewidth 1 -under no
                }
                text-0 {
                    $graph marker create text -name $name -coords [list $x $y] -text M123 -font TkSmallCaptionFont\
                            -rotate 0 -anchor center -under no
                }
                text-45 {
                    $graph marker create text -name $name -coords [list $x $y] -text M123 -font TkSmallCaptionFont\
                            -rotate 45 -anchor center -under no
                }
                bitmap {
                    $graph marker create bitmap -name $name -coords [list $x $y] -bitmap questhead -anchor center\
                            -under no
                }
                image {
                    $graph marker create image -name $name -coords [list $x $y] -image $imageName -anchor center\
                            -under no
                }
            }
        }
        ::rbcBenchmark::SyncDisplay
    }]
}

proc ::rbcMarkersBenchmark::RunCase {caseName n width height csv} {
    variable options
    variable graph
    lassign [::rbcBenchmark::SetSize $width $height] actualWidth actualHeight
    $graph axis configure x -max 1.0
    ::rbcBenchmark::SyncDisplay
    set createMs [CreateMarkers $caseName $n]
    set warmup [dict get $options warmup]
    set iterations [dict get $options iterations]
    set redraw [::rbcBenchmark::Collect $warmup $iterations [list ::rbcBenchmark::MeasureRedraw]]
    set remap [::rbcBenchmark::CollectIndexed $warmup $iterations [list ::rbcBenchmark::MeasureAxisRemap x 1.0]]
    $graph axis configure x -max 1.0
    ::rbcBenchmark::SyncDisplay
    puts [format "%-18s %10d %6dx%-6d %6dx%-6d %11.3f %11.3f %11.3f" $caseName $n $width $height $actualWidth\
                  $actualHeight $createMs [dict get $redraw median] [dict get $remap median]]
    flush stdout
    ::rbcBenchmark::WriteStandardMetrics $csv markers $caseName $n $width $height $actualWidth $actualHeight $createMs\
            $redraw $remap
    ::rbcBenchmark::DeleteAllMarkers
    ::rbcBenchmark::SyncDisplay
}

proc ::rbcMarkersBenchmark::PrintHeader {} {
    ::rbcBenchmark::PrintEnvironment "RBC marker rendering benchmark"
    puts {
-bufferelements is disabled.

Each case creates many markers of one concrete type.

Data preparation is outside timing.

create-ms includes marker creation, mapping, and first display.

redraw-med measures already-mapped marker drawing and is the primary
renderer metric.

Window markers are intentionally excluded: large numbers of child Tk
windows are primarily a Tk/window-management benchmark rather than a
graph renderer benchmark.
}

    puts [format "%-18s %10s %13s %13s %11s %11s %11s" case markers requested actual create-ms redraw-med axis-remap]
    puts [string repeat - 100]
}

proc ::rbcMarkersBenchmark::Cleanup {} {
    variable top
    variable imageName
    catch {destroy $top}
    catch {image delete $imageName}
}

proc ::rbcMarkersBenchmark::Main {argv} {
    variable options
    InitOptions $argv
    ParseArgs $argv
    CreateGraph
    CreateImage
    PrintHeader
    set csv [::rbcBenchmark::OpenLongCsv [dict get $options csv]]
    try {
        foreach n [dict get $options counts] {
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

if {[catch {::rbcMarkersBenchmark::Main $argv} message opts]} {
    puts stderr "benchmark failed: $message"
    if {[dict exists $opts -errorinfo]} {
        puts stderr [dict get $opts -errorinfo]
    }
    ::rbcMarkersBenchmark::Cleanup
    exit 1
}

exit 0
