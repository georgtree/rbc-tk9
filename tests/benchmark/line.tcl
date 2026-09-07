#!/usr/bin/env tclsh

package require Tk
package require rbc

source [file join [file dirname [file normalize [info script]]] common.tcl]

namespace eval ::rbcBenchmark {
    variable options [dict create points {10000 100000 1000000 5000000} sizes {640x480 1280x720 1920x1080 2560x1440}\
                              iterations 3 warmup 1 decimate {none auto} stripchart 0 csv {}]

    variable xVector ::rbcBenchmarkX
    variable yVector ::rbcBenchmarkY
    variable top .rbcLineBenchmark
    variable graph .rbcLineBenchmark.g
    variable resultReport {}
}

proc ::rbcBenchmark::ParseArgs {argv} {
    variable options
    set parsed [argparse -inline -exact -long\
                        -help {Benchmark RBC line or strip elements, including redraw, remapping, ranged vector\
                                       updates, tail append, closest-point search, and optional display decimation.} {
            {-profile= -enum {smoke standard stress} -help {Select workload profile. If omitted, historical standalone\
                                                                    line.tcl defaults are retained}}
            {-stripchart -boolean -help {Benchmark a strip element in a stripchart}}
            {-points= -validate {[::rbcBenchmark::IsCountList $arg 2]} -errormsg {-points must contain integers >= 2}}
            {-sizes= -validate {[::rbcBenchmark::IsSizeList $arg]} -errormsg {-sizes must contain WIDTHxHEIGHT values}}
            {-iterations= -type integer -validate {$arg >= 1} -errormsg {-iterations must be >= 1}}
            {-warmup= -type integer -validate {$arg >= 0} -errormsg {-warmup must be >= 0}}
            {-decimate= -validate {[::rbcBenchmark::IsEnumList $arg {none auto}]}\
                     -errormsg {-decimate must contain none or auto}}
            {-csv= -default {} -help {Write benchmark results to CSV}}
        } $argv]
    # Keep the historical namespace defaults unless an explicit profile was supplied.
    if {[dict exists $parsed profile]} {
        set options [dict merge $options [ProfileDefaults line [dict get $parsed profile]]]
    }
    set options [dict merge $options $parsed]
    foreach key {points decimate} {
        dict set options $key [ParseList [dict get $options $key]]
    }
    dict set options sizes [ParseSizes [dict get $options sizes]]
}

proc ::rbcBenchmark::WaveformValue {x} {
    return [expr {0.60*sin($x*106.81415022205297)+0.25*sin($x*823.0972752405258)+0.10*sin($x*6264.335751258144)}]
}

# Generate the source data once per point count.
#
# Using RBC vectors is important: constructing a five-million-element
# Tcl list would benchmark Tcl object/list creation instead of graph
# rendering.
proc ::rbcBenchmark::CreateVectors {n} {
    variable xVector
    variable yVector
    #
    # Remove the previous Tcl-array mapping before destroying/recreating
    # the vector.
    #
    catch {$yVector variable {}}
    catch {unset ::rbcBenchmarkYData}
    catch {::rbc::vector destroy $xVector}
    catch {::rbc::vector destroy $yVector}
    ::rbc::vector create [format "%s(%d)" $xVector $n] -variable {}
    ::rbc::vector create [format "%s(%d)" $yVector $n] -variable {}
    #
    # Exactly n monotonically increasing X values from 0 to 1.
    #
    set step [expr {1.0 / double($n - 1)}]
    $xVector seq 0.0 end $step
    #
    # Deterministic waveform.
    #
    # It deliberately contains several frequency components so this
    # isn't simply a perfectly smooth low-frequency sine wave.
    #
    $yVector expr {0.60*sin(::rbcBenchmarkX*106.81415022205297)+0.25*sin(::rbcBenchmarkX*823.0972752405258)+\
                            0.10*sin(::rbcBenchmarkX*6264.335751258144)}
    #
    # Map the Y vector to a Tcl array only after initial data generation.
    # A write to one array element updates the existing vector in place
    # and sends the normal vector-change notification to graph clients.
    #
    $yVector variable ::rbcBenchmarkYData
}

