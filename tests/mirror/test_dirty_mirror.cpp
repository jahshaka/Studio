// mirror.dirty_equals_full — THE DIFFERENTIAL TEST, and the reason the full
// walk still exists at all.
//
// SPECS/DIRTY_SET_MIRROR_SPEC.md §7.1 (owner option A). The mirror no longer
// asks every object in the document "did you change?" once a frame — each
// object says so when it changes, into a list its scene keeps, and the mirror
// handles the list. That design fails in exactly ONE way: a change that forgets
// to report itself never reaches the screen, silently, and no pixel test
// written against the scenes we happen to have will catch it.
//
// This is the test that catches it. For every class of mutation the document
// supports it does the same three things:
//
//   1. make the change,
//   2. run the DIRTY sync (what ships),
//   3. run the WHOLE WALK behind it as an ORACLE and demand it pushes NOTHING.
//
// Step 3 is `SceneMirror::verifyAgainstFullWalk()`: the same visitNode every
// latch lives in, over every node, with every material re-fingerprinted from
// its own fields rather than trusted to its revision counter. A single push
// means the dirty sync left the engine in a different state from the one the
// full walk would have produced — i.e. a missing mark — and the mirror names
// the object and the latch in the log before this line fails.
//
// THE F6 CONTRACT gets its own case, by name (SMOKE_FIX S12, and the lead's
// note on the spec): since ENGINE-3 the engine's visibility walk descends only
// into ENGINE-owned children, so the mirror is the SOLE pusher of a document
// node's effective visibility. "Hide a subtree root after adoption" must drop
// the GI bit of every descendant — a Visibility mark that stops at the node it
// was raised on leaves a hidden model's children drawn and voxelised with no
// backstop anywhere in the system.
#include <QColor>

#include "bridge/previewmesh.h"
#include <QGuiApplication>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

/// One document + one engine scene + one mirror, with every node kept alive.
struct Rig {
    Engine *engine = nullptr;
    Scene *target = nullptr;
    iris::ScenePtr doc;
    std::unique_ptr<SceneMirror> mirror;

    iris::SceneNodePtr group;          ///< a subtree root with children
    iris::MeshNodePtr  cube;           ///< under `group`
    iris::MeshNodePtr  deep;           ///< under `cube` — two levels down
    iris::MeshNodePtr  loner;          ///< straight under the root
    iris::LightNodePtr light;
    iris::CameraNodePtr camera;
    iris::ParticleSystemNodePtr emitter;
    iris::DecalNodePtr decal;
    std::vector<iris::SceneNodePtr> keep;

    iris::MeshPtr cubeMesh, planeMesh;
};

iris::MeshNodePtr makeMesh(Rig &r, const char *name, const iris::MeshPtr &mesh)
{
    auto m = iris::MeshNode::create();
    m->setName(QLatin1String(name));
    m->setMesh(mesh);
    m->setMaterial(iris::PbrMaterial::create());
    r.keep.push_back(m);
    return m;
}

void build(Rig &r, Engine *engine)
{
    r.engine = engine;
    r.target = engine->createScene("dirty");
    r.doc = iris::Scene::create();
    r.cubeMesh  = previewmesh::load(":assets/models/cube.obj");
    r.planeMesh = previewmesh::load(":assets/models/plane.obj");

    r.group = iris::SceneNode::create();
    r.group->setName(QStringLiteral("group"));
    r.doc->getRootNode()->addChild(r.group);
    r.keep.push_back(r.group);

    r.cube = makeMesh(r, "cube", r.cubeMesh);
    r.group->addChild(r.cube);
    r.deep = makeMesh(r, "deep", r.cubeMesh);
    r.cube->addChild(r.deep);
    r.loner = makeMesh(r, "loner", r.cubeMesh);
    r.doc->getRootNode()->addChild(r.loner);

    r.light = iris::LightNode::create();
    r.light->setName(QStringLiteral("lamp"));
    r.light->lightType = iris::LightType::Point;
    r.doc->getRootNode()->addChild(r.light);
    r.keep.push_back(r.light);

    r.camera = iris::CameraNode::create();
    r.camera->setName(QStringLiteral("cam"));
    r.doc->getRootNode()->addChild(r.camera);
    r.keep.push_back(r.camera);

    r.emitter = iris::ParticleSystemNode::create();
    r.emitter->setName(QStringLiteral("emitter"));
    r.doc->getRootNode()->addChild(r.emitter);
    r.keep.push_back(r.emitter);

    r.decal = iris::DecalNode::create();
    r.decal->setName(QStringLiteral("decal"));
    r.doc->getRootNode()->addChild(r.decal);
    r.keep.push_back(r.decal);

    r.doc->getRootNode()->applyStaticDefaults();

    r.mirror.reset(new SceneMirror(r.target));
    // THE VERIFIER OFF. It would HEAL every miss this suite exists to find —
    // 64 nodes a sync over a ten-node scene is a full re-read every frame, so
    // a missing mark would be pushed by the safety net and the oracle below
    // would then find nothing to report. The oracle IS this suite's verifier.
    r.mirror->setVerifierBudget(0);
    r.mirror->setSource(r.doc);
    r.mirror->sync();          // the adopting FULL walk
    r.mirror->sync();          // ...and one settling sync
}

