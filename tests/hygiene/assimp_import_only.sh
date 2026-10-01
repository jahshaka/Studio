#!/usr/bin/env bash
#
# source.assimp_import_only — assimp is reached ONLY from the import pipeline
# (SHIPPED-BAKES-1, the owner's forward-only rule).
#
# WHY. assimp is an IMPORT-TIME dependency: an import parses a file once and
# BAKES it — a model's mesh bake (with the facts of its parse beside it), an
# animation clip's clip bake — and every reader after that reads the bake. The
# app used to keep parsing shipped content at run time: the preview docks'
# furniture, the VR controllers, the avatar character, every clip door, the
# thumbnail strip and the metadata backfill. Each was a slow parse (often on
# the UI thread) and a second geometry path beside the bake. They are gone;
# this row keeps them gone, because a parse call is one line and looks
# harmless.
#
# THE RULE: a call to one of IrisGL's PARSE ENTRY POINTS (below) may appear
# only in
#   irisgl/import/                          the import library itself
#   irisgl/document/scenegraph/meshnode.cpp defines loadAsSceneFragment
#   src/services/import/                    the importers (sniff/validate/convert)
#   src/services/assethelper.cpp            the importers' parse helper
#   src/services/animationfile.cpp          the clip importer's reader (animfile::read)
#   src/services/meshbakestore.cpp          bake BUILDS: a stale/absent bake rebuilt
#                                           from its source on a worker
#   src/ui/dialogs/importsettingsdialog.cpp the import dialog's light pre-read
# White-box test suites (tests/) are not covered: a suite's fixture parse is
# tests/support/testmesh.h, with no library behind it.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: assimp_import_only.sh <source-root>}"
cd "$ROOT" || { echo "source.assimp_import_only: no such root $ROOT"; exit 1; }

# THE PARSE ENTRY POINTS — every public IrisGL function that reaches
# Assimp::Importer, plus the Studio wrappers the importers call.
ENTRY='GraphicsHelper::load|ModelSceneInfo::(read|fromSource)\b|ClipFileInfo::read[[:space:]]*\(|ModelPreRead::read|readDeclaredUnitScale[[:space:]]*\(|loadAsSceneFragment[[:space:]]*\(|MeshBake::build(Clip)?From(File|Scene)[[:space:]]*\(|readSceneFile[[:space:]]*\(|animfile::(read|isAnimationFile)[[:space:]]*\(|extractTexturesAndMaterialFromMesh[[:space:]]*\(|iris::SceneSource\b|Assimp::Importer'

ALLOWED='^(irisgl/import/|irisgl/document/scenegraph/meshnode\.(cpp|h):|src/services/import/|src/services/assethelper\.(cpp|h):|src/services/animationfile\.(cpp|h):|src/services/meshbakestore\.cpp:|src/ui/dialogs/importsettingsdialog\.cpp:)'

# A CALL, not a mention: comments name these all the time. A line whose first
# non-blank characters open a comment is prose.
hits=$(grep -rnE "$ENTRY" src irisgl --include='*.cpp' --include='*.h' \
           --exclude-dir=thirdparty 2>/dev/null \
       | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*|/\*)' \
       | grep -vE "$ALLOWED" || true)

if [ -n "$hits" ]; then
    echo "source.assimp_import_only: FAIL — a parse outside the import pipeline"
    echo "$hits" | sed 's/^/    /'
    echo "    Read the BAKE instead: MeshBakeStore::load / ensureFresh (a model),"
    echo "    MeshBakeStore::loadClip / ensureClip (a clip), iris::ShippedMeshes::mesh"
    echo "    (a shipped mesh, by its seed key in src/data/primitives.h)."
    exit 1
fi

# The sites that ARE allowed, listed — the row's own evidence.
echo "source.assimp_import_only: ok — the parse entry points are reached only from:"
grep -rnE "$ENTRY" src irisgl --include='*.cpp' --include='*.h' --exclude-dir=thirdparty 2>/dev/null \
    | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*|/\*)' \
    | cut -d: -f1 | sort | uniq -c | sed 's/^/    /'
exit 0
