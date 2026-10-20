// ATOM stage 2 — THE CLUSTER DAG'S TEST FIXTURES (lane ATOM-CLUSTER-1; a TEST
// TOOL, compiled into the suites that name it and into nothing else).
//
// Document side only: the shipped meshes read through assimp exactly as an
// import reads them, procedural fixtures built as iris::Mesh arrays, and every
// one of them BAKED through the product's own entry points (MeshBake::
// buildLodChain + buildClusterDag) and handed across through the product's own
// SceneMirror::toMeshData — so what a suite measures is what an import produces
// and what the engine receives, not a second implementation of either.
#pragma once

#include "irisgl/document/assets/mesh.h"
#include "irisgl/import/meshbake.h"
#include "jahshaka/engine/Types.h"

#include <string>
#include <vector>

namespace clusterfix {

struct Fixture
{
    std::string name;
    iris::MeshPtr mesh;
    jahshaka::engine::MeshData data;           ///< toMeshData's output: the engine's view
    iris::MeshBake::ClusterDagStats stats;
    float extent = 0.0f;                       ///< largest AABB axis, mesh units
    size_t triangles = 0;                      ///< level 0
};

/// An iris::Mesh from plain arrays (positions xyz, normals xyz, uvs uv — either
/// of the last two may be empty), the way a procedural fixture is born.
iris::MeshPtr meshFromArrays(const std::vector<float> &positions, const std::vector<float> &normals,
                             const std::vector<float> &uvs, const std::vector<unsigned> &indices);

/// Every triangle mesh of a model file, parsed with the import's own flags.
std::vector<iris::MeshPtr> loadModel(const std::string &path);

/// The dolly gate's fixture: a 100 x 100 UV sphere, 20,000 triangles, radius 0.5.
iris::MeshPtr uvSphere(int seg = 100, int ring = 100, float radius = 0.5f);

/// A BAR `length` long along -Z from the origin, `width` square, subdivided into
/// `segments` rings along its length and `around` quads across each face — the
/// "partly near, partly far" mesh (B2 §3c).
iris::MeshPtr bar(float length, float width, int segments, int around);

/// A ROUND bar: a closed cylinder `length` long along -Z from the origin,
/// `radius` round, `segments` rings along its length and `around` quads around —
/// a bar whose simplification has real error around its section (a flat-faced
/// box simplifies losslessly, so every level of its chain is free at any
/// distance). `bump` > 0 displaces the radius by that fraction in a 0.5 m x
/// three-lobe pattern, so the bar has real detail ALONG its length too — the
/// case where every coarser level costs real error everywhere.
iris::MeshPtr roundBar(float length, float radius, int segments, int around, float bump = 0.0f);

/// Bake `mesh` (chain + DAG, `variant`) and hand it across. False when the
/// hand-off produced nothing.
bool bake(Fixture &f, iris::MeshPtr mesh, const std::string &name,
          iris::MeshBake::ClusterDagVariant variant = iris::MeshBake::ClusterDagVariant::Shipped,
          bool wantRegions = false);

/// The shipped content this lane measures: the sixteen primitives (the ones the
/// bake gives a DAG — two leaves of triangles or more) + the two shipped
/// high-poly models extracted at configure time (the Matcaps dragon and the
/// Physics sample's model) + the 20k sphere. `sourceDir` is the Studio root,
/// `fixtureDir` the configure-time extraction directory.
struct Named { std::string name; iris::MeshPtr mesh; };
std::vector<Named> shippedMeshes(const std::string &sourceDir, const std::string &fixtureDir,
                                 bool includeSmall = false);

}   // namespace clusterfix
