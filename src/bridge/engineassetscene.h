#ifndef ENGINEASSETSCENE_H
#define ENGINEASSETSCENE_H

// EngineAssetScene — the Assets page preview on the engine, minus the widget.
//
// Owns a small iris::Scene document built exactly like AssetViewer's
// (key + rim light, the tiled floor at y = -5, sky (25,25,25)), a third engine
// Scene ("assets") and a SceneMirror that pushes the document into it, and the
// orbit camera (the OrbitalCameraController maths in preview mode, kept local
// so this class links without the editor). One asset is previewed at a time:
// a node (mesh hierarchy) placed on the floor and framed by its bounds, or a
// material on the preview sphere. No GL, no Ogre, no QWidget, no database —
// testable headless with an offscreen View (tests/assets). EngineAssetViewer
// wraps it and does the database/JSON side.
#include "irisgl/core/math/vec.h"
#include <memory>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <Qt>
#include "irisgl/irisglfwd.h"
#include "jahshaka/engine/Engine.h"

class SceneMirror;

class EngineAssetScene
{
public:
    /// Holds the engine weakly, like EnginePlayerScene: never keeps it alive,
    /// every call checks it is still there.
    explicit EngineAssetScene(const std::shared_ptr<jahshaka::engine::Engine> &engine);
    ~EngineAssetScene();

    /// Creates the assets Scene and its mirror and binds them to `view` (the View
    /// must already exist: Engine.h, ORDER MATTERS). Idempotent; false if the
    /// engine is gone or the scene could not be created.
    bool attach(jahshaka::engine::View *view);
    /// Destroys the engine Scene and mirror while the Engine is alive. The View
    /// is the caller's. Safe to call repeatedly; the destructor calls it.
    void release();
    jahshaka::engine::Scene *engineScene() const { return mScene; }
    jahshaka::engine::View *view() const { return mView; }

    /// The preview document and its camera.
    iris::ScenePtr document() const { return mDocument; }
    iris::CameraNodePtr camera() const { return mCamera; }

    // ---- what is previewed (AssetViewer::addNodeToScene and friends) ----
    /// Replaces the previewed asset with `node`. `isOnGround` drops it onto the
    /// floor; unless `viewed`, the camera is re-framed on its bounds (the
    /// legacy rule: radius * 1.2 / tan(fov / 2) away, looking at the centre).
    /// Materials without a mirrorable type get the default material.
    void setSubject(iris::SceneNodePtr node, bool viewed = false, bool isOnGround = true);
    /// Previews `material` on the high-poly sphere (AssetViewer::addJafMaterial).
    iris::SceneNodePtr setMaterialSubject(iris::MaterialPtr material, const QString &name = "ae98cx7u_mat_ball");
    /// Removes the previewed asset (lights and floor stay). AssetViewer::clearScene.
    void clearSubject();
    iris::SceneNodePtr subject() const;

    /// AssetViewer::changeBackdrop: 1 dark, 2 grey (no floor, no shadows), 3 floor + shadows.
    void setBackdrop(unsigned int id);
    void setSkyColor(const QColor &c);

    // ---- the orbit camera (AssetViewer::resetViewerCamera[After] / orientCamera) ----
    /// First look at a freshly framed asset: from the framing position, looking
    /// at its centre, at the framing distance.
    void resetCamera();
    /// Re-apply a saved orbit without re-framing.
    void resetCameraAfter();
    void orientCamera(iris::Vec3 pos, iris::Vec3 localRot, float distanceFromPivot);
    QJsonObject sceneProperties() const;

    /// Mouse, in the viewer's convention (AssetViewer forwards -dx, -dy).
    void mouseDown(Qt::MouseButton b);
    void mouseUp(Qt::MouseButton b);
    void mouseMove(int dx, int dy);
    void wheel(int delta);
    /// Turns the orbit by whole angles (tests; the same path the mouse takes).
    void orbit(float yawDegrees, float pitchDegrees);

    /// One frame: orbit lerp, document update, document -> engine, sky and camera
    /// -> view. `width`/`height` are the view's pixel size (aspect ratio).
    void step(float dt, int width, int height);

    /// The RTT preview: renders the current scene from the current camera into a
    /// temporary offscreen View of this size and reads it back (AssetViewer::takeScreenshot).
    QImage renderImage(int width, int height);

    /// Mesh of the preview sphere, loaded once (resource, then app/content/primitives).
    iris::MeshPtr previewSphere();

private:
    void buildDocument();
    void updateCameraRot();
    void orbitFromCamera();
    /// Fits nearClip/farClip to the orbit distance and the subject's radius
    /// (a preview camera must see the framed model regardless of its scale).
    void applyClipPlanes();
    static QImage toQImage(const jahshaka::engine::Image &img);

    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View  *mView  = nullptr;
    jahshaka::engine::Scene *mScene = nullptr;
    std::unique_ptr<SceneMirror> mMirror;
    unsigned mShotSerial = 0;

    iris::ScenePtr      mDocument;
    iris::CameraNodePtr mCamera;
    iris::SceneNodePtr  mFloor;
    iris::MeshPtr       mSphere;
    bool mShadows = true;

    // Orbit state (OrbitalCameraController, previewMode, rotationSpeed .5)
    float mYaw = 0, mPitch = 0, mTargetYaw = 0, mTargetPitch = 0;
    float mRotationSpeed = 0.5f;
    iris::Vec3 mPivot;
    float mDistFromPivot = 5.0f;
    bool mLeftDown = false, mRightDown = false, mMiddleDown = false;

    // Framing (AssetViewer::localPos / localRot / lookAt / distanceFromPivot)
    iris::Vec3 mLocalPos, mLocalRot, mLookAt;
    float mDistanceFromPivot = 5.0f;
    float mSubjectRadius = 1.0f;   // world-space radius of the framed subject
};

#endif // ENGINEASSETSCENE_H
