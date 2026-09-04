#ifndef ENGINEMATERIALPREVIEWSCENE_H
#define ENGINEMATERIALPREVIEWSCENE_H

// EngineMaterialPreviewScene — the Effects/Materials Display preview on the
// engine, minus the widget.
//
// Owns a small iris::Scene document (one primitive at the origin, a key
// directional light and a fill point light, the legacy grey sky), its own
// engine Scene ("matpreview") and a SceneMirror pushing document -> engine,
// plus the same orbit-camera maths as EngineAssetScene. What the legacy GL
// SceneWidget showed — the graph's material on a chosen primitive over a
// flat background — this shows engine-rendered. No GL, no Ogre, no QWidget:
// testable headless with an offscreen View (tests/materialpreview).
// EngineMaterialPreview wraps it into the Display dock.
#include "irisgl/core/math/vec.h"
#include <memory>
#include <QColor>
#include <Qt>
#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"

class SceneMirror;

/// The legacy Display-menu primitives (widgets/scenewidget.h PreviewModel),
/// loaded from the same app/shadergraph/*.obj meshes.
enum class PreviewMesh
{
    Sphere,
    Cube,
    Plane,
    Cylinder,
    Capsule,
    Torus
};

class EngineMaterialPreviewScene
{
public:
    /// Holds the engine weakly (EngineAssetScene's contract): never keeps it
    /// alive, every call checks it is still there.
    explicit EngineMaterialPreviewScene(const std::shared_ptr<jahshaka::engine::Engine> &engine);
    ~EngineMaterialPreviewScene();

    /// Creates the matpreview Scene and its mirror and binds them to `view`
    /// (the View must already exist: Engine.h, ORDER MATTERS). Idempotent;
    /// false if the engine is gone or the scene could not be created.
    bool attach(jahshaka::engine::View *view);
    /// Destroys the engine Scene and mirror while the Engine is alive. The
    /// View is the caller's. Safe to call repeatedly; the destructor calls it.
    void release();
    jahshaka::engine::Scene *engineScene() const { return mScene; }
    jahshaka::engine::View *view() const { return mView; }
    iris::ScenePtr document() const { return mDocument; }
    iris::CameraNodePtr camera() const { return mCamera; }

    /// Applies `material` to the preview primitive (kept across primitive
    /// switches). Null is ignored.
    void setMaterial(iris::MaterialPtr material);
    /// Switches the primitive; false if its mesh could not be loaded (the
    /// current one stays). The mesh node is REPLACED, not mutated: the mirror
    /// re-attaches a node's mesh only when its material pointer changes
    /// (scenemirror.cpp), so an in-place mesh swap would never reach the engine.
    bool setPreviewMesh(PreviewMesh mesh);
    PreviewMesh previewMesh() const { return mMesh; }
    /// The flat background behind the primitive (legacy setClearColor).
    void setBackground(const QColor &colour);

    // ---- the orbit (EngineAssetScene's maths; left/right drag orbits) ----
    void mouseDown(Qt::MouseButton b);
    void mouseUp(Qt::MouseButton b);
    void mouseMove(int dx, int dy);
    void wheel(int delta);
    /// Turns the orbit by whole angles (tests; the same path the mouse takes).
    void orbit(float yawDegrees, float pitchDegrees);

    /// One frame: orbit lerp, document update, document -> engine, sky and
    /// camera -> view. `width`/`height` are the view's pixel size.
    void step(float dt, int width, int height);

private:
    void buildDocument();
    void rebuildSubject();
    void updateCameraRot();
    iris::MeshPtr meshFor(PreviewMesh mesh);

    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View  *mView  = nullptr;
    jahshaka::engine::Scene *mScene = nullptr;
    std::unique_ptr<SceneMirror> mMirror;

    iris::ScenePtr      mDocument;
    iris::CameraNodePtr mCamera;
    iris::SceneNodePtr  mSubject;
    iris::MaterialPtr   mMaterial;
    iris::MeshPtr       mMeshes[6];
    PreviewMesh mMesh = PreviewMesh::Sphere;

    // Orbit state (OrbitalCameraController in preview mode, rotation speed .5)
    float mYaw = 0, mPitch = 0, mTargetYaw = 0, mTargetPitch = 0;
    float mRotationSpeed = 0.5f;
    iris::Vec3 mPivot;
    float mDistFromPivot = 4.0f;
    bool mLeftDown = false, mRightDown = false, mMiddleDown = false;
};

#endif // ENGINEMATERIALPREVIEWSCENE_H
