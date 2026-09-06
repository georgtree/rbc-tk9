# Common support for RBC rendering benchmarks.
#
# Workload scripts set ::rbcBenchmark::top and ::rbcBenchmark::graph.
# The helpers in this file deliberately do not create benchmark data;
# data generation belongs to each workload and remains outside timed
# rendering intervals.

namespace eval ::rbcBenchmark {
    variable renderer native

    # This can later be set to "cairo", "gdi", etc. without changing
    # the CSV schema.
    if {[info exists ::env(RBC_BENCH_RENDERER)] && $::env(RBC_BENCH_RENDERER) ne {}} {
        set renderer $::env(RBC_BENCH_RENDERER)
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

proc ::rbcBenchmark::ParseSizes {value} {
    set result {}
    foreach size [ParseList $value] {
        if {![regexp {^([1-9][0-9]*)x([1-9][0-9]*)$} $size -> width height]} {
            error "size '$size' must have the form WIDTHxHEIGHT"
        }
        lappend result [list $width $height]
    }
    if {[llength $result] == 0} {
        error "size list must contain at least one value"
    }
    return $result
}

proc ::rbcBenchmark::ValidateCounts {values {minimum 1}} {
    set result {}
    foreach value $values {
        if {![string is entier -strict $value] || $value < $minimum} {
            error "count '$value' must be an integer >= $minimum"
        }
        lappend result $value
    }
    if {[llength $result] == 0} {
        error "count list must contain at least one value"
    }
    return $result
}

proc ::rbcBenchmark::ValidateIterations {iterations warmup} {
    if {![string is integer -strict $iterations] || $iterations < 1} {
        error "-iterations must be an integer >= 1"
    }
    if {![string is integer -strict $warmup] || $warmup < 0} {
        error "-warmup must be a non-negative integer"
    }
}

proc ::rbcBenchmark::FindProfile {argv {default {}}} {
    set profile $default
    for {set i 0} {$i < [llength $argv]} {incr i} {
        if {[lindex $argv $i] eq "-profile"} {
            incr i
            if {$i >= [llength $argv]} {
                error "missing value for -profile"
            }
            set profile [lindex $argv $i]
        }
    }
    if {$profile ne {} && $profile ni {smoke standard stress}} {
        error "profile must be smoke, standard, or stress"
    }
    return $profile
}

proc ::rbcBenchmark::ProfileDefaults {family profile} {
    switch -- $family {
        line {
            switch -- $profile {
                smoke {
                    return [dict create points {10000 100000} sizes {640x480} iterations 1 warmup 1]
                }
                standard {
                    return [dict create points {10000 100000 1000000} sizes {640x480 1920x1080} iterations 3 warmup 1]
                }
                stress {
                    return [dict create points {10000 100000 1000000 5000000}\
                                    sizes {640x480 1280x720 1920x1080 2560x1440} iterations 5 warmup 1]
                }
            }
        }
        symbols {
            switch -- $profile {
                smoke {
                    return [dict create points {1000 10000} sizes {640x480} iterations 1 warmup 1 pixels 7]
                }
                standard {
                    return [dict create points {1000 10000 100000 500000} sizes {640x480 1920x1080} iterations 3\
                                    warmup 1 pixels 7]
                }
                stress {
                    return [dict create points {1000 10000 100000 500000 1000000}\
                                    sizes {640x480 1280x720 1920x1080 2560x1440} iterations 5 warmup 1 pixels {3 7 15}]
                }
            }
        }
        errorbars {
            switch -- $profile {
                smoke {
                    return [dict create points {1000 10000} sizes {640x480} iterations 1 warmup 1]
                }
                standard {
                    return [dict create points {1000 10000 100000 500000} sizes {640x480 1920x1080} iterations 3\
                                    warmup 1]
                }
                stress {
                    return [dict create points {1000 10000 100000 500000 1000000}\
                                    sizes {640x480 1280x720 1920x1080 2560x1440} iterations 5 warmup 1]
                }
            }
        }
        bars {
            switch -- $profile {
                smoke {
                    return [dict create points {100 1000} element_counts {100 1000} sizes {640x480} iterations 1\
                                    warmup 1]
                }
                standard {
                    return [dict create points {100 1000 10000 50000} element_counts {100 1000 5000}\
                                    sizes {640x480 1920x1080} iterations 3 warmup 1]
                }
                stress {
                    return [dict create points {100 1000 10000 50000 100000} element_counts {100 1000 5000 10000}\
                                    sizes {640x480 1280x720 1920x1080 2560x1440} iterations 5 warmup 1]
                }
            }
        }
        markers {
            switch -- $profile {
                smoke {
                    return [dict create counts {100 1000} sizes {640x480} iterations 1 warmup 1]
                }
                standard {
                    return [dict create counts {100 1000 5000 10000} sizes {640x480 1920x1080} iterations 3 warmup 1]
                }
                stress {
                    return [dict create counts {100 1000 5000 10000 20000}\
                                    sizes {640x480 1280x720 1920x1080 2560x1440} iterations 5 warmup 1]
                }
            }
        }
        mixed {
            switch -- $profile {
                smoke {
                    return [dict create scales {1} sizes {640x480} iterations 1 warmup 1]
                }
                standard {
                    return [dict create scales {1 2} sizes {640x480 1920x1080} iterations 3 warmup 1]
                }
                stress {
                    return [dict create scales {1 2 4} sizes {640x480 1280x720 1920x1080 2560x1440} iterations 5\
                                    warmup 1]
                }
            }
        }
        default {
            error "unknown benchmark family \"$family\""
        }
    }
    error "unknown profile \"$profile\""
}

proc ::rbcBenchmark::Mean {values} {
    set sum 0.0
    foreach value $values {
        set sum [expr {$sum + $value}]
    }
    return [expr {$sum/double([llength $values])}]
}

proc ::rbcBenchmark::Median {values} {
    set values [lsort -real $values]
    set n [llength $values]
    set middle [expr {$n/2}]
    if {$n & 1} {
        return [lindex $values $middle]
    }
    return [expr {([lindex $values [expr {$middle-1}]]+[lindex $values $middle])/2.0}]
}

proc ::rbcBenchmark::Stats {values} {
    if {[llength $values] == 0} {
        error "can't calculate statistics of an empty sample"
    }
    set values [lsort -real $values]
    return [dict create min [lindex $values 0] median [Median $values] mean [Mean $values] max [lindex $values end]]
}

proc ::rbcBenchmark::ScalarStats {value} {
    return [dict create min $value median $value mean $value max $value]
}

proc ::rbcBenchmark::Time {script} {
    set start [clock microseconds]
    uplevel 1 $script
    set stop [clock microseconds]
    return [expr {($stop-$start)/1000.0}]
}

proc ::rbcBenchmark::SyncDisplay {{widget {}}} {
    variable graph
    if {$widget eq {}} {
        set widget $graph
    }
    # RBC redraws are normally scheduled using an idle callback.
    update idletasks
    # Force an X11 server round trip so X drawing requests are consumed
    # before the timer stops.
    #
    # On Windows this is a cheap native query.
    catch {winfo pointerx $widget}
}

proc ::rbcBenchmark::SetSize {width height} {
    variable top
    variable graph
    wm geometry $top [format "%dx%d+0+0" $width $height]
    # Native Configure events must be consumed before querying the
    # actual child size.
    update
    return [list [winfo width $graph] [winfo height $graph]]
}

proc ::rbcBenchmark::MeasureRedraw {} {
    variable graph
    return [Time {
        event generate $graph <Expose>
        SyncDisplay
    }]
}

proc ::rbcBenchmark::MeasureAxisRemap {axis canonicalMax iteration} {
    variable graph
    set delta [expr {max(abs(double($canonicalMax)),1.0)*1.0e-6}]
    if {$iteration & 1} {
        set value [expr {double($canonicalMax)+$delta}]
    } else {
        set value $canonicalMax
    }
    return [Time {
        $graph axis configure $axis -max $value
        SyncDisplay
    }]
}

proc ::rbcBenchmark::Collect {warmup iterations commandPrefix} {
    for {set i 0} {$i < $warmup} {incr i} {
        uplevel #0 $commandPrefix
    }
    set values {}
    for {set i 0} {$i < $iterations} {incr i} {
        lappend values [uplevel #0 $commandPrefix]
    }
    return [Stats $values]
}

proc ::rbcBenchmark::CollectIndexed {warmup iterations commandPrefix} {
    for {set i 0} {$i < $warmup} {incr i} {
        uplevel #0 [list {*}$commandPrefix $i]
    }
    set values {}
    for {set i 0} {$i < $iterations} {incr i} {
        lappend values [uplevel #0 [list {*}$commandPrefix $i]]
    }
    return [Stats $values]
}

proc ::rbcBenchmark::PrintEnvironment {title} {
    variable renderer
    puts {}
    puts $title
    puts {}
    puts [format " %-12s %s" "platform:" $::tcl_platform(platform)]
    puts [format " %-12s %s %s" "OS:" $::tcl_platform(os) $::tcl_platform(osVersion)]
    puts [format " %-12s %s" "machine:" $::tcl_platform(machine)]
    puts [format " %-12s %s" "Tcl:" [info patchlevel]]
    puts [format " %-12s %s" "Tk:" [package provide Tk]]
    puts [format " %-12s %s" "RBC:" [package provide rbc]]
    puts [format " %-12s %s" "renderer:" $renderer]
    puts [format " %-12s %s" "windowing:" [tk windowingsystem]]
    puts [format " %-12s %g" "Tk scaling:" [tk scaling]]
    puts [format " %-12s %dx%d depth=%d" "screen:" [winfo screenwidth .]  [winfo screenheight .]  [winfo screendepth .]]
    puts {}
}

proc ::rbcBenchmark::CsvQuote {value} {
    set value [string map {\" \"\"} $value]
    return "\"$value\""
}

proc ::rbcBenchmark::OpenLongCsv {path} {
    if {$path eq {}} {
        return
    }
    set channel [open $path w]
    puts $channel [join {platform os os_version machine tcl tk rbc renderer windowing benchmark case count\
                                 requested_width requested_height actual_width actual_height metric min_ms median_ms\
                                 mean_ms max_ms} ,]
    return $channel
}

proc ::rbcBenchmark::WriteLongMetric {channel benchmark case count width height actualWidth actualHeight metric stats} {
    variable renderer
    if {$channel eq {}} {
        return
    }

    set values [list $::tcl_platform(platform) $::tcl_platform(os) $::tcl_platform(osVersion) $::tcl_platform(machine)\
                        [info patchlevel] [package provide Tk] [package provide rbc] $renderer [tk windowingsystem]\
                        $benchmark $case $count $width $height $actualWidth $actualHeight $metric\
                        [format %.3f [dict get $stats min]] [format %.3f [dict get $stats median]]\
                        [format %.3f [dict get $stats mean]] [format %.3f [dict get $stats max]]]
    set quoted {}
    foreach value $values {
        lappend quoted [CsvQuote $value]
    }
    puts $channel [join $quoted ,]
    flush $channel
}

proc ::rbcBenchmark::WriteStandardMetrics {channel benchmark case count width height actualWidth actualHeight createMs\
                                                   redraw remap } {
    WriteLongMetric $channel $benchmark $case $count $width $height $actualWidth $actualHeight create\
            [ScalarStats $createMs]
    WriteLongMetric $channel $benchmark $case $count $width $height $actualWidth $actualHeight redraw $redraw
    WriteLongMetric $channel $benchmark $case $count $width $height $actualWidth $actualHeight axis-remap $remap
}

proc ::rbcBenchmark::CreateBareGraph {widgetCommand title} {
    variable top
    variable graph
    wm withdraw .
    catch {destroy $top}
    toplevel $top
    wm title $top $title
    wm overrideredirect $top 1
    $widgetCommand $graph -bufferelements 0 -buffergraph 1 -borderwidth 0 -highlightthickness 0 -plotborderwidth 0\
            -plotpadx 0 -plotpady 0 -title {}
    pack $graph -fill both -expand yes
    $graph legend configure -hide yes
    $graph grid configure -hide yes
    wm geometry $top 640x480+0+0
    wm deiconify $top
    update
}

proc ::rbcBenchmark::DeleteAllElements {} {
    variable graph
    set names [$graph element show]
    if {[llength $names] > 0} {
        $graph element delete {*}$names
    }
}

proc ::rbcBenchmark::DeleteAllMarkers {} {
    variable graph
    set names [$graph marker names]
    if {[llength $names] > 0} {
        $graph marker delete {*}$names
    }
}

proc ::rbcBenchmark::GridPosition {index count} {
    set columns [expr {int(ceil(sqrt(double($count))))}]
    set rows [expr {int(ceil(double($count)/$columns))}]
    set column [expr {$index%$columns}]
    set row [expr  {$index/$columns}]
    set x [expr {(double($column)+0.5)/$columns}]
    set y [expr {(double($row)+0.5)/$rows}]
    return [list $x $y $columns $rows]
}
