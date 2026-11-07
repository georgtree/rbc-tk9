# stripchart.marker.configure.polygon.tcl --
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
package provide stripchart.marker

package require rbc
namespace import rbc::*

namespace eval stripchart.marker {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring dashes changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.polygon.1.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1} -linewidth 10
	}
	
	proc stripchart.marker.configure.polygon.1.1.Body {} {
		.stripchart1 marker configure marker1 -dashes {2 3 2 5 2 6} 
	}
	
	proc stripchart.marker.configure.polygon.1.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring fill changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.polygon.2.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1}
	}
	
	proc stripchart.marker.configure.polygon.2.1.Body {} {
		.stripchart1 marker configure marker1 -fill blue
	}
	
	proc stripchart.marker.configure.polygon.2.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring linewidth changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.polygon.3.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1}
	}
	
	proc stripchart.marker.configure.polygon.3.1.Body {} {
		.stripchart1 marker configure marker1 -linewidth 12
	}
	
	proc stripchart.marker.configure.polygon.3.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring outline changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.polygon.4.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1} -linewidth 10
	}
	
	proc stripchart.marker.configure.polygon.4.1.Body {} {
		.stripchart1 marker configure marker1 -outline green
	}
	
	proc stripchart.marker.configure.polygon.4.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring stipple changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.polygon.5.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1}
	}
	
	proc stripchart.marker.configure.polygon.5.1.Body {} {
		.stripchart1 marker configure marker1 -stipple @greenback.xbm
	}
	
	proc stripchart.marker.configure.polygon.5.1.Cleanup {} {
		destroy .stripchart1
	}
}
