# graph.marker.configure.bitmap.test --
#
###Abstract
# This file contains the manual tests that test the bitmap marker configure
# function of the graph BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide graph.marker

package require rbc
namespace import rbc::*

namespace eval graph.marker {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring background changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.bitmap.1.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc graph.marker.configure.bitmap.1.1.Body {} {
		.graph1 marker configure marker1 -background red
	}
	
	proc graph.marker.configure.bitmap.1.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring bitmap changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.bitmap.2.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create bitmap -name marker1 -coords {0 0}
	}
	
	proc graph.marker.configure.bitmap.2.1.Body {} {
		.graph1 marker configure marker1 -bitmap @greenback.xbm
	}
	
	proc graph.marker.configure.bitmap.2.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring fill changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.bitmap.3.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc graph.marker.configure.bitmap.3.1.Body {} {
		.graph1 marker configure marker1 -fill red
	}
	
	proc graph.marker.configure.bitmap.3.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring foreground changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.bitmap.4.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc graph.marker.configure.bitmap.4.1.Body {} {
		.graph1 marker configure marker1 -foreground red
	}
	
	proc graph.marker.configure.bitmap.4.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring outline changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.bitmap.5.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc graph.marker.configure.bitmap.5.1.Body {} {
		.graph1 marker configure marker1 -outline red
	}
	
	proc graph.marker.configure.bitmap.5.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring rotate changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.bitmap.6.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc graph.marker.configure.bitmap.6.1.Body {} {
		.graph1 marker configure marker1 -rotate 30
	}
	
	proc graph.marker.configure.bitmap.6.1.Cleanup {} {
		destroy .graph1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring anchor changes on screen
	# ------------------------------------------------------------------------------------
	proc graph.marker.configure.bitmap.7.1.Setup {} {
		graph .graph1
		pack .graph1
		.graph1 marker create bitmap -name marker1 -bitmap @greenback.xbm -coords {0 0}
	}
	
	proc graph.marker.configure.bitmap.7.1.Body {} {
		.graph1 marker configure marker1 -anchor n
	}
	
	proc graph.marker.configure.bitmap.7.1.Cleanup {} {
		destroy .graph1
	}
    # ------------------------------------------------------------------------------------
    # Purpose: Ensure configuring a bitmap mask makes pixels outside the mask transparent.
    # ------------------------------------------------------------------------------------
    proc graph.marker.configure.bitmap.8.1.Setup {} {
        graph .graph1 -width 300 -height 300 -plotbackground red

        .graph1 axis configure x -min 0 -max 1
        .graph1 axis configure y -min 0 -max 1

        pack .graph1

        .graph1 marker create bitmap -name marker1 -bitmap @circle.xbm -foreground black -fill white -coords {0.5 0.5}
    }

    proc graph.marker.configure.bitmap.8.1.Body {} {
        .graph1 marker configure marker1 -mask @circle_mask.xbm
    }

    proc graph.marker.configure.bitmap.8.1.Cleanup {} {
        destroy .graph1
    }

    # ------------------------------------------------------------------------------------
    # Purpose: Ensure the mask remains aligned when the bitmap is scaled and rotated.
    # ------------------------------------------------------------------------------------
    proc graph.marker.configure.bitmap.8.2.Setup {} {
        graph .graph1 -width 300 -height 300 -plotbackground red

        .graph1 axis configure x -min 0 -max 1
        .graph1 axis configure y -min 0 -max 1

        pack .graph1

        .graph1 marker create bitmap -name marker1 -bitmap @circle.xbm -mask @circle_mask.xbm -foreground black -fill\
                white -coords {0.5 0.5}
    }

    proc graph.marker.configure.bitmap.8.2.Body {} {
        .graph1 marker configure marker1 -coords {0.25 0.25 0.75 0.75} -rotate 30
    }

    proc graph.marker.configure.bitmap.8.2.Cleanup {} {
        destroy .graph1
    }
}
