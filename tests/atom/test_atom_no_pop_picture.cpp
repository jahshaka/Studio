// atom.no_pop.picture — THE RASTERISED IDS MOVE AS THE CPU SAYS (ATOM-TESTS-BACK-1 stage 2, the lead's ruling 1: the no-pop
// law is a CPU sweep; ONE card row confirms the device's picture agrees with it). An engine fixture, GI off by construction
// (an id image only: no lighting term is read).
//
// ONE S1 BRACKET: the closed sphere (tests/support/clustermesh.h) baked by the product's bake, one instance at the origin,
// ONE scene shown by two offscreen views - the desktop lens (45 deg vertical, 16:9, 960 x 540) aimed at the frame's
// CENTRE and at its CORNER. The group g is chosen on the CPU: a reduction (it produced clusters) whose flip is ISOLATED in
// both views (no other group's t* within 1e-3 of it: atom.switch_bracket's condition), each view's camera placed so g flips
// at t = 1 there (atom_fixture.h placeFlip), the one with the largest switch the independent measure reads in the smaller
// of the two views. The scene's tolerance is then 1 - 1e-3 and 1 + 1e-3; each view's id image is read once settled (two
// equal reads in a row: the engine has no wall clock, a cut lands within the frames in flight).
// THE LAWS, per view:
//   (1) THE IDS: every pixel whose id words changed names, where it is covered, one of g's MEMBERS before and one of g's
//       OUTPUTS after (y >> 8, the mesh-local cluster index - read on the first card run: atom_fixture.h IdImage; coverage
//       from x, the empty marker's word); and some pixel changed (the bracket is seen).
//   NEGATIVE CONTROLS: the background reads empty in both pictures (a reader taking y for coverage sees none); a copy of
//   the after image with one changed pixel's cluster perturbed to a non-output reads foreign.
//   (2) THE COVERAGE: every pixel covered on one side of the flip and not the other lies within Chebyshev ceil(switch_px)
//       of a pixel covered on both - switch_px the CPU's reading of THAT view (atom.no_pop.centre's measure): the picture
//       moves no farther than the measure says.
#include "atom_fixture.h"
#include "cluster_fixtures.h"
#include "../support/clustermesh.h"
#include "../support/lawline.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using namespace atomfix;

static int gFailures = 0;
static void check(bool ok, const std::string &what)
{
    std::printf("%s: %s\n", ok ? "ok" : "FAIL", what.c_str());
    std::fflush(stdout);
    if (!ok) ++gFailures;
}

/// Whether g's flip in `l` is isolated: no other finite group's t* within 1e-3 of g's at g's placement.
static bool isolated(const MeshData &d, size_t g, const Lens &l, const Placement &p)
{
    const ClusterCutView cv = cutView(l, p.translate, 1.0f, 0.0f);
    for (size_t h = 0; h < d.clusterGroups.size(); ++h) {
        const MeshClusterGroup &gr = d.clusterGroups[h];
        if (h == g || !(gr.error > 0.0f) || gr.error >= kClusterTerminal) continue;
        const float F = clusterGroupFootprint(gr, cv);
        if (!(F > 0.0f)) continue;
        const double th = double(gr.error) * double(cv.scale) / double(F);
        if (th >= p.t * (1.0 - 1e-3) && th <= p.t * (1.0 + 1e-3)) return false;
    }
    return true;
}

/// One view's two pictures judged (named fields): the changed pixels, those naming a cluster outside g's members before or
/// outputs after, the coverage changes and those farther than Chebyshev R of a pixel covered on both.
struct Judgement
{
    size_t changed = 0, foreign = 0, coverage = 0, far = 0, covered = 0;
    std::set<uint32_t> seenLo, seenHi;
};
static Judgement judgeView(const IdImage &lo, const IdImage &hi, const std::set<uint32_t> &members,
                           const std::set<uint32_t> &outputs, int R)
{
    Judgement j;
    const size_t n = size_t(lo.width) * lo.height;
    for (size_t i = 0; i < n; ++i) {
        j.covered += lo.covered(i) ? 1u : 0u;
        if (lo.words[2 * i] == hi.words[2 * i] && lo.words[2 * i + 1] == hi.words[2 * i + 1]) continue;
        ++j.changed;
        if (lo.covered(i)) { j.seenLo.insert(lo.cluster(i)); if (!members.count(lo.cluster(i))) ++j.foreign; }
        if (hi.covered(i)) { j.seenHi.insert(hi.cluster(i)); if (!outputs.count(hi.cluster(i))) ++j.foreign; }
        if (lo.covered(i) == hi.covered(i)) continue;
        ++j.coverage;
        const int x = int(i % lo.width), y = int(i / lo.width);
        bool near = false;
        for (int dy = -R; dy <= R && !near; ++dy)
            for (int dx = -R; dx <= R && !near; ++dx) {
                const int xx = x + dx, yy = y + dy;
                if (xx < 0 || yy < 0 || xx >= int(lo.width) || yy >= int(lo.height)) continue;
                const size_t k = size_t(yy) * lo.width + size_t(xx);
                near = lo.covered(k) && hi.covered(k);
            }
        if (!near) ++j.far;
    }
    return j;
}

