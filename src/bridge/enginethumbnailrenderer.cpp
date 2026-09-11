#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "bridge/secondarysurfacetonemap.h"
#include "bridge/enginethumbnailrenderer.h"

#include <QColor>
#include <QtMath>
#include <cstring>

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

Colour EngineThumbnailRenderer::backgroundColour()
{
    // The legacy generator cleared to (25, 25, 25).
    return Colour(25 / 255.0f, 25 / 255.0f, 25 / 255.0f, 1.0f);
}

void EngineThumbnailRenderer::configureScene(Scene *scene)
{
    scene->setAmbient(Colour(0.45f, 0.45f, 0.45f), Colour(0.30f, 0.30f, 0.30f));
}

void EngineThumbnailRenderer::releaseSubject(bool)
{
    mSphere.reset();
}

bool EngineThumbnailRenderer::ensureResources(QSize size)
{
    auto engine = this->engine();
    if (!engine || size.width() <= 0 || size.height() <= 0) return false;

    if (!view()) {
        // The View must exist before the Scene (Engine.h: ORDER MATTERS), and
        // nothing else is going to make one for a thumbnail renderer: it owns
        // this one, and the base destroys it in release().
        View *own = engine->createOffscreenView("thumbs", unsigned(size.width()), unsigned(size.height()),
                                                backgroundColour());
        if (!own) return false;
        own->setEnabled(false);
        adoptView(own);
    }
    if (!engineScene() && !attach(view())) return false;
    if (view()->width() != unsigned(size.width()) || view()->height() != unsigned(size.height()))
        view()->resize(unsigned(size.width()), unsigned(size.height()));
    return true;
}

iris::ScenePtr EngineThumbnailRenderer::buildPreviewScene(iris::CameraNodePtr &cameraOut)
{
    auto scene = iris::Scene::create();
    scene->setSkyColor(QColor(25, 25, 25, 0));
    scene->setAmbientColor(QColor(190, 190, 190));
    scene->fogEnabled = false;
    scene->shadowEnabled = false;

    auto dlight = iris::LightNode::create();
    dlight->color = QColor(255, 255, 240);
    dlight->intensity = 0.76f;
    dlight->setLightType(iris::LightType::Directional);
    dlight->setName("Key Light");
    dlight->setLocalRot(iris::Quat::fromEulerAngles(45, 45, 0));
    dlight->setShadowMapType(iris::ShadowMapType::None);
    scene->rootNode->addChild(dlight);

    auto plight = iris::LightNode::create();
    plight->color = QColor(210, 210, 255);
    plight->intensity = 0.47f;
    plight->setLightType(iris::LightType::Point);
    plight->setName("Rim Light");
    plight->setLocalPos(iris::Vec3(0, 0, -3));
    plight->setShadowMapType(iris::ShadowMapType::None);
    scene->rootNode->addChild(plight);

    cameraOut = iris::CameraNode::create();
    cameraOut->setLocalPos(iris::Vec3(1, 1, 5));
    cameraOut->lookAt(iris::Vec3(0, 0.5f, 0));
    cameraOut->update(0);
    return scene;
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
    if (!subject) return QImage();
    iris::CameraNodePtr cam;
    auto document = buildPreviewScene(cam);
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
        if (!mSphere) return QImage();
    }
    auto node = iris::MeshNode::create();
    node->setMesh(mSphere);
    node->setMaterial(material ? material : iris::DefaultMaterial::create().staticCast<iris::Material>());

    iris::CameraNodePtr cam;
    auto document = buildPreviewScene(cam);
    document->rootNode->addChild(node);
    const float dist = 1.2f / qTan(qDegreesToRadians(cam->angle / 2.0f));
    cam->setLocalPos(iris::Vec3(0, 0, dist));
    cam->lookAt(iris::Vec3(0, 0, 0));
    cam->update(0);
    return render(document, cam, size);
}

QImage EngineThumbnailRenderer::render(iris::ScenePtr document, iris::CameraNodePtr camera, QSize size)
{
    auto engine = this->engine();
    if (!engine || !ensureResources(size)) return QImage();

    document->refresh();
    // Reproducible warm-up: the engine-side simulation (particles, shader
    // time) steps on the default grid for a thumbnail regardless of what the
    // last host left in force (A4.2 review S3).
    engine->setFixedFrameDelta(jahshaka::engine::Engine::kDefaultFrameDelta);
    mirror()->setSource(document);
    mirror()->sync();
    // Background from the document's sky (buildPreviewScene's 25,25,25 for asset
    // previews; a real scene's sky colour matches the viewport). Ambient stays the
    // renderer's own studio lighting — deliberately not applyEnvironment.
    mirror()->applySky(view());
    mirror()->applyCamera(camera, view());
    // THE SECONDARY-SURFACE TONEMAP (bridge/secondarysurfacetonemap.h). A
    // thumbnail is a photograph of a world the viewport grades filmically; raw
    // linear radiance clipped to 8 bits made every brightly-lit asset a white
    // card. Deterministic (fixed exposure), so a thumbnail is still a
    // reproducible picture of its content.
    secondaryfx::apply(view(), true);

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

    // Nothing leaks across requests: drop every mirrored node, mesh and material.
    mirror()->setSource(nullptr);

    return ok ? toQImage(img) : QImage();
}
