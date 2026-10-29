# RBC.stripchart.axis.transform.M.tcl --
#
###Abstract
# This file tests the axis transform function of the stripchart BLT component.
# Axis transform is an instance function of stripchart.
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
	# Purpose: Ensure the axis transform command works correctly
	# ------------------------------------------------------------------------------------
	proc RBC.stripchart.axis.transform.M.1.1.Setup {} {
        stripchart .stripchart1
        .stripchart1 axis configure x y -min 0 -max 10 -loose no
        pack .stripchart1
        update idletasks
	}
	
	proc RBC.stripchart.axis.transform.M.1.1.Body {} {
        set value 5.0
        set pixel [.stripchart1 axis transform x $value]
        set restored [.stripchart1 axis invtransform x $pixel]
        if {abs($restored-$value)>10.0/abs([.stripchart1 axis transform x 10]-[.stripchart1 axis transform x 0])} {
            error "axis round trip: expected $value, got $restored"
        }
        puts "CHECK axis round trip: data=$value pixel=$pixel restored=$restored (tolerance 1 pixel)"
	}
	
	proc RBC.stripchart.axis.transform.M.1.1.Cleanup {} {
		destroy .stripchart1
	}
}