/// The whole point: make a change, sync the way the app syncs, then run the
/// full walk as an oracle and demand it has nothing to do.
bool differential(Rig &r, const char *what, const std::function<void()> &mutate)
{
    r.mirror->sync();                       // settle
    const quint64 before = r.mirror->verifierCatchCount();
    mutate();
    r.mirror->sync();                       // THE DIRTY SYNC — what ships
    const quint64 late = r.mirror->verifyAgainstFullWalk();
    const bool ok = late == 0 && r.mirror->verifierCatchCount() == before + late;
    if (ok) std::printf("ok:   %-38s dirty %3llu visited %3d, the full walk after it pushes nothing\n",
                        what, (unsigned long long)r.mirror->dirtyNodeCount(),
                        r.mirror->visitedCount());
    else {
        std::printf("FAIL: %-38s the full walk after the dirty sync made %llu push(es)\n",
                    what, (unsigned long long)late);
        ++failures;
    }
    // A second oracle run must always be clean: the first one healed whatever
    // it found, so a repeat that still pushes is a latch that never settles.
    const quint64 again = r.mirror->verifyAgainstFullWalk();
    if (again) {
        std::printf("FAIL: %-38s ...and it does NOT settle (%llu more)\n",
                    what, (unsigned long long)again);
        ++failures;
    }
    return ok;
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_dirty_mirror-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("dirty", 64, 64, Colour(0, 0, 0));
    CHECK(view != nullptr, "offscreen view (the graph device)");
    if (!view) return 1;

    Rig r;
    build(r, engine.get());
    view->setScene(r.target);

    // ---- a STILL frame ----------------------------------------------------
    r.mirror->sync();
    CHECK(r.mirror->visitedCount() == 0, "a still sync visits nothing");
    CHECK(r.mirror->dirtyNodeCount() == 0, "...because the document reported nothing");
    CHECK(r.mirror->verifyAgainstFullWalk() == 0,
          "...and the full walk over the whole scene agrees with it");

    std::printf("  -- one mutation class at a time --\n");

    // ---- TRANSFORMS -------------------------------------------------------
    differential(r, "setLocalPos", [&] { r.cube->setLocalPos(iris::Vec3(1, 0, 0)); });
    differential(r, "setLocalRot", [&] {
        r.cube->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(0, 45, 0)));
    });
    differential(r, "setLocalScale", [&] { r.cube->setLocalScale(iris::Vec3(2, 2, 2)); });
    differential(r, "setGlobalPos", [&] { r.loner->setGlobalPos(iris::Vec3(0, 3, 0)); });
    differential(r, "setGlobalPosRot (the physics write-back)", [&] {
        r.loner->setGlobalPosRot(iris::Vec3(0, 4, 0), iris::Quat());
    });
    differential(r, "setGlobalTransform", [&] {
        r.loner->setGlobalTransform(iris::Mat4());
    });
    differential(r, "rotate()", [&] {
        r.cube->rotate(iris::Quat::fromEulerAngles(iris::Vec3(0, 10, 0)));
    });

    // ---- VISIBILITY, and the F6 contract ----------------------------------
    differential(r, "setVisible on a LEAF", [&] { r.deep->setVisible(false); });
    differential(r, "setVisible back", [&] { r.deep->setVisible(true); });
    // THE NAMED CASE (SMOKE_FIX S12). Hiding a subtree ROOT after adoption must
    // reach every descendant: the mirror is the only thing that pushes their
    // effective visibility, and a descendant it does not visit stays drawn AND
    // stays in the voxel bounce.
    differential(r, "hide a subtree ROOT after adoption", [&] { r.group->setVisible(false); });
    {
        CHECK(r.mirror->engineNode(r.deep.data()) != 0,
              "F6: the descendant still has an engine node");
        CHECK(r.mirror->pushedVisibility(r.deep.data()) == 0,
              "F6: hiding the subtree ROOT pushed a descendant TWO levels down HIDDEN");
        CHECK(!r.deep->isVisibleInScene(), "F6: ...which is what the document says too");
    }
    differential(r, "show the subtree root again", [&] { r.group->setVisible(true); });
    CHECK(r.mirror->pushedVisibility(r.deep.data()) == 1,
          "F6: showing it brings the descendant back");
    // ...and the differential case: a descendant the USER hid stays hidden when
    // its ancestor is shown again (each node's own flag is the user's).
    r.deep->setVisible(false);
    r.group->setVisible(false);
    r.mirror->sync();
    differential(r, "show an ancestor over a self-hidden child", [&] { r.group->setVisible(true); });
    CHECK(r.mirror->pushedVisibility(r.deep.data()) == 0,
          "F6: a child the user hid itself stays hidden when its parent comes back");
    r.deep->setVisible(true);
    r.mirror->sync();

    // ---- FLAGS ------------------------------------------------------------
    differential(r, "setPickable", [&] { r.cube->setPickable(false); });
    differential(r, "setPickable back", [&] { r.cube->setPickable(true); });
    differential(r, "setShadowCastingEnabled", [&] { r.cube->setShadowCastingEnabled(false); });
    differential(r, "setLightMask", [&] { r.cube->setLightMask(0x3); });
    differential(r, "setPlanarReflector", [&] { r.loner->setPlanarReflector(true); });
    differential(r, "setGiBoundsExcluded", [&] { r.cube->setGiBoundsExcluded(true); });
    differential(r, "setCollisionEnabled", [&] { r.cube->setCollisionEnabled(true); });
    differential(r, "setName", [&] { r.cube->setName(QStringLiteral("cube-renamed")); });

    // ---- MOBILITY (and its subtree rule) ----------------------------------
    differential(r, "setMobility(Movable) on a subtree root", [&] {
        r.group->setMobility(iris::Mobility::Movable);
    });
    differential(r, "setMobility(Auto) back", [&] {
        r.group->setMobility(iris::Mobility::Auto);
    });
    differential(r, "_setMobility (undo's raw replay)", [&] {
        r.cube->_setMobility(iris::Mobility::Static);
    });

    // ---- STRUCTURE --------------------------------------------------------
    iris::SceneNodePtr second = iris::SceneNode::create();
    second->setName(QStringLiteral("second"));
    r.keep.push_back(second);
    differential(r, "addChild a new subtree root", [&] {
        r.doc->getRootNode()->addChild(second);
    });
    differential(r, "reparent a subtree under it", [&] { second->addChild(r.group); });
    differential(r, "reparent it back", [&] { r.doc->getRootNode()->addChild(r.group); });
    // A subtree INSERTED UNDER A HIDDEN PARENT: the inserted nodes must be
    // pushed hidden, and nothing in the document ever wrote their own flags.
    second->setVisible(false);
    r.mirror->sync();
    iris::MeshNodePtr rider = makeMesh(r, "rider", r.cubeMesh);
    differential(r, "insert a subtree under a HIDDEN parent", [&] { second->addChild(rider); });
    CHECK(r.mirror->engineNode(rider.data()) != 0, "the inserted node was adopted");
    CHECK(r.mirror->pushedVisibility(rider.data()) == 0,
          "F6: a node inserted under a hidden parent is pushed HIDDEN");
    differential(r, "insertChild at a position", [&] {
        second->insertChild(0, makeMesh(r, "inserted", r.cubeMesh));
    });
    differential(r, "removeChild", [&] { second->removeChild(rider); });
    {
        CHECK(r.mirror->engineNode(rider.data()) == 0,
              "the removed node's entry was released (the eviction list, not a sweep)");
    }
    second->setVisible(true);
    r.mirror->sync();

    // ---- CONTENT: mesh / material pointer swaps ---------------------------
    differential(r, "setMesh (a live mesh swap)", [&] { r.cube->setMesh(r.planeMesh); });
    differential(r, "setMesh back", [&] { r.cube->setMesh(r.cubeMesh); });
    differential(r, "setMaterial (a live material swap)", [&] {
        r.cube->setMaterial(iris::PbrMaterial::create());
    });
    differential(r, "setMesh(null) — the geometry goes", [&] { r.cube->setMesh(iris::MeshPtr()); });
    differential(r, "setMesh back again", [&] { r.cube->setMesh(r.cubeMesh); });

    // ---- MATERIAL PARAMETERS ----------------------------------------------
    //
    // A material edit moves NO NODE, so the change list cannot see one: the
    // material's own revision is what reaches the objects drawing it. Every
    // authored row of the material is driven, which is the material half of
    // "every latch the walk would push".
    {
        auto pbr = r.cube->getMaterial().dynamicCast<iris::PbrMaterial>();
        CHECK(!pbr.isNull(), "the cube carries a PbrMaterial to drive");
        if (pbr) {
            int covered = 0, bad = 0;
            for (iris::Property *row : pbr->properties) {
                if (!row) continue;
                const QVariant before = row->getValue();
                QVariant after;
                switch (row->type) {
                case iris::PropertyType::Bool:  after = !before.toBool(); break;
                case iris::PropertyType::Int:
                case iris::PropertyType::List:  after = before.toInt() + 1; break;
                case iris::PropertyType::Float: after = before.toFloat() + 0.137f; break;
                case iris::PropertyType::Color: {
                    const QColor c = before.value<QColor>();
                    after = QColor(c.red() ^ 0x5A, c.green() ^ 0x3C, c.blue() ^ 0x27, 255);
                    break;
                }
                default: continue;      // texture/file rows need a real image; mirror.scale drives those
                }
                r.mirror->sync();
                pbr->setValue(row->name, after);
                r.mirror->sync();
                const quint64 late = r.mirror->verifyAgainstFullWalk();
                if (late) {
                    std::printf("    MATERIAL ROW MISS: '%s' (%llu late pushes)\n",
                                qPrintable(row->name), (unsigned long long)late);
                    ++bad;
                } else {
                    ++covered;
                }
                r.mirror->verifyAgainstFullWalk();   // settle
            }
            std::printf("    material rows driven: %d clean, %d missed\n", covered, bad);
            CHECK(covered > 20, "the material's authoring surface is really being driven");
            CHECK(bad == 0, "EVERY authored material row reaches the engine through the revision");
        }
    }
    // ...and a TEXTURE bind, which goes through Material::addTexture.
    differential(r, "setTexture on the material", [&] {
        auto pbr = r.cube->getMaterial().dynamicCast<iris::PbrMaterial>();
        if (pbr) pbr->addTexture(QStringLiteral("u_baseColorMap"), iris::Texture2DPtr());
    });

    // ---- A MATERIAL DIES WHILE ANOTHER IS TOUCHED, IN ONE FRAME -----------
    //
    // THE USE-AFTER-FREE THE LEAD'S SECOND READ FOUND (R2 #1). The mirror's
    // per-material memo is keyed by RAW `iris::Material *`, and the material
    // question — "did any material in this process change?" — is answered by
    // dereferencing every key it holds. A material dropped by its last node is
    // free to die the moment the caller's reference goes, which is the ordinary
    // rebake/replace path: build a fresh material, assign it, let the old one
    // go at scope end. If anything else is touched in the same interval, the
    // memo is walked with a dead key in it.
    //
    // Run under ASan this case is the whole proof; without it, it still asserts
    // that the replacement really reaches the engine.
    {
        auto victimMat = iris::PbrMaterial::create();
        victimMat->setName(QStringLiteral("about-to-die"));
        r.loner->setMaterial(victimMat);
        r.mirror->sync();
        r.mirror->sync();
        CHECK(r.mirror->verifyAgainstFullWalk() == 0, "the doomed material settles first");

        // The rebake shape: a fresh material, built through setValue (so it
        // touches), assigned, and the old one's last reference dropped — all
        // before the next sync. Plus a touch on a DIFFERENT, living material in
        // the same interval, which is what makes the mirror ask the question.
        {
            auto fresh = iris::PbrMaterial::create();
            fresh->setName(QStringLiteral("the-replacement"));
            fresh->setValue(QStringLiteral("baseColor"), QColor(10, 200, 40));
            r.loner->setMaterial(fresh);
            if (auto other = r.cube->getMaterial().dynamicCast<iris::PbrMaterial>())
                other->setValue(QStringLiteral("roughness"), 0.63f);
        }
        victimMat.reset();               // the last reference: it is gone now
        r.mirror->sync();                // <- the frame that used to walk a dead key
        CHECK(r.mirror->verifyAgainstFullWalk() == 0,
              "R2 #1: a material freed while another is touched — the sync is clean");
        // ...and the replacement really landed, not just "did not crash".
        CHECK(r.mirror->engineMaterial(r.loner.data()) != 0,
              "R2 #1: the node draws its NEW material");
        CHECK(r.mirror->verifierCatchCount() == 0, "R2 #1: ...and nothing was missed");
    }

    // ---- A SKY LIGHT'S CHILDREN ARE ORDINARY NODES (R2 #6) ----------------
    //
    // The light branch used to RETURN for a Sky Light, children included: the
    // full walk therefore never reached them (and removeMissing released their
    // entries every frame) while the dirty pass, which reaches a marked node
    // directly, adopted them. Two modes disagreeing about one scene.
    {
        auto sky = iris::LightNode::create();
        sky->setName(QStringLiteral("sky"));
        sky->lightType = iris::LightType::Sky;
        r.doc->getRootNode()->addChild(sky);
        r.keep.push_back(sky);
        iris::MeshNodePtr underSky = makeMesh(r, "under-sky", r.cubeMesh);
        sky->addChild(underSky);
        r.mirror->sync();
        CHECK(r.mirror->engineNode(underSky.data()) != 0,
              "R2 #6: a Sky Light's child is mirrored");
        // The mode that used to drop it.
        r.mirror->requestFullWalk();
        r.mirror->sync();
        CHECK(r.mirror->engineNode(underSky.data()) != 0,
              "R2 #6: ...and the FULL walk keeps it (it used to release the entry)");
        CHECK(r.mirror->pushedVisibility(underSky.data()) == 1,
              "R2 #6: ...with its visibility pushed like any other node");
        differential(r, "hide a Sky Light (its child follows)",
                     [&] { sky->setVisible(false); });
        CHECK(r.mirror->pushedVisibility(underSky.data()) == 0,
              "R2 #6: hiding the Sky Light hid its child");
        sky->setVisible(true);
        r.mirror->sync();
    }

    // ---- SUBCLASS PARAMETERS (the Params mark) ----------------------------
    for (const char *key : { "intensity", "distance", "spotCutOff", "spotFalloff",
                             "rectWidth", "rectHeight", "doubleSided",
                             "accurate", "iconSize", "forwardShadingPriority" }) {
        const QString k = QLatin1String(key);
        const QVariant before = r.light->getPropertyValue(k);
        const QVariant after = before.typeId() == QMetaType::Bool
                                   ? QVariant(!before.toBool())
                                   : QVariant(before.toFloat() + 0.5f);
        differential(r, qPrintable(QStringLiteral("light.%1").arg(k)),
                     [&] { r.light->setPropertyValue(k, after); });
    }
    differential(r, "light.lightColor", [&] {
        r.light->setPropertyValue(QStringLiteral("lightColor"), QColor(30, 200, 90));
    });
    differential(r, "light.lightType (point -> spot)", [&] {
        r.light->setPropertyValue(QStringLiteral("lightType"), int(iris::LightType::Spot));
    });
    differential(r, "light.shadowMapType", [&] {
        r.light->setPropertyValue(QStringLiteral("shadowMapType"), int(iris::ShadowMapType::Soft));
    });
    differential(r, "light.shadowMapResolution", [&] {
        r.light->setPropertyValue(QStringLiteral("shadowMapResolution"), 512);
    });
    differential(r, "setLightType (the typed setter)", [&] {
        r.light->setLightType(iris::LightType::Directional);
    });

    for (const char *key : { "speed", "lifeLength", "particlesPerSecond", "particleScale",
                             "coneAngle", "turbulence", "gravityComplement" }) {
        const QString k = QLatin1String(key);
        const QVariant after = r.emitter->getPropertyValue(k).toFloat() + 0.25f;
        differential(r, qPrintable(QStringLiteral("emitter.%1").arg(k)),
                     [&] { r.emitter->setPropertyValue(k, after); });
    }
    for (const char *key : { "width", "height", "depth", "metalness", "roughness" }) {
        const QString k = QLatin1String(key);
        const QVariant after = r.decal->getPropertyValue(k).toFloat() + 0.3f;
        differential(r, qPrintable(QStringLiteral("decal.%1").arg(k)),
                     [&] { r.decal->setPropertyValue(k, after); });
    }
    for (const char *key : { "angle", "nearClip", "farClip", "orthoSize",
                             "focusDistance" }) {
        const QString k = QLatin1String(key);
        const QVariant after = r.camera->getPropertyValue(k).toFloat() + 1.5f;
        differential(r, qPrintable(QStringLiteral("camera.%1").arg(k)),
                     [&] { r.camera->setPropertyValue(k, after); });
    }

    // ---- THE TYPED SETTERS (R2 #2 / #9) -----------------------------------
    //
    // setPropertyValue is not the only way into these nodes: every one of them
    // has typed setters the panels and the API call directly, and thirteen on
    // the emitter alone reported nothing (the emitter panel drives two of them
    // live and on undo). One case each, by name, so a setter added next year
    // that forgets to mark fails here as well as in
    // document.no_silent_setters.
    differential(r, "emitter.setRandomRotation", [&] { r.emitter->setRandomRotation(true); });
    differential(r, "emitter.setBlendMode",      [&] { r.emitter->setBlendMode(true); });
    differential(r, "emitter.setDissipation",    [&] { r.emitter->setDissipation(true); });
    differential(r, "emitter.setDissipationInv", [&] { r.emitter->setDissipationInv(true); });
    differential(r, "emitter.setParticleScale",  [&] { r.emitter->setParticleScale(0.7f); });
    differential(r, "emitter.setPPS",            [&] { r.emitter->setPPS(37.0f); });
    differential(r, "emitter.setGravity",        [&] { r.emitter->setGravity(0.4f); });
    differential(r, "emitter.setLife",           [&] { r.emitter->setLife(2.5f); });
    differential(r, "emitter.setSpeed",          [&] { r.emitter->setSpeed(1.7f); });
    differential(r, "emitter.setSpeedError",     [&] { r.emitter->setSpeedError(0.3f); });
    differential(r, "emitter.setLifeError",      [&] { r.emitter->setLifeError(0.2f); });
    differential(r, "emitter.setScaleError",     [&] { r.emitter->setScaleError(0.15f); });
    differential(r, "emitter.setTexture",        [&] { r.emitter->setTexture(iris::Texture2DPtr()); });
    differential(r, "emitter.applyPreset",       [&] {
        r.emitter->applyPreset(iris::ParticlePreset::Smoke);
    });

    differential(r, "camera.setProjection",       [&] {
        r.camera->setProjection(iris::CameraProjection::Orthogonal);
    });
    differential(r, "camera.setProjection back",  [&] {
        r.camera->setProjection(iris::CameraProjection::Perspective);
    });
    differential(r, "camera.setAspectRatio",      [&] { r.camera->setAspectRatio(2.35f); });
    differential(r, "camera.setFieldOfViewDegrees", [&] { r.camera->setFieldOfViewDegrees(31.0f); });
    differential(r, "camera.setFieldOfViewRadians", [&] { r.camera->setFieldOfViewRadians(0.9f); });
    differential(r, "camera.setFocalLength",      [&] { r.camera->setFocalLength(85.0f); });
    differential(r, "camera.setSensorSize",       [&] { r.camera->setSensorSize(36.0f, 24.0f); });
    differential(r, "camera.setSensorFit",        [&] {
        r.camera->setSensorFit(iris::CameraSensorFit::Horizontal);
    });
    differential(r, "camera.setAnamorphicSqueeze", [&] { r.camera->setAnamorphicSqueeze(2.0f); });
    differential(r, "camera.setOrthagonalZoom",   [&] { r.camera->setOrthagonalZoom(7.5f); });
    differential(r, "camera.setFramingAspect",    [&] { r.camera->setFramingAspect(16.0f / 9.0f); });
    differential(r, "camera.setPostOverride",     [&] {
        r.camera->setPostOverride(QStringLiteral("bloom"), true);
    });
    differential(r, "mesh.setFaceCullingMode",    [&] {
        r.cube->setFaceCullingMode(iris::FaceCullingMode::Front);
    });
    differential(r, "node.setAnimation (a mobility driver)", [&] {
        r.loner->setAnimation(iris::Animation::create("drive"));
    });
    differential(r, "node.setAnimation(null)",    [&] {
        r.loner->setAnimation(iris::AnimationPtr());
    });

    // ...AND A RE-SET TO THE SAME VALUE MARKS NOTHING. The other half of the
    // rule: several of these are written EVERY FRAME by a host re-asserting a
    // value it already set (the player's aspect ratio on resize, the viewport's
    // ortho zoom), and an unconditional mark would put a node on the change
    // list per frame forever — which is the defect the selection push already
    // taught this lane.
    {
        r.mirror->sync();
        // READ THEN SET: the value each node HOLDS right now, whatever the
        // cases above left it at (applyPreset rewrites the whole emitter).
        r.camera->setAspectRatio(r.camera->aspectRatio);
        r.camera->setOrthagonalZoom(r.camera->orthoSize);
        r.camera->setProjection(r.camera->getProjection());
        r.camera->setSensorFit(r.camera->sensorFit);
        r.emitter->setSpeed(r.emitter->getSpeed());
        r.emitter->setPPS(r.emitter->getPPS());
        r.emitter->setGravity(r.emitter->getGravity());
        r.emitter->setLife(r.emitter->getLife());
        r.cube->setFaceCullingMode(r.cube->getFaceCullingMode());
        r.loner->setAnimation(r.loner->getAnimation());
        r.mirror->sync();
        CHECK(r.mirror->dirtyNodeCount() == 0,
              "R2 #9: re-setting a typed value to the one it already holds marks NOTHING");
    }

    // ---- ANIMATION and the PLAY EDGES -------------------------------------
    differential(r, "updateAnimation (the play-mode tick)", [&] {
        r.doc->getRootNode()->updateAnimation(0.5f);
    });
    differential(r, "setPlaying(true)", [&] { r.doc->setPlaying(true); });
    differential(r, "a script moves a prop DURING play", [&] {
        r.loner->setLocalPos(iris::Vec3(2, 0, 0));
    });
    differential(r, "...and again (the soft promotion)", [&] {
        r.loner->setLocalPos(iris::Vec3(3, 0, 0));
    });
    differential(r, "setPlaying(false)", [&] { r.doc->setPlaying(false); });

    // ---- SELECTION, which is the EDITOR's state and not the document's -----
    differential(r, "select a light (its wires change shape)", [&] {
        r.mirror->setHighlightedNodes({ r.light }, r.light);
    });
    differential(r, "select a camera", [&] {
        r.mirror->setHighlightedNodes({ r.camera }, r.camera);
    });
    differential(r, "clear the selection", [&] {
        r.mirror->setHighlightedNodes({}, iris::SceneNodePtr());
    });
    differential(r, "helpers off", [&] { r.mirror->setLightWires(false); });
    differential(r, "helpers on", [&] { r.mirror->setLightWires(true); });

    // ---- A RE-BIND is a full walk, by contract ----------------------------
    r.mirror->requestFullWalk();
    r.mirror->sync();
    CHECK(std::string(r.mirror->walkMode()) == "full", "requestFullWalk() really walks it all");
    CHECK(r.mirror->visitedCount() > 8, "...and reaches every node");
    r.mirror->sync();
    CHECK(std::string(r.mirror->walkMode()) == "dirty", "...and the sync after it is dirty again");

    // ---- NOTHING WAS EVER MISSED -----------------------------------------
    CHECK(r.mirror->verifierCatchCount() == 0,
          "across every mutation class above, the change list missed NOTHING");

    r.mirror->setSource(iris::ScenePtr());
    std::printf("%s\n", failures ? "FAILURES" : "all ok");
    return failures ? 1 : 0;
}
