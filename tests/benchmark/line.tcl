#!/usr/bin/env tclsh

package require Tk
package require rbc

namespace eval ::rbcBenchmark {
    variable options [dict create \
        points     {10000 100000 1000000 5000000} \
        sizes      {640x480 1280x720 1920x1080 2560x1440} \
        iterations 3 \
        warmup     1 \
        decimate   {none} \
        csv        {}]

    variable xVector ::rbcBenchmarkX
    variable yVector ::rbcBenchmarkY
    variable top   .rbcLineBenchmark
    variable graph .rbcLineBenchmark.g
}

proc ::rbcBenchmark::Usage {} {
    puts {Usage: line.tcl ?options?

Options:

  -points LIST
      Comma-separated point counts.

      Default:
          10000,100000,1000000,5000000

  -sizes LIST
      Comma-separated graph sizes.

      Default:
          640x480,1280x720,1920x1080,2560x1440

  -iterations N
      Number of measured iterations.
      Default: 3

  -warmup N
      Number of warm-up iterations.
      Default: 1

  -decimate LIST
      Decimation modes.

      Default:
          none

      Once -decimate is implemented:

          -decimate none,auto

      can benchmark both implementations.

  -csv FILE
      Also write results as CSV.

  -help
      Show this message.

Examples:

  tclsh line.tcl

  tclsh line.tcl -points 100000,1000000 -sizes 800x600,1920x1080

  tclsh line.tcl -iterations 5 -csv benchmark.csv

  tclsh line.tcl -decimate none,auto
}
}

proc ::rbcBenchmark::ParseList {value} {
    set result {}
    foreach item [split $value ,] {
        set item [string trim $item]
        if {$item ne {}} {
            lappend result $item
        }
    }
    return $result
}

