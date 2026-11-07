# winop.map.tcl --
#
###Abstract
# This file contains the manual tests that test the map 
# function of the winop BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide winop.map

package require rbc
namespace import rbc::*

namespace eval winop.map {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the map function of the winop component works correctly when given a
	# single exiting window name.
	# ------------------------------------------------------------------------------------
	proc winop.map.1.1.Setup {} {
		graph .graph1 -background blue
		pack .graph1
	}
	
	proc winop.map.1.1.Body {} {
		winop map .graph1
	}
	
	proc winop.map.1.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the map function of the winop component works correctly when given
	# multiple exiting window names.
	# ------------------------------------------------------------------------------------
	proc winop.map.1.2.Setup {} {
		graph .graph1 -background blue
		graph .graph2 -background red
		pack .graph1
		pack .graph2
	}
	
	proc winop.map.1.2.Body {} {
		winop map .graph1 .graph2
	}
	
	proc winop.map.1.2.Cleanup {} {
		destroy .graph2
		destroy .graph1
	}
}
