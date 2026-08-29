// The Ogre-Next 4.0 backend.
//
// THE ONLY translation unit in Jahshaka that includes Ogre. If Ogre appears in
// any other file, the boundary has been breached.
//
// Verified behaviours this file depends on (spikes/qt-ogre-next/FINDINGS.md and
// spikes/headless-vulkan/README.md):
//   * Vulkan supports MULTIPLE on-screen windows; GL3Plus does not (single mGlobalVao).
//   * v1 meshes render NOTHING on Vulkan — geometry is built as v2 buffers directly.
//   * Hlms shader templates are required at runtime, not optional sample data.
//   * A render target must exist BEFORE Hlms registration or any SceneManager;
//     for a purely offscreen engine a surfaceless "null" window satisfies this.
//   * Teardown order is load-bearing: workspaces -> scenes -> our MeshPtrs ->
//     MeshManager::removeAll -> Root. A MeshPtr outliving Root hits a dead VaoManager.
//   * No Ogre exception may escape: every virtual is wrapped and translated.
#include "jahshaka/engine/Engine.h"

#include <OgreRoot.h>
#include <OgreAbiUtils.h>
#include <OgreWindow.h>
#include <OgreCamera.h>
#include <OgreSceneManager.h>
#include <OgreItem.h>
#include <OgreMesh2.h>
#include <OgreMeshManager2.h>
#include <OgreSubMesh2.h>
#include <OgreArchiveManager.h>
#include <OgreHlmsManager.h>
#include <OgreHlmsPbs.h>
#include <OgreHlmsUnlit.h>
#include <OgreHlmsPbsDatablock.h>
#include <OgreHlmsUnlitDatablock.h>
#include <OgreTextureGpuManager.h>
#include <OgreTextureFilters.h>
#include <OgreTextureGpu.h>
#include <OgreAsyncTextureTicket.h>
#include <OgreLogManager.h>
#include <OgreResourceGroupManager.h>
#include <OgreHlmsSamplerblock.h>
#include <set>
#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <Compositor/OgreCompositorNodeDef.h>
#include <Compositor/OgreCompositorShadowNode.h>
#include <Compositor/OgreCompositorShadowNodeDef.h>
#include <OgreRenderSystemCapabilities.h>
#include <Vao/OgreVaoManager.h>
#include <Vao/OgreVertexArrayObject.h>

#include <X11/Xlib.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <vector>

namespace jahshaka { namespace engine {
namespace {

inline Ogre::Vector3     toOgre(const Vec3 &v)   { return Ogre::Vector3(v.x, v.y, v.z); }
inline Ogre::ColourValue toOgre(const Colour &c) { return Ogre::ColourValue(c.r, c.g, c.b, c.a); }

/// Names handed to Ogre must be unique for the life of the process (a destroyed
/// scene may be recreated under the same name while stale resources linger).
std::string processUniqueName(const char *prefix) {
    static std::atomic<unsigned> counter{0};
    return std::string(prefix) + "_" + std::to_string(++counter);
}

// Every backend virtual is wrapped: `JAH_TRY { ... } JAH_CATCH(errSink, failValue)`.
// Ogre throws Ogre::Exception; its own allocations may throw std::bad_alloc.
#define JAH_TRY try
#define JAH_CATCH(sink, ret)                                                          \
    catch (Ogre::Exception &e) { (sink) = e.getFullDescription(); return ret; }       \
    catch (std::exception &e)  { (sink) = std::string("engine: ") + e.what(); return ret; }

class OgreEngine;

// ---------------------------------------------------------------------------
class OgreScene final : public Scene {
public:
    OgreScene(Ogre::Root *root, Ogre::SceneManager *sm, const std::string &name,
              std::string &errorSink)
        : mRoot(root), mSceneMgr(sm), mName(name), mError(errorSink) {}
    ~OgreScene() override { destroy(); }

    const std::string &name() const override { return mName; }

    void setAmbient(const Colour &upper, const Colour &lower) override {
        JAH_TRY {
            mSceneMgr->setAmbientLight(toOgre(upper), toOgre(lower), Ogre::Vector3::UNIT_Y);
        } JAH_CATCH(mError, );
    }

    NodeId addDirectionalLight(const Vec3 &direction, float power) override {
        JAH_TRY {
            Ogre::Light *light = mSceneMgr->createLight();
            Ogre::SceneNode *node = mSceneMgr->getRootSceneNode()->createChildSceneNode();
            node->attachObject(light);
            light->setType(Ogre::Light::LT_DIRECTIONAL);
            light->setPowerScale(power);
            light->setDirection(toOgre(direction).normalisedCopy());
            Node rec; rec.node = node; rec.light = light;
            return track(rec);
        } JAH_CATCH(mError, 0);
    }

    // TEMPORARY — replaced by mesh loading in step 3 of VIEWPORT_MIGRATION_PLAN.md
    NodeId addTestCube(const Colour &albedo, float metalness, float roughness) override {
        JAH_TRY {
            Node rec;
            rec.meshName = processUniqueName("cube");
            rec.mesh = buildCubeV2(rec.meshName);

            auto *hlmsPbs = static_cast<Ogre::HlmsPbs *>(
                mRoot->getHlmsManager()->getHlms(Ogre::HLMS_PBS));
            rec.datablockName = processUniqueName("mat");
            auto *db = static_cast<Ogre::HlmsPbsDatablock *>(hlmsPbs->createDatablock(
                Ogre::IdString(rec.datablockName), rec.datablockName,
                Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec()));
            // Ogre defaults to SpecularWorkflow; Jahshaka's material model is metallic-roughness.
            db->setWorkflow(Ogre::HlmsPbsDatablock::MetallicWorkflow);
            db->setDiffuse(Ogre::Vector3(albedo.r, albedo.g, albedo.b));
            db->setMetalness(metalness);
            db->setRoughness(roughness);

            rec.item = mSceneMgr->createItem(rec.mesh, Ogre::SCENE_DYNAMIC);
            rec.item->setDatablock(db);
            rec.node = mSceneMgr->getRootSceneNode(Ogre::SCENE_DYNAMIC)
                                ->createChildSceneNode(Ogre::SCENE_DYNAMIC);
            rec.node->attachObject(rec.item);
            return track(rec);
        } JAH_CATCH(mError, 0);
    }

    bool removeNode(NodeId id) override {
        auto it = mNodes.find(id);
        if (it == mNodes.end()) return false;
        JAH_TRY {
            releaseNode(it->second);
            mNodes.erase(it);
            return true;
        } JAH_CATCH(mError, false);
    }

    void setNodePosition(NodeId id, const Vec3 &p) override {
        JAH_TRY { if (auto *n = node(id)) n->setPosition(toOgre(p)); } JAH_CATCH(mError, );
    }
    void setNodeScale(NodeId id, const Vec3 &s) override {
        JAH_TRY { if (auto *n = node(id)) n->setScale(toOgre(s)); } JAH_CATCH(mError, );
    }
    void rotateNode(NodeId id, float yaw, float pitch, float roll) override {
        JAH_TRY {
            if (auto *n = node(id)) {
                n->yaw(Ogre::Radian(yaw)); n->pitch(Ogre::Radian(pitch)); n->roll(Ogre::Radian(roll));
            }
        } JAH_CATCH(mError, );
    }

