# graph.marker.configure.polygon.tcl --
#
###Abstract
# This file tests the different configuration options of polygon markers.
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
	proc graph.marker.configure.polygon.1.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create polygon -name marker1 -coords {0 0 1 1 0 1} -linewidth 10
	}
	
	proc graph.marker.configure.polygon.1.1.Body {} {
		.graph1 marker configure marker1 -dashes {2 3 2 5 2 6} 
	}
	
	proc graph.marker.configure.polygon.1.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring fill changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.polygon.2.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create polygon -name marker1 -coords {0 0 1 1 0 1}
	}
	
	proc graph.marker.configure.polygon.2.1.Body {} {
		.graph1 marker configure marker1 -fill blue
	}
	
	proc graph.marker.configure.polygon.2.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring linewidth changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.polygon.3.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create polygon -name marker1 -coords {0 0 1 1 0 1}
	}
	
	proc graph.marker.configure.polygon.3.1.Body {} {
		.graph1 marker configure marker1 -linewidth 12
	}
	
	proc graph.marker.configure.polygon.3.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring outline changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.polygon.4.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create polygon -name marker1 -coords {0 0 1 1 0 1} -linewidth 10
	}
	
	proc graph.marker.configure.polygon.4.1.Body {} {
		.graph1 marker configure marker1 -outline green
	}
	
	proc graph.marker.configure.polygon.4.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring stipple changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.polygon.5.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create polygon -name marker1 -coords {0 0 1 1 0 1}
	}
	
	proc graph.marker.configure.polygon.5.1.Body {} {
		.graph1 marker configure marker1 -stipple @greenback.xbm
	}
	
	proc graph.marker.configure.polygon.5.1.Cleanup {} {
		destroy .graph1
	}
}
