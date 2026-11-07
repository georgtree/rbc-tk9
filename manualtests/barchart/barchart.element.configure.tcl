# barchart.element.configure.tcl --
#
###Abstract
# This file contains the manual tests that test the element configure 
# function of the barchart BLT component.
#
###Copyright
# See the file "license.terms" for information on usage and
# redistribution of this file, and for a DISCLAIMER OF ALL WARRANTIES.
#
###Revision String
# SVN: $Id$

###Package Definition
package provide barchart.element

package require rbc
namespace import rbc::*

namespace eval barchart.element {
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the activepen of the element can be changed.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.1.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -background blue -data {1 1}
		.barchart1 pen create Pen1 -background red
	}
	
	proc barchart.element.configure.1.1.Body {} {
		.barchart1 element configure Element1 -activepen Pen1
		.barchart1 element activate Element1
	}
	
	proc barchart.element.configure.1.1.Cleanup {} {
		.barchart1 pen delete Pen1
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the bindtags of the element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.2.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1}
		.barchart1 element bind Binding1 <Double-1> {puts foobar}
	}
	
	proc barchart.element.configure.2.1.Body {} {
		.barchart1 element configure Element1 -bindtags Binding1
	}
	
	proc barchart.element.configure.2.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy Binding1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the data of the element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.3.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1
	}
	
	proc barchart.element.configure.3.1.Body {} {
		.barchart1 element configure Element1 -data {1 1 2 2}
	}
	
	proc barchart.element.configure.3.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the element can be hidden.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.4.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1}
		.barchart1 element configure Element1 -hide no
	}
	
	proc barchart.element.configure.4.1.Body {} {
		.barchart1 element configure Element1 -hide yes
	}
	
	proc barchart.element.configure.4.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the element can be show if hidden.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.4.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1}
		.barchart1 element configure Element1 -hide yes
	}
	
	proc barchart.element.configure.4.2.Body {} {
		.barchart1 element configure Element1 -hide no
	}
	
	proc barchart.element.configure.4.2.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the legend label can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.5.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1}
	}
	
	proc barchart.element.configure.5.1.Body {} {
		.barchart1 element configure Element1 -label myLabel
	}
	
	proc barchart.element.configure.5.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the legend label can be hidden in legend.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.5.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1}
		.barchart1 element create Element2 -data {2 2}
	}
	
	proc barchart.element.configure.5.2.Body {} {
		.barchart1 element configure Element1 -label ""
	}
	
	proc barchart.element.configure.5.2.Cleanup {} {
		.barchart1 element delete Element2
		.barchart1 element delete Element1
		destroy .barchart1
	}

	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the mapx of the element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.6.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 axis create Axis1 -min 0 -max 4
		.barchart1 element create Element1 -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.6.1.Body {} {
		.barchart1 xaxis use Axis1
		.barchart1 element configure Element1 -mapx Axis1
	}
	
	proc barchart.element.configure.6.1.Cleanup {} {
		.barchart1 axis delete Axis1
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the mapy of the element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.7.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 axis create Axis1 -min 0 -max 4
		.barchart1 element create Element1 -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.7.1.Body {} {
		.barchart1 yaxis use Axis1
		.barchart1 element configure Element1 -mapy Axis1
	}
	
	proc barchart.element.configure.7.1.Cleanup {} {
		.barchart1 axis delete Axis1
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the pen of the element can be changed.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.8.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -background blue -data {1 1}
		.barchart1 pen create Pen1 -background red
	}
	
	proc barchart.element.configure.8.1.Body {} {
		.barchart1 element configure Element1 -pen Pen1
	}
	
	proc barchart.element.configure.8.1.Cleanup {} {
		.barchart1 pen delete Pen1
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure elements can be drawn with a different pen based on their weights.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.9.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3} -weights {0 1 2}
		.barchart1 pen create Pen1 -background red
	}
	
	proc barchart.element.configure.9.1.Body {} {
		.barchart1 element configure Element1 -styles {{Pen1 0.5 1.5}}
		.barchart1 element configure Element1 -hide yes
		.barchart1 element configure Element1 -hide no
	}
	
	proc barchart.element.configure.9.1.Cleanup {} {
		.barchart1 pen delete Pen1
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the xdata and ydata of an element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.10.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1
	}
	
	proc barchart.element.configure.10.1.Body {} {
		.barchart1 element configure Element1 -xdata {1 2 3}
		.barchart1 element configure Element1 -ydata {1 2 3}
	}
	
	proc barchart.element.configure.10.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the x and y of an element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.11.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1
	}
	
	proc barchart.element.configure.11.1.Body {} {
		.barchart1 element configure Element1 -x {1 2 3}
		.barchart1 element configure Element1 -y {1 2 3}
	}
	
	proc barchart.element.configure.11.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the xerror of an element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.12.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1
	}
	
	proc barchart.element.configure.12.1.Body {} {
		.barchart1 element configure Element1 -xerror {1 2}
		.barchart1 element configure Element1 -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.12.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the yerror of an element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.13.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1
	}
	
	proc barchart.element.configure.13.1.Body {} {
		.barchart1 element configure Element1 -yerror {1 2}
		.barchart1 element configure Element1 -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.13.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the yhigh and ylow of an element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.14.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3 4 4}
	}
	
	proc barchart.element.configure.14.1.Body {} {
		.barchart1 element configure Element1 -yhigh 3 -ylow 2
		.barchart1 element configure Element1 -data {1 1 2 2 3 3 4 4}
	}
	
	proc barchart.element.configure.14.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the xhigh and xlow of an element can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.15.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3 4 4}
	}
	
	proc barchart.element.configure.15.1.Body {} {
		.barchart1 element configure Element1 -xhigh 3 -xlow 2
		.barchart1 element configure Element1 -data {1 1 2 2 3 3 4 4}
	}
	
	proc barchart.element.configure.15.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the errorbarcolor of an element can be set when the xerror has been
	# set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.16.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -xerror {1 2} -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.16.1.Body {} {
		.barchart1 element configure Element1 -errorbarcolor red
	}
	
	proc barchart.element.configure.16.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the errorbarcolor of an element can be set when the yerror has been
	# set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.16.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -yerror {1 2} -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.16.2.Body {} {
		.barchart1 element configure Element1 -errorbarcolor red
	}
	
	proc barchart.element.configure.16.2.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the errorbarwidth of an element can be set when the xerror has been
	# set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.17.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -xerror {1 2} -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.17.1.Body {} {
		.barchart1 element configure Element1 -errorbarwidth 5
	}
	
	proc barchart.element.configure.17.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the errorbarwidth of an element can be set when the yerror has been
	# set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.17.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -yerror {1 2} -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.17.2.Body {} {
		.barchart1 element configure Element1 -errorbarwidth 5
	}
	
	proc barchart.element.configure.17.2.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the errorbarcap of an element can be set when the xerror has been
	# set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.18.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -xerror {1 2} -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.18.1.Body {} {
		.barchart1 element configure Element1 -errorbarcap 30
		.barchart1 element configure Element1 -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.18.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the errorbarcap of an element can be set when the yerror has been
	# set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.18.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -yerror {1 2} -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.18.2.Body {} {
		.barchart1 element configure Element1 -errorbarcap 30
		.barchart1 element configure Element1 -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.18.2.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}

	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the anchor of the element value can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.19.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 9 2 10 3 11}  \
			-showvalues both -valuefont {Arial 12} -label n -background blue
	}
	
	proc barchart.element.configure.19.1.Body {} {
		.barchart1 element configure Element1 -valueanchor n
		sleep 1000
		.barchart1 element configure Element1 -valueanchor ne
		sleep 1000
		.barchart1 element configure Element1 -valueanchor e
		sleep 1000
		.barchart1 element configure Element1 -valueanchor se
		sleep 1000
		.barchart1 element configure Element1 -valueanchor s
		sleep 1000
		.barchart1 element configure Element1 -valueanchor sw
		sleep 1000
		.barchart1 element configure Element1 -valueanchor w
		sleep 1000
		.barchart1 element configure Element1 -valueanchor nw
		sleep 1000
		.barchart1 element configure Element1 -valueanchor center
	}
	
	proc barchart.element.configure.19.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the color of the element value can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.20.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}  \
			-showvalues both -valuefont {Arial 12}
	}
	
	proc barchart.element.configure.20.1.Body {} {
		.barchart1 element configure Element1 -valuecolor red
	}
	
	proc barchart.element.configure.20.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the element value can be rotated.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.21.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}  \
			-showvalues both -valuefont {Arial 12}
	}
	
	proc barchart.element.configure.21.1.Body {} {
		.barchart1 element configure Element1 -valuerotate 90
	}
	
	proc barchart.element.configure.21.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the color of the shadow of the element value can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.22.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}  \
			-showvalues both -valuefont {Arial 12}
	}
	
	proc barchart.element.configure.22.1.Body {} {
		.barchart1 element configure Element1 -valueshadow red
	}
	
	proc barchart.element.configure.22.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the color of the shadow of the element value can be set and the
	# offset of the shadow can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.22.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}  \
			-showvalues both -valuefont {Arial 12}
	}
	
	proc barchart.element.configure.22.2.Body {} {
		.barchart1 element configure Element1 -valueshadow {red 5}
	}
	
	proc barchart.element.configure.22.2.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the format of the element value can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.23.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}  \
			-showvalues both -valuefont {Arial 12}
	}
	
	proc barchart.element.configure.23.1.Body {} {
		.barchart1 element configure Element1 -valueformat %f
	}
	
	proc barchart.element.configure.23.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the font of the element value can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.24.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}  -showvalues both
	}
	
	proc barchart.element.configure.24.1.Body {} {
		.barchart1 element configure Element1 -valuefont Times
	}
	
	proc barchart.element.configure.24.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the font and font size of the element value can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.24.2.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}  -showvalues both
	}
	
	proc barchart.element.configure.24.2.Body {} {
		.barchart1 element configure Element1 -valuefont {Times 16}
	}
	
	proc barchart.element.configure.24.2.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the font command works when given a font string as input.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.24.3.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}  -showvalues both
	}
	
	proc barchart.element.configure.24.3.Body {} {
		.barchart1 element configure Element1 -valuefont "*-Arial-Bold-R-Normal-*-14-120-*"
	}
	
	proc barchart.element.configure.24.3.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the label relief of the element in the legend can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.25.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}
		.barchart1 element create Element2 -data {1 3 2 4 3 5}
	}
	
	proc barchart.element.configure.25.1.Body {} {
		.barchart1 element configure Element1 -labelrelief raised
	}
	
	proc barchart.element.configure.25.1.Cleanup {} {
		.barchart1 element delete Element2
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the state of the element can be set.
	# DOES NOT WORK
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.26.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.26.1.Body {} {
		.barchart1 element configure Element1 -state active
	}
	
	proc barchart.element.configure.26.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the element values can be shown.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.27.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 4 2 5 3 6} -label x -background blue \
			-valuefont {Arial 12}
	}
	
	proc barchart.element.configure.27.1.Body {} {
		.barchart1 element configure Element1 -showvalues x
		sleep 1000
		.barchart1 element configure Element1 -showvalues y
		sleep 1000
		.barchart1 element configure Element1 -showvalues both
		sleep 1000
		.barchart1 element configure Element1 -showvalues none
	}
	
	proc barchart.element.configure.27.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the error bars of the element can be shown.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.28.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 7 2 8 3 9} -xerror 1 -yerror 1 \
			-background blue -label both
	}
	
	proc barchart.element.configure.28.1.Body {} {
		.barchart1 element configure Element1 -showerrorbars both
		sleep 1000
		.barchart1 element configure Element1 -showerrorbars x
		sleep 1000
		.barchart1 element configure Element1 -showerrorbars y
		sleep 1000
		.barchart1 element configure Element1 -showerrorbars none
	}
	
	proc barchart.element.configure.28.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the background of the element can be changed.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.29.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2}
	}
	
	proc barchart.element.configure.29.1.Body {} {
		.barchart1 element configure Element1 -background pink
	}
	
	proc barchart.element.configure.29.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the barwidth of the element can be changed.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.30.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2}
	}
	
	proc barchart.element.configure.30.1.Body {} {
		.barchart1 element configure Element1 -barwidth 0.5
	}
	
	proc barchart.element.configure.30.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the borderwidth of the element can be changed.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.31.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2}
	}
	
	proc barchart.element.configure.31.1.Body {} {
		.barchart1 element configure Element1 -borderwidth 5
	}
	
	proc barchart.element.configure.31.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the foreground of the element can be changed.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.32.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2}
	}
	
	proc barchart.element.configure.32.1.Body {} {
		.barchart1 element configure Element1 -foreground pink
	}
	
	proc barchart.element.configure.32.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure the relief of the element in the legend can be set.
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.33.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.33.1.Body {} {
		.barchart1 element configure Element1 -relief raised
	}
	
	proc barchart.element.configure.33.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
	
	# ------------------------------------------------------------------------------------
	# Purpose: Ensure configuring stipple changes on screen
	# ------------------------------------------------------------------------------------
	proc barchart.element.configure.34.1.Setup {} {
		barchart .barchart1
		pack .barchart1
		.barchart1 element create Element1 -data {1 1 2 2 3 3}
	}
	
	proc barchart.element.configure.34.1.Body {} {
		.barchart1 element configure Element1 -stipple @greenback.xbm
	}
	
	proc barchart.element.configure.34.1.Cleanup {} {
		.barchart1 element delete Element1
		destroy .barchart1
	}
}