proc ::rbcBenchmark::CreateGraph {} {
    variable options
    variable top
    variable graph
    set stripchart [dict get $options stripchart]
    wm withdraw .
    catch {destroy $top}
    toplevel $top
    if {$stripchart} {
        wm title $top "RBC strip element benchmark"
        set widgetCommand ::rbc::stripchart
    } else {
        wm title $top "RBC line element benchmark"
        set widgetCommand ::rbc::graph
    }
    wm overrideredirect $top 1
    $widgetCommand $graph -bufferelements 0 -buffergraph 1 -borderwidth 0 -highlightthickness 0 -plotborderwidth 0\
            -plotpadx 0 -plotpady 0 -title {}
    pack $graph -fill both -expand yes
    $graph legend configure -hide yes
    $graph grid configure -hide yes
    $graph axis configure x -hide yes -min 0.0 -max 1.0
    $graph axis configure y -hide yes -min -1.1 -max 1.1
    # The benchmark window must actually be mapped before measurements
    # start.  update, rather than update idletasks, is intentional here:
    # native Map/Configure events must be processed.
    wm geometry $top 640x480+0+0
    wm deiconify $top
    update
}

#
# First mapping + first complete drawing.
#
proc ::rbcBenchmark::CreateElement {mode} {
    variable options
    variable graph
    variable xVector
    variable yVector
    catch {$graph element delete signal}
    set elementOptions [list -xdata $xVector -ydata $yVector -linewidth 1 -symbol none -smooth linear -decimate $mode]
    # Ordinary line elements support -reduce.  Strip elements do not.
    if {![dict get $options stripchart]} {
        lappend elementOptions -reduce 0.0
    }
    return [Time {
        $graph element create signal {*}$elementOptions
        SyncDisplay
    }]
}

# Force remapping while changing the visible range by an insignificant
# amount.
#
# This exercises an axis-only remap.
#
# With -decimate none this maps the complete source data.
#
# With -decimate auto the persistent decimation cache should survive
# the axis change, and only the pixel-density representatives should
# require full world-to-screen mapping.
proc ::rbcBenchmark::MeasureRemap {iteration} {
    variable graph
    if {$iteration & 1} {
        set xmax 1.000001
    } else {
        set xmax 1.0
    }
    return [Time {
        $graph axis configure x -max $xmax
        SyncDisplay
    }]
}

#
# Measure remapping after an in-place source-data modification through
# the vector's mapped Tcl array.
#
# The vector length and storage remain unchanged.  With -decimate auto,
# a ranged Y update can preserve the persistent cache and refresh only
# affected block summaries.
#
proc ::rbcBenchmark::MeasureDataArrayRemap {index value} {
    variable graph
    return [Time {
        set ::rbcBenchmarkYData($index) $value
        SyncDisplay
    }]
}

#
# Measure the equivalent in-place source-data modification through the
# vector instance "index" command.
#
# This deliberately performs the same logical update as
# MeasureDataArrayRemap so the two notification/update paths can be
# compared directly.
#
proc ::rbcBenchmark::MeasureDataIndexRemap {index value} {
    variable graph
    variable yVector
    return [Time {
        $yVector index $index $value
        SyncDisplay
    }]
}

#
# Measure a one-sample tail append to both source vectors.
#
# X and Y are appended before SyncDisplay, so the timed interval includes:
#
#   vector growth
#   ranged vector notification
#   Stage-3 cache growth/invalidation
#   element remapping
#   redraw
#
# With -decimate auto, a pure monotonic tail append should extend the
# persistent data-domain cache without rebuilding its existing prefix.
#
proc ::rbcBenchmark::MeasureAppendRemap {x y} {
    variable xVector
    variable yVector
    return [Time {
        $xVector append $x
        $yVector append $y
        SyncDisplay
    }]
}

