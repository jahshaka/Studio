#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "bridge/secondarysurfacetonemap.h"
#include "bridge/enginethumbnailrenderer.h"

#include "irisgl/core/color.h"

#include <QColor>
#include <QCoreApplication>
#include <QDebug>
#include <QThread>
#include <QtMath>
#include <cstring>
#include <memory>
#include <utility>

#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/core/properties/property.h"
#include <QFileInfo>
#include "io/builtinmaterials.h"
#include "irisgl/mirror/scenemirror.h"
#include "bridge/offscreenrenderscope.h"
#include "bridge/previewenvironment.h"
#include "bridge/stableoffscreenrender.h"
#include "viewport/previewframing.h"

using namespace jahshaka::engine;

EngineThumbnailRenderer::EngineThumbnailRenderer(const std::shared_ptr<Engine> &engine)
    // One frame into a small texture, then emptied again: a worker pool would
    // be pure barrier cost (fps audit F3).
    //
    // NO POOL, not a pool of one (THREADING_ADOPTION_SPEC.md P5). This asked
    // for Tier::Utility until the hygiene phase, and 1 does not avoid the
    // barrier — Ogre spawns a thread and pays two syncs per parallel pass to
    // do the same serial work. Tier::MainThread reaches the backend as a
    // genuine 0, where mForceMainThread runs every pass inline
    // (OgreSceneManager.cpp:171, :4705-4717).
    : EnginePreviewScene(engine, "thumbs", sceneworkers::Tier::MainThread)
{
}

EngineThumbnailRenderer::~EngineThumbnailRenderer()
{
    // The base destructor cannot run the hooks (see enginepreviewscene.h).
    release();
}

// ---------------------------------------------------------------------------
// THE ONE RENDERER (THUMBS-1). See the header for what a second one cost.
// ---------------------------------------------------------------------------

namespace
{
/// The instance, its borrow flag, and nothing else. Main thread only — every
/// caller is (ThumbnailGenerator's tick, the import tails, the verbs).
std::unique_ptr<EngineThumbnailRenderer> gShared;
bool gLoaned = false;
}   // namespace

EngineThumbnailRenderer::Loan::Loan(Loan &&other) noexcept
    : mRenderer(other.mRenderer), mReason(std::move(other.mReason))
{
    other.mRenderer = nullptr;
}

EngineThumbnailRenderer::Loan &EngineThumbnailRenderer::Loan::operator=(Loan &&other) noexcept
{
    if (this != &other) {
        if (mRenderer) { mRenderer->clearSubject(); gLoaned = false; }
        mRenderer = other.mRenderer;
        mReason = std::move(other.mReason);
        other.mRenderer = nullptr;
    }
    return *this;
}

EngineThumbnailRenderer::Loan::~Loan()
{
    if (!mRenderer) return;
    // NOTHING OF THIS BORROWER'S SUBJECT REACHES THE NEXT ONE. render() already
    // clears the mirror on the way out, but a loan that never rendered (or one
    // whose render failed before the mirror was cleared) must not leave a
    // document, a mesh or a material pointer behind in the engine scene.
    mRenderer->clearSubject();
    mRenderer = nullptr;
    gLoaned = false;
}

EngineThumbnailRenderer::Loan
EngineThumbnailRenderer::borrow(const std::shared_ptr<Engine> &engine, const char *who)
{
    // MAIN THREAD ONLY, asserted (fix round F11). The Engine has one thread
    // affinity, the instance and its borrow flag are plain globals, and every
    // caller is on the UI thread by construction (the queue's tick, the import
    // tails, the verbs). A borrow from anywhere else is a bug in the caller,
    // and a silent race is the worst way to find out.
    Q_ASSERT(!qApp || QThread::currentThread() == qApp->thread());
    const QString caller = QString::fromUtf8(who ? who : "a thumbnail");
    if (!engine)
        return Loan(nullptr, QStringLiteral("%1: the engine is not running").arg(caller));
    if (gLoaned) {
        // REFUSED BY NAME, never aliased: the renderer has one Scene, one View
        // and one mirror, and a second render pushed into them mid-frame would
        // draw the first caller's subject into the second caller's tile.
        const QString why = QStringLiteral(
            "%1: the thumbnail renderer is busy with another render").arg(caller);
        qWarning("%s", qUtf8Printable(why));
        return Loan(nullptr, why);
    }
    // A new Engine (a restart, or a second boot in one test process) means the
    // old renderer's View and Scene belong to a dead Root: drop it first.
    if (gShared && gShared->engine() != engine) gShared.reset();
    if (!gShared) gShared.reset(new EngineThumbnailRenderer(engine));
    gLoaned = true;
    return Loan(gShared.get(), QString());
}

