# RBC.graph.invtransform.M.tcl --
#
###Abstract
# This file tests the invtransform functions that do not test properly in eclipse.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.invtransform

package require rbc
namespace import rbc::*

namespace eval graph.invtransform {
	set tcl_precision 12
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the invtransform command works correctly
	# ------------------------------------------------------------------------------------
	proc RBC.graph.invtransform.M.1.1.Setup {} {
        graph .graph1
        .graph1 axis configure x y -min 0 -max 10 -loose no
        pack .graph1
        update idletasks
	}
	
	proc RBC.graph.invtransform.M.1.1.Body {} {
        set px [expr {[winfo width .graph1]/2.0}]
        set py [expr {[winfo height .graph1]/2.0}]
        lassign [.graph1 invtransform $px $py] x y
        lassign [.graph1 transform $x $y] restoredX restoredY
        if {abs($restoredX-$px)>1 || abs($restoredY-$py)>1} {
            error "inverse round trip: expected $px,$py, got $restoredX,$restoredY"
        }
        puts "CHECK inverse round trip: pixel=$px,$py data=$x,$y restored=$restoredX,$restoredY (tolerance 1 pixel)"
	}
	
	proc RBC.graph.invtransform.M.1.1.Cleanup {} {
		destroy .graph1
	}
}