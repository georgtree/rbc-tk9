# winop.readjpeg.tcl --
#
###Abstract
# This file contains the manual tests that test the readjpeg 
# function of the winop BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide winop.readjpeg

package require rbc
namespace import rbc::*

namespace eval winop.readjpeg {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the readjpeg function of the winop component works correctly when
	# given the filepath to a valid jpeg image. 
	# ------------------------------------------------------------------------------------
	proc winop.readjpeg.1.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc winop.readjpeg.1.1.Body {} {
		
	}
	
	proc winop.readjpeg.1.1.Cleanup {} {
		destroy .graph1
	}
}