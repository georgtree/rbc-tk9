Bundled argparse fallback for rbc::graphtoolbar

Upstream: https://github.com/georgtree/argparse
Version: 0.65
Commit: 87d05d0a103170288e88e59e3496bfc065893def
File: argparse.tcl (pure Tcl implementation)
License: MIT; see argparse-LICENSE.txt.

The source is retained from upstream, with the licence filename in its header
adjusted to argparse-LICENSE.txt and trailing whitespace removed. The original
copyright notices remain.

The toolbar first tries package require argparse. It uses this copy only when
argparse is not registered or installed. Failures from an installed package
are propagated. This file has no package index entry of its own, so native RBC
and rbc::vector do not load or advertise it automatically.
