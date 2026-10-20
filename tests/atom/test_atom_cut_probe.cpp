// atom.cut_probe — THE CUT PROBE AND THE DENSITY VIEW READ WHAT THE CUT DID (ATOM-2 stage 1; SPECS/ATOM-2.md).
//
// Scene::armAtomCutProbe records ONE frame of the id pass's cluster cut on the device: every group of every survivor's
// DAG with the terms the cut job judged it on, the clusters it drew and the frame's visible pixels per object. The
// fixture: a baked UV sphere (a real DAG, 8,192 triangles, radius 0.5 m) attached twice - (A) as authored in metres at
// scale 1, and (B) the SAME DAG authored in centimetres (every length of the bake x 100) at scale 0.01: the same object
// in the world - side by side at equal distances from the eye.
//   (a) THE PROBE RECORDS EVERY GROUP ONCE: each instance's records are exactly its mesh's groups, none dropped;
//   (b) THE VERDICT IS THE RULE'S: each record's verdict and 'allowed' equal the C++ twin (Types.h clusterGroupAllowed
//       / clusterGroupAffordable) recomputed from the probe's own request and the instance's world rows;
//   (c) THE UNITS (the scale term): (B)'s allowed is (A)'s x 100 group for group, its verdicts are (A)'s and it draws
//       (A)'s triangles - a mesh authored in centimetres is cut exactly as the same object authored in metres;
//   (d) THE DRAWN CLUSTERS ARE THE RULE'S: each instance's drawn triangles = the C++ cut over the probe's verdicts;
//   (e) THE PIXELS ARE THE ID IMAGE'S: each instance's counted pixels = its slot's pixels in the id image read back;
//   (f) THE DENSITY VIEW: every pixel of each sphere is painted the class of its BASE mesh's level-0 triangles per
//       visible pixel (the probe's figure), and atomStatus().density says the same pixels and classes; and with the
//       count's compute jobs missing (a test door) it paints nothing and reports no figures (BUGS-1 item 6);
//   (g) THE PROBE IS ONE FRAME: the cut job's probe permutation is gone the frame after (no group is recorded twice
//       and a second read without an arming answers "not armed"); a probe DISARMED before its frame records no later
//       frame (BUGS-1 item 7, the verb's timeout);
//   (h) THE TIER'S THRESHOLD (ATOM-3 stage 1): at the World Mode's 1 / 2 / 3 px the drawn triangles fall strictly.
// FRAMES, NEVER TIME: every reading follows a counted frame.
// The engine's private headers BEFORE the document's (Qt's keyword macros - emit, slots - would rewrite them).
#include "EnginePrivate.h"
#include "GpuScene.h"
#include "HlmsAtom.h"

#include <Compositor/OgreCompositorNode.h>
#include <Compositor/OgreCompositorWorkspace.h>
#include <OgreImage2.h>
#include <OgreTextureBox.h>

#include "jahshaka/engine/Engine.h"
#include "cluster_fixtures.h"
#include "../support/enginetesthelpers.h"

#include <QCoreApplication>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace jahshaka::engine;
using jahshaka::engine::detail::AtomId;
using jahshaka::engine::detail::OgreScene;
using jahshaka::engine::detail::OgreView;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[720];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 480, kH = 270;

