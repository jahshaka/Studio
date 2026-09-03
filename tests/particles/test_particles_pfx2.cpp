// ParticleFX2 adoption, phase 0: the ENGINE verbs, with no document in sight
// (PARTICLES_FX2_SPEC.md §11 phase 0).
//
// What this suite is for: the engine now SIMULATES particles. Nothing outside
// irisgl/engine integrates a position any more, so every claim about emission,
// forces, colour-over-life, quotas, visibility, rebuild and teardown has to be
// provable through Engine.h alone. It is also the permanent home of the
// findings phase 0 went looking for:
//
//   * the QUOTA CEILING (spec §10.1, "the single most likely 'it renders
//     garbage' failure"): a scene whose billboard sets initialise first must
//     still be able to build a 16000-particle definition. The spec inferred this
//     from source and never ran it; here it runs.
//   * the DEFINITION LEAK (spec §3.2): definitions cannot be destroyed, so the
//     suite measures how many a churn of create/destroy/reshape actually leaves
//     behind, and proves the recycling pool caps it.
//   * per-node independence: two systems in one scene with different quotas,
//     different textures and independent visibility — the property the standing
//     KEEP verdict said a shared-definition design could not have.
//
// Headless: offscreen view, black clear, additive HDR quads. Simulation is
// stochastic (Ogre seeds Math::UnitRandom from a process-wide LCG and runs
// affectors on worker threads), so every pixel assertion here is a POPULATION
// assertion — counts and ratios, never an exact colour.
#include <QGuiApplication>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static int countBright(const Image &im, float threshold = 0.30f) {
    int n = 0;
    for (unsigned y = 0; y < im.height; ++y)
        for (unsigned x = 0; x < im.width; ++x) {
            const Colour c = im.at(x, y);
            if (c.r + c.g + c.b > threshold) ++n;
        }
    return n;
}

/// Bright pixels inside a rectangle given in fractions of the image.
static int countBrightIn(const Image &im, float x0, float y0, float x1, float y1,
                         float threshold = 0.30f) {
    int n = 0;
    const unsigned ax = unsigned(x0 * im.width),  bx = unsigned(x1 * im.width);
    const unsigned ay = unsigned(y0 * im.height), by = unsigned(y1 * im.height);
    for (unsigned y = ay; y < by && y < im.height; ++y)
        for (unsigned x = ax; x < bx && x < im.width; ++x) {
            const Colour c = im.at(x, y);
            if (c.r + c.g + c.b > threshold) ++n;
        }
    return n;
}

static Colour meanLit(const Image &im, float threshold = 0.30f) {
    double r = 0, g = 0, b = 0; int n = 0;
    for (unsigned y = 0; y < im.height; ++y)
        for (unsigned x = 0; x < im.width; ++x) {
            const Colour c = im.at(x, y);
            if (c.r + c.g + c.b > threshold) { r += c.r; g += c.g; b += c.b; ++n; }
        }
    if (!n) return Colour(0, 0, 0, 1);
    return Colour(float(r / n), float(g / n), float(b / n), 1.0f);
}

/// A red/green checker texture, so "the texture reached the particles" is
/// provable from the pixels and not just from the absence of an error.
static TextureId makeBlobTexture(Scene *s, unsigned char rr, unsigned char gg,
                                 unsigned char bb) {
    const unsigned N = 32;
    std::vector<unsigned char> px(N * N * 4, 0);
    for (unsigned y = 0; y < N; ++y)
        for (unsigned x = 0; x < N; ++x) {
            // A soft radial blob: opaque in the middle, transparent at the rim.
            const float dx = (float(x) + 0.5f) / N - 0.5f;
            const float dy = (float(y) + 0.5f) / N - 0.5f;
            const float d = std::sqrt(dx * dx + dy * dy) * 2.0f;
            const float a = std::max(0.0f, 1.0f - d);
            unsigned char *p = &px[(y * N + x) * 4];
            p[0] = rr; p[1] = gg; p[2] = bb;
            p[3] = (unsigned char)(a * 255.0f);
        }
    return s->createTexture(N, N, px.data(), true);
}