void EngineThumbnailRenderer::shutdown()
{
    if (gLoaned) {
        // A render is on the stack above us; destroying the object under it
        // would be the crash this class exists to stop.
        qWarning("EngineThumbnailRenderer::shutdown: a render is in flight — not destroying it");
        return;
    }
    gShared.reset();
}

bool EngineThumbnailRenderer::exists() { return gShared != nullptr; }

QImage EngineThumbnailRenderer::failed(const QString &why)
{
    mLastFailure = why;
    // A FAILED THUMBNAIL IS NEVER SILENT (THUMBS-1). The owner's two grey tiles
    // had one warning between them, from Qt, about a null pixmap — the render
    // that produced nothing said nothing at all.
    qWarning("thumbnail: %s", qUtf8Printable(why));
    return QImage();
}

void EngineThumbnailRenderer::clearSubject()
{
    // NOTHING OF ONE BORROWER'S SUBJECT REACHES THE NEXT — and that is a
    // SUBJECT, not the studio (PREVIEWENV-2 item a). This used to unbind the
    // document entirely, which is a much bigger hammer than the job: a
    // `setSource(nullptr)` destroys every cached mesh, material and TEXTURE
    // the mirror holds and pushes `setSky(SkyDesc())`, so the next tile
    // re-uploaded the 512x256 studio equirect and made the engine capture and
    // convolve it again — one upload, one capture and one convolution per
    // tile, for a sky that is the same session texture for every preview
    // surface in the process.
    //
    // Taking the subject OFF the document does the whole of what this is for:
    // the mirror drops the nodes, and reclaimUnused frees the meshes,
    // materials and textures nothing references any more on the next sync
    // (the sky texture is pinned by the mirror itself for as long as the sky
    // stands, which is exactly the state worth keeping).
    if (!mStudio || !mirror() || !engine()) return;
    bool removedAny = false;
    const QList<iris::SceneNodePtr> subjects = mStudio->rootNode->children();
    for (const iris::SceneNodePtr &child : subjects) {
        if (!child) continue;
        child->removeFromParent();
        removedAny = true;
    }
    if (removedAny) {
        mStudio->refresh();
        mirror()->sync();
    }
}

EngineThumbnailRenderer::Held EngineThumbnailRenderer::held() const
{
    Held out;
    if (const SceneMirror *m = mirror()) out.nodes = m->mirroredNodeCount();
    out.lastRenderMaterialBuilds = mLastRenderMaterialBuilds;
    return out;
}

Colour EngineThumbnailRenderer::backgroundColour()
{
    // THE FALLBACK BEHIND THE SKY, and only that (MATPREVIEW-ENV-1). The studio
    // environment draws a real sky quad over the whole frame, so this is what
    // shows where no sky pass ran at all (a failed upload, a headless view). It
    // is the environment's own mean radiance rather than the legacy 25-grey, so
    // the seam nobody should ever see is the right colour if they do.
    const float mean = previewenv::ambientSh()[0];
    return Colour(mean, mean, mean, 1.0f);
}

void EngineThumbnailRenderer::configureScene(Scene *scene)
{
    // ONE STUDIO for every preview surface (previewenv): the environment's nine
    // diffuse bands and its specular gain, instead of the flat hemisphere pair
    // that stood in for a sky.
    previewenv::pushAmbient(scene);
}

void EngineThumbnailRenderer::releaseSubject(bool sceneAlive)
{
    // THE STUDIO DOCUMENT COMES OUT OF THE ENGINE HERE, while everything is
    // still alive (this hook runs first in release(), before the mirror is
    // dropped and long before destroyScene). Since PREVIEWENV-2 the mirror
    // stays BOUND to it between renders, so this class is the one that has to
    // put it down.
    //
    // WHEN THE SCENE IS ALREADY GONE THERE IS NOTHING SAFE TO CALL, and the
    // fix round's suggestion — unbind anyway, because "setSource(nullptr)
    // guards its own engine calls" — does not hold at this tree: that function
    // dereferences the engine Scene unconditionally (scenemirror.cpp ~471-505:
    // destroyMesh on the wire meshes, setGrid, removeNode/destroyMesh/
    // destroyMaterial on the horizon and the GI-volume overlay, destroyMaterial
    // on the highlight, then releaseEntry for every entry). Calling it with the
    // Engine gone is a read-after-free where the present behaviour is one
    // warning: ~SceneMirror takes the document out of the dead manager and
    // iris::Scene::setGraphScene says so by design ("SceneMirror must unbind
    // (setSource(null)) before Engine::destroyScene()", scene.cpp:1083-1093),
    // dropping every stale handle instead of walking it.
    //
    // AND NO SHIPPING PATH REACHES IT: EngineHost::shutdown calls
    // EngineThumbnailRenderer::shutdown() (enginehost.cpp:583) BEFORE
    // mEngine.reset(), and ThumbnailGenerator::shutdown() does the same on the
    // window-close path — both with the Engine alive, so `sceneAlive` is true
    // and the unbind below is the one that runs. What is left is borrow()'s
    // "a new Engine" branch with the old one already expired (a second boot in
    // one test process); the real answer there is to let go of the renderer
    // before the Engine, which is what both shutdown calls do.
    if (sceneAlive && mirror() && mirror()->source() == mStudio && mStudio)
        mirror()->setSource(nullptr);
    mStudio.reset();
    mSphere.reset();
}

