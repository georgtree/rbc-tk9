#!/usr/bin/env tclsh

set benchmarkDir [file dirname [file normalize [info script]]]

source [file join $benchmarkDir common.tcl]

namespace eval ::rbcSuite {
    variable benchmarkDir $::benchmarkDir
    variable options [dict create profile standard benchmarks {line strip symbols errorbars bars markers mixed} sizes\
                              {} iterations {} warmup {} csv_dir {}]
}

proc ::rbcSuite::ParseArgs {argv} {
    variable options
    set parsed [argparse -inline -exact -long\
                        -help {Run one or more RBC rendering benchmarks as separate Tcl processes using a common\
                                       workload profile.} {
            {-profile= -enum {smoke standard stress} -default standard -help {Select workload profile}}
            {-benchmarks= -default {line,strip,symbols,errorbars,bars,markers,mixed}\
                     -validate {[::rbcBenchmark::IsEnumList $arg {line strip symbols errorbars bars markers mixed}]}\
                     -errormsg {invalid benchmark list} -help {Comma-separated benchmarks to execute}}
            {-sizes= -validate {[::rbcBenchmark::IsSizeList $arg]} -errormsg {-sizes must contain WIDTHxHEIGHT values}\
                     -help {Override graph sizes for all workloads}}
            {-iterations= -type integer -validate {$arg >= 1} -errormsg {-iterations must be >= 1}\
                     -help {Override measured iteration count}}
            {-warmup= -type integer -validate {$arg >= 0} -errormsg {-warmup must be >= 0}\
                     -help {Override warm-up iteration count}}
            {-csv-dir= -key csv_dir -default {} -help {Write one CSV file per benchmark into this directory}}
        } $argv]
    dict set parsed benchmarks [::rbcBenchmark::ParseList [dict get $parsed benchmarks]]
    #
    # argparse omits optional switches without defaults. Preserve the
    # suite-level empty defaults for optional overrides that were not
    # supplied.
    #
    set options [dict merge $options $parsed]
}

proc ::rbcSuite::BenchmarkCommand {benchmark} {
    variable options
    variable benchmarkDir
    switch -- $benchmark {
        line {
            set script line.tcl
            set extra {}
        }
        strip {
            set script line.tcl
            set extra {-stripchart}
        }

        symbols {
            set script symbols.tcl
            set extra {}
        }
        errorbars {
            set script errorbars.tcl
            set extra {}
        }
        bars {
            set script bars.tcl
            set extra {}
        }
        markers {
            set script markers.tcl
            set extra {}
        }
        mixed {
            set script mixed.tcl
            set extra {}
        }
    }

    set cmd [list [info nameofexecutable] [file join $benchmarkDir $script] -profile [dict get $options profile]\
                     {*}$extra]
    foreach {key optionName} {sizes -sizes iterations -iterations warmup -warmup} {
        set value [dict get $options $key]
        if {$value ne {}} {
            lappend cmd $optionName $value
        }
    }
    set csvDir [dict get $options csv_dir]
    if {$csvDir ne {}} {
        file mkdir $csvDir
        lappend cmd -csv [file join $csvDir ${benchmark}.csv]
    }
    return $cmd
}

proc ::rbcSuite::RunChild {benchmark cmd} {
    puts {}
    puts [string repeat = 78]
    puts "benchmark: $benchmark"
    puts "command:   [join $cmd { }]"
    puts [string repeat = 78]
    flush stdout

    set pipeline [linsert $cmd 0 |]
    set pipe [open $pipeline r]

    fconfigure $pipe -buffering line

    while {[gets $pipe line] >= 0} {
        puts $line
    }

    if {[catch {close $pipe} message opts]} {
        return -options $opts \
            "benchmark \"$benchmark\" failed: $message"
    }
}

proc ::rbcSuite::Main {argv} {
    variable options
    ParseArgs $argv
    foreach benchmark [dict get $options benchmarks] {
        RunChild $benchmark [BenchmarkCommand $benchmark]
    }
}

if {[catch {::rbcSuite::Main $argv} message opts]} {
    puts stderr "benchmark suite failed: $message"
    if {[dict exists $opts -errorinfo]} {
        puts stderr [dict get $opts -errorinfo]
    }
    exit 1
}

exit 0
