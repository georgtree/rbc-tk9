# stripchart.legend.configure.tcl --
#
###Abstract
# This file contains the manual tests that test the legend configure 
# function of the stripchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide stripchart.legend

package require rbc
namespace import rbc::*

namespace eval stripchart.legend {
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the activebackground configuration works for valid colors.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.1.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend activate Line1      
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.1.1.Body {} {
        .stripchart1 legend configure -activebackground salmon
    }
    
    proc stripchart.legend.configure.1.1.Cleanup {} {
        destroy .stripchart1
    }  
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the activeborderwidth configuration works for valid widths.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.2.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend activate Line1      
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.2.1.Body {} {
        .stripchart1 legend configure -activeborderwidth 20
    }
    
    proc stripchart.legend.configure.2.1.Cleanup {} {
        destroy .stripchart1
    }
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the activeforeground configuration works for valid colors.
    # ------------------------------------------------------------------------------------ 
    proc stripchart.legend.configure.3.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend activate Line1      
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.3.1.Body {} {
        .stripchart1 legend configure -activeforeground white
    }
    
    proc stripchart.legend.configure.3.1.Cleanup {} {
        destroy .stripchart1
    }    
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the activerelief configuration works for raised reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.4.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend activate Line1  
        .stripchart1 legend configure -activeborderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.4.1.Body {} {
        .stripchart1 legend configure -activerelief raised
    }
    
    proc stripchart.legend.configure.4.1.Cleanup {} {
        destroy .stripchart1
    } 
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the activerelief configuration works for flat reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.4.2.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend activate Line1  
        .stripchart1 legend configure -activeborderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.4.2.Body {} {
        .stripchart1 legend configure -activerelief flat
    }
    
    proc stripchart.legend.configure.4.2.Cleanup {} {
        destroy .stripchart1
    }  
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the activerelief configuration works for grooved reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.4.3.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend activate Line1  
        .stripchart1 legend configure -activeborderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.4.3.Body {} {
        .stripchart1 legend configure -activerelief groove
    }
    
    proc stripchart.legend.configure.4.3.Cleanup {} {
        destroy .stripchart1
    }   
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the activerelief configuration works for ridged reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.4.4.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend activate Line1  
        .stripchart1 legend configure -activeborderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.4.4.Body {} {
        .stripchart1 legend configure -activerelief ridge
    }
    
    proc stripchart.legend.configure.4.4.Cleanup {} {
        destroy .stripchart1
    }
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the activerelief configuration works for solid reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.4.5.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend activate Line1  
        .stripchart1 legend configure -activeborderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.4.5.Body {} {
        .stripchart1 legend configure -activerelief solid
    }
    
    proc stripchart.legend.configure.4.5.Cleanup {} {
        destroy .stripchart1
    }
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the activerelief configuration works for sunken reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.4.6.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend activate Line1  
        .stripchart1 legend configure -activeborderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.4.6.Body {} {
        .stripchart1 legend configure -activerelief sunken
    }
    
    proc stripchart.legend.configure.4.6.Cleanup {} {
        destroy .stripchart1
    }   
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the anchor configuration works for all anchors.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.5.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y  
        .stripchart1 legend  configure -position plotarea
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.5.1.Body {} {
        .stripchart1 legend configure -anchor center
		sleep 1000
        .stripchart1 legend configure -anchor n
		sleep 1000
        .stripchart1 legend configure -anchor ne
		sleep 1000
        .stripchart1 legend configure -anchor e
		sleep 1000
        .stripchart1 legend configure -anchor se
		sleep 1000
        .stripchart1 legend configure -anchor s
		sleep 1000
        .stripchart1 legend configure -anchor sw
		sleep 1000
        .stripchart1 legend configure -anchor w
		sleep 1000
        .stripchart1 legend configure -anchor nw
    }
    
    proc stripchart.legend.configure.5.1.Cleanup {} {
        destroy .stripchart1
    } 
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the background configuration works for no background.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.6.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 configure -background salmon  
        .stripchart1 legend configure -background gray        
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.6.1.Body {} {
        .stripchart1 legend configure -background ""
    }
    
    proc stripchart.legend.configure.6.1.Cleanup {} {
        destroy .stripchart1
    } 
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the background configuration works for valid colors.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.6.2.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.6.2.Body {} {
        .stripchart1 legend configure -background black
    }
    
    proc stripchart.legend.configure.6.2.Cleanup {} {
        destroy .stripchart1
    }    
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the borderwidth configuration works for valid widths.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.7.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y      
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.7.1.Body {} {
        .stripchart1 legend configure -borderwidth 20
    }
    
    proc stripchart.legend.configure.7.1.Cleanup {} {
        destroy .stripchart1
    }    
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the font configuration works for valid font strings.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.8.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y      
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.8.1.Body {} {
        .stripchart1 legend configure -font "*-Arial-Bold-R-Normal-*-14-120-*" 
    }
    
