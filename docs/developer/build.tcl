#!/usr/bin/env tclsh
# Build the private C manual without configuring or loading RBC.
# Only Tcl core commands are required. See license.terms.

namespace eval ::rbcDocBuild {
    variable source [file dirname [file normalize [info script]]]
}

proc ::rbcDocBuild::readFile {path} {
    set channel [open $path r]
    try {
        chan configure $channel -encoding utf-8
        return [read $channel]
    } finally {
        close $channel
    }
}

proc ::rbcDocBuild::writeFile {path contents} {
    set channel [open $path w]
    try {
        chan configure $channel -encoding utf-8 -translation lf
        puts -nonewline $channel $contents
    } finally {
        close $channel
    }
}

proc ::rbcDocBuild::doxyQuote {path} {
    # Tcl's normalized Windows paths use forward slashes, also accepted by Doxygen.
    return "\"[string map [list \" \\\"] [file normalize $path]]\""
}

proc ::rbcDocBuild::executable {path} {
    # Preserve relative executable paths before changing the working directory.
    if {[file exists $path]} {
        return [file normalize $path]
    }
    return $path
}

proc ::rbcDocBuild::main {arguments} {
    variable source
    set output _build/developer
    set html [file join $source html]
    set doxygen doxygen
    set python python
    set strict 0
    while {[llength $arguments]} {
        set option [lindex $arguments 0]
        set arguments [lrange $arguments 1 end]
        switch -- $option {
            --help - -h {
                puts {usage: build.tcl ?--build-dir PATH? ?--html-dir PATH? ?--doxygen PATH? ?--python PATH? ?--strict?}
                return
            }
            --strict {set strict 1}
            --build-dir - --html-dir - --doxygen - --python {
                if {![llength $arguments] || [string match --* [lindex $arguments 0]]} {
                    error "missing value for $option"
                }
                set value [lindex $arguments 0]
                set arguments [lrange $arguments 1 end]
                switch -- $option {
                    --build-dir {set output $value}
                    --html-dir {set html $value}
                    --doxygen {set doxygen $value}
                    --python {set python $value}
                }
            }
            default {error "unknown option: $option"}
        }
    }
    set root [file dirname [file dirname $source]]
    set output [file normalize $output]
    set html [file normalize $html]
    if {$html eq $source} {
        error "HTML output must not overwrite the manual source directory"
    }
    set doxygen [executable $doxygen]
    set python [executable $python]
    if {![regexp {AC_INIT\(\[rbc\],\[([^]]+)\]\)} [readFile [file join $root configure.ac]] -> version]} {
        error "cannot read RBC version from configure.ac"
    }
    file mkdir $output
    set log [file join $output doxygen-warnings.log]
    set xml [file join $output doxygen xml]
    # Never let declarations removed from the source survive in old XML.
    file delete -force $xml
    set substitutions [list \
        @ROOT@ [doxyQuote $root] \
        @OUTPUT@ [doxyQuote [file join $output doxygen]] \
        @WARNINGS@ [doxyQuote $log] \
        @RENDER_HEADER@ [doxyQuote [file join $root generic rbcRender.h]] \
        @DISPATCH_HEADER@ [doxyQuote [file join $root generic rbcRenderInt.h]] \
        @GRAPH_HEADER@ [doxyQuote [file join $root generic rbcGraph.h]] \
        @GRAPH_SOURCE@ [doxyQuote [file join $root generic rbcGraph.c]] \
        @VECTOR_HEADER@ [doxyQuote [file join $root generic rbcVector.h]] \
        @VECTOR_INT_HEADER@ [doxyQuote [file join $root generic rbcVectorInt.h]] \
        @CONFIG_HEADER@ [doxyQuote [file join $root generic rbcInt.h]] \
        @BIND_HEADER@ [doxyQuote [file join $root generic rbcBind.h]] \
        @BIND_SOURCE@ [doxyQuote [file join $root generic rbcBind.c]] \
        @VERSION@ $version]
    set config [file join $output Doxyfile]
    writeFile $config [string map $substitutions [readFile [file join $source Doxyfile.in]]]
    writeFile $log {}
    set directory [pwd]
    set savedEnvironment {}
    foreach name {RBC_DOXYGEN_XML RBC_DOC_VERSION RBC_DOC_HTML} {
        if {[info exists ::env($name)]} {
            dict set savedEnvironment $name $::env($name)
        }
    }
    try {
        cd $root
        # Inherit both streams: successful tools may write progress to stderr.
        exec -- $doxygen $config >@stdout 2>@stderr
        set warnings [string trim [readFile $log]]
        if {$warnings ne {}} {
            puts stderr $warnings
            if {$strict} {
                error "Doxygen reported documentation warnings"
            }
        }
        if {![file isfile [file join $xml index.xml]]} {
            error "Doxygen did not produce XML"
        }
        set ::env(RBC_DOXYGEN_XML) $xml
        set ::env(RBC_DOC_VERSION) $version
        set ::env(RBC_DOC_HTML) $html
        set command [list $python -m sphinx -E -a -b html -d [file join $output doctrees]]
        if {$strict} {
            lappend command -W --keep-going
        }
        lappend command $source $html
        exec -- {*}$command >@stdout 2>@stderr
    } finally {
        cd $directory
        foreach name {RBC_DOXYGEN_XML RBC_DOC_VERSION RBC_DOC_HTML} {
            unset -nocomplain ::env($name)
            if {[dict exists $savedEnvironment $name]} {
                set ::env($name) [dict get $savedEnvironment $name]
            }
        }
    }
    puts "Developer manual: [file join $html index.html]"
}

if {[catch {::rbcDocBuild::main $argv} message]} {
    puts stderr "Developer documentation build failed: $message"
    exit 1
}