proc ::rbcBenchmark::ParseArgs {argv} {
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
            -points -
            -sizes -
            -iterations -
            -warmup -
            -decimate -
            -csv {
                incr i
                if {$i >= [llength $argv]} {
                    error "missing value for $arg"
                }
                set value [lindex $argv $i]
                switch -- $arg {
                    -points {
                        dict set options points [ParseList $value]
                    }
                    -sizes {
                        dict set options sizes [ParseList $value]
                    }
                    -iterations {
                        dict set options iterations $value
                    }
                    -warmup {
                        dict set options warmup $value
                    }
                    -decimate {
                        dict set options decimate [ParseList $value]
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
    ### Validate iterations.
    foreach name {iterations warmup} {
        set value [dict get $options $name]
        if {![string is integer -strict $value] || $value < 0} {
            error "-$name must be a non-negative integer"
        }
    }
    if {[dict get $options iterations] < 1} {
        error "-iterations must be at least 1"
    }

    ### Validate point counts.
    set checked {}
    foreach n [dict get $options points] {
        if {![string is entier -strict $n] || $n < 2} {
            error "point count \"$n\" must be an integer >= 2"
        }
        lappend checked $n
    }
    if {[llength $checked] == 0} {
        error "-points must contain at least one value"
    }
    dict set options points $checked
    ### Validate requested graph dimensions.
    set checked {}
    foreach size [dict get $options sizes] {
        if {![regexp {^([1-9][0-9]*)x([1-9][0-9]*)$} $size -> width height]} {
            error "size \"$size\" must have the form WIDTHxHEIGHT"
        }
        lappend checked [list $width $height]
    }
    if {[llength $checked] == 0} {
        error "-sizes must contain at least one size"
    }
    dict set options sizes $checked
    ### We already reserve both values for the coming implementation.
    set checked {}
    foreach mode [dict get $options decimate] {
        if {$mode ni {none auto}} {
            error "decimation mode must be none or auto"
        }
        lappend checked $mode
    }
    dict set options decimate $checked
}

proc ::rbcBenchmark::Mean {values} {
    set sum 0.0
    foreach value $values {
        set sum [expr {$sum + $value}]
    }
    return [expr {$sum / double([llength $values])}]
}

proc ::rbcBenchmark::Median {values} {
    set values [lsort -real $values]
    set n [llength $values]
    set middle [expr {$n / 2}]
    if {$n & 1} {
        return [lindex $values $middle]
    }
    return [expr {([lindex $values [expr {$middle - 1}]] + [lindex $values $middle]) / 2.0}]
}

proc ::rbcBenchmark::Stats {values} {
    set values [lsort -real $values]
    return [dict create min [lindex $values 0] median [Median $values] mean [Mean $values] max [lindex $values end]]
}

proc ::rbcBenchmark::SyncDisplay {} {
    variable graph
    # RBC redraws are normally scheduled using an idle callback.
    update idletasks
    # On X11 this causes a server round trip.  This is important:
    # otherwise XDrawLines() may merely enqueue requests and our timer
    # would stop before the X server had consumed them.
    #
    # On Windows this is a cheap native query.
    catch {winfo pointerx $graph}
}

proc ::rbcBenchmark::Time {script} {
    set start [clock microseconds]
    uplevel 1 $script
    set stop [clock microseconds]
    return [expr {($stop - $start)/1000.0}]
}

# Generate the source data once per point count.
#
# Using RBC vectors is important: constructing a five-million-element
# Tcl list would benchmark Tcl object/list creation instead of graph
# rendering.
proc ::rbcBenchmark::CreateVectors {n} {
    variable xVector
    variable yVector
    catch {::rbc::vector destroy $xVector}
    catch {::rbc::vector destroy $yVector}
    ::rbc::vector create [format "%s(%d)" $xVector $n] -variable {}
    ::rbc::vector create [format "%s(%d)" $yVector $n] -variable {}
    # Exactly n monotonically increasing X values from 0 to 1.
    set step [expr {1.0 / double($n - 1)}]
    $xVector seq 0.0 end $step
    # Deterministic waveform.
    #
    # It deliberately contains several frequency components so this
    # isn't simply a perfectly smooth low-frequency sine wave.
    $yVector expr {0.60*sin(::rbcBenchmarkX*106.81415022205297)+0.25*sin(::rbcBenchmarkX*823.0972752405258)+\
                           0.10*sin(::rbcBenchmarkX*6264.335751258144)}
}

proc ::rbcBenchmark::CreateGraph {} {
    variable top
    variable graph
    wm withdraw .
    catch {destroy $top}
    toplevel $top
    wm title $top "RBC line benchmark"
    wm overrideredirect $top 1
    ::rbc::graph $graph -bufferelements 0 -buffergraph 1 -borderwidth 0 -highlightthickness 0 -plotborderwidth 0\
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

proc ::rbcBenchmark::SetSize {width height} {
    variable top
    variable graph
    wm geometry $top [format "%dx%d+0+0" $width $height]
    # Process native Configure events.  update idletasks alone does not
    # guarantee that winfo width/height reflects the requested native
    # window size.
    update
    return [list [winfo width $graph] [winfo height $graph]]
}

# This deliberately works both before and after -decimate is added.
proc ::rbcBenchmark::ConfigureDecimate {mode} {
    variable graph

    if {![catch {$graph element cget signal -decimate}]} {
        $graph element configure signal -decimate $mode
        return
    }
    # Current RBC has no -decimate option yet.
    if {$mode ne "none"} {
        error "this RBC build does not provide -decimate yet; use -decimate none"
    }
}

#
# First mapping + first complete drawing.
#
proc ::rbcBenchmark::CreateElement {mode} {
    variable graph
    variable xVector
    variable yVector
    catch {$graph element delete signal}
    return [Time {
        $graph element create signal -xdata $xVector -ydata $yVector -linewidth 1 -symbol none -smooth linear\
                -reduce 0.0
        ConfigureDecimate $mode
        SyncDisplay
    }]
}

# Redraw already mapped geometry.
#
# Because -bufferelements is off, RBC cannot satisfy this simply by
# copying its cached element backing pixmap.
proc ::rbcBenchmark::MeasureRedraw {} {
    variable graph
    return [Time {
        event generate $graph <Expose>
        SyncDisplay
    }]
}

# Force remapping while changing the visible range by an insignificant
# amount.
#
# This exercises:
#
#       GetScreenPoints
#       MapTraces
#       renderer
#
# without meaningfully changing how much of the waveform is visible.
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

proc ::rbcBenchmark::OpenCsv {} {
    variable options
    set path [dict get $options csv]
    if {$path eq {}} {
        return {}
    }
    set channel [open $path w]
    puts $channel [join {platform os os_version machine tcl tk rbc decimate points requested_width requested_height\
                                 actual_width actual_height plot_width points_per_plot_pixel create_ms redraw_min_ms\
                                 redraw_median_ms redraw_mean_ms redraw_max_ms remap_min_ms remap_median_ms\
                                 remap_mean_ms remap_max_ms} ,]
    return $channel
}

proc ::rbcBenchmark::CsvQuote {value} {
    set value [string map {\" \"\"} $value]
    return "\"$value\""
}

proc ::rbcBenchmark::WriteCsv {channel row} {
    if {$channel eq {}} {
        return
    }
    set values {}
    foreach key {platform os os_version machine tcl tk rbc decimate points requested_width requested_height\
                         actual_width actual_height plot_width points_per_plot_pixel create_ms redraw_min_ms\
                         redraw_median_ms redraw_mean_ms redraw_max_ms remap_min_ms remap_median_ms remap_mean_ms\
                         remap_max_ms} {
        lappend values [CsvQuote [dict get $row $key]]
    }
    puts $channel [join $values ,]
    flush $channel
}

proc ::rbcBenchmark::PrintHeader {} {
    variable options
    puts {}
    puts {RBC line rendering benchmark}
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

remap:
    changes the X axis slightly, therefore remapping and redrawing
    the complete line.
}

    puts [format "%-8s %10s %13s %13s %11s %11s %12s %12s" mode points requested actual pts/xpixel create-ms redraw-med\
                  remap-med]
    puts [string repeat - 102]
}

proc ::rbcBenchmark::RunCase {mode n width height csv} {
    variable options
    variable graph
    lassign [SetSize $width $height] actualWidth actualHeight
    # Always start from exactly the same axis range.
    $graph axis configure x -max 1.0
    SyncDisplay
    set createMs [CreateElement $mode]
    set plotWidth [PlotWidth]
    set density [expr {$n / double($plotWidth)}]
    # Warm-up.
    set warmup [dict get $options warmup]
    for {set i 0} {$i < $warmup} {incr i} {
        MeasureRedraw
        MeasureRemap $i
    }
    $graph axis configure x -max 1.0
    SyncDisplay
    # Timed redraw runs.
    set redrawTimes {}
    set iterations [dict get $options iterations]
    for {set i 0} {$i < $iterations} {incr i} {
        lappend redrawTimes [MeasureRedraw]
    }
    # Timed remap runs.
    set remapTimes {}
    for {set i 0} {$i < $iterations} {incr i} {
        lappend remapTimes [MeasureRemap $i]
    }
    set redraw [Stats $redrawTimes]
    set remap [Stats $remapTimes]
    set row [dict create\
        platform              $::tcl_platform(platform)\
        os                    $::tcl_platform(os)\
        os_version            $::tcl_platform(osVersion)\
        machine               $::tcl_platform(machine)\
        tcl                   [info patchlevel]\
        tk                    [package provide Tk]\
        rbc                   [package provide rbc]\
        decimate              $mode\
        points                $n\
        requested_width       $width\
        requested_height      $height\
        actual_width          $actualWidth\
        actual_height         $actualHeight\
        plot_width            [format %.0f $plotWidth]\
        points_per_plot_pixel [format %.3f $density]\
        create_ms             [format %.3f $createMs]\
        redraw_min_ms         [format %.3f [dict get $redraw min]]\
        redraw_median_ms      [format %.3f [dict get $redraw median]]\
        redraw_mean_ms        [format %.3f [dict get $redraw mean]]\
        redraw_max_ms         [format %.3f [dict get $redraw max]]\
        remap_min_ms          [format %.3f [dict get $remap min]]\
        remap_median_ms       [format %.3f [dict get $remap median]]\
        remap_mean_ms         [format %.3f [dict get $remap mean]]\
        remap_max_ms          [format %.3f [dict get $remap max]]]
    puts [format "%-8s %10d %6dx%-6d %6dx%-6d %11.1f %11.3f %12.3f %12.3f" $mode $n $width $height $actualWidth\
                  $actualHeight $density $createMs [dict get $redraw median] [dict get $remap median]]
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
    catch {::rbc::vector destroy $xVector}
    catch {::rbc::vector destroy $yVector}
}

proc ::rbcBenchmark::Main {argv} {
    variable options
    ParseArgs $argv
    CreateGraph
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
    } finally {
        if {$csv ne {}} {
            close $csv
        }
        Cleanup
    }
}

if {[catch {::rbcBenchmark::Main $argv } message options]} {
    puts stderr "benchmark failed: $message"
    if {[dict exists $options -errorinfo]} {
        puts stderr [dict get $options -errorinfo]
    }
    ::rbcBenchmark::Cleanup
    exit 1
}

exit 0
