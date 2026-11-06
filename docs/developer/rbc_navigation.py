"""Reuse the main contents headings as RTD navigation groups.

Only rendered navigation trees are changed. Sphinx's document relationships,
C inventory and stored reference-page TOCs remain available to other builders.
"""
from urllib.parse import urlsplit

from docutils import nodes
from sphinx import addnodes
from sphinx.environment.adapters.toctree import TocTree


def _filter_declarations(tree, keep_current=False):
    """Omit declaration list items, optionally retaining same-page targets."""
    for item in list(tree.findall(nodes.list_item)):
        if not item.children or item.parent is None:
            continue
        for link in item.children[0].findall(nodes.reference):
            target = urlsplit(link.get("refuri", ""))
            if target.fragment.startswith("c.") and not (
                keep_current and not target.path
            ):
                parent = item.parent
                parent.remove(item)
                if not parent.children and parent.parent is not None:
                    parent.parent.remove(parent)
                break


def _contents_groups_only(app, doctree, docname):
    """Filter in-page toctrees, leaving reference declarations untouched."""
    if app.builder.format != "html":
        return
    # doctree-resolved runs before Sphinx expands toctree directives.
    adapter = TocTree(app.env)
    for node in list(doctree.findall(addnodes.toctree)):
        resolved = adapter.resolve(docname, app.builder, node)
        if resolved is None:
            node.replace_self([])
        else:
            _filter_declarations(resolved)
            node.replace_self(resolved)


def _sidebar(app, pagename, templatename, context, doctree):
    def grouped_toctree(**options):
        root_doc = app.config.root_doc
        source = app.env.get_doctree(root_doc)
        adapter = TocTree(app.env)
        root_uri = app.builder.get_relative_uri(pagename, root_doc)

        def children(parent):
            result = nodes.bullet_list()
            for child in parent.children:
                if isinstance(child, nodes.compound):
                    # Sphinx places each toctree inside a transparent wrapper.
                    result.extend(children(child).children)
                elif isinstance(child, nodes.section):
                    nested = children(child)
                    if not nested.children:
                        continue
                    title = child.next_node(nodes.title)
                    link = nodes.reference(
                        "", title.astext(), internal=True,
                        refuri=root_uri + "#" + child["ids"][0],
                    )
                    item = nodes.list_item()
                    item += addnodes.compact_paragraph("", "", link)
                    item += nested
                    result += item
                elif isinstance(child, addnodes.toctree):
                    resolved = adapter.resolve(
                        pagename, app.builder, child, maxdepth=-1,
                        collapse=False, includehidden=True,
                        titles_only=options.get("titles_only", False),
                    )
                    if resolved is not None:
                        _filter_declarations(resolved, keep_current=True)
                        listing = resolved.next_node(nodes.bullet_list)
                        if listing is not None:
                            result.extend(listing.children)
            return result

        # The document's first section is its page title, not a sidebar group.
        section = next(source.findall(nodes.section), source)
        tree = children(section)

        def levels(listing, depth):
            active = False
            for item in listing.children:
                item["classes"] = [
                    name for name in item.get("classes", [])
                    if not name.startswith("toctree-l")
                ] + ["toctree-l%d" % depth]
                for nested in item.children:
                    if isinstance(nested, nodes.bullet_list) and levels(nested, depth + 1):
                        if "current" not in item["classes"]:
                            item["classes"].append("current")
                        item["iscurrent"] = True
                        item["aria-expanded"] = "true"
                active = active or "current" in item["classes"]
            return active

        levels(tree, 1)
        return app.builder.render_partial(tree)["fragment"]

    context["toctree"] = grouped_toctree


def setup(app):
    app.connect("doctree-resolved", _contents_groups_only)
    app.connect("html-page-context", _sidebar)
    return {"version": "1", "parallel_read_safe": True, "parallel_write_safe": True}