    // ---- Hierarchy and transforms ----
    NodeId createNode(NodeId parent) override {
        JAH_TRY {
            Ogre::SceneNode *p = parent ? node(parent) : nullptr;
            if (parent && !p) { mError = "createNode: unknown parent"; return 0; }
            if (!p) p = mSceneMgr->getRootSceneNode(Ogre::SCENE_DYNAMIC);
            Node rec; rec.node = p->createChildSceneNode(Ogre::SCENE_DYNAMIC);
            return track(rec);
        } JAH_CATCH(mError, 0);
    }
    bool setNodeParent(NodeId id, NodeId parent) override {
        JAH_TRY {
            Ogre::SceneNode *n = node(id);
            if (!n) { mError = "setNodeParent: unknown node"; return false; }
            Ogre::SceneNode *p = parent ? node(parent) : mSceneMgr->getRootSceneNode(Ogre::SCENE_DYNAMIC);
            if (!p) { mError = "setNodeParent: unknown parent"; return false; }
            if (n->getParent() == p) return true;
            if (n->getParent()) n->getParent()->removeChild(n);
            p->addChild(n);
            return true;
        } JAH_CATCH(mError, false);
    }
    void setNodeTransform(NodeId id, const Vec3 &pos, const Quat &rot, const Vec3 &scale) override {
        JAH_TRY {
            if (auto *n = node(id)) {
                n->setPosition(toOgre(pos));
                n->setOrientation(Ogre::Quaternion(rot.w, rot.x, rot.y, rot.z));
                n->setScale(toOgre(scale));
            }
        } JAH_CATCH(mError, );
    }
    void setNodeVisible(NodeId id, bool visible) override {
        JAH_TRY { if (auto *n = node(id)) n->setVisible(visible, true); } JAH_CATCH(mError, );
    }

    // ---- Meshes and materials ----
    MeshId createMesh(const MeshData &data) override {
        if (data.positions.empty() || data.positions.size() % 3 != 0) { mError = "createMesh: positions must be xyz triples"; return 0; }
        if (data.indices.empty() || data.indices.size() % 3 != 0)     { mError = "createMesh: indices must be triangles"; return 0; }
        const size_t nv = data.vertexCount();
        for (unsigned i : data.indices) if (i >= nv) { mError = "createMesh: index out of range"; return 0; }
        if (!data.normals.empty() && data.normals.size() != data.positions.size()) { mError = "createMesh: normals count mismatch"; return 0; }
        if (!data.uvs.empty() && data.uvs.size() != nv * 2) { mError = "createMesh: uv count mismatch"; return 0; }
        JAH_TRY {
            MeshRec rec; rec.name = processUniqueName("mesh");
            rec.mesh = buildMeshV2(rec.name, data);
            mMeshes[++mNextMeshId] = rec;
            return mNextMeshId;
        } JAH_CATCH(mError, 0);
    }
    bool destroyMesh(MeshId id) override {
        auto it = mMeshes.find(id);
        if (it == mMeshes.end()) return false;
        JAH_TRY {
            for (auto &kv : mNodes) if (kv.second.meshRef == id) detachItem(kv.second);
            it->second.mesh.reset();
            Ogre::MeshManager &mm = Ogre::MeshManager::getSingleton();
            if (mm.resourceExists(it->second.name)) mm.remove(it->second.name);
            mMeshes.erase(it);
            return true;
        } JAH_CATCH(mError, false);
    }
    MaterialId createPbrMaterial(const PbrParams &p) override {
        JAH_TRY {
            MaterialRec rec; rec.datablockName = processUniqueName("pbr");
            auto *hlmsPbs = static_cast<Ogre::HlmsPbs *>(mRoot->getHlmsManager()->getHlms(Ogre::HLMS_PBS));
            auto *db = static_cast<Ogre::HlmsPbsDatablock *>(hlmsPbs->createDatablock(
                Ogre::IdString(rec.datablockName), rec.datablockName,
                Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec()));
            db->setWorkflow(Ogre::HlmsPbsDatablock::MetallicWorkflow);
            applyPbr(db, p);
            mMaterials[++mNextMaterialId] = rec;
            return mNextMaterialId;
        } JAH_CATCH(mError, 0);
    }
    bool setPbrMaterial(MaterialId id, const PbrParams &p) override {
        auto it = mMaterials.find(id);
        if (it == mMaterials.end()) return false;
        if (it->second.unlit) { mError = "setPbrMaterial: material is unlit"; return false; }
        JAH_TRY {
            auto *hlmsPbs = mRoot->getHlmsManager()->getHlms(Ogre::HLMS_PBS);
            auto *db = static_cast<Ogre::HlmsPbsDatablock *>(hlmsPbs->getDatablock(Ogre::IdString(it->second.datablockName)));
            if (!db) return false;
            applyPbr(db, p);
            return true;
        } JAH_CATCH(mError, false);
    }
    bool destroyMaterial(MaterialId id) override {
        auto it = mMaterials.find(id);
        if (it == mMaterials.end()) return false;
        JAH_TRY {
            for (auto &kv : mNodes) if (kv.second.materialRef == id) detachItem(kv.second);
            Ogre::Hlms *hlms = hlmsFor(it->second);
            if (hlms->getDatablock(Ogre::IdString(it->second.datablockName)))
                hlms->destroyDatablock(Ogre::IdString(it->second.datablockName));
            mMaterials.erase(it);
            return true;
        } JAH_CATCH(mError, false);
    }
    bool attachMesh(NodeId id, MeshId meshId, MaterialId matId) override {
        auto nit = mNodes.find(id); auto mit = mMeshes.find(meshId); auto tit = mMaterials.find(matId);
        if (nit == mNodes.end()) { mError = "attachMesh: unknown node"; return false; }
        if (mit == mMeshes.end()) { mError = "attachMesh: unknown mesh"; return false; }
        if (tit == mMaterials.end()) { mError = "attachMesh: unknown material"; return false; }
        JAH_TRY {
            Node &n = nit->second;
            detachItem(n);
            n.item = mSceneMgr->createItem(mit->second.mesh, Ogre::SCENE_DYNAMIC);
            n.item->setDatablock(hlmsFor(tit->second)->getDatablock(Ogre::IdString(tit->second.datablockName)));
            // On-top overlays render after everything else (all RQ groups default to
            // v2 FAST mode, so a high group id is enough); depth test is off in the material.
            if (tit->second.onTop) n.item->setRenderQueueGroup(200);
            n.node->attachObject(n.item);
            n.meshRef = meshId; n.materialRef = matId;
            return true;
        } JAH_CATCH(mError, false);
    }
    bool detachMesh(NodeId id) override {
        auto it = mNodes.find(id);
        if (it == mNodes.end()) return false;
        JAH_TRY { detachItem(it->second); return true; } JAH_CATCH(mError, false);
    }

