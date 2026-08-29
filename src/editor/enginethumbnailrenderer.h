#ifndef ENGINETHUMBNAILRENDERER_H
#define ENGINETHUMBNAILRENDERER_H

// EngineThumbnailRenderer — renders asset previews (meshes, materials) through
// the engine on the MAIN thread, into an offscreen View on a dedicated Scene.
//
// This replaces ThumbnailGenerator's legacy RenderThread (own QOpenGLContext on
// a worker thread) in engine mode: the engine has one thread affinity, so
// thumbnails are drawn synchronously, one per tick, by ThumbnailGenerator's
// queue. The document for a preview is an ordinary iris::Scene (GL-free) that
// SceneMirror pushes into the "thumbs" engine Scene, exactly like the viewport.
//
// Studio-side code: includes iris (Qt) and the engine abstraction. Never Ogre.
#include <memory>
#include <QImage>
#include <QSize>
#include "irisgl/src/irisglfwd.h"
#include "jahshaka/engine/Engine.h"

class SceneMirror;

class EngineThumbnailRenderer
{
public:
    /// Holds the engine weakly: the renderer never keeps the Engine alive, and
    /// every call checks it is still there.
    explicit EngineThumbnailRenderer(const std::shared_ptr<jahshaka::engine::Engine> &engine);
    ~EngineThumbnailRenderer();

    /// Renders `subject` (a mesh node or a hierarchy of them) framed by its bounding
    /// spheres, with the preview lights. Null image if the engine is gone or the
    /// subject has no geometry.
    QImage renderNode(iris::SceneNodePtr subject, QSize size);
    /// Renders `material` on the preview sphere (app/content/primitives/sphere.obj).
    QImage renderMaterial(iris::MaterialPtr material, QSize size);

    /// Destroys the offscreen View, the thumbs Scene and the mirror while the
    /// Engine is still alive. Safe to call repeatedly; the destructor calls it.
    void release();

    /// Background the offscreen view is cleared to (what "not the background" means).
    static jahshaka::engine::Colour backgroundColour();

private:
    bool ensureResources(QSize size);
    QImage render(iris::ScenePtr document, iris::CameraNodePtr camera, QSize size);
    /// The preview scene every thumbnail shares: ambient, key light, rim light, camera.
    static iris::ScenePtr buildPreviewScene(iris::CameraNodePtr &cameraOut);

    std::weak_ptr<jahshaka::engine::Engine> mEngine;
    jahshaka::engine::View  *mView  = nullptr;
    jahshaka::engine::Scene *mScene = nullptr;
    std::unique_ptr<SceneMirror> mMirror;
    iris::MeshPtr mSphere;   // preview sphere, loaded once
};

#endif // ENGINETHUMBNAILRENDERER_H
