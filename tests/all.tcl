# With only tcltest options, run widgets as in the standalone runner.
# The makefiles pass a suite name before any options.
package require tcltest 2
set suite widgets
if {[llength $argv] && ![string match -* [lindex $argv 0]]} {
    set suite [lindex $argv 0]
    set argv [lrange $argv 1 end]
}
set here [file dirname [file normalize [info script]]]
set private [dict create notify {RBC_NOTIFY_TEST_LIB vector.notify.test}\
                     tile {RBC_TILE_TEST_LIB tile.notify.test}\
                     temporary {RBC_IMAGE_TEST_LIB image.temporary.test}\
                     photo {RBC_IMAGE_TEST_LIB image.photo.test}\
                     resources {RBC_RESOURCE_TEST_LIB resources.lifecycle.test}\
                     ps {RBC_RESOURCE_TEST_LIB postscript.primitives.test}]

if {$suite eq {all}} {
    foreach phase [list widgets vector {*}[dict keys $private]] {
        try {
            exec -- [info nameofexecutable] [info script] $phase {*}$argv >@stdout 2>@stderr
        } trap CHILDSTATUS {message options} {
            exit [lindex [dict get $options -errorcode] 2]
        }
    }
    exit 0
}

set privateFiles {}
dict for {phase spec} $private {
    lassign $spec var file
    if {![dict exists $private $suite] || ($var ne [lindex [dict get $private $suite] 0])} {
        unset -nocomplain ::env($var)
    }
    lappend privateFiles $file
}

::tcltest::configure {*}$argv
set include [::tcltest::configure -file]
set exclude [::tcltest::configure -notfile]
if {$suite eq {widgets}} {
    lappend exclude {*}$privateFiles
}
switch -- $suite {
    widgets {
        set candidates [lmap path [glob -directory $here runtime/*/*.test] {file tail $path}]
    }
    vector {
        set candidates {vector.load.test vector.capi.test vector.readonly.test\
                                vector.parentheses.test vector.namespace.test vector.rename.test vector.map.test\
                                vector.fft.test vector.ifft.test vector.fftfreq.test vector.window.test\
                                vector.simplify.test graph.legacy.headless.test}
    }
    default {
        set candidates [lrange [dict get $private $suite] 1 end]
    }
}
set files {}
foreach file $candidates {
    set selected 0
    foreach pattern $include {
        if {[string match $pattern $file]} {
            set selected 1
            break
        }
    }
    foreach pattern $exclude {
        if {[string match $pattern $file]} {
            set selected 0
            break
        }
    }
    if {$selected} {
        # <group>.<test>.test lives under runtime/<group>.
        lappend files [file join runtime [lindex [split $file .] 0] $file]
    }
}
if {![llength $files]} {
    exit 0
}

puts "Running RBC tests: $suite"
if {$suite eq {widgets}} {
    package require Tk
    package require rbc
    foreach class {Graph Barchart Stripchart Polar Trackgraph} {
        option add *${class}.renderer native interactive
    }
    ::tcltest::testConstraint rbcNativeSuiteDefault 1
} elseif {$suite eq {vector}} {
    package require rbc::vector
    if {[package provide Tk] ne {}} {
        error {vector tests must start in a Tcl-only interpreter}
    }
} else {
    set var [lindex [dict get $private $suite] 0]
    if {![info exists ::env($var)] || ![file isfile [set ::env($var)]]} {
        error "missing $suite test library: $var"
    }
}
::tcltest::configure -testdir $here -file $files -notfile {} -asidefromdir * -singleproc 1
# Some tcltest versions call an unqualified exit from cleanupTests when Tk
# is loaded. Let the runner return its actual failure status to make.
proc ::tcltest::exit {args} {
    if {![llength $args] && ([namespace tail [lindex [info level -1] 0]] eq {cleanupTests})} {
        return
    }
    ::exit {*}$args
}
exit [::tcltest::runAllTests]
