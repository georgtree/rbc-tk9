# Optional separator gestures for the native trackgraph widget.
# See license.terms for details. The native track commands own the layout;
# these private bindings only translate pointer gestures into those commands.
namespace eval ::rbc::trackgraph {
    variable drag {}
    variable cursors {}

    proc RestoreCursor {w} {
        variable cursors
        if {[dict exists $cursors $w]} {
            set old [dict get $cursors $w]
            dict unset cursors $w
            if {[winfo exists $w] && [$w cget -cursor] in {sb_v_double_arrow sb_h_double_arrow}} {
                $w configure -cursor $old
            }
        }
    }

    # Borrow keyboard focus for Escape only while the gesture is active. Do
    # not override focus moved elsewhere by the application or the user.
    proc RestoreFocus {w state} {
        if {![winfo exists $w] || [focus -displayof $w] ne $w} {return}
        set old [dict get $state focus]
        # Tk treats "focus {}" as a no-op, not a request to clear focus.
        if {$old eq {} || ![winfo exists $old]} {set old [winfo toplevel $w]}
        focus $old
    }

    proc Hover {w x y} {
        variable drag
        variable cursors
        if {[dict exists $drag $w]} {return}
        if {[$w cget -trackresize] && [$w track separator $x $y] ne {}} {
            if {![dict exists $cursors $w]} {
                dict set cursors $w [$w cget -cursor]
                $w configure -cursor [expr {[$w cget -invertxy] ? "sb_h_double_arrow" : "sb_v_double_arrow"}]
            }
        } else {
            RestoreCursor $w
        }
    }

    proc ValidPair {w state} {
        if {![winfo exists $w] || ![$w cget -trackresize]} {return 0}
        set names [$w track names]
        if {$names ne [dict get $state order] || [$w cget -invertxy] != [dict get $state inverted]} {return 0}
        set i [lsearch -exact $names [dict get $state first]]
        expr {$i >= 0 && [lindex $names [expr {$i+1}]] eq [dict get $state second]}
    }

    proc Press {w x y} {
        variable drag
        if {![$w cget -trackresize] || [dict exists $drag $w]} {return}
        set first [$w track separator $x $y]
        if {$first eq {}} {return}
        set names [$w track names]
        set second [lindex $names [expr {[lsearch -exact $names $first]+1}]]
        Hover $w $x $y
        set oldFocus [focus -displayof $w]
        dict set drag $w [dict create focus $oldFocus first $first second $second order $names inverted [$w cget -invertxy] \
            offset [expr {([$w cget -invertxy] ? $x : $y)-[$w track sashpos $first]}] \
            firstWeight [$w track cget $first -weight] secondWeight [$w track cget $second -weight]]
        focus $w
        return -code break
    }

    proc Motion {w x y} {
        variable drag
        if {![dict exists $drag $w]} {return}
        set state [dict get $drag $w]
        if {![ValidPair $w $state]} {
            dict unset drag $w
            RestoreCursor $w
            RestoreFocus $w $state
            return -code break
        }
        $w track sashpos [dict get $state first] [expr {([$w cget -invertxy] ? $x : $y)-[dict get $state offset]}]
        return -code break
    }

    proc Release {w x y} {
        variable drag
        if {![dict exists $drag $w]} {return}
        set state [dict get $drag $w]
        try {
            Motion $w $x $y
        } on break {} {
            # Motion consumes the binding event while a drag is active.
        } finally {
            if {[dict exists $drag $w]} {dict unset drag $w}
            RestoreCursor $w
            RestoreFocus $w $state
        }
        return -code break
    }

    proc Cancel {w} {
        variable drag
        if {![dict exists $drag $w]} {return}
        set state [dict get $drag $w]
        dict unset drag $w
        try {
            if {[ValidPair $w $state]} {
                $w track configure [dict get $state first] -weight [dict get $state firstWeight]
                $w track configure [dict get $state second] -weight [dict get $state secondWeight]
            }
        } finally {
            RestoreCursor $w
            RestoreFocus $w $state
        }
        return -code break
    }

    proc Leave {w} {
        variable drag
        if {![dict exists $drag $w]} {RestoreCursor $w}
    }

    proc Destroy {w} {
        variable drag
        variable cursors
        if {[dict exists $drag $w]} {dict unset drag $w}
        if {[dict exists $cursors $w]} {dict unset cursors $w}
    }
}
bind Trackgraph <Motion> {::rbc::trackgraph::Hover %W %x %y}
bind Trackgraph <ButtonPress-1> {::rbc::trackgraph::Press %W %x %y}
bind Trackgraph <B1-Motion> {::rbc::trackgraph::Motion %W %x %y}
bind Trackgraph <ButtonRelease-1> {::rbc::trackgraph::Release %W %x %y}
bind Trackgraph <Escape> {::rbc::trackgraph::Cancel %W}
bind Trackgraph <Leave> {::rbc::trackgraph::Leave %W}
bind Trackgraph <Destroy> {::rbc::trackgraph::Destroy %W}
