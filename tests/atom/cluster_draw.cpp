// See cluster_draw.h.
#include "cluster_draw.h"
#include "EnginePrivate.h"

#include <OgreHlmsCompute.h>
#include <OgreHlmsComputeJob.h>
#include <OgreHlmsManager.h>
#include <OgreMesh2.h>
#include <OgreRenderSystem.h>
#include <OgreResourceGroupManager.h>
#include <OgreResourceTransition.h>
#include <OgreRoot.h>
#include <OgreSubMesh2.h>
#include <Vao/OgreAsyncTicket.h>
#include <Vao/OgreIndexBufferPacked.h>
#include <Vao/OgreUavBufferPacked.h>
#include <Vao/OgreVaoManager.h>
#include <Vao/OgreVertexArrayObject.h>

#include <algorithm>
#include <cstring>

namespace clusterdraw {

using jahshaka::engine::detail::OgreScene;

namespace {
Ogre::VaoManager *vaoManager()
{
    return Ogre::Root::getSingleton().getRenderSystem()->getVaoManager();
}
}   // namespace

// ---------------------------------------------------------------------------
bool ClusterDraw::create(Engine *engine, Scene *scene, const MeshData &src, MaterialId material,
                         bool reversed, std::string &err)
{
    destroy();
    if (src.clusters.empty() || src.clusterIndices.empty()) { err = "the mesh has no DAG"; return false; }
    mEngine = engine;
    mScene = scene;
    mReversed = reversed;
    mStream = src.clusterIndices;
    mClusters = src.clusters;

    // The engine mesh: the source's vertices with the WHOLE cluster stream as its
    // one index list — every vertex a cluster names is in it, and the level-0
    // VAO it gets is the one this harness swaps out.
    MeshData d;
    d.positions = src.positions;
    d.normals = src.normals;
    d.uvs = src.uvs;
    d.tangents = src.tangents;
    d.indices = mStream;
    if (reversed)
        for (size_t t = 0; t + 2 < d.indices.size(); t += 3) std::swap(d.indices[t + 1], d.indices[t + 2]);
    mMesh = scene->createMesh(d);
    if (!mMesh) { err = "createMesh: " + engine->lastError(); return false; }

    OgreScene::ClusterStreamView view;
    if (!static_cast<OgreScene *>(scene)->clusterStreamOf(mMesh, view) || !view.mesh ||
        view.mesh->getNumSubMeshes() == 0) {
        err = "the harness mesh has no Ogre mesh";
        return false;
    }
    Ogre::SubMesh *sub = view.mesh->getSubMesh(0);
    if (sub->mVao[Ogre::VpNormal].empty() || sub->mVao[Ogre::VpShadow].empty()) {
        err = "the harness mesh has no VAO";
        return false;
    }
    Ogre::VaoManager *vm = vaoManager();
    mOriginalNormal = sub->mVao[Ogre::VpNormal][0];
    mOriginalShadow = sub->mVao[Ogre::VpShadow][0];
    // THE REWRITTEN BUFFER: BT_DEFAULT (upload() goes through a staging buffer,
    // synchronised) and 32-bit, sized for the whole stream — the most any cut
    // can draw is every leaf, and every cut draws at most as many triangles as
    // the stream holds.
    mIndices = vm->createIndexBuffer(Ogre::IndexBufferPacked::IT_32BIT, Ogre::uint32(mStream.size()),
                                     Ogre::BT_DEFAULT, nullptr, false);
    mVao = vm->createVertexArrayObject(mOriginalNormal->getVertexBuffers(), mIndices,
                                       Ogre::OT_TRIANGLE_LIST);
    // BOTH lists: a shadow pass must see the same cut the colour pass draws, or
    // the object would shadow itself with the whole overlapping DAG.
    sub->mVao[Ogre::VpNormal][0] = mVao;
    sub->mVao[Ogre::VpShadow][0] = mVao;

    mNode = scene->createNode();
    if (!mNode || !scene->attachMesh(mNode, mMesh, material)) {
        err = "attachMesh: " + engine->lastError();
        return false;
    }
    // Level 0: every leaf cluster.
    std::vector<unsigned> leaves;
    for (size_t c = 0; c < mClusters.size(); ++c)
        if (mClusters[c].refined < 0) leaves.push_back(unsigned(c));
    setCut(leaves);
    return true;
}

size_t ClusterDraw::setCut(const std::vector<unsigned> &clusterIds)
{
    if (!mVao || !mIndices) return 0;
    mScratch.clear();
    for (unsigned c : clusterIds) {
        if (c >= mClusters.size()) continue;
        const MeshCluster &cl = mClusters[c];
        mScratch.insert(mScratch.end(), mStream.begin() + cl.firstIndex,
                        mStream.begin() + cl.firstIndex + cl.indexCount);
    }
    if (mReversed)
        for (size_t t = 0; t + 2 < mScratch.size(); t += 3) std::swap(mScratch[t + 1], mScratch[t + 2]);
    if (!mScratch.empty()) mIndices->upload(mScratch.data(), 0, mScratch.size());
    mVao->setPrimitiveRange(0, mScratch.size());
    return mScratch.size() / 3;
}

void ClusterDraw::destroy()
{
    if (!mScene) return;
    if (mNode) mScene->removeNode(mNode);
    mNode = 0;
    OgreScene::ClusterStreamView view;
    if (mMesh && static_cast<OgreScene *>(mScene)->clusterStreamOf(mMesh, view) && view.mesh &&
        view.mesh->getNumSubMeshes() > 0) {
        Ogre::SubMesh *sub = view.mesh->getSubMesh(0);
        if (mOriginalNormal && !sub->mVao[Ogre::VpNormal].empty()) sub->mVao[Ogre::VpNormal][0] = mOriginalNormal;
        if (mOriginalShadow && !sub->mVao[Ogre::VpShadow].empty()) sub->mVao[Ogre::VpShadow][0] = mOriginalShadow;
    }
    Ogre::VaoManager *vm = vaoManager();
    if (mVao) vm->destroyVertexArrayObject(mVao);
    if (mIndices) vm->destroyIndexBuffer(mIndices);
    mVao = nullptr;
    mIndices = nullptr;
    if (mMesh) mScene->destroyMesh(mMesh);
    mMesh = 0;
    mOriginalNormal = mOriginalShadow = nullptr;
    mScene = nullptr;
}

// ---------------------------------------------------------------------------
namespace {

struct ParityView
{
    float row[3][4];
    float eyeScale[4];
    float lod[4];
};
struct GpuGroup { float sphere[4]; float error[4]; };
struct GpuCluster { unsigned range[4]; float sphere[4]; };

Ogre::DescriptorSetUav::BufferSlot slotOf(Ogre::UavBufferPacked *b, Ogre::ResourceAccess::ResourceAccess a)
{
    Ogre::DescriptorSetUav::BufferSlot s = Ogre::DescriptorSetUav::BufferSlot::makeEmpty();
    s.buffer = b;
    s.offset = 0;
    s.sizeBytes = 0;
    s.access = a;
    return s;
}

}   // namespace

bool GpuCut::init(Engine *, std::string &err)
{
    Ogre::HlmsCompute *hc = Ogre::Root::getSingleton().getHlmsManager()->getComputeHlms();
    if (!hc) { err = "no compute Hlms"; return false; }
    const Ogre::IdString name("Test/ClusterCutParity");
    mJob = hc->findComputeJobNoThrow(name);
    if (mJob) return true;
    Ogre::ResourceGroupManager::getSingleton().addResourceLocation(
        CLUSTER_TEST_MEDIA_DIR, "FileSystem", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
    mJob = hc->createComputeJob(name, "Test/ClusterCutParity", "JahClusterCutParity_cs",
                                { "CrossPlatformSettings_piece_all", "JahClusterCut.glsl" });
    if (!mJob) { err = "createComputeJob"; return false; }
    mJob->setThreadsPerGroup(64u, 1u, 1u);
    mJob->setNumUavUnits(5u);
    return true;
}

bool GpuCut::run(const std::vector<MeshClusterGroup> &groups, const std::vector<MeshCluster> &clusters,
                 const std::vector<ClusterCutView> &views, std::vector<unsigned char> &drawn,
                 std::string &err)
{
    drawn.clear();
    if (!mJob) { err = "GpuCut not initialised"; return false; }
    if (groups.empty() || clusters.empty() || views.empty()) { err = "empty input"; return false; }
    Ogre::VaoManager *vm = vaoManager();
    Ogre::RenderSystem *rs = Ogre::Root::getSingleton().getRenderSystem();
    Ogre::HlmsCompute *hc = Ogre::Root::getSingleton().getHlmsManager()->getComputeHlms();

    std::vector<GpuGroup> g(groups.size());
    for (size_t i = 0; i < groups.size(); ++i) {
        const MeshClusterGroup &s = groups[i];
        g[i] = GpuGroup{ { s.centre[0], s.centre[1], s.centre[2], s.radius },
                         { s.error, s.estimate, float(s.depth), 0.0f } };
    }
    std::vector<GpuCluster> c(clusters.size());
    for (size_t i = 0; i < clusters.size(); ++i) {
        const MeshCluster &s = clusters[i];
        c[i] = GpuCluster{ { s.firstIndex, s.indexCount, unsigned(s.group),
                             s.refined < 0 ? 0xFFFFFFFFu : unsigned(s.refined) },
                           { s.centre[0], s.centre[1], s.centre[2], s.radius } };
    }
    std::vector<ParityView> v(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        const ClusterCutView &s = views[i];
        ParityView p;
        std::memcpy(p.row, s.worldRow, sizeof(p.row));
        p.eyeScale[0] = s.eye[0]; p.eyeScale[1] = s.eye[1]; p.eyeScale[2] = s.eye[2];
        p.eyeScale[3] = s.scale;
        p.lod[0] = s.tolerance; p.lod[1] = s.projScaleY; p.lod[2] = s.viewportHeight; p.lod[3] = 0.0f;
        v[i] = p;
    }
    const unsigned total = unsigned(clusters.size() * views.size());
    const unsigned counts[4] = { unsigned(clusters.size()), unsigned(views.size()), 0u, 0u };
    std::vector<unsigned> zeros(total, 0u);

    Ogre::UavBufferPacked *bCount = vm->createUavBuffer(1u, sizeof(counts), 0, const_cast<unsigned *>(counts), false);
    Ogre::UavBufferPacked *bViews = vm->createUavBuffer(v.size(), sizeof(ParityView), 0, v.data(), false);
    Ogre::UavBufferPacked *bGroups = vm->createUavBuffer(g.size(), sizeof(GpuGroup), 0, g.data(), false);
    Ogre::UavBufferPacked *bClusters = vm->createUavBuffer(c.size(), sizeof(GpuCluster), 0, c.data(), false);
    Ogre::UavBufferPacked *bOut = vm->createUavBuffer(total, sizeof(unsigned), 0, zeros.data(), false);

    bool ok = true;
    try {
        mJob->_setUavBuffer(0u, slotOf(bCount, Ogre::ResourceAccess::Read));
        mJob->_setUavBuffer(1u, slotOf(bViews, Ogre::ResourceAccess::Read));
        mJob->_setUavBuffer(2u, slotOf(bGroups, Ogre::ResourceAccess::Read));
        mJob->_setUavBuffer(3u, slotOf(bClusters, Ogre::ResourceAccess::Read));
        mJob->_setUavBuffer(4u, slotOf(bOut, Ogre::ResourceAccess::Write));
        mJob->setNumThreadGroups((total + 63u) / 64u, 1u, 1u);
        Ogre::ResourceTransitionArray &rt = rs->getBarrierSolver().getNewResourceTransitionsArrayTmp();
        mJob->analyzeBarriers(rt);
        rs->executeResourceTransition(rt);
        hc->dispatch(mJob, 0, 0);
        Ogre::AsyncTicketPtr ticket = bOut->readRequest(0, total);
        const unsigned *res = reinterpret_cast<const unsigned *>(ticket->map());
        drawn.resize(total);
        for (unsigned i = 0; i < total; ++i) drawn[i] = res[i] ? 1u : 0u;
        ticket->unmap();
    } catch (Ogre::Exception &e) {
        err = e.getFullDescription();
        ok = false;
    }
    const Ogre::DescriptorSetUav::BufferSlot empty = Ogre::DescriptorSetUav::BufferSlot::makeEmpty();
    for (uint8_t i = 0; i < 5u; ++i) mJob->_setUavBuffer(i, empty);
    vm->destroyUavBuffer(bOut);
    vm->destroyUavBuffer(bClusters);
    vm->destroyUavBuffer(bGroups);
    vm->destroyUavBuffer(bViews);
    vm->destroyUavBuffer(bCount);
    return ok;
}

// ---------------------------------------------------------------------------
bool readClusterStream(Scene *scene, MeshId mesh, std::vector<unsigned> &out, std::string &err)
{
    out.clear();
    OgreScene::ClusterStreamView view;
    if (!static_cast<OgreScene *>(scene)->clusterStreamOf(mesh, view)) { err = "unknown mesh"; return false; }
    if (!view.stream) { err = "the mesh has no cluster stream"; return false; }
    const size_t n = view.stream->getNumElements();
    Ogre::AsyncTicketPtr ticket = view.stream->readRequest(0, n);
    const void *data = ticket->map();
    out.resize(n);
    if (view.stream->getIndexType() == Ogre::IndexBufferPacked::IT_16BIT) {
        const Ogre::uint16 *s = reinterpret_cast<const Ogre::uint16 *>(data);
        for (size_t i = 0; i < n; ++i) out[i] = s[i];
    } else {
        std::memcpy(out.data(), data, n * sizeof(unsigned));
    }
    ticket->unmap();
    return true;
}

}   // namespace clusterdraw
