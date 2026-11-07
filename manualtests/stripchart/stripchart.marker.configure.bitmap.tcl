# stripchart.marker.configure.bitmap.test --
#
###Abstract
# This file contains the manual tests that test the bitmap marker configure
# function of the stripchart BLT component.
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
	# Purpose: Ensure configuring background changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.bitmap.1.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc stripchart.marker.configure.bitmap.1.1.Body {} {
		.stripchart1 marker configure marker1 -background red
	}
	
	proc stripchart.marker.configure.bitmap.1.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring bitmap changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.bitmap.2.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create bitmap -name marker1 -coords {0 0}
	}
	
	proc stripchart.marker.configure.bitmap.2.1.Body {} {
		.stripchart1 marker configure marker1 -bitmap @greenback.xbm
	}
	
	proc stripchart.marker.configure.bitmap.2.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring fill changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.bitmap.3.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc stripchart.marker.configure.bitmap.3.1.Body {} {
		.stripchart1 marker configure marker1 -fill red
	}
	
	proc stripchart.marker.configure.bitmap.3.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring foreground changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.bitmap.4.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc stripchart.marker.configure.bitmap.4.1.Body {} {
		.stripchart1 marker configure marker1 -foreground red
	}
	
	proc stripchart.marker.configure.bitmap.4.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring outline changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.bitmap.5.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc stripchart.marker.configure.bitmap.5.1.Body {} {
		.stripchart1 marker configure marker1 -outline red
	}
	
	proc stripchart.marker.configure.bitmap.5.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring rotate changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.bitmap.6.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc stripchart.marker.configure.bitmap.6.1.Body {} {
		.stripchart1 marker configure marker1 -rotate 30
	}
	
	proc stripchart.marker.configure.bitmap.6.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring anchor changes on screen
	# ------------------------------------------------------------------------------------
	proc stripchart.marker.configure.bitmap.7.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc stripchart.marker.configure.bitmap.7.1.Body {} {
		.stripchart1 marker configure marker1 -anchor n
	}
	
	proc stripchart.marker.configure.bitmap.7.1.Cleanup {} {
		destroy .stripchart1
	}
}
