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

source [file join $::rbcDocBuild::source doxygenCompat.tcl]

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
            --strict {
                set strict 1
            }
            --build-dir -
            --html-dir -
            --doxygen -
            --python {
                if {![llength $arguments] || [string match --* [lindex $arguments 0]]} {
                    error "missing value for $option"
                }
                set value [lindex $arguments 0]
                set arguments [lrange $arguments 1 end]
                switch -- $option {
                    --build-dir {
                        set output $value
                    }
                    --html-dir {
                        set html $value
                    }
                    --doxygen {
                        set doxygen $value
                    }
                    --python {
                        set python $value
                    }
                }
            }
            default {
                error "unknown option: $option"
            }
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
    set substitutions [list @GROUPS@ [doxyQuote [file join $source groups.dox]]\
                               @CALLING_CONVENTIONS@ [doxyQuote [file join $source calling-conventions.dox]]\
                               @ROOT@ [doxyQuote $root]\
                               @OUTPUT@ [doxyQuote [file join $output doxygen]]\
                               @WARNINGS@ [doxyQuote $log]]
    foreach {key filename} {
        PUBLIC_HEADER           rbc.h
        NATIVE_X11_SOURCE       rbcRenderNativeX11.c
        NATIVE_SOURCE           rbcRenderNative.c
        NATIVE_HEADER           rbcRenderNative.h
        RENDER_SOURCE           rbcRender.c
        RENDER_HEADER           rbcRender.h
        DISPATCH_HEADER         rbcRenderInt.h
        CHAIN_HEADER            rbcChain.h
        LIST_HEADER             rbcList.h
        IMAGE_HEADER            rbcImage.h
        UNIX_IMAGE_SOURCE       rbcUnixImage.c
        IMAGE_SOURCE            rbcImage.c
        TILE_HEADER             rbcTile.h
        TEXT_HEADER             rbcText.h
        TEXT_SOURCE             rbcText.c
        WINDOW_SOURCE           rbcWindow.c
        DRAW_SOURCE             rbcDraw.c
        FONT_SOURCE             rbcFont.c
        PDF_BUILDER_SOURCE      rbcPdf.c
        PDF_FONT_SOURCE         rbcPdfFont.c
        FONT_HEADER             rbcFont.h
        PS_HEADER               rbcPs.h
        PS_SOURCE               rbcPs.c
        GRAPH_PS_SOURCE         rbcGrPs.c
        GRAPH_EXPORT_SOURCE     rbcGrExport.c
        GRAPH_SVG_SOURCE        rbcGrSvg.c
        GRAPH_PDF_SOURCE        rbcGrPdf.c
        EXPORT_HEADER           rbcExport.h
        GRAPH_HEADER            rbcGraph.h
        PDF_RENDER_SOURCE       rbcRenderPdf.c
        PS_RENDER_SOURCE        rbcRenderPostscript.c
        EXPORT_RENDER_HEADER    rbcRenderExportInt.h
        SVG_SOURCE              rbcRenderSvg.c
        CAIRO_SOURCE            rbcRenderCairo.c
        POLAR_SOURCE            rbcGrPolar.c
        BAR_SOURCE              rbcGrBar.c
        LINE_SOURCE             rbcGrLine.c
        GRID_SOURCE             rbcGrGrid.c
        HAIRS_SOURCE            rbcGrHairs.c
        GRAPH_SOURCE            rbcGraph.c
        TILE_SOURCE             rbcTile.c
        FFT_SOURCE              rbcVecFft.c
        SPLINE_SOURCE           rbcSpline.c
        EXPRESSION_SOURCE       rbcVecMath.c
        VECTOR_SOURCE           rbcVector.c
        VECTOR_HEADER           rbcVector.h
        VECTOR_COMMAND_SOURCE   rbcVecCmd.c
        VECTOR_INT_HEADER       rbcVectorInt.h
        ELEMENT_HEADER          rbcGrElem.h
        LEGEND_HEADER           rbcGrLegd.h
        LEGEND_SOURCE           rbcGrLegd.c
        AXIS_HEADER             rbcGrAxis.h
        TYPES_HEADER            rbcTypes.h
        AXIS_SOURCE             rbcGrAxis.c
        MARKER_SOURCE           rbcGrMarker.c
        TRACK_SOURCE            rbcGrTrack.c
        TRACK_HEADER            rbcGrTrack.h
        INIT_SOURCE             rbcInt.c
        STUB_SOURCE             rbcStubLib.c
        CONFIG_HEADER           rbcInt.h
        BIND_HEADER             rbcBind.h
        BIND_SOURCE             rbcBind.c
        WINOP_SOURCE            rbcWinop.c
        EPS_SOURCE              rbcCanvEps.c
        GEOMETRY_SOURCE         rbcGeometry.c
        LIST_SOURCE             rbcList.c
        CONFIG_SOURCE           rbcConfig.c
        MISC_SOURCE             rbcGrMisc.c
        UTIL_SOURCE             rbcUtil.c
        PEN_SOURCE              rbcGrPen.c
        ELEMENT_SOURCE          rbcGrElem.c
        SCROLL_SOURCE           rbcScroll.c
        MATH_HEADER             rbcMath.h} {
        lappend substitutions @${key}@ [doxyQuote [file join $root generic $filename]]
    }
    lappend substitutions @VERSION@ $version
    set config [file join $output Doxyfile]
    writeFile $config [string map $substitutions [readFile [file join $source Doxyfile.in]]]
    writeFile $log {}
    # Same C names have different layouts/contracts on each native platform.
    # Keep Windows XML separate rather than merging it with the X11 project.
    set winXml [file join $output doxygen-win xml]
    set winLog [file join $output doxygen-win-warnings.log]
    set winConfig [file join $output Doxyfile-win]
    file delete -force $winXml
    set winSettings [readFile $config]
    set genericDir [file join $root generic]
    append winSettings "\nINPUT ="
    foreach {directory filenames} [list $genericDir {rbcFont.c rbcCanvEps.c rbcWin.h}\
                                           $source {groups.dox calling-conventions.dox} \
                                           $genericDir {rbcInt.c rbcRenderNativeWin.c rbcRenderCairo.c rbcWinDraw.c\
                                                                rbcWinImage.c rbcWinUtil.c rbcTile.c rbcGrPs.c\
                                                                rbcWindow.c rbcGrHairs.c rbcGraph.c}
                                  ] {
        foreach filename $filenames {
            append winSettings " " [doxyQuote [file join $directory $filename]]
        }
    }
    append winSettings "\n"
    append winSettings "OUTPUT_DIRECTORY = [doxyQuote [file join $output doxygen-win]]\n"
    append winSettings "WARN_LOGFILE = [doxyQuote $winLog]\n"
    # Select Windows branches and expand Xlib spelling without platform headers.
    append winSettings "PREDEFINED += _Xconst=const APIENTRY= WIN32=1 _WIN32=1\n"
    writeFile $winConfig $winSettings
    writeFile $winLog {}
    set directory [pwd]
    set savedEnvironment {}
    foreach name {RBC_DOXYGEN_XML RBC_DOXYGEN_WIN_XML RBC_DOC_VERSION RBC_DOC_HTML} {
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
        exec -- $doxygen $winConfig >@stdout 2>@stderr
        set warnings [string trim [readFile $winLog]]
        if {$warnings ne {}} {
            puts stderr $warnings
            if {$strict} {
                error "Windows Doxygen project reported documentation warnings"
            }
        }
        if {![file isfile [file join $winXml index.xml]]} {
            error "Windows Doxygen project did not produce XML"
        }
        # Keep declaration layout compatible across Doxygen versions. Only
        # generated compound XML is rewritten; source comments stay untouched.
        foreach xmlDirectory [list $xml $winXml] {
            foreach path [glob -nocomplain -directory $xmlDirectory *.xml] {
                set original [readFile $path]
                set normalized [normalizeCallbackXml $original]
                if {$normalized ne $original} {
                    writeFile $path $normalized
                }
            }
        }
        set ::env(RBC_DOXYGEN_WIN_XML) $winXml
        set ::env(RBC_DOXYGEN_XML) $xml
        set ::env(RBC_DOC_VERSION) $version
        set ::env(RBC_DOC_HTML) $html
        set command [list $python -m sphinx -E -a -b html -d [file join $output doctrees]]
        if {$strict} {
            lappend command -W --keep-going
        }
        lappend command $source $html
        # Stage maintained styles before Sphinx computes their asset hashes.
        # Existing output must not retain an older copy of our custom CSS.
        set themeDir [file join $source .. theme]
        set themeStyles {rbc.css rbc-layout.css rbc-developer.css}
        foreach name $themeStyles {
            set path [file join $themeDir $name]
            if {![file isfile $path] || ![file readable $path]} {
                error "missing documentation stylesheet: $path"
            }
        }
        file mkdir [file join $html _static]
        foreach name $themeStyles {
            file copy -force [file join $themeDir $name] [file join $html _static $name]
        }
        exec -- {*}$command >@stdout 2>@stderr
        foreach name $themeStyles {
            set stylesheet [file join $html _static $name]
            if {![file isfile $stylesheet] ||
                [readFile $stylesheet] ne [readFile [file join $themeDir $name]]} {
                error "documentation stylesheet was not copied correctly: $stylesheet"
            }
        }
    } finally {
        cd $directory
        foreach name {RBC_DOXYGEN_XML RBC_DOXYGEN_WIN_XML RBC_DOC_VERSION RBC_DOC_HTML} {
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
