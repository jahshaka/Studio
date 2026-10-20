// atom.component (archived: archive/tests/atom) — ATOM IS A COMPONENT OF ITS SCENE (ENGINE-UPGRADE-2 stage 3;
// irisgl/engine/src/atom/AtomComponent.h). Counts only, no picture.
//
// Two scenes, each drawn by its own view with shadows: A holds six Atom cubes, a floor and
// a sun; B two cubes, a floor and a sun. On every recorded frame:
//   1. THE PASS ORDER, by the compositor's own listing (MonitorRecord.passes): in each view's
//      workspace the id pass ("Jahshaka atom id") runs ONCE and before the first screen
//      decode ("Jahshaka atom decode*"); the late pass, where the chain has it, between the
//      two; and every caster pass ("Jahshaka atom caster") directly follows a shadow scene
//      pass of the same node and the same map;
//   2. THE WORDS STEP ONCE A FRAME (the `engine.atomwords` stage, stage 1's instrument);
//   3. EACH SCENE ITS OWN STATE: a pass handed only a SceneManager finds its scene bound on
//      the manager, so A's caster cut counts A's casters and B's counts B's (the same three
//      sun splits each; A's cut draws more caster instances than B's, both > 0), each
//      scene's split is live and shading its own items, and each view's stats ring reads
//      its own cut back (cutValid);
//   4. OWNERS RETIRE WITH THEIR OBJECTS: B's view destroyed (its stats ring retires through
//      the device's window), then B (its caster state retires likewise) — and A goes on
//      drawing its casters frame after frame.
// RED ON THE BASE: nothing here is red on the base except through its companion
// source.atom_no_registries (the registries this replaces, 14 process-wide variables); this
// row guards the behaviour the component must keep.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)

static const unsigned kSize = 128;

static unsigned stageCount(const MonitorRecord &r, const char *name) {
    unsigned n = 0;
    for (const FrameStage &s : r.stages)
        if (s.name == name) ++n;
    return n;
}

static Scene *buildScene(Engine *e, const char *name, int cubes) {
    Scene *s = e->createScene(name);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
    const NodeId floorN = enginetest::addTestCube(s, Colour(0.6f, 0.6f, 0.6f), 0.0f, 0.9f);
    s->setNodeTransform(floorN, Vec3(0.0f, -0.55f, 0.0f), Quat(), Vec3(12.0f, 0.1f, 12.0f));
    for (int i = 0; i < cubes; ++i) {
        const NodeId c = enginetest::addTestCube(s, Colour(0.7f, 0.4f, 0.2f), 0.0f, 0.6f);
        enginetest::setNodePosition(s, c, Vec3(-2.5f + 1.0f * float(i), 0.0f, float(i % 2) - 0.5f));
    }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -1.0f, -0.6f), 3.0f);
    return s;
}

struct Order {
    unsigned frames = 0, good = 0, casters = 0, badCasters = 0, wordsMin = ~0u, wordsMax = 0;
};

