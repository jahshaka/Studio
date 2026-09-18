#!/usr/bin/env python3
"""Extract a line range from a vendored file as a notice text (NOTICES-1).

    extract_notice.py <src> <first> <last> <out>

Used by cmake/Notices.cmake for the components whose notice lives inside a
source or a provenance document rather than in a licence file of its own
(QtAwesome's header comment, the WebXR asset provenance's Licence section).
It exists because CMake's own `file(STRINGS)` mangles exactly the text a
licence is made of — it splits on semicolons and drops non-ASCII, which turned
an em dash into a line break in the first cut. Lines are 1-based and inclusive.
"""
import io
import sys

if len(sys.argv) != 5:
    sys.stderr.write(__doc__)
    sys.exit(2)
src, first, last, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
with io.open(src, encoding="utf-8") as f:
    lines = f.read().split("\n")
if last > len(lines):
    sys.stderr.write("extract_notice: %s has %d lines, asked for %d-%d\n"
                     % (src, len(lines), first, last))
    sys.exit(1)
with io.open(out, "w", encoding="utf-8") as f:
    f.write("\n".join(lines[first - 1:last]).rstrip() + "\n")
