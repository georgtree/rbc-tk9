# Named tracks with shared time axes and live RBC vectors.
package require Tk
package require rbc

wm title . {RBC stacked tracks}
::rbc::trackgraph .g -trackresize yes -width 900 -height 620 -title {Shared time, independent Y scales}
.g axis configure x -title {Time (s)} -min 0 -max 10 -loose no
.g axis create samples -min 0 -max 1000 -loose no -title Samples
.g x2axis use samples
.g axis create voltage -min -1.5 -max 1.5 -loose no -title {Voltage (V)}
.g axis create current -min -15 -max 15 -loose no -title {Current (mA)}
.g axis create logic -min -0.2 -max 1.2 -loose no -title Enable
.g track create voltage -weight 2 -yaxes voltage
.g track create current -weight 1 -yaxes current
.g track create enable -weight 1 -yaxes logic

::rbc::vector create trackTime trackVoltage trackCurrent trackEnable
for {set i 0} {$i <= 1000} {incr i} {
    set t [expr {$i*0.01}]
    trackTime append $t
    trackVoltage append [expr {sin(2*acos(-1)*$t)}]
    trackCurrent append [expr {10*cos(2*acos(-1)*$t)}]
    trackEnable append [expr {int($t)%2}]
}
.g line create voltage -track voltage -mapy voltage -xdata trackTime -ydata trackVoltage \
    -symbol none -color steelblue -linewidth 2 -label Voltage
.g strip create current -track current -mapy current -xdata trackTime -ydata trackCurrent \
    -symbol none -color darkorange -linewidth 2 -label Current
.g line create enable -track enable -mapy logic -xdata trackTime -ydata trackEnable \
    -symbol none -color seagreen -linewidth 2 -smooth step -label Enable
.g grid configure -hide no

# Track-owned annotations share the row's scale and cannot spill into another row.
.g marker create line -name threshold -track voltage -mapy voltage \
    -coords {-Inf 0.5 Inf 0.5} -outline firebrick -dashes {4 4}
.g marker create text -name thresholdLabel -track voltage -mapy voltage \
    -coords {0.1 0.5} -text {Threshold: 0.5 V} -foreground firebrick -anchor sw
.g marker create polygon -name interval -track current -mapy current \
    -coords {2 -Inf 3 -Inf 3 Inf 2 Inf} -fill lightyellow -outline {} -under yes

# One vertical cursor spans the tracks; its horizontal line stays at pointer Y.
bind .g <Motion> {
    .g crosshairs configure -position @%x,%y
    .g crosshairs on
    set track [.g track identify %y]
    if {$track ne {}} {
        lassign [.g track invtransform $track %x %y] time value
        .status configure -text [format {%s: time %.4g, value %.4g} $track $time $value]
    }
}
bind .g <Leave> {.g crosshairs off}

# Horizontal navigation updates both time representations together.
proc TrackZoom {factor} {
    foreach axis {x samples} {
        lassign [.g axis limits $axis] lo hi
        set mid [expr {($lo+$hi)/2.0}]
        set half [expr {($hi-$lo)*$factor/2.0}]
        .g axis configure $axis -min [expr {$mid-$half}] -max [expr {$mid+$half}]
    }
}
proc TrackReset {} {
    .g axis configure x -min 0 -max 10
    .g axis configure samples -min 0 -max 1000
}
proc TrackWeight {} {
    .g track configure voltage -weight $::voltageWeight
}
set voltageWeight 2
set phase 0
proc TrackAdvance {} {
    set ::phase [expr {$::phase+0.2}]
    trackVoltage expr {sin(2*acos(-1)*trackTime + $::phase)}
    trackCurrent expr {10*cos(2*acos(-1)*trackTime + $::phase)}
}
ttk::frame .controls
ttk::button .controls.in -text {Zoom X in} -command {TrackZoom 0.8}
ttk::button .controls.out -text {Zoom X out} -command {TrackZoom 1.25}
ttk::button .controls.reset -text Reset -command TrackReset
ttk::button .controls.reverse -text {Reverse tracks} -command {.g track order [lreverse [.g track order]]}
ttk::button .controls.advance -text {Update vectors} -command TrackAdvance
ttk::label .controls.label -text {Voltage track weight}
ttk::spinbox .controls.weight -from 0.1 -to 5 -increment 0.1 -width 8 -textvariable voltageWeight -command TrackWeight
pack .controls.in .controls.out .controls.reset .controls.advance .controls.reverse .controls.label .controls.weight -side left -padx 4 -pady 5
proc TrackWeightDisplay {} {
    set ::voltageWeight [format %.4g [.g track cget voltage -weight]]
}
bind .g <<RbcGraphChanged>> TrackWeightDisplay
ttk::label .status -text {Drag a gap to resize tracks; Escape cancels. Move over a trace to read values.}
grid .controls -sticky ew
grid .g -sticky nsew
grid .status -sticky ew -padx 8 -pady 4
grid columnconfigure . 0 -weight 1
grid rowconfigure . 1 -weight 1
