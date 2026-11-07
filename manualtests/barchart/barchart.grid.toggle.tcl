# barchart.grid.toggle.tcl --
#
###Abstract
# This file contains the manual tests that test the grid on 
# function of the barchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide barchart.grid

package require rbc
namespace import rbc::*

namespace eval barchart.grid {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the grid can be toggled on a barchart.
	# ------------------------------------------------------------------------------------
	proc barchart.grid.toggle.1.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 grid on
	}
	
	proc barchart.grid.toggle.1.1.Body {} {
		.barchart1 grid toggle
	}
	
	proc barchart.grid.toggle.1.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the grid can be toggled on a barchart.
	# ------------------------------------------------------------------------------------
	proc barchart.grid.toggle.1.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 grid off
	}
	
	proc barchart.grid.toggle.1.2.Body {} {
		.barchart1 grid toggle
	}
	
	proc barchart.grid.toggle.1.2.Cleanup {} {
		destroy .barchart1
	}
}
