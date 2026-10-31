# Generate compile-only probes; no RBC package or display is required.
# The caller compiles every generated file with the normal package flags.
if {[llength $argv] != 2} {
    error {usage: headerProbes.tcl outputDirectory auto|unix|windows}
}
lassign $argv output platform
if {$platform eq "auto"} {set platform $::tcl_platform(platform)}
if {$platform ni {unix windows}} {
    error "unknown platform: $platform"
}
file mkdir $output
set generic [file normalize [file join [file dirname [info script]] .. generic]]
set headers {}
foreach path [lsort [glob -directory $generic *.h]] {
    set name [file tail $path]
    # Generated declarations are included through the supported public entry.
    if {$name eq "rbcDecls.h"} {continue}
    if {$platform eq "unix" && $name in {rbcWin.h rbcWinConfig.h}} {continue}
    if {$platform eq "windows" && $name eq "rbcConfig.h"} {continue}
    lappend headers $name
}
proc writeProbe {output name headers} {
    set channel [open [file join $output $name.c] w]
    try {
        foreach header $headers {
            puts $channel "#include \"$header\""
        }
        # A nonempty translation unit also works with strict C compilers.
        puts $channel {typedef int RbcHeaderProbe;}
    } finally {
        close $channel
    }
}
foreach header $headers {
    set name [file rootname $header]
    writeProbe $output $name [list $header $header]
    writeProbe $output $name-before-graph [list $header rbcGraph.h]
    writeProbe $output $name-after-graph [list rbcGraph.h $header]
}
writeProbe $output all-forward $headers
writeProbe $output all-reverse [lreverse $headers]
writeProbe $output public-stubs {rbc.h rbcVector.h rbcDecls.h rbc.h}
set channel [open [file join $output public-stubs.c] a]
puts $channel {#ifdef _TK
#error "The public vector API must not include Tk"
#endif}
close $channel
# Ordinary identifiers must remain available to later SDK declarations.
writeProbe $output image-identifiers {rbcImage.h}
set channel [open [file join $output image-identifiers.c] a]
puts $channel {
typedef struct {
    unsigned short Red, Green, Blue, Alpha;
} RbcHeaderChannelNames;
void RbcHeaderPixelMembers(void) {
    Pix32 pixel;
    pixel.rgba.red = pixel.rgba.green = pixel.rgba.blue = pixel.rgba.alpha = 0;
    (void)pixel;
}
}
close $channel
# Check the selected values, not only the absence of redefinition warnings.
foreach order {before after} {
    if {$order eq "before"} {
        set includes {rbcDefaults.h rbcInt.h}
    } else {
        set includes {rbcInt.h rbcDefaults.h}
    }
    writeProbe $output defaults-$order $includes
    set channel [open [file join $output defaults-$order.c] a]
    puts $channel {
#ifdef WIN32
typedef char RbcHeaderBackground[
    sizeof(STD_NORMAL_BACKGROUND) == sizeof("SystemButtonFace") ? 1 : -1];
typedef char RbcHeaderFont[
    sizeof(STD_FONT) == sizeof("Arial 8") ? 1 : -1];
#else
typedef char RbcHeaderBackground[
    sizeof(STD_NORMAL_BACKGROUND) == sizeof("#d9d9d9") ? 1 : -1];
typedef char RbcHeaderFont[
    sizeof(STD_FONT) == sizeof("TkDefaultFont") ? 1 : -1];
#endif
}
    close $channel
}
puts "Generated [expr {3 * [llength $headers] + 6}] header probes in $output"
