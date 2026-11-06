"""Maintained configuration for the private C manual; Ruff does not generate it."""
import os
import sys
from pathlib import Path

project = "RBC Developer Manual"
release = os.environ.get("RBC_DOC_VERSION", "")
version = release
# Local Sphinx navigation extension; build orchestration remains in Tcl.
sys.path.insert(0, str(Path(__file__).resolve().parent))
extensions = ["breathe", "sphinx_rtd_theme", "sphinx.ext.githubpages", "rbc_navigation"]
breathe_projects = {
    "rbc": os.environ["RBC_DOXYGEN_XML"],
    "rbc-win": os.environ["RBC_DOXYGEN_WIN_XML"],
}
breathe_default_project = "rbc"
breathe_domain_by_extension = {"h": "c", "c": "c"}
primary_domain = "c"
# VectorData uses "complex" as a member identifier, not the optional C99
# <complex.h> macro. Preserve Sphinx's other extra keyword spellings.
c_extra_keywords = ["alignas", "alignof", "bool", "imaginary", "noreturn",
                    "static_assert", "thread_local"]
highlight_language = "c"
# Include declarations in navigation, using short names within their C groups.
toc_object_entries = True
toc_object_entries_show_parents = "hide"
root_doc = "index"
# Published HTML lives below the sources. Never rescan its copied RST files.
exclude_patterns = ["html", "__pycache__"]
try:
    _output_relative = os.path.relpath(os.environ["RBC_DOC_HTML"], os.path.dirname(__file__))
except ValueError:  # Output on another Windows drive is outside the source tree.
    _output_relative = None
if _output_relative is not None and _output_relative != os.pardir and not _output_relative.startswith(os.pardir + os.sep):
    exclude_patterns.append(_output_relative.replace(os.sep, "/"))
html_theme = "sphinx_rtd_theme"
html_theme_options = {
    "collapse_navigation": False,
    "sticky_navigation": True,
    "navigation_depth": -1,
    "includehidden": True,
    "titles_only": False,
}
html_static_path = [str(Path(__file__).resolve().parent.parent / "theme")]
html_css_files = ["rbc.css", "rbc-layout.css", "rbc-developer.css"]
html_title = f"RBC {release} Developer Manual"
# Published sources remain in the repository, not in the generated site.
html_copy_source = False
html_show_sourcelink = False
