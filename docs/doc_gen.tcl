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
                        ::rbc::LINEELEMENT ::rbc::STRIPELEMENT ::rbc::BARELEMENT ::rbc::PEN\
                        ::rbc::vector ::rbc::VECINST ::rbc::spline]

ruff::document $namespaces -format sphinx -outfile rbc-tk9.rst -outdir [file join $docDir sphinx] {*}$commonSphinx
ruff::document $namespaces -format nroff -outdir $docDir -outfile rbc.n {*}$commonNroff

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
