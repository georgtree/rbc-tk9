# barchart.marker.configure.polygon.tcl --
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
package provide barchart.marker

package require rbc
namespace import rbc::*

namespace eval barchart.marker {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring dashes changes on screen
	# ------------------------------------------------------------------------------------
	proc barchart.marker.configure.polygon.1.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1} -linewidth 10
	}
	
	proc barchart.marker.configure.polygon.1.1.Body {} {
		.barchart1 marker configure marker1 -dashes {2 3 2 5 2 6} 
	}
	
	proc barchart.marker.configure.polygon.1.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring fill changes on screen
	# ------------------------------------------------------------------------------------
	proc barchart.marker.configure.polygon.2.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1}
	}
	
	proc barchart.marker.configure.polygon.2.1.Body {} {
		.barchart1 marker configure marker1 -fill blue
	}
	
	proc barchart.marker.configure.polygon.2.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring linewidth changes on screen
	# ------------------------------------------------------------------------------------
	proc barchart.marker.configure.polygon.3.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1}
	}
	
	proc barchart.marker.configure.polygon.3.1.Body {} {
		.barchart1 marker configure marker1 -linewidth 12
	}
	
	proc barchart.marker.configure.polygon.3.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring outline changes on screen
	# ------------------------------------------------------------------------------------
	proc barchart.marker.configure.polygon.4.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1} -linewidth 10
	}
	
	proc barchart.marker.configure.polygon.4.1.Body {} {
		.barchart1 marker configure marker1 -outline green
	}
	
	proc barchart.marker.configure.polygon.4.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring stipple changes on screen
	# ------------------------------------------------------------------------------------
	proc barchart.marker.configure.polygon.5.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 marker create polygon -name marker1 -coords {0 0 1 1 0 1}
	}
	
	proc barchart.marker.configure.polygon.5.1.Body {} {
		.barchart1 marker configure marker1 -stipple @greenback.xbm
	}
	
	proc barchart.marker.configure.polygon.5.1.Cleanup {} {
		destroy .barchart1
	}
}
