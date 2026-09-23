// ATOM stage 2 — THE CLUSTER CUT'S TEST HARNESS (lane ATOM-CLUSTER-1;
// SPECS/atom/B2_CLUSTER_DAG_DESIGN.md §3). A TEST TOOL: it lives in the test
// executables that compile it and in nothing of the product.
//
// THE LEAD'S SHAPING DECISION is why this is a harness and not a draw path: the
// product cut is stage 3's, on the GPU; stage 2 proves the DAG and the rule are
// crack-free and measures what they buy, and that proof needs pixels. So the
// harness reaches past the public Engine API (EnginePrivate.h, as
// tests/shadow's spike does) for exactly two things:
//
//   ClusterDraw — ROUTE B2 (NANITE_SPEC route F): a mesh whose drawn VAO is
//       swapped for one over the SAME vertex buffer and a BT_DEFAULT index buffer
//       the harness REWRITES each time the cut changes (the selected clusters'
//       triangles, concatenated), with `VertexArrayObject::setPrimitiveRange`
//       drawing exactly that many. No shader change, no fork change. Built
//       through the engine's own createMesh/attachMesh, so it is lit, shadowed
//       and graded like any object.
//   GpuCut — the GLSL half of the rule (the product piece JahClusterCut.glsl)
//       run on the device by a TEST job (tests/atom/media) over a DAG's tables.
#pragma once

#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Types.h"

#include <string>
#include <vector>

namespace Ogre { class HlmsComputeJob; class IndexBufferPacked; class VertexArrayObject; }

namespace clusterdraw {

using namespace jahshaka::engine;

class ClusterDraw
{
public:
    /// Builds the drawable from `src` (its vertices and its CLUSTER STREAM) in
    /// `scene`, attached to a new node with `material`. `reversed` winds every
    /// triangle the other way — the crack detector's INSIDE copy: its front
    /// faces are the object's back faces, so it shows only where a ray has
    /// passed THROUGH the surface. The cut starts at level 0 (every leaf).
    bool create(Engine *engine, Scene *scene, const MeshData &src, MaterialId material,
                bool reversed, std::string &err);
    /// Uploads the triangles of `clusterIds` (indices into src.clusters) and
    /// draws exactly those. Returns the triangle count.
    size_t setCut(const std::vector<unsigned> &clusterIds);
    /// Detaches, restores the mesh's own VAOs and frees the harness's buffers.
    void destroy();
    ~ClusterDraw() { destroy(); }

    NodeId node() const { return mNode; }
    MeshId mesh() const { return mMesh; }

private:
    Engine *mEngine = nullptr;
    Scene *mScene = nullptr;
    MeshId mMesh = 0;
    NodeId mNode = 0;
    bool mReversed = false;
    std::vector<unsigned> mStream;               // the cluster stream (src.clusterIndices)
    std::vector<MeshCluster> mClusters;
    Ogre::IndexBufferPacked *mIndices = nullptr;
    Ogre::VertexArrayObject *mVao = nullptr;
    Ogre::VertexArrayObject *mOriginalNormal = nullptr;
    Ogre::VertexArrayObject *mOriginalShadow = nullptr;
    std::vector<unsigned> mScratch;
};

/// The GLSL half of the rule, on the device.
class GpuCut
{
public:
    bool init(Engine *engine, std::string &err);
    /// `drawn[v * clusters.size() + c]` = 1 where the GLSL rule draws cluster c
    /// for view v.
    bool run(const std::vector<MeshClusterGroup> &groups, const std::vector<MeshCluster> &clusters,
             const std::vector<ClusterCutView> &views, std::vector<unsigned char> &drawn,
             std::string &err);
private:
    Ogre::HlmsComputeJob *mJob = nullptr;
};

/// The engine's uploaded cluster stream for `mesh`, read back from the GPU
/// (widened to 32 bits). False when the mesh has none.
bool readClusterStream(Scene *scene, MeshId mesh, std::vector<unsigned> &out, std::string &err);

}   // namespace clusterdraw
