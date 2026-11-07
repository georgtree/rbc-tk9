# stripchart.grid.toggle.tcl --
#
###Abstract
# This file contains the manual tests that test the grid on 
# function of the stripchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide stripchart.grid

package require rbc
namespace import rbc::*

namespace eval stripchart.grid {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the grid can be toggled on a stripchart.
	# ------------------------------------------------------------------------------------
	proc stripchart.grid.toggle.1.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 grid on
	}
	
	proc stripchart.grid.toggle.1.1.Body {} {
		.stripchart1 grid toggle
	}
	
	proc stripchart.grid.toggle.1.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the grid can be toggled on a stripchart.
	# ------------------------------------------------------------------------------------
	proc stripchart.grid.toggle.1.2.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 grid off
	}
	
	proc stripchart.grid.toggle.1.2.Body {} {
		.stripchart1 grid toggle
	}
	
	proc stripchart.grid.toggle.1.2.Cleanup {} {
		destroy .stripchart1
	}
}
