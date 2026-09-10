#!/usr/bin/env tclsh
# Compare renderer runs without loading Tk unless the RBC viewer is requested.
# See license.terms and tests/benchmark/README.md.
namespace eval ::rbcCompare {
    variable fields {host platform os os_version machine tcl tk rbc windowing benchmark case requested_width requested_height actual_width actual_height metric}
    variable panels {}
    variable selected {}
}

proc ::rbcCompare::ReadCsv {path} {
    package require csv
    set ch [open $path r]
    try {
        fconfigure $ch -encoding utf-8
        set header {}; set rows {}; set record {}; set line 0
        while {[gets $ch part] >= 0} {
            incr line
            append record $part
            if {![::csv::iscomplete $record]} {append record \n; continue}
            if {$record eq {}} {continue}
            set values [::csv::split $record]; set record {}
            if {$header eq {}} {
                set header [lreplace $values 0 0 [string trimleft [lindex $values 0] \ufeff]]
                if {[llength [lsort -unique $header]] != [llength $header]} {error "$path: duplicate CSV columns"}
                continue
            }
            if {[llength $values] != [llength $header]} {error "$path:$line: wrong column count"}
            set row {}; foreach k $header v $values {dict set row $k $v}
            lappend rows $row
        }
        if {$record ne {}} {error "$path: incomplete CSV record"}
        if {$header eq {}} {error "$path: empty CSV"}
        return $rows
    } finally {close $ch}
}

proc ::rbcCompare::Median {values} {
    set values [lsort -real $values]; set n [llength $values]; set mid [expr {$n/2}]
    if {$n % 2} {return [lindex $values $mid]}
    expr {([lindex $values [expr {$mid-1}]] + [lindex $values $mid])/2.0}
}

proc ::rbcCompare::Normalize {row host source} {
    # Old files may label the renderer without actually selecting it. Require
    # the new schema rather than guessing from filenames or relabeling old runs.
    foreach field {platform os os_version machine tcl tk rbc renderer antialias windowing requested_width requested_height actual_width actual_height} {
        if {![dict exists $row $field] || [dict get $row $field] eq {}} {
            error "$source: missing $field; rerun with the updated benchmark scripts"
        }
    }
    if {[dict get $row renderer] ni {native cairo} || [dict get $row antialias] ni {default none gray}} {
        error "$source: unsupported renderer/antialias"
    }
    dict set row host $host
    if {[dict exists $row metric]} {
        foreach k {benchmark case count median_ms} {
            if {![dict exists $row $k]} {error "$source: missing $k"}
        }
        return [list $row]
    }
    foreach k {element_type decimate points} {
        if {![dict exists $row $k]} {error "$source: unrecognized CSV schema (missing $k)"}
    }
    dict set row benchmark [dict get $row element_type]
    dict set row case "decimate=[dict get $row decimate]"
    dict set row count [dict get $row points]
    set rows {}
    foreach {metric column} {create create_ms redraw redraw_median_ms axis-remap remap_median_ms array-remap data_array_remap_median_ms index-remap data_index_remap_median_ms append-remap append_remap_median_ms closest closest_median_ms} {
        if {![dict exists $row $column]} {error "$source: missing $column"}
        dict set row metric $metric; dict set row median_ms [dict get $row $column]
        lappend rows $row
    }
    return $rows
}

proc ::rbcCompare::Load {inputs} {
    variable fields
    set samples {}
    foreach {host path} $inputs {
        if {[file isdirectory $path]} {set files [lsort [glob -nocomplain -directory $path *.csv]]} else {set files [list $path]}
        if {![llength $files]} {error "$path: no CSV files"}
        foreach file $files {
            foreach raw [ReadCsv $file] {
                foreach row [Normalize $raw $host $file] {
                    set key {}; foreach field $fields {lappend key [dict get $row $field]}
                    foreach k {count actual_width actual_height requested_width requested_height} {
                        set v [dict get $row $k]
                        if {![string is integer -strict $v] || $v <= 0} {error "$file: invalid $k: $v"}
                    }
                    set ms [dict get $row median_ms]
                    if {![string is double -strict $ms] || [catch {expr {$ms >= 0 && $ms < Inf}} valid] || !$valid} {
                        error "$file: invalid timing: $ms"
                    }
                    set series [dict get $row renderer]
                    if {$series eq "cairo"} {append series / [dict get $row antialias]}
                    set sampleKey [list $key [dict get $row count] $series]
                    dict lappend samples $sampleKey $ms
                }
            }
        }
    }
    set result {}
    dict for {sampleKey values} $samples {
        lassign $sampleKey key count series
        dict set result $key $count $series [list [Median $values] [llength $values]]
    }
    return $result
}

proc ::rbcCompare::WriteFile {path content} {
    set ch [open $path w]
    try {fconfigure $ch -encoding utf-8 -translation lf; puts -nonewline $ch $content} finally {close $ch}
}

