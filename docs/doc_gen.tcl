package require ruff
package require fileutil

set docDir [file dirname [file normalize [info script]]]
set sourceDir [file join $docDir ..]
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

set commonSphinx [list -title $title -sortnamespaces false -preamble $startPage -pagesplit namespace -recurse false\
                    -includesource false -pagesplit namespace -autopunctuate true -compact false -includeprivate true\
                    -product rbc -diagrammer "ditaa --border-width 1" -version $packageVersion\
                    -copyright "George Yashin" {*}$::argv]
set commonNroff [list -title $title -sortnamespaces false -preamble $startPage -pagesplit namespace -recurse false\
                         -pagesplit namespace -autopunctuate true -compact false -includeprivate false\
                         -product rbc -diagrammer "ditaa --border-width 1" -version $packageVersion\
                         -copyright "George Yashin" {*}$::argv]

set namespaces [list ::TclTk9Upgrade ::rbc ::rbc::GRAPHINST ::rbc::AXIS ::rbc::MARGINAXIS ::rbc::ELEMENT\
                        ::rbc::LINEELEMENT ::rbc::STRIPELEMENT ::rbc::BARELEMENT ::rbc::PEN ::rbc::MARKER\
                        ::rbc::LEGEND ::rbc::GRID ::rbc::CROSSHAIRS ::rbc::POSTSCRIPT\
                        ::rbc::vector ::rbc::VECINST ::rbc::spline ::rbc::winop ::rbc::WINOPIMAGE]
set namespacesNroff $namespaces

ruff::document $namespaces -format sphinx -outfile rbc-tk9.rst -outdir [file join $docDir sphinx] {*}$commonSphinx
ruff::document $namespacesNroff -format nroff -outdir $docDir -outfile rbc.n {*}$commonNroff

::fileutil::appendToFile [file join $docDir sphinx conf.py] {html_theme = "classic"
extensions = [
    "sphinx.ext.githubpages",
]
from pygments.lexers.tcl import TclLexer
from pygments.token import Operator

class MyTclLexer(TclLexer):
    def get_tokens_unprocessed(self, text):
        for i, t, v in super().get_tokens_unprocessed(text):
            if v == "=":
                yield i, Operator, v   # or Name.Builtin
            else:
                yield i, t, v

def setup(app):
    from sphinx.highlighting import lexers
    lexers["tcl"] = MyTclLexer()
}

catch {exec sphinx-build -b html [file join $docDir sphinx] [file join $docDir]} errorStr
puts $errorStr

# nroff pages names processing
foreach file [glob -directory $docDir *.n] {
    set old $file
    set tmp [file join $docDir __temp_rename__.n]
    set new [file join $docDir [string tolower [file tail $file]]]
    file rename $old $tmp
    file rename $tmp $new
}

set specialPages [list  rbc-tcltk9upgrade]

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
