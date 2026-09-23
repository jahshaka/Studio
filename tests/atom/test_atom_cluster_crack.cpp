// atom.cluster_crack — THE CLUSTER CUT, RENDERED (ATOM stage 2, lane
// ATOM-CLUSTER-1; SPECS/atom/B2_CLUSTER_DAG_DESIGN.md §3). Offscreen, plain grade.
//
// The rule is proven to select ONE cut combinatorially by atom.cluster_cut; this
// suite proves the cut is CRACK-FREE on the rasteriser, where a crack actually
// lives (a T-junction or a moved boundary vertex between two clusters of
// different depth), and measures what the per-cluster cut buys. The draws go
// through the TEST HARNESS (cluster_draw.h, route B2: a rewritten BT_DEFAULT
// index buffer + setPrimitiveRange) — the product draws no cut in stage 2.
//
//   0. THE UPLOAD: the engine's cluster stream (MeshRec::clusterStream), read
//      back from the GPU, is the mirror's expansion of the bake, index for index.
//   A. THE CRACK SWEEP, per fixture (the 20k sphere, the shipped high-poly models
//      and three primitives): the object is drawn TWICE — lit, and wound
//      backwards in an unlit INSIDE colour, so the inside copy shows exactly
//      where a view ray has passed THROUGH the surface. A CRACK is a pixel inside
//      the level-0 silhouette — eroded by the cut's own allowed deviation in
//      pixels plus one, so a silhouette that legitimately moves inward is not
//      counted — that shows the background or the inside colour. ZERO, not few,
//      at every threshold of a 16-step sweep from level 0 to the root, and at
//      four view-dependent tolerances.
//   B. THE DOLLY THROUGH THE CUT: atom.dolly_gate's walk (40 m -> 2 m, one frame
//      per 0.25 m) on the 20k sphere with the per-view cluster cut at the view's
//      pixel budget, against the level-0 control at the same poses — the excess
//      of every step over the control <= 3x the walk's ordinary step. The chain
//      is walked over the same poses for the comparison the design asks to quote.
//   C. THE NUMBERS: a 40 m bar seen end-on — the DAG's triangles against the
//      chain's at the same allowed error (the chain has to pick ONE level for the
//      whole bar from its nearest point). The DAG must draw fewer.
//
// THE INSTRUMENT SATURATES AT 1.0 (PFG_RGBA8_UNORM), so the lit fixture sits
// mid-range: mid-grey, one moderate directional light, an ambient floor.
#include "cluster_draw.h"
#include "cluster_fixtures.h"
#include "../support/enginetesthelpers.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <map>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (cond) { std::printf("ok: "); std::printf(__VA_ARGS__); }                       \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); ++failures; }              \
        std::printf("\n");                                                                 \
    } while (0)

static const unsigned kSize = 1024;
static const Colour kInside(0.85f, 0.0f, 0.85f);

enum Px : unsigned char { Background = 0, Surface = 1, Inside = 2 };

static std::vector<unsigned char> classify(const Image &img)
{
    std::vector<unsigned char> out(size_t(img.width) * img.height, Background);
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const size_t i = (size_t(y) * img.width + x) * 4u;
            const int r = img.rgba[i], g = img.rgba[i + 1], b = img.rgba[i + 2];
            unsigned char k = Background;
            if (r > 120 && b > 120 && 2 * g < std::min(r, b) && std::abs(r - b) < 40) k = Inside;
            else if (std::max(r, std::max(g, b)) > 3) k = Surface;
            out[size_t(y) * img.width + x] = k;
        }
    return out;
}

