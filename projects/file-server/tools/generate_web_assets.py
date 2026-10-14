#!/usr/bin/env python3
"""Reproducibly embed the browser page, without external CDNs or a build chain."""
from pathlib import Path
import gzip
web = Path(__file__).resolve().parents[1] / 'firmware/web'
destination = web / 'app.html.gz'
data = gzip.compress((web / 'app.html').read_bytes(), compresslevel=9, mtime=0)
if not destination.exists() or destination.read_bytes() != data:
    destination.write_bytes(data)
