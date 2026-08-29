// The Ogre-Next 4.0 backend.
//
// THE ONLY translation unit in Jahshaka that includes Ogre. If Ogre appears in
// any other file, the boundary has been breached.
//
// Verified behaviours this file depends on (spikes/qt-ogre-next/FINDINGS.md):
//   * Vulkan supports MULTIPLE on-screen windows; GL3Plus does not (single mGlobalVao).
//   * v1 meshes render NOTHING on Vulkan — geometry is built as v2 buffers directly.
//   * Hlms shader templates are required at runtime, not optional sample data.
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
#include <OgreTextureGpuManager.h>
#include <Compositor/OgreCompositorManager2.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <Compositor/OgreCompositorNodeDef.h>
#include <Vao/OgreVaoManager.h>
#include <Vao/OgreVertexArrayObject.h>

#include <X11/Xlib.h>
#include <unordered_map>
#include <vector>

#ifndef JAHSHAKA_OGRE_PLUGIN_DIR
#  error "JAHSHAKA_OGRE_PLUGIN_DIR not defined — see cmake/IncludeOgre.cmake"
#endif

namespace jahshaka { namespace engine {
namespace {

inline Ogre::Vector3     toOgre(const Vec3 &v)   { return Ogre::Vector3(v.x, v.y, v.z); }
inline Ogre::ColourValue toOgre(const Colour &c) { return Ogre::ColourValue(c.r, c.g, c.b, c.a); }

class OgreScene final : public Scene {
public:
    OgreScene(Ogre::Root *root, Ogre::SceneManager *sm) : mRoot(root), mSceneMgr(sm) {}

    void setAmbient(const Colour &upper, const Colour &lower) override {
        mSceneMgr->setAmbientLight(toOgre(upper), toOgre(lower), Ogre::Vector3::UNIT_Y);
    }

    NodeId addDirectionalLight(const Vec3 &direction, float power) override {
        Ogre::Light *light = mSceneMgr->createLight();
        Ogre::SceneNode *node = mSceneMgr->getRootSceneNode()->createChildSceneNode();
        node->attachObject(light);
        light->setType(Ogre::Light::LT_DIRECTIONAL);
        light->setPowerScale(power);
        light->setDirection(toOgre(direction).normalisedCopy());
        return track(node);
    }

    NodeId addTestCube(const Colour &albedo, float metalness, float roughness) override {
        Ogre::MeshPtr mesh = buildCubeV2(uniqueName("cube"));
        auto *hlmsPbs = static_cast<Ogre::HlmsPbs *>(
            mRoot->getHlmsManager()->getHlms(Ogre::HLMS_PBS));
        auto *db = static_cast<Ogre::HlmsPbsDatablock *>(hlmsPbs->createDatablock(
            Ogre::IdString(uniqueName("mat")), uniqueName("mat"),
            Ogre::HlmsMacroblock(), Ogre::HlmsBlendblock(), Ogre::HlmsParamVec()));
        // Ogre defaults to SpecularWorkflow; Jahshaka's material model is metallic-roughness.
        db->setWorkflow(Ogre::HlmsPbsDatablock::MetallicWorkflow);
        db->setDiffuse(Ogre::Vector3(albedo.r, albedo.g, albedo.b));
        db->setMetalness(metalness);
        db->setRoughness(roughness);

        Ogre::Item *item = mSceneMgr->createItem(mesh, Ogre::SCENE_DYNAMIC);
        item->setDatablock(db);
        Ogre::SceneNode *node =
            mSceneMgr->getRootSceneNode(Ogre::SCENE_DYNAMIC)
                     ->createChildSceneNode(Ogre::SCENE_DYNAMIC);
        node->attachObject(item);
        return track(node);
    }

    void setNodePosition(NodeId id, const Vec3 &p) override {
        if (auto *n = node(id)) n->setPosition(toOgre(p));
    }
    void setNodeScale(NodeId id, const Vec3 &s) override {
        if (auto *n = node(id)) n->setScale(toOgre(s));
    }
    void rotateNode(NodeId id, float yaw, float pitch, float roll) override {
        if (auto *n = node(id)) {
            n->yaw(Ogre::Radian(yaw)); n->pitch(Ogre::Radian(pitch)); n->roll(Ogre::Radian(roll));
        }
    }