/// A UV sphere whose last column and ring are WRAPPED BY INDEX (DOCS/traps/ENGINE.md: procedural seams do not weld),
/// `seg` x `ring`, radius `r`.
static iris::MeshPtr wrappedSphere(int seg, int ring, float r)
{
    std::vector<float> pos, nrm, uv;
    std::vector<unsigned> idx;
    // the two poles, then ring 1..ring-1 of `seg` vertices each
    pos.insert(pos.end(), { 0.0f, r, 0.0f });
    nrm.insert(nrm.end(), { 0.0f, 1.0f, 0.0f });
    uv.insert(uv.end(), { 0.5f, 0.0f });
    for (int j = 1; j < ring; ++j) {
        const float th = float(j) / float(ring) * 3.14159265f;
        for (int i = 0; i < seg; ++i) {
            const float ph = float(i) / float(seg) * 6.28318531f;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = -std::sin(th) * std::sin(ph);
            pos.insert(pos.end(), { r * x, r * y, r * z });
            nrm.insert(nrm.end(), { x, y, z });
            uv.insert(uv.end(), { float(i) / float(seg), float(j) / float(ring) });
        }
    }
    const unsigned south = unsigned(pos.size() / 3);
    pos.insert(pos.end(), { 0.0f, -r, 0.0f });
    nrm.insert(nrm.end(), { 0.0f, -1.0f, 0.0f });
    uv.insert(uv.end(), { 0.5f, 1.0f });
    auto at = [&](int j, int i) { return unsigned(1 + (j - 1) * seg + (i % seg)); };
    for (int i = 0; i < seg; ++i) idx.insert(idx.end(), { 0u, at(1, i), at(1, i + 1) });
    for (int j = 1; j < ring - 1; ++j)
        for (int i = 0; i < seg; ++i) {
            const unsigned a = at(j, i), b = at(j, i + 1), c = at(j + 1, i), e = at(j + 1, i + 1);
            idx.insert(idx.end(), { a, c, b, b, c, e });
        }
    for (int i = 0; i < seg; ++i) idx.insert(idx.end(), { at(ring - 1, i), south, at(ring - 1, i + 1) });
    return clusterfix::meshFromArrays(pos, nrm, uv, idx);
}

/// THE SAME BAKE AUTHORED IN OTHER UNITS: every length x `k` (positions, the chain's bounds and claims, the DAG's
/// spheres and errors - a terminal group stays terminal). Cards are dropped from both copies (they are GI's, not
/// the cut's), so the two meshes differ by the unit alone.
static MeshData inUnits(const MeshData &m, float k)
{
    MeshData d = m;
    for (float &p : d.positions) p *= k;
    for (float &b : d.lodBounds) b *= k;
    for (float &e : d.lodErrors) e *= k;
    for (MeshCluster &c : d.clusters) {
        for (float &x : c.centre) x *= k;
        c.radius *= k;
    }
    for (MeshClusterGroup &g : d.clusterGroups) {
        for (float &x : g.centre) x *= k;
        g.radius *= k;
        if (g.error < FLT_MAX) g.error *= k;
        g.estimate *= k;
    }
    d.cards.clear();
    d.cardCoverage = 0.0f;
    return d;
}

static bool readIds(OgreView *v, std::vector<uint32_t> &x)
{
    Ogre::CompositorWorkspace *ws = v->workspace();
    if (!ws) return false;
    Ogre::TextureGpu *tex = nullptr;
    for (Ogre::CompositorNode *n : ws->getNodeSequence())
        if ((tex = n->getDefinedTexture(Ogre::IdString(detail::kAtomIdTexture))) != nullptr) break;
    if (!tex || tex->getWidth() != kW || tex->getHeight() != kH) return false;
    Ogre::Image2 img;
    img.convertFromTexture(tex, 0u, 0u);
    const Ogre::TextureBox box = img.getData(0u);
    x.assign(size_t(kW) * kH, AtomId::kEmpty);
    for (unsigned r = 0; r < kH; ++r) {
        const auto *row = reinterpret_cast<const uint32_t *>(box.at(0, r, 0));
        for (unsigned c = 0; c < kW; ++c) x[size_t(r) * kW + c] = row[c * 2u];
    }
    return true;
}

static uint32_t slotOf(OgreScene *os, NodeId n)
{
    const detail::GpuScene &gs = os->gpuScene();
    for (unsigned s = 0; s < gs.slotCount(); ++s)
        if (os->nodeOfGpuKey(gs.entry(s).ids[0]) == n) return s;
    return 0xFFFFFFFFu;
}

