#!/usr/bin/env tclsh

set benchmarkDir [file dirname [file normalize [info script]]]

source [file join $benchmarkDir common.tcl]

namespace eval ::rbcSuite {
    variable benchmarkDir $::benchmarkDir
    variable options [dict create profile standard benchmarks {line strip symbols errorbars bars markers mixed} sizes\
                              {} iterations {} warmup {} csv_dir {}]
}

proc ::rbcSuite::Usage {} {
    puts {Usage: run.tcl ?options?

Options:

  -profile NAME
      smoke, standard, or stress.
      Default: standard

  -benchmarks LIST
      line
      strip
      symbols
      errorbars
      bars
      markers
      mixed

  -sizes LIST
      Override graph sizes for every workload.

  -iterations N
      Override measured iterations for every workload.

  -warmup N
      Override warm-up iterations for every workload.

  -csv-dir DIR
      Write one CSV file per benchmark.

  -help
      Show this message.

Examples:

  tclsh tests/benchmark/run.tcl

  tclsh tests/benchmark/run.tcl \
      -profile smoke

  tclsh tests/benchmark/run.tcl \
      -profile stress \
      -benchmarks symbols,errorbars,bars,markers

  tclsh tests/benchmark/run.tcl \
      -profile standard \
      -csv-dir benchmark-results
}
}

proc ::rbcSuite::ParseArgs {argv} {
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
            -benchmarks -
            -sizes -
            -iterations -
            -warmup -
            -csv-dir {
                incr i
                if {$i >= [llength $argv]} {
                    error "missing value for $arg"
                }
                set value [lindex $argv $i]
                switch -- $arg {
                    -profile {
                        dict set options profile $value
                    }
                    -benchmarks {
                        dict set options benchmarks [::rbcBenchmark::ParseList $value]
                    }
                    -sizes {
                        dict set options sizes $value
                    }
                    -iterations {
                        dict set options iterations $value
                    }
                    -warmup {
                        dict set options warmup $value
                    }
                    -csv-dir {
                        dict set options csv_dir $value
                    }
                }
            }
            default {
                error "unknown option '$arg'; use -help"
            }
        }
    }

    set profile [dict get $options profile]

    if {$profile ni {smoke standard stress}} {
        error "profile must be smoke, standard, or stress"
    }
    set valid {line strip symbols errorbars bars markers mixed}
    foreach benchmark [dict get $options benchmarks] {
        if {$benchmark ni $valid} {
            error "unknown benchmark '$benchmark'"
        }
    }
    if {[dict get $options iterations] ne {} || [dict get $options warmup] ne {}} {
        if {[dict get $options iterations] eq {}} {
            set iterations 1
        } else {
            set iterations [dict get $options iterations]
        }
        if {[dict get $options warmup] eq {}} {
            set warmup 0
        } else {
            set warmup [dict get $options warmup]
        }
        ::rbcBenchmark::ValidateIterations $iterations $warmup
    }
    if {[dict get $options sizes] ne {}} {
        ::rbcBenchmark::ParseSizes [dict get $options sizes]
    }
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
