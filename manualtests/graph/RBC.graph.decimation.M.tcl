# RBC.graph.decimation.M.tcl --
#
###Abstract
# Visual manual tests for graph decimation.
# Each case describes what to inspect before and after its Body procedure.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.

package provide graph.decimation

package require rbc
namespace import rbc::*

namespace eval graph.decimation {
    # A dense trace retains its isolated positive and negative peaks. After: zoom
    # and vector update preserve both extrema; no connecting gaps should appear.
    proc RBC.graph.decimation.M.1.1.Setup {} {
        vector create denseX denseY
        denseX seq 0 10 0.001
        denseY expr {denseX * 0}
        denseY index 5000 0.9
        denseY index 6000 -0.9
        graph .graph1 -width 720 -height 480
        pack .graph1 -fill both -expand yes
        .graph1 axis configure y -min -1 -max 1
        .graph1 line create trace -xdata denseX -ydata denseY -symbol none -color red -decimate auto
    }

    proc RBC.graph.decimation.M.1.1.Body {} {
        .graph1 axis configure x -min 4 -max 7
        denseY index 5000 0.7
    }

    proc RBC.graph.decimation.M.1.1.Cleanup {} {
        destroy .graph1
        vector destroy denseX denseY
    }

    # A graph keeps its vector connection after rename and readonly locking.
    # After: explicitly unlock and update the renamed vector; the trace changes.
    proc RBC.graph.decimation.M.1.2.Setup {} {
        vector create values
        values set {1 4 2 5}
        graph .graph1 -width 720 -height 480
        pack .graph1 -fill both -expand yes
        .graph1 line create trace -xdata {0 1 2 3} -ydata values -color blue
        vector rename values renamedValues
        renamedValues readonly true
    }

    proc RBC.graph.decimation.M.1.2.Body {} {
        renamedValues readonly false
        renamedValues set {5 2 4 1}
    }

    proc RBC.graph.decimation.M.1.2.Cleanup {} {
        destroy .graph1
        vector destroy renamedValues
    }

}