/// Chebyshev distance (in pixels) from every mask pixel to the nearest non-mask
/// pixel — the erosion radius a pixel survives.
static std::vector<int> insideDistance(const std::vector<unsigned char> &mask, unsigned w, unsigned h)
{
    const int big = 1 << 20;
    std::vector<int> d(mask.size());
    for (size_t i = 0; i < mask.size(); ++i) d[i] = mask[i] ? big : 0;
    auto at = [&](int x, int y) -> int & { return d[size_t(y) * w + size_t(x)]; };
    for (int y = 0; y < int(h); ++y)
        for (int x = 0; x < int(w); ++x) {
            if (!at(x, y)) continue;
            int v = at(x, y);
            if (x == 0 || y == 0) v = 1;
            else v = std::min({ v, at(x - 1, y) + 1, at(x, y - 1) + 1, at(x - 1, y - 1) + 1,
                                x + 1 < int(w) ? at(x + 1, y - 1) + 1 : 1 });
            at(x, y) = v;
        }
    for (int y = int(h) - 1; y >= 0; --y)
        for (int x = int(w) - 1; x >= 0; --x) {
            if (!at(x, y)) continue;
            int v = at(x, y);
            if (x == int(w) - 1 || y == int(h) - 1) v = 1;
            else v = std::min({ v, at(x + 1, y) + 1, at(x, y + 1) + 1, at(x + 1, y + 1) + 1,
                                x > 0 ? at(x - 1, y + 1) + 1 : 1 });
            at(x, y) = v;
        }
    return d;
}

struct Rig
{
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *scene = nullptr;
    MaterialId lit = 0, inside = 0;
    float p11 = 0.0f, height = float(kSize);
    Vec3 eye;
};

static bool frame(Rig &r, Image &img)
{
    r.e->renderOneFrame();
    return r.view->readPixels(img);
}

/// The world size of one pixel at `dist` metres, from this view's projection.
static float footprint(const Rig &r, float dist) { return sampleFootprintPerspective(dist, r.p11, r.height); }

struct Sphere { float c[3]; float r; };
static Sphere boundsOf(const MeshData &d)
{
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t v = 0; v + 2 < d.positions.size(); v += 3)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], d.positions[v + size_t(k)]);
            hi[k] = std::max(hi[k], d.positions[v + size_t(k)]);
        }
    Sphere s;
    for (int k = 0; k < 3; ++k) s.c[k] = 0.5f * (lo[k] + hi[k]);
    const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    s.r = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    return s;
}

// ---- A. the crack sweep -------------------------------------------------------
struct SweepResult { size_t worstCracks = 0; std::string worstAt; size_t evaluatedSteps = 0; size_t worstInside = 0; };

