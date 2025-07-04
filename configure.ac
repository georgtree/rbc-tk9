#!/bin/bash -norc
dnl	This file is an input file used by the GNU "autoconf" program to
dnl	generate the file "configure", which is run during Tcl installation
dnl	to configure the system for the local environment.
#
# RCS: @(#) $Id: configure.in,v 1.48 2008/11/05 00:13:00 hobbs Exp $

#-----------------------------------------------------------------------
# __CHANGE__
# Set your package name and version numbers here.
#
# This initializes the environment with PACKAGE_NAME and PACKAGE_VERSION
# set as provided.  These will also be added as -D defs in your Makefile
# so you can encode the package version directly into the source files.
#-----------------------------------------------------------------------

AC_INIT([rbc],[0.2.0])

#--------------------------------------------------------------------
# Call TEA_INIT as the first TEA_ macro to set up initial vars.
# This will define a ${TEA_PLATFORM} variable == "unix" or "windows"
# as well as PKG_LIB_FILE and PKG_STUB_LIB_FILE.
#--------------------------------------------------------------------

TEA_INIT([3.7])

AC_CONFIG_AUX_DIR(tclconfig)

#--------------------------------------------------------------------
# Load the tclConfig.sh file
#--------------------------------------------------------------------

TEA_PATH_TCLCONFIG
TEA_LOAD_TCLCONFIG

#--------------------------------------------------------------------
# Load the tkConfig.sh file if necessary (Tk extension)
#--------------------------------------------------------------------

TEA_PATH_TKCONFIG
TEA_LOAD_TKCONFIG

#-----------------------------------------------------------------------
# Handle the --prefix=... option by defaulting to what Tcl gave.
# Must be called after TEA_LOAD_TCLCONFIG and before TEA_SETUP_COMPILER.
#-----------------------------------------------------------------------

TEA_PREFIX

#-----------------------------------------------------------------------
# Standard compiler checks.
# This sets up CC by using the CC env var, or looks for gcc otherwise.
# This also calls AC_PROG_CC, AC_PROG_INSTALL and a few others to create
# the basic setup necessary to compile executables.
#-----------------------------------------------------------------------

TEA_SETUP_COMPILER

#-----------------------------------------------------------------------
# __CHANGE__
# Specify the C source files to compile in TEA_ADD_SOURCES,
# public headers that need to be installed in TEA_ADD_HEADERS,
# stub library C source files to compile in TEA_ADD_STUB_SOURCES,
# and runtime Tcl library files in TEA_ADD_TCL_SOURCES.
# This defines PKG(_STUB)_SOURCES, PKG(_STUB)_OBJECTS, PKG_HEADERS
# and PKG_TCL_SOURCES.
#-----------------------------------------------------------------------

TEA_ADD_SOURCES([rbcAlloc.c rbcBind.c rbcCanvEps.c rbcChain.c rbcColor.c rbcConfig.c rbcGraph.c rbcGrAxis.c rbcGrBar.c rbcGrElem.c rbcGrGrid.c rbcGrHairs.c rbcGrLegd.c rbcGrLine.c rbcGrMarker.c rbcGrMisc.c rbcGrPen.c rbcGrPs.c rbcImage.c rbcInt.c rbcList.c rbcParse.c rbcPs.c rbcSpline.c rbcText.c rbcTile.c rbcUtil.c rbcVecMath.c rbcVecCmd.c rbcVector.c rbcWindow.c rbcWinop.c rbcStubInit.c])
TEA_ADD_HEADERS([])
TEA_ADD_INCLUDES([])
TEA_ADD_LIBS([])
TEA_ADD_CFLAGS([])
TEA_ADD_STUB_SOURCES([generic/rbcStubLib.c])
TEA_ADD_TCL_SOURCES([library/graph.tcl library/rbcCanvEps.pro library/rbcGraph.pro library/circle.xbm])


#------------------------------------------------------------------------
# RBC_ADD_TCL_DEMOS --
#
#	Specify one or more demo script directories, 
#       to be installed in a subdirectory 
#
# Arguments:
#	one or more directory names
#
# Results:
#
#	Defines and substs the following vars:
#		PKG_TCL_DEMOS
#------------------------------------------------------------------------
AC_DEFUN([RBC_ADD_TCL_DEMOS], [
    vars="$@"
    for i in $vars; do
	# check for existence, be strict because it is installed
	if test ! -f "${srcdir}/$i" ; then
	    AC_MSG_ERROR([could not find tcl demo file '${srcdir}/$i'])
	fi
	PKG_TCL_DEMOS="$PKG_TCL_DEMOS $i"
    done
    AC_SUBST(PKG_TCL_DEMOS)
])