    Ogre::SceneManager *sceneManager() const { return mSceneMgr; }

private:
    Ogre::SceneNode *node(NodeId id) const {
        auto it = mNodes.find(id);
        return it == mNodes.end() ? nullptr : it->second;
    }
    NodeId track(Ogre::SceneNode *n) { mNodes[++mNextId] = n; return mNextId; }
    std::string uniqueName(const char *prefix) {
        return std::string(prefix) + "_" + std::to_string(++mNameCounter) + "_" + mSceneMgr->getName();
    }

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
    std::unordered_map<NodeId, Ogre::SceneNode *> mNodes;
    NodeId       mNextId      = 0;
    unsigned     mNameCounter = 0;
};

class OgreScene;

class OgreView final : public View {
public:
    OgreView(Ogre::Window *w, Ogre::CompositorManager2 *cm, const std::string &defName,
             const Colour &background)
        : mWindow(w), mCompositor(cm), mWorkspaceDef(defName), mBackground(background) {}

    void setScene(Scene *scene) override;

    void setCameraPosition(const Vec3 &p) override { if (mCamera) mCamera->setPosition(toOgre(p)); }
    void lookAt(const Vec3 &t) override            { if (mCamera) mCamera->lookAt(toOgre(t)); }
    void setRenderFlags(const RenderFlags &f) override { mFlags = f; }
    RenderFlags renderFlags() const override           { return mFlags; }
    void resize(unsigned w, unsigned h) override {
        if (!w || !h) return;
        mWindow->requestResolution(w, h);
        mWindow->windowMovedOrResized();
    }

private:
    Ogre::Window              *mWindow;
    Ogre::Camera              *mCamera    = nullptr;
    Ogre::CompositorWorkspace *mWorkspace = nullptr;
    Ogre::CompositorManager2  *mCompositor;
    std::string                mWorkspaceDef;
    Colour                     mBackground;
    RenderFlags                mFlags;
};

void OgreView::setScene(Scene *scene) {
    auto *s = static_cast<OgreScene *>(scene);
    if (!s) return;
    mCamera = s->sceneManager()->createCamera(mWorkspaceDef + "/Camera");
    mCamera->setNearClipDistance(0.1f);
    mCamera->setAutoAspectRatio(true);
    mWorkspace = mCompositor->addWorkspace(s->sceneManager(), mWindow->getTexture(),
                                           mCamera, mWorkspaceDef, true);
}

class OgreEngine final : public Engine {
public:
    bool init(Backend backend, NativeDisplayHandle display, std::string &error) {
        mDisplay = reinterpret_cast<Display *>(display);
        mAbiCookie = Ogre::generateAbiCookie();
        mRoot = new Ogre::Root(&mAbiCookie, "", "", "jahshaka-ogre.log", "Jahshaka");

        const char *plugin = (backend == Backend::Vulkan) ? "RenderSystem_Vulkan"
                                                          : "RenderSystem_GL3Plus";
        try {
            mRoot->loadPlugin(std::string(JAHSHAKA_OGRE_PLUGIN_DIR) + "/" + plugin, false, nullptr);
        } catch (Ogre::Exception &e) { error = e.getFullDescription(); return false; }

        const Ogre::RenderSystemList &list = mRoot->getAvailableRenderers();
        if (list.empty()) { error = "no Ogre render systems available"; return false; }
        mRoot->setRenderSystem(list[0]);
        mBackendName = list[0]->getName();
        mRoot->initialise(false);
        // NOTE: Hlms registration is deferred to the first createView(). The VaoManager
        // does not exist until a render window is created, and HlmsUnlit/HlmsPbs
        // registration walks it via ConstBufferPool::_changeRenderSystem — registering
        // here segfaults.
        return true;
    }

