set files {axisRunAll.tcl barRunAll.tcl configureRunAll.tcl configureRunAll.tcl crosshairsRunAll.tcl elementRunAll.tcl\
               extentsRunAll.tcl gridRunAll.tcl insideRunAll.tcl invtransformRunAll.tcl invtransformRunAll.tcl\
               legendRunAll.tcl markerRunAll.tcl penRunAll.tcl postscriptRunAll.tcl snapRunAll.tcl transformRunAll.tcl}
proc Crosshairs {graph {event Any-Motion} {state on}} {
    $graph crosshairs $state
    bind crosshairs-$graph <$event> {
        %W crosshairs configure -position @%x,%y
    }
    bind crosshairs-$graph <Leave> {
        %W crosshairs off
    }
    bind crosshairs-$graph <Enter> {
        %W crosshairs on
    }
    $graph crosshairs configure -color red
    if {$state eq {on}} {
        AddBindTag $graph crosshairs-$graph
    } elseif {$state eq {off}} {
        RemoveBindTag $graph crosshairs-$graph
    }
}
proc AddBindTag {widget tag} {
    set oldTagList [bindtags $widget]
    if {[lsearch $oldTagList $tag] < 0} {
        bindtags $widget [linsert $oldTagList 0 $tag]
    }
}
proc RemoveBindTag {widget tag} {
    set oldTagList [bindtags $widget]
    set index [lsearch $oldTagList $tag]
    if {$index >= 0} {
        bindtags $widget [lreplace $oldTagList $index $index]
    }
}
proc Rbc_Crosshairs {graph} {
    # Initialize and enable crosshairs for provided graph/stripchart/barchart
    #   graph - name of the graph/stripchart/barchart widget
    Crosshairs $graph
}
foreach file $files {
    source $file
}