bool EngineThumbnailRenderer::ensureResources(QSize size)
{
    auto engine = this->engine();
    if (!engine) { failed(QStringLiteral("the engine is not running")); return false; }
    if (size.width() <= 0 || size.height() <= 0) {
        failed(QStringLiteral("a thumbnail was asked for at %1x%2")
                   .arg(size.width()).arg(size.height()));
        return false;
    }

    if (!view()) {
        // The View must exist before the Scene (Engine.h: ORDER MATTERS), and
        // nothing else is going to make one for a thumbnail renderer: it owns
        // this one, and the base destroys it in release().
        //
        // THE NAME IS FIXED AND THAT IS SAFE NOW: there is one renderer per
        // process (borrow()), so nothing else can be holding "thumbs". When
        // this DID fail — a second instance — it failed silently, which is the
        // defect THUMBS-1 fixed; it now says what the engine said.
        View *own = engine->createOffscreenView("thumbs", unsigned(size.width()), unsigned(size.height()),
                                                backgroundColour());
        if (!own) {
            failed(QStringLiteral("the offscreen view could not be created: %1")
                       .arg(QString::fromStdString(engine->lastError())));
            return false;
        }
        own->setEnabled(false);
        adoptView(own);
    }
    if (!engineScene() && !attach(view())) {
        failed(QStringLiteral("the preview scene could not be created: %1")
                   .arg(QString::fromStdString(engine->lastError())));
        return false;
    }
    if (view()->width() != unsigned(size.width()) || view()->height() != unsigned(size.height()))
        view()->resize(unsigned(size.width()), unsigned(size.height()));
    return true;
}

iris::ScenePtr EngineThumbnailRenderer::studioDocument(iris::CameraNodePtr &cameraOut)
{
    // THE STUDIO, AND NOTHING ELSE (MATPREVIEW-ENV-1, owner review R9). This
    // carried a hand rig of its own — a 0.76 key and a 0.47 blue-white rim over
    // a near-black 25-grey sky — which was a THIRD lighting setup beside the
    // material preview dock's and the editor's: a material's tile could not
    // agree with its own preview, because nothing about the two rigs was the
    // same. Both lights are gone. `previewenv::apply` binds the generated
    // studio environment, the exposure a preview grades at (read back out by
    // render(), below, through the document's own `exposure` field) and the
    // three things a photograph of an asset never has: a sun disc, fog,
    // shadows.
    // ONE DOCUMENT FOR THE WHOLE SESSION (PREVIEWENV-2 item a). It used to be
    // a fresh iris::Scene per request, and a fresh document means a fresh
    // bind, and a bind is the mirror's full teardown: the studio sky was
    // uploaded, captured and convolved again for every tile. The sky, the
    // exposure and the three things a preview never has do not vary by
    // subject, so neither does the document — only what is parented into it.
    // (The CAMERA is per request: framing is the subject's.)
    if (!mStudio) {
        mStudio = iris::Scene::create();
        previewenv::apply(mStudio);
    }

    cameraOut = iris::CameraNode::create();
    cameraOut->setLocalPos(iris::Vec3(1, 1, 5));
    cameraOut->lookAt(iris::Vec3(0, 0.5f, 0));
    cameraOut->update(0);
    return mStudio;
}

