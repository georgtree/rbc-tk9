# graph.axis.bind.tcl --
#
###Abstract
# This file contains the manual tests that test the axis bind
# function of the graph BLT component.
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
	# Purpose: Ensure bind actions execute
	# ------------------------------------------------------------------------------------
	proc graph.axis.bind.1.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.axis.bind.1.1.Body {} {
		.graph1 axis bind x <Double-1> {puts test}
	}
	
	proc graph.axis.bind.1.1.Cleanup {} {
		destroy .graph1
	}
}
