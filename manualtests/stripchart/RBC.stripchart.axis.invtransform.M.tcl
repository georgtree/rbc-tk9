# RBC.stripchart.axis.invtransform.M.tcl --
#
###Abstract
# This file tests the axis invtransform function of the stripchart BLT component.
# Axis invtransform is an instance function of stripchart.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide stripchart.axis

package require rbc
namespace import rbc::*

namespace eval stripchart.axis {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the axis invtransform command works correctly
	# ------------------------------------------------------------------------------------
	proc RBC.stripchart.axis.invtransform.M.1.1.Setup {} {
        stripchart .stripchart1
        .stripchart1 axis configure x y -min 0 -max 10 -loose no
        pack .stripchart1
        update idletasks
	}
	
	proc RBC.stripchart.axis.invtransform.M.1.1.Body {} {
        set pixel [expr {[winfo width .stripchart1]/2}]
        set value [.stripchart1 axis invtransform x $pixel]
        set restored [.stripchart1 axis transform x $value]
        if {abs($restored-$pixel)>1} {
            error "axis inverse round trip: expected $pixel, got $restored"
        }
        puts "CHECK axis inverse round trip: pixel=$pixel data=$value restored=$restored (tolerance 1 pixel)"
	}
	
	proc RBC.stripchart.axis.invtransform.M.1.1.Cleanup {} {
		destroy .stripchart1
	}
}