// THE FRAMING (smoke S6, the second half of the owner's Assets report).
//
// This used to merge the subject's per-mesh bounding SPHERES — starting from a
// unit sphere at the ORIGIN, which no subject need be near — and then park the
// camera at `(0, centre.y, dist)`, i.e. on the world's Z axis whatever the
// subject's x/z. A model authored away from its origin (the owner's
// ruined_city_free_5.glb) was therefore framed for a box that also had to
// contain the origin, and viewed from off to one side: a textured but tiny,
// off-centre tile. The preview and the editor's F already measured the world
// AABB, so this asks the one function they ask (viewport/previewframing.h) and
// backs the camera straight out of the box's centre.
static void frameCamera(iris::CameraNodePtr cam, iris::SceneNodePtr subject)
{
    const preview::Framing framing = preview::frameSubject(subject, cam->effectiveFovDegrees());
    cam->nearClip = framing.nearClip;
    cam->farClip = framing.farClip;
    cam->setLocalPos(framing.target + iris::Vec3(0, 0, framing.distance));
    cam->lookAt(framing.target);
    cam->update(0);
}

// THE THIRD MATERIAL-CONSTRUCTION PATH, retired (hygiene lane, 2026-09-09).
//
// This built an iris::DefaultMaterial out of the legacy Blinn fields —
// diffuse/specular/ambient/shininess plus the diffuse, specular and normal
// maps — and IGNORED every PBR field the importer reads. A thumbnail of a
// glTF model was therefore rendered from a lossy back-conversion of data the
// document already had exactly: baseColorFactor, metallic, roughness, the
// base-colour/metallic/roughness/emissive maps, and KHR_materials_unlit all
// went to the floor, so a thumbnail could not agree with the asset preview,
// the viewport, or the model as imported.
//
// It is the third copy of a conversion the tree had already reduced to one:
// AssetHelper's import path and SceneEditService both call
// BuiltinMaterials::fromMeshData, and the comment at assethelper.cpp:236
// records what a SECOND copy already cost (emissiveIntensity dropped on every
// import). There is nothing in a thumbnail that wants a different answer from
// the scene, so it now asks the same function.
iris::MaterialPtr EngineThumbnailRenderer::previewMaterialForMeshData(const iris::MeshMaterialData &data)
{
    return iris::MaterialPtr(BuiltinMaterials::fromMeshData(data));
}

// (previewMaterialFor and previewMaterials are GONE with HLMS_ADOPTION P4b.
// They existed to convert an iris::CustomMaterial's Property rows into a
// DefaultMaterial so a shader-graph material showed its real look in a
// thumbnail instead of a grey stand-in. There is no CustomMaterial any more —
// every material the document holds is a PbrMaterial, which the mirror renders
// natively — so the conversion has nothing left to convert.)

QImage EngineThumbnailRenderer::renderNode(iris::SceneNodePtr subject, QSize size)
{
    if (!subject) return failed(QStringLiteral("there is no subject node to render"));
    iris::CameraNodePtr cam;
    auto document = studioDocument(cam);
    document->rootNode->addChild(subject);
    frameCamera(cam, subject);
    QImage img = render(document, cam, size);
    subject->removeFromParent();   // the caller keeps its node; the document dies here
    return img;
}

QImage EngineThumbnailRenderer::renderMaterial(iris::MaterialPtr material, QSize size)
{
    if (!mSphere) {
        mSphere = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/sphere.obj"));
        if (!mSphere)
            return failed(QStringLiteral("the preview sphere (app/content/primitives/sphere.obj) "
                                         "could not be loaded"));
    }
    auto node = iris::MeshNode::create();
    node->setMesh(mSphere);
    node->setMaterial(material ? material : iris::DefaultMaterial::create().staticCast<iris::Material>());

    iris::CameraNodePtr cam;
    auto document = studioDocument(cam);
    document->rootNode->addChild(node);
    // THE SAME FRAMING THE DOCK USES (previewframing.h), at the tile's own
    // aspect: the sphere fills a fixed fraction of the SMALLER dimension, so a
    // non-square tile keeps it whole. (This was `1.2 / tan(fov/2)` — the
    // vertical angle only, with the sphere's radius assumed to be exactly 1.)
    node->update(0);
    const float aspect = size.height() > 0 ? float(size.width()) / float(size.height()) : 1.0f;
    cam->setAspectRatio(aspect);
    const iris::AABB bounds = preview::worldBoundingBox(node);
    const float radius = preview::subjectRadius(node, bounds);
    const float dist = preview::previewDistance(radius, cam->effectiveFovDegrees(), aspect);
    preview::clipPlanesForFraming(dist, radius, cam->nearClip, cam->farClip);
    // FROM THE ENVIRONMENT'S OWN VIEWPOINT (previewenv::viewDirection), not
    // straight down +Z. The studio is not isotropic — the lamps are over one
    // shoulder and the dark wall is opposite — so a tile shot from a different
    // side of it is a different picture of the same material: measured 108.1
    // against the dock's 118.4 on an 18 % grey ball, ten codes apart, before
    // this line. The two agree to under one code with it.
    float v[3];
    previewenv::viewDirection(v);
    cam->setLocalPos(iris::Vec3(v[0] * dist, v[1] * dist, v[2] * dist));
    cam->lookAt(iris::Vec3(0, 0, 0));
    cam->update(0);
    return render(document, cam, size);
}

