# RBC.barchart.axis.transform.M.tcl --
#
###Abstract
# This file tests the axis transform function of the barchart BLT component.
# Axis transform is an instance function of barchart.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide barchart.axis

package require rbc
namespace import rbc::*

namespace eval barchart.axis {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the axis transform command works correctly
	# ------------------------------------------------------------------------------------
	proc RBC.barchart.axis.transform.M.1.1.Setup {} {
        barchart .barchart1
        .barchart1 axis configure x y -min 0 -max 10 -loose no
        pack .barchart1
        update idletasks
	}
	
	proc RBC.barchart.axis.transform.M.1.1.Body {} {
        set value 5.0
        set pixel [.barchart1 axis transform x $value]
        set restored [.barchart1 axis invtransform x $pixel]
        if {abs($restored-$value)>10.0/abs([.barchart1 axis transform x 10]-[.barchart1 axis transform x 0])} {
            error "axis round trip: expected $value, got $restored"
        }
        puts "CHECK axis round trip: data=$value pixel=$pixel restored=$restored (tolerance 1 pixel)"
	}
	
	proc RBC.barchart.axis.transform.M.1.1.Cleanup {} {
		destroy .barchart1
	}
}