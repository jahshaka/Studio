// atom.cut_overflow_whole / atom.still_does_nothing (ATOM-TESTS-BACK-1 stage 4; SPECS/ATOM_TESTS_BACK_1_SPEC.md). COUNTS,
// NEVER MS. One scene: a grid of the closed sphere (tests/support/clustermesh.h, baked by the product's bake) in one view.
//
// atom.cut_overflow_whole — AN OVERFLOW DRAWS EVERY OBJECT WHOLE AND GROWS THE BUDGET ONCE, TO WHAT WAS ASKED
// (irisgl/engine/src/OgreGpuCull.cpp ensureCut :97, noteCutOverflow :162-177; JahCullCut_cs.glsl :595-645). The scene's
// cut at rest gives I (indices asked: counter[11], the FULL main ask of every survivor, :599), C (records: counter[8], all
// of them while everything fits) and R (the root cuts' reserve, AtomDrawStatus::cutReserveIndices = min(R, budget / 2)).
// The test door (Scene::setAtomCutBudgetForTest, Engine.h:957-962) forces the budget to start = I / 8 and ignores the
// scene's high-water mark (the scene's lodBias 0.5 makes the cut fine enough that the reserve is whole under the door:
// start = I / 8 >= 2 R, asserted as a premise - the first run, at lodBias 1, had I / 8 = 17668 < 2 R = 21168); then:
//   (a) WHOLE: in the frames the door's budget records, cutOverflow > 0 (instances drawn COARSE, their root cut from the
//       reserve) and cutMissing = 0 (nothing drawn NOTHING: start >= 2 R by the fixture, so the reserve is whole); and on
//       one such frame every instance in view keeps >= 1 id pixel.
//   (b) THE BUDGET'S CLOSED FORM: noteCutOverflow doubles from start until want - min(R, want / 2) >= I and
//       want / kCutIndicesPerRecord (24, GpuCull.h) >= C, so the settled budget is start x 2^k, k the least with both -
//       never a doubling per report (the ring reports one overflow per frame in flight: a blind doubling overshot a
//       209 k need to 4.4 M, the archived atom.cluster_crack's part E). The overflow frames ask C_ov <= C records (only
//       those that fit reserve one, :602), so C is the bound's own upper read: the row prints whether the record term binds.
//   (c) ONCE, AND SOON: the budget changes once, and the reads of an overflow are consecutive and at most F + 2 - the last
//       F + 1 frames after the first, as the spec words it ("within frames-in-flight + 1 frames overflow = 0"). F = the
//       frames in flight read from the device (VaoManager::getDynamicBufferMultiplier, 3 on this stack: the first run).
//       From the code: the stats ring has F + 1 slots (OgreAtomIdPass.cpp:398) and a slot is read when it is reused, F + 1
//       frames after its write (:398-402); a frame writes the LAST cull's counters (:386-389), and its read and the
//       budget's growth (noteCutOverflow) run BEFORE its own request (cycleStats :897, then recordGpuCull :904, which
//       calls ensureCut). So the cull of frame m is read at frame m + 1 + (F + 1), and that frame's own cull is the first
//       at the grown budget: the culls m0 .. m0 + F + 1 overflow, F + 2 of them, read on F + 2 consecutive frames.
//   Negative control: none needed beyond (a)'s own (> 0 proves the door bit); (b) is an equality.
//
// atom.still_does_nothing — A STILL FRAME DOES NO ATOM WORK: after the scene settles, 16 frames of nothing moving leave
// AtomDrawStatus::wordSlotVisits (the change feed's slots the split's words visited, cumulative) and cutIndexBudget
// unchanged. NEGATIVE CONTROL: one sphere moved, two frames: wordSlotVisits rises (the counter counts).
#include "atom_fixture.h"
#include "cluster_fixtures.h"
#include "../support/clustermesh.h"

#include "EnginePrivate.h"
#include "GpuScene.h"
#include "HlmsAtom.h"

#include <OgreRenderSystem.h>
#include <OgreRoot.h>
#include <Vao/OgreVaoManager.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>
#include "jahshaka/engine/Atom.h"
#include "jahshaka/engine/GiModes.h"
#include "jahshaka/engine/GiParams.h"
#include "jahshaka/engine/Ids.h"
#include "jahshaka/engine/Materials.h"
#include "jahshaka/engine/Math.h"

using namespace jahshaka::engine;
using jahshaka::engine::detail::AtomId;
using jahshaka::engine::detail::OgreScene;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const int kCols = 7, kRows = 4;
static const unsigned kCutIndicesPerRecord = 24u;   // GpuCull.h kCutIndicesPerRecord
/// Frames for the scene's cut and its read-back to settle before a reading (two reads apart equal, at most this many).
static const int kSettleCap = 40;

static unsigned slotOf(Scene *s, NodeId n)
{
    auto *os = static_cast<OgreScene *>(s);
    os->ensureGpuScene(false);
    const detail::GpuScene &gs = os->gpuScene();
    for (unsigned k = 0; k < gs.slotCount(); ++k)
        if (os->nodeOfGpuKey(gs.entry(k).ids[0]) == n) return k;
    return 0xFFFFFFFFu;
}