#
# Measure an exact source-point lookup.
#
# With -decimate none, the current implementation searches the complete
# mapped point array.
#
# With -decimate auto and a valid Stage-3 monotonic-X cache, the source
# candidate interval can be restricted by binary search before the
# remaining points are mapped and compared.
#
proc ::rbcBenchmark::MeasureClosest {queries iteration} {
    variable graph
    set query [lindex $queries [expr {$iteration % [llength $queries]}]]
    lassign $query px py
    unset -nocomplain closestInfo
    return [Time {
        set found [$graph element closest $px $py closestInfo -along both -interpolate 0 -halo 2 signal]
        if {!$found || ![info exists closestInfo(index)]} {
            error "closest lookup failed"
        }
    }]
}

# The requested toplevel width is not necessarily the actual X-axis
# drawable span.  Calculate the actual plot width from the axis.
proc ::rbcBenchmark::PlotWidth {} {
    variable graph
    set x0 [$graph axis transform x 0.0]
    set x1 [$graph axis transform x 1.0]
    set width [expr {abs(double($x1) - double($x0))}]
    if {$width < 1.0} {
        return 1.0
    }
    return $width
}

#
# Prepare several exact source-point queries for closest-point timing.
#
# The world-to-screen transformation is deliberately performed outside
# the measured interval.  This benchmark is intended to measure
# "element closest", not "graph transform".
#
proc ::rbcBenchmark::CreateClosestQueries {n} {
    variable graph
    variable xVector
    variable yVector
    set queries {}
    foreach fraction {0.2 0.5 0.8} {
        set index [expr {round(double($n - 1) * $fraction)}]
        set x [$xVector index $index]
        set y [$yVector index $index]
        lappend queries [$graph transform $x $y]
    }
    return $queries
}

proc ::rbcBenchmark::OpenCsv {} {
    variable options
    set path [dict get $options csv]
    if {$path eq {}} {
        return {}
    }
    set channel [open $path w]
    puts $channel [join {platform os os_version machine tcl tk rbc element_type decimate points requested_width\
                                 requested_height actual_width actual_height plot_width points_per_plot_pixel create_ms\
                                 redraw_min_ms redraw_median_ms redraw_mean_ms redraw_max_ms remap_min_ms\
                                 remap_median_ms remap_mean_ms remap_max_ms data_array_remap_min_ms\
                                 data_array_remap_median_ms data_array_remap_mean_ms data_array_remap_max_ms\
                                 data_index_remap_min_ms data_index_remap_median_ms data_index_remap_mean_ms\
                                 data_index_remap_max_ms append_remap_min_ms append_remap_median_ms\
                                 append_remap_mean_ms append_remap_max_ms closest_min_ms closest_median_ms\
                                 closest_mean_ms closest_max_ms} ,]
    return $channel
}

proc ::rbcBenchmark::WriteCsv {channel row} {
    if {$channel eq {}} {
        return
    }
    set values {}
    foreach key {platform os os_version machine tcl tk rbc element_type decimate points requested_width\
                         requested_height actual_width actual_height plot_width points_per_plot_pixel create_ms\
                         redraw_min_ms redraw_median_ms redraw_mean_ms redraw_max_ms remap_min_ms remap_median_ms\
                         remap_mean_ms remap_max_ms data_array_remap_min_ms data_array_remap_median_ms\
                         data_array_remap_mean_ms data_array_remap_max_ms data_index_remap_min_ms\
                         data_index_remap_median_ms data_index_remap_mean_ms data_index_remap_max_ms\
                         append_remap_min_ms append_remap_median_ms append_remap_mean_ms append_remap_max_ms\
                         closest_min_ms closest_median_ms closest_mean_ms closest_max_ms} {
        lappend values [CsvQuote [dict get $row $key]]
    }
    puts $channel [join $values ,]
    flush $channel
}

