#!/usr/bin/env bash
#
# source.one_readfile — every assimp parse of a model path goes through THE
# CHOKE POINT (IMPORT-1, SPECS/IMPORT_DIALOG_SPEC.md §4.1/§10).
#
# WHY. Since the import dialog an asset's scale, rotation and origin are BAKED
# at import: the transform is handed to assimp's own GLOBAL_SCALE_FACTOR before
# the read and pre-multiplied onto the parsed scene's root node after it
# (irisgl/import/scenesource.cpp, readSceneFile). A parse that does not go
# through there gets NO transform, so it produces geometry of a DIFFERENT SIZE
# from the asset's bake — silently, and only for assets whose settings are not
# identity. That is the exact class services/meshbakestore.cpp already
# documents for the unit factor, and it is why the routing needs a gate rather
# than a convention: a `ReadFile` call is one line and looks harmless.
#
# THE RULE: inside irisgl/import and irisgl/document, the only translation unit
# that may call Importer::ReadFile / ReadFileFromMemory is
# irisgl/import/scenesource.cpp. Everything else calls readSceneFile.
#
# White-box test suites (tests/) are NOT covered: they link assimp themselves
# and drive the vendored importer directly, on fixtures, with no bake behind
# them.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: one_readfile.sh <source-root>}"
cd "$ROOT" || { echo "source.one_readfile: no such root $ROOT"; exit 1; }

failures=0

# A CALL, not a mention: importflags.h and this script's own prose name
# ReadFile in comments, and a comment cannot parse a file.
hits=$(grep -rnE '(\.|->)ReadFile(FromMemory)?[[:space:]]*\(' \
           irisgl/import irisgl/document \
       | grep -v '^irisgl/import/scenesource\.cpp:' || true)

if [ -n "$hits" ]; then
    echo "source.one_readfile: FAIL — an assimp read outside the choke point"
    echo "$hits" | sed 's/^/    /'
    echo "    Route it through iris::readSceneFile (irisgl/import/scenesource.h) and"
    echo "    hand it the asset's ImportTransform, or the parse and the bake disagree."
    failures=1
else
    echo "source.one_readfile: ok — irisgl/import/scenesource.cpp is the only assimp read"
fi

# The choke point must still BE one: readSceneFile has to keep taking a
# transform, or routing through it would mean nothing.
if ! grep -q 'const ImportTransform &xf' irisgl/import/scenesource.h; then
    echo "source.one_readfile: FAIL — readSceneFile no longer takes an ImportTransform"
    failures=1
fi

exit $failures
