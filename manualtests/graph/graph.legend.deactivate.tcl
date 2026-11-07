# graph.legend.deactivate.tcl --
#
###Abstract
# This file contains the manual tests that test the legend deactivate 
# function of the graph BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.legend

package require rbc
namespace import rbc::*

namespace eval graph.legend {
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that deactivating all legend elements works properly.
    # ------------------------------------------------------------------------------------     
    proc graph.legend.deactivate.1.1.Setup {} {
        graph .graph1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .graph1 element create Line1 -x $X -y $Y
        .graph1 element create Line2 -x $Y -y $X
        .graph1 legend activate *        
        pack .graph1 -fill both
    }
    
    proc graph.legend.deactivate.1.1.Body {} {
        .graph1 legend deactivate *
    }
    
    proc graph.legend.deactivate.1.1.Cleanup {} {
        destroy .graph1
    } 
    
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure that deactivating a subset of legend elements works properly.
    # ------------------------------------------------------------------------------------
    proc graph.legend.deactivate.1.2.Setup {} {
        graph .graph1
        set X [list 1 2 3 4 5]
        set Y [list 2 4 6 8 10]
        .graph1 element create Line1 -x $X -y $Y
        .graph1 element create Line2 -x $Y -y $X
        .graph1 legend activate *        
        pack .graph1 -fill both
    }
    
    proc graph.legend.deactivate.1.2.Body {} {
        .graph1 legend deactivate Line1
    }
    
    proc graph.legend.deactivate.1.2.Cleanup {} {
        destroy .graph1
    }     
}