proc ::rbcBenchmark::PrintHeader {} {
    variable options
    if {[dict get $options stripchart]} {
        set elementType strip
    } else {
        set elementType line
    }
    puts {}
    puts "RBC $elementType element rendering benchmark"
    puts {}
    puts [format " %-10s %s" "platform:" $::tcl_platform(platform)]
    puts [format " %-10s %s %s" "OS:" $::tcl_platform(os) $::tcl_platform(osVersion)]
    puts [format " %-10s %s" "machine:" $::tcl_platform(machine)]
    puts [format " %-10s %s" "Tcl:" [info patchlevel]]
    puts [format " %-10s %s" "Tk:" [package provide Tk]]
    puts [format " %-10s %s" "RBC:" [package provide rbc]]
    puts [format " %-10s %dx%d depth=%d" "screen:" [winfo screenwidth .] [winfo screenheight .] [winfo screendepth .]]
    puts {}
    puts {
-bufferelements is disabled.

redraw:
    redraws already-mapped geometry.

axis-remap:
    changes the X axis slightly, forcing element geometry to be remapped
    and redrawn.  With -decimate auto, a persistent data-domain cache
    may be reused across this axis-only change.

array-remap:
    changes one existing Y-vector element in place through the vector's
    mapped Tcl array.  The timed interval includes ranged vector
    notification, cache maintenance, remapping, and redraw.

index-remap:
    performs the equivalent Y-vector modification through the vector
    instance "index" command.  It uses the same source index and values
    as array-remap so the two update paths are directly comparable.

append-remap:
    appends one new monotonically increasing X sample and its Y value
    to the ends of the two source vectors.  The timed interval includes
    vector growth, ranged notification, cache maintenance, remapping,
    and redraw.

    With -decimate auto, a pure tail append can extend the persistent
    Stage-3 cache instead of rebuilding its existing source prefix.

closest:
    performs an exact source-point search using:
        -along both -interpolate 0 -halo 2

    Query screen coordinates are calculated before timing.  The timed
    interval therefore measures element closest itself, including its
    result-array updates, but not graph transform or rendering.
}
}

proc ::rbcBenchmark::InitResultReport {} {
    variable resultReport
    set resultReport [NewReport {mode points requested actual pts/xpixel create-ms redraw-med axis-remap array-remap\
                                         index-remap append-remap closest-med}\
                              {left right right right right right right right right right right right}]
}

