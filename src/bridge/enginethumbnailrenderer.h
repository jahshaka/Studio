#ifndef ENGINETHUMBNAILRENDERER_H
#define ENGINETHUMBNAILRENDERER_H

// EngineThumbnailRenderer — renders asset previews (meshes, materials) through
// the engine on the MAIN thread, into an offscreen View on a dedicated Scene.
//
// This replaces ThumbnailGenerator's legacy RenderThread (own QOpenGLContext on
// a worker thread) in engine mode: the engine has one thread affinity, so
// thumbnails are drawn synchronously, one per tick, by ThumbnailGenerator's
// queue. The document for a preview is an ordinary iris::Scene (GL-free) that
// SceneMirror pushes into the thumbs engine Scene, exactly like the viewport.
//
// An EnginePreviewScene, with one difference from the on-screen previews: it
// OWNS its View. Nothing shows a thumbnail renderer, so there is no widget to
// hand it a native window — it makes a small offscreen one, keeps it disabled
// between requests, resizes it when a request asks for another size, and the
// base destroys it in release().
//
// ONE PER PROCESS, BORROWED — AND WHY (THUMBS-1, the owner's 2026-09-18 report:
// two GLB imports, two grey tiles, and in his log `engine: Warning: View
// 'thumbs' already exists` beside `QPixmap::scaleHeight: Pixmap is a null
// pixmap`). The View name is a fixed string, the Engine refuses a duplicate
// name (OgreEngine::viewNameTaken), and this class used to be constructed
// freely: ThumbnailGenerator kept ONE alive for the whole session from the
// first material/shader thumbnail, and every TEMPORARY instance after that —
// assetthumb::renderObject, `assets.refreshThumbnail` — then failed to create
// its View, produced a null image and said nothing. Order-dependent and
// therefore invisible in a fresh session: fine until the first material
// thumbnail, broken for every model import afterwards.
//
// The fix is OWNERSHIP, not naming: a unique name per instance would work
// until the pin's 8-bit pass field overflowed on uniquely named captures again
// (SHADERCACHE-2), and two live thumbnail scenes are two voxelised worlds
// nobody asked for. So the constructor is private, `borrow()` hands out the
// one instance, and a borrow while a render is in flight is REFUSED BY NAME
// rather than aliasing the state of the render already running. Between
// borrowers the loan clears the mirror's source, so no document, mesh or
// material pointer survives into the next caller's picture.
//
// Studio-side code: includes iris (Qt) and the engine abstraction. Never Ogre.
#include <memory>
#include <QImage>
#include <QSize>
#include <QString>
#include "irisgl/irisglfwd.h"
#include "bridge/enginepreviewscene.h"   // brings jahshaka/engine/Engine.h

class SceneMirror;
namespace iris { struct MeshMaterialData; }

class EngineThumbnailRenderer : public EnginePreviewScene
{
public:
    ~EngineThumbnailRenderer() override;

    /// A loan of THE process-wide renderer, for the length of one scope.
    ///
    /// Empty (`!loan`) when there is no engine, or when another borrower holds
    /// it — `reason()` says which, in words meant for a log line and for a
    /// verb's `{ok:false, reason}`. Move-only: a loan is a lock.
    class Loan
    {
    public:
        Loan() = default;
        Loan(Loan &&other) noexcept;
        Loan &operator=(Loan &&other) noexcept;
        Loan(const Loan &) = delete;
        Loan &operator=(const Loan &) = delete;
        ~Loan();

        explicit operator bool() const { return mRenderer != nullptr; }
        EngineThumbnailRenderer *get() const { return mRenderer; }
        EngineThumbnailRenderer *operator->() const { return mRenderer; }
        EngineThumbnailRenderer &operator*() const { return *mRenderer; }
        /// Why the loan is empty (empty string when it is not).
        QString reason() const { return mReason; }

    private:
        friend class EngineThumbnailRenderer;
        Loan(EngineThumbnailRenderer *renderer, QString reason)
            : mRenderer(renderer), mReason(std::move(reason)) {}