/// The probe of the next frame, read before any other frame.
static bool probeFrame(Engine *e, Scene *scene, AtomCutProbe &p)
{
    if (!scene->armAtomCutProbe()) return false;
    for (int f = 0; f < 8; ++f) {
        enginetest::frames(e, 1);
        if (scene->atomCutProbe(p)) return true;
    }
    return false;
}

/// The colour class a painted pixel shows (JahAtomView_ps.glsl mode 5): 0 green, 1 yellow, 2 red, -1 none of them.
static int classOf(const unsigned char *px)
{
    const int r = px[0], g = px[1], b = px[2];
    if (r > 200 && g < 80 && b < 80) return 2;
    if (r > 200 && g > 180 && b < 80) return 1;
    if (r < 80 && g > 180 && b < 120) return 0;
    return -1;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("== atom.cut_probe: the probe and the density view read what the cut did\n");
    clusterfix::Fixture fix;
    if (!clusterfix::bake(fix, wrappedSphere(64, 65, 0.5f), "sphere-0.5m") || fix.data.clusterGroups.empty()) {
        std::printf("FAIL: the fixture sphere baked no DAG\n");
        return 1;
    }
    const MeshData metres = inUnits(fix.data, 1.0f), centimetres = inUnits(fix.data, 100.0f);
    std::printf("   the fixture: %zu triangles, %zu clusters, %zu groups\n", metres.triangleCount(),
                metres.clusters.size(), metres.clusterGroups.size());

    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-atom-cut-probe-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    View *view = e->createOffscreenView("cutprobe", kW, kH, Colour(0, 0, 0));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    Scene *scene = e->createScene("cutprobe");
    view->setScene(scene);
    auto *os = static_cast<OgreScene *>(scene);
    auto *ov = static_cast<OgreView *>(view);
    {
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.ssr = 0;
        view->setPostFx(fx);
    }
    scene->setLodBias(1.0f);
    scene->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    // FAR ENOUGH THAT THE CUT SIMPLIFIES (the sphere ~ 60 px across), symmetric so (A) and (B) sit at one distance.
    enginetest::testCameraLookAt(view, Vec3(0.0f, 0.0f, 9.0f), Vec3(0.0f, 0.0f, 0.0f));
    enginetest::addDirectionalLight(scene, Vec3(-0.3f, -0.8f, -0.5f), 2.0f);
    PbrParams pp;
    pp.albedo = Colour(0.6f, 0.6f, 0.6f);
    const MaterialId mat = scene->createPbrMaterial(pp);
    const MeshId meshM = scene->createMesh(metres), meshCm = scene->createMesh(centimetres);
    const NodeId a = scene->createNode(), b = scene->createNode();
    scene->setNodeTransform(a, Vec3(-0.8f, 0.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    scene->setNodeTransform(b, Vec3(0.8f, 0.0f, 0.0f), Quat(), Vec3(0.01f, 0.01f, 0.01f));
    scene->attachMesh(a, meshM, mat);
    scene->attachMesh(b, meshCm, mat);
    enginetest::frames(e, 12);

    AtomCutProbe p;
    CHECK(probeFrame(e, scene, p), "the probe answers within 8 frames");
    CHECK_MSG(p.valid, "the probe is valid (%s)", p.error.empty() ? "no error" : p.error.c_str());
    os->ensureGpuScene(false);
    const uint32_t sA = slotOf(os, a), sB = slotOf(os, b);
    const detail::GpuScene &gs = os->gpuScene();
    std::printf("   request: tolerance %.3f px, projScaleY %.4f, viewport %.0f px, eye (%.2f %.2f %.2f); slots A %u, B %u\n",
                p.tolerance, p.projScaleY, p.viewportHeight, p.eye[0], p.eye[1], p.eye[2], sA, sB);
    CHECK_MSG(p.tolerance == 1.0f && p.viewportHeight == float(kH) && p.projScaleY > 0.5f,
              "the request read back is the view's (tolerance %.3f, viewport %.0f, projScaleY %.4f)", p.tolerance,
              p.viewportHeight, p.projScaleY);

    // (a) every group once, per instance
    std::map<uint32_t, std::vector<const AtomCutProbeGroup *>> bySlot;
    for (const AtomCutProbeGroup &g : p.groups) bySlot[g.slot].push_back(&g);
    const size_t G = metres.clusterGroups.size();
    for (uint32_t s : { sA, sB }) {
        std::vector<int> seen(G, 0);
        for (const AtomCutProbeGroup *g : bySlot[s])
            if (g->group < G) ++seen[g->group];
        const bool once = std::all_of(seen.begin(), seen.end(), [](int n) { return n == 1; });
        CHECK_MSG(once && bySlot[s].size() == G, "(a) slot %u: every one of its mesh's %zu groups recorded once (%zu records)",
                  s, G, bySlot[s].size());
    }
    CHECK_MSG(p.groupsDropped == 0u, "(a) no record dropped past the buffer (%u)", p.groupsDropped);

    // (b) the verdict and allowed are the C++ twin's
    auto twinView = [&](uint32_t s) {
        ClusterCutView v;
        const detail::GpuInstance &ent = gs.entry(s);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) v.worldRow[r][c] = ent.world[r * 4 + c];
        float sc = 0.0f;
        for (int c = 0; c < 3; ++c)
            sc = std::max(sc, std::sqrt(ent.world[c] * ent.world[c] + ent.world[4 + c] * ent.world[4 + c] +
                                        ent.world[8 + c] * ent.world[8 + c]));
        v.scale = sc;
        for (int i = 0; i < 3; ++i) v.eye[i] = p.eye[i];
        v.tolerance = p.tolerance;
        v.projScaleY = p.projScaleY;
        v.viewportHeight = p.viewportHeight;
        v.orthographic = p.orthographic;
        return v;
    };
    for (uint32_t s : { sA, sB }) {
        const MeshData &md = s == sA ? metres : centimetres;
        const ClusterCutView v = twinView(s);
        unsigned verdictMiss = 0, allowedMiss = 0, affordable = 0;
        for (const AtomCutProbeGroup *g : bySlot[s]) {
            const MeshClusterGroup &cg = md.clusterGroups[g->group];
            const float allowed = clusterGroupAllowed(cg, v);
            if (clusterGroupAffordable(cg.error, allowed) != g->affordable) ++verdictMiss;
            if (std::fabs(allowed - g->allowed) > 1e-5f * std::max(allowed, 1e-12f)) ++allowedMiss;
            affordable += g->affordable ? 1u : 0u;
        }
        CHECK_MSG(verdictMiss == 0u && allowedMiss == 0u,
                  "(b) slot %u: the device's verdicts and allowed are the C++ rule's (%u verdicts, %u allowed differ; %u of %zu affordable)",
                  s, verdictMiss, allowedMiss, affordable, bySlot[s].size());
    }

    // (d) the drawn clusters are the rule's, (e) the pixels are the id image's
    std::vector<uint32_t> ids;
    CHECK(readIds(ov, ids), "the id image reads back (no frame drawn since the probe's)");
    std::map<uint32_t, unsigned> idPixels;
    for (uint32_t x : ids)
        if (x != AtomId::kEmpty) ++idPixels[x & AtomId::kSlotMask];
    std::map<uint32_t, const AtomCutProbeObject *> objects;
    for (const AtomCutProbeObject &o : p.objects) objects[o.slot] = &o;
    unsigned long long drawnA = 0ull;
    for (uint32_t s : { sA, sB }) {
        const MeshData &md = s == sA ? metres : centimetres;
        const AtomCutProbeObject *o = objects.count(s) ? objects[s] : nullptr;
        CHECK_MSG(o != nullptr, "slot %u is one of the probe's objects", s);
        if (!o) continue;
        std::vector<unsigned char> aff(G, 0);
        for (const AtomCutProbeGroup *g : bySlot[s]) aff[g->group] = g->affordable ? 1 : 0;
        std::vector<unsigned> drawn;
        const size_t tris = clusterCutFromAffordable(md.clusters, aff, drawn);
        CHECK_MSG(o->drawnTriangles == tris,
                  "(d) slot %u: the cut drew the rule's clusters (%llu drawn, %zu from the probe's verdicts; level 0 %llu)", s,
                  o->drawnTriangles, tris, o->level0Triangles);
        CHECK_MSG(o->pixels == idPixels[s] && o->pixels > 500u,
                  "(e) slot %u: the counted pixels are the id image's (%u counted, %u in the image)", s, o->pixels,
                  idPixels[s]);
        if (s == sA) drawnA = o->drawnTriangles;
        std::printf("   slot %u: %u px, level-0 %llu tris = %.2f per px, drawn %llu = %.2f per px; depth px:", s, o->pixels,
                    o->level0Triangles, double(o->level0Triangles) / std::max(1u, o->pixels), o->drawnTriangles,
                    double(o->drawnTriangles) / std::max(1u, o->pixels));
        for (int d = 0; d < 6; ++d) std::printf(" %u", o->depthPixels[d]);
        std::printf("\n");
    }

    // (g) one frame
    AtomCutProbe again;
    CHECK(scene->atomCutProbe(again) && !again.valid && again.error.find("not armed") != std::string::npos,
          "(g) a read without an arming answers 'not armed'");
    // (g') A DISARMED PROBE RECORDS NOTHING (BUGS-1 item 7: world.atomCutProbe's timeout disarms): armed, taken back
    // before any frame, then two frames drawn - the read answers 'not armed', no later frame took the arming.
    {
        CHECK(scene->armAtomCutProbe(), "(g') the probe arms");
        scene->disarmAtomCutProbe();
        enginetest::frames(e, 2);
        AtomCutProbe late;
        const bool lateAnswered = scene->atomCutProbe(late);   // read before the message is formatted
        CHECK_MSG(lateAnswered && !late.valid && late.error.find("not armed") != std::string::npos,
                  "(g') a disarmed probe records no later frame: the read answers '%s' (valid %d)", late.error.c_str(),
                  int(late.valid));
    }

    // (c) the units: (B) is (A) in centimetres - B moved to where A stood (and A to where B stood), probed again,
    // so each group of B is judged from exactly the eye and place each group of A was
    scene->setNodeTransform(a, Vec3(0.8f, 0.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    scene->setNodeTransform(b, Vec3(-0.8f, 0.0f, 0.0f), Quat(), Vec3(0.01f, 0.01f, 0.01f));
    enginetest::frames(e, 4);
    AtomCutProbe q;
    CHECK(probeFrame(e, scene, q) && q.valid, "the second probe (the two swapped) is valid");
    {
        std::vector<const AtomCutProbeGroup *> ga(G, nullptr), gb(G, nullptr);
        for (const AtomCutProbeGroup *g : bySlot[sA]) if (g->group < G) ga[g->group] = g;
        for (const AtomCutProbeGroup &g : q.groups) if (g.slot == sB && g.group < G) gb[g.group] = &g;
        unsigned flips = 0, nearOne = 0, scaleMiss = 0, affordableA = 0;
        for (size_t i = 0; i < G; ++i) {
            if (!ga[i] || !gb[i]) { ++scaleMiss; continue; }
            if (std::fabs(gb[i]->allowed - 100.0f * ga[i]->allowed) > 1e-4f * gb[i]->allowed ||
                std::fabs(gb[i]->distance - ga[i]->distance) > 1e-4f * ga[i]->distance) {
                if (scaleMiss < 3u)
                    std::printf("   group %zu: allowed A %.6g B %.6g (x %.6f), d A %.6g B %.6g, r A %.6g B %.6g\n", i,
                                ga[i]->allowed, gb[i]->allowed, gb[i]->allowed / ga[i]->allowed, ga[i]->distance,
                                gb[i]->distance, ga[i]->radius, gb[i]->radius);
                ++scaleMiss;
            }
            const float ratio = ga[i]->terminal ? FLT_MAX : ga[i]->error / ga[i]->allowed;
            if (std::fabs(ratio - 1.0f) < 1e-4f) { ++nearOne; continue; }
            if (ga[i]->affordable != gb[i]->affordable) ++flips;
            affordableA += ga[i]->affordable ? 1u : 0u;
        }
        CHECK_MSG(scaleMiss == 0u && flips == 0u && affordableA > 0u,
                  "(c) the centimetre twin at A's place: the same distances and allowed x 100 for every group (%u miss), the "
                  "same verdicts (%u flips, %u at the threshold skipped; %u affordable - the cut simplifies here)",
                  scaleMiss, flips, nearOne, affordableA);
        unsigned long long drawnB = 0ull;
        for (const AtomCutProbeObject &o : q.objects) if (o.slot == sB) drawnB = o.drawnTriangles;
        CHECK_MSG(drawnB == drawnA && drawnA > 0ull,
                  "(c) ...and it draws the metre sphere's triangles (%llu, %llu)", drawnB, drawnA);
    }
    scene->setNodeTransform(a, Vec3(-0.8f, 0.0f, 0.0f), Quat(), Vec3(1, 1, 1));
    scene->setNodeTransform(b, Vec3(0.8f, 0.0f, 0.0f), Quat(), Vec3(0.01f, 0.01f, 0.01f));

    // (f) the density view
    scene->setAtomView(AtomView::Density);
    enginetest::frames(e, 4);
    Image img;
    CHECK(view->readPixels(img) && img.width == kW && img.height == kH, "the density view's picture reads back");
    AtomDrawStatus st = scene->atomDrawStatus();
    std::vector<uint32_t> ids2;
    CHECK(readIds(ov, ids2), "the id image reads back under the density view");
    unsigned painted = 0, wrong = 0, total = 0;
    unsigned classPx[3] = { 0, 0, 0 };
    for (uint32_t s : { sA, sB }) {
        const AtomCutProbeObject *o = objects.count(s) ? objects[s] : nullptr;
        if (!o) continue;
        unsigned px = 0;
        for (uint32_t x : ids2) px += (x != AtomId::kEmpty && (x & AtomId::kSlotMask) == s) ? 1u : 0u;
        const float tpp = float(o->level0Triangles) / float(std::max(1u, px));
        const int want = tpp > 4.0f ? 2 : tpp >= 1.0f ? 1 : 0;
        classPx[want] += px;
        for (size_t i = 0; i < ids2.size(); ++i) {
            if (ids2[i] == AtomId::kEmpty || (ids2[i] & AtomId::kSlotMask) != s) continue;
            ++total;
            const int got = classOf(&img.rgba[i * 4u]);
            if (got >= 0) ++painted;
            if (got != want) ++wrong;
        }
        std::printf("   density: slot %u %.2f level-0 triangles a pixel -> class %d\n", s, tpp, want);
    }
    CHECK_MSG(total > 1000u && wrong == 0u,
              "(f) every pixel of the spheres is painted its object's class (%u of %u wrong, %u painted)", wrong, total,
              painted);
    CHECK_MSG(st.densityValid && st.densityPixels == total && st.densityObjects == 2u &&
                  st.densityClassPixels[0] == classPx[0] && st.densityClassPixels[1] == classPx[1] &&
                  st.densityClassPixels[2] == classPx[2],
              "(f) atomStatus' density says the same (valid %d, %u px in %u objects; classes %u/%u/%u, the image's %u/%u/%u)",
              int(st.densityValid), st.densityPixels, st.densityObjects, st.densityClassPixels[0],
              st.densityClassPixels[1], st.densityClassPixels[2], classPx[0], classPx[1], classPx[2]);
    // (f') THE DENSITY FAIL-SAFE (BUGS-1 item 6): the count's compute jobs missing (the test door "atom.densityJobsMissing",
    // as on a tree whose media did not stage them) - the frames paint NOTHING over the spheres and report no figures,
    // never the table the frames before them wrote.
    {
        e->setArm("atom.densityJobsMissing", 1.0);
        enginetest::frames(e, 4);
        Image miss;
        const bool read = view->readPixels(miss) && miss.width == kW && miss.height == kH;
        const AtomDrawStatus ms = scene->atomDrawStatus();
        unsigned paintedMiss = 0, spherePx = 0;
        for (size_t i = 0; read && i < ids2.size(); ++i) {
            if (ids2[i] == AtomId::kEmpty) continue;
            const uint32_t sl = ids2[i] & AtomId::kSlotMask;
            if (sl != sA && sl != sB) continue;
            ++spherePx;
            if (classOf(&miss.rgba[i * 4u]) >= 0) ++paintedMiss;
        }
        e->setArm("atom.densityJobsMissing", 0.0);
        CHECK_MSG(read && spherePx > 1000u && paintedMiss == 0u && !ms.densityValid,
                  "(f') THE DENSITY FAIL-SAFE: with the count's jobs missing %u of %u sphere pixels are painted a class "
                  "(0 wanted) and the figures are %s", paintedMiss, spherePx, ms.densityValid ? "STILL VALID" : "gone");
    }
    scene->setAtomView(AtomView::Off);
    enginetest::frames(e, 2);
    AtomDrawStatus off = scene->atomDrawStatus();
    CHECK(!off.densityValid, "(f) the density figures are gone with the view");

    // (h) THE TIER'S THRESHOLD (ATOM-3 stage 1): the World Mode's pixel-error row reaches the cut
    // (Scene::setPixelTolerance -> the id pass's request = row x lodBias) and the drawn triangles fall
    // STRICTLY with it — Epic 1, High 2, Medium 3 px at the same pose. At the base every tier drew the
    // 1 px count (the threshold was the constant kLodBudgetPixels).
    {
        unsigned long long drawnAt[3] = { 0ull, 0ull, 0ull };
        bool readAllH = true, tolOk = true;
        for (int t = 0; t < 3; ++t) {
            const float tol = float(t + 1);
            scene->setPixelTolerance(tol);
            enginetest::frames(e, 2);
            AtomCutProbe h;
            if (!probeFrame(e, scene, h) || !h.valid) { readAllH = false; continue; }
            if (h.tolerance != tol) tolOk = false;
            for (const AtomCutProbeObject &o : h.objects)
                if (o.slot == sA || o.slot == sB) drawnAt[t] += o.drawnTriangles;
        }
        scene->setPixelTolerance(kLodBudgetPixels);
        std::printf("   (h) drawn triangles (A + B) at 1 / 2 / 3 px: %llu / %llu / %llu (level 0 %zu)\n", drawnAt[0],
                    drawnAt[1], drawnAt[2], 2u * metres.triangleCount());
        CHECK_MSG(readAllH && tolOk, "(h) the three probes are valid and their request carries the row (%d, %d)",
                  int(readAllH), int(tolOk));
        CHECK_MSG(drawnAt[0] > drawnAt[1] && drawnAt[1] > drawnAt[2] && drawnAt[2] > 0ull,
                  "(h) THE DRAWN TRIANGLES FALL STRICTLY WITH THE THRESHOLD: %llu > %llu > %llu", drawnAt[0],
                  drawnAt[1], drawnAt[2]);
    }

    e->destroyView(view);
    e->destroyScene(scene);
    engine.reset();
    std::printf("%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
