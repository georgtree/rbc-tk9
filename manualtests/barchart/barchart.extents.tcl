# barchart.extents.tcl --
#
###Abstract
# This file tests the extent function of the barchart BLT Component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide barchart.extents

package require rbc
namespace import rbc::*

namespace eval barchart.extents {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the barchart extents are valid for leftmargin
	# ------------------------------------------------------------------------------------
	proc barchart.extents.1.1.Setup {} {
		barchart .barchart1 -leftmargin 20
	}
	
	proc barchart.extents.1.1.Body {} {
		.barchart1 extents leftmargin
	}
	
	proc barchart.extents.1.1.Cleanup {} {
		destroy .barchart1
	}

	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the barchart extents are valid for rightmargin
	# ------------------------------------------------------------------------------------
	proc barchart.extents.2.1.Setup {} {
		barchart .barchart1 -rightmargin 20
	}
	
	proc barchart.extents.2.1.Body {} {
		.barchart1 extents rightmargin
	}
	
	proc barchart.extents.2.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the barchart extents are valid for topmargin
	# ------------------------------------------------------------------------------------
	proc barchart.extents.3.1.Setup {} {
		barchart .barchart1 -topmargin 20
	}
	
	proc barchart.extents.3.1.Body {} {
		.barchart1 extents topmargin
	}
	
	proc barchart.extents.3.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the barchart extents are valid for bottommargin
	# ------------------------------------------------------------------------------------
	proc barchart.extents.4.1.Setup {} {
		barchart .barchart1 -bottommargin 20
	}
	
	proc barchart.extents.4.1.Body {} {
		.barchart1 extents bottommargin
	}
	
	proc barchart.extents.4.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the barchart extents are valid for plotwidth
	# ------------------------------------------------------------------------------------
	proc barchart.extents.5.1.Setup {} {
		barchart .barchart1
	}
	
	proc barchart.extents.5.1.Body {} {
		.barchart1 extents plotwidth
	}
	
	proc barchart.extents.5.1.Cleanup {} {
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the barchart extents are valid for plotheight
	# ------------------------------------------------------------------------------------
	proc barchart.extents.6.1.Setup {} {
		barchart .barchart1
	}
	
	proc barchart.extents.6.1.Body {} {
		.barchart1 extents plotheight
	}
	
	proc barchart.extents.6.1.Cleanup {} {
		destroy .barchart1
	}
}
