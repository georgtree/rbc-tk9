# barchart.element.show.tcl --
#
###Abstract
# This file contains the manual tests that test the element show 
# function of the barchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide barchart.element

package require rbc
namespace import rbc::*

namespace eval barchart.element {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the element show command works correctly when given a single element
	# name.
	# ------------------------------------------------------------------------------------
	proc barchart.element.show.1.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2} -hide yes
	}
	
	proc barchart.element.show.1.1.Body {} {
		.barchart1 element show Element1
	}
	
	proc barchart.element.show.1.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the element show command works correctly when given multiple element
	# names.
	# ------------------------------------------------------------------------------------
	proc barchart.element.show.1.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2} -hide yes
		.barchart1 element create Element2 -data {1 2 2 3} -hide yes
	}
	
	proc barchart.element.show.1.2.Body {} {
		.barchart1 element show {Element1 Element2}
	}
	
	proc barchart.element.show.1.2.Cleanup {} {
		.barchart1 element delete Element2
		.barchart1 element delete Element1
		destroy .barchart1
	}
}