    // ---- Textures ----
    TextureId loadTexture(const std::string &path, bool srgb) override {
        for (auto &kv : mTextures) if (kv.second.path == path) return kv.first;
        const size_t slash = path.find_last_of("/\\");
        const std::string dir  = slash == std::string::npos ? "." : path.substr(0, slash);
        const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
        JAH_TRY {
            Ogre::ResourceGroupManager &rgm = Ogre::ResourceGroupManager::getSingleton();
            static const char *kGroup = "Jahshaka";
            if (!rgm.resourceGroupExists(kGroup)) rgm.createResourceGroup(kGroup, false);
            if (!mTextureDirs.count(dir)) {
                rgm.addResourceLocation(dir, "FileSystem", kGroup, false);
                mTextureDirs.insert(dir);
            }
            if (!rgm.resourceExists(kGroup, file)) { mError = "loadTexture: file not found: " + path; return 0; }
            Ogre::TextureGpuManager *tm = mRoot->getRenderSystem()->getTextureGpuManager();
            Ogre::uint32 flags = Ogre::TextureFlags::AutomaticBatching;
            if (srgb) flags |= Ogre::TextureFlags::PrefersLoadingFromFileAsSRGB;
            // Alias by full path so the same file name in two folders stays distinct.
            Ogre::TextureGpu *tex = tm->createOrRetrieveTexture(file, path, Ogre::GpuPageOutStrategy::Discard,
                                                                flags, Ogre::TextureTypes::Type2D, kGroup,
                                                                Ogre::TextureFilter::TypeGenerateDefaultMipmaps);
            if (!tex) { mError = "loadTexture: could not create texture for " + path; return 0; }
            tex->scheduleTransitionTo(Ogre::GpuResidency::Resident);
            tex->waitForData();
            TextureRec rec; rec.texture = tex; rec.path = path;
            mTextures[++mNextTextureId] = rec;
            return mNextTextureId;
        } JAH_CATCH(mError, 0);
    }
    bool destroyTexture(TextureId id) override {
        auto it = mTextures.find(id);
        if (it == mTextures.end()) return false;
        JAH_TRY {
            Ogre::TextureGpuManager *tm = mRoot->getRenderSystem()->getTextureGpuManager();
            tm->destroyTexture(it->second.texture);
            mTextures.erase(it);
            return true;
        } JAH_CATCH(mError, false);
    }
    bool setPbrTexture(MaterialId mat, PbrTextureSlot slot, TextureId texId) override {
        auto mit = mMaterials.find(mat);
        if (mit == mMaterials.end() || mit->second.unlit) { mError = "setPbrTexture: not a PBR material"; return false; }
        Ogre::TextureGpu *tex = nullptr;
        if (texId) { auto tit = mTextures.find(texId); if (tit == mTextures.end()) { mError = "setPbrTexture: unknown texture"; return false; } tex = tit->second.texture; }
        JAH_TRY {
            auto *db = static_cast<Ogre::HlmsPbsDatablock *>(hlmsFor(mit->second)->getDatablock(Ogre::IdString(mit->second.datablockName)));
            if (!db) return false;
            Ogre::PbsTextureTypes unit = Ogre::PBSM_DIFFUSE;
            switch (slot) {
            case PbrTextureSlot::Albedo:    unit = Ogre::PBSM_DIFFUSE;   break;
            case PbrTextureSlot::Normal:    unit = Ogre::PBSM_NORMAL;    break;
            case PbrTextureSlot::Metalness: unit = Ogre::PBSM_METALLIC;  break;
            case PbrTextureSlot::Roughness: unit = Ogre::PBSM_ROUGHNESS; break;
            case PbrTextureSlot::Emissive:  unit = Ogre::PBSM_EMISSIVE;  break;
            }
            Ogre::HlmsSamplerblock sampler;
            sampler.mU = Ogre::TAM_WRAP; sampler.mV = Ogre::TAM_WRAP;
            sampler.mMaxAnisotropy = 4; sampler.mMipFilter = Ogre::FO_LINEAR;
            db->setTexture(static_cast<Ogre::uint8>(unit), tex, &sampler);
            return true;
        } JAH_CATCH(mError, false);
    }

    // ---- Overlay primitives ----
    MaterialId createUnlitMaterial(const Colour &c, bool depthTest, bool wireframe) override {
        JAH_TRY {
            MaterialRec rec; rec.datablockName = processUniqueName("unlit"); rec.unlit = true; rec.onTop = !depthTest;
            auto *hlmsUnlit = static_cast<Ogre::HlmsUnlit *>(mRoot->getHlmsManager()->getHlms(Ogre::HLMS_UNLIT));
            Ogre::HlmsMacroblock macro;
            macro.mDepthCheck = depthTest;
            macro.mDepthWrite = depthTest;
            macro.mCullMode = Ogre::CULL_NONE;
            if (wireframe) macro.mPolygonMode = Ogre::PM_WIREFRAME;
            Ogre::HlmsBlendblock blend;
            if (c.a < 0.999f) blend.setBlendType(Ogre::SBT_TRANSPARENT_ALPHA);
            auto *db = static_cast<Ogre::HlmsUnlitDatablock *>(hlmsUnlit->createDatablock(
                Ogre::IdString(rec.datablockName), rec.datablockName, macro, blend, Ogre::HlmsParamVec()));
            db->setUseColour(true);
            db->setColour(toOgre(c));
            mMaterials[++mNextMaterialId] = rec;
            return mNextMaterialId;
        } JAH_CATCH(mError, 0);
    }
    bool setUnlitMaterial(MaterialId id, const Colour &c) override {
        auto it = mMaterials.find(id);
        if (it == mMaterials.end() || !it->second.unlit) return false;
        JAH_TRY {
            auto *db = static_cast<Ogre::HlmsUnlitDatablock *>(hlmsFor(it->second)->getDatablock(Ogre::IdString(it->second.datablockName)));
            if (!db) return false;
            db->setColour(toOgre(c));
            return true;
        } JAH_CATCH(mError, false);
    }
    MeshId createLineMesh(const std::vector<Vec3> &points, bool strip) override {
        if (points.size() < 2) { mError = "createLineMesh: need at least 2 points"; return 0; }
        JAH_TRY {
            MeshRec rec; rec.name = processUniqueName("lines");
            const size_t nv = points.size();
            struct V { float px, py, pz, nx, ny, nz, u, v; };
            V *verts = reinterpret_cast<V *>(OGRE_MALLOC_SIMD(sizeof(V) * nv, Ogre::MEMCATEGORY_GEOMETRY));
            Ogre::Vector3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
            for (size_t i = 0; i < nv; ++i) {
                verts[i] = { points[i].x, points[i].y, points[i].z, 0, 1, 0, 0, 0 };
                mn.makeFloor(toOgre(points[i])); mx.makeCeil(toOgre(points[i]));
            }
            Ogre::VaoManager *vaoMgr = mRoot->getRenderSystem()->getVaoManager();
            Ogre::VertexElement2Vec decl;
            decl.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_POSITION));
            decl.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_NORMAL));
            decl.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT2, Ogre::VES_TEXTURE_COORDINATES));
            Ogre::VertexBufferPacked *vbuf = vaoMgr->createVertexBuffer(decl, Ogre::uint32(nv), Ogre::BT_IMMUTABLE, verts, true);
            Ogre::VertexBufferPackedVec vbufs; vbufs.push_back(vbuf);
            Ogre::VertexArrayObject *vao = vaoMgr->createVertexArrayObject(
                vbufs, nullptr, strip ? Ogre::OT_LINE_STRIP : Ogre::OT_LINE_LIST);
            rec.mesh = Ogre::MeshManager::getSingleton().createManual(rec.name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
            Ogre::SubMesh *sub = rec.mesh->createSubMesh();
            sub->mVao[Ogre::VpNormal].push_back(vao);
            sub->mVao[Ogre::VpShadow].push_back(vao);
            const Ogre::Aabb aabb = Ogre::Aabb::newFromExtents(mn, mx);
            rec.mesh->_setBounds(aabb, false);
            rec.mesh->_setBoundingSphereRadius(std::max(aabb.getRadius(), 0.001f));
            mMeshes[++mNextMeshId] = rec;
            return mNextMeshId;
        } JAH_CATCH(mError, 0);
    }

    // ---- Lights ----
    bool setLight(NodeId id, const LightDesc &d) override {
        auto it = mNodes.find(id);
        if (it == mNodes.end()) { mError = "setLight: unknown node"; return false; }
        JAH_TRY {
            Node &n = it->second;
            if (!n.light) { n.light = mSceneMgr->createLight(); n.node->attachObject(n.light); }
            Ogre::Light *L = n.light;
            switch (d.type) {
            case LightType::Directional: L->setType(Ogre::Light::LT_DIRECTIONAL); break;
            case LightType::Point:       L->setType(Ogre::Light::LT_POINT); break;
            case LightType::Spot:        L->setType(Ogre::Light::LT_SPOTLIGHT); break;
            }
            L->setDiffuseColour(toOgre(d.colour));
            L->setSpecularColour(toOgre(d.colour));
            L->setCastShadows(d.castShadows);
            L->setPowerScale(d.intensity * Ogre::Math::PI);
            if (d.type != LightType::Directional)
                L->setAttenuationBasedOnRadius(std::max(d.range, 0.01f), 0.01f);
            if (d.type == LightType::Spot) {
                const float outer = std::max(1.0f, std::min(d.spotAngleDegrees, 179.0f));
                const float inner = outer * (1.0f - std::min(std::max(d.spotSoftness, 0.0f), 0.99f));
                L->setSpotlightRange(Ogre::Degree(inner), Ogre::Degree(outer), 1.0f);
            }
            // Ogre lights shine down their node's -Z once attached (default direction).
            return true;
        } JAH_CATCH(mError, false);
    }
    bool removeLight(NodeId id) override {
        auto it = mNodes.find(id);
        if (it == mNodes.end() || !it->second.light) return false;
        JAH_TRY {
            it->second.light->detachFromParent();
            mSceneMgr->destroyLight(it->second.light);
            it->second.light = nullptr;
            return true;
        } JAH_CATCH(mError, false);
    }

    Ogre::SceneManager *sceneManager() const { return mSceneMgr; }

    /// Releases everything in dependency order. Safe to call twice. Called by
    /// Engine::destroyScene and by the Engine destructor BEFORE Root dies.
    void destroy() {
        if (!mSceneMgr) return;
        JAH_TRY {
            for (auto &kv : mNodes) releaseNode(kv.second);
            mNodes.clear();
            for (auto &kv : mMaterials) {
                Ogre::Hlms *hlms = hlmsFor(kv.second);
                if (hlms->getDatablock(Ogre::IdString(kv.second.datablockName)))
                    hlms->destroyDatablock(Ogre::IdString(kv.second.datablockName));
            }
            mMaterials.clear();
            {
                Ogre::TextureGpuManager *tm = mRoot->getRenderSystem()->getTextureGpuManager();
                for (auto &kv : mTextures) tm->destroyTexture(kv.second.texture);
                mTextures.clear();
            }
            Ogre::MeshManager &mm = Ogre::MeshManager::getSingleton();
            for (auto &kv : mMeshes) {
                kv.second.mesh.reset();
                if (mm.resourceExists(kv.second.name)) mm.remove(kv.second.name);
            }
            mMeshes.clear();
            mRoot->destroySceneManager(mSceneMgr);
        } JAH_CATCH(mError, );
        mSceneMgr = nullptr;
    }