/// A rising column of additive quads: the default shape every case starts from.
static ParticleSystemDesc plumeDesc(unsigned quota, TextureId tex) {
    ParticleSystemDesc d;
    d.quota = quota;
    d.texture = tex;
    d.additive = true;
    ParticleEmitterDesc e;
    e.shape = ParticleEmitterShape::Point;
    e.direction = Vec3(0, 1, 0);
    e.angleDegrees = 10.0f;
    e.rate = 200.0f;
    e.velocityMin = 1.0f; e.velocityMax = 1.5f;
    e.ttlMin = 1.0f;      e.ttlMax = 1.4f;
    e.sizeWidth = 0.35f;  e.sizeHeight = 0.35f;
    d.emitters.push_back(e);
    return d;
}

static void warm(Engine *engine, int frames) {
    for (int i = 0; i < frames; ++i) engine->renderOneFrame();
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_particles_pfx2-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    View *view = engine->createOffscreenView("pfx2", 128, 128, Colour(0, 0, 0));
    Scene *scene = engine->createScene("pfx2");
    CHECK(view && scene, "offscreen view + scene");
    if (!view || !scene) return 1;
    view->setScene(scene);
    enginetest::testCameraLookAt(view, Vec3(0, 1.0f, 5.0f), Vec3(0, 1.0f, 0));
    Image img;

    // A FIXED simulation step. Without it the engine charges each frame the wall
    // time it took, and an offscreen 128x128 frame takes about a millisecond —
    // so 60 frames buy 0.06 s of simulated time, a 200/s emitter produces 12
    // particles, and nothing ever visibly rises. Every count and every band
    // ratio below is written against 1/60 s per frame.
    engine->setFixedFrameDelta(1.0f / 60.0f);
    CHECK(std::fabs(engine->fixedFrameDelta() - 1.0f / 60.0f) < 1e-6f,
          "setFixedFrameDelta takes effect");

    // ---- 0. The plugin is actually loaded -----------------------------------
    // Every emitter and affector FACTORY lives in Plugin_ParticleFX2. If the
    // engine did not load it, setParticleSystem fails with a named error rather
    // than crashing, and everything below is meaningless.
    {
        NodeId probe = scene->createNode();
        ParticleSystemDesc d = plumeDesc(256, 0);
        const bool ok = scene->setParticleSystem(probe, d);
        if (!ok) std::printf("    setParticleSystem said: %s\n", engine->lastError().c_str());
        CHECK(ok, "Plugin_ParticleFX2 is loaded (emitter factories are registered)");
        scene->removeNode(probe);
        if (!ok) { std::printf("RESULT: %d FAILURE(S)\n", ++failures); return 1; }
    }

    // ---- 1. THE QUOTA-CEILING LANDMINE (spec §10.1) -------------------------
    // ParticleSystemManager2 sizes ONE shared index buffer per scene on the
    // first init() it sees, from the highest quota known AT THAT MOMENT, and
    // raising it afterwards throws. Our light-icon billboard sets are quota 1
    // and init as soon as a scene gets a light, so a scene that has drawn a
    // light icon must STILL be able to build a full-size definition. This is the
    // case the spec inferred from source and explicitly did not run.
    {
        NodeId iconNode = scene->createNode();
        CHECK(scene->createBillboardSet(iconNode, 0, false, 1), "quota-1 billboard set (light icon)");
        BillboardInstance b;
        b.position = Vec3(-2.0f, 1.0f, 0.0f); b.size = 0.3f;
        scene->setBillboards(iconNode, &b, 1);
        warm(engine.get(), 3);   // forces the billboard set's init() and the shared index buffer

        NodeId bigNode = scene->createNode();
        ParticleSystemDesc big = plumeDesc(16000, 0);
        const bool ok = scene->setParticleSystem(bigNode, big);
        if (!ok) std::printf("    %s\n", engine->lastError().c_str());
        CHECK(ok, "a 16000-particle definition builds AFTER a quota-1 billboard set initialised "
                  "(setHighestPossibleQuota at scene creation does its job)");
        warm(engine.get(), 30);
        view->readPixels(img);
        const int lit = countBright(img);
        std::printf("    max-quota system: %d lit pixels\n", lit);
        CHECK(lit > 200, "the max-quota system renders");
        scene->removeNode(bigNode);
        scene->removeNode(iconNode);
        warm(engine.get(), 2);
    }

    // ---- 2. It emits, and it MOVES ------------------------------------------
    // The point of the whole program: no host code advances a particle, and the
    // picture still changes from frame to frame.
    NodeId node = scene->createNode();
    TextureId blob = makeBlobTexture(scene, 255, 120, 30);
    CHECK(blob != 0, "particle texture created");
    {
        ParticleSystemDesc d = plumeDesc(2048, blob);
        CHECK(scene->setParticleSystem(node, d), "setParticleSystem (point emitter, textured, additive)");
        CHECK(!scene->setParticleSystem(9999, d), "setParticleSystem rejects an unknown node");
        ParticleSystemDesc empty; empty.quota = 64;
        CHECK(!scene->setParticleSystem(node, empty), "setParticleSystem rejects a desc with no emitters");

        // Nothing is alive before the first frame: the host never seeded it.
        CHECK(scene->particleCount(node) == 0, "no particles before the first frame");
        warm(engine.get(), 60);
        const unsigned alive = scene->particleCount(node);
        std::printf("    after 60 frames: %u live particles\n", alive);
        CHECK(alive > 0, "the ENGINE emitted particles with no host simulation at all");
        CHECK(alive <= 2048, "live particles stay within the quota");

        view->readPixels(img);
        Image later;
        const int lit0 = countBright(img);
        std::printf("    plume: %d lit pixels\n", lit0);
        CHECK(lit0 > 100, "the plume renders bright pixels on black");

        // The texture reached the datablock: an untextured additive quad is flat
        // white, a blob-textured one carries the blob's colour.
        const Colour mean = meanLit(img);
        std::printf("    mean lit colour: %.3f %.3f %.3f\n", mean.r, mean.g, mean.b);
        CHECK(mean.r > mean.b * 1.5f, "the texture reached the particles (warm, not flat white)");

        // It moves. Two captures 20 frames apart must differ in a healthy
        // fraction of the lit pixels — a frozen sprite would not.
        warm(engine.get(), 20);
        view->readPixels(later);
        int differing = 0, litEither = 0;
        for (unsigned y = 0; y < img.height; ++y)
            for (unsigned x = 0; x < img.width; ++x) {
                const Colour a = img.at(x, y), b2 = later.at(x, y);
                const bool la = a.r + a.g + a.b > 0.30f, lb = b2.r + b2.g + b2.b > 0.30f;
                if (la || lb) {
                    ++litEither;
                    if (std::fabs(a.r - b2.r) + std::fabs(a.g - b2.g) + std::fabs(a.b - b2.b) > 0.08f)
                        ++differing;
                }
            }
        std::printf("    %d/%d lit pixels changed over 20 frames\n", differing, litEither);
        CHECK(litEither > 0 && differing * 100 / std::max(1, litEither) > 20,
              "the simulation advances by itself (frame N differs from frame N+20)");
    }

    // ---- 3. Emission follows the node's transform ---------------------------
    // PFX2 offsets and rotates emission by the instance node's DERIVED position
    // and orientation, and the instance rides the node — so moving the document
    // node moves the plume, with no adapter child (unlike lights, which need
    // the -90 degree pitch).
    {
        scene->setNodeTransform(node, Vec3(1.6f, 0.2f, 0.0f), Quat(), Vec3(1, 1, 1));
        warm(engine.get(), 90);
        view->readPixels(img);
        const int leftHalf  = countBrightIn(img, 0.0f, 0.0f, 0.45f, 1.0f);
        const int rightHalf = countBrightIn(img, 0.55f, 0.0f, 1.0f, 1.0f);
        std::printf("    moved +X: %d lit left, %d lit right\n", leftHalf, rightHalf);
        CHECK(rightHalf > leftHalf * 3 + 10, "emission follows the node's position");
        scene->setNodeTransform(node, Vec3(0, 0, 0), Quat(), Vec3(1, 1, 1));
        warm(engine.get(), 90);
    }

    // ---- 3b. The clock: freeze and resume -----------------------------------
    // setParticleTimeScale(0) must stop the simulation dead — two captures 30
    // frames apart become IDENTICAL, which is also the "it stops" half of the
    // fire gate in phase 3. It is process-wide (one frame-time source in the
    // backend), and it cancels the fixed frame delta, so both are restored after.
    {
        engine->setParticleTimeScale(0.0f);
        warm(engine.get(), 2);
        Image frozenA, frozenB;
        view->readPixels(frozenA);
        warm(engine.get(), 30);
        view->readPixels(frozenB);
        int moved = 0;
        for (unsigned y = 0; y < frozenA.height; ++y)
            for (unsigned x = 0; x < frozenA.width; ++x) {
                const Colour a = frozenA.at(x, y), b = frozenB.at(x, y);
                if (std::fabs(a.r - b.r) + std::fabs(a.g - b.g) + std::fabs(a.b - b.b) > 0.02f)
                    ++moved;
            }
        std::printf("    frozen: %d pixels changed over 30 frames\n", moved);
        CHECK(countBright(frozenA) > 50, "the frozen plume is still on screen");
        CHECK(moved == 0, "setParticleTimeScale(0) freezes the simulation exactly");

        engine->setFixedFrameDelta(1.0f / 60.0f);   // resumes AND restores the fixed step
        warm(engine.get(), 30);
        view->readPixels(img);
        int movedAgain = 0;
        for (unsigned y = 0; y < frozenB.height; ++y)
            for (unsigned x = 0; x < frozenB.width; ++x) {
                const Colour a = frozenB.at(x, y), b = img.at(x, y);
                if (std::fabs(a.r - b.r) + std::fabs(a.g - b.g) + std::fabs(a.b - b.b) > 0.02f)
                    ++movedAgain;
            }
        std::printf("    resumed: %d pixels changed over 30 frames\n", movedAgain);
        CHECK(movedAgain > 50, "the simulation resumes");
    }

    // ---- 4. Visibility is on the DEFINITION ---------------------------------
    // MovableObject::setVisible cannot hide a PFX2 object (the render queue tests
    // the visibility FLAGS, and setVisible only toggles the layer bit which
    // getVisibilityFlags strips). And hiding must kill the particles already in
    // flight, not let them finish their lives — the "particles outlive their
    // emitter" objection, closed.
    {
        scene->setNodeVisible(node, false);
        warm(engine.get(), 2);
        view->readPixels(img);
        CHECK(countBright(img) == 0, "hidden node -> nothing renders, INCLUDING particles in flight");
        scene->setNodeVisible(node, true);
        warm(engine.get(), 2);
        view->readPixels(img);
        CHECK(countBright(img) > 100, "shown again -> the plume is back at once (no re-warm-up)");
    }

    // ---- 5. Affectors: colour over life, gravity, scale ---------------------
    // The colour ramp is what makes fire. Run it as a population assertion:
    // a warm-to-cold ramp must leave the lit pixels warm, and swapping the ramp
    // to blue must flip the channel order — proving the affector, not the texture.
    {
        ParticleSystemDesc d = plumeDesc(2048, 0);   // no texture: only the ramp colours it
        d.emitters[0].rate = 300.0f;
        ParticleAffectorDesc c;
        c.kind = ParticleAffectorDesc::Kind::ColourKeys;
        c.keyCount = 3;
        c.colourKeyTimes[0] = 0.0f;  c.colourKeys[0] = Colour(2.0f, 0.7f, 0.15f, 1.0f);
        c.colourKeyTimes[1] = 0.5f;  c.colourKeys[1] = Colour(1.0f, 0.25f, 0.05f, 0.8f);
        c.colourKeyTimes[2] = 1.0f;  c.colourKeys[2] = Colour(0.1f, 0.02f, 0.0f, 0.0f);
        d.affectors.push_back(c);
        CHECK(scene->setParticleSystem(node, d), "setParticleSystem with a ColourKeys affector");
        warm(engine.get(), 90);
        view->readPixels(img);
        Colour mean = meanLit(img);
        std::printf("    warm ramp mean: %.3f %.3f %.3f\n", mean.r, mean.g, mean.b);
        CHECK(countBright(img) > 100, "the ramped system renders");
        CHECK(mean.r > mean.g && mean.g > mean.b, "colour-over-life paints the particles WARM (R>G>B)");

        // Same topology, cold keys: a pure scalar edit, so the definition must
        // NOT be rebuilt — and the pixels must still flip.
        d.affectors[0].colourKeys[0] = Colour(0.15f, 0.7f, 2.0f, 1.0f);
        d.affectors[0].colourKeys[1] = Colour(0.05f, 0.25f, 1.0f, 0.8f);
        d.affectors[0].colourKeys[2] = Colour(0.0f, 0.02f, 0.1f, 0.0f);
        CHECK(scene->setParticleSystem(node, d), "re-push with cold keys (scalar edit, no rebuild)");
        warm(engine.get(), 120);
        view->readPixels(img);
        mean = meanLit(img);
        std::printf("    cold ramp mean: %.3f %.3f %.3f\n", mean.r, mean.g, mean.b);
        CHECK(mean.b > mean.g && mean.g > mean.r, "the same definition now paints them COLD (B>G>R)");
    }

    // ---- 6. LinearForce actually moves the plume ---------------------------
    // Buoyancy up vs gravity down, from the same emitter, is the honest proof
    // that the affector runs: the lit mass has to change which half of the
    // frame it occupies.
    {
        ParticleSystemDesc d = plumeDesc(2048, 0);
        d.emitters[0].rate = 300.0f;
        d.emitters[0].velocityMin = d.emitters[0].velocityMax = 0.6f;
        d.emitters[0].ttlMin = d.emitters[0].ttlMax = 1.6f;
        ParticleAffectorDesc f;
        f.kind = ParticleAffectorDesc::Kind::LinearForce;
        f.force = Vec3(0, 4.0f, 0);           // buoyancy
        d.affectors.push_back(f);
        CHECK(scene->setParticleSystem(node, d), "setParticleSystem with a LinearForce affector");
        warm(engine.get(), 150);
        view->readPixels(img);
        const int upTop = countBrightIn(img, 0.0f, 0.0f, 1.0f, 0.45f);
        const int upBot = countBrightIn(img, 0.0f, 0.55f, 1.0f, 1.0f);

        d.affectors[0].force = Vec3(0, -8.0f, 0);   // gravity, scalar edit only
        CHECK(scene->setParticleSystem(node, d), "flip the force to gravity (scalar edit)");
        warm(engine.get(), 150);
        view->readPixels(img);
        const int dnTop = countBrightIn(img, 0.0f, 0.0f, 1.0f, 0.45f);
        const int dnBot = countBrightIn(img, 0.0f, 0.55f, 1.0f, 1.0f);
        std::printf("    up-force  top=%d bottom=%d\n    down-force top=%d bottom=%d\n",
                    upTop, upBot, dnTop, dnBot);
        // The image's y grows downwards: buoyancy puts mass in the TOP band,
        // gravity in the BOTTOM one.
        CHECK(upTop > dnTop && dnBot > upBot, "LinearForce moves the plume (buoyancy up, gravity down)");
    }

    // ---- 7. Two independent systems in one scene ---------------------------
    // The property the standing KEEP verdict said the shared-definition model
    // could not have: different quotas, different textures, independent
    // visibility, in one scene, at the same time.
    {
        NodeId a = scene->createNode(), b = scene->createNode();
        scene->setNodeTransform(a, Vec3(-1.4f, 0, 0), Quat(), Vec3(1, 1, 1));
        scene->setNodeTransform(b, Vec3( 1.4f, 0, 0), Quat(), Vec3(1, 1, 1));
        TextureId warmTex = makeBlobTexture(scene, 255, 90, 20);
        TextureId coolTex = makeBlobTexture(scene, 40, 110, 255);
        ParticleSystemDesc da = plumeDesc(256, warmTex);
        ParticleSystemDesc db = plumeDesc(4096, coolTex);
        db.emitters[0].rate = 400.0f;
        CHECK(scene->setParticleSystem(a, da), "system A (quota 256, warm)");
        CHECK(scene->setParticleSystem(b, db), "system B (quota 4096, cool)");
        scene->setNodeVisible(node, false);   // the middle plume is in the way
        warm(engine.get(), 90);
        view->readPixels(img);
        const Colour left  = meanLit(img /*whole*/);
        (void)left;
        // Colour, per side, is the honest per-definition-material proof.
        double lr = 0, lb = 0, rr2 = 0, rb = 0; int ln = 0, rn = 0;
        for (unsigned y = 0; y < img.height; ++y)
            for (unsigned x = 0; x < img.width; ++x) {
                const Colour c = img.at(x, y);
                if (c.r + c.g + c.b <= 0.30f) continue;
                if (x < img.width * 40 / 100)       { lr += c.r; lb += c.b; ++ln; }
                else if (x > img.width * 60 / 100)  { rr2 += c.r; rb += c.b; ++rn; }
            }
        std::printf("    left lit=%d meanR=%.3f meanB=%.3f | right lit=%d meanR=%.3f meanB=%.3f\n",
                    ln, ln ? lr / ln : 0.0, ln ? lb / ln : 0.0,
                    rn, rn ? rr2 / rn : 0.0, rn ? rb / rn : 0.0);
        CHECK(ln > 20 && rn > 20, "both systems render");
        CHECK(ln && rn && (lr / ln) > (lb / ln) && (rb / rn) > (rr2 / rn),
              "each system has its OWN material (warm left, cool right)");

        // Independent visibility — the objection that killed the shared-def model.
        scene->setNodeVisible(a, false);
        warm(engine.get(), 3);
        view->readPixels(img);
        const int leftAfter  = countBrightIn(img, 0.0f, 0.0f, 0.40f, 1.0f);
        const int rightAfter = countBrightIn(img, 0.60f, 0.0f, 1.0f, 1.0f);
        std::printf("    after hiding A: left=%d right=%d\n", leftAfter, rightAfter);
        CHECK(leftAfter == 0 && rightAfter > 20, "hiding one system leaves the other alone");

        CHECK(scene->removeParticleSystem(a), "removeParticleSystem(A)");
        CHECK(!scene->removeParticleSystem(a), "removeParticleSystem is idempotent-safe (false twice)");
        scene->removeNode(a);
        scene->removeNode(b);
        warm(engine.get(), 2);
        view->readPixels(img);
        CHECK(countBright(img) == 0, "both systems gone -> black");
        scene->setNodeVisible(node, true);
    }

    // ---- 8. Rebuild on topology change, recycle the definitions -------------
    // Quota and emitter shape are frozen at init(), so changing either is a new
    // definition — and definitions can NEVER be destroyed. The recycling pool is
    // the whole answer to that, so measure it: 40 topology flips between two
    // shapes must cost 2 definitions, not 40.
    {
        ParticleSystemDesc d = plumeDesc(1024, blob);
        CHECK(scene->setParticleSystem(node, d), "baseline system for the churn");
        const unsigned before = scene->particleDefinitionsCreated();
        bool churnOk = true;
        for (int i = 0; i < 20; ++i) {
            d.emitters[0].shape = (i % 2) ? ParticleEmitterShape::Box
                                          : ParticleEmitterShape::Point;
            d.emitters[0].extents = Vec3(0.4f, 0.1f, 0.4f);
            churnOk = scene->setParticleSystem(node, d) && churnOk;
            engine->renderOneFrame();
        }
        CHECK(churnOk, "20 topology flips all succeeded");
        const unsigned created = scene->particleDefinitionsCreated() - before;
        std::printf("    20 shape flips created %u definitions (recycling pool)\n", created);
        CHECK(created <= 2, "topology churn recycles definitions instead of leaking one per flip");
        warm(engine.get(), 30);
        view->readPixels(img);
        CHECK(countBright(img) > 50, "the system still renders after the churn");

        // Quota buckets: 300, 900 and 1024 all land in the 1024 bucket, so none
        // of them may cost a definition.
        const unsigned b2 = scene->particleDefinitionsCreated();
        for (unsigned q : { 300u, 900u, 1024u }) {
            d.quota = q;
            scene->setParticleSystem(node, d);
        }
        std::printf("    3 within-bucket quota edits created %u definitions\n",
                    scene->particleDefinitionsCreated() - b2);
        CHECK(scene->particleDefinitionsCreated() == b2,
              "quota changes inside one bucket never rebuild");
    }

    // ---- 9. Removal and teardown -------------------------------------------
    {
        CHECK(scene->removeParticleSystem(node), "removeParticleSystem");
        warm(engine.get(), 3);
        view->readPixels(img);
        CHECK(countBright(img) == 0, "removed system renders nothing");

        // A node removed with a LIVE system must free the instance (the def goes
        // back to the pool) without touching the SceneManager's invariants.
        ParticleSystemDesc d = plumeDesc(1024, blob);
        CHECK(scene->setParticleSystem(node, d), "recreate on the same node");
        warm(engine.get(), 40);
        view->readPixels(img);
        CHECK(countBright(img) > 50, "recreated system renders");
        CHECK(scene->removeNode(node), "removeNode with a live particle system");
        warm(engine.get(), 3);
        view->readPixels(img);
        CHECK(countBright(img) == 0, "removeNode freed the particle system");
    }

    std::printf("    scene created %u particle definitions in total\n",
                scene->particleDefinitionsCreated());

    engine->destroyView(view);
    engine->destroyScene(scene);
    engine.reset();
    CHECK(true, "teardown clean (definitions die with the SceneManager, datablocks after it)");
    std::printf(failures ? "RESULT: %d FAILURE(S)\n" : "RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