/// Frames until the view's id image reads the same twice in a row (cap 12 after 3 to start); the frames rendered added.
static IdImage settled(Rig &r, const RigView &v, int &frames)
{
    enginetest::frames(r.engine.get(), 3);
    frames += 3;
    IdImage last = readIds(r, v);
    for (int i = 0; i < 12; ++i) {
        enginetest::frames(r.engine.get(), 1);
        ++frames;
        IdImage now = readIds(r, v);
        if (now.ok && last.ok && now.words == last.words) return now;
        last = std::move(now);
    }
    last.ok = false;
    return last;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("== atom.no_pop.picture ==\n");
    clusterfix::Fixture f;
    if (!clusterfix::bake(f, clusterfix::meshFromShell(clustermesh::closedSphere()), "closed-sphere") ||
        f.data.clusters.empty()) {
        std::printf("FAIL: the closed sphere bakes to no DAG\natom.no_pop.picture: FAILED (1)\n");
        return 1;
    }
    const MeshData &d = f.data;
    const Lens lenses[2] = { sized(kCentre, 960.0, 540.0), sized(kCorner, 960.0, 540.0) };

    // THE GROUP, chosen on the CPU
    int g = -1;
    double best = -1.0;
    Flip chosen[2];
    for (size_t k = 0; k < d.clusterGroups.size(); ++k) {
        const MeshClusterGroup &gr = d.clusterGroups[k];
        if (!(gr.error > 0.0f) || gr.error >= kClusterTerminal) continue;
        bool produced = false;
        for (const MeshCluster &c : d.clusters) produced = produced || c.refined == int(k);
        if (!produced) continue;
        Flip fl[2];
        bool ok = true;
        for (int v = 0; v < 2 && ok; ++v) {
            ok = isolated(d, k, lenses[v], placeFlip(gr, lenses[v], 1.0));
            if (ok) fl[v] = readFlip(d, int(k), lenses[v], 1.0);
            ok = ok && fl[v].counted;
        }
        if (ok && std::min(fl[0].px, fl[1].px) > best) {
            best = std::min(fl[0].px, fl[1].px);
            g = int(k);
            chosen[0] = fl[0];
            chosen[1] = fl[1];
        }
    }
    check(g >= 0, "the closed sphere has a reduction group isolated in both views");
    if (g < 0) { std::printf("atom.no_pop.picture: FAILED (%d)\n", gFailures); return 1; }
    std::set<uint32_t> members, outputs;
    for (size_t c = 0; c < d.clusters.size(); ++c) {
        if (d.clusters[c].group == g) members.insert(uint32_t(c));
        if (d.clusters[c].refined == g) outputs.insert(uint32_t(c));
    }
    std::printf("   the group %d (depth %d): %zu members -> %zu outputs; the CPU's switch: centre %.4f px at t %.6f, corner "
                "%.4f px at t %.6f\n", g, d.clusterGroups[size_t(g)].depth, members.size(), outputs.size(), chosen[0].px,
                chosen[0].t, chosen[1].px, chosen[1].t);

    // THE PICTURE
    Rig r;
    if (!boot(r, "test-atom-no-pop-picture-ogre.log", { lenses[0], lenses[1] })) {
        std::printf("FAIL: boot: %s\natom.no_pop.picture: FAILED (1)\n", r.error.c_str());
        return 1;
    }
    r.scene->setLodBias(1.0f);
    PbrParams pp;
    pp.albedo = Colour(0.7f, 0.7f, 0.7f);
    pp.roughness = 0.6f;
    const MaterialId mat = r.scene->createPbrMaterial(pp);
    const MeshId mesh = r.scene->createMesh(d);
    const NodeId node = r.scene->createNode();
    r.scene->attachMesh(node, mesh, mat);
    r.scene->setNodeTransform(node, Vec3(0.0f, 0.0f, 0.0f), Quat(), Vec3(1.0f, 1.0f, 1.0f));
    for (int v = 0; v < 2; ++v) placeCamera(r.views[size_t(v)], placeFlip(d.clusterGroups[size_t(g)], lenses[v], 1.0));
    int frames = 0;
    IdImage before[2], after[2];
    r.scene->setPixelTolerance(float(1.0 - 1e-3));
    for (int v = 0; v < 2; ++v) before[v] = settled(r, r.views[size_t(v)], frames);
    r.scene->setPixelTolerance(float(1.0 + 1e-3));
    for (int v = 0; v < 2; ++v) after[v] = settled(r, r.views[size_t(v)], frames);
    std::printf("   %d frames rendered\n", frames);

    for (int v = 0; v < 2; ++v) {
        const std::string at = lenses[v].name;
        const IdImage &lo = before[v], &hi = after[v];
        check(lo.ok && hi.ok && lo.width == hi.width && lo.height == hi.height,
              at + ": both id images read back and settled");
        if (!(lo.ok && hi.ok && lo.width == hi.width && lo.height == hi.height)) continue;
        const int R = int(std::ceil(chosen[v].px));
        const Judgement j = judgeView(lo, hi, members, outputs, R);
        const size_t changed = j.changed, foreign = j.foreign, coverage = j.coverage, far = j.far, covered = j.covered;
        const std::set<uint32_t> &seenLo = j.seenLo, &seenHi = j.seenHi;
        std::printf("   %-7s %zu covered, %zu changed ids (%zu naming a cluster outside g's members -> outputs), %zu "
                    "coverage changes, %zu of them farther than Chebyshev %d of a pixel covered on both\n", at.c_str(),
                    covered, changed, foreign, coverage, far, R);
        std::printf("   %-7s clusters seen before:", at.c_str());
        for (uint32_t c : seenLo) std::printf(" %u", c);
        std::printf("; after:");
        for (uint32_t c : seenHi) std::printf(" %u", c);
        std::printf("\n");
        enginetest::lawLine((at + ":foreign").c_str(), double(foreign), "q", "==", 0.0);
        enginetest::lawLine((at + ":far").c_str(), double(far), "q", "==", 0.0);
        check(changed > 0, at + ": the bracket changes the picture (g's flip is seen)");
        check(foreign == 0, at + ": every changed id names g's members before and g's outputs after (y >> 8)");
        check(far == 0, at + ": every coverage change lies within ceil(switch_px) of a pixel covered on both");
        // THE NEGATIVE CONTROLS (the lead's condition: the row cannot pass the way its first run "worked").
        // (i) THE BACKGROUND IS SEEN: the sphere covers part of each frame, so both pictures hold empty pixels by x; a
        //     reader that took y for coverage saw none (the first run: 518,400 of 518,400 "covered"), and fails here.
        size_t emptyLo = 0, emptyHi = 0, emptyByY = 0;
        for (size_t i = 0; i < size_t(lo.width) * lo.height; ++i) {
            emptyLo += lo.covered(i) ? 0u : 1u;
            emptyHi += hi.covered(i) ? 0u : 1u;
            emptyByY += lo.words[2 * i + 1] == 0xFFFFFFFFu ? 1u : 0u;
        }
        std::printf("   %-7s empty pixels by x: %zu before, %zu after (a reader taking y for coverage would find %zu)\n",
                    at.c_str(), emptyLo, emptyHi, emptyByY);
        check(emptyLo > 0 && emptyHi > 0, at + ": the negative control - the background is read as empty in both pictures");
        // (ii) A FOREIGN ID FAILS: a copy of the after image with one changed covered pixel's cluster set to a cluster that
        //      is not g's output must read foreign.
        IdImage forced = hi;
        size_t at_ = SIZE_MAX;
        for (size_t i = 0; i < size_t(lo.width) * lo.height && at_ == SIZE_MAX; ++i)
            if (hi.covered(i) && (lo.words[2 * i] != hi.words[2 * i] || lo.words[2 * i + 1] != hi.words[2 * i + 1])) at_ = i;
        if (at_ != SIZE_MAX) {
            uint32_t other = 0;
            while (outputs.count(other)) ++other;
            forced.words[2 * at_ + 1] = (other << 8) | (forced.words[2 * at_ + 1] & 0xFFu);
        }
        const Judgement jf = judgeView(lo, forced, members, outputs, R);
        std::printf("   %-7s the forced copy (pixel %zu's cluster -> a non-output): %zu foreign\n", at.c_str(), at_, jf.foreign);
        check(at_ != SIZE_MAX && jf.foreign > foreign, at + ": the negative control - a perturbed cluster id reads foreign");
    }
    shutdown(r);
    if (gFailures == 0) std::printf("atom.no_pop.picture: PASSED\n");
    else std::printf("atom.no_pop.picture: FAILED (%d)\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
