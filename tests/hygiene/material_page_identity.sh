#!/usr/bin/env bash
#
# source.material_page_identity — THE MATERIALS PAGE BECOMES A MATERIAL IN ONE
# PLACE, AFTER ITS GRAPH HAS LOADED (LEGACY-CONVERT-CRUD fix round).
#
# THE DEFECT THIS GATES, which was live and is data loss. Five tile handlers
# wrote `currentShaderInformation` — the guid, the name and the origin the page
# SAVES BY — before calling `loadGraph`, and `loadGraph` itself dropped the
# read-only banner for the guid it was about to read. When the read then
# REFUSED (a graph written on the deleted "Surface Material" master), every one
# of those writes stayed: the canvas still held the PREVIOUS material's graph,
# the page's identity was the REFUSED material, and the save was armed. The
# next node drag — or the 1.5 s autosave on the way out of the page — wrote the
# previous graph into the refused material's row through `MaterialBundle::write`.
# Worst case that is a shipped preset's graph landing on a user's material,
# because the refusal had just taken the read-only banner down.
#
# Nothing about it is visible at a call site: `currentShaderInformation.GUID =
# item->...; loadGraph(...)` reads like ordinary bookkeeping, and the loss only
# happens on the one path where the load says no.
#
# THE RULE: the three identity fields are written ONLY by the functions that
# own an identity change — `adoptGraph` (a document takes a material that HAS
# loaded), `createShader` (a material this page just made), `loadGraphFromTemplate`
# (an unsaved new graph clears the guid) and `editingFinishedOnListItem` (a
# rename). A signal handler naming a material must pass it to `loadGraph` as an
# ARGUMENT and let the load decide.
#
# THE IDENTITY IS A DOCUMENT'S SINCE MATERIALS-TABS-1 (several materials are
# open at once, each with its own guid, origin, graph, undo stack and pending
# autosave), so the field spelling this reads is `doc->info.<field>` as well as
# the page's old `currentShaderInformation.<field>`. The rule and the defect it
# gates are unchanged — one place, after the graph is in hand — and the list
# gains the two functions that own the OTHER two identity changes a document
# can have, both of which are about a material that is no longer what it was:
#   * `renameOpenDocuments` — the body of `editingFinishedOnListItem`'s rename,
#     which now has to reach EVERY open document of the renamed material rather
#     than "the current one".
#   * `forgetMaterial` — the material was deleted from the library, so the
#     documents that are it stop being it (and close). Clearing an identity
#     cannot write a graph into a row: the save is stood down first.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: material_page_identity.sh <source-root>}"
FILE="$ROOT/src/modules/materials/effectspage.cpp"
failures=0

if [ ! -f "$FILE" ]; then
    echo "source.material_page_identity: FAIL — $FILE is missing"
    exit 1
fi

# The functions allowed to write the page's identity. A new one here is a
# deliberate decision, which is the point of the list.
ALLOWED="adoptGraph createShader loadGraphFromTemplate editingFinishedOnListItem renameOpenDocuments forgetMaterial"

offenders=$(awk -v allowed="$ALLOWED" '
    # a function definition starts at column 0: "void EffectsPage::name(..."
    /^[A-Za-z_].*EffectsPage::[A-Za-z_]+\(/ {
        line = $0
        sub(/^.*EffectsPage::/, "", line)
        sub(/\(.*$/, "", line)
        fn = line
    }
    /(currentShaderInformation|->info)\.(GUID|name|origin)[ \t]*=([^=]|$)/ || /->info[ \t]*=[ \t]*shaderInfo\(\)/ {
        ok = 0
        n = split(allowed, a, " ")
        for (i = 1; i <= n; i++) if (fn == a[i]) ok = 1
        if (!ok) printf "    %s:%d (in %s): %s\n", FILENAME, NR, fn, $0
    }
' "$FILE")

if [ -n "$offenders" ]; then
    echo "source.material_page_identity: FAIL — the page's identity is written outside the"
    echo "  functions that own an identity change ($ALLOWED):"
    echo "$offenders"
    echo "    A handler that names a material passes it to loadGraph and lets the LOAD"
    echo "    decide: a refused graph must leave the page exactly as it found it, or the"
    echo "    next edit is written into the row of a material that never opened."
    failures=1
else
    echo "source.material_page_identity: ok — identity changes only where a material is adopted"
fi

# AND THE ADOPTION MUST STILL BE ONE STEP. `adoptGraph` is what makes the rule
# above enforceable: identity, read-only state and canvas together, after the
# graph is in hand.
if ! grep -q 'void EffectsPage::adoptGraph(' "$FILE"; then
    echo "source.material_page_identity: FAIL — EffectsPage::adoptGraph is gone or renamed"
    failures=1
fi
# ...and the read-only state still moves WITH the graph, in that one step: it
# is what stands the save down for a material that cannot take an edit, and
# dropping it for a material that turned out not to open is how an edit
# reached a read-only row.
# (It is the DOCUMENT's state since MATERIALS-TABS-1 — one tab may be a
# locked preset while another is the user's own material — and since
# PRESET-EDIT-1 "locked" is no longer "is a preset": a preset a project holds
# is editable there and the first edit copies it, so the line reads
# `doc->readOnly = !shippedName.isEmpty() && <the refusal stands>`.)
if ! grep -q 'doc->readOnly = !shippedName.isEmpty()' "$FILE"; then
    echo "source.material_page_identity: FAIL — the read-only state no longer moves with the graph"
    failures=1
fi

exit $failures