private:
    /// What a node owns. mNodes used to track only the SceneNode, leaking the
    /// Item, Light, mesh and datablock on removal (audit).
    struct Node {
        Ogre::SceneNode *node  = nullptr;
        Ogre::Item      *item  = nullptr;
        Ogre::Light     *light = nullptr;
        Ogre::MeshPtr    mesh;              // uniquely owned (addTestCube); MUST be dropped before Root
        std::string      meshName;
        std::string      datablockName;     // uniquely owned (addTestCube)
        MeshId           meshRef     = 0;   // shared, owned by mMeshes
        MaterialId       materialRef = 0;   // shared, owned by mMaterials
    };
    struct MeshRec { Ogre::MeshPtr mesh; std::string name; };
    struct MaterialRec { std::string datablockName; bool unlit = false; bool onTop = false; };
    struct TextureRec { Ogre::TextureGpu *texture = nullptr; std::string path; };

    Ogre::Hlms *hlmsFor(const MaterialRec &m) const {
        return mRoot->getHlmsManager()->getHlms(m.unlit ? Ogre::HLMS_UNLIT : Ogre::HLMS_PBS);
    }
    /// Removes the renderable from a node that references a SHARED mesh/material.
    void detachItem(Node &n) {
        if (n.item && n.meshRef) {
            n.item->detachFromParent(); mSceneMgr->destroyItem(n.item); n.item = nullptr;
        }
        n.meshRef = 0; n.materialRef = 0;
    }
    static void applyPbr(Ogre::HlmsPbsDatablock *db, const PbrParams &p) {
        db->setDiffuse(Ogre::Vector3(p.albedo.r, p.albedo.g, p.albedo.b));
        db->setMetalness(p.metalness);
        db->setRoughness(p.roughness);
        db->setEmissive(Ogre::Vector3(p.emissive.r, p.emissive.g, p.emissive.b));
    }
    /// Uploads MeshData as a v2 mesh: interleaved position/normal/uv, 16- or 32-bit
    /// indices. v1 meshes silently render nothing on Vulkan, so only this path exists.
    Ogre::MeshPtr buildMeshV2(const std::string &name, const MeshData &data) {
        const size_t nv = data.vertexCount(), ni = data.indices.size();
        std::vector<float> normals = data.normals;
        if (normals.empty()) {
            // Smooth normals from face normals — enough for a lit preview.
            normals.assign(nv * 3, 0.0f);
            for (size_t t = 0; t + 2 < ni; t += 3) {
                const unsigned a = data.indices[t], b = data.indices[t+1], c = data.indices[t+2];
                const float *pa = &data.positions[a*3], *pb = &data.positions[b*3], *pc = &data.positions[c*3];
                const float e1[3] = { pb[0]-pa[0], pb[1]-pa[1], pb[2]-pa[2] };
                const float e2[3] = { pc[0]-pa[0], pc[1]-pa[1], pc[2]-pa[2] };
                const float n[3] = { e1[1]*e2[2]-e1[2]*e2[1], e1[2]*e2[0]-e1[0]*e2[2], e1[0]*e2[1]-e1[1]*e2[0] };
                for (unsigned v : { a, b, c }) for (int k = 0; k < 3; ++k) normals[v*3+k] += n[k];
            }
            for (size_t v = 0; v < nv; ++v) {
                float *n = &normals[v*3];
                const float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
                if (len > 1e-8f) { n[0] /= len; n[1] /= len; n[2] /= len; } else { n[1] = 1.0f; }
            }
        }
        struct V { float px, py, pz, nx, ny, nz, u, v; };
        V *verts = reinterpret_cast<V *>(OGRE_MALLOC_SIMD(sizeof(V) * nv, Ogre::MEMCATEGORY_GEOMETRY));
        Ogre::Vector3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
        for (size_t v = 0; v < nv; ++v) {
            const float *p = &data.positions[v*3];
            const float *n = &normals[v*3];
            verts[v] = { p[0], p[1], p[2], n[0], n[1], n[2],
                         data.uvs.empty() ? 0.0f : data.uvs[v*2], data.uvs.empty() ? 0.0f : data.uvs[v*2+1] };
            mn.makeFloor(Ogre::Vector3(p[0], p[1], p[2])); mx.makeCeil(Ogre::Vector3(p[0], p[1], p[2]));
        }
        Ogre::VaoManager *vaoMgr = mRoot->getRenderSystem()->getVaoManager();
        Ogre::VertexElement2Vec decl;
        decl.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_POSITION));
        decl.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_NORMAL));
        decl.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT2, Ogre::VES_TEXTURE_COORDINATES));
        Ogre::VertexBufferPacked *vbuf = vaoMgr->createVertexBuffer(decl, Ogre::uint32(nv), Ogre::BT_IMMUTABLE, verts, true);
        Ogre::IndexBufferPacked *ibuf = nullptr;
        if (nv <= 65535u) {
            Ogre::uint16 *idx = reinterpret_cast<Ogre::uint16 *>(OGRE_MALLOC_SIMD(sizeof(Ogre::uint16) * ni, Ogre::MEMCATEGORY_GEOMETRY));
            for (size_t i = 0; i < ni; ++i) idx[i] = Ogre::uint16(data.indices[i]);
            ibuf = vaoMgr->createIndexBuffer(Ogre::IndexBufferPacked::IT_16BIT, Ogre::uint32(ni), Ogre::BT_IMMUTABLE, idx, true);
        } else {
            Ogre::uint32 *idx = reinterpret_cast<Ogre::uint32 *>(OGRE_MALLOC_SIMD(sizeof(Ogre::uint32) * ni, Ogre::MEMCATEGORY_GEOMETRY));
            for (size_t i = 0; i < ni; ++i) idx[i] = data.indices[i];
            ibuf = vaoMgr->createIndexBuffer(Ogre::IndexBufferPacked::IT_32BIT, Ogre::uint32(ni), Ogre::BT_IMMUTABLE, idx, true);
        }
        Ogre::MeshPtr mesh = Ogre::MeshManager::getSingleton().createManual(name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
        Ogre::SubMesh *sub = mesh->createSubMesh();
        Ogre::VertexBufferPackedVec vbufs; vbufs.push_back(vbuf);
        Ogre::VertexArrayObject *vao = vaoMgr->createVertexArrayObject(vbufs, ibuf, Ogre::OT_TRIANGLE_LIST);
        sub->mVao[Ogre::VpNormal].push_back(vao);
        sub->mVao[Ogre::VpShadow].push_back(vao);
        const Ogre::Aabb aabb = Ogre::Aabb::newFromExtents(mn, mx);
        mesh->_setBounds(aabb, false);
        mesh->_setBoundingSphereRadius(aabb.getRadius());
        return mesh;
    }
    void releaseNode(Node &n) {
        // Order: renderable off the node -> item (drops the datablock link and one
        // mesh ref) -> datablock -> node -> our mesh ref -> the mesh itself.
        if (n.item)  { n.item->detachFromParent();  mSceneMgr->destroyItem(n.item);   n.item = nullptr; }
        n.meshRef = 0; n.materialRef = 0;
        if (n.node) {   // children survive: re-parent them to the root
            Ogre::SceneNode *root = mSceneMgr->getRootSceneNode(Ogre::SCENE_DYNAMIC);
            while (n.node->numChildren() > 0) {
                Ogre::Node *c = n.node->getChild(0);
                n.node->removeChild(c); root->addChild(c);
            }
        }
        if (n.light) { n.light->detachFromParent(); mSceneMgr->destroyLight(n.light); n.light = nullptr; }
        if (!n.datablockName.empty()) {
            auto *hlmsPbs = mRoot->getHlmsManager()->getHlms(Ogre::HLMS_PBS);
            if (hlmsPbs->getDatablock(Ogre::IdString(n.datablockName)))
                hlmsPbs->destroyDatablock(Ogre::IdString(n.datablockName));
            n.datablockName.clear();
        }
        if (n.node) { mSceneMgr->destroySceneNode(n.node); n.node = nullptr; }
        n.mesh.reset();
        if (!n.meshName.empty()) {
            Ogre::MeshManager &mm = Ogre::MeshManager::getSingleton();
            if (mm.resourceExists(n.meshName)) mm.remove(n.meshName);
            n.meshName.clear();
        }
    }

    Ogre::SceneNode *node(NodeId id) const {
        auto it = mNodes.find(id);
        return it == mNodes.end() ? nullptr : it->second.node;
    }
    /// Ids are monotonic per scene and never reused.
    NodeId track(const Node &n) { mNodes[++mNextId] = n; return mNextId; }

    /// Builds a unit cube as a v2 mesh. v1 meshes silently render nothing on Vulkan,
    /// so the v1 API is avoided entirely — this is also the shape the assimp importer takes.
    Ogre::MeshPtr buildCubeV2(const std::string &name) {
        Ogre::MeshPtr mesh = Ogre::MeshManager::getSingleton().createManual(
            name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
        Ogre::SubMesh *sub = mesh->createSubMesh();
        Ogre::VaoManager *vaoMgr = mRoot->getRenderSystem()->getVaoManager();

        struct V { float px, py, pz, nx, ny, nz; };
        const float h = 0.5f;
        const float fn[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
        const float fv[6][4][3] = {
            {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}},
            {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}},
            {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}},
            {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}},
            {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}},
            {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}} };
        const Ogre::uint32 numVerts = 24, numIdx = 36;

        V *verts = reinterpret_cast<V *>(
            OGRE_MALLOC_SIMD(sizeof(V) * numVerts, Ogre::MEMCATEGORY_GEOMETRY));
        Ogre::uint16 *idx = reinterpret_cast<Ogre::uint16 *>(
            OGRE_MALLOC_SIMD(sizeof(Ogre::uint16) * numIdx, Ogre::MEMCATEGORY_GEOMETRY));
        for (int f = 0; f < 6; ++f)
            for (int v = 0; v < 4; ++v)
                verts[f * 4 + v] = { fv[f][v][0], fv[f][v][1], fv[f][v][2],
                                     fn[f][0], fn[f][1], fn[f][2] };
        for (int f = 0; f < 6; ++f) {
            const Ogre::uint16 b = static_cast<Ogre::uint16>(f * 4);
            const int o = f * 6;
            idx[o+0]=b; idx[o+1]=Ogre::uint16(b+1); idx[o+2]=Ogre::uint16(b+2);
            idx[o+3]=b; idx[o+4]=Ogre::uint16(b+2); idx[o+5]=Ogre::uint16(b+3);
        }

        Ogre::VertexElement2Vec decl;
        decl.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_POSITION));
        decl.push_back(Ogre::VertexElement2(Ogre::VET_FLOAT3, Ogre::VES_NORMAL));
        Ogre::VertexBufferPacked *vbuf =
            vaoMgr->createVertexBuffer(decl, numVerts, Ogre::BT_IMMUTABLE, verts, true);
        Ogre::VertexBufferPackedVec vbufs; vbufs.push_back(vbuf);
        Ogre::IndexBufferPacked *ibuf = vaoMgr->createIndexBuffer(
            Ogre::IndexBufferPacked::IT_16BIT, numIdx, Ogre::BT_IMMUTABLE, idx, true);
        Ogre::VertexArrayObject *vao =
            vaoMgr->createVertexArrayObject(vbufs, ibuf, Ogre::OT_TRIANGLE_LIST);
        sub->mVao[Ogre::VpNormal].push_back(vao);
        sub->mVao[Ogre::VpShadow].push_back(vao);
        mesh->_setBounds(Ogre::Aabb(Ogre::Vector3::ZERO, Ogre::Vector3(h, h, h)), false);
        mesh->_setBoundingSphereRadius(h * 1.733f);
        return mesh;
    }

    Ogre::Root         *mRoot;
    Ogre::SceneManager *mSceneMgr;
    std::string         mName;
    std::string        &mError;
    std::map<NodeId, Node> mNodes;
    std::map<MeshId, MeshRec> mMeshes;
    std::map<MaterialId, MaterialRec> mMaterials;
    std::map<TextureId, TextureRec> mTextures;
    std::set<std::string> mTextureDirs;
    TextureId           mNextTextureId = 0;
    NodeId              mNextId = 0;
    MeshId              mNextMeshId = 0;
    MaterialId          mNextMaterialId = 0;
};

