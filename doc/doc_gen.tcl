package require fileutil

set script_path [file dirname [file normalize [info script]]]

# nroff generating
set files {index.md graph.md stripchart.md barchart.md spline.md vector.md winop.md}

foreach file [lrange $files 1 end] {
    exec md2man-roff [file join $script_path $file] > [file join $script_path [file rootname $file].n]
}

# html generating
foreach file $files {
    catch {exec {*}[list pandoc [file join $script_path $file] -s -o [file join $script_path\
                                                                              [file rootname $file].html]]} errorStr
    puts $errorStr
    lappend html_files [file join $script_path [file rootname $file].html]
}

proc processContents {fileContents} {
    return [string map {{max-width: 36em} max-width:72em .md .html} $fileContents]
}
foreach html_file $html_files {
    fileutil::updateInPlace $html_file processContents
}
