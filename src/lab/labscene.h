// jahlab — THE SCENE (LAB-1 stage 1): a model file through the REAL import + bake path into an iris::Scene, with the
// lab's own bake cache, and the Basic template's world around it.
//
// THE IMPORT CHAIN is the app's, call for call (src/services/import/assetimporters.cpp:186-296): SceneSource::read
// under the default ImportSettings' transform, MeshBake::buildFromScene, MeshBake::write; the fragment is
// MeshBake::buildFragment with irisgl's one material conversion (iris::materialFromMeshData). THE CACHE KEY IS THE
// BAKE'S OWN FINGERPRINT — fingerprintFor(oid, settings) = sha256(producerId | oid | settings), producerId = format |
// producer hash | assimp | flags, oid = the file's sha256 (the store's oid, re-created: six lines) — and the file is
// `<data-root>/bakes/<fileNameFor(oid, settings)>`. A second open whose header matches skips the parse and the bake
// (the row's law); a bake of another producer is rebuilt in place (write is atomic).
//
// THE FILE'S LIGHTS (irisgl/import/scenelights.h) are adopted from the PARSE, which a cached open does not have: the
// bake writes them beside itself (`<bake>.lights.json`, the parent's index path in the fragment + the light's fields)
// and EVERY open — the first included — builds the fragment from the bake and re-attaches them from that file, so the
// first and the second open are one document by construction.
#pragma once

#include <QString>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"

#include <string>

namespace lab {

/// What one model load did and cost (wall ms, for the report — never consulted by a decision).
struct LoadReport
{
    bool ok = false;
    QString error;
    bool baked = false;            ///< true: parsed and baked now; false: the cache hit
    bool parsed = false;           ///< true: the file was parsed (a bake, or a valid bake whose sidecar was missing)
    QString bakePath;
    double hashMs = 0, parseMs = 0, bakeMs = 0, readMs = 0, fragmentMs = 0;
    int meshes = 0, lights = 0;
    unsigned long long triangles = 0;   ///< level-0 triangles of every mesh of the fragment
    int texturesResolved = 0, texturesMissing = 0, textureNameCollisions = 0;   ///< the maps' names (TextureIndex)
    double texturesMs = 0.0;       ///< the maps' name resolution (TextureIndex)
    int twoSidedNodes = 0;   ///< nodes given FaceCullingMode::None from their material's twoSided flag
};

/// Loads `path` (FBX / OBJ / glTF / GLB...) through the bake cache under `dataRoot`. Null + report.error on failure.
iris::SceneNodePtr loadModel(const QString &path, const QString &dataRoot, LoadReport &report);

/// The Basic template's world (src/services/scenetemplatebuilder.cpp, its document half): the World Mode Epic, the
/// realistic sky with the height fog, the 100 x 1 x 100 m floor (the shipped cube, through the same bake, top at y = 0,
/// the tile, casting no shadow), the Sky Light — and the default sun only when `wantSun` (the file's own directional
/// is the sun otherwise: the app's one-sun rule). `binDir` resolves app/content beside the binary.
bool addBasicWorld(const iris::ScenePtr &doc, const QString &binDir, const QString &dataRoot, bool wantSun,
                   QString &error);

/// True when `node`'s subtree holds a directional light.
bool hasDirectional(const iris::SceneNodePtr &node);
/// The scene's mesh bounds in world space (min, max); false when it holds no mesh.
bool worldBounds(const iris::ScenePtr &doc, iris::Vec3 &mn, iris::Vec3 &mx);

/// A .jlab: the session as JSON (LAB-1: {file, camera, tier, views, sun}).
struct Jlab
{
    QString file;                 ///< the model, absolute
    QString env;                  ///< a generated environment's name (stage 2), instead of a file
    bool hasCamera = false;
    iris::Vec3 camPos, camTarget;
    QString tier = QStringLiteral("epic");
    QString view = QStringLiteral("off");
    bool gi = true, occlusion = true, draw = true, overlay = false;
    bool hasSun = false;
    iris::Quat sunRot;
    float sunIntensity = 1.0f;
};
bool readJlab(const QString &path, Jlab &out, QString &error);
bool writeJlab(const QString &path, const Jlab &j, QString &error);

}   // namespace lab
