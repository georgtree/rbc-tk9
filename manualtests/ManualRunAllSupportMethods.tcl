# Shared reporting for manual runners. Successful execution is not a visual pass.
namespace eval ::rbcManual {
    if {![info exists attempted]} {
        variable attempted 0
        variable failed 0
    }

    proc Counts {} {
        variable attempted
        variable failed
        list $attempted $failed
    }

    proc RunCase {name delay} {
        variable attempted
        variable failed
        incr attempted
        set name ::[string trimleft $name :]
        puts "CASE [namespace tail $name]"
        set stage Setup
        set errors {}
        try {
            ${name}.Setup
            update
            sleep $delay
            set stage Body
            ${name}.Body
            update
            sleep $delay
        } on error {message options} {
            lappend errors "$stage: $message"
        } finally {
            if {[catch {${name}.Cleanup} message]} {
                lappend errors "Cleanup: $message"
            }
        }
        if {[llength $errors]} {
            incr failed
            foreach message $errors {
                puts stderr "ERROR [namespace tail $name] $message"
            }
        }
        sleep $delay
    }

    proc Report {topic before} {
        variable attempted
        variable failed
        lassign $before oldAttempted oldFailed
        set errors [expr {$failed-$oldFailed}]
        set completed [expr {$attempted-$oldAttempted-$errors}]
        puts "EXECUTION $topic: $completed completed, $errors errors; visual review still required."
    }
    proc Usage {} {
        return {Usage: runner.tcl ?-renderer native|cairo? ?-delay milliseconds? ?-help?
Defaults: -renderer native -delay 200
Applies to top-level, component and topic runners.}
    }

    proc Configure {arguments} {
        # Validate everything before loading Tk or changing option defaults.
        set renderer native
        set delay 200
        foreach {option value} $arguments {
            if {$option eq "-help" && [llength $arguments] == 1} {
                puts [Usage]
                return 0
            }
            switch -- $option {
                -renderer {
                    if {$value ni {native cairo}} {
                        error "-renderer must be native or cairo\n[Usage]"
                    }
                    set renderer $value
                }
                -delay {
                    if {![string is integer -strict $value] || $value < 0} {
                        error "-delay must be a non-negative integer in milliseconds\n[Usage]"
                    }
                    set delay $value
                }
                default {error "unknown option: $option\n[Usage]"}
            }
        }
        package require rbc
        # Fail before running cases when the requested renderer is unavailable.
        set probe .rbcManualRendererProbe
        while {[winfo exists $probe]} {append probe _}
        try {
            ::rbc::graph $probe -renderer $renderer
        } finally {
            catch {destroy $probe}
        }
        foreach class {Graph Barchart Stripchart Polar Trackgraph} {
            option add *${class}.renderer $renderer interactive
        }
        set ::manualTestDelay $delay
        puts "OPTIONS renderer=$renderer delay=${delay}ms"
        return 1
    }

    proc RunRunner {path script} {
        variable runnerDepth
        if {![info exists runnerDepth]} {set runnerDepth 0}
        if {$runnerDepth == 0} {
            set arguments [expr {[info exists ::argv] ? $::argv : {}}]
            if {![Configure $arguments]} {return}
        }
        set path [file normalize $path]
        set standalone [expr {$runnerDepth == 0 && [info exists ::argv0] &&
                [file normalize $::argv0] eq $path}]
        set directory [pwd]
        incr runnerDepth
        try {
            cd [file dirname [file normalize $path]]
            uplevel 1 $script
        } finally {
            cd $directory
            incr runnerDepth -1
            if {$standalone && [winfo exists .]} {destroy .}
        }
    }

}