static void crackSweep(Rig &r, const clusterfix::Fixture &f, const float dirIn[3], SweepResult &total,
                       const float *eyeIn = nullptr, const float *targetIn = nullptr)
{
    std::string err;
    clusterdraw::ClusterDraw body, back;
    if (!body.create(r.e, r.scene, f.data, r.lit, false, err) ||
        !back.create(r.e, r.scene, f.data, r.inside, true, err)) {
        CHECK(false, "%s: the harness draws it (%s)", f.name.c_str(), err.c_str());
        return;
    }
    const Sphere s = boundsOf(f.data);
    float dir[3] = { dirIn[0], dirIn[1], dirIn[2] };
    const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    for (float &v : dir) v /= dl;
    const float fovHalf = std::atan(1.0f / r.p11);
    float dist = s.r / std::sin(fovHalf) * 1.05f;
    r.eye = Vec3(s.c[0] + dir[0] * dist, s.c[1] + dir[1] * dist, s.c[2] + dir[2] * dist);
    Vec3 target(s.c[0], s.c[1], s.c[2]);
    if (eyeIn && targetIn) {
        // An explicit pose (the bars: a 40 m object framed whole is a hair).
        r.eye = Vec3(eyeIn[0], eyeIn[1], eyeIn[2]);
        target = Vec3(targetIn[0], targetIn[1], targetIn[2]);
        const float dx = s.c[0] - eyeIn[0], dy = s.c[1] - eyeIn[1], dz = s.c[2] - eyeIn[2];
        dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    enginetest::testCameraLookAt(r.view, r.eye, target);
    for (int i = 0; i < 3; ++i) r.e->renderOneFrame();   // settle the pose, in frames

    Image img;
    const bool read = frame(r, img);
    CHECK(read && img.width == kSize, "%s: the level-0 picture reads back", f.name.c_str());
    if (const char *dump = std::getenv("CLUSTER_CRACK_DUMP")) {
        const std::string path = std::string(dump) + "/" + f.name + ".ppm";
        if (FILE *fp = std::fopen(path.c_str(), "wb")) {
            std::fprintf(fp, "P6 %u %u 255\n", img.width, img.height);
            for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) std::fwrite(&img.rgba[i], 1, 3, fp);
            std::fclose(fp);
        }
    }
    const std::vector<unsigned char> level0 = classify(img);
    std::vector<unsigned char> mask(level0.size());
    size_t inside0 = 0, surface0 = 0;
    for (size_t i = 0; i < level0.size(); ++i) {
        mask[i] = level0[i] == Surface ? 1 : 0;
        inside0 += level0[i] == Inside;
        surface0 += level0[i] == Surface;
    }
    const std::vector<int> depthIn = insideDistance(mask, img.width, img.height);
    std::printf("\n-- %s: level 0 covers %zu px (%zu px of inside colour at level 0)\n",
                f.name.c_str(), surface0, inside0);
    // WATERTIGHT or not decides what the INSIDE colour may mean: through a closed
    // level 0 no view ray can meet a back face, so inside colour within the
    // silhouette is a hole; through an OPEN one (the teapot's lid gap, a plane's
    // rim) a simplified border legitimately exposes the far side, and only the
    // design's own signal — the BACKGROUND inside the silhouette — is a crack.
    const bool watertight = [&] {
        std::map<std::array<float, 3>, unsigned> first;
        std::vector<unsigned> canon(f.data.positions.size() / 3);
        for (size_t v = 0; v < canon.size(); ++v)
            canon[v] = first.emplace(std::array<float, 3>{ f.data.positions[v * 3], f.data.positions[v * 3 + 1],
                                                           f.data.positions[v * 3 + 2] }, unsigned(v)).first->second;
        std::map<std::pair<unsigned, unsigned>, int> uses;
        const auto &idx = f.data.indices;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            const unsigned a = canon[idx[t]], b = canon[idx[t + 1]], c = canon[idx[t + 2]];
            if (a == b || b == c || a == c) continue;
            for (auto e : { std::make_pair(a, b), std::make_pair(b, c), std::make_pair(c, a) })
                ++uses[std::minmax(e.first, e.second)];
        }
        for (const auto &kv : uses) if (kv.second != 2) return false;
        return true;
    }();
    std::printf("   level 0 is %s\n", watertight ? "WATERTIGHT (inside colour counts)" : "OPEN (background only)");
    std::printf("   %-12s %-10s %-9s %-10s %-9s %-9s %-9s\n", "allowed", "triangles", "erode px",
                "evaluated", "inside", "bg-crack", "in-crack");

    // The nearest the surface comes to the eye, for the erosion radius.
    const float dNear = std::max(dist - s.r, 1e-3f);
    const auto &groups = f.data.clusterGroups;
    const auto &clusters = f.data.clusters;
    float minErr = FLT_MAX, maxErr = 0.0f;
    for (const MeshClusterGroup &g : groups)
        if (g.error < FLT_MAX) { minErr = std::min(minErr, g.error); maxErr = std::max(maxErr, g.error); }

    struct Step { std::string label; std::vector<unsigned> cut; size_t tris; int erode; };
    std::vector<Step> steps;
    std::vector<unsigned> cut;
    for (int i = 0; i < 16; ++i) {
        const float allowed = i == 0 ? 0.0f
                                     : minErr * 0.5f * std::pow((maxErr * 2.0f) / (minErr * 0.5f), float(i - 1) / 14.0f);
        const size_t t = clusterCutAtAllowed(groups, clusters, allowed, cut);
        const float px = allowed > 0.0f ? allowed / footprint(r, dNear) : 0.0f;
        char label[64];
        std::snprintf(label, sizeof(label), "%.5g", double(allowed));
        steps.push_back({ label, cut, t, int(std::ceil(px)) + 1 });
    }
    // ...and the VIEW-DEPENDENT cut from this pose, at four pixel tolerances.
    for (float tol : { 0.5f, 1.0f, 4.0f, 16.0f }) {
        ClusterCutView v;
        v.eye[0] = r.eye.x; v.eye[1] = r.eye.y; v.eye[2] = r.eye.z;
        v.tolerance = tol;
        v.projScaleY = r.p11;
        v.viewportHeight = r.height;
        const size_t t = clusterCut(groups, clusters, v, cut);
        char label[64];
        std::snprintf(label, sizeof(label), "view %.1fpx", double(tol));
        steps.push_back({ label, cut, t, int(std::ceil(tol)) + 1 });
    }

    size_t worst = 0;
    size_t nonVacuous = 0;
    for (const Step &st : steps) {
        body.setCut(st.cut);
        back.setCut(st.cut);
        frame(r, img);
        const std::vector<unsigned char> px = classify(img);
        size_t evaluated = 0, bgCracks = 0, inCracks = 0, insideTotal = 0;
        for (size_t i = 0; i < px.size(); ++i) {
            insideTotal += px[i] == Inside;
            if (!mask[i] || depthIn[i] <= st.erode) continue;
            ++evaluated;
            if (px[i] == Background) ++bgCracks;
            if (px[i] == Inside) ++inCracks;
        }
        // THE GATE IS THE DESIGN'S: background inside the silhouette. The inside
        // colour is printed, not gated — a closed level 0 can still show back
        // faces where its parts INTERPENETRATE (the Physics model shows 319 px of
        // them at level 0) or where simplification FOLDS a thin feature, and
        // neither is a crack; a crack between clusters is an OPEN EDGE of the cut,
        // which atom.cluster_cut counts exactly (zero, on every mesh, at every
        // threshold).
        const size_t cracks = bgCracks;
        total.worstInside = std::max(total.worstInside, watertight ? inCracks : size_t(0));
        if (evaluated > 1000) ++nonVacuous;
        worst = std::max(worst, cracks);
        if (cracks > total.worstCracks) {
            total.worstCracks = cracks;
            total.worstAt = f.name + " @ " + st.label;
        }
        std::printf("   %-12s %-10zu %-9d %-10zu %-9zu %-9zu %-9zu\n", st.label.c_str(), st.tris, st.erode,
                    evaluated, insideTotal, bgCracks, inCracks);
    }
    total.evaluatedSteps += nonVacuous;
    CHECK(worst == 0, "%s: NO CRACK at any of the %zu cuts (worst %zu px)", f.name.c_str(), steps.size(),
          worst);
    CHECK(nonVacuous >= 8, "%s: %zu of the cuts were judged over more than 1,000 interior pixels",
          f.name.c_str(), nonVacuous);
    body.destroy();
    back.destroy();
}

