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
//   5. THE ATOM SPLIT: a two-sided MATERIAL worn by Back nodes -> their shared Back
//      twin routes to Atom as ONE material (one twin, one bucket); the node that
//      keeps the material's own two-sidedness stays on PBS.
#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "bridge/previewmesh.h"
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
    return previewmesh::load(QStringLiteral(JAHSHAKA_SOURCE_DIR "/app/content/primitives/") +
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
    frames(2);
    CHECK(scene->cullTwinCount() == 1, "the Front twin died with its last wearer");

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
    frames(3);
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
        auto twoSidedMat = iris::PbrMaterial::create();
        twoSidedMat->setBaseColor(QColor(200, 200, 60));
        twoSidedMat->renderStates.rasterState.cullMode = iris::CullMode::None;
        iris::MeshNodePtr nodes[3];
        for (int i = 0; i < 3; ++i) {
            nodes[i] = iris::MeshNode::create();
            nodes[i]->setName(QStringLiteral("atom%1").arg(i));
            nodes[i]->setMesh(cubeMesh);
            nodes[i]->setMaterial(twoSidedMat);
            nodes[i]->setLocalPos(iris::Vec3(-3.0f + 3.0f * float(i), 0.5f, 0.0f));
            if (i > 0) nodes[i]->setFaceCullingMode(iris::FaceCullingMode::Back);
            doc->getRootNode()->addChild(nodes[i]);
        }
        enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 10.0f), Vec3(0.0f, 0.5f, 0.0f));
        frames(4);
        const AtomDrawStatus st = scene->atomDrawStatus();
        std::printf("    atom: on %d atomItems %u (two-sided %u) cullFront %u pbs %u buckets %u, cull twins %u\n",
                    int(st.on), st.atomItems, st.atomTwoSided, st.cullFront, st.pbsItems, st.buckets,
                    scene->cullTwinCount());
        CHECK(scene->cullTwinCount() == 1, "two Back nodes on a two-sided material share ONE twin");
        // ATOM-TWO-SIDED-1: the node keeping the material's two-sidedness rides Atom too,
        // drawn from both sides — in a bucket of its own (the two-sided permutation).
        CHECK(st.atomItems == 3 && st.atomTwoSided == 1 && st.pbsItems == 0,
              "the two Back nodes route to Atom through the twin, the two-sided one as itself");
        CHECK(st.buckets == 2, "...the Back twin ONE bucket, not one per node; the two-sided material its own");
        for (auto &n : nodes) n->removeFromParent();
        frames(3);
        CHECK(scene->cullTwinCount() == 0, "every twin gone with its wearers");
    }

    mirror.setSource(nullptr);
    engine->renderOneFrame();
    engine->destroyView(view);
    engine->destroyScene(scene);
    std::printf(failures ? "FAILURES: %d\n" : "all good (%d failures)\n", failures);
    return failures ? 1 : 0;
}
