# graph.postscript.output.tcl --
#
###Abstract
# This file contains the manual tests that test the postscript output
# function of the barchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.postscript

package require rbc
namespace import rbc::*

namespace eval graph.postscript {

	# ------------------------------------------------------------------------------------
	# Purpose: Ensure that correct postscript output is returned
	# ------------------------------------------------------------------------------------
	proc graph.postscript.output.1.1.Setup {} {
	    global FileName
	    global Channel
	    
	    set FileName "graph.postscript.output.1.1.ps"
	    set Channel [open $FileName w]
	    
	    graph .graph1
	    pack .graph1
	}
	
	proc graph.postscript.output.1.1.Body {} {
	    global Channel
	    set TestPs [.graph1 postscript output]
	    puts $Channel $TestPs
	    close $Channel
	}
	
	proc graph.postscript.output.1.1.Cleanup {} {
	    global Channel
	    global FileName
	    
	    unset Channel
	    
	    if {[file exists $FileName]} {
	        file delete $FileName
	    }
	    
	    destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure that correct postscript output is written to file
	# ------------------------------------------------------------------------------------
	proc graph.postscript.output.1.2.Setup {} {
	    global FileName    
	    set FileName "graph.postscript.output.1.2.ps"
	    
	    graph .graph1
	    pack .graph1
	}
	
	proc graph.postscript.output.1.2.Body {} {
	    global FileName
	    .graph1 postscript output $FileName
	}
	
	proc graph.postscript.output.1.2.Cleanup {} {
	    global FileName
	    
	    if {[file exists $FileName]} {
	        file delete $FileName
	    }
	    unset FileName    
	    
	    destroy .graph1
	}

}