QImage EngineThumbnailRenderer::render(iris::ScenePtr document, iris::CameraNodePtr camera, QSize size)
{
    auto engine = this->engine();
    if (!engine) return failed(QStringLiteral("the engine is not running"));
    if (!ensureResources(size)) return QImage();   // ensureResources said why
    mLastFailure.clear();

    document->refresh();
    // Reproducible warm-up: the engine-side simulation (particles, shader
    // time) steps on the default grid for a thumbnail regardless of what the
    // last host left in force (A4.2 review S3).
    engine->setFixedFrameDelta(jahshaka::engine::Engine::kDefaultFrameDelta);
    // BOUND ONCE (PREVIEWENV-2 item a): the studio document is the same
    // document every time, and `setSource` is a full teardown-and-rebind even
    // when the answer is the same — the whole cost this lane is about.
    if (mirror()->source() != document) mirror()->setSource(document);
    mirror()->sync();
    // THE SUBJECT'S OWN COST, captured where the counter means something: the
    // mirror zeroes its material-build counter at the top of every sync, and
    // this is the sync that mirrored the subject (thumbnails.studio_env's
    // growth arm reads it through held()).
    mLastRenderMaterialBuilds = mirror()->materialBuildCount();
    // The sky — the studio environment for every request (studioDocument).
    // NOT applyEnvironment, deliberately: the ambient is the environment's own
    // (configureScene), and a thumbnail takes no shadow, GI or post row from a
    // document it never had.
    mirror()->applySky(view());
    mirror()->applyCamera(camera, view());
    // THE SECONDARY-SURFACE TONEMAP (bridge/secondarysurfacetonemap.h). A
    // thumbnail is a photograph of a world the viewport grades filmically; raw
    // linear radiance clipped to 8 bits made every brightly-lit asset a white
    // card. Deterministic (fixed exposure), so a thumbnail is still a
    // reproducible picture of its content.
    //
    // AT THE DOCUMENT'S OWN EXPOSURE (SS1, 2026-09-13). This used to take the
    // header's default and therefore ignored the World's exposure completely:
    // a scene the user had regraded produced thumbnails of the ungraded world.
    // Asset previews are built at iris::Scene's default, so their pictures move
    // only when that default does; a regraded document moves TOWARDS the
    // viewport.
    //
    // THE UNIT (EXPOSURE-1): the document holds STOPS and secondaryfx::apply
    // takes the post chain's `E`, so this is the same one conversion the mirror
    // does — iris::lens, in both places, and nowhere else. The thumbnail grade
    // itself is unchanged: it is the chain's fixed-exposure form, which is
    // exactly what MANUAL exposure now is on screen, so a manually exposed
    // world and its thumbnails agree by construction rather than by a derived
    // constant.
    secondaryfx::apply(view(), true, iris::lens::exposureStopsToChain(document->exposure));

    view()->setEnabled(true);
    // The editor does not pay for a thumbnail (fps audit F5): renderOneFrame
    // draws every enabled view, so without this each thumbnail also redrew the
    // whole editor twice and blocked twice on the display's vsync — which is
    // what made a thumbnail sweep feel like a frozen application.
    OffscreenRenderScope quiet(engine.get());
    // Two frames, plus however many more the texture load-request counter says
    // this picture still owes (THREADING_ADOPTION_SPEC.md P2 item 4). Textures
    // are streamed since P2, so "two frames" alone is no longer a guarantee that
    // everything the thumbnail draws is resident — see bridge/
    // stableoffscreenrender.h for upstream's recipe and why the minimum stays 2.
    renderStableFrames(engine.get());
    Image img;
    const bool ok = view()->readPixels(img);
    view()->setEnabled(false);

    // Nothing leaks across requests: the SUBJECT comes off the studio document
    // and the mirror reclaims its meshes, materials and textures. The studio
    // itself — the sky, its capture and its convolution — stays (clearSubject).
    clearSubject();

    if (!ok) return failed(QStringLiteral("the offscreen view produced no pixels: %1")
                               .arg(QString::fromStdString(engine->lastError())));
    QImage result = toQImage(img);
    if (result.isNull())
        return failed(QStringLiteral("the rendered image was empty (%1x%2)")
                          .arg(img.width).arg(img.height));
    return result;
}
