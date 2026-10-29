package require rbc
namespace import ::rbc::*

proc MakeSpiral {name} {
    vector create $name -type complex
    set values {}
    for {set i 0} {$i <= 120} {incr i} {
        set theta [expr {$i * 6.0 * acos(-1.0) / 180.0}]
        set radius [expr {0.12 + 0.0065 * $i}]
        lappend values [list [expr {$radius * cos($theta)}] [expr {$radius * sin($theta)}]]
    }
    $name set $values
}

set winSize {-width 800 -height 800}
set titleFont {-font {Helvetica 10}}
set widget [graphtoolbar .gtb {*}$winSize -type polar -zoom -zoommod {Any-} -zoomtitle -controlmode context -zoommark -pan -representation polar\
                    -zoomboxopts {-linewidth 1} -zoommarkboxopts {-outline grey -fill #FFEB3B} -crosshairs\
                    -crosshairsmode closest -crosshairsclosestopts {-single true -halo 50 -hide no} -scaletoggle x\
                    -activelegend -zoomwheel]
$widget graph grid on
$widget graph configure -invertxy no -renderer cairo

grid .gtb -row 0 -column 0 -sticky nsew
grid columnconfigure . 0 -weight 1
grid rowconfigure . 0 -weight 1
$widget graph configure -title {Interactive crosshairs with closes element coordinate marker} {*}$titleFont
MakeSpiral spiral
$widget graph element create line1 -cdata spiral

puts [$widget getAxisActiveScale]
set file [open svgtests.svg w+]
puts $file [$widget graph svg output]
close $file
