# graph.crosshairs.configure.tcl --
#
###Abstract
# This file contains the automatic tests that test the crosshairs configure 
# function of the graph BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.crosshairs

package require rbc
namespace import rbc::*

namespace eval graph.crosshairs {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the color of the crosshairs can be changed.
	# ------------------------------------------------------------------------------------
	proc graph.crosshairs.configure.1.1.Setup {} {
		graph .graph1
		pack .graph1
		Rbc_Crosshairs .graph1
		.graph1 crosshairs configure -color red
	}
	
	proc graph.crosshairs.configure.1.1.Body {} {
		.graph1 crosshairs configure -color blue
	}
	
	proc graph.crosshairs.configure.1.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the dash style of the crosshairs can be changed.
	# ------------------------------------------------------------------------------------
	proc graph.crosshairs.configure.2.1.Setup {} {
		graph .graph1
		pack .graph1
		Rbc_Crosshairs .graph1
	}
	
	proc graph.crosshairs.configure.2.1.Body {} {
		.graph1 crosshairs configure -dashes {10 3 5 2}
	}
	
	proc graph.crosshairs.configure.2.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the crosshairs can be hidden.
	# ------------------------------------------------------------------------------------
	proc graph.crosshairs.configure.3.1.Setup {} {
		graph .graph1
		pack .graph1
		Rbc_Crosshairs .graph1
		.graph1 crosshairs configure -hide no
	}
	
	proc graph.crosshairs.configure.3.1.Body {} {
		.graph1 crosshairs configure -hide yes
	}
	
	proc graph.crosshairs.configure.3.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the crosshairs can be show if hidden.
	# ------------------------------------------------------------------------------------
	proc graph.crosshairs.configure.3.2.Setup {} {
		graph .graph1
		pack .graph1
		Rbc_Crosshairs .graph1
		.graph1 crosshairs configure -hide yes
	}
	
	proc graph.crosshairs.configure.3.2.Body {} {
		.graph1 crosshairs configure -hide no
	}
	
	proc graph.crosshairs.configure.3.2.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the linewidth of the crosshairs can be changed.
	# ------------------------------------------------------------------------------------
	proc graph.crosshairs.configure.4.1.Setup {} {
		graph .graph1
		pack .graph1
		Rbc_Crosshairs .graph1
	}
	
	proc graph.crosshairs.configure.4.1.Body {} {
		.graph1 crosshairs configure -linewidth 20
	}
	
	proc graph.crosshairs.configure.4.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the position of the crosshairs can be set.
	# DOES NOT WORK
	# ------------------------------------------------------------------------------------
	proc graph.crosshairs.configure.5.1.Setup {} {
		graph .graph1
		pack .graph1
		Rbc_Crosshairs .graph1
	}
	
	proc graph.crosshairs.configure.5.1.Body {} {
		.graph1 crosshairs configure -position @0.5,0.8
	}
	
	proc graph.crosshairs.configure.5.1.Cleanup {} {
		destroy .graph1
	}
}
