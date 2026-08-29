#pragma once
// Jahshaka's engine abstraction.
//
// This is THE boundary between the application and the 3D engine. Studio talks
// only to these types. No Ogre type, header or symbol appears here — swapping the
// backend must not touch a single file under src/.
//
// Interface derived from what Studio DOES, not from what any engine offers.
//
// THREAD AFFINITY — no exceptions: every call on Engine, Scene and View, including
// destruction, must happen on the thread that called Engine::create(). The backend
// owns a single device and is not internally synchronised. Background work
// (thumbnails, imports) posts to that thread; it never calls in directly.
//
// ERRORS: no backend exception ever escapes this boundary. A failing call returns
// null/false and the reason is available from Engine::lastError() until the next
// failing call overwrites it.
#include <memory>
#include <string>
#include "Types.h"

namespace jahshaka { namespace engine {

class Scene;
class View;

/// A renderable scene. Views draw it; several Views may share one, or each may own one.
/// Owned by the Engine: destroy with Engine::destroyScene().
class Scene {
public:
    virtual ~Scene() = default;
    virtual const std::string &name() const = 0;
    virtual void        setAmbient(const Colour &upper, const Colour &lower) = 0;
    virtual NodeId      addDirectionalLight(const Vec3 &direction, float power) = 0;
    /// Unit cube with a PBR metallic-roughness material. Proves the material path end to end.
    // TEMPORARY — replaced by mesh loading in step 3 of VIEWPORT_MIGRATION_PLAN.md
    virtual NodeId      addTestCube(const Colour &albedo, float metalness, float roughness) = 0;
    /// Removes a node and everything it uniquely owns (mesh, material). Unknown or
    /// already-removed ids are ignored and return false. Children are NOT removed;
    /// they are re-parented to the scene root.
    virtual bool        removeNode(NodeId) = 0;
    virtual void        setNodePosition(NodeId, const Vec3 &) = 0;
    virtual void        setNodeScale(NodeId, const Vec3 &) = 0;
    virtual void        rotateNode(NodeId, float yawRadians, float pitchRadians, float rollRadians) = 0;

    // ---- Hierarchy and transforms (VIEWPORT_MIGRATION_PLAN.md step 2) ----
    /// An empty transform node under `parent` (0 = the scene root).
    virtual NodeId      createNode(NodeId parent = 0) = 0;
    virtual bool        setNodeParent(NodeId, NodeId parent) = 0;
    /// Absolute LOCAL transform (relative to the parent). The document owns the
    /// numbers; the engine composes the hierarchy.
    virtual void        setNodeTransform(NodeId, const Vec3 &position, const Quat &rotation,
                                         const Vec3 &scale) = 0;
    /// Hides the node and its subtree.
    virtual void        setNodeVisible(NodeId, bool) = 0;

    // ---- Meshes and materials (step 3/4) ----
    /// Uploads geometry. Returns 0 on invalid data (lastError()).
    virtual MeshId      createMesh(const MeshData &) = 0;
    virtual bool        destroyMesh(MeshId) = 0;
    virtual MaterialId  createPbrMaterial(const PbrParams &) = 0;
    virtual bool        setPbrMaterial(MaterialId, const PbrParams &) = 0;
    virtual bool        destroyMaterial(MaterialId) = 0;
    /// Makes the node render `mesh` with `material`. A node renders at most one mesh;
    /// attaching again replaces it. Mesh and material may be shared across nodes and
    /// survive the node.
    virtual bool        attachMesh(NodeId, MeshId, MaterialId) = 0;
    virtual bool        detachMesh(NodeId) = 0;

    // ---- Textures (step 4b): image files on disk, shared across materials ----
    /// Loads an image file (png/jpg/tga/dds...). `srgb` for colour maps (albedo,
    /// emissive); false for data maps (normal, roughness, metalness). The same path
    /// loaded twice returns the same id. 0 on failure (lastError()).
    virtual TextureId   loadTexture(const std::string &path, bool srgb) = 0;
    virtual bool        destroyTexture(TextureId) = 0;
    /// Binds (or, with 0, clears) a texture slot on a PBR material.
    virtual bool        setPbrTexture(MaterialId, PbrTextureSlot, TextureId) = 0;

    // ---- Overlay primitives (step 8): gizmos, light wires, animation paths ----
    /// Flat colour, unlit. With depthTest=false it draws on top of everything —
    /// what gizmo handles need. Alpha < 1 blends.
    /// `wireframe` draws only the triangle edges — the selection outline uses it.
    virtual MaterialId  createUnlitMaterial(const Colour &, bool depthTest, bool wireframe = false) = 0;
    virtual bool        setUnlitMaterial(MaterialId, const Colour &) = 0;
    /// A line list (pairs of points) or, with `strip`, a connected polyline.
    /// Attach with attachMesh like any mesh. One pixel wide.
    virtual MeshId      createLineMesh(const std::vector<Vec3> &points, bool strip) = 0;