    proc stripchart.legend.configure.8.1.Cleanup {} {
        destroy .stripchart1
    }
	
	# ------------------------------------------------------------------------------------
    # Purpose: Ensure that the font configuration works for a string.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.8.2.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y      
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.8.2.Body {} {
        .stripchart1 legend configure -font Times 
    }
    
    proc stripchart.legend.configure.8.2.Cleanup {} {
        destroy .stripchart1
    }
    
	# ------------------------------------------------------------------------------------
    # Purpose: Ensure that the font configuration works for a string and integer.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.8.3.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y      
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.8.3.Body {} {
        .stripchart1 legend configure -font {Times 12} 
    }
    
    proc stripchart.legend.configure.8.3.Cleanup {} {
        destroy .stripchart1
    }
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the foreground configuration works for valid colors.
    # ------------------------------------------------------------------------------------ 
    proc stripchart.legend.configure.9.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y  
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.9.1.Body {} {
        .stripchart1 legend configure -foreground white
    }
    
    proc stripchart.legend.configure.9.1.Cleanup {} {
        destroy .stripchart1
    } 
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the hide configuration works.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.10.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.10.1.Body {} {
        .stripchart1 legend configure -hide true
    }
    
    proc stripchart.legend.configure.10.1.Cleanup {} {
        destroy .stripchart1
    }   
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the ipadx configuration works.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.11.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.11.1.Body {} {
        .stripchart1 legend configure -ipadx 5
    }
    
    proc stripchart.legend.configure.11.1.Cleanup {} {
        destroy .stripchart1
    }
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the ipady configuration works.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.12.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.12.1.Body {} {
        .stripchart1 legend configure -ipady 5
    }
    
    proc stripchart.legend.configure.12.1.Cleanup {} {
        destroy .stripchart1
    }    
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the padx configuration works.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.13.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.13.1.Body {} {
        .stripchart1 legend configure -padx 5
    }
    
    proc stripchart.legend.configure.13.1.Cleanup {} {
        destroy .stripchart1
    }    
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the pady configuration works.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.14.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.14.1.Body {} {
        .stripchart1 legend configure -pady 5
    }
    
    proc stripchart.legend.configure.14.1.Cleanup {} {
        destroy .stripchart1
    }   
    # TODO position
	# TODO relief
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the relief configuration works for raised reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.15.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend configure -borderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.15.1.Body {} {
        .stripchart1 legend configure -relief raised
    }
    
    proc stripchart.legend.configure.15.1.Cleanup {} {
        destroy .stripchart1
    } 
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the relief configuration works for flat reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.15.2.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend configure -borderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.15.2.Body {} {
        .stripchart1 legend configure -relief flat
    }
    
    proc stripchart.legend.configure.15.2.Cleanup {} {
        destroy .stripchart1
    }  
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the relief configuration works for grooved reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.15.3.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend configure -borderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.15.3.Body {} {
        .stripchart1 legend configure -relief groove
    }
    
    proc stripchart.legend.configure.15.3.Cleanup {} {
        destroy .stripchart1
    }   
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the relief configuration works for ridged reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.15.4.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend configure -borderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.15.4.Body {} {
        .stripchart1 legend configure -relief ridge
    }
    
    proc stripchart.legend.configure.15.4.Cleanup {} {
        destroy .stripchart1
    }
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the relief configuration works for solid reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.15.5.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y
        .stripchart1 legend configure -borderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.15.5.Body {} {
        .stripchart1 legend configure -relief solid
    }
    
    proc stripchart.legend.configure.15.5.Cleanup {} {
        destroy .stripchart1
    }
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the relief configuration works for sunken reliefs.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.15.6.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y 
        .stripchart1 legend configure -borderwidth 10
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.15.6.Body {} {
        .stripchart1 legend configure -relief sunken
    }
    
    proc stripchart.legend.configure.15.6.Cleanup {} {
        destroy .stripchart1
    }     
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the shadow configuration works for valid colors.
    # ------------------------------------------------------------------------------------
    proc stripchart.legend.configure.16.1.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y 
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.16.1.Body {} {
        .stripchart1 legend configure -shadow red
    }
    
    proc stripchart.legend.configure.16.1.Cleanup {} {
        destroy .stripchart1
    }    
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that the shadow configuration works for a shadow and a depth.
    # ------------------------------------------------------------------------------------ 
    proc stripchart.legend.configure.16.2.Setup {} {
        stripchart .stripchart1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .stripchart1 element create Line1 -x $X -y $Y 
        pack .stripchart1 -fill both
    }
    
    proc stripchart.legend.configure.16.2.Body {} {
        .stripchart1 legend configure -shadow {red 3}
    }
    
    proc stripchart.legend.configure.16.2.Cleanup {} {
        destroy .stripchart1
    }    
}