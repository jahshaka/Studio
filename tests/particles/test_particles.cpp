// Particle rendering on the engine viewport (PARTICLES_AUDIT.md option a).
//
// Part 1 drives the engine billboard verbs directly (BillboardSet2, no plugin):
// bright additive quads against a black background, cleared, destroyed, and a
// node removed with a live set.
// Part 2 mirrors a document ParticleSystemNode: the document simulates (CPU,
// world space), SceneMirror pushes the live particles each sync, pixels appear,
// follow visibility, and disappear when the node leaves the document.
// No window; DISPLAY must be reachable (Vulkan). QT_QPA_PLATFORM=offscreen.
#include <QGuiApplication>
#include <QVector3D>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/particle.h"
#include "jahshaka/engine/Engine.h"
#include "bridge/scenemirror.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// Additive white quads over a black clear colour: bright = unmistakably particle.
static int countBright(const Image &im) {
    int n = 0;
    for (unsigned y = 0; y < im.height; ++y)
        for (unsigned x = 0; x < im.width; ++x) {
            const Colour c = im.at(x, y);
            if (c.r + c.g + c.b > 1.2f) ++n;
        }
    return n;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_particles-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("particles", 96, 96, Colour(0, 0, 0));
    Scene *target = engine->createScene("particles");
    CHECK(view && target, "offscreen view + engine scene");
    if (!view || !target) return 1;
    view->setScene(target);
    view->setCameraPosition(Vec3(0, 0.8f, 6.0f));
    view->lookAt(Vec3(0, 0.8f, 0));
    Image img;

    // ---- Part 1: the engine verbs, no document ----
    {
        NodeId node = target->createNode();
        CHECK(node != 0, "engine node for the billboard set");
        CHECK(target->createBillboardSet(node, 0, true, 64), "createBillboardSet (untextured, additive)");
        CHECK(!target->createBillboardSet(9999, 0, true, 8), "createBillboardSet rejects unknown node");
        std::vector<BillboardInstance> quads;
        for (int i = 0; i < 8; ++i) {
            BillboardInstance b;
            b.position = Vec3(float(i % 3) * 0.4f - 0.4f, 0.6f + float(i / 3) * 0.4f, 0.0f);
            b.size = 1.0f;
            b.rotationRadians = float(i) * 0.7f;
            quads.push_back(b);
        }
        CHECK(target->setBillboards(node, quads.data(), quads.size()), "setBillboards(8)");
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        CHECK(view->readPixels(img), "readPixels");
        const int lit = countBright(img);
        std::printf("    engine billboards: %d bright pixels\n", lit);
        CHECK(lit > 50, "additive billboards render bright pixels on black");

        // More instances than capacity: clamped, not fatal.
        quads.resize(quads.size());
        std::vector<BillboardInstance> many(200, quads[0]);
        CHECK(target->setBillboards(node, many.data(), many.size()), "setBillboards over capacity clamps");

        CHECK(target->setBillboards(node, nullptr, 0), "setBillboards(0) clears");
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) == 0, "no billboards -> black again");

        CHECK(target->setBillboards(node, quads.data(), quads.size()), "re-populate");
        CHECK(target->destroyBillboardSet(node), "destroyBillboardSet");
        CHECK(!target->setBillboards(node, quads.data(), quads.size()), "setBillboards after destroy fails");
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) == 0, "destroyed set renders nothing");

        // A node removed with a live set must free it (releaseNode path).
        CHECK(target->createBillboardSet(node, 0, true, 64), "recreate set");
        CHECK(target->setBillboards(node, quads.data(), quads.size()), "re-populate again");
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) > 50, "billboards back before node removal");
        CHECK(target->removeNode(node), "removeNode with live billboard set");
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) == 0, "removeNode freed the billboard set");
    }

    // ---- Part 2: a document emitter through SceneMirror ----
    {
        auto doc = iris::Scene::create();
        auto ps = iris::ParticleSystemNode::create();
        ps->setName("emitter");
        ps->particlesPerSecond = 300.0f;
        ps->speed = 2.0f;
        ps->lifeLength = 2.0f;
        ps->particleScale = 0.5f;
        ps->gravityComplement = 0.0f;   // no gravity: a rising column
        ps->useAdditive = true;
        ps->dissipate = false;          // constant size: deterministic pixels
        doc->getRootNode()->addChild(ps);
        CHECK(ps->maxParticles == 0, "maxParticles initialized (audit defect #5)");

        // Simulate ~0.64s the way the editor tick does.
        for (int i = 0; i < 40; ++i) ps->update(1.0f / 60.0f);
        const size_t simulated = ps->particles.size();
        std::printf("    document simulated %zu particles\n", simulated);
        CHECK(simulated > 100, "document emitter simulated particles (no GL needed)");

        // update(0) is a pure transform refresh: the mirror calls it every sync.
        ps->dissipate = true;
        const float scaleBefore = ps->particles.front()->scale;
        ps->update(0.0f);
        CHECK(ps->particles.size() == simulated && ps->particles.front()->scale == scaleBefore,
              "update(0) neither emits nor decays (idempotent for the mirror)");
        ps->dissipate = false;

        SceneMirror mirror(target);
        mirror.setSource(doc);
        const int n = mirror.sync();
        CHECK(n == 1, "sync mirrored the emitter node");
        CHECK(mirror.engineNode(ps.data()) != 0, "emitter has an engine node");
        for (int i = 0; i < 3; ++i) engine->renderOneFrame();
        view->readPixels(img);
        const int lit = countBright(img);
        std::printf("    mirrored emitter: %d bright pixels\n", lit);
        CHECK(lit > 100, "mirrored particles render bright pixels");

        // Visibility follows the document node.
        ps->visible = false;
        mirror.sync();
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) == 0, "hidden emitter renders nothing");
        ps->visible = true;
        mirror.sync();
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) > 100, "shown again");

        // Continued ticking + syncing keeps rendering (the per-frame path).
        for (int f = 0; f < 5; ++f) {
            ps->update(1.0f / 60.0f);
            mirror.sync();
            engine->renderOneFrame();
        }
        view->readPixels(img);
        CHECK(countBright(img) > 100, "tick -> sync -> render loop keeps particles alive");

        // Removing the node from the document removes the engine visual.
        doc->getRootNode()->removeChild(ps);
        mirror.sync();
        CHECK(mirror.engineNode(ps.data()) == 0, "removed emitter has no engine node");
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) == 0, "removed emitter renders nothing");

        mirror.setSource(nullptr);
    }

    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();
    CHECK(true, "teardown clean");
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
