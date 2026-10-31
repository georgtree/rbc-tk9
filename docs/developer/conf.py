"""Maintained configuration for the private C manual; Ruff does not generate it."""
import os

project = "RBC Developer Manual"
release = os.environ.get("RBC_DOC_VERSION", "")
version = release
extensions = ["breathe"]
breathe_projects = {"rbc": os.environ["RBC_DOXYGEN_XML"]}
breathe_default_project = "rbc"
breathe_domain_by_extension = {"h": "c", "c": "c"}
primary_domain = "c"
highlight_language = "c"
root_doc = "index"
# Published HTML lives below the sources. Never rescan its copied RST files.
exclude_patterns = ["html", "__pycache__"]
try:
    _output_relative = os.path.relpath(os.environ["RBC_DOC_HTML"], os.path.dirname(__file__))
except ValueError:  # Output on another Windows drive is outside the source tree.
    _output_relative = None
if _output_relative is not None and _output_relative != os.pardir and not _output_relative.startswith(os.pardir + os.sep):
    exclude_patterns.append(_output_relative.replace(os.sep, "/"))
html_theme = "classic"
html_title = f"RBC {release} Developer Manual"
# Published sources remain in the repository, not in the generated site.
html_copy_source = False
html_show_sourcelink = False