/// Frames until the cut's read-back stands still (indices and budget equal two frames running), no overflow.
static bool settle(Engine *e, Scene *s, AtomDrawStatus &st, int &frames)
{
    AtomDrawStatus prev;
    for (frames = 1; frames <= kSettleCap; ++frames) {
        enginetest::frames(e, 1);
        st = s->atomDrawStatus();
        if (frames > 2 && st.cutValid && prev.cutValid && st.cutIndices == prev.cutIndices &&
            st.cutIndexBudget == prev.cutIndexBudget && st.cutClusters == prev.cutClusters && st.cutOverflow == 0u)
            return true;
        prev = st;
    }
    return false;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode != "overflow" && mode != "still") {
        std::printf("FAIL: usage: test_atom_cut_overflow overflow|still\n");
        return 2;
    }
    std::printf("== atom.%s\n", mode == "overflow" ? "cut_overflow_whole" : "still_does_nothing");
    clusterfix::Fixture f;
    if (!clusterfix::bake(f, clusterfix::meshFromShell(clustermesh::closedSphere()), "closed-sphere") ||
        f.data.clusterGroups.empty()) {
        std::printf("FAIL: the closed sphere bakes to no DAG\n");
        return 1;
    }
    atomfix::Rig r;
    const atomfix::Lens lens = atomfix::sized(atomfix::kCentre, 960.0, 540.0);
    if (!atomfix::boot(r, "test-atom-cut-" + mode + "-ogre.log", { lens })) {
        std::printf("FAIL: boot: %s\n", r.error.c_str());
        return 1;
    }
    Engine *e = r.engine.get();
    Scene *s = r.scene;
    View *v = r.views[0].view;
    {
        GiParams gi;
        gi.mode = GiMode::Off;
        s->setGlobalIllumination(gi);
    }
    PbrParams pp;
    pp.albedo = Colour(0.7f, 0.7f, 0.7f);
    pp.roughness = 0.6f;
    const MaterialId mat = s->createPbrMaterial(pp);
    const MeshId mesh = s->createMesh(f.data);
    std::vector<NodeId> nodes;
    for (int j = 0; j < kRows; ++j)
        for (int i = 0; i < kCols; ++i) {
            const NodeId n = s->createNode();
            s->attachMesh(n, mesh, mat);
            s->setNodeTransform(n, Vec3(-2.7f + 0.9f * float(i), -1.2f + 0.8f * float(j), -4.0f), Quat(), Vec3(1, 1, 1));
            nodes.push_back(n);
        }
    enginetest::addDirectionalLight(s, Vec3(-0.3f, -0.5f, -0.8f), 2.0f);
    enginetest::testCameraAt(v, Vec3(0.0f, 0.0f, 0.0f));
    s->setLodBias(0.5f);
    AtomDrawStatus st;
    int fr = 0;
    const bool settled = settle(e, s, st, fr);
    std::printf("   at rest (%d frames): items %u, clusters %u, indices %u, budget %u, reserve %u, overflow %u, missing %u, "
                "late clusters %u, word slot visits %llu\n", fr, st.atomItems, st.cutClusters, st.cutIndices,
                st.cutIndexBudget, st.cutReserveIndices, st.cutOverflow, st.cutMissing, st.cutLateClusters,
                st.wordSlotVisits);
    CHECK_MSG(settled && st.on && st.atomItems == unsigned(nodes.size()) && st.cutIndices > 0u,
              "the scene's cut settles with every sphere on Atom (%u of %zu, %d frames)", st.atomItems, nodes.size(), fr);
    if (!settled) return 1;

    if (mode == "still") {
        const AtomDrawStatus a = st;
        enginetest::frames(e, 16);
        const AtomDrawStatus b = s->atomDrawStatus();
        std::printf("   16 still frames: word slot visits %llu -> %llu, budget %u -> %u, indices %u -> %u\n",
                    a.wordSlotVisits, b.wordSlotVisits, a.cutIndexBudget, b.cutIndexBudget, a.cutIndices, b.cutIndices);
        CHECK_MSG(b.wordSlotVisits == a.wordSlotVisits && b.cutIndexBudget == a.cutIndexBudget,
                  "A STILL FRAME DOES NO ATOM WORK: 16 still frames visit no word slot (%llu) and re-size no cut (%u)",
                  b.wordSlotVisits - a.wordSlotVisits, b.cutIndexBudget);
        s->setNodeTransform(nodes[0], Vec3(-2.7f, -1.2f, -4.05f), Quat(), Vec3(1, 1, 1));
        enginetest::frames(e, 2);
        const AtomDrawStatus c = s->atomDrawStatus();
        CHECK_MSG(c.wordSlotVisits > b.wordSlotVisits,
                  "NEGATIVE CONTROL: one sphere moved, the words visit its slot (%llu -> %llu)", b.wordSlotVisits,
                  c.wordSlotVisits);
    } else {
        Ogre::VaoManager *vao = Ogre::Root::getSingleton().getRenderSystem()->getVaoManager();
        const unsigned F = vao ? unsigned(vao->getDynamicBufferMultiplier()) : 0u;
        const unsigned I = st.cutIndices, C = st.cutClusters, R = st.cutReserveIndices;
        const unsigned start = I / 8u;
        // the closed form (b)
        auto reserveFor = [&](unsigned long long w) { return std::min<unsigned long long>(R, w / 2u); };
        unsigned long long want = start;
        int k = 0;
        while (want - reserveFor(want) < I || want / kCutIndicesPerRecord < C) { want *= 2u; ++k; }
        unsigned long long wantIdx = start;
        while (wantIdx - reserveFor(wantIdx) < I) wantIdx *= 2u;
        std::printf("   frames in flight F = %u; I %u, C %u, R %u, start = I / 8 = %u; the closed form: %llu = start x 2^%d "
                    "(the index term alone %llu: the record term %s)\n", F, I, C, R, start, want, k, wantIdx,
                    want > wantIdx ? "BINDS" : "does not bind");
        CHECK_MSG(F >= 1u && st.cutLateClusters == 0u && start >= 2u * R,
                  "the fixture's premises: F read (%u), nothing late at rest (%u: I is the first list's ask), the reserve "
                  "whole under the door (start %u >= 2 R = %u)", F, st.cutLateClusters, start, 2u * R);
        s->setAtomCutBudgetForTest(start);
        unsigned overflowFrames = 0, missing = 0, changes = 0, lastBudget = st.cutIndexBudget, seenStart = 0, runs = 0;
        bool wasOver = false;
        const int trace = int(2u * F + 6u);
        for (int j = 1; j <= trace; ++j) {
            enginetest::frames(e, 1);
            const AtomDrawStatus t = s->atomDrawStatus();
            std::printf("     frame %2d: overflow %u, missing %u, budget %u, indices %u\n", j, t.cutOverflow, t.cutMissing,
                        t.cutIndexBudget, t.cutIndices);
            overflowFrames += t.cutOverflow > 0u;
            runs += (t.cutOverflow > 0u && !wasOver);
            wasOver = t.cutOverflow > 0u;
            missing += t.cutMissing;
            if (t.cutIndexBudget == start) seenStart = 1;
            if (t.cutIndexBudget != lastBudget && t.cutIndexBudget != start) ++changes;
            lastBudget = t.cutIndexBudget;
        }
        const AtomDrawStatus end = s->atomDrawStatus();
        CHECK_MSG(overflowFrames > 0u && missing == 0u,
                  "(a) WHOLE: the door's frames overflowed (%u frames read it) and nothing was drawn nothing (missing %u)",
                  overflowFrames, missing);
        CHECK_MSG(end.cutIndexBudget == want && end.cutOverflow == 0u,
                  "(b) the budget settles at the closed form start x 2^k = %llu (read %u), the overflow gone", want,
                  end.cutIndexBudget);
        CHECK_MSG(changes == 1u && overflowFrames <= F + 2u && runs == 1u,
                  "(c) ONCE, AND SOON: the budget grew %u time(s) after the door (seen at start: %u); %u frames read an "
                  "overflow, consecutive (%u run), <= F + 2 = %u", changes, seenStart, overflowFrames, runs, F + 2u);
        // (a)'s pixels: the door again, then frames until the budget the frame records is the door's (the trace: the
        // frame after the door's message), and that frame's id image (a stall: read after the counted trace)
        s->setAtomCutBudgetForTest(start);
        int doorFrames = 0;
        do {
            enginetest::frames(e, 1);
            ++doorFrames;
        } while (s->atomDrawStatus().cutIndexBudget != start && doorFrames < 8);
        const atomfix::IdImage ids = atomfix::readIds(r, r.views[0]);
        std::vector<unsigned> px(nodes.size(), 0u);
        std::vector<unsigned> slots;
        for (NodeId n : nodes) slots.push_back(slotOf(s, n));
        if (ids.ok)
            for (size_t i = 0; i < size_t(ids.width) * ids.height; ++i) {
                const uint32_t x = ids.words[2 * i];
                if (x == AtomId::kEmpty) continue;
                for (size_t k2 = 0; k2 < slots.size(); ++k2)
                    if ((x & AtomId::kSlotMask) == slots[k2]) ++px[k2];
            }
        const unsigned least = px.empty() ? 0u : *std::min_element(px.begin(), px.end());
        const AtomDrawStatus door = s->atomDrawStatus();
        std::printf("   the door's frame (%d after the door, budget %u): the fewest id pixels of an instance %u\n",
                    doorFrames, door.cutIndexBudget, least);
        CHECK_MSG(ids.ok && least >= 1u,
                  "(a) WHOLE: on the door's frame every one of the %zu instances keeps >= 1 id pixel (fewest %u)",
                  nodes.size(), least);
    }
    atomfix::shutdown(r);
    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