RBC_ADD_TCL_DEMOS([
demos/barchart1.tcl
demos/barchart2.tcl
demos/barchart3.tcl
demos/barchart4.tcl
demos/barchart5.tcl
demos/bitmaps/bigBLT.xbm
demos/bitmaps/BLT.xbm
demos/bitmaps/face.xbm
demos/bitmaps/fish/left.xbm
demos/bitmaps/fish/left1.xbm
demos/bitmaps/fish/left1m.xbm
demos/bitmaps/fish/leftm.xbm
demos/bitmaps/fish/mid.xbm
demos/bitmaps/fish/midm.xbm
demos/bitmaps/fish/right.xbm
demos/bitmaps/fish/right1.xbm
demos/bitmaps/fish/right1m.xbm
demos/bitmaps/fish/rightm.xbm
demos/bitmaps/greenback.xbm
demos/bitmaps/hand/hand01.xbm
demos/bitmaps/hand/hand01m.xbm
demos/bitmaps/hand/hand02.xbm
demos/bitmaps/hand/hand02m.xbm
demos/bitmaps/hand/hand03.xbm
demos/bitmaps/hand/hand03m.xbm
demos/bitmaps/hand/hand04.xbm
demos/bitmaps/hand/hand04m.xbm
demos/bitmaps/hand/hand05.xbm
demos/bitmaps/hand/hand05m.xbm
demos/bitmaps/hand/hand06.xbm
demos/bitmaps/hand/hand06m.xbm
demos/bitmaps/hand/hand07.xbm
demos/bitmaps/hand/hand07m.xbm
demos/bitmaps/hand/hand08.xbm
demos/bitmaps/hand/hand08m.xbm
demos/bitmaps/hand/hand09.xbm
demos/bitmaps/hand/hand09m.xbm
demos/bitmaps/hand/hand10.xbm
demos/bitmaps/hand/hand10m.xbm
demos/bitmaps/hand/hand11.xbm
demos/bitmaps/hand/hand11m.xbm
demos/bitmaps/hand/hand12.xbm
demos/bitmaps/hand/hand12m.xbm
demos/bitmaps/hand/hand13.xbm
demos/bitmaps/hand/hand13m.xbm
demos/bitmaps/hand/hand14.xbm
demos/bitmaps/hand/hand14m.xbm
demos/bitmaps/hobbes.xbm
demos/bitmaps/hobbes_mask.xbm
demos/bitmaps/sharky.xbm
demos/bitmaps/xbob.xbm
demos/demos.tcl
demos/extras/clone.tcl
demos/extras/globe.tcl
demos/extras/page.tcl
demos/extras/rgb.txt
demos/extras/send.tcl
demos/extras/xcolors.tcl
demos/graph1.tcl
demos/graph2.tcl
demos/graph3.tcl
demos/graph4.tcl
demos/graph5.tcl
demos/graph6.tcl
demos/graph7.tcl
demos/spline1.tcl
demos/images/blt98.gif
demos/images/flowers.png
demos/images/buckskin.gif
demos/images/chalk.gif
demos/images/close.gif
demos/images/close2.gif
demos/images/clouds.gif
demos/images/corrugated_metal.gif
demos/images/folder.gif
demos/images/mini-book1.gif
demos/images/mini-book2.gif
demos/images/mini-display.gif
demos/images/mini-doc.gif
demos/images/mini-filemgr.gif
demos/images/mini-ofolder.gif
demos/images/mini-windows.gif
demos/images/ofolder.gif
demos/images/open.gif
demos/images/open2.gif
demos/images/out.ps
demos/images/qv100.t.gif
demos/images/metal.png
demos/images/sample.gif
demos/images/smblue_rock.gif
demos/images/stopsign.gif
demos/images/tan_paper.gif
demos/images/tan_paper2.gif
demos/images/txtrflag.gif
demos/README.txt
demos/scripts/common.tcl
demos/scripts/graph46.tcl
demos/scripts/other/barchart2.tcl
demos/scripts/other/graph5.tcl
demos/scripts/other/graph8.tcl
demos/scripts/ps.tcl
demos/stipples/bdiagonal1.xbm
demos/stipples/bdiagonal2.xbm
demos/stipples/checker2.xbm
demos/stipples/checker3.xbm
demos/stipples/cross1.xbm
demos/stipples/cross2.xbm
demos/stipples/cross3.xbm
demos/stipples/crossdiag.xbm
demos/stipples/dot1.xbm
demos/stipples/dot2.xbm
demos/stipples/dot3.xbm
demos/stipples/dot4.xbm
demos/stipples/fdiagonal1.xbm
demos/stipples/fdiagonal2.xbm
demos/stipples/globe.0.xbm
demos/stipples/globe.1.xbm
demos/stipples/globe.10.xbm
demos/stipples/globe.11.xbm
demos/stipples/globe.12.xbm
demos/stipples/globe.13.xbm
demos/stipples/globe.14.xbm
demos/stipples/globe.15.xbm
demos/stipples/globe.16.xbm
demos/stipples/globe.17.xbm
demos/stipples/globe.18.xbm
demos/stipples/globe.19.xbm
demos/stipples/globe.2.xbm
demos/stipples/globe.20.xbm
demos/stipples/globe.21.xbm
demos/stipples/globe.22.xbm
demos/stipples/globe.23.xbm
demos/stipples/globe.24.xbm
demos/stipples/globe.25.xbm
demos/stipples/globe.26.xbm
demos/stipples/globe.27.xbm
demos/stipples/globe.28.xbm
demos/stipples/globe.29.xbm
demos/stipples/globe.3.xbm
demos/stipples/globe.4.xbm
demos/stipples/globe.5.xbm
demos/stipples/globe.6.xbm
demos/stipples/globe.7.xbm
demos/stipples/globe.8.xbm
demos/stipples/globe.9.xbm
demos/stipples/globe.mask.xbm
demos/stipples/hline1.xbm
demos/stipples/hline2.xbm
demos/stipples/hobbes.xbm
demos/stipples/lbottom.xbm
demos/stipples/ltop.xbm
demos/stipples/pattern1.xbm
demos/stipples/pattern2.xbm
demos/stipples/pattern3.xbm
demos/stipples/pattern4.xbm
demos/stipples/pattern5.xbm
demos/stipples/pattern6.xbm
demos/stipples/pattern7.xbm
demos/stipples/pattern8.xbm
demos/stipples/pattern9.xbm
demos/stipples/rbottom.xbm
demos/stipples/rtop.xbm
demos/stipples/vline1.xbm
demos/stipples/vline2.xbm
demos/stripchart1.tcl
demos/thumbnails/barchart1.png
demos/thumbnails/barchart2.png
demos/thumbnails/barchart3.png
demos/thumbnails/barchart4.png
demos/thumbnails/barchart5.png
demos/thumbnails/graph1.png
demos/thumbnails/graph2.png
demos/thumbnails/graph3.png
demos/thumbnails/graph4.png
demos/thumbnails/graph5.png
demos/thumbnails/graph6.png
demos/thumbnails/graph7.png
demos/thumbnails/stripchart1.png
demos/thumbnails/winop1.png
demos/thumbnails/winop2.png
demos/thumbnails/spline1.png
demos/winop1.tcl
demos/winop2.tcl
])