// ---------------------------------------------------------------------------
class OgreView final : public View {
public:
    /// On-screen: `window` is set. Offscreen: `texture` is set. Never both.
    OgreView(Ogre::Root *root, Ogre::Window *window, Ogre::TextureGpu *texture,
             const std::string &name, unsigned w, unsigned h, const Colour &background,
             std::string &errorSink)
        : mRoot(root), mWindow(window), mTexture(texture), mName(name),
          mWidth(w), mHeight(h), mBackground(background), mError(errorSink) {
        mWorkspaceDef = name + "/Workspace";
        mNodeDef = "AutoGen " + Ogre::IdString(mWorkspaceDef + "/Node").getReleaseText();
        mRoot->getCompositorManager2()->createBasicWorkspaceDef(
            mWorkspaceDef, toOgre(background), Ogre::IdString());
    }
    ~OgreView() override { destroy(); }

    const std::string &name() const override { return mName; }
    Scene *scene() const override { return mScene; }

    bool setScene(Scene *scene) override {
        JAH_TRY {
            if (!scene) { detachScene(); return true; }
            if (mScene) {
                mError = "View '" + mName + "' already shows scene '" + mScene->name() +
                         "'; detach it (setScene(nullptr)) before binding another";
                return false;
            }
            auto *s = static_cast<OgreScene *>(scene);
            mCamera = s->sceneManager()->createCamera(mName + "/Camera");
            mCamera->setNearClipDistance(0.1f);
            mCamera->setAutoAspectRatio(true);
            mWorkspace = mRoot->getCompositorManager2()->addWorkspace(
                s->sceneManager(), target(), mCamera, mWorkspaceDef, mEnabled);
            mScene = s;
            return true;
        } JAH_CATCH(mError, false);
    }

