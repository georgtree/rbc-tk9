# graph.marker.configure.common.tcl --
#
###Abstract
# This file contains the manual tests that test the common flags for
# the marker configure function of the graph BLT component.
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
	# Purpose: Ensure coords changes position on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.1.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc graph.marker.configure.common.1.1.Body {} {
		.graph1 marker configure Marker1 -coords {0.5 0.25}
	}
	
	proc graph.marker.configure.common.1.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure element links to the marker
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.2.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
		.graph1 element create element1 -hide true
	}
	
	proc graph.marker.configure.common.2.1.Body {} {
		.graph1 marker configure Marker1 -element element1
	}
	
	proc graph.marker.configure.common.2.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure hide removes the marker from view
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.3.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc graph.marker.configure.common.3.1.Body {} {
		.graph1 marker configure Marker1 -hide true
	}
	
	proc graph.marker.configure.common.3.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure under draws the marker below data elements
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.4.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc graph.marker.configure.common.4.1.Body {} {
		.graph1 marker configure Marker1 -under true
	}
	
	proc graph.marker.configure.common.4.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure xoffset draws the marker at a horizontal offset
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.5.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc graph.marker.configure.common.5.1.Body {} {
		.graph1 marker configure Marker1 -xoffset 1.5i
	}
	
	proc graph.marker.configure.common.5.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure yoffset draws the marker at a vertical offset
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.6.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc graph.marker.configure.common.6.1.Body {} {
		.graph1 marker configure Marker1 -yoffset 1.5i
	}
	
	proc graph.marker.configure.common.6.1.Cleanup {} {
		destroy .graph1
	}

	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the mapx of the marker can be set.
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.7.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 axis create Axis1 -min 0 -max 4
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc graph.marker.configure.common.7.1.Body {} {
		.graph1 xaxis use Axis1
		.graph1 marker configure Marker1 -mapx Axis1
	}
	
	proc graph.marker.configure.common.7.1.Cleanup {} {
		.graph1 axis delete Axis1
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the mapy of the marker can be set.
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.8.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 axis create Axis1 -min 0 -max 4
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc graph.marker.configure.common.8.1.Body {} {
		.graph1 yaxis use Axis1
		.graph1 marker configure Marker1 -mapy Axis1
	}
	
	proc graph.marker.configure.common.8.1.Cleanup {} {
		.graph1 axis delete Axis1
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the state of the marker can be set.
	# DOES NOT WORK
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.9.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc graph.marker.configure.common.9.1.Body {} {
		.graph1 marker configure Marker1 -state active
	}
	
	proc graph.marker.configure.common.9.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the bindtags of the marker can be set.
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.common.10.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
		.graph1 marker bind Binding1 <Double-1> {puts foobar}
	}
	
	proc graph.marker.configure.common.10.1.Body {} {
		.graph1 marker configure Marker1 -bindtags Binding1
	}
	
	proc graph.marker.configure.common.10.1.Cleanup {} {
		destroy Binding1
		destroy .graph1
	}
}