/// Rule 1 for the workspace whose name holds `tag`, and the caster rule frame-wide.
static void readOrder(const std::vector<MonitorRecord> &recs, const char *tagA, const char *tagB, Order &o) {
    for (const MonitorRecord &r : recs) {
        if (r.passes.empty()) continue;
        ++o.frames;
        const unsigned w = stageCount(r, "engine.atomwords");
        o.wordsMin = std::min(o.wordsMin, w);
        o.wordsMax = std::max(o.wordsMax, w);
        bool frameOk = true;
        for (const char *tag : { tagA, tagB }) {
            int id = -1, late = -1, decode = -1;
            unsigned nId = 0;
            for (size_t i = 0; i < r.passes.size(); ++i) {
                const FramePass &p = r.passes[i];
                if (p.workspace.find(tag) == std::string::npos) continue;
                if (p.pass == "Jahshaka atom id") { ++nId; if (id < 0) id = int(i); }
                if (p.pass == "Jahshaka atom id late" && late < 0) late = int(i);
                if (p.pass.rfind("Jahshaka atom decode", 0) == 0 && decode < 0) decode = int(i);
            }
            const bool ok = nId == 1 && id >= 0 && decode > id && (late < 0 || (late > id && late < decode));
            if (!ok) {
                frameOk = false;
                std::printf("   frame %llu [%s]: id %d (x%u) late %d decode %d\n", r.frame, tag, id, nId, late, decode);
            }
        }
        for (size_t i = 0; i < r.passes.size(); ++i) {
            const FramePass &p = r.passes[i];
            if (p.pass != "Jahshaka atom caster") continue;
            ++o.casters;
            const FramePass *prev = i ? &r.passes[i - 1] : nullptr;
            if (!prev || prev->node != p.node || prev->workspace != p.workspace ||
                prev->shadowMapIdx != p.shadowMapIdx || p.shadowMapIdx == FramePass::kNoShadowMap) {
                ++o.badCasters;
                frameOk = false;
            }
        }
        if (frameOk) ++o.good;
    }
}

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-component-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();

    View *va = e->createOffscreenView("atomcompA", kSize, kSize, Colour(0, 0, 0));
    View *vb = e->createOffscreenView("atomcompB", kSize, kSize, Colour(0, 0, 0));
    Scene *a = buildScene(e, "atomcompA", 6);
    Scene *b = buildScene(e, "atomcompB", 2);
    va->setScene(a);
    vb->setScene(b);
    va->setShadows(true);
    vb->setShadows(true);
    enginetest::testCameraLookAt(va, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 0.0f, 0.0f));
    enginetest::testCameraLookAt(vb, Vec3(0.0f, 3.0f, 6.0f), Vec3(0.0f, 0.0f, 0.0f));
    enginetest::frames(e, 12);

    const AtomDrawStatus sa0 = a->atomDrawStatus();
    if (!sa0.on) {
        std::printf("ok: the split is off on this device (no id pass) — atom.component skips\n");
        return 0;
    }

    e->setFrameMonitor(MonitorLevel::Review);
    {
        std::vector<MonitorRecord> drop;
        enginetest::frames(e, 1);
        e->popMonitorRecords(drop);
    }
    enginetest::frames(e, 4);
    e->setFrameMonitor(MonitorLevel::Off);
    std::vector<MonitorRecord> recs;
    e->popMonitorRecords(recs);
    Order o;
    readOrder(recs, "atomcompA", "atomcompB", o);
    const AtomDrawStatus sa = a->atomDrawStatus(), sb = b->atomDrawStatus();
    std::printf("   %u frames recorded, %u in order; %u caster passes (%u out of place); atomwords/frame %u..%u\n",
                o.frames, o.good, o.casters, o.badCasters, o.wordsMin, o.wordsMax);
    std::printf("   A: atom items %u, screen draws %u, cut %d, caster maps %u (instances %u)\n", sa.atomItems,
                sa.screenDraws, sa.cutValid ? 1 : 0, sa.casterMaps, sa.casterInstances);
    std::printf("   B: atom items %u, screen draws %u, cut %d, caster maps %u (instances %u)\n", sb.atomItems,
                sb.screenDraws, sb.cutValid ? 1 : 0, sb.casterMaps, sb.casterInstances);
    CHECK(o.frames > 0 && o.good == o.frames,
          "EVERY FRAME, EACH VIEW: the id pass once, before the first screen decode (the late pass between), and "
          "every caster pass right after its map's shadow scene pass");
    CHECK(o.casters > 0, "...the caster passes ran (not vacuous)");
    CHECK(o.frames > 0 && o.wordsMin == 1u && o.wordsMax == 1u, "ATOM'S WORDS STEP ONCE A FRAME (engine.atomwords)");
    CHECK(sa.on && sb.on && sa.atomItems >= 7u && sb.atomItems >= 3u && sa.screenDraws > 0u && sb.screenDraws > 0u,
          "each scene's split is live and shades its own items through its screen decode");
    CHECK(sa.cutValid && sb.cutValid, "each view's stats ring read its own cut back");
    CHECK(sa.casterValid && sb.casterValid && sa.casterMaps > 0u && sa.casterMaps == sb.casterMaps &&
              sb.casterInstances > 0u && sa.casterInstances > sb.casterInstances,
          "EACH SCENE ITS OWN CASTER STATE: the same maps each, A's cut counting A's casters and B's B's");

    // ---- 4. the owners retire with their objects ------------------------------------
    e->destroyView(vb);
    enginetest::frames(e, 3);
    e->destroyScene(b);
    enginetest::frames(e, 12);
    e->setFrameMonitor(MonitorLevel::Review);
    {
        std::vector<MonitorRecord> drop;
        enginetest::frames(e, 1);
        e->popMonitorRecords(drop);
    }
    enginetest::frames(e, 3);
    e->setFrameMonitor(MonitorLevel::Off);
    recs.clear();
    e->popMonitorRecords(recs);
    Order o2;
    readOrder(recs, "atomcompA", "atomcompA", o2);
    const AtomDrawStatus sa2 = a->atomDrawStatus();
    std::printf("   after B went: %u frames, %u in order, %u caster passes; A's caster maps %u (instances %u)\n",
                o2.frames, o2.good, o2.casters, sa2.casterMaps, sa2.casterInstances);
    CHECK(o2.frames > 0 && o2.good == o2.frames && o2.casters > 0 && sa2.casterValid &&
              sa2.casterMaps == sa.casterMaps && sa2.casterInstances == sa.casterInstances,
          "B's view and B gone (their rings retired through the device's window): A draws on, its cut unchanged");

    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
