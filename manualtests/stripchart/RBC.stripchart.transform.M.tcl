# RBC.stripchart.transform.1.tcl --
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
package provide stripchart.transform

package require rbc
namespace import rbc::*

namespace eval stripchart.transform {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure transform works as specified
	# ------------------------------------------------------------------------------------
	proc RBC.stripchart.transform.M.1.1.Setup {} {
        stripchart .stripchart1
        .stripchart1 axis configure x y -min 0 -max 10 -loose no
        pack .stripchart1
        update idletasks
	}
	
	proc RBC.stripchart.transform.M.1.1.Body {} {
        set x 3.09330143541
        set y 5.15030674847
        lassign [.stripchart1 transform $x $y] px py
        lassign [.stripchart1 invtransform $px $py] restoredX restoredY
        lassign [.stripchart1 transform 0 0] x0 y0
        lassign [.stripchart1 transform 10 10] x1 y1
        if {abs($restoredX-$x)>10.0/abs($x1-$x0) || abs($restoredY-$y)>10.0/abs($y1-$y0)} {
            error "transform round trip: expected $x,$y, got $restoredX,$restoredY"
        }
        puts "CHECK transform round trip: data=$x,$y pixel=$px,$py restored=$restoredX,$restoredY (tolerance 1 pixel)"
	}
	
	proc RBC.stripchart.transform.M.1.1.Cleanup {} {
		destroy .stripchart1
	}
}
