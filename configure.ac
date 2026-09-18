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

AC_INIT([rbc],[0.5.0])

#--------------------------------------------------------------------
# Call TEA_INIT as the first TEA_ macro to set up initial vars.
# This will define a ${TEA_PLATFORM} variable == "unix" or "windows"
# as well as PKG_LIB_FILE and PKG_STUB_LIB_FILE.
#--------------------------------------------------------------------

TEA_INIT()

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
AC_SUBST([SHLIB_SUFFIX])

#-----------------------------------------------------------------------
# __CHANGE__
# Specify the C source files to compile in TEA_ADD_SOURCES,
# public headers that need to be installed in TEA_ADD_HEADERS,
# stub library C source files to compile in TEA_ADD_STUB_SOURCES,
# and runtime Tcl library files in TEA_ADD_TCL_SOURCES.
# This defines PKG(_STUB)_SOURCES, PKG(_STUB)_OBJECTS, PKG_HEADERS
# and PKG_TCL_SOURCES.
#-----------------------------------------------------------------------

TEA_ADD_SOURCES([rbcRender.c rbcAlloc.c rbcBind.c rbcCanvEps.c rbcChain.c rbcConfig.c rbcGraph.c rbcGrAxis.c rbcGrBar.c rbcGrElem.c rbcGrGrid.c rbcGrHairs.c rbcGrLegd.c rbcGrLine.c rbcGrMarker.c rbcGrMisc.c rbcGrPen.c rbcGrPolar.c rbcGrPs.c rbcGrExport.c rbcGrSvg.c rbcImage.c rbcInt.c rbcList.c rbcPs.c rbcSpline.c rbcText.c rbcTile.c rbcUtil.c rbcVecMath.c rbcVecCmd.c rbcVector.c rbcWindow.c rbcWinop.c rbcStubInit.c])
TEA_ADD_HEADERS([])
TEA_ADD_INCLUDES([])
TEA_ADD_LIBS([])
TEA_ADD_CFLAGS([])
TEA_ADD_STUB_SOURCES([generic/rbcStubLib.c])
TEA_ADD_TCL_SOURCES([library/graphtoolbar.tcl library/rbcCanvEps.pro library/rbcGraph.pro library/pointer.xbm library/pointer_mask.xbm])

