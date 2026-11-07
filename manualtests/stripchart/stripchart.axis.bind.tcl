# stripchart.axis.bind.tcl --
#
###Abstract
# This file contains the manual tests that test the axis bind
# function of the stripchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide stripchart.axis

package require rbc
namespace import rbc::*

namespace eval stripchart.axis {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure bind actions execute
	# ------------------------------------------------------------------------------------
	proc stripchart.axis.bind.1.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
	}
	
	proc stripchart.axis.bind.1.1.Body {} {
		.stripchart1 axis bind x <Double-1> {puts test}
	}
	
	proc stripchart.axis.bind.1.1.Cleanup {} {
		destroy .stripchart1
	}
}
