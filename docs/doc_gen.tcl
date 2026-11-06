package require ruff
package require fileutil
package require rbc::graphtoolbar

set docDir [file dirname [file normalize [info script]]]
set sourceDir [file join $docDir ..]
# These maintained files must be present before generation. Missing styles
# otherwise become nonfatal Sphinx warnings and leave an unstyled manual.
set themeStyles {rbc.css rbc-layout.css}
foreach name $themeStyles {
    set path [file join $docDir theme $name]
    if {![file isfile $path] || ![file readable $path]} {
        error "missing documentation stylesheet: $path"
    }
}
source [file join $docDir startPage.ruff]
source [file join $docDir tclTk9Upgrade.ruff]
source [file join $docDir graph.ruff]
source [file join $docDir graphShared.ruff]
source [file join $docDir vector.ruff]
source [file join $docDir spline.ruff]
source [file join $docDir winop.ruff]

set packageVersion [package versions rbc]
puts $packageVersion
set title "Upgraded Tcl/Tk9.0-ready RBC package"

# Image paths are relative to each output's source directory.
set startPageSphinx [string map {docs/images/ images/} $startPage]
set commonSphinx [list -title $title -sortnamespaces false -preamble $startPageSphinx -pagesplit namespace\
                          -recurse false -includesource false -pagesplit namespace -autopunctuate true -compact false\
                          -includeprivate false -product rbc -diagrammer "ditaa --border-width 1" -version\
                          $packageVersion -copyright "George Yashin" {*}$::argv]
set commonNroff [list -title $title -sortnamespaces false -preamble $startPage -pagesplit namespace -recurse false\
                         -pagesplit namespace -autopunctuate true -compact false -includeprivate false\
                         -product rbc -diagrammer "ditaa --border-width 1" -version $packageVersion\
                         -copyright "George Yashin" {*}$::argv]

set namespaces [list ::TclTk9Upgrade ::rbc ::rbc::graphtoolbar ::rbc::GRAPHINST ::rbc::TRACKGRAPH ::rbc::AXIS\
                        ::rbc::MARGINAXIS ::rbc::ELEMENT ::rbc::LINEELEMENT ::rbc::STRIPELEMENT ::rbc::POLARELEMENT\
                        ::rbc::BARELEMENT ::rbc::PEN ::rbc::MARKER ::rbc::LEGEND ::rbc::GRID ::rbc::CROSSHAIRS\
                        ::rbc::POSTSCRIPT ::rbc::SVG ::rbc::PDF ::rbc::vector ::rbc::VECINST ::rbc::spline ::rbc::winop\
                        ::rbc::WINOPIMAGE]
set namespacesNroff $namespaces

# Work around a Ruff ambiguity: ::rbc::graphtoolbar is both a namespace
# and an imported alias for the graphtoolbar TclOO class.
namespace eval ::rbc {
    namespace forget graphtoolbar
}
try {
    ruff::document $namespaces -format sphinx -outfile rbc-tk9.rst -outdir [file join $docDir sphinx] {*}$commonSphinx
    ruff::document $namespacesNroff -format nroff -outdir $docDir -outfile rbc.n {*}$commonNroff
} finally {
    namespace eval ::rbc {
        namespace import ::rbc::graphtoolbar::graphtoolbar
    }
}

::fileutil::appendToFile [file join $docDir sphinx conf.py] {
from pathlib import Path

extensions = list(dict.fromkeys(globals().get("extensions", []) + [
    "sphinx_rtd_theme",
    "sphinx.ext.githubpages",
]))
suppress_warnings = [
    "image.not_readable",
]
html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "collapse_navigation": False,
    "sticky_navigation": True,
    "navigation_depth": 4,
    "includehidden": True,
    "titles_only": False,
}
html_static_path = list(globals().get("html_static_path", []))
html_static_path.append(str(Path(__file__).resolve().parent.parent / "theme"))
html_css_files = list(globals().get("html_css_files", [])) + ["rbc.css", "rbc-layout.css"]
from pygments.lexers.tcl import TclLexer
from pygments.token import Operator

class MyTclLexer(TclLexer):
    def get_tokens_unprocessed(self, text):
        for i, t, v in super().get_tokens_unprocessed(text):
            if v == "=":
                yield i, Operator, v   # or Name.Builtin
            elif v == "$":
                yield i, Operator, v   # or Name.Builtin
            elif v == "\\":
                yield i, Operator, v   # or Name.Builtin
            elif v == "%":
                yield i, Operator, v   # or Name.Builtin
            elif v == "'":
                yield i, Operator, v   # or Name.Builtin
            else:
                yield i, t, v

def setup(app):
    from sphinx.highlighting import lexers
    lexers["tcl"] = MyTclLexer()
}

# Use the same interpreter for Sphinx and its theme dependencies. A failed
# HTML build must fail make doc rather than merely printing the diagnostic.
set python python
if {[info exists ::env(RBC_DOC_PYTHON)] && $::env(RBC_DOC_PYTHON) ne {}} {
    set python $::env(RBC_DOC_PYTHON)
}
# Stage maintained styles explicitly before Sphinx computes asset hashes.
# Do not depend on its static-file overwrite policy for existing HTML output.
file mkdir [file join $docDir _static]
foreach name $themeStyles {
    file copy -force [file join $docDir theme $name] [file join $docDir _static $name]
}
exec -- $python -m sphinx -E -a -b html [file join $docDir sphinx] $docDir >@stdout 2>@stderr

# Confirm that publication/install will receive the current custom styles,
# rather than silently retaining an old output file after a failed copy.
foreach name $themeStyles {
    set output [file join $docDir _static $name]
    if {![file isfile $output] ||
        [::fileutil::cat $output] ne [::fileutil::cat [file join $docDir theme $name]]} {
        error "documentation stylesheet was not copied correctly: $output"
    }
}

# nroff pages names processing
foreach file [glob -directory $docDir *.n] {
    set old $file
    set tmp [file join $docDir __temp_rename__.n]
    set new [file join $docDir [string tolower [file tail $file]]]
    file rename $old $tmp
    file rename $tmp $new
}

set specialPages [list rbc-tcltk9upgrade]

foreach namespacePath $namespacesNroff {
    set tails [list]
    while {$namespacePath ne {}} {
        set tail [string tolower [namespace tail $namespacePath]]
        regsub -all {\s+} [string trim $tail] {-} tail
        set namespacePath [namespace qualifiers $namespacePath]
        lappend tails $tail
    }
    lappend tails [string tolower rbc]
    set manFileName [join [lreverse $tails] -]
    if {$manFileName ni $specialPages} {
        lappend manFilesLinks "${manFileName}(n)"
    }
}

set linksString ".SH SEE ALSO
rbc(n) - package's main page
.br
rbc-tcltk9upgrade(n) - list of upgrades comparing to Tcl/Tk8 RBC package
.br
.sp 1
Public commands and classes documentation:
.br
[join $manFilesLinks \n.br\n]"

proc addLinks2man {fileContents} {
    global linksString
    append fileContents "\n$linksString"
    return $fileContents
}

foreach file [glob -directory $docDir *.n] {
    fileutil::updateInPlace $file addLinks2man
}