DOC_HTML_FILES="index.html objects.inv search.html searchindex.js genindex.html"
DOC_SRCDIR="${srcdir}/docs"
if test -d "$DOC_SRCDIR"; then
    # Add all top-level *.html files from docs/, not subdirectories.
    for f in "$DOC_SRCDIR"/*.html; do
        test -f "$f" || continue

        b=`basename "$f"`

        case " $DOC_HTML_FILES " in
            *" $b "*) ;;
            *) DOC_HTML_FILES="$DOC_HTML_FILES $b" ;;
        esac
    done
    # Add everything from docs/_static and docs/assets recursively.
    for d in _static assets; do
        if test -d "$DOC_SRCDIR/$d"; then
            for f in `find "$DOC_SRCDIR/$d" -type f -print`; do
                rel=`echo "$f" | sed "s|^$DOC_SRCDIR/||"`

                case " $DOC_HTML_FILES " in
                    *" $rel "*) ;;
                    *) DOC_HTML_FILES="$DOC_HTML_FILES $rel" ;;
                esac
            done
        fi
    done

else
    AC_MSG_WARN([documentation directory not found: $DOC_SRCDIR])
fi
#AC_MSG_NOTICE([DOC_HTML_FILES = $DOC_HTML_FILES])
AC_SUBST([DOC_HTML_FILES])

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
demos/cairoGraph1.tcl
demos/cairoBar1.tcl
demos/barchart1.tcl
demos/barchart2.tcl
demos/barchart3.tcl
demos/barchart4.tcl
demos/barchart5.tcl
demos/bitmaps/spiral.xbm
demos/bitmaps/spiral_mask.xbm
demos/bitmaps/hobbes.xbm
demos/bitmaps/hobbes_mask.xbm
demos/demos.tcl
demos/extras/clone.tcl
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
demos/graph8.tcl
demos/spline1.tcl
demos/polar1.tcl
demos/polar2.tcl
demos/mixed1.tcl
demos/combined1.tcl
demos/images/chalk.gif
demos/images/stopsign.gif
demos/images/qv100.t.gif
demos/images/sample.gif
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
demos/stipples/hline1.xbm
demos/stipples/hline2.xbm
demos/stipples/hobbes.xbm
demos/stipples/lbottom.xbm
demos/stipples/ltop.xbm
demos/stipples/pattern1.xbm
demos/stipples/pattern2.xbm
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
demos/thumbnails/graph8.png
demos/thumbnails/stripchart1.png
demos/thumbnails/winop1.png
demos/thumbnails/winop2.png
demos/thumbnails/spline1.png
demos/thumbnails/mixed1.png
demos/thumbnails/polar1.png
demos/thumbnails/polar2.png
demos/thumbnails/cairoGraph1.png
demos/thumbnails/cairoBar1.png
demos/thumbnails/combined1.png
demos/winop1.tcl
demos/winop2.tcl
demos/cairo-bitmap.xbm
demos/data/periodic_elements.csv
demos/data/bandwidth.csv
demos/data/filter_s11.csv
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
TEA_ADD_CFLAGS([-DRBCAPI=MODULE_SCOPE])

# Optional Cairo support. Native builds have no Cairo dependency.
AC_ARG_ENABLE([cairo-static],
    [AS_HELP_STRING([--enable-cairo-static],
        [link Cairo and its non-system dependencies statically into the shared RBC library])],
    [], [enable_cairo_static=no])
AS_CASE([$enable_cairo_static], [yes|no], [],
    [AC_MSG_ERROR([--enable-cairo-static accepts only yes or no])])

AC_ARG_ENABLE([cairo],
    [AS_HELP_STRING([--enable-cairo], [build the optional Cairo renderer (default: no)])],
    [], [enable_cairo=$enable_cairo_static])
AC_ARG_VAR([CAIRO_CFLAGS], [compiler flags for Cairo])
AC_ARG_VAR([CAIRO_LIBS], [linker flags for Cairo])
AS_CASE([$enable_cairo], [yes|no], [],
    [AC_MSG_ERROR([--enable-cairo accepts only yes or no])])

AS_IF([test "x$enable_cairo_static" = xyes && test "x$enable_cairo" = xno],
    [AC_MSG_ERROR([--enable-cairo-static cannot be combined with --disable-cairo])])

AS_IF([test "x$enable_cairo" = xyes], [
    AS_IF([test "x$TEA_PLATFORM" = xwindows],
        [rbc_cairo_module=cairo-win32], [rbc_cairo_module=cairo-xlib])
    AS_IF([test -z "$CAIRO_CFLAGS" || test -z "$CAIRO_LIBS"], [
        AC_PATH_TOOL([PKG_CONFIG], [pkg-config], [no])
        AS_IF([test "x$PKG_CONFIG" = xno],
            [AC_MSG_ERROR([Cairo requires pkg-config or CAIRO_CFLAGS and CAIRO_LIBS])])
        AS_IF([$PKG_CONFIG --exists "$rbc_cairo_module >= 1.12"], [],
            [AC_MSG_ERROR([Cairo >= 1.12 with $rbc_cairo_module support is required])])
        AS_IF([test -z "$CAIRO_CFLAGS"],
            [CAIRO_CFLAGS=`$PKG_CONFIG --cflags "$rbc_cairo_module"`])
        AS_IF([test -z "$CAIRO_LIBS"], [
            rbc_cairo_pkg_flags=
            AS_IF([test "x$enable_cairo_static" = xyes],
                [rbc_cairo_pkg_flags=--static])
            CAIRO_LIBS=`$PKG_CONFIG $rbc_cairo_pkg_flags --libs "$rbc_cairo_module"`
            AS_IF([test $? -ne 0],
                [AC_MSG_ERROR([cannot obtain Cairo linker flags from pkg-config])])
        ])
    ])
    AS_IF([test "x$enable_cairo_static" = xyes], [
        AC_MSG_NOTICE([selecting static Cairo dependency archives])
        AS_IF([test "x$TEA_PLATFORM" = xwindows], [
            CAIRO_CFLAGS="$CAIRO_CFLAGS -DCAIRO_WIN32_STATIC_BUILD"
        ])
        # Exact archive names prevent falling back to shared libraries.
        # Operating-system and compiler runtime libraries remain dynamic.
        rbc_cairo_static_libs=
        for rbc_cairo_flag in $CAIRO_LIBS; do
            case "$rbc_cairo_flag" in
                -lc|-lm|-ldl|-lrt|-lpthread|-lgcc|-lgcc_s|-lstdc++)
                    ;;
                -lX11|-lXext|-lXrender|-lXau|-lXdmcp|\
                -lxcb|-lxcb-render|-lxcb-shm)
                    ;;
                -lkernel32|-luser32|-lgdi32|-lmsimg32|-ladvapi32|\
                -lshell32|-lshlwapi|-lole32|-loleaut32|-luuid|\
                -lws2_32|-lwinspool|-lcomdlg32|-lcomctl32|\
                -limm32|-lwinmm|-lversion|-lsetupapi|-luserenv|\
                -lcrypt32|-lbcrypt|-lrpcrt4|-lsecur32|-lusp10|\
                -ldwrite|-ld2d1|-ld3d11|-ldxgi|-lwindowscodecs|\
                -lmsvcrt|-lucrt|-lucrtbase|-lmingw32|-lmingwex)
                    ;;
                *.dll.a|*.so|*.so.*|*.dylib|*.dll)
                    AC_MSG_ERROR([shared library in static Cairo flags: $rbc_cairo_flag])
                    ;;
                -l:*.a)
                    ;;
                -l:*)
                    AC_MSG_ERROR([expected a static archive in Cairo flags: $rbc_cairo_flag])
                    ;;
                -l*)
                    rbc_cairo_name=${rbc_cairo_flag#-l}
                    rbc_cairo_flag="-l:lib${rbc_cairo_name}.a"
                    ;;
            esac
            rbc_cairo_static_libs="$rbc_cairo_static_libs $rbc_cairo_flag"
        done

        # Allow dependencies between static archives to be resolved
        # without requiring a particular archive ordering.
        rbc_cairo_runtime_flags=
        AS_IF([test "x$TEA_PLATFORM" = xwindows], [
            # MSYS2 Cairo includes C++ code; static gettext also needs iconv.
            rbc_cairo_static_libs="$rbc_cairo_static_libs -l:libstdc++.a -l:libiconv.a"
            rbc_cairo_runtime_flags=-static-libgcc
        ])
        CAIRO_LIBS="$rbc_cairo_runtime_flags -Wl,--start-group $rbc_cairo_static_libs -Wl,--end-group"
    ])
    rbc_saved_CPPFLAGS=$CPPFLAGS
    rbc_saved_CFLAGS=$CFLAGS
    rbc_saved_LDFLAGS=$LDFLAGS
    rbc_saved_LIBS=$LIBS
    CPPFLAGS="$CPPFLAGS $CAIRO_CFLAGS"
    LIBS="$CAIRO_LIBS $LIBS"

    AS_IF([test "x$enable_cairo_static" = xyes], [
        # Test incorporation into a shared library, not just an
        # executable. On Linux this also detects unsuitable non-PIC
        # archives. No test program is executed.
        AS_IF([test "x$TEA_PLATFORM" != xwindows],
            [CFLAGS="$CFLAGS -fPIC"])
        LDFLAGS="$LDFLAGS -shared -Wl,--no-undefined"
        AC_MSG_CHECKING([whether static Cairo links into a shared library])
    ], [
        AC_MSG_CHECKING([whether the Cairo platform backend links])
    ])
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <cairo.h>
#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1, 12, 0)
#error Cairo 1.12 or newer is required
#endif
#ifdef WIN32
#include <cairo-win32.h>
#if !CAIRO_HAS_WIN32_SURFACE
#error Cairo Win32 surfaces are required
#endif
#else
#include <cairo-xlib.h>
#if !CAIRO_HAS_XLIB_SURFACE
#error Cairo Xlib surfaces are required
#endif
#endif
    ]], [[
#ifdef WIN32
        cairo_surface_t *s = cairo_win32_surface_create((HDC)0);
#else
        cairo_surface_t *s = cairo_xlib_surface_create((Display *)0,
            (Drawable)0, (Visual *)0, 1, 1);
#endif
        cairo_t *cr = cairo_create(s);
        cairo_stroke(cr);
        cairo_destroy(cr);
        cairo_surface_destroy(s);
    ]])], [rbc_cairo_link=yes], [rbc_cairo_link=no])

    CPPFLAGS=$rbc_saved_CPPFLAGS
    CFLAGS=$rbc_saved_CFLAGS
    LDFLAGS=$rbc_saved_LDFLAGS
    LIBS=$rbc_saved_LIBS
    AC_MSG_RESULT([$rbc_cairo_link])
    AS_IF([test "x$rbc_cairo_link" != xyes], [
        AS_IF([test "x$enable_cairo_static" = xyes], [
            AC_MSG_ERROR([static Cairo link failed; check config.log. Cairo and its non-system dependencies require genuine static archives; Linux archives must support linking into a shared library (normally built with -fPIC).])
        ], [
            AC_MSG_ERROR([Cairo backend link failed; check CAIRO_CFLAGS, CAIRO_LIBS and config.log])
        ])
    ])
    AC_DEFINE([RBC_HAVE_CAIRO], [1], [Build the optional Cairo renderer])
    TEA_ADD_INCLUDES([$CAIRO_CFLAGS])
    TEA_ADD_LIBS([$CAIRO_LIBS])
])


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


#--------------------------------------------------------------------
# Finally, substitute all of the various values into the Makefile.
# You may alternatively have a special pkgIndex.tcl.in or other files
# which require substituting th AC variables in.  Include these here.
#--------------------------------------------------------------------

AC_CONFIG_FILES([Makefile pkgIndex.tcl])
AC_OUTPUT

