# graph.marker.configure.line.tcl --
#
###Abstract
# This file contains the manual tests that test the line marker configure
# function of the graph BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.marker

package require rbc
namespace import rbc::*

namespace eval graph.marker {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring dashes changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.line.1.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create line -name marker1 -coords {0.5 0.5 1.0 1.0}
	}
	
	proc graph.marker.configure.line.1.1.Body {} {
		.graph1 marker configure marker1 -dashes {2 3 2 5 2 6}
	}
	
	proc graph.marker.configure.line.1.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring fill changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.line.2.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create line -name marker1 -coords {0.5 0.5 1.0 1.0}
	}
	
	proc graph.marker.configure.line.2.1.Body {} {
		.graph1 marker configure marker1 -fill red
	}
	
	proc graph.marker.configure.line.2.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring linewidth changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.line.3.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create line -name marker1 -coords {0.5 0.5 1.0 1.0}
	}
	
	proc graph.marker.configure.line.3.1.Body {} {
		.graph1 marker configure marker1 -linewidth 20
	}
	
	proc graph.marker.configure.line.3.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring outline changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.line.4.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create line -name marker1 -coords {0.5 0.5 1.0 1.0}
	}
	
	proc graph.marker.configure.line.4.1.Body {} {
		.graph1 marker configure marker1 -outline red
	}
	
	proc graph.marker.configure.line.4.1.Cleanup {} {
		destroy .graph1
	}
	
#	# ------------------------------------------------------------------------------------
#	# Purpose: Ensure dashoffset command works
#	# ------------------------------------------------------------------------------------
#	proc graph.marker.configure.common.5.1.Setup {} {
#		graph .graph1
#		pack .graph1
#		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
#	}
#	
#	proc graph.marker.configure.common.5.1.Body {} {
#		.graph1 marker configure Marker1 -dashoffset 3
#	}
#	
#	proc graph.marker.configure.common.5.1.Cleanup {} {
#		destroy .graph1
#	}
}