proc ::rbcBenchmark::RunCase {mode n width height csv} {
    variable options
    variable graph
    variable xVector
    variable yVector
    variable resultReport
    lassign [SetSize $width $height] actualWidth actualHeight
    # Always start from exactly the same axis range.
    $graph axis configure x -max 1.0
    SyncDisplay
    set createMs [CreateElement $mode]
    set plotWidth [PlotWidth]
    set density [expr { $n / double($plotWidth)}]
    set warmup [dict get $options warmup]
    set iterations [dict get $options iterations]
    set elementType [expr {[dict get $options stripchart] ? "strip" : "line"}]
    # Warm up redraw and axis-remap paths.
    for {set i 0} {$i < $warmup} {incr i} {
        MeasureRedraw
        MeasureRemap $i
    }
    # Restore the canonical axis range before measurements.
    $graph axis configure x -max 1.0
    SyncDisplay
    # Timed redraw runs.
    set redrawTimes {}
    for {set i 0} {$i < $iterations} {incr i} {
        lappend redrawTimes [MeasureRedraw]
    }
    # Timed axis-remap runs.
    set remapTimes {}
    for {set i 0} {$i < $iterations} {incr i} {
        lappend remapTimes [MeasureRemap $i]
    }
    # Restore precisely x=[0,1] before closest measurements.
    $graph axis configure x -max 1.0
    SyncDisplay
    set closestQueries [CreateClosestQueries $n]
    # Warm up the closest path separately.
    for {set i 0} {$i < $warmup} {incr i} {
        MeasureClosest $closestQueries $i
    }
    # Timed exact closest-point runs.
    set closestTimes {}
    for {set i 0} {$i < $iterations} {incr i} {
        lappend closestTimes [MeasureClosest $closestQueries $i]
    }
    # Prepare one in-place Y-vector modification shared by both source
    # update benchmarks.
    #
    # The point is near the middle of the source array.  The alternate
    # value differs only slightly so the visible waveform is effectively
    # unchanged; the purpose is to exercise notification, cache
    # maintenance, remapping, and redraw.
    set dataIndex [expr {round(double($n - 1) * 0.5)}]
    set dataOriginal [$yVector index $dataIndex]
    set dataAlternate [expr {double($dataOriginal) + 1.0e-6}]
    # --------------------------------------------------------------
    # Mapped Tcl-array update.
    # --------------------------------------------------------------
    #
    # Alternate explicitly so every invocation performs a real source
    # modification, regardless of warmup/iteration counts.
    set dataState 0

    for {set i 0} {$i < $warmup} {incr i} {
        set dataState [expr {!$dataState}]
        if {$dataState} {
            set value $dataAlternate
        } else {
            set value $dataOriginal
        }
        MeasureDataArrayRemap $dataIndex $value
    }
    set dataArrayRemapTimes {}
    for {set i 0} {$i < $iterations} {incr i} {
        set dataState [expr {!$dataState}]
        if {$dataState} {
            set value $dataAlternate
        } else {
            set value $dataOriginal
        }
        lappend dataArrayRemapTimes [MeasureDataArrayRemap $dataIndex $value]
    }
    # Restore the canonical source value before measuring the vector
    # instance command.  Do this outside the timed interval.
    if {$dataState} {
        set ::rbcBenchmarkYData($dataIndex) $dataOriginal
        SyncDisplay
    }
    # --------------------------------------------------------------
    # Vector "index" update.
    # --------------------------------------------------------------
    #
    # Start from the same canonical source value and use exactly the
    # same alternate value as the Tcl-array benchmark.
    set dataState 0
    for {set i 0} {$i < $warmup} {incr i} {
        set dataState [expr {!$dataState}]

        if {$dataState} {
            set value $dataAlternate
        } else {
            set value $dataOriginal
        }
        MeasureDataIndexRemap $dataIndex $value
    }
    set dataIndexRemapTimes {}
    for {set i 0} {$i < $iterations} {incr i} {
        set dataState [expr {!$dataState}]
        if {$dataState} {
            set value $dataAlternate
        } else {
            set value $dataOriginal
        }
        lappend dataIndexRemapTimes [MeasureDataIndexRemap $dataIndex $value]
    }
    # Restore the canonical waveform outside the timed interval so the
    # following graph size/mode starts with exactly the same source data.
    if {$dataState} {
        $yVector index $dataIndex $dataOriginal
        SyncDisplay
    }
    # --------------------------------------------------------------
    # Tail append.
    # --------------------------------------------------------------
    #
    # Continue the original monotonically increasing X sequence.
    #
    # The graph's visible X range deliberately remains fixed at [0,1].
    # The newly appended points therefore lie immediately beyond the
    # right edge.  This isolates source-growth/cache-maintenance cost
    # from a simultaneous axis-range change.
    #
    set appendStep [expr {1.0 / double($n - 1)}]
    set appendX [$xVector index end]
    # Warmup appends are retained while the timed appends run.  This
    # ensures timed iterations exercise steady-state incremental growth
    # rather than rebuilding state after a shrink.
    for {set i 0} {$i < $warmup} {incr i} {
        set appendX [expr {$appendX + $appendStep}]
        set appendY [WaveformValue $appendX]

        MeasureAppendRemap $appendX $appendY
    }
    set appendRemapTimes {}
    for {set i 0} {$i < $iterations} {incr i} {
        set appendX [expr {$appendX + $appendStep}]
        set appendY [WaveformValue $appendX]

        lappend appendRemapTimes \
            [MeasureAppendRemap $appendX $appendY]
    }
    # Main reuses these vectors for all size/mode combinations belonging
    # to the same nominal point count.  Restore their exact original
    # length outside the timed interval.
    $xVector length $n
    $yVector length $n
    SyncDisplay
    set redraw [Stats $redrawTimes]
    set remap [Stats $remapTimes]
    set dataArrayRemap [Stats $dataArrayRemapTimes]
    set dataIndexRemap [Stats $dataIndexRemapTimes]
    set appendRemap [Stats $appendRemapTimes]
    set closest [Stats $closestTimes]
    set row [dict create \
        platform                    $::tcl_platform(platform) \
        os                          $::tcl_platform(os) \
        os_version                  $::tcl_platform(osVersion) \
        machine                     $::tcl_platform(machine) \
        tcl                         [info patchlevel] \
        tk                          [package provide Tk] \
        rbc                         [package provide rbc] \
        element_type                $elementType \
        decimate                    $mode \
        points                      $n \
        requested_width             $width \
        requested_height            $height \
        actual_width                $actualWidth \
        actual_height               $actualHeight \
        plot_width                  [format %.0f $plotWidth] \
        points_per_plot_pixel       [format %.3f $density] \
        create_ms                   [format %.3f $createMs] \
        redraw_min_ms               [format %.3f [dict get $redraw min]] \
        redraw_median_ms            [format %.3f [dict get $redraw median]] \
        redraw_mean_ms              [format %.3f [dict get $redraw mean]] \
        redraw_max_ms               [format %.3f [dict get $redraw max]] \
        remap_min_ms                [format %.3f [dict get $remap min]] \
        remap_median_ms             [format %.3f [dict get $remap median]] \
        remap_mean_ms               [format %.3f [dict get $remap mean]] \
        remap_max_ms                [format %.3f [dict get $remap max]] \
        data_array_remap_min_ms     [format %.3f [dict get $dataArrayRemap min]] \
        data_array_remap_median_ms  [format %.3f [dict get $dataArrayRemap median]] \
        data_array_remap_mean_ms    [format %.3f [dict get $dataArrayRemap mean]] \
        data_array_remap_max_ms     [format %.3f [dict get $dataArrayRemap max]] \
        data_index_remap_min_ms     [format %.3f [dict get $dataIndexRemap min]] \
        data_index_remap_median_ms  [format %.3f [dict get $dataIndexRemap median]] \
        data_index_remap_mean_ms    [format %.3f [dict get $dataIndexRemap mean]] \
        data_index_remap_max_ms     [format %.3f [dict get $dataIndexRemap max]] \
        append_remap_min_ms         [format %.3f [dict get $appendRemap min]] \
        append_remap_median_ms      [format %.3f [dict get $appendRemap median]] \
        append_remap_mean_ms        [format %.3f [dict get $appendRemap mean]] \
        append_remap_max_ms         [format %.3f [dict get $appendRemap max]] \
        closest_min_ms              [format %.3f [dict get $closest min]] \
        closest_median_ms           [format %.3f [dict get $closest median]] \
        closest_mean_ms             [format %.3f [dict get $closest mean]] \
        closest_max_ms              [format %.3f [dict get $closest max]]]

    ReportAdd resultReport [list $mode $n "${width}x${height}" "${actualWidth}x${actualHeight}" [format %.1f $density]\
                                    [format %.3f $createMs] [format %.3f [dict get $redraw median]]\
                                    [format %.3f [dict get $remap median]]\
                                    [format %.3f [dict get $dataArrayRemap median]]\
                                    [format %.3f [dict get $dataIndexRemap median]]\
                                    [format %.3f [dict get $appendRemap median]]\
                                    [format %.3f [dict get $closest median]]]
    flush stdout
    WriteCsv $csv $row
    $graph element delete signal
    SyncDisplay
}

proc ::rbcBenchmark::Cleanup {} {
    variable top
    variable xVector
    variable yVector
    catch {destroy $top}
    catch {$yVector variable {}}
    catch {unset ::rbcBenchmarkYData}
    catch {::rbc::vector destroy $xVector}
    catch {::rbc::vector destroy $yVector}
}

proc ::rbcBenchmark::Main {argv} {
    variable options
    variable resultReport
    ParseArgs $argv
    CreateGraph
    InitResultReport
    PrintHeader
    set csv [OpenCsv]
    try {
        foreach n [dict get $options points] {
            # Data generation itself is deliberately outside the
            # graph-rendering timings.
            CreateVectors $n
            foreach mode [dict get $options decimate] {
                foreach size [dict get $options sizes] {
                    lassign $size width height
                    RunCase $mode $n $width $height $csv
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

if {[catch {::rbcBenchmark::Main $argv} message options]} {
    puts stderr "benchmark failed: $message"
    if {[dict exists $options -errorinfo]} {
        puts stderr [dict get $options -errorinfo]
    }
    ::rbcBenchmark::Cleanup
    exit 1
}

exit 0
