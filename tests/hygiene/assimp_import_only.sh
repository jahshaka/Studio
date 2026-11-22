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
#   irisgl/import/                          the import library itself (loadAsSceneFragment: import/scenebuilder.h)
#   src/services/import/                    the importers (sniff/validate/convert)
#   src/services/assethelper.cpp            the importers' parse helper
#   src/services/animationfile.cpp          the clip importer's reader (animfile::read)
#   src/services/meshbakestore.cpp          bake BUILDS: a stale/absent bake rebuilt
#                                           from its source on a worker
#   src/ui/dialogs/importsettingsdialog.cpp the import dialog's light pre-read
#   src/lab/labscene.cpp                    jahlab's import (LAB-1): `jahlab --bake-only` parses and BAKES a file into
#                                           the lab's data root; every run reads the bake
# White-box test suites (tests/) are not covered: a suite's fixture parse is
# tests/support/testmesh.h, with no library behind it.
#
# AND THE DOCUMENT INCLUDES NEITHER (NOQT-DOCUMENT-1 stage 1): no `#include` of an
# assimp header or of import/ under irisgl/document. The document is what a scene
# IS; what BUILDS one from a parse is the import's (import/scenebuilder.h), and a
# document that includes the importer cannot be built without it (or without Qt).
#
# $1 = the repo root
set -u

ROOT="${1:?usage: assimp_import_only.sh <source-root>}"
cd "$ROOT" || { echo "source.assimp_import_only: no such root $ROOT"; exit 1; }

# THE PARSE ENTRY POINTS — every public IrisGL function that reaches
# Assimp::Importer, plus the Studio wrappers the importers call.
ENTRY='GraphicsHelper::load|ModelSceneInfo::(read|fromSource)\b|ClipFileInfo::read[[:space:]]*\(|ModelPreRead::read|readDeclaredUnitScale[[:space:]]*\(|loadAsSceneFragment[[:space:]]*\(|MeshBake::build(Clip)?From(File|Scene)[[:space:]]*\(|readSceneFile[[:space:]]*\(|animfile::(read|isAnimationFile)[[:space:]]*\(|extractTexturesAndMaterialFromMesh[[:space:]]*\(|iris::SceneSource\b|Assimp::Importer'

ALLOWED='^(irisgl/import/|src/services/import/|src/services/assethelper\.(cpp|h):|src/services/animationfile\.(cpp|h):|src/services/meshbakestore\.cpp:|src/ui/dialogs/importsettingsdialog\.cpp:|src/lab/labscene\.cpp:)'

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

# THE DOCUMENT's INCLUDES: an assimp header or an import/ header (either spelling).
doc_inc=$(grep -rnE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](assimp/|import/|irisgl/import/)' \
              irisgl/document --include='*.cpp' --include='*.h' 2>/dev/null || true)
if [ -n "$doc_inc" ]; then
    echo "source.assimp_import_only: FAIL — the document includes assimp or the import layer"
    echo "$doc_inc" | sed 's/^/    /'
    echo "    What builds document objects from a parse lives in irisgl/import (import/scenebuilder.h)."
    exit 1
fi
# The control: the same scan over irisgl/import, where such includes ARE the code - a scan that finds none there
# reads nothing (a moved tree, a broken pattern) and proves nothing here.
imp_inc=$(grep -rlE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"](assimp/|import/|irisgl/import/)' \
              irisgl/import --include='*.cpp' --include='*.h' 2>/dev/null | wc -l)
if [ "$imp_inc" -eq 0 ]; then
    echo "source.assimp_import_only: FAIL — the include scan found no assimp/import include even in irisgl/import (a broken scan)"
    exit 1
fi
echo "source.assimp_import_only: ok — irisgl/document includes no assimp or import/ header ($imp_inc irisgl/import files do, the scan's control)"

# The sites that ARE allowed, listed — the row's own evidence.
echo "source.assimp_import_only: ok — the parse entry points are reached only from:"
grep -rnE "$ENTRY" src irisgl --include='*.cpp' --include='*.h' --exclude-dir=thirdparty 2>/dev/null \
    | grep -vE '^[^:]+:[0-9]+:[[:space:]]*(//|\*|/\*)' \
    | cut -d: -f1 | sort | uniq -c | sed 's/^/    /'
exit 0
