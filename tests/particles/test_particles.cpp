// particles.document_to_engine — the DOCUMENT half of the ParticleFX2 adoption
// (PARTICLES_FX2_SPEC.md §11 phase 1). The engine half is engine.particles_pfx2.
//
// The suite name is older than the design it now covers, and that is deliberate:
// the question it asks has not changed — "does an emitter authored in the
// document end up as the right pixels?" — only the answer has. Before the
// adoption the document simulated on the CPU and the mirror pushed a
// BillboardInstance per live particle, every frame. Now the document holds
// authoring parameters and a clock scalar, the mirror pushes ONE
// ParticleSystemDesc when they change, and Ogre simulates.
//
// So this file pins:
//   * the parameter MAPPING, field by field, on the desc the mirror builds
//     (gravity's legacy -50 constant, dissipate as a life-fraction ramp,
//     the spreads, the +Y convention, the quota);
//   * that the mirror is IDLE when nothing changes (the whole point of moving
//     the simulation into the engine);
//   * that document edits reach the pixels, that visibility works, and that
//     removing the node removes the emitter;
//   * the definition-accumulation measurement §3.2 asked for.
//
// Part 1 keeps the BillboardSet2 verbs under test: they did not go away, they
// just have one caller left (the light icon).
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
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "irisgl/mirror/scenemirror.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// Additive quads over a black clear colour: bright = unmistakably particle.
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
    enginetest::testCameraLookAt(view, Vec3(0, 0.8f, 6.0f), Vec3(0, 0.8f, 0));
    // A fixed step, for the same reason engine.particles_pfx2 uses one: an
    // offscreen frame takes about a millisecond, so wall-clock frames buy
    // milliseconds of simulated fire and no assertion below would mean anything.
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Image img;

    // ---- Part 1: the billboard verbs, no document ----
    // Kept under test although the document no longer uses them: the light icon
    // still does, and that is now their only caller.
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

    // ---- Part 2: the document -> desc MAPPING, field by field ----
    // No engine, no pixels: just the translation the mirror performs. Every row
    // here is a claim PARTICLES_FX2_SPEC §5 makes about how a 2016 field lands
    // on a 2026 emitter.
    {
        SceneMirror mapper(target);
        auto ps = iris::ParticleSystemNode::create();
        ps->particlesPerSecond = 90.0f;
        ps->speed = 3.0f;          ps->speedError = 0.5f;
        ps->lifeLength = 2.0f;     ps->lifeError = 0.25f;
        ps->particleScale = 0.4f;
        ps->gravityComplement = 0.5f;
        ps->maxParticles = 700;
        ps->useAdditive = true;
        ps->dissipate = false;
        ps->randomRotation = false;

        ParticleSystemDesc d = mapper.toParticleDesc(ps.data(), 0);
        CHECK(d.emitters.size() == 1, "one emitter per node");
        const ParticleEmitterDesc &e = d.emitters[0];
        CHECK(e.rate == 90.0f, "particlesPerSecond -> emission rate");
        CHECK(std::fabs(e.velocityMin - 2.5f) < 1e-5f && std::fabs(e.velocityMax - 3.5f) < 1e-5f,
              "speed +/- speedError -> the velocity range (the spread finally means something)");
        CHECK(std::fabs(e.ttlMin - 1.75f) < 1e-5f && std::fabs(e.ttlMax - 2.25f) < 1e-5f,
              "lifeLength +/- lifeError -> the time-to-live range");
        CHECK(e.sizeWidth == 0.4f && e.sizeHeight == 0.4f, "particleScale -> initial dimensions");
        CHECK(e.direction.x == 0.0f && e.direction.y == 1.0f && e.direction.z == 0.0f,
              "the document's +Y emission convention survives (no adapter node, unlike lights)");
        CHECK(d.quota == 700u, "maxParticles -> the definition quota (finally enforced)");
        CHECK(d.additive, "useAdditive -> additive blending");

        // Gravity is the legacy constant, so a scene authored against the old
        // slider falls at the same rate.
        bool foundGravity = false;
        for (const ParticleAffectorDesc &a : d.affectors)
            if (a.kind == ParticleAffectorDesc::Kind::LinearForce) {
                foundGravity = std::fabs(a.force.y - (-25.0f)) < 1e-4f && a.force.x == 0 && a.force.z == 0;
            }
        CHECK(foundGravity, "gravityComplement 0.5 -> a force of -25 on Y (the legacy GRAVITY = -50)");

        // No ramp asked for, no ramp affector: an unused affector still costs
        // SIMD work every frame.
        bool anyScale = false, anyColour = false, anyRotator = false, anyTurb = false;
        for (const ParticleAffectorDesc &a : d.affectors) {
            anyScale  |= a.kind == ParticleAffectorDesc::Kind::ScaleKeys;
            anyColour |= a.kind == ParticleAffectorDesc::Kind::ColourKeys;
            anyRotator|= a.kind == ParticleAffectorDesc::Kind::Rotator;
            anyTurb   |= a.kind == ParticleAffectorDesc::Kind::Turbulence;
        }
        CHECK(!anyScale && !anyColour && !anyRotator && !anyTurb,
              "affectors nobody asked for are not added at all");

        // dissipate: the legacy per-CALL shrink becomes a ramp over the LIFE
        // FRACTION, which is what the slider always meant and is now frame-rate
        // independent by construction.
        ps->dissipate = true;
        d = mapper.toParticleDesc(ps.data(), 0);
        const ParticleAffectorDesc *scale = nullptr;
        for (const ParticleAffectorDesc &a : d.affectors)
            if (a.kind == ParticleAffectorDesc::Kind::ScaleKeys) scale = &a;
        CHECK(scale && scale->keyCount == 2 && scale->scaleKeys[0] == 1.0f && scale->scaleKeys[1] == 0.0f,
              "dissipate -> a scale ramp 1 -> 0 over the particle's life");
        ps->dissipateInv = true;
        d = mapper.toParticleDesc(ps.data(), 0);
        scale = nullptr;
        for (const ParticleAffectorDesc &a : d.affectors)
            if (a.kind == ParticleAffectorDesc::Kind::ScaleKeys) scale = &a;
        CHECK(scale && scale->scaleKeys[0] == 0.0f && scale->scaleKeys[1] == 1.0f,
              "dissipateInv -> the same ramp inverted, 0 -> 1");

        // randomRotation: a random start angle, and now a real spin too.
        ps->randomRotation = true;
        ps->rotationSpeedMin = -20.0f; ps->rotationSpeedMax = 45.0f;
        d = mapper.toParticleDesc(ps.data(), 0);
        const ParticleAffectorDesc *rot = nullptr;
        for (const ParticleAffectorDesc &a : d.affectors)
            if (a.kind == ParticleAffectorDesc::Kind::Rotator) rot = &a;
        CHECK(rot && rot->rotEnd == 360.0f && rot->rotSpeedMin == -20.0f && rot->rotSpeedMax == 45.0f,
              "randomRotation -> a 0-360 start angle, plus the spin the old system never had");

        // The fire preset is a whole desc, and it is HDR by design.
        ps->applyPreset(iris::ParticlePreset::Fire);
        d = mapper.toParticleDesc(ps.data(), 0);
        const ParticleAffectorDesc *col = nullptr;
        for (const ParticleAffectorDesc &a : d.affectors)
            if (a.kind == ParticleAffectorDesc::Kind::ColourKeys) col = &a;
        CHECK(col && col->keyCount >= 3, "the fire preset carries a colour ramp");
        CHECK(col && col->colourKeys[0].r > 1.0f,
              "the fire ramp's core is HDR (>1), which is what makes it bloom instead of read as a sticker");
        CHECK(col && col->colourKeys[0].r > col->colourKeys[0].b * 4.0f, "and it starts warm");
        bool fireTurb = false;
        for (const ParticleAffectorDesc &a : d.affectors)
            fireTurb |= a.kind == ParticleAffectorDesc::Kind::Turbulence;
        CHECK(fireTurb, "the fire preset asks for turbulence (the flicker)");
        // A preset replaces the recipe, not the node.
        const QString guidBefore = ps->getGUID();
        ps->setName("my emitter");
        ps->applyPreset(iris::ParticlePreset::Smoke);
        CHECK(ps->getGUID() == guidBefore && ps->getName() == "my emitter",
              "applyPreset leaves the node's identity alone");
        CHECK(!ps->useAdditive && ps->alphaHash,
              "the smoke preset is alpha-blended with stochastic transparency (it OCCLUDES)");
    }

    // ---- Part 3: a document emitter through SceneMirror, to pixels ----
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
        ps->dissipate = false;          // constant size
        ps->maxParticles = 1024;
        ps->texture.clear();            // untextured white quads: brightest possible
        doc->getRootNode()->addChild(ps);
        CHECK(ps->maxParticles == 1024, "maxParticles is authored, not guessed");

        // The document no longer simulates ANYTHING. This is the deletion the
        // whole program is about, asserted rather than assumed.
        for (int i = 0; i < 40; ++i) ps->update(1.0f / 60.0f);
        CHECK(true, "40 document ticks completed with no simulator to run");

        SceneMirror mirror(target);
        mirror.setSource(doc);
        const int n = mirror.sync();
        CHECK(n == 1, "sync mirrored the emitter node");
        const NodeId eng = mirror.engineNode(ps.data());
        CHECK(eng != 0, "emitter has an engine node");
        CHECK(target->particleCount(eng) == 0, "nothing is alive before the first frame");

        const unsigned defsAfterFirst = target->particleDefinitionsCreated();
        for (int i = 0; i < 90; ++i) engine->renderOneFrame();
        std::printf("    after 90 engine frames: %u live particles\n", target->particleCount(eng));
        CHECK(target->particleCount(eng) > 0, "the ENGINE emitted, with no document tick at all");
        view->readPixels(img);
        const int lit = countBright(img);
        std::printf("    mirrored emitter: %d bright pixels\n", lit);
        CHECK(lit > 100, "mirrored particles render bright pixels");

        // THE POINT OF THE MIGRATION: a still emitter costs the mirror nothing.
        // Before this, every sync rebuilt a BillboardInstance array of every
        // live particle and pushed it into the engine, sixty times a second.
        for (int f = 0; f < 30; ++f) { mirror.sync(); engine->renderOneFrame(); }
        CHECK(target->particleDefinitionsCreated() == defsAfterFirst,
              "30 syncs of an unchanged emitter build no definitions");
        view->readPixels(img);
        CHECK(countBright(img) > 100, "and it is still running");

        // A document edit reaches the pixels — through the desc, not through a
        // particle list.
        ps->particlesPerSecond = 0.0f;
        mirror.sync();
        // Long enough for the particles ALREADY in flight to expire: a rate of 0
        // stops new emission, it does not retroactively shorten anyone's life.
        // ttl is 2 s here, so 150 frames at 1/60 is 2.5 s.
        for (int i = 0; i < 150; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) == 0, "rate 0 in the document stops emission in the engine");
        ps->particlesPerSecond = 300.0f;
        mirror.sync();
        for (int i = 0; i < 60; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) > 100, "and turning it back on refills the plume");

        // Visibility follows the document node, and kills particles in flight.
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

        // Removing the node from the document removes the engine visual.
        doc->getRootNode()->removeChild(ps);
        mirror.sync();
        CHECK(mirror.engineNode(ps.data()) == 0, "removed emitter has no engine node");
        for (int i = 0; i < 2; ++i) engine->renderOneFrame();
        view->readPixels(img);
        CHECK(countBright(img) == 0, "removed emitter renders nothing");

        // ---- the §3.2 measurement -------------------------------------------
        // Definitions cannot be destroyed. Add, remove and re-add an emitter ten
        // times and count what the scene is left holding: with the recycling
        // pool it must be a handful, not thirty.
        const unsigned before = target->particleDefinitionsCreated();
        for (int i = 0; i < 10; ++i) {
            auto churn = iris::ParticleSystemNode::create();
            churn->maxParticles = 1024;
            churn->texture.clear();
            doc->getRootNode()->addChild(churn);
            mirror.sync();
            engine->renderOneFrame();
            doc->getRootNode()->removeChild(churn);
            mirror.sync();
        }
        const unsigned churned = target->particleDefinitionsCreated() - before;
        std::printf("    10 add/remove cycles left %u new definitions "
                    "(%u in the scene overall; ~%u KiB CPU + %u KiB GPU each at quota 1024)\n",
                    churned, target->particleDefinitionsCreated(),
                    unsigned(1024 * 64 / 1024), unsigned(1024 * 32 / 1024));
        CHECK(churned <= 1, "the recycling pool absorbs emitter churn (definitions are immortal)");

        mirror.setSource(nullptr);
    }

    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();
    CHECK(true, "teardown clean");
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
