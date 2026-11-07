# stripchart.marker.configure.common.tcl --
#
###Abstract
# This file contains the manual tests that test the common flags for
# the marker configure function of the stripchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide stripchart.marker

package require rbc
namespace import rbc::*

namespace eval stripchart.marker {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure coords changes position on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.1.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc stripchart.marker.configure.common.1.1.Body {} {
		.stripchart1 marker configure Marker1 -coords {0.5 0.25}
	}
	
	proc stripchart.marker.configure.common.1.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure element links to the marker
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.2.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
		.stripchart1 element create element1 -hide true
	}
	
	proc stripchart.marker.configure.common.2.1.Body {} {
		.stripchart1 marker configure Marker1 -element element1
	}
	
	proc stripchart.marker.configure.common.2.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure hide removes the marker from view
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.3.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc stripchart.marker.configure.common.3.1.Body {} {
		.stripchart1 marker configure Marker1 -hide true
	}
	
	proc stripchart.marker.configure.common.3.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure under draws the marker below data elements
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.4.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc stripchart.marker.configure.common.4.1.Body {} {
		.stripchart1 marker configure Marker1 -under true
	}
	
	proc stripchart.marker.configure.common.4.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure xoffset draws the marker at a horizontal offset
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.5.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc stripchart.marker.configure.common.5.1.Body {} {
		.stripchart1 marker configure Marker1 -xoffset 1.5i
	}
	
	proc stripchart.marker.configure.common.5.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure yoffset draws the marker at a vertical offset
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.6.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc stripchart.marker.configure.common.6.1.Body {} {
		.stripchart1 marker configure Marker1 -yoffset 1.5i
	}
	
	proc stripchart.marker.configure.common.6.1.Cleanup {} {
		destroy .stripchart1
	}

	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the mapx of the marker can be set.
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.7.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 axis create Axis1 -min 0 -max 4
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc stripchart.marker.configure.common.7.1.Body {} {
		.stripchart1 xaxis use Axis1
		.stripchart1 marker configure Marker1 -mapx Axis1
	}
	
	proc stripchart.marker.configure.common.7.1.Cleanup {} {
		.stripchart1 axis delete Axis1
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the mapy of the marker can be set.
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.8.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 axis create Axis1 -min 0 -max 4
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc stripchart.marker.configure.common.8.1.Body {} {
		.stripchart1 yaxis use Axis1
		.stripchart1 marker configure Marker1 -mapy Axis1
	}
	
	proc stripchart.marker.configure.common.8.1.Cleanup {} {
		.stripchart1 axis delete Axis1
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the state of the marker can be set.
	# DOES NOT WORK
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.9.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
	}
	
	proc stripchart.marker.configure.common.9.1.Body {} {
		.stripchart1 marker configure Marker1 -state active
	}
	
	proc stripchart.marker.configure.common.9.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the bindtags of the marker can be set.
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.common.10.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create text -name Marker1 -coords {0.5 0.5} -text "Marker1"
		.stripchart1 marker bind Binding1 <Double-1> {puts foobar}
	}
	
	proc stripchart.marker.configure.common.10.1.Body {} {
		.stripchart1 marker configure Marker1 -bindtags Binding1
	}
	
	proc stripchart.marker.configure.common.10.1.Cleanup {} {
		destroy Binding1
		destroy .stripchart1
	}
}
