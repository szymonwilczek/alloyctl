# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Szymon Wilczek
#
# Configuration for the Sphinx documentation builder.
# https://www.sphinx-doc.org/en/master/usage/configuration.html
#
# Build locally with:  make htmldocs   (or: make -C Documentation html)
# CI builds with warnings promoted to errors (see SPHINXOPTS in the Makefile).

import os
import pathlib
import re

# -- Project information ------------------------------------------------------

project = "alloyctl"
author = "Szymon Wilczek"
copyright = "2026, Szymon Wilczek"  # noqa: A001 - Sphinx-reserved name


# Version shown on the site.
# Deploy/CI can override it through ALLOYCTL_DOCS_VERSION (release tag);
# Otherwise fall back to ALLOY_VERSION as defined in src/alloy.h so the docs
# and the binary never disagree about the version string.
def _version_from_source() -> str:
    header = pathlib.Path(__file__).resolve().parent.parent / "src" / "alloy.h"
    if header.is_file():
        match = re.search(
            r'#define\s+ALLOY_VERSION\s+"([^"]+)"',
            header.read_text(encoding="utf-8"),
        )
        if match:
            return match.group(1)
    return "0.0.0"


_tag = os.environ.get("ALLOYCTL_DOCS_VERSION", "").strip()
release = (_tag[1:] if _tag.startswith("v") else _tag) or _version_from_source()
version = release.split("-", 1)[0]

# Branch this build documents;
# Source-tree link resolves against it.
# Deploy/CI sets ALLOYCTL_DOCS_REF; local builds default to main.
ref = os.environ.get("ALLOYCTL_DOCS_REF", "").strip() or "main"

# -- General configuration ----------------------------------------------------

extensions: list[str] = ["sphinx.ext.extlinks"]

# :ghsrc:`path/to/file` links a repository-relative path to its source on GitHub,
# pinned to the branch this build documents (see ref above).
extlinks = {
    "ghsrc": (f"https://github.com/szymonwilczek/alloyctl/blob/{ref}/%s", "%s"),
}
extlinks_detect_hardcoded_links = True

source_suffix = {".rst": "restructuredtext"}
root_doc = "index"
language = "en"

exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

# Technical documentation quotes shell commands and CLI flags verbatim.
# Leave "--flag", "---", and quotes untouched so an option never renders
# as an en- or em-dash.
smartquotes = False

# -- HTML output --------------------------------------------------------------

# Prefer the Read the Docs theme.
# (CI pins it in Documentation/requirements.txt).
try:
    import sphinx_rtd_theme  # noqa: F401

    html_theme = "sphinx_rtd_theme"
except ImportError:
    html_theme = "alabaster"

html_title = f"alloyctl {release}"
html_static_path: list[str] = []
html_show_sourcelink = True

# -- Link checking ------------------------------------------------------------

# Source-tree links resolve to github.com/szymonwilczek/alloyctl/blob/<ref>/...;
# their existence is owned by the repository, and the internal :doc:/:ref: graph
# is already validated by the strict HTML build, so leave these out of linkcheck.
linkcheck_ignore = [r"https://github\.com/szymonwilczek/alloyctl/blob/"]
linkcheck_retries = 2
linkcheck_timeout = 15
