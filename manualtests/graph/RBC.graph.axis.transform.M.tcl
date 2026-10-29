# RBC.graph.axis.transform.M.tcl --
#
###Abstract
# This file tests the axis transform function of the graph BLT component.
# Axis transform is an instance function of graph.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.axis

package require rbc
namespace import rbc::*

namespace eval graph.axis {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the axis transform command works correctly
	# ------------------------------------------------------------------------------------
	proc RBC.graph.axis.transform.M.1.1.Setup {} {
        graph .graph1
        .graph1 axis configure x y -min 0 -max 10 -loose no
        pack .graph1
        update idletasks
	}
	
	proc RBC.graph.axis.transform.M.1.1.Body {} {
        set value 5.0
        set pixel [.graph1 axis transform x $value]
        set restored [.graph1 axis invtransform x $pixel]
        if {abs($restored-$value)>10.0/abs([.graph1 axis transform x 10]-[.graph1 axis transform x 0])} {
            error "axis round trip: expected $value, got $restored"
        }
        puts "CHECK axis round trip: data=$value pixel=$pixel restored=$restored (tolerance 1 pixel)"
	}
	
	proc RBC.graph.axis.transform.M.1.1.Cleanup {} {
		destroy .graph1
	}
}