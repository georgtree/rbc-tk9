# barchart.crosshairs.configure.tcl --
#
###Abstract
# This file contains the automatic tests that test the crosshairs configure 
# function of the barchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide barchart.crosshairs

package require rbc
namespace import rbc::*

namespace eval barchart.crosshairs {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the color of the crosshairs can be changed.
	# ------------------------------------------------------------------------------------
	proc barchart.crosshairs.configure.1.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		Rbc_Crosshairs .barchart1
		.barchart1 crosshairs configure -color red
	}
	
	proc barchart.crosshairs.configure.1.1.Body {} {
		.barchart1 crosshairs configure -color blue
	}
	
	proc barchart.crosshairs.configure.1.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the dash style of the crosshairs can be changed.
	# ------------------------------------------------------------------------------------
	proc barchart.crosshairs.configure.2.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		Rbc_Crosshairs .barchart1
	}
	
	proc barchart.crosshairs.configure.2.1.Body {} {
		.barchart1 crosshairs configure -dashes {10 3 5 2}
	}
	
	proc barchart.crosshairs.configure.2.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the crosshairs can be hidden.
	# ------------------------------------------------------------------------------------
	proc barchart.crosshairs.configure.3.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		Rbc_Crosshairs .barchart1
		.barchart1 crosshairs configure -hide no
	}
	
	proc barchart.crosshairs.configure.3.1.Body {} {
		.barchart1 crosshairs configure -hide yes
	}
	
	proc barchart.crosshairs.configure.3.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the crosshairs can be show if hidden.
	# ------------------------------------------------------------------------------------
	proc barchart.crosshairs.configure.3.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		Rbc_Crosshairs .barchart1
		.barchart1 crosshairs configure -hide yes
	}
	
	proc barchart.crosshairs.configure.3.2.Body {} {
		.barchart1 crosshairs configure -hide no
	}
	
	proc barchart.crosshairs.configure.3.2.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the linewidth of the crosshairs can be changed.
	# ------------------------------------------------------------------------------------
	proc barchart.crosshairs.configure.4.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		Rbc_Crosshairs .barchart1
	}
	
	proc barchart.crosshairs.configure.4.1.Body {} {
		.barchart1 crosshairs configure -linewidth 20
	}
	
	proc barchart.crosshairs.configure.4.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the position of the crosshairs can be set.
	# DOES NOT WORK
	# ------------------------------------------------------------------------------------
	proc barchart.crosshairs.configure.5.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		Rbc_Crosshairs .barchart1
	}
	
	proc barchart.crosshairs.configure.5.1.Body {} {
		.barchart1 crosshairs configure -position @0.5,0.8
	}
	
	proc barchart.crosshairs.configure.5.1.Cleanup {} {
		destroy .barchart1
	}
}
