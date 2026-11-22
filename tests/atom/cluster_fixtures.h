// ATOM stage 2 — THE CLUSTER DAG'S TEST FIXTURES (lane ATOM-CLUSTER-1; a TEST
// TOOL, compiled into the suites that name it and into nothing else).
//
// Document side only: the product's primitives read through assimp exactly as an
// import reads them, the harness meshes (tests/support/clustermesh.h) built as
// iris::Mesh arrays, and every one of them BAKED through the product's own
// entry points (MeshBake::buildClusterDag + buildLevelTable) and handed across through the product's own
// SceneMirror::toMeshData — so what a suite measures is what an import produces
// and what the engine receives, not a second implementation of either.
#pragma once

#include "irisgl/document/assets/mesh.h"
#include "irisgl/import/meshbake.h"
#include "jahshaka/engine/Mesh.h"
#include "../support/proceduralshell.h"

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

/// The same from a harness mesh's plain arrays (tests/support/clustermesh.h, proceduralshell.h).
iris::MeshPtr meshFromShell(const enginetest::ShellMesh &m);

/// Every triangle mesh of a model file, parsed with the import's own flags (the product's primitives).
std::vector<iris::MeshPtr> loadModel(const std::string &path);

/// Bake `mesh` (chain + DAG) and hand it across. False when the hand-off produced nothing. `options`: what the
/// builder records into f.stats (wantRegions: the level-0 provenance per cluster; wantGroups: one record per group).
bool bake(Fixture &f, iris::MeshPtr mesh, const std::string &name,
          const iris::MeshBake::ClusterDagOptions &options = iris::MeshBake::ClusterDagOptions());

}   // namespace clusterfix
