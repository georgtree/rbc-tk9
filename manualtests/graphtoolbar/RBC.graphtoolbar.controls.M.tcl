# RBC.graphtoolbar.controls.M.tcl --
#
###Abstract
# Visual manual tests for graphtoolbar controls.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide graphtoolbar.controls

package require rbc
namespace import rbc::*

package require rbc::graphtoolbar
namespace import rbc::*

namespace eval graphtoolbar.controls {
    # Toolbar and context menu coexist. Try zoom box, Shift-drag pan, Ctrl-wheel,
    # legend clicks and Export menus. After: show-hide legend mode toggles visibility.
    proc RBC.graphtoolbar.controls.M.1.1.Setup {} {
        graphtoolbar .toolbar1 -interactive -controlmode both -activelegendmode show-hide
        pack .toolbar1 -fill both -expand yes
        .toolbar1 graph line create a -data {0 1 1 4 2 2 3 5} -label Short
        .toolbar1 graph line create b -data {0 5 1 2 2 4 3 1} -label {Longer label}
    }

    proc RBC.graphtoolbar.controls.M.1.1.Body {} {
        .toolbar1 graph configure -title {Click legend: show / hide; inspect SVG and PDF dialogs}
    }

    proc RBC.graphtoolbar.controls.M.1.1.Cleanup {} {
        destroy .toolbar1
    }

    # Context-only controls and show-active legend cycle. Right-click to open export
    # submenu. After: zoom/pan and return to original limits using middle click.
    proc RBC.graphtoolbar.controls.M.1.2.Setup {} {
        graphtoolbar .toolbar1 -interactive -controlmode contextmenu -activelegendmode show-active
        pack .toolbar1 -fill both -expand yes
        .toolbar1 graph line create a -data {0 1 1 4 2 2 3 5}
    }

    proc RBC.graphtoolbar.controls.M.1.2.Body {} {
        .toolbar1 graph configure -title {Click legend: show / active}
    }

    proc RBC.graphtoolbar.controls.M.1.2.Cleanup {} {
        destroy .toolbar1
    }

    # Track toolbar: crosshairs and zoom select the owning track. Click local legends,
    # drag separator, and zoom one track; other Y range should remain unchanged.
    proc RBC.graphtoolbar.controls.M.1.3.Setup {} {
        graphtoolbar .toolbar1 -type trackgraph -interactive -controlmode both
        pack .toolbar1 -fill both -expand yes
        set graph [.toolbar1 subwidget graph]
        $graph configure -trackresize yes -trackseparatorcolor gray50
        $graph axis create volts -min -1 -max 1
        $graph axis create amps -min -10 -max 10
        foreach {track axis data} {voltage volts {0 -1 5 1 10 -1} current amps {0 10 5 -10 10 10}} {
            $graph track create $track -yaxes $axis
            $graph line create $track -track $track -mapy $axis -data $data -symbol none
            $graph track legend $track configure -hide no -position right
        }
    }

    proc RBC.graphtoolbar.controls.M.1.3.Body {} {
        .toolbar1 graph track order {current voltage}
    }

    proc RBC.graphtoolbar.controls.M.1.3.Cleanup {} {
        destroy .toolbar1
    }

}
