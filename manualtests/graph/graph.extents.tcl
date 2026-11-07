# graph.extents.tcl --
#
###Abstract
# This file tests the extent function of the graph BLT Component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.extents

package require rbc
namespace import rbc::*

namespace eval graph.extents {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the graph extents are valid for leftmargin
	# ------------------------------------------------------------------------------------
	proc graph.extents.1.1.Setup {} {
		graph .graph1 -leftmargin 20
		pack .graph1
	}
	
	proc graph.extents.1.1.Body {} {
		.graph1 extents leftmargin
	}
	
	proc graph.extents.1.1.Cleanup {} {
		destroy .graph1
	}

	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the graph extents are valid for rightmargin
	# ------------------------------------------------------------------------------------
	proc graph.extents.2.1.Setup {} {
		graph .graph1 -rightmargin 20
		pack .graph1
	}
	
	proc graph.extents.2.1.Body {} {
		.graph1 extents rightmargin
	}
	
	proc graph.extents.2.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the graph extents are valid for topmargin
	# ------------------------------------------------------------------------------------
	proc graph.extents.3.1.Setup {} {
		graph .graph1 -topmargin 20
		pack .graph1
	}
	
	proc graph.extents.3.1.Body {} {
		.graph1 extents topmargin
	}
	
	proc graph.extents.3.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the graph extents are valid for bottommargin
	# ------------------------------------------------------------------------------------
	proc graph.extents.4.1.Setup {} {
		graph .graph1 -bottommargin 20
		pack .graph1
	}
	
	proc graph.extents.4.1.Body {} {
		.graph1 extents bottommargin
	}
	
	proc graph.extents.4.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the graph extents are valid for plotwidth
	# ------------------------------------------------------------------------------------
	proc graph.extents.5.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.extents.5.1.Body {} {
		.graph1 extents plotwidth
	}
	
	proc graph.extents.5.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the graph extents are valid for plotheight
	# ------------------------------------------------------------------------------------
	proc graph.extents.6.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.extents.6.1.Body {} {
		.graph1 extents plotheight
	}
	
	proc graph.extents.6.1.Cleanup {} {
		destroy .graph1
	}
}
