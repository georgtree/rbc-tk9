# winop.quantize.tcl --
#
###Abstract
# This file contains the manual tests that test the quantize 
# function of the winop BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide winop.quantize

package require rbc
namespace import rbc::*

namespace eval winop.quantize {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the quantize function of the winop component works correctly. 
	# ------------------------------------------------------------------------------------
	proc winop.quantize.1.1.Setup {} {
		set dest [image create photo -file ./rain.gif]
		button .button1 -image $dest
		pack .button1
	}
	
	proc winop.quantize.1.1.Body {} {
		set src [image create photo -file ./rain.gif]
		set dest [image create photo -file ./rain.gif]
		winop quantize $src $dest 2
		.button1 configure -image $dest
	}
	
	proc winop.quantize.1.1.Cleanup {} {
		destroy .button1
	}
}

 