#!/usr/bin/env bash
#
# source.no_engine_transform_reads — THE DOCUMENT NEVER ASKS THE ENGINE WHERE A NODE IS (ENGINE-UPGRADE-U2 stage 1,
# DOCSTORE).
#
# WHAT WAS WRONG. Since SCENEGRAPH_SPEC D2 the document's hierarchy WAS Ogre's: every world-transform read in the
# program (119 getter sites in 38 files) ended in `Ogre::Node::_getDerived*Updated()` / `_getFullTransformUpdated()`
# inside the document's graph layer, on the UI thread, writing Ogre's SoA as it went. That one fact forbids a render
# thread (ENGINE_UPGRADE_SPEC §1: the render thread owns every Ogre object). U2 gave the document its own transform
# store (irisgl/document/scenegraph/nodegraph.h: the local TRS, the parent, the ordered children and a world derived
# by Ogre's own rule in document maths) and answers every read there.
#
# THE RULE: no `_get…Updated` call — code or prose — anywhere outside the engine's private translation units
# (irisgl/engine/src). The engine's own reads there run on the engine's thread and are not the document's.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: no_engine_transform_reads.sh <source-root>}"
cd "$ROOT" || { echo "source.no_engine_transform_reads: no such root $ROOT"; exit 1; }

# FIRST-PARTY PATHS ONLY (tests/hygiene/first_party.sh): src, tests, app and irisgl WITHOUT its submodules (the
# vendored Ogre fork and its install are where these functions are defined).
. "$(dirname "$0")/first_party.sh"
mapfile -t OWN < <(first_party_paths irisgl | grep -v '^irisgl/engine$'; find irisgl/engine -mindepth 1 -maxdepth 1 ! -name src)
self="tests/hygiene/no_engine_transform_reads.sh"
sites=$(grep -rnE '_get[A-Za-z]*Updated\b' src tests app "${OWN[@]}" \
            --include=*.cpp --include=*.h --include=*.hpp --include=*.inl --include=*.mm 2>/dev/null \
        | grep -v "^$self:" || true)
if [ -n "$sites" ]; then
    echo "source.no_engine_transform_reads: FAIL — a read of the engine's derived transform outside irisgl/engine/src:"
    echo "$sites" | sed 's/^/    /'
    echo "    The document's world transforms are its own store's: SceneNode::getGlobal* / iris::graph::world"
    echo "    (irisgl/document/scenegraph/nodegraph.h)."
    exit 1
fi
echo "source.no_engine_transform_reads: ok — no _get…Updated read outside irisgl/engine/src"
exit 0
