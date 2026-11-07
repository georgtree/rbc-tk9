# stripchart.postscript.output.tcl --
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
package provide stripchart.postscript

package require rbc
namespace import rbc::*

namespace eval stripchart.postscript {

	# ------------------------------------------------------------------------------------
	# Purpose: Ensure that correct postscript output is returned
	# ------------------------------------------------------------------------------------
	proc stripchart.postscript.output.1.1.Setup {} {
	    global FileName
	    global Channel
	    
	    set FileName "stripchart.postscript.output.1.1.ps"
	    set Channel [open $FileName w]
	    
	    stripchart .stripchart1
	    pack .stripchart1
	}
	
	proc stripchart.postscript.output.1.1.Body {} {
	    global Channel
	    set TestPs [.stripchart1 postscript output]
	    puts $Channel $TestPs
	    close $Channel
	}
	
	proc stripchart.postscript.output.1.1.Cleanup {} {
	    global Channel
	    global FileName
	    
	    unset Channel
	    
	    if {[file exists $FileName]} {
	        file delete $FileName
	    }
	    
	    destroy .stripchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure that correct postscript output is written to file
	# ------------------------------------------------------------------------------------
	proc stripchart.postscript.output.1.2.Setup {} {
	    global FileName    
	    set FileName "stripchart.postscript.output.1.2.ps"
	    
	    stripchart .stripchart1
	    pack .stripchart1
	}
	
	proc stripchart.postscript.output.1.2.Body {} {
	    global FileName
	    .stripchart1 postscript output $FileName
	}
	
	proc stripchart.postscript.output.1.2.Cleanup {} {
	    global FileName
	    
	    if {[file exists $FileName]} {
	        file delete $FileName
	    }
	    unset FileName    
	    
	    destroy .stripchart1
	}

}