#--------------------------------------------------------------------
# __CHANGE__
# A few miscellaneous platform-specific items:
#
# Define a special symbol for Windows (BUILD_sample in this case) so
# that we create the export library with the dll.
#
# Windows creates a few extra files that need to be cleaned up.
# You can add more files to clean if your extension creates any extra
# files.
#
# TEA_ADD_* any platform specific compiler/build info here.
#--------------------------------------------------------------------

# Add pkgIndex.tcl if it is generated in the Makefile instead of ./configure
# and change Makefile.in to move it from CONFIG_CLEAN_FILES to BINARIES var.
#CLEANFILES="pkgIndex.tcl"
if test "${TEA_PLATFORM}" = "windows" ; then
    AC_DEFINE(BUILD_RBC, 1, [Build windows export dll])
    AC_DEFINE(WIN32, 1, [Tell rbc to use windows])
    CLEANFILES="$CLEANFILES *.lib *.dll *.exp *.ilk *.pdb vc*.pch"
    TEA_ADD_SOURCES([rbcWinDraw.c rbcWinImage.c rbcWinUtil.c])
    #TEA_ADD_INCLUDES([-I\"$(${CYGPATH} ${srcdir}/win)\"])
    TEA_ADD_LIBS([user32.lib winspool.lib advapi32.lib gdi32.lib])