        EngineThumbnailRenderer *mRenderer = nullptr;
        QString mReason;
    };

    /// Borrow the one renderer. `who` names the caller in the refusal message
    /// and in the log — "assets.refreshThumbnail", "the import tail" — so a
    /// failed thumbnail never has to be traced back from a grey tile.
    static Loan borrow(const std::shared_ptr<jahshaka::engine::Engine> &engine, const char *who);

    /// Destroy the shared renderer while the Engine is still alive (EngineHost::
    /// shutdown, ThumbnailGenerator::shutdown). Safe with nothing borrowed and
    /// safe to call twice; refuses while a loan is out (there is a render on
    /// the stack) and says so in the log.
    static void shutdown();

    /// True while the shared renderer exists. Tests and diagnostics only.
    static bool exists();

    /// Renders `subject` (a mesh node or a hierarchy of them) framed by its
    /// world bounds, in the studio environment. Null image if the engine is
    /// gone or the subject has no geometry.
    QImage renderNode(iris::SceneNodePtr subject, QSize size);
    /// Renders `material` on the preview sphere (app/content/primitives/sphere.obj).
    QImage renderMaterial(iris::MaterialPtr material, QSize size);

    /// WHY the last render returned a null image, in words. Empty after a
    /// render that produced pixels. A THUMBNAIL THAT FAILS IS NEVER SILENT
    /// (THUMBS-1): every null-returning path here sets this and logs it.
    QString lastFailure() const { return mLastFailure; }

    /// Background the offscreen view is cleared to (what "not the background" means).
    static jahshaka::engine::Colour backgroundColour();

    /// Preview material for an imported model's assimp material data: the colours
    /// AND the diffuse/specular/normal maps — the same material the asset preview
    /// viewer shows. (Thumbnails used to drop the textures and render grey.)
    static iris::MaterialPtr previewMaterialForMeshData(const iris::MeshMaterialData &data);

protected:
    void configureScene(jahshaka::engine::Scene *scene) override;
    /// Nothing: the mirror's source is the ONE studio document, bound once and
    /// emptied of its subject after every render (clearSubject), so nothing
    /// leaks between thumbnails and the studio's own sky is not re-uploaded
    /// for each one.
    void configureMirror(SceneMirror *mirror) override { (void)mirror; }
    void releaseSubject(bool sceneAlive) override;

private:
    /// PRIVATE: the one instance is borrow()'s to make. A second renderer
    /// cannot be constructed — it is not a rule, it is the type.
    explicit EngineThumbnailRenderer(const std::shared_ptr<jahshaka::engine::Engine> &engine);

    bool ensureResources(QSize size);
    QImage render(iris::ScenePtr document, iris::CameraNodePtr camera, QSize size);
    /// THE preview document, built on first use and kept for the session: the
    /// ONE studio environment (bridge/previewenvironment.h), and a fresh
    /// camera per request (framing is the subject's). No light node — the
    /// environment is the lighting.
    ///
    /// It is kept because a document SWAP is what made every tile re-upload
    /// the studio sky: `SceneMirror::setSource` drops every cached texture and
    /// pushes an empty sky, so binding a new document per request meant one
    /// 512x256 upload, one environment capture and one convolution per tile
    /// for a sky that never changes (PREVIEWENV-2 item a).
    iris::ScenePtr studioDocument(iris::CameraNodePtr &cameraOut);
    /// Records `why`, logs it, and returns a null image (the one failure exit).
    QImage failed(const QString &why);
    /// Drop everything the last borrower put in the scene — the SUBJECT, not
    /// the studio. Runs when a loan ends, so nothing of one caller's subject
    /// is alive for the next one, and after every render for the same reason.
    void clearSubject();

    iris::ScenePtr mStudio;  // the one preview document (studioDocument)
    iris::MeshPtr mSphere;   // preview sphere, loaded once
    QString mLastFailure;
};

#endif // ENGINETHUMBNAILRENDERER_H