    /// Unbinds the scene: workspace and camera go, the scene itself survives.
    void detachScene() {
        JAH_TRY {
            if (mWorkspace) { mRoot->getCompositorManager2()->removeWorkspace(mWorkspace); mWorkspace = nullptr; }
            if (mCamera && mScene && mScene->sceneManager()) mScene->sceneManager()->destroyCamera(mCamera);
            mCamera = nullptr;
            mScene  = nullptr;
        } JAH_CATCH(mError, );
    }

    void setCameraPosition(const Vec3 &p) override {
        JAH_TRY { if (mCamera) mCamera->setPosition(toOgre(p)); } JAH_CATCH(mError, );
    }
    void lookAt(const Vec3 &t) override {
        JAH_TRY { if (mCamera) mCamera->lookAt(toOgre(t)); } JAH_CATCH(mError, );
    }
    void setCamera(const CameraDesc &c) override {
        JAH_TRY {
            if (!mCamera) return;
            mCamera->setPosition(toOgre(c.position));
            mCamera->setOrientation(Ogre::Quaternion(c.orientation.w, c.orientation.x, c.orientation.y, c.orientation.z));
            mCamera->setNearClipDistance(std::max(c.nearClip, 0.001f));
            mCamera->setFarClipDistance(std::max(c.farClip, c.nearClip + 0.01f));
            if (c.orthographic) {
                mCamera->setProjectionType(Ogre::PT_ORTHOGRAPHIC);
                const float aspect = mHeight ? float(mWidth) / float(mHeight) : 1.0f;
                mCamera->setOrthoWindow(c.orthoSize * aspect, c.orthoSize);
            } else {
                mCamera->setProjectionType(Ogre::PT_PERSPECTIVE);
                mCamera->setFOVy(Ogre::Degree(std::max(1.0f, std::min(c.fovDegrees, 179.0f))));
            }
        } JAH_CATCH(mError, );
    }
    void setEnabled(bool on) override {
        mEnabled = on;
        JAH_TRY { if (mWorkspace) mWorkspace->setEnabled(on); } JAH_CATCH(mError, );
    }
    Colour background() const override { return mBackground; }
    bool shadows() const override { return mShadows; }
    void setShadows(bool on) override {
        if (on == mShadows) return;
        mShadows = on;
        rebuildWorkspaceDef();
    }
    void setBackground(const Colour &c) override {
        const bool same = std::abs(c.r - mBackground.r) < 1e-4f && std::abs(c.g - mBackground.g) < 1e-4f &&
                          std::abs(c.b - mBackground.b) < 1e-4f && std::abs(c.a - mBackground.a) < 1e-4f;
        if (same) return;
        mBackground = c;
        rebuildWorkspaceDef();
    }
    /// The clear colour and the shadow node live in the workspace definition:
    /// rebuild definition + workspace, keeping scene, camera and enabled state.
    void rebuildWorkspaceDef() {
        JAH_TRY {
            Ogre::CompositorManager2 *cm = mRoot->getCompositorManager2();
            OgreScene *scene = mScene;
            const bool hadWorkspace = mWorkspace != nullptr;
            if (mWorkspace) { cm->removeWorkspace(mWorkspace); mWorkspace = nullptr; }
            if (cm->hasWorkspaceDefinition(mWorkspaceDef)) cm->removeWorkspaceDefinition(mWorkspaceDef);
            if (cm->hasNodeDefinition(mNodeDef)) cm->removeNodeDefinition(mNodeDef);
            cm->createBasicWorkspaceDef(mWorkspaceDef, toOgre(mBackground),
                                        mShadows ? Ogre::IdString(kShadowNodeName) : Ogre::IdString());
            if (hadWorkspace && scene && mCamera)
                mWorkspace = cm->addWorkspace(scene->sceneManager(), target(), mCamera, mWorkspaceDef, mEnabled);
        } JAH_CATCH(mError, );
    }
    static constexpr const char *kShadowNodeName = "JahshakaShadowNode";
    bool isEnabled() const override { return mEnabled; }
    unsigned width()  const override { return mWidth; }
    unsigned height() const override { return mHeight; }
    bool isOffscreen() const override { return mTexture != nullptr; }

    void resize(unsigned w, unsigned h) override {
        if (!w || !h) return;
        JAH_TRY {
            if (mWindow) {
                mWindow->requestResolution(w, h);
                mWindow->windowMovedOrResized();
            } else {
                // An RTT cannot be resized in place: rebuild it and re-add the workspace.
                Ogre::CompositorManager2 *cm = mRoot->getCompositorManager2();
                const bool hadWorkspace = mWorkspace != nullptr;
                if (mWorkspace) { cm->removeWorkspace(mWorkspace); mWorkspace = nullptr; }
                Ogre::TextureGpuManager *tm = mRoot->getRenderSystem()->getTextureGpuManager();
                tm->destroyTexture(mTexture);
                mTexture = createRtt(mRoot, processUniqueName("rtt"), w, h);
                if (hadWorkspace && mScene)
                    mWorkspace = cm->addWorkspace(mScene->sceneManager(), mTexture, mCamera,
                                                  mWorkspaceDef, mEnabled);
            }
            mWidth = w; mHeight = h;
        } JAH_CATCH(mError, );
    }