    // ---- Lights (step 5): a node may carry one light ----
    virtual bool        setLight(NodeId, const LightDesc &) = 0;   // creates or updates
    virtual bool        removeLight(NodeId) = 0;
};

/// A view onto a Scene, rendering into a native window supplied by the host or
/// into an offscreen texture. Owned by the Engine: destroy with Engine::destroyView().
class View {
public:
    virtual ~View() = default;
    virtual const std::string &name() const = 0;
    /// Binds a Scene to this View. Call after createScene(); a View renders nothing
    /// until a Scene is attached. A View holds at most one Scene: binding a second
    /// while one is attached fails (false, lastError()). Pass null to detach.
    virtual bool setScene(Scene *) = 0;
    virtual Scene *scene() const = 0;
    virtual void setCameraPosition(const Vec3 &) = 0;
    virtual void lookAt(const Vec3 &) = 0;
    /// Full camera state in one call (step 5). The document camera is pushed
    /// through this every frame.
    virtual void setCamera(const CameraDesc &) = 0;
    /// Clear colour behind the scene (the document's flat sky colour). Cheap to
    /// call with the same value; a change rebuilds the view's compositor workspace.
    virtual void setBackground(const Colour &) = 0;
    virtual Colour background() const = 0;
    /// A disabled View is skipped by renderOneFrame(). Hidden viewports MUST be
    /// disabled — the backend otherwise keeps drawing them at full cost.
    virtual void setEnabled(bool) = 0;
    virtual bool isEnabled() const = 0;
    virtual void resize(unsigned width, unsigned height) = 0;
    virtual unsigned width() const = 0;
    virtual unsigned height() const = 0;
    virtual bool isOffscreen() const = 0;
    /// Reads this View's rendered pixels back to the CPU. Offscreen Views only —
    /// returns false for on-screen windows. This is the thumbnail path, and what
    /// makes the engine testable without a window.
    virtual bool readPixels(Image &out) = 0;
};

/// Owns the device and every Scene and View.
///
/// ONE PER PROCESS. The backend is a process-wide singleton; create() refuses to
/// make a second Engine while one is alive (returns null + error). Destroying it
/// and creating another later is supported (tests/engine/test_engine_recreate) —
/// this needs the Ogre-Next patch recorded in OGRE_PLATFORM_DEPS.md.
///
/// LIFETIME CONTRACT: every View and Scene pointer handed out is owned by the
/// Engine and dies with it. Hosts that cache a View* (e.g. a widget) must call
/// destroyView() before the Engine is destroyed, or must check the Engine is still
/// alive before touching the pointer — see EngineViewWidget for the pattern.
class Engine {
public:
    virtual ~Engine() = default;

    /// Creates the engine. Returns null on failure and fills `error`.
    static std::unique_ptr<Engine> create(const EngineConfig &, std::string &error);
    /// True while an Engine exists in this process.
    static bool isAlive();

    /// ORDER MATTERS. A View must be created before any Scene: the underlying engine
    /// only starts its material and buffer systems when the first render target
    /// exists, and creating a Scene before that dereferences null.
    ///   createView(...)  ->  createScene(...)  ->  view->setScene(scene)
    /// Names must be unique among live Views; a duplicate returns null (lastError()).
    virtual View  *createView(const std::string &name,
                              NativeWindowHandle, unsigned width, unsigned height,
                              const Colour &background) = 0;
    /// An offscreen View: renders to a texture instead of a window. Needs no native
    /// handle, so it works headless. Used for thumbnails, asset previews and tests.
    virtual View  *createOffscreenView(const std::string &name,
                                       unsigned width, unsigned height,
                                       const Colour &background) = 0;
    /// Releases the View's window/texture and camera. Any Scene it showed survives.
    /// Null or unknown pointers are ignored.
    virtual void   destroyView(View *) = 0;

    /// Returns null if called before the first createView()/createOffscreenView(),
    /// or if the name is already in use (lastError()).
    virtual Scene *createScene(const std::string &name) = 0;
    /// Destroys the Scene and every node, mesh and material it owns. Views bound to
    /// it are detached first (they stay alive, showing nothing).
    virtual void   destroyScene(Scene *) = 0;

    /// Draws every enabled View once. The host owns the loop and calls this.
    virtual void renderOneFrame() = 0;

    virtual std::string backendName() const = 0;
    /// Reason for the most recent failure; empty if none.
    virtual const std::string &lastError() const = 0;
};

}}  // namespace jahshaka::engine