proc ::rbcCompare::Title {key} {
    variable fields
    set r {}
    foreach k $fields v $key {dict set r $k $v}
    return "[dict get $r host] / [dict get $r benchmark] / [dict get $r case] / [dict get $r metric] / [dict get $r actual_width]x[dict get $r actual_height]"
}

proc ::rbcCompare::Report {data out} {
    variable fields
    package require csv
    file mkdir $out
    set header [concat $fields {count cairo_mode native_ms cairo_ms native_over_cairo native_runs cairo_runs}]
    set csv "[::csv::join $header]\n"
    set text "Renderer comparison (milliseconds; lower is better)\nRatio = native/Cairo: above 1 means Cairo is faster.\nRepeated rows: median of run medians, not pooled samples.\nZero rounded timings have no ratio. Missing pairs are listed explicitly.\n"
    foreach key [lsort [dict keys $data]] {
        append text "\n[Title $key]\nEnvironment: [lrange $key 1 8]\n"
        append text [format "%10s %-12s %12s %12s %10s %9s\n" count renderer native-ms cairo-ms ratio runs-n/c]
        foreach count [lsort -integer [dict keys [dict get $data $key]]] {
            set row [dict get $data $key $count]
            set native {}; set nn 0
            if {[dict exists $row native]} {lassign [dict get $row native] native nn}
            set modes [lsearch -all -inline -glob [lsort [dict keys $row]] cairo/*]
            if {![llength $modes]} {set modes {cairo/missing}}
            foreach mode $modes {
                set cairo {}; set cn 0; set ratio {}
                if {[dict exists $row $mode]} {lassign [dict get $row $mode] cairo cn}
                if {$native ne {} && $cairo ne {} && $native > 0 && $cairo > 0} {set ratio [expr {$native/double($cairo)}]}
                append csv [::csv::join [concat $key [list $count $mode $native $cairo $ratio $nn $cn]]] \n
                append text [format "%10d %-12s %12s %12s %10s %9s\n" $count $mode $native $cairo \
                    [expr {$ratio eq {} ? "n/a" : [format %.3f $ratio]}] "$nn/$cn"]
            }
        }
    }
    WriteFile [file join $out comparison.csv] $csv
    WriteFile [file join $out comparison.txt] $text
    return $text
}

proc ::rbcCompare::Series {rows ratio} {
    set result {}
    foreach count [lsort -integer [dict keys $rows]] {
        set row [dict get $rows $count]
        dict for {name value} $row {
            set ms [lindex $value 0]
            if {$ratio} {
                if {$name eq "native" || ![dict exists $row native]} {continue}
                set native [lindex [dict get $row native] 0]
                if {$native <= 0 || $ms <= 0} {continue}
                set ms [expr {$native/double($ms)}]
            }
            dict lappend result $name [list $count $ms]
        }
    }
    return $result
}

proc ::rbcCompare::Html {data out snapshot browser} {
    package require ticklecharts
    set ::ticklecharts::htmlstdout False
    set map {}; set ruff {}; set index {}; set serial 0
    foreach key [lsort [dict keys $data]] {
        incr serial
        foreach {kind ratio ylabel} {time 0 {Milliseconds (lower is better)} ratio 1 {Native / Cairo (higher is better)}} {
            set series [Series [dict get $data $key] $ratio]
            if {![dict size $series]} {continue}
            set id [format "benchmark_%04d_%s" $serial $kind]
            set title "[Title $key] — $ylabel"
            set chart [::ticklecharts::chart new]
            try {
                $chart SetOptions -animation False -title [list text [Title $key] subtext "OS/runtime: [lrange $key 1 8]" textStyle {fontSize 14}] \
                    -tooltip {trigger axis} -legend {top 55} -grid {top 100 bottom 65 left 95 right 35 containLabel True} \
                    -toolbox {feature {saveAsImage {type png}}}
                $chart Xaxis -type value -name {Item count} -min 0
                $chart Yaxis -type value -name $ylabel -min 0
                dict for {name points} $series {$chart Add lineSeries -name $name -data $points -showAllSymbol nothing}
                $chart Render -outfile [file join $out $id.html] -title $title -width 1100px -height 550px -renderer svg
                if {$snapshot ne {}} {
                    set args [list -html [$chart toHTML -renderer svg -width 1100px -height 550px] \
                        -renderer $snapshot -outfile [file join $out $id.$snapshot]]
                    if {$browser ne {}} {lappend args -exe $browser}
                    $chart SnapShot {*}$args
                }
            } finally {$chart destroy}
            dict set map !$id! $id.html
            append ruff "\n[Title $key] — $ylabel\n\n!$id!\n"
            append index "<li><a href=\"$id.html\">[Escape $title]</a></li>\n"
        }
    }
    WriteFile [file join $out charts.map] $map
    WriteFile [file join $out charts.ruff] $ruff
    WriteFile [file join $out index.html] "<!doctype html><meta charset=\"utf-8\"><title>RBC benchmarks</title><h1>Renderer comparisons</h1><ul>$index</ul>"
}

proc ::rbcCompare::Escape {text} {string map {& &amp; < &lt; > &gt; \" &quot;} $text}

proc ::rbcCompare::Embed {page out} {
    set ch [open [file join $out charts.map]]
    try {set map [read $ch]} finally {close $ch}
    set ch [open $page]; try {set text [read $ch]} finally {close $ch}
    set replacements {}
    dict for {mark file} $map {
        set ch [open [file join $out $file]]
        try {lappend replacements $mark [read $ch]} finally {close $ch}
    }
    WriteFile $page [string map $replacements $text]
}

proc ::rbcCompare::DrawPanel {} {
    variable panels; variable selected
    set key [dict get $panels $selected]
    variable plotData
    foreach {g ratio title} {.times 0 {Milliseconds (lower is better)} .ratios 1 {Native / Cairo (higher is better)}} {
        set names [$g element names]
        if {[llength $names]} {$g element delete {*}$names}
        $g configure -title $title
        set colors {#3366aa #cc6633 #339966 #8844aa}; set i 0
        dict for {name points} [Series [dict get $plotData $key] $ratio] {
            $g element create s$i -label $name -data [concat {*}$points] -color [lindex $colors [expr {$i%4}]] \
                -symbol circle -pixels 5 -linewidth 2
            incr i
        }
    }
}

proc ::rbcCompare::Viewer {data} {
    package require Tk
    package require rbc
    variable panels; variable selected; variable plotData $data
    set labels {}; set i 0
    foreach key [lsort [dict keys $data]] {
        set label "[incr i]. [Title $key]"
        dict set panels $label $key; lappend labels $label
    }
    wm title . {RBC renderer benchmark comparison}
    ttk::combobox .choose -state readonly -values $labels -textvariable ::rbcCompare::selected
    pack .choose -fill x
    bind .choose <<ComboboxSelected>> ::rbcCompare::DrawPanel
    foreach g {.times .ratios} {
        ::rbc::graph $g -width 1000 -height 330 -background white -plotbackground white
        $g axis configure x -title {Item count} -min 0
        $g axis configure y -min 0
        $g legend configure -position rightmargin
        pack $g -fill both -expand yes
    }
    set selected [lindex $labels 0]; DrawPanel
    bind . <Destroy> {if {%W eq "."} {set ::rbcCompare::closed 1}}
    vwait ::rbcCompare::closed
}

proc ::rbcCompare::Main {args} {
    set metrics {}; set inputs {}; set out renderer-comparison; set html 0; set gui 0; set snapshot {}; set browser {}; set embed {}
    while {[llength $args]} {
        set flag [lindex $args 0]; set args [lrange $args 1 end]
        if {$flag in {-html -gui}} {set [string range $flag 1 end] 1; continue}
        if {$flag eq "-help"} {
            puts {Usage: compare.tcl -input HOST PATH ?-input HOST PATH ...? -out DIR ?-html? ?-gui?
  -metrics redraw,axis-remap: optional metric filter.
  PATH: a benchmark CSV or directory. Use the same HOST label for matched runs.
  -snapshot svg|png -browser EXE: optional ticklecharts static exports (implies -html).
  -embed PAGE -out DIR: replace generated chart markers in a Ruff HTML page.}
            return
        }
        set n [expr {$flag eq "-input" ? 2 : 1}]
        if {[llength $args] < $n} {error "$flag requires $n argument(s)"}
        set value [lindex $args 0]
        switch -- $flag {
            -input {lappend inputs $value [lindex $args 1]}
            -metrics {set metrics [split $value ,]}
            -out {set out $value}
            -snapshot {if {$value ni {png svg}} {error "-snapshot must be png or svg"}; set snapshot $value; set html 1}
            -browser {set browser $value}
            -embed {set embed $value}
            default {error "unknown option: $flag"}
        }
        set args [lrange $args $n end]
    }
    if {$embed ne {}} {Embed $embed $out; return}
    if {![llength $inputs]} {error "supply -input HOST PATH; see -help"}
    set data [Load $inputs]
    if {[llength $metrics]} {
        dict for {key rows} $data {if {[lindex $key end] ni $metrics} {dict unset data $key}}
    }
    if {![dict size $data]} {error "no benchmark rows match"}
    puts [Report $data $out]
    if {$html} {Html $data $out $snapshot $browser}
    if {$gui} {Viewer $data}
}

if {[file normalize [info script]] eq [file normalize $::argv0]} {
    if {[catch {::rbcCompare::Main {*}$::argv} message]} {puts stderr $message; exit 1}
}
