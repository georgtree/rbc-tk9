package require Tk
package require rbc

namespace eval ::snapshot {
    variable demo
    variable outputDir
    variable settings
    variable finished 0
}

proc ::snapshot::options {metadata} {
    set options [dict merge {width {} height {} exclude {} windows {}} $metadata]
    foreach key {width height} {
        set value [dict get $options $key]
        if {$value ne {} && (![string is integer -strict $value] || $value <= 0)} {
            error "Snapshot $key must be a positive integer"
        }
    }
    foreach window [dict get $options exclude] {
        if {![string match .* $window]} {
            error "Excluded window must be a widget path: $window"
        }
    }
    return $options
}

proc ::snapshot::excluded {window exclusions} {
    foreach parent $exclusions {
        if {$parent eq "." || $window eq $parent || [string first "${parent}." $window] == 0} {
            return 1
        }
    }
    return 0
}

proc ::snapshot::resize {source width height} {
    set sourceWidth [image width $source]
    set sourceHeight [image height $source]
    if {$width eq {} && $height eq {}} {
        return $source
    }
    if {$sourceWidth <= 0 || $sourceHeight <= 0} {
        error "Cannot resize an empty snapshot"
    }
    if {$width eq {}} {
        set width [expr {max(1, round(double($sourceWidth) * $height / $sourceHeight))}]
    }
    if {$height eq {}} {
        set height [expr {max(1, round(double($sourceHeight) * $width / $sourceWidth))}]
    }
    set scale [expr {min(double($width) / $sourceWidth, double($height) / $sourceHeight)}]
    set scaledWidth [expr {max(1, min($width, round($sourceWidth * $scale)))}]
    set scaledHeight [expr {max(1, min($height, round($sourceHeight * $scale)))}]
    set scaled [image create photo -width $scaledWidth -height $scaledHeight]
    set result {}
    try {
        ::rbc::winop resample $source $scaled sinc
        set result [image create photo -width $width -height $height]
        $result put white -to 0 0 $width $height
        $result copy $scaled -to [expr {($width - $scaledWidth) / 2}] [expr {($height - $scaledHeight) / 2}]
    } on error {message options} {
        if {$result ne {}} {
            image delete $result
        }
        return -options $options $message
    } finally {
        image delete $scaled
    }
    return $result
}

proc ::snapshot::findGraphs {parent} {
    set result {}
    foreach win [winfo children $parent] {
        if {[winfo class $win] in {Graph Barchart Stripchart Polar}} {
            lappend result $win
        }
        lappend result {*}[::snapshot::findGraphs $win]
    }
    return $result
}

# Toplevel capture currently uses an unsafe native-window path on Windows.
# Compose its mapped children instead; suitable for the winop demo labels.
proc ::snapshot::captureWindows {window exclusions} {
    if {[::snapshot::excluded $window $exclusions]} {
        return {}
    }
    if {![winfo exists $window] || ![winfo ismapped $window]} {
        error "Snapshot window is missing or unmapped: $window"
    }
    if {$window ne [winfo toplevel $window]} {
        return [list $window]
    }
    set result {}
    foreach child [winfo children $window] {
        if {[winfo ismapped $child] && [winfo toplevel $child] eq $window} {
            lappend result {*}[::snapshot::captureWindows $child $exclusions]
        }
    }
    return $result
}

proc ::snapshot::saveSnapshots {demo outputDir settings} {
    update idletasks
    set exclusions [dict get $settings exclude]
    set captures {}
    set seen {}
    if {[llength [dict get $settings windows]] > 0} {
        # Explicit window capture for demos without RBC graph widgets.
        foreach requested [dict get $settings windows] {
            foreach window [::snapshot::captureWindows $requested $exclusions] {
                if {$window ni $seen} {
                    lappend captures [list $window window]
                    lappend seen $window
                }
            }
        }
    } else {
        foreach graph [::snapshot::findGraphs .] {
            if {![winfo ismapped $graph] || [::snapshot::excluded $graph $exclusions]} {
                continue
            }
            if {$graph ni $seen} {
                lappend captures [list $graph graph]
                lappend seen $graph
            }
            # Excluding a graph also omits its external legend.
            set legend [$graph legend cget -position]
            if {[string match .* $legend] && [winfo exists $legend] && [winfo ismapped $legend] &&\
                        ![$graph legend cget -hide] && ![::snapshot::excluded $legend $exclusions] &&\
                        $legend ni $seen} {
                lappend captures [list $legend window]
                lappend seen $legend
            }
        }
    }
    if {[llength $captures] == 0} {
        error "No mapped snapshot windows remain after exclusions"
    }
    file mkdir $outputDir
    set photos {}
    set tiles {}
    set combined {}
    set first 1
    set top [winfo toplevel [lindex [lindex $captures 0] 0]]
    try {
        foreach capture $captures {
            lassign $capture window kind
            if {[winfo toplevel $window] ne $top} {
                error "Cannot combine widgets from different toplevel windows"
            }
            set photo [image create photo]
            lappend photos $photo
            if {$kind eq "graph"} {
                $window snap $photo
            } else {
                ::rbc::winop snap $window $photo
            }
            set x [winfo rootx $window]
            set y [winfo rooty $window]
            set right [expr {$x + [image width $photo]}]
            set bottom [expr {$y + [image height $photo]}]
            if {$first} {
                set minX $x
                set minY $y
                set maxX $right
                set maxY $bottom
                set first 0
            } else {
                set minX [expr {min($minX, $x)}]
                set minY [expr {min($minY, $y)}]
                set maxX [expr {max($maxX, $right)}]
                set maxY [expr {max($maxY, $bottom)}]
            }
            lappend tiles [list $photo $x $y]
        }
        set width [expr {$maxX - $minX}]
        set height [expr {$maxY - $minY}]
        set combined [image create photo -width $width -height $height]
        $combined put white -to 0 0 $width $height
        foreach tile $tiles {
            lassign $tile photo x y
            $combined copy $photo -to [expr {$x - $minX}] [expr {$y - $minY}]
        }
        set resized [::snapshot::resize $combined [dict get $settings width] [dict get $settings height]]
        if {$resized ne $combined} {
            image delete $combined
            set combined $resized
        }
        set name [file tail [file rootname $demo]]
        $combined write [file join $outputDir ${name}.png] -format png
    } finally {
        foreach photo $photos {
            image delete $photo
        }
        if {$combined ne {}} {
            image delete $combined
        }
    }
}

proc ::snapshot::finish {} {
    variable demo
    variable outputDir
    variable settings
    if {[catch {::snapshot::saveSnapshots $demo $outputDir $settings} message options]} {
        puts stderr [dict get $options -errorinfo]
        exit 1
    }
    exit 0
}

proc ::snapshot::main {arguments} {
    variable demo
    variable outputDir
    variable settings
    if {[llength $arguments] ni {2 3}} {
        error "usage: snapshotDemo.tcl demo.tcl output-directory ?metadata-dict?"
    }
    set demo [file normalize [lindex $arguments 0]]
    set outputDir [file normalize [lindex $arguments 1]]
    set settings [::snapshot::options [lindex $arguments 2]]
    cd [file dirname $demo]
    # Demos execute globally, but cannot overwrite the wrapper's local state.
    set ::argv {}
    set ::argc 0
    set ::argv0 $demo
    uplevel #0 [list source $demo]
    set delay [expr {[file tail $demo] eq "stripchart1.tcl" ? 2000 : 500}]
    after $delay ::snapshot::finish
    vwait ::snapshot::finished
}

::snapshot::main $::argv