else
    # Ensure no empty else clauses
    :
    TEA_ADD_SOURCES([rbcUnixImage.c])
    TEA_ADD_LIBS([-lX11])
    #TEA_ADD_LIBS([-lsuperfly])
fi
AC_SUBST(CLEANFILES)

#--------------------------------------------------------------------
# __CHANGE__
# Choose which headers you need.  Extension authors should try very
# hard to only rely on the Tcl public header files.  Internal headers
# contain private data structures and are subject to change without
# notice.
# This MUST be called after TEA_LOAD_TCLCONFIG / TEA_LOAD_TKCONFIG
#--------------------------------------------------------------------

TEA_PUBLIC_TCL_HEADERS
#TEA_PRIVATE_TCL_HEADERS

TEA_PUBLIC_TK_HEADERS
TEA_PRIVATE_TK_HEADERS
#TEA_PATH_X

#--------------------------------------------------------------------
# Check whether --enable-threads or --disable-threads was given.
# This auto-enables if Tcl was compiled threaded.
#--------------------------------------------------------------------

TEA_ENABLE_THREADS

#--------------------------------------------------------------------
# The statement below defines a collection of symbols related to
# building as a shared library instead of a static library.
#--------------------------------------------------------------------

TEA_ENABLE_SHARED

#--------------------------------------------------------------------
# This macro figures out what flags to use with the compiler/linker
# when building shared/static debug/optimized objects.  This information
# can be taken from the tclConfig.sh file, but this figures it all out.
#--------------------------------------------------------------------

TEA_CONFIG_CFLAGS

#--------------------------------------------------------------------
# Set the default compiler switches based on the --enable-symbols option.
#--------------------------------------------------------------------

TEA_ENABLE_SYMBOLS

#--------------------------------------------------------------------
# Everyone should be linking against the Tcl stub library.  If you
# can't for some reason, remove this definition.  If you aren't using
# stubs, you also need to modify the SHLIB_LD_LIBS setting below to
# link against the non-stubbed Tcl library.  Add Tk too if necessary.
#--------------------------------------------------------------------

AC_DEFINE(USE_TCL_STUBS, 1, [Use Tcl stubs])
AC_DEFINE(USE_TK_STUBS, 1, [Use Tk stubs])

#--------------------------------------------------------------------
# This macro generates a line to use when building a library.  It
# depends on values set by the TEA_ENABLE_SHARED, TEA_ENABLE_SYMBOLS,
# and TEA_LOAD_TCLCONFIG macros above.
#--------------------------------------------------------------------

TEA_MAKE_LIB

#--------------------------------------------------------------------
# Determine the name of the tclsh and/or wish executables in the
# Tcl and Tk build directories or the location they were installed
# into. These paths are used to support running test cases only,
# the Makefile should not be making use of these paths to generate
# a pkgIndex.tcl file or anything else at extension build time.
#--------------------------------------------------------------------

TEA_PROG_TCLSH
TEA_PROG_WISH


#------------------------------------------------------------------------
# Define the PLATFORM variable in a very tcl-ish way for teapot.txt
#------------------------------------------------------------------------
PTL=`echo ${TCL_SRC_DIR}/library` 
PLATFORM=`echo "catch { package require platform ; puts [[platform::identify]] } ; exit" | TCL_LIBRARY="$PTL" ${TCLSH_PROG}`
if test X"$PLATFORM" = X
then
    PLATFORM=unknown
fi
AC_SUBST(PLATFORM)


#--------------------------------------------------------------------
# Finally, substitute all of the various values into the Makefile.
# You may alternatively have a special pkgIndex.tcl.in or other files
# which require substituting th AC variables in.  Include these here.
#--------------------------------------------------------------------

AC_CONFIG_FILES([Makefile pkgIndex.tcl])
AC_OUTPUT

