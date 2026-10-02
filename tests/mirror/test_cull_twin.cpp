// mirror.cull_twin — A NODE'S FACE CULL THROUGH ITS MATERIAL'S CULL TWIN (CULL-MODE-2).
//
// The owner's rule (2026-09-29): a primitive is one-sided like every imported
// model, and a user makes ONE OBJECT two-sided (MeshNode::faceCullingMode). Ogre
// culls per datablock, so a node whose cull differs from its material's wears the
// material's CULL TWIN (Scene::setNodeFaceCull): one datablock per (material, cull)
// actually worn, kept in step with every edit of the material, gone when nothing
// wears it. This suite drives it the product's way — document nodes through the
// SceneMirror — and reads the engine's twin count and pixels:
//
//   1. BOOKKEEPING: one material on two planes, one of them two-sided -> ONE twin;
//      a node that restates its material's own cull wears the material (no twin);
//      the twin dies once the two-sided node leaves the document.
//   2. THE PICTURE, a plane (normal +Y) seen from BELOW: the material's own cull and
//      Back hide it; None draws it; Front draws it from below and not from above.
//   3. THE TWIN FOLLOWS ITS MASTER: an edit of the shared material's base colour
//      reaches the two-sided node's pixels (the twin), not just the master's wearers.
//   4. A ONE-SIDED CLOSED BOX SEEN FROM INSIDE SHOWS THE VOID; two-sided, its walls.
//   5. THE ATOM SPLIT: two TwoSided nodes on one one-sided material share ONE twin,
//      routed to Atom drawn from both sides in a bucket of its own (ATOM-TWO-SIDED-1);
//      the node keeping the material's own cull rides the master's bucket.
//   6. A CULL TOGGLED EVERY FRAME re-wears ONE twin (CULL-TWIN-DEBTS-1): the twin
//      outlives its last wearer by kCullTwinGraceFrames (60) of the scene's frames,
//      so the toggle never builds and destroys a datablock per frame.
//   7. THE THREE READ PATHS (CULL-TWIN-DEBTS-1): a material SWAPPED under a node
//      that keeps its cull; the material's OWN two-sidedness flipping under a Back
//      node (an engine-level material: the document has no material cull); twins
//      across a SHADING-MODEL switch (Lit -> Unlit -> Lit).
//   8. A CULL EDIT RE-VOXELISES NOTHING (CULL-TWIN-DEBTS-1): the voxelisers read no
//      cull, so no cascade rebuilds for one.
#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "../support/testmesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static iris::MeshPtr primitive(const char *file)
{
    return testmesh::load(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/") +
                             QString::fromLatin1(file));
}