    Scene *createScene(const std::string &name) override {
        if (!mHlmsRegistered) return nullptr;   // no window yet: engine not started
        Ogre::SceneManager *sm = mRoot->createSceneManager(Ogre::ST_GENERIC, 2, name);
        mScenes.emplace_back(new OgreScene(mRoot, sm));
        return mScenes.back().get();
    }

    View *createView(const std::string &name,
                     NativeWindowHandle handle, unsigned width, unsigned height,
                     const Colour &background) override {
        Ogre::NameValuePairList params;
        if (mBackendName.find("Vulkan") != std::string::npos) {
            // Vulkan/XCB takes only "SDL2x11": a pointer to {Display*, Window}.
            if (!mDisplay) return nullptr;   // host must supply its X display
            mX11Handles.emplace_back(new X11Handle{ mDisplay, (::Window)handle });
            params["SDL2x11"] =
                Ogre::StringConverter::toString((unsigned long)mX11Handles.back().get());
        } else {
            params["parentWindowHandle"] = Ogre::StringConverter::toString((unsigned long)handle);
            params["gamma"] = "true";
        }
        params["vsync"]         = "true";
        params["vsyncInterval"] = "1";
        Ogre::Window *window =
            mRoot->createRenderWindow(name, width, height, false, &params);
        window->setVSync(true, 1);

        // First window: the VaoManager now exists, so Hlms can be registered.
        if (!mHlmsRegistered) { registerHlms(); mHlmsRegistered = true; }

        Ogre::CompositorManager2 *cm = mRoot->getCompositorManager2();
        const std::string defName = name + "/Workspace";
        cm->createBasicWorkspaceDef(defName, toOgre(background), Ogre::IdString());

        mViews.emplace_back(new OgreView(window, cm, defName, background));
        return mViews.back().get();
    }

    void renderOneFrame() override { if (mRoot) mRoot->renderOneFrame(); }
    std::string backendName() const override { return mBackendName; }

    ~OgreEngine() override {
        mViews.clear(); mScenes.clear();
        delete mRoot;
    }

private:
    struct X11Handle { Display *display; ::Window window; };

    void registerHlms() {
        const Ogre::String root = JAHSHAKA_OGRE_MEDIA_DIR;
        Ogre::ArchiveManager &am = Ogre::ArchiveManager::getSingleton();
        Ogre::String mainPath; Ogre::StringVector libPaths;

        Ogre::HlmsUnlit::getDefaultPaths(mainPath, libPaths);
        {
            Ogre::ArchiveVec libs;
            for (const auto &p : libPaths) libs.push_back(am.load(root + p, "FileSystem", true));
            mRoot->getHlmsManager()->registerHlms(
                OGRE_NEW Ogre::HlmsUnlit(am.load(root + mainPath, "FileSystem", true), &libs));
        }
        Ogre::HlmsPbs::getDefaultPaths(mainPath, libPaths);
        {
            Ogre::ArchiveVec libs;
            for (const auto &p : libPaths) libs.push_back(am.load(root + p, "FileSystem", true));
            mRoot->getHlmsManager()->registerHlms(
                OGRE_NEW Ogre::HlmsPbs(am.load(root + mainPath, "FileSystem", true), &libs));
        }
    }

    Ogre::Root     *mRoot = nullptr;
    Display        *mDisplay = nullptr;
    bool            mHlmsRegistered = false;
    Ogre::AbiCookie mAbiCookie{};
    std::string     mBackendName;
    std::vector<std::unique_ptr<OgreScene>> mScenes;
    std::vector<std::unique_ptr<OgreView>>  mViews;
    std::vector<std::unique_ptr<X11Handle>> mX11Handles;
};

}  // namespace

std::unique_ptr<Engine> Engine::create(Backend backend, NativeDisplayHandle display,
                                       std::string &error) {
    auto engine = std::unique_ptr<OgreEngine>(new OgreEngine());
    if (!engine->init(backend, display, error)) return nullptr;
    return engine;
}

}}  // namespace jahshaka::engine
