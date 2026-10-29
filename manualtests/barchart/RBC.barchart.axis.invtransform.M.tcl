# RBC.barchart.axis.invtransform.M.tcl --
#
###Abstract
# This file tests the axis invtransform function of the barchart BLT component.
# Axis invtransform is an instance function of barchart.
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
	proc RBC.barchart.axis.invtransform.M.1.1.Setup {} {
        barchart .barchart1
        .barchart1 axis configure x y -min 0 -max 10 -loose no
        pack .barchart1
        update idletasks
	}
	
	proc RBC.barchart.axis.invtransform.M.1.1.Body {} {
        set pixel [expr {[winfo width .barchart1]/2}]
        set value [.barchart1 axis invtransform x $pixel]
        set restored [.barchart1 axis transform x $value]
        if {abs($restored-$pixel)>1} {
            error "axis inverse round trip: expected $pixel, got $restored"
        }
        puts "CHECK axis inverse round trip: pixel=$pixel data=$value restored=$restored (tolerance 1 pixel)"
	}
	
	proc RBC.barchart.axis.invtransform.M.1.1.Cleanup {} {
		destroy .barchart1
	}
}