static double meanDiff(const Image &a, const Image &b)
{
    double sum = 0.0;
    for (size_t i = 0; i < a.rgba.size() && i < b.rgba.size(); ++i)
        sum += std::abs(int(a.rgba[i]) - int(b.rgba[i]));
    return a.rgba.empty() ? 0.0 : sum / double(a.rgba.size());
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_cull_twin-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    const int W = 96, H = 96;
    View *view = engine->createOffscreenView("cull", W, H, Colour(0, 0, 1));
    Scene *scene = engine->createScene("cull");
    if (!view || !scene) { std::printf("FAIL: no view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(0.4f, 0.4f, 0.4f), Colour(0.3f, 0.3f, 0.3f));

    auto doc = iris::Scene::create();
    SceneMirror mirror(scene);
    mirror.setLightWires(false);
    auto sun = iris::LightNode::create();
    sun->lightType = iris::LightType::Directional;
    sun->intensity = 1.0f;
    sun->setLocalPos(iris::Vec3(0.0f, 6.0f, 20.0f));
    doc->getRootNode()->addChild(sun);

    auto planeMesh = primitive("plane.obj");
    auto cubeMesh = primitive("cube.obj");
    CHECK(planeMesh && cubeMesh, "the shipped plane and cube primitives load");
    if (!planeMesh || !cubeMesh) return 1;

    auto mat = iris::PbrMaterial::create();   // one-sided, as authored by default
    mat->setBaseColor(QColor(220, 40, 40));
    auto plane = iris::MeshNode::create();
    plane->setName(QStringLiteral("plane"));
    plane->setMesh(planeMesh);
    plane->setMaterial(mat);
    doc->getRootNode()->addChild(plane);
    auto other = iris::MeshNode::create();   // the same material, 4 m off to the side
    other->setName(QStringLiteral("other"));
    other->setMesh(planeMesh);
    other->setMaterial(mat);
    other->setLocalPos(iris::Vec3(4.0f, 0.0f, 0.0f));
    doc->getRootNode()->addChild(other);

    mirror.setSource(doc);
    auto frames = [&](int n) { for (int i = 0; i < n; ++i) { mirror.sync(); engine->renderOneFrame(); } };
    // An unworn twin outlives its last wearer by the engine's kCullTwinGraceFrames
    // (60) of the scene's drawn frames; two more for the sweep that ends it.
    const int kGrace = 62;
    frames(3);
    mirror.applyEnvironment(view);

    // ---- 1. BOOKKEEPING ----------------------------------------------------
    CHECK(plane->getFaceCullingMode() == iris::FaceCullingMode::DefinedInMaterial,
          "a mesh node is born leaving the cull to its material");
    CHECK(scene->cullTwinCount() == 0, "no node names a cull: no twin");
    other->setFaceCullingMode(iris::FaceCullingMode::Back);
    frames(2);
    CHECK(scene->cullTwinCount() == 0, "Back on a one-sided material restates it: no twin");
    other->setFaceCullingMode(iris::FaceCullingMode::None);
    frames(2);
    CHECK(scene->cullTwinCount() == 1, "one material worn one-sided AND two-sided: ONE twin");
    const NodeId otherNode = mirror.engineNode(other.data());
    CHECK(otherNode && scene->nodeFaceCull(otherNode) == FaceCull::TwoSided,
          "the mirror pushed the node's cull (None -> TwoSided)");

    // ---- 2. THE PICTURE: a plane from below ------------------------------
    // DRAWN = the picture differs from the same pose with the plane hidden.
    auto drawn = [&](const iris::MeshNodePtr &subject, const iris::Vec3 &eye, const char *tag,
                     Image *shownOut = nullptr) {
        const iris::Vec3 c = subject->getGlobalPosition();
        enginetest::testCameraLookAt(view, Vec3(c.x() + eye.x(), c.y() + eye.y(), c.z() + eye.z()),
                                     Vec3(c.x(), c.y(), c.z()));
        frames(3);
        Image shown;
        view->readPixels(shown);
        subject->setVisible(false);
        frames(3);
        Image empty;
        view->readPixels(empty);
        subject->setVisible(true);
        frames(1);
        const double mean = meanDiff(shown, empty);
        std::printf("    %-34s differs from the empty pose by %.2f/255\n", tag, mean);
        if (shownOut) *shownOut = shown;
        return mean > 2.0;
    };
    const iris::Vec3 below(0.3f, -2.5f, 0.4f), above(0.3f, 2.5f, 0.4f);
    CHECK(drawn(plane, above, "material's own, from above"), "the plane draws from above (its front)");
    CHECK(!drawn(plane, below, "material's own, from below"), "a one-sided plane hides its back");
    CHECK(drawn(other, below, "None, from below"), "a node with cull None draws from below (the twin)");
    plane->setFaceCullingMode(iris::FaceCullingMode::Front);
    CHECK(drawn(plane, below, "Front, from below"), "cull Front draws the back face from below");
    CHECK(!drawn(plane, above, "Front, from above"), "...and nothing from above");
    CHECK(scene->cullTwinCount() == 2, "two cull modes worn beside the material's own: two twins");
    plane->setFaceCullingMode(iris::FaceCullingMode::Back);
    CHECK(!drawn(plane, below, "Back, from below"), "cull Back hides it again");
    frames(kGrace);
    CHECK(scene->cullTwinCount() == 1, "the Front twin died with its last wearer (after its grace)");

    // ---- 3. THE TWIN FOLLOWS ITS MASTER ----------------------------------
    {
        // From ABOVE (the sun's side): `other` wears the twin from every side.
        Image red, green;
        drawn(other, above, "None, red, from above", &red);
        mat->setBaseColor(QColor(40, 220, 40));
        drawn(other, above, "None, green, from above", &green);
        const Colour r = red.at(W / 2, H / 2), g = green.at(W / 2, H / 2);
        std::printf("    centre red (%.3f %.3f %.3f) -> green (%.3f %.3f %.3f)\n",
                    double(r.r), double(r.g), double(r.b), double(g.r), double(g.g), double(g.b));
        CHECK(r.r > r.g && g.g > g.r,
              "an edit of the shared material reaches the node wearing its twin");
    }

    // ---- the twin goes when its last wearer leaves -------------------------
    other->removeFromParent();
    frames(kGrace);
    CHECK(scene->cullTwinCount() == 0, "the two-sided node left the document: its twin is gone");

    // ---- 4. A CLOSED BOX FROM INSIDE ---------------------------------------
    {
        plane->setVisible(false);
        auto box = iris::MeshNode::create();
        box->setName(QStringLiteral("box"));
        box->setMesh(cubeMesh);
        box->setMaterial(mat);
        box->setLocalScale(iris::Vec3(4.0f, 4.0f, 4.0f));
        box->setLocalPos(iris::Vec3(0.0f, 20.0f, 0.0f));
        doc->getRootNode()->addChild(box);
        const iris::Vec3 inside(0.3f, 0.2f, 0.1f);   // the eye at the box's centre, looking at +x
        enginetest::testCameraLookAt(view, Vec3(inside.x(), 20.0f + inside.y(), inside.z()),
                                     Vec3(3.0f, 20.0f, 0.0f));
        frames(3);
        Image oneSided;
        view->readPixels(oneSided);
        box->setFaceCullingMode(iris::FaceCullingMode::None);
        frames(3);
        Image twoSided;
        view->readPixels(twoSided);
        box->setVisible(false);
        frames(3);
        Image empty;
        view->readPixels(empty);
        const double one = meanDiff(oneSided, empty), two = meanDiff(twoSided, empty);
        std::printf("    inside the box: one-sided %.2f/255, two-sided %.2f/255 from the empty pose\n", one, two);
        CHECK(one <= 2.0, "a one-sided closed box seen from inside shows the void");
        CHECK(two > 2.0, "...two-sided, it shows its walls");
        box->removeFromParent();
        plane->setVisible(true);
        frames(3);
    }

    // ---- 5. THE ATOM SPLIT ---------------------------------------------------
    {
        plane->removeFromParent();
        frames(kGrace);
        auto atomMat = iris::PbrMaterial::create();
        atomMat->setBaseColor(QColor(200, 200, 60));
        iris::MeshNodePtr nodes[3];
        for (int i = 0; i < 3; ++i) {
            nodes[i] = iris::MeshNode::create();
            nodes[i]->setName(QStringLiteral("atom%1").arg(i));
            nodes[i]->setMesh(cubeMesh);
            nodes[i]->setMaterial(atomMat);
            nodes[i]->setLocalPos(iris::Vec3(-3.0f + 3.0f * float(i), 0.5f, 0.0f));
            if (i > 0) nodes[i]->setFaceCullingMode(iris::FaceCullingMode::None);
            doc->getRootNode()->addChild(nodes[i]);
        }
        enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 10.0f), Vec3(0.0f, 0.5f, 0.0f));
        frames(4);
        const AtomDrawStatus st = scene->atomDrawStatus();
        std::printf("    atom: on %d atomItems %u (two-sided %u) cullFront %u pbs %u buckets %u, cull twins %u\n",
                    int(st.on), st.atomItems, st.atomTwoSided, st.cullFront, st.pbsItems, st.buckets,
                    scene->cullTwinCount());
        CHECK(scene->cullTwinCount() == 1, "two TwoSided nodes on one material share ONE twin");
        CHECK(st.atomItems == 3 && st.atomTwoSided == 2 && st.pbsItems == 0,
              "the two TwoSided nodes route to Atom drawn from both sides, the third as itself");
        CHECK(st.buckets == 2, "...the twin ONE bucket, not one per node; the master its own");
        for (auto &n : nodes) n->removeFromParent();
        frames(kGrace);
        CHECK(scene->cullTwinCount() == 0, "every twin gone with its wearers");
    }

    // ---- 6. A CULL TOGGLED EVERY FRAME ---------------------------------------
    {
        auto toggled = iris::MeshNode::create();
        toggled->setName(QStringLiteral("toggled"));
        toggled->setMesh(planeMesh);
        toggled->setMaterial(mat);
        doc->getRootNode()->addChild(toggled);
        frames(2);
        int unbornFrames = 0, frameCount = 40;
        for (int i = 0; i < frameCount; ++i) {
            toggled->setFaceCullingMode((i & 1) ? iris::FaceCullingMode::DefinedInMaterial
                                                : iris::FaceCullingMode::None);
            frames(1);
            if (scene->cullTwinCount() != 1) ++unbornFrames;
        }
        std::printf("    per-frame toggle: %d of %d frames ended with no twin alive\n", unbornFrames, frameCount);
        CHECK(unbornFrames == 0, "a cull toggled every frame keeps ONE twin alive (no datablock built per frame)");
        toggled->setFaceCullingMode(iris::FaceCullingMode::DefinedInMaterial);
        frames(kGrace);
        CHECK(scene->cullTwinCount() == 0, "...and it dies once the toggling stops for its grace");
        toggled->removeFromParent();
        frames(2);
    }

    // ---- 7. THE THREE READ PATHS ---------------------------------------------
    {
        // (a) A MATERIAL SWAPPED UNDER A NODE THAT KEEPS ITS CULL: the node wears the
        // NEW material's twin, drawn from below; the old twin follows its grace out.
        auto swapped = iris::MeshNode::create();
        swapped->setName(QStringLiteral("swapped"));
        swapped->setMesh(planeMesh);
        swapped->setMaterial(mat);
        swapped->setFaceCullingMode(iris::FaceCullingMode::None);
        swapped->setLocalPos(iris::Vec3(-8.0f, 0.0f, 0.0f));
        doc->getRootNode()->addChild(swapped);
        frames(2);
        CHECK(scene->cullTwinCount() == 1, "(a) a TwoSided node on the first material: one twin");
        auto mat2 = iris::PbrMaterial::create();
        mat2->setBaseColor(QColor(40, 40, 220));
        swapped->setMaterial(mat2);
        CHECK(drawn(swapped, below, "(a) swapped, None, from below"),
              "(a) the swapped material is still drawn from below (the node kept its cull)");
        frames(kGrace);
        CHECK(scene->cullTwinCount() == 1, "(a) ...through the NEW material's twin; the old one has gone");

        // (c) TWINS ACROSS A SHADING-MODEL SWITCH: Unlit and back, the node stays two-sided.
        mat2->setShadingModel(1);
        CHECK(drawn(swapped, below, "(c) unlit, None, from below"),
              "(c) after Lit -> Unlit the node is still drawn from below");
        mat2->setShadingModel(0);
        CHECK(drawn(swapped, below, "(c) lit again, None, from below"),
              "(c) ...and after Unlit -> Lit");
        swapped->setFaceCullingMode(iris::FaceCullingMode::DefinedInMaterial);
        CHECK(!drawn(swapped, below, "(c) own cull, from below"),
              "(c) its own (one-sided) cull hides the back again after the switches");
        swapped->removeFromParent();
        frames(kGrace);
        CHECK(scene->cullTwinCount() == 0, "(a)/(c) every twin gone");

        // (b) THE MATERIAL'S OWN TWO-SIDEDNESS FLIPPING UNDER A BACK NODE. The document
        // has no material cull (the node is the authority), so this is an engine-level
        // material: a node that names Back wears the master while the material is
        // one-sided, a Back twin while it is two-sided, and the master again after.
        PbrParams p;
        p.albedo = Colour(0.8f, 0.2f, 0.2f);
        const MaterialId em = scene->createPbrMaterial(p);
        const MeshId mesh = scene->createMesh(enginetest::unitCubeMesh());
        const NodeId en = scene->createNode();
        CHECK(em && mesh && en && scene->attachMesh(en, mesh, em), "(b) an engine-level cube");
        enginetest::setNodePosition(scene, en, Vec3(0.0f, -40.0f, 0.0f));
        scene->setNodeFaceCull(en, FaceCull::Back);
        engine->renderOneFrame();
        CHECK(scene->cullTwinCount() == 0, "(b) Back on a one-sided material wears the master");
        p.twoSided = true;
        CHECK(scene->setPbrMaterial(em, p), "(b) the material turns two-sided");
        engine->renderOneFrame();
        CHECK(scene->cullTwinCount() == 1, "(b) ...and the Back node now wears a Back twin");
        p.twoSided = false;
        CHECK(scene->setPbrMaterial(em, p), "(b) the material turns one-sided again");
        for (int i = 0; i < kGrace; ++i) engine->renderOneFrame();
        CHECK(scene->cullTwinCount() == 0, "(b) ...the node wears the master again and the twin has gone");
        scene->removeNode(en);
        scene->destroyMaterial(em);
        engine->renderOneFrame();
    }

    // ---- 8. A CULL EDIT RE-VOXELISES NOTHING ---------------------------------
    {
        GiParams gi;
        gi.mode = GiMode::Vct;
        CHECK(scene->setGlobalIllumination(gi), "voxel GI on for the cull-edit arm");
        auto box = iris::MeshNode::create();
        box->setName(QStringLiteral("voxbox"));
        box->setMesh(cubeMesh);
        box->setMaterial(mat);
        box->setLocalPos(iris::Vec3(0.0f, 0.5f, 0.0f));
        doc->getRootNode()->addChild(box);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 10.0f), Vec3(0.0f, 0.5f, 0.0f));
        auto rebuilds = [&] {
            unsigned long long n = 0;
            for (const auto &c : scene->giStatus().cascades) n += c.rebuilds;
            return n;
        };
        unsigned long long settled = rebuilds();
        for (int i = 0, still = 0; i < 600 && still < 30; ++i) {
            frames(1);
            const unsigned long long now = rebuilds();
            still = now == settled ? still + 1 : 0;
            settled = now;
        }
        box->setFaceCullingMode(iris::FaceCullingMode::None);
        frames(30);
        const unsigned long long after = rebuilds();
        std::printf("    cascade rebuilds: settled %llu, 30 frames after a cull edit %llu\n", settled, after);
        CHECK(after == settled, "a node cull edit re-voxelises no cascade");
        box->removeFromParent();
        GiParams off;
        scene->setGlobalIllumination(off);
        frames(2);
    }

    mirror.setSource(nullptr);
    engine->renderOneFrame();
    engine->destroyView(view);
    engine->destroyScene(scene);
    std::printf(failures ? "FAILURES: %d\n" : "all good (%d failures)\n", failures);
    return failures ? 1 : 0;
}