    bool readPixels(Image &out) override {
        if (!mTexture) { mError = "readPixels: View '" + mName + "' is on-screen"; return false; }
        JAH_TRY {
            Ogre::TextureGpuManager *tm = mRoot->getRenderSystem()->getTextureGpuManager();
            const Ogre::uint32 w = mTexture->getWidth(), h = mTexture->getHeight();
            Ogre::AsyncTextureTicket *t = tm->createAsyncTextureTicket(
                w, h, 1u, Ogre::TextureTypes::Type2D, mTexture->getPixelFormat());
            t->download(mTexture, 0, true);
            const Ogre::TextureBox box = t->map(0);
            out.width = w; out.height = h;
            out.rgba.resize(static_cast<size_t>(w) * h * 4u);
            for (Ogre::uint32 y = 0; y < h; ++y)
                std::memcpy(&out.rgba[static_cast<size_t>(y) * w * 4u], box.at(0, y, 0), w * 4u);
            t->unmap();
            tm->destroyAsyncTextureTicket(t);
            return true;
        } JAH_CATCH(mError, false);
    }

    /// Releases workspace, camera, workspace definitions and the window/texture.
    /// Safe to call twice. Called by Engine::destroyView and by the Engine
    /// destructor BEFORE Root dies.
    void destroy() {
        detachScene();
        JAH_TRY {
            Ogre::CompositorManager2 *cm = mRoot->getCompositorManager2();
            if (cm->hasWorkspaceDefinition(mWorkspaceDef)) cm->removeWorkspaceDefinition(mWorkspaceDef);
            if (cm->hasNodeDefinition(mNodeDef))           cm->removeNodeDefinition(mNodeDef);
            if (mWindow)  { mRoot->getRenderSystem()->destroyRenderWindow(mWindow); mWindow = nullptr; }
            if (mTexture) { mRoot->getRenderSystem()->getTextureGpuManager()->destroyTexture(mTexture); mTexture = nullptr; }
        } JAH_CATCH(mError, );
    }

    static Ogre::TextureGpu *createRtt(Ogre::Root *root, const std::string &name,
                                       unsigned w, unsigned h) {
        Ogre::TextureGpuManager *tm = root->getRenderSystem()->getTextureGpuManager();
        Ogre::TextureGpu *rtt = tm->createTexture(
            name, Ogre::GpuPageOutStrategy::Discard,
            Ogre::TextureFlags::RenderToTexture, Ogre::TextureTypes::Type2D);
        rtt->setResolution(w, h);
        rtt->setPixelFormat(Ogre::PFG_RGBA8_UNORM);
        rtt->scheduleTransitionTo(Ogre::GpuResidency::Resident);
        return rtt;
    }

private:
    Ogre::TextureGpu *target() const { return mWindow ? mWindow->getTexture() : mTexture; }

    Ogre::Root                *mRoot;
    Ogre::Window              *mWindow;
    Ogre::TextureGpu          *mTexture;
    Ogre::Camera              *mCamera    = nullptr;
    Ogre::CompositorWorkspace *mWorkspace = nullptr;
    OgreScene                 *mScene     = nullptr;
    std::string                mName, mWorkspaceDef, mNodeDef;
    unsigned                   mWidth, mHeight;
    Colour                     mBackground;
    bool                       mEnabled = true;
    bool                       mShadows = false;
    std::string               &mError;
};

// ---------------------------------------------------------------------------
/// The one live engine in this process. Ogre::Root is a Singleton: a second
/// `new Root` asserts, so create() refuses while this is set.
OgreEngine *gLiveEngine = nullptr;

class OgreEngine final : public Engine {
public:
    bool init(const EngineConfig &cfg, std::string &error) {
        mDisplay = reinterpret_cast<Display *>(cfg.display);
        mMediaDir = cfg.hlmsMediaDir;
        if (!mMediaDir.empty() && mMediaDir.back() != '/') mMediaDir += '/';
        try {
            mAbiCookie = Ogre::generateAbiCookie();
            mRoot = new Ogre::Root(&mAbiCookie, "", "",
                                   cfg.logFile.empty() ? "jahshaka-ogre.log" : cfg.logFile,
                                   "Jahshaka");
            const char *plugin = (cfg.backend == Backend::Vulkan) ? "RenderSystem_Vulkan"
                                                                  : "RenderSystem_GL3Plus";
            mRoot->loadPlugin(cfg.pluginDir + "/" + plugin, false, nullptr);

            const Ogre::RenderSystemList &list = mRoot->getAvailableRenderers();
            if (list.empty()) { error = "no Ogre render systems available"; return false; }
            mRoot->setRenderSystem(list[0]);
            mBackendName = list[0]->getName();
            mRoot->initialise(false);
            // NOTE: Hlms registration is deferred to the first view. The VaoManager
            // does not exist until a render target is created, and HlmsUnlit/HlmsPbs
            // registration walks it via ConstBufferPool::_changeRenderSystem —
            // registering here segfaults.
            return true;
        } JAH_CATCH(error, false);
    }

    Scene *createScene(const std::string &name) override {
        if (!mHlmsRegistered) {
            mLastError = "createScene('" + name + "'): no View exists yet — create a View first";
            return nullptr;
        }
        for (auto &s : mScenes)
            if (s->name() == name) { mLastError = "Scene '" + name + "' already exists"; return nullptr; }
        JAH_TRY {
            Ogre::SceneManager *sm = mRoot->createSceneManager(Ogre::ST_GENERIC, 2, name);
            // HlmsPbs shades point and spot lights ONLY through Forward+ (Forward3D /
            // ForwardClustered); without it only directional lights reach the shader.
            // Values are Ogre's sample defaults: 16x8 grid, 24 slices, 96 lights per
            // cell, no decals/probes, 2..50 units depth range.
            sm->setForwardClustered(true, 16, 8, 24, 96, 0, 0, 2.0f, 50.0f);
            // Shadow maps cover nothing until these are set (Ogre's samples set both).
            sm->setShadowDirectionalLightExtrusionDistance(500.0f);
            sm->setShadowFarDistance(500.0f);
            mScenes.emplace_back(new OgreScene(mRoot, sm, name, mLastError));
            return mScenes.back().get();
        } JAH_CATCH(mLastError, nullptr);
    }

    void destroyScene(Scene *scene) override {
        if (!scene) return;
        for (auto it = mScenes.begin(); it != mScenes.end(); ++it) {
            if (it->get() != scene) continue;
            for (auto &v : mViews)
                if (v->scene() == scene) v->detachScene();
            (*it)->destroy();
            mScenes.erase(it);
            return;
        }
        mLastError = "destroyScene: unknown Scene";
    }

    View *createView(const std::string &name,
                     NativeWindowHandle handle, unsigned width, unsigned height,
                     const Colour &background) override {
        if (viewNameTaken(name)) return nullptr;
        JAH_TRY {
            Ogre::NameValuePairList params;
            // Ogre consumes the SDL2x11 struct synchronously inside createRenderWindow;
            // a stack local is correct (the old heap vector was a leak).
            X11Handle x11{ mDisplay, (::Window)handle };
            if (mBackendName.find("Vulkan") != std::string::npos) {
                // Vulkan/XCB takes only "SDL2x11": a pointer to {Display*, Window}.
                if (!mDisplay) { mLastError = "createView: host must supply its X display"; return nullptr; }
                params["SDL2x11"] = Ogre::StringConverter::toString((unsigned long)&x11);
            } else {
                params["parentWindowHandle"] = Ogre::StringConverter::toString((unsigned long)handle);
                params["gamma"] = "true";
            }
            params["vsync"]         = "true";
            params["vsyncInterval"] = "1";
            Ogre::Window *window = mRoot->createRenderWindow(name, width, height, false, &params);
            window->setVSync(true, 1);
            ensureHlms();
            mViews.emplace_back(new OgreView(mRoot, window, nullptr, name, width, height,
                                             background, mLastError));
            return mViews.back().get();
        } JAH_CATCH(mLastError, nullptr);
    }

