# stripchart.element.delete.tcl --
#
###Abstract
# This file contains the manual tests that test the element delete 
# function of the stripchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide stripchart.element

package require rbc
namespace import rbc::*

namespace eval stripchart.element {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the element delete command works correctly when given a single
	# element name.
	# ------------------------------------------------------------------------------------
	proc stripchart.element.delete.1.1.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 element create Element1 -data {1 1 2 2}
	}
	
	proc stripchart.element.delete.1.1.Body {} {
		.stripchart1 element delete Element1
	}
	
	proc stripchart.element.delete.1.1.Cleanup {} {
		destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the element delete command works correctly when given multiple
	# element names.
	# ------------------------------------------------------------------------------------
	proc stripchart.element.delete.1.2.Setup {} {
		stripchart .stripchart1
		pack .stripchart1
		.stripchart1 element create Element1 -data {1 1 2 2}
		.stripchart1 element create Element2 -data {1 2 2 3}
	}
	
	proc stripchart.element.delete.1.2.Body {} {
		.stripchart1 element delete Element1 Element2
	}
	
	proc stripchart.element.delete.1.2.Cleanup {} {
		destroy .stripchart1
	}
}