# graph.element.create.tcl --
#
###Abstract
# This file contains the manual tests that test the element create 
# function of the graph BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.element

package require rbc
namespace import rbc::*

namespace eval graph.element {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the element create command works correctly when given a unique 
	# element name.
	# ------------------------------------------------------------------------------------
	proc graph.element.create.1.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.element.create.1.1.Body {} {
		.graph1 element create Element1
	}
	
	proc graph.element.create.1.1.Cleanup {} {
		.graph1 element delete Element1
		destroy .graph1
	}
}