// ---- B. the dolly ---------------------------------------------------------------
static double maskedMeanAbsDiff(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return 1.0e9;
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        const int la = std::max({ a.rgba[i], a.rgba[i + 1], a.rgba[i + 2] });
        const int lb = std::max({ b.rgba[i], b.rgba[i + 1], b.rgba[i + 2] });
        if (la <= 3 && lb <= 3) continue;
        ++n;
        sum += std::abs(int(a.rgba[i]) - int(b.rgba[i])) + std::abs(int(a.rgba[i + 1]) - int(b.rgba[i + 1])) +
               std::abs(int(a.rgba[i + 2]) - int(b.rgba[i + 2]));
    }
    return n ? sum / (3.0 * 255.0 * double(n)) : 0.0;
}

struct Walk { std::vector<float> dist; std::vector<double> delta; std::vector<bool> switched; std::vector<size_t> tris; };

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atom-cluster-crack-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Rig r;
    r.e = engine.get();
    r.view = r.e->createOffscreenView("crack", kSize, kSize, Colour(0, 0, 0));
    r.scene = r.e->createScene("crack");
    if (!r.view || !r.scene) { std::printf("FAIL: view/scene\n"); return 1; }
    r.view->setScene(r.scene);
    r.scene->setAmbient(Colour(0.10f, 0.10f, 0.12f), Colour(0.06f, 0.06f, 0.08f));
    enginetest::addDirectionalLight(r.scene, Vec3(-0.5f, -0.6f, -0.62f), 2.2f);
    PbrParams p;
    p.albedo = Colour(0.55f, 0.55f, 0.55f);
    p.roughness = 0.55f;
    p.metalness = 0.0f;
    r.lit = r.scene->createPbrMaterial(p);
    // THE INSIDE COPY IS A PBR MATERIAL, black and EMISSIVE, not an unlit one:
    // the unlit family draws both faces (it is the overlay's), and a copy that
    // is not back-face culled covers the whole object instead of only its holes.
    PbrParams q;
    q.albedo = Colour(0.0f, 0.0f, 0.0f);
    q.emissive = kInside;
    q.roughness = 1.0f;
    q.metalness = 0.0f;
    q.receiveShadows = false;
    r.inside = r.scene->createPbrMaterial(q);
    enginetest::testCameraLookAt(r.view, Vec3(0, 0, 5), Vec3(0, 0, 0));
    for (int i = 0; i < 3; ++i) r.e->renderOneFrame();
    GpuCullRequest vr;
    if (!r.e->fillCullView(r.view, vr)) { std::printf("FAIL: fillCullView\n"); return 1; }
    r.p11 = vr.projScaleY;
    r.height = vr.viewportHeight;
    std::printf("the view: %.0f px tall, proj11 %.4f\n", double(r.height), double(r.p11));

    // The fixtures.
    std::vector<clusterfix::Named> named =
        clusterfix::shippedMeshes(JAHSHAKA_TEST_SOURCE_DIR, CLUSTER_FIXTURE_DIR);
    std::vector<clusterfix::Fixture> fx;
    for (const auto &n : named) {
        if (n.name != "uv-sphere-20k" && n.name != "matcaps-dragon" && n.name != "physics-model" &&
            n.name != "torus.obj" && n.name != "teapot.obj" && n.name != "capsule.obj")
            continue;
        clusterfix::Fixture f;
        if (clusterfix::bake(f, n.mesh, n.name) && !f.data.clusters.empty()) fx.push_back(std::move(f));
    }
    CHECK(fx.size() == 6u, "six fixtures carry a DAG (%zu)", fx.size());

    // ---- 0. the upload ----------------------------------------------------------
    {
        const clusterfix::Fixture *dragon = nullptr;
        for (const auto &f : fx) if (f.name == "matcaps-dragon") dragon = &f;
        if (dragon) {
            const MeshId m = r.scene->createMesh(dragon->data);
            std::vector<unsigned> stream;
            const bool read = m && clusterdraw::readClusterStream(r.scene, m, stream, err);
            CHECK(read && stream == dragon->data.clusterIndices,
                  "THE UPLOAD: the dragon's cluster stream on the GPU is the mirror's expansion "
                  "of the bake, index for index (%zu indices, %zu clusters)",
                  stream.size(), dragon->data.clusters.size());
            if (m) r.scene->destroyMesh(m);
        }
        MeshData small = enginetest::unitCubeMesh();
        const MeshId m = r.scene->createMesh(small);
        std::vector<unsigned> none;
        CHECK(m && !clusterdraw::readClusterStream(r.scene, m, none, err),
              "a mesh with no DAG gets no cluster stream");
        if (m) r.scene->destroyMesh(m);
    }

    // ---- A. the crack sweep ---------------------------------------------------
    SweepResult sweep;
    const float dirs[][3] = { { 0.45f, 0.35f, 1.0f }, { -0.8f, 0.5f, -0.3f } };
    for (const auto &f : fx)
        for (int d = 0; d < 2; ++d) crackSweep(r, f, dirs[d], sweep);
    std::printf("\n   THE CRACK SWEEP'S WORST STEP: %zu px of background (%s); the most back-face "
                "pixels a closed fixture showed inside its silhouette: %zu\n", sweep.worstCracks,
                sweep.worstAt.empty() ? "none" : sweep.worstAt.c_str(), sweep.worstInside);

    // ---- B. the dolly through the cut -------------------------------------------
    const clusterfix::Fixture *sphere = nullptr;
    for (const auto &f : fx) if (f.name == "uv-sphere-20k") sphere = &f;
    if (sphere) {
        const float kFar = 40.0f, kNear = 2.0f, kStep = 0.25f;
        clusterdraw::ClusterDraw body;
        if (!body.create(r.e, r.scene, sphere->data, r.lit, false, err)) {
            CHECK(false, "the dolly's harness: %s", err.c_str());
        } else {
            std::vector<unsigned> leaves;
            for (size_t c = 0; c < sphere->data.clusters.size(); ++c)
                if (sphere->data.clusters[c].refined < 0) leaves.push_back(unsigned(c));
            auto walk = [&](bool cutArm) {
                Walk w;
                enginetest::testCameraLookAt(r.view, Vec3(0, 0, kFar), Vec3(0, 0, 0));
                body.setCut(leaves);
                for (int i = 0; i < 30; ++i) r.e->renderOneFrame();
                Image prev, img;
                std::vector<unsigned> prevCut, cut = leaves;
                for (float dist = kFar; dist >= kNear - 1.0e-4f; dist -= kStep) {
                    enginetest::testCameraLookAt(r.view, Vec3(0, 0, dist), Vec3(0, 0, 0));
                    size_t tris = sphere->triangles;
                    if (cutArm) {
                        ClusterCutView v;
                        v.eye[2] = dist;
                        v.tolerance = kLodBudgetPixels;
                        v.projScaleY = r.p11;
                        v.viewportHeight = r.height;
                        tris = clusterCut(sphere->data.clusterGroups, sphere->data.clusters, v, cut);
                        body.setCut(cut);
                    }
                    frame(r, img);
                    if (prev.width) {
                        w.dist.push_back(dist);
                        w.delta.push_back(maskedMeanAbsDiff(prev, img));
                        w.switched.push_back(cutArm && cut != prevCut);
                        w.tris.push_back(tris);
                    }
                    prev = img;
                    prevCut = cut;
                }
                return w;
            };
            const Walk ref = walk(false);
            const Walk cw = walk(true);
            body.destroy();

            // THE CHAIN over the same poses: the product mesh, its chain, the
            // shipped strategy (hysteresis off: this is a capture view).
            Walk chain;
            {
                const MeshId m = r.scene->createMesh(sphere->data);
                const NodeId n = r.scene->createNode();
                r.scene->attachMesh(n, m, r.lit);
                enginetest::testCameraLookAt(r.view, Vec3(0, 0, kFar), Vec3(0, 0, 0));
                for (int i = 0; i < 30; ++i) r.e->renderOneFrame();
                Image prev, img;
                unsigned prevLevel = 0xFFFFFFFFu;
                for (float dist = kFar; dist >= kNear - 1.0e-4f; dist -= kStep) {
                    enginetest::testCameraLookAt(r.view, Vec3(0, 0, dist), Vec3(0, 0, 0));
                    frame(r, img);
                    std::vector<ObjectLodDesc> lods;
                    r.scene->objectLods(lods);
                    unsigned level = 0, tris = 0;
                    for (const auto &d : lods) if (d.node == n) { level = d.level; tris = unsigned(d.triangles); }
                    if (prev.width) {
                        chain.dist.push_back(dist);
                        chain.delta.push_back(maskedMeanAbsDiff(prev, img));
                        chain.switched.push_back(level != prevLevel);
                        chain.tris.push_back(tris);
                    }
                    prev = img;
                    prevLevel = level;
                }
                r.scene->removeNode(n);
                r.scene->destroyMesh(m);
            }

            auto judge = [&](const Walk &w, const char *what, bool gate) {
                double ordSum = 0.0; unsigned ordN = 0, switches = 0;
                for (size_t i = 0; i < w.delta.size(); ++i) {
                    if (w.switched[i]) ++switches; else { ordSum += w.delta[i]; ++ordN; }
                }
                const double ordinary = ordN ? ordSum / ordN : 0.0;
                unsigned over = 0, overExcess = 0;
                double worstSwitchExcess = 0.0, worstExcess = 0.0; float worstAt = 0.0f;
                for (size_t i = 0; i < w.delta.size() && i < ref.delta.size(); ++i) {
                    const double bar = 3.0 * std::max(ordinary, ref.delta[i]);
                    const double excess = w.delta[i] - ref.delta[i];
                    if (w.delta[i] > bar) ++over;
                    if (excess > 3.0 * ordinary) ++overExcess;
                    if (excess > worstExcess) { worstExcess = excess; worstAt = w.dist[i]; }
                    if (w.switched[i]) worstSwitchExcess = std::max(worstSwitchExcess, excess);
                }
                std::printf("   %-26s %3u switches, ordinary step %.2f codes, worst excess %.2f codes at "
                            "%.2f m (worst at a switch %.2f), bar %.2f codes; %zu..%zu triangles\n",
                            what, switches, ordinary * 255.0, worstExcess * 255.0, worstAt,
                            worstSwitchExcess * 255.0, 3.0 * ordinary * 255.0,
                            w.tris.empty() ? 0 : *std::min_element(w.tris.begin(), w.tris.end()),
                            w.tris.empty() ? 0 : *std::max_element(w.tris.begin(), w.tris.end()));
                if (gate) {
                    CHECK(switches >= 3u, "%s: the walk crosses %u cut changes", what, switches);
                    CHECK(over == 0u, "%s: no step exceeds 3x the ordinary movement at its own pose (%u over)",
                          what, over);
                    CHECK(overExcess == 0u, "%s: THE POP — no step adds more than 3x the ordinary step "
                          "over the level-0 control (%u over; worst %.2f codes of %.2f)", what, overExcess,
                          worstExcess * 255.0, 3.0 * ordinary * 255.0);
                }
            };
            std::printf("\n== B. the dolly, 40 m -> 2 m, one frame per 0.25 m (the 20k sphere) ==\n");
            CHECK(cw.delta.size() >= 150u && cw.delta.size() == ref.delta.size(),
                  "%zu compared pairs, the same poses in every arm", cw.delta.size());
            judge(cw, "the CLUSTER CUT", true);
            judge(chain, "the chain (for comparison)", false);
        }
    }

    // ---- C. the numbers: a 40 m bar, end-on ----------------------------------
    //
    // THREE BARS, and two of them are findings. The BUMPY bar has real detail
    // along its length and around it, so every coarser level of the chain costs
    // real error everywhere and the chain must hold a fine level for the whole
    // bar while the near end needs it; the DAG coarsens the far end. That is the
    // case the design names, and it is asserted. The ROUND and the BOX bar are
    // printed, not asserted: along their length they simplify LOSSLESSLY (a
    // straight cylinder's rings and a box's coplanar faces), so the chain's
    // levels carry the arithmetic floor as their bound and it drops to a coarse
    // level at almost any distance — while the DAG keeps every group border
    // locked through the build and clamps a group whose sphere reaches the eye
    // to distance zero, so it can draw MORE there.
    auto barNumbers = [&](clusterfix::Fixture &bar, bool gate) {
        const Sphere s = boundsOf(bar.data);
        std::printf("\n== C. %s seen end-on (%zu triangles, %zu clusters, %zu chain levels, "
                    "level-1 bound %.3g) ==\n", bar.name.c_str(), bar.triangles, bar.data.clusters.size(),
                    bar.data.lodIndices.size(), bar.data.lodBounds.empty() ? 0.0 : double(bar.data.lodBounds[0]));
        std::printf("   %-24s %-10s %-10s %-8s\n", "eye", "DAG tris", "chain tris", "chain/DAG");
        const float eyes[][3] = { { 0.0f, 0.0f, 0.5f }, { 0.0f, 0.0f, 2.0f }, { 0.0f, 0.3f, 5.0f },
                                  { 0.4f, 0.4f, 10.0f }, { 0.0f, 1.0f, 20.0f } };
        std::vector<unsigned> cut;
        bool fewer = true;
        double worstRatio = 1e30, bestRatio = 0.0;
        for (const auto &e : eyes) {
            ClusterCutView v;
            v.eye[0] = e[0]; v.eye[1] = e[1]; v.eye[2] = e[2];
            v.tolerance = kLodBudgetPixels;
            v.projScaleY = r.p11;
            v.viewportHeight = r.height;
            const size_t dagTris = clusterCut(bar.data.clusterGroups, bar.data.clusters, v, cut);
            // THE CHAIN'S ANSWER in the same currency: ONE level for the whole
            // object, from Ogre's own distance to its bounding sphere.
            const float dx = s.c[0] - e[0], dy = s.c[1] - e[1], dz = s.c[2] - e[2];
            const float d = std::max(0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) - s.r);
            const float allowed = allowedWorldError(kLodBudgetPixels, footprint(r, d), 1.0f);
            const size_t level = lodLevelForWorldError(bar.data.lodBounds, allowed, bar.data.lodIndices.size());
            const size_t chainTris = level == 0 ? bar.triangles : bar.data.lodIndices[level - 1].size() / 3;
            const double ratio = dagTris ? double(chainTris) / double(dagTris) : 0.0;
            worstRatio = std::min(worstRatio, ratio);
            bestRatio = std::max(bestRatio, ratio);
            if (dagTris >= chainTris) fewer = false;
            char eyeStr[64];
            std::snprintf(eyeStr, sizeof(eyeStr), "(%.1f, %.1f, %.1f)", e[0], e[1], e[2]);
            std::printf("   %-24s %-10zu %-10zu %-8.2f\n", eyeStr, dagTris, chainTris, ratio);
        }
        if (gate)
            CHECK(fewer, "%s: THE DAG DRAWS FEWER TRIANGLES than the chain at every end-on pose "
                  "(chain/DAG %.2fx .. %.2fx)", bar.name.c_str(), worstRatio, bestRatio);
        else
            std::printf("   (printed, not asserted: chain/DAG %.2fx .. %.2fx)\n", worstRatio, bestRatio);
    };
    {
        clusterfix::Fixture bumpy, round, box;
        clusterfix::bake(bumpy, clusterfix::roundBar(40.0f, 0.25f, 400, 32, 0.08f), "bumpy-bar-40m");
        clusterfix::bake(round, clusterfix::roundBar(40.0f, 0.25f, 400, 32), "round-bar-40m");
        clusterfix::bake(box, clusterfix::bar(40.0f, 0.5f, 400, 4), "box-bar-40m");
        barNumbers(bumpy, true);
        barNumbers(round, false);
        barNumbers(box, false);
        // ...and the bumpy bar's cut is crack-free on the rasteriser, end-on.
        const float along[3] = { 0.05f, 0.08f, 1.0f };
        const float eye[3] = { 0.55f, 0.4f, 1.2f }, target[3] = { 0.0f, 0.0f, -6.0f };
        crackSweep(r, bumpy, along, sweep, eye, target);
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