    View *createOffscreenView(const std::string &name, unsigned width, unsigned height,
                              const Colour &background) override {
        if (viewNameTaken(name)) return nullptr;
        if (!width || !height) { mLastError = "createOffscreenView: zero size"; return nullptr; }
        JAH_TRY {
            // Ogre requires a Window before Hlms/SceneManager exist. A purely offscreen
            // engine satisfies it with a surfaceless "null" window, kept for the
            // engine's lifetime (needs Ogre built with OGRE_VULKAN_WINDOW_NULL).
            if (!mHlmsRegistered && !mNullWindow) {
                Ogre::NameValuePairList wp; wp["windowType"] = "null";
                mNullWindow = mRoot->createRenderWindow(processUniqueName("jahshaka-null"),
                                                        8, 8, false, &wp);
            }
            ensureHlms();   // retried on every call until it succeeds (e.g. bad media dir)
            Ogre::TextureGpu *rtt = OgreView::createRtt(mRoot, processUniqueName("rtt"), width, height);
            mViews.emplace_back(new OgreView(mRoot, nullptr, rtt, name, width, height,
                                             background, mLastError));
            return mViews.back().get();
        } JAH_CATCH(mLastError, nullptr);
    }

    void destroyView(View *view) override {
        if (!view) return;
        for (auto it = mViews.begin(); it != mViews.end(); ++it) {
            if (it->get() != view) continue;
            (*it)->destroy();
            mViews.erase(it);
            return;
        }
        mLastError = "destroyView: unknown View";
    }

    void renderOneFrame() override {
        JAH_TRY { if (mRoot) mRoot->renderOneFrame(); } JAH_CATCH(mLastError, );
    }
    std::string backendName() const override { return mBackendName; }
    const std::string &lastError() const override { return mLastError; }

    ~OgreEngine() override {
        // Dependency order, all BEFORE Root: views (workspaces, cameras, windows,
        // textures) -> scenes (items, datablocks, our MeshPtrs, scene managers)
        // -> null window -> any leftover meshes -> Root.
        for (auto &v : mViews)  v->destroy();
        mViews.clear();
        for (auto &s : mScenes) s->destroy();
        mScenes.clear();
        try {
            if (mNullWindow && mRoot) mRoot->getRenderSystem()->destroyRenderWindow(mNullWindow);
            mNullWindow = nullptr;
            if (mRoot && Ogre::MeshManager::getSingletonPtr())
                Ogre::MeshManager::getSingleton().removeAll();
        } catch (...) {}
        delete mRoot;
        mRoot = nullptr;
        gLiveEngine = nullptr;
    }

private:
    struct X11Handle { Display *display; ::Window window; };

    bool viewNameTaken(const std::string &name) {
        for (auto &v : mViews)
            if (v->name() == name) { mLastError = "View '" + name + "' already exists"; return true; }
        return false;
    }

    /// First render target: the VaoManager now exists, so Hlms can be registered.
    void ensureHlms() {
        if (mHlmsRegistered) return;
        Ogre::ArchiveManager &am = Ogre::ArchiveManager::getSingleton();
        Ogre::String mainPath; Ogre::StringVector libPaths;

        Ogre::HlmsUnlit::getDefaultPaths(mainPath, libPaths);
        {
            Ogre::ArchiveVec libs;
            for (const auto &p : libPaths) libs.push_back(am.load(mMediaDir + p, "FileSystem", true));
            mRoot->getHlmsManager()->registerHlms(
                OGRE_NEW Ogre::HlmsUnlit(am.load(mMediaDir + mainPath, "FileSystem", true), &libs));
        }
        Ogre::HlmsPbs::getDefaultPaths(mainPath, libPaths);
        {
            Ogre::ArchiveVec libs;
            for (const auto &p : libPaths) libs.push_back(am.load(mMediaDir + p, "FileSystem", true));
            mRoot->getHlmsManager()->registerHlms(
                OGRE_NEW Ogre::HlmsPbs(am.load(mMediaDir + mainPath, "FileSystem", true), &libs));
        }
        mHlmsRegistered = true;
        createShadowNode();
    }
    /// One shadow node for the process: PSSM (3 splits) for the first directional
    /// light (point/spot shadow maps pending — see below).
    /// Mirrors Ogre's ShadowMapFromCode sample. Views opt in with setShadows(true).
    void createShadowNode() {
        Ogre::CompositorManager2 *cm = mRoot->getCompositorManager2();
        if (cm->hasShadowNodeDefinition(OgreView::kShadowNodeName)) return;
        Ogre::ShadowNodeHelper::ShadowParamVec params;
        Ogre::ShadowNodeHelper::ShadowParam p;
        memset(&p, 0, sizeof(p));
        p.technique = Ogre::SHADOWMAP_PSSM;
        p.numPssmSplits = 3u;
        p.resolution[0].x = 2048u; p.resolution[0].y = 2048u;
        for (size_t i = 1u; i < 4u; ++i) { p.resolution[i].x = 1024u; p.resolution[i].y = 1024u; }
        p.atlasStart[0].x = 0u;    p.atlasStart[0].y = 0u;
        p.atlasStart[1].x = 0u;    p.atlasStart[1].y = 2048u;
        p.atlasStart[2].x = 1024u; p.atlasStart[2].y = 2048u;
        p.supportedLightTypes = 0u;
        p.addLightType(Ogre::Light::LT_DIRECTIONAL);
        params.push_back(p);
        // Point/spot shadow maps need Ogre's 'Ogre/DPSM/CubeToDpsm' material from
        // Samples/Media/2.0/scripts/materials/Common, which is not shipped yet.
        // Directional (PSSM) only until that media is staged.
        Ogre::ShadowNodeHelper::createShadowNodeWithSettings(
            cm, mRoot->getRenderSystem()->getCapabilities(), OgreView::kShadowNodeName, params, false);
    }

    Ogre::Root     *mRoot = nullptr;
    Ogre::Window   *mNullWindow = nullptr;
    Display        *mDisplay = nullptr;
    bool            mHlmsRegistered = false;
    Ogre::AbiCookie mAbiCookie{};
    std::string     mBackendName, mMediaDir, mLastError;
    std::vector<std::unique_ptr<OgreScene>> mScenes;
    std::vector<std::unique_ptr<OgreView>>  mViews;
};

}  // namespace

bool Engine::isAlive() { return gLiveEngine != nullptr || Ogre::Root::getSingletonPtr() != nullptr; }

std::unique_ptr<Engine> Engine::create(const EngineConfig &cfg, std::string &error) {
    if (isAlive()) {
        error = "an Engine already exists in this process; destroy it before creating another";
        return nullptr;
    }
    if (cfg.pluginDir.empty())    { error = "EngineConfig::pluginDir is empty";    return nullptr; }
    if (cfg.hlmsMediaDir.empty()) { error = "EngineConfig::hlmsMediaDir is empty"; return nullptr; }
    auto engine = std::unique_ptr<OgreEngine>(new OgreEngine());
    gLiveEngine = engine.get();
    if (!engine->init(cfg, error)) return nullptr;   // ~OgreEngine clears gLiveEngine
    return engine;
}

}}  // namespace jahshaka::engine
