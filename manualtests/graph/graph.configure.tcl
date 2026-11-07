# graph.configure.tcl --
#
###Abstract
# This file tests the different configuration options of the graph BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.configure

package require rbc
namespace import rbc::*

namespace eval graph.configure {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring aspect changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.1.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.1.1.Body {} {
		.graph1 configure -aspect 2
	}
	
	proc graph.configure.1.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring background changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.2.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.2.1.Body {} {
		.graph1 configure -background red
	}
	
	proc graph.configure.2.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring barmode changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.3.1.Setup {} {
		graph .graph1
		.graph1 bar create Bar1 -x [list 1 2 3 3] -y [list 1 2 3 4]
		pack .graph1
	}
	
	proc graph.configure.3.1.Body {} {
		.graph1 configure -barmode aligned
	}
	
	proc graph.configure.3.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring barwidth changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.4.1.Setup {} {
		graph .graph1
		.graph1 bar create Bar1 -x [list 1 2 3 3] -y [list 1 2 3 4]
		pack .graph1
	}
	
	proc graph.configure.4.1.Body {} {
		.graph1 configure -barwidth 80
	}
	
	proc graph.configure.4.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring baseline changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.5.1.Setup {} {
		graph .graph1
		.graph1 bar create Bar1 -x [list 1 2 3 3] -y [list 1 2 3 4]
		pack .graph1
	}
	
	proc graph.configure.5.1.Body {} {
		.graph1 configure -baseline 10000
		.graph1 bar configure Bar1 -hide yes
		.graph1 bar configure Bar1 -hide no
	}
	
	proc graph.configure.5.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring borderwidth changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.6.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.6.1.Body {} {
		.graph1 configure -borderwidth 1000
	}
	
	proc graph.configure.6.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring bottommargin changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.7.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.7.1.Body {} {
		.graph1 configure -bottommargin 2
	}
	
	proc graph.configure.7.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring bottomvariable changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.8.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.8.1.Body {} {
		.graph1 configure -bottomvariable left
	}
	
	proc graph.configure.8.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring cursor changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.9.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.9.1.Body {} {
		.graph1 configure -cursor circle
	}
	
	proc graph.configure.9.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring data changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.10.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.10.1.Body {} {
		.graph1 configure -data {1 2 3 4}
	}
	
	proc graph.configure.10.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring datacommand changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.11.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.11.1.Body {} {
		.graph1 configure -datacommand {set x}
	}
	
	proc graph.configure.11.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring font changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.12.1.Setup {} {
		graph .graph1 -title "Test"
		pack .graph1
	}
	
	proc graph.configure.12.1.Body {} {
		.graph1 configure -font *-Helvetica-*-R-Normal-*-10-120-*
	}
	
	proc graph.configure.12.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring foreground changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.13.1.Setup {} {
		graph .graph1 -title "Test"
		pack .graph1
	}
	
	proc graph.configure.13.1.Body {} {
		.graph1 configure -foreground yellow
	}
	
	proc graph.configure.13.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring height changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.14.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.14.1.Body {} {
		.graph1 configure -height 20
	}
	
	proc graph.configure.14.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring highlightBackground changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.15.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.15.1.Body {} {
		.graph1 configure -highlightbackground yellow
	}
	
	proc graph.configure.15.1.Cleanup {} {
		destroy .graph1
	}
		
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring highlightcolor changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.16.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.16.1.Body {} {
		.graph1 configure -highlightcolor red
	}
	
	proc graph.configure.16.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring highlightthickness changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.17.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.17.1.Body {} {
		.graph1 configure -highlightthickness 10
	}
	
	proc graph.configure.17.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring invertxy changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.18.1.Setup {} {
		graph .graph1
		.graph1 xaxis configure -title "X Axis"
		.graph1 yaxis configure -title "Y Axis"
		pack .graph1
	}
	
	proc graph.configure.18.1.Body {} {
		.graph1 configure -invertxy true
	}
	
	proc graph.configure.18.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring justify changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.19.1.Setup {} {
		graph .graph1 -title "Test Title is Long \nTitle"
		pack .graph1
	}
	
	proc graph.configure.19.1.Body {} {
		.graph1 configure -justify left
	}
	
	proc graph.configure.19.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring leftmargin changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.20.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.20.1.Body {} {
		.graph1 configure -leftmargin 10
	}
	
	proc graph.configure.20.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring leftvariable changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.21.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.21.1.Body {} {
		.graph1 configure -leftvariable left
	}
	
	proc graph.configure.21.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring plotbackground changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.22.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.22.1.Body {} {
		.graph1 configure -plotbackground red
	}
	
	proc graph.configure.22.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring plotborderwidth changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.23.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.23.1.Body {} {
		.graph1 configure -plotborderwidth 10
	}
	
	proc graph.configure.23.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring plotpadx changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.24.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.24.1.Body {} {
		.graph1 configure -plotpadx 200
	}
	
	proc graph.configure.24.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring plotpady changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.25.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.25.1.Body {} {
		.graph1 configure -plotpady 200
	}
	
	proc graph.configure.25.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring plotrelief changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.26.1.Setup {} {
		graph .graph1 -plotborderwidth 2
		pack .graph1
	}
	
	proc graph.configure.26.1.Body {} {
		.graph1 configure -plotrelief raised
	}
	
	proc graph.configure.26.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring relief changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.27.1.Setup {} {
		graph .graph1 -borderwidth 2 -plotborderwidth 2
		pack .graph1
	}
	
	proc graph.configure.27.1.Body {} {
		.graph1 configure -relief raised
	}
	
	proc graph.configure.27.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring rightmargin changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.28.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.28.1.Body {} {
		.graph1 configure -rightmargin 10
	}
	
	proc graph.configure.28.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring rightvariable changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.29.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.29.1.Body {} {
		.graph1 configure -rightvariable right
	}
	
	proc graph.configure.29.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring shadow changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.30.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.30.1.Body {} {
		.graph1 configure -shadow red
	}
	
	proc graph.configure.30.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring takefocus changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.31.1.Setup {} {
		graph .graph1
		button .button1 -text "Test"
		pack .graph1
		pack .button1
	}
	
	proc graph.configure.31.1.Body {} {
		.graph1 configure -takefocus 1
	}
	
	proc graph.configure.31.1.Cleanup {} {
		destroy .graph1 .button1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring tile changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.32.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.32.1.Body {} {
		.graph1 configure -tile [image create photo -file "buckskin.gif"]
	}
	
	proc graph.configure.32.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring title changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.33.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.33.1.Body {} {
		.graph1 configure -title "Test"
	}
	
	proc graph.configure.33.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring topmargin changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.34.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.34.1.Body {} {
		.graph1 configure -topmargin 20
	}
	
	proc graph.configure.34.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring topvariable changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.35.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.35.1.Body {} {
		.graph1 configure -topvariable top
	}
	
	proc graph.configure.35.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring width changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.configure.36.1.Setup {} {
		graph .graph1
		pack .graph1
	}
	
	proc graph.configure.36.1.Body {} {
		.graph1 configure -width 200
	}
	
	proc graph.configure.36.1.Cleanup {} {
		destroy .graph1
	}
}
