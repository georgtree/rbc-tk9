# RBC.graph.transform.1.tcl --
#
###Abstract
# This file tests the transform functions that do not test properly in eclipse.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.transform

package require rbc
namespace import rbc::*

namespace eval graph.transform {
	set tcl_precision 12
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure transform works as specified.
	# ------------------------------------------------------------------------------------
	proc RBC.graph.transform.M.1.1.Setup {} {
        graph .graph1
        .graph1 axis configure x y -min 0 -max 10 -loose no
        pack .graph1
        update idletasks
	}
	
	proc RBC.graph.transform.M.1.1.Body {} {
        set x 3.09330143541
        set y 5.15030674847
        lassign [.graph1 transform $x $y] px py
        lassign [.graph1 invtransform $px $py] restoredX restoredY
        lassign [.graph1 transform 0 0] x0 y0
        lassign [.graph1 transform 10 10] x1 y1
        if {abs($restoredX-$x)>10.0/abs($x1-$x0) || abs($restoredY-$y)>10.0/abs($y1-$y0)} {
            error "transform round trip: expected $x,$y, got $restoredX,$restoredY"
        }
        puts "CHECK transform round trip: data=$x,$y pixel=$px,$py restored=$restoredX,$restoredY (tolerance 1 pixel)"
	}
	
	proc RBC.graph.transform.M.1.1.Cleanup {} {
		destroy .graph1
	}
}
