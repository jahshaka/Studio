// gi.hit_shade — A RAY HIT IS A PIXEL OF A SECOND VISIBILITY BUFFER
// (PHOTON-HIT-SHADE-1; SPECS/atom/D2_HIT_SHADING_DESIGN.md with the lead's
// decisions F1-F8).
//
// WHAT WAS WRONG: a ray that hit geometry the caches cannot shade — a MOVER (the
// voxels exclude it, it has no cards), a RIGGED item (the voxels hold its bind
// pose), a surface outside every cascade, the gather's far copies — was handed
// back to the probe's answer (reflections) or drawn BLACK (the gather). Every
// mover was absent from ray reflections and from the gather's bounce.
//
// WHAT IS PROVED HERE, every number in frames and radiance (the float readback,
// HDR-READBACK-1), never time:
//
//   (a) A MOVER CRATE IN A PERFECT MIRROR beside a STATIC control (the SKIN-1
//       mirror suite's method: the reflection's silhouette against the raster
//       seen from the camera reflected in the mirror plane). The mover's
//       reflection covers at least the static control's fraction of its raster
//       silhouette, and its COLOUR — albedo x the sun x the shadow ray, plus the
//       scene's GI at the hit — is the RASTER's colour of the same face.
//   (b) THE GATHER: an emissive red panel that is a MOVER over a matte floor,
//       top-down: the floor's bounce against the closed form (Lambert's
//       projected solid angle, gi.gather_reference's instrument), at least
//       (1 - eps), eps derived from the probe's ray count and the frames averaged.
//       Before this lane the gather read the panel BLACK.
//   (c) The RIGGED arm is gi.skin_rays_mirror's (the posed character shaded).
//   (d) A HIT OUTSIDE THE FRUSTUM has NO Forward+ cell (the fork's
//       fwdFragCoord hook): a point light beside the crate behind the camera
//       does not reach its reflection — the reflection equals the crate's raster
//       with the point light OFF, and the raster with the light ON differs. The
//       CARDED floor around it is the cards' to answer (no record) and carries
//       the lamp the card captured.
//   (e) THE SHADOW RAY: a mover wall under a static overhang in sunlight
//       reflects DARK where the overhang's shadow falls and LIT beside it, as its
//       raster does.
//   (f) `engine.atom_parity` byte-identical is that suite's own (the screen
//       decode is untouched).
//   (g) A SAME-SLOT TEXTURE SWAP on a mover's live material reaches its hits
//       the next frame (the decode twin's witness carries the texture set).
//   (h) A GLOSSY mover (metal, roughness 0.1) in a sky that turns around the
//       horizon: its reflection's specular environment is its raster's from
//       the reflected camera (the reflection vector at a hit is the ray's).
//
// `--cost` (not a ctest row): the hit decode's and the write-back's GPU
// milliseconds against the frame's at 1920x1080, paired arms in ONE process —
// High and Epic, 0 / 1 / 30 movers, the records each arm appended — run it
// under scripts/gpu-exclusive.sh.
//
// SKIPPED, NOT FAILED, without ray-query hardware.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

/// THE MATTE RECIPE (the default ground's, GF1): Specular workflow at ior 1.0
/// with a black specular colour is F0 = 0 — no specular term at all, so a
/// surface's radiance is its diffuse (the sun and the GI) and nothing it
/// reflects, which is what makes a reflection of it and a raster of it the same
/// number.
static MaterialId matte(Scene *s, const Colour &albedo, const Colour &emissive = Colour(0, 0, 0))
{
    PbrParams p;
    p.albedo = albedo;
    p.emissive = emissive;
    p.roughness = 1.0f;
    p.workflow = PbrParams::Workflow::Specular;
    p.ior = 1.0f;
    p.specularColour = Colour(0.0f, 0.0f, 0.0f);
    return s->createPbrMaterial(p);
}

// ---------------------------------------------------------------------------
// THE MIRROR FIXTURE (arms a, d, e): the SKIN-1 suite's — a perfect mirror wall
// the camera faces, the subjects BEHIND the camera where no screen-space march
// can see them. The raster of a subject is seen from the camera reflected in the
// mirror plane, flipped left-right.
static const unsigned kSize = 384;
static const float kMirrorZ = 4.85f;
static const Vec3 kCam(0.0f, 1.0f, -3.0f);
static const Vec3 kCamR(0.0f, 1.0f, 2.0f * kMirrorZ + 3.0f);

struct Mask {
    std::vector<unsigned char> m;
    unsigned n = 0;
};

/// Pixels where two float pictures differ by more than `thr` of radiance in any
/// channel (flipped left-right for the raster).
static Mask diffMask(const ImageF &a, const ImageF &b, bool flip, float thr)
{
    Mask r;
    r.m.assign(size_t(a.width) * a.height, 0u);
    for (unsigned y = 0; y < a.height; ++y)
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour p = a.at(x, y), q = b.at(x, y);
            const float d = std::max(std::fabs(p.r - q.r), std::max(std::fabs(p.g - q.g), std::fabs(p.b - q.b)));
            if (d > thr) {
                const unsigned ox = flip ? a.width - 1u - x : x;
                r.m[size_t(y) * a.width + ox] = 1u;
                ++r.n;
            }
        }
    return r;
}

/// The mask eroded by `k` pixels (a pixel whose (2k+1)^2 neighbourhood is all in).
static Mask erode(const Mask &a, unsigned w, unsigned h, int k)
{
    Mask r;
    r.m.assign(a.m.size(), 0u);
    for (unsigned y = 0; y < h; ++y)
        for (unsigned x = 0; x < w; ++x) {
            bool all = true;
            for (int dy = -k; dy <= k && all; ++dy)
                for (int dx = -k; dx <= k && all; ++dx) {
                    const int xx = int(x) + dx, yy = int(y) + dy;
                    all = xx >= 0 && yy >= 0 && xx < int(w) && yy < int(h) && a.m[size_t(yy) * w + xx];
                }
            if (all) { r.m[size_t(y) * w + x] = 1u; ++r.n; }
        }
    return r;
}

/// The mean radiance of a picture over a mask (the mask in the MIRROR's frame;
/// `flip` reads the raster's own pixel).
static Colour maskMean(const ImageF &img, const Mask &m, bool flip)
{
    double s[3] = { 0, 0, 0 };
    unsigned n = 0;
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            if (!m.m[size_t(y) * img.width + x]) continue;
            const Colour c = img.at(flip ? img.width - 1u - x : x, y);
            s[0] += c.r; s[1] += c.g; s[2] += c.b;
            ++n;
        }
    return n ? Colour(float(s[0] / n), float(s[1] / n), float(s[2] / n)) : Colour(0, 0, 0);
}

static float relDiff(const Colour &a, const Colour &b)
{
    float worst = 0.0f;
    const float av[3] = { a.r, a.g, a.b }, bv[3] = { b.r, b.g, b.b };
    for (int k = 0; k < 3; ++k)
        worst = std::max(worst, std::fabs(av[k] - bv[k]) / std::max(bv[k], 1e-4f));
    return worst;
}

static int mirrorArms(Engine *e)
{
    View *view = e->createOffscreenView("hitmirror", kSize, kSize, Colour(0, 0, 0));
    Scene *s = e->createScene("hitmirror");
    view->setScene(s);
    view->setOffscreenContract(OffscreenContract::StillPicture);
    view->setShadows(true);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.04f, 0.04f, 0.04f));
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const NodeId wall = s->createNode();
    {
        PbrParams wp;
        wp.albedo = Colour(1, 1, 1);
        wp.metalness = 1.0f;
        wp.roughness = 0.0f;
        s->attachMesh(wall, cube, s->createPbrMaterial(wp));
    }
    enginetest::setNodeScale(s, wall, Vec3(14.0f, 9.0f, 0.3f));
    enginetest::setNodePosition(s, wall, Vec3(0.0f, 1.0f, kMirrorZ + 0.15f));
    // A matte floor under everything (the crates stand on it; its own reflection
    // is the same in every arm, so the difference masks never see it).
    const NodeId floorN = s->createNode();
    {
        MeshData md = enginetest::unitCubeMesh();
        md.cards = enginetest::boxCards(0.5f);
        s->attachMesh(floorN, s->createMesh(md), matte(s, Colour(0.3f, 0.3f, 0.3f)));
    }
    s->setNodeTransform(floorN, Vec3(0.0f, -0.55f, -4.0f), Quat(), Vec3(30.0f, 0.1f, 30.0f));
    // THE SUN FROM THE MIRROR'S SIDE (it travels towards -Z), so the faces the
    // mirror sees are lit, at a slope that lets arm (e)'s overhang shade half a wall.
    NodeId sun = enginetest::addDirectionalLight(s, Vec3(-0.25f, -1.0f, -1.05f), 3.0f);

    // THE SUBJECTS, behind the camera: a static crate (the control, the voxels
    // hold it) and a MOVER crate (kMovableBit: not voxelised, no cards) of the
    // same mesh and material.
    const MaterialId crateMat = matte(s, Colour(0.7f, 0.45f, 0.2f));
    const NodeId stat = s->createNode(), mov = s->createNode();
    s->setNodeMovable(mov, true);
    s->attachMesh(stat, cube, crateMat);
    s->attachMesh(mov, cube, crateMat);
    // THE SAME PLACE, shown one at a time: the silhouettes are then the same
    // pixels, and the coverage fractions compare like for like.
    enginetest::setNodePosition(s, stat, Vec3(0.3f, 0.0f, -8.0f));
    enginetest::setNodePosition(s, mov, Vec3(0.3f, 0.0f, -8.0f));

    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.numBounces = 1;
    gi.gather = GiToggle::Off;   // the raster's diffuse and the decode's are then the same text
    // THE CARDS ON, the whole suite: the floor is carded, and a hit on it is the
    // cards' to answer (with the movers' traced shadow, PHOTON-CARDS-4) — a hit
    // on a mover never is (movers carry no cards), and that one is the decode's.
    gi.cards = GiToggle::On;
    gi.cardResidencyRadius = 40.0f;
    gi.testBoundsMin = Vec3(-8.0f, -2.0f, -12.0f);
    gi.testBoundsMax = Vec3(8.0f, 8.0f, 6.0f);
    s->setGlobalIllumination(gi);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;                  // full-resolution rays (Epic's trace)
    fx.hdrReadback = true;
    view->setPostFx(fx);

    const auto shot = [&](bool mirrorArm) {
        s->setNodeVisible(wall, mirrorArm);
        if (mirrorArm) enginetest::testCameraLookAt(view, kCam, Vec3(0.0f, 1.0f, kMirrorZ));
        else enginetest::testCameraLookAt(view, kCamR, Vec3(0.0f, 1.0f, -20.0f));
        render(e, 40);
        ImageF img;
        view->readPixelsHdr(img);
        return img;
    };
    const auto show = [&](bool st, bool mv) {
        s->setNodeVisible(stat, st);
        s->setNodeVisible(mov, mv);
    };

    // ---- (a) coverage and colour -------------------------------------------
    std::printf("\n== (a) a MOVER crate beside a STATIC control in a perfect mirror ==\n");
    // THE CRATE AND ITS SHADOW: the floor is SHOWN and carded for this arm, so the
    // difference masks hold the crate AND its sun shadow on the floor — the
    // floor's hit is the CARDS' to shade, and they carry a mover's shadow as the
    // traced movers' term (PHOTON-CARDS-4) and the static control's as the
    // captured term. (Before PHOTON-CARDS-4 the floor was hidden here: the caches
    // held the still world only.)
    s->setNodeVisible(floorN, true);
    float frac[2] = { 0.0f, 0.0f };
    unsigned interiorMiss[2] = { 0u, 0u };
    unsigned coreN[2] = { 0u, 0u };
    Colour moverRefl, moverRast;
    // PASS 1, GI ON — THE COVERAGE: the static control's hits are the CACHES'
    // (its voxels), the mover's the decode's.
    // PASS 2, GI OFF — THE COLOUR: the raster is seen with the mirror wall
    // HIDDEN, and the wall bounces the sun back onto the crate in the voxels only
    // while it is shown; with GI off the colour compared is albedo x the sun x
    // the shadow ray (+ the ambient), the same on both sides.
    for (int pass = 0; pass < 2; ++pass) {
        GiParams g = gi;
        if (pass == 1) g.mode = GiMode::Off;
        s->setGlobalIllumination(g);
        show(false, false);
        const ImageF mNone = shot(true), rNone = shot(false);
        for (int arm = (pass == 0 ? 0 : 1); arm < 2; ++arm) {
            show(arm == 0, arm == 1);
            const ImageF mOn = shot(true), rOn = shot(false);
            const Mask refl = diffMask(mOn, mNone, false, 0.01f);
            const Mask rast = diffMask(rOn, rNone, true, 0.01f);
            if (pass == 0) {
                unsigned both = 0;
                for (size_t i = 0; i < refl.m.size(); ++i) both += (refl.m[i] && rast.m[i]);
                frac[arm] = rast.n ? float(both) / float(rast.n) : 0.0f;
                // A MISS INSIDE THE SILHOUETTE (2 px in from its edge) is a hit
                // nothing shaded; a miss on the edge is the trace's own sampling.
                const Mask rastCore = erode(rast, kSize, kSize, 2);
                for (size_t i = 0; i < rastCore.m.size(); ++i) interiorMiss[arm] += (rastCore.m[i] && !refl.m[i]);
                coreN[arm] = rastCore.n;
                std::printf("   %-7s: reflection %u px, raster %u px, both %u (%.1f %% of the raster), %u misses "
                            "inside the silhouette (2 px in, %u px)\n", arm == 0 ? "STATIC" : "MOVER", refl.n, rast.n,
                            both, 100.0f * frac[arm], interiorMiss[arm], rastCore.n);
                if (std::getenv("JAH_HIT_DUMP")) {
                    // Evidence: the reflection mask (red), the raster mask (green), both = yellow.
                    FILE *f = std::fopen(arm == 0 ? "hit-a-static.ppm" : "hit-a-mover.ppm", "wb");
                    if (f) {
                        std::fprintf(f, "P6\n%u %u\n255\n", kSize, kSize);
                        for (size_t i = 0; i < refl.m.size(); ++i) {
                            const unsigned char px[3] = { refl.m[i] ? (unsigned char)255 : (unsigned char)0,
                                                          rast.m[i] ? (unsigned char)255 : (unsigned char)0, 0 };
                            std::fwrite(px, 1, 3, f);
                        }
                        std::fclose(f);
                    }
                }
                continue;
            }
            // THE COLOUR over the interior both agree on (2 px in from every edge:
            // the trace's own edge at full resolution, and the mirror's half-pixel).
            Mask in;
            in.m.assign(refl.m.size(), 0u);
            for (size_t i = 0; i < refl.m.size(); ++i)
                if (refl.m[i] && rast.m[i]) { in.m[i] = 1u; ++in.n; }
            const Mask core = erode(in, kSize, kSize, 2);
            moverRefl = maskMean(mOn, core, false);
            moverRast = maskMean(rOn, core, true);
            std::printf("   MOVER, GI off: interior %u px: reflection (%.4f %.4f %.4f), raster (%.4f %.4f %.4f)\n",
                        core.n, moverRefl.r, moverRefl.g, moverRefl.b, moverRast.r, moverRast.g, moverRast.b);
        }
    }
    s->setGlobalIllumination(gi);

    // THE CRATE IS WATERTIGHT TO A RAY (F-B): a 64 x 64 sheet of parallel rays at
    // an oblique direction that sees three faces and their shared edges; every ray
    // whose line passes through the box shrunk by 5 mm must hit the crate at the
    // analytic entry distance. A seam between two faces that lets a ray through
    // would be an interior miss. The unit cube's 24 unwelded vertices carry
    // bit-identical corners, and the trace is watertight across them (measured:
    // 0 of 1840) — the 2 "misses inside the silhouette" above are the mirror
    // picture's masks, not rays through the crate.
    {
        s->setNodeVisible(wall, false);
        const float c[3] = { 0.3f, 0.0f, -8.0f };
        float d[3] = { 0.55f, -0.45f, -1.0f };
        const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (float &x : d) x /= dl;
        // A basis of the sheet's plane.
        float u[3] = { d[2], 0.0f, -d[0] };
        const float ul = std::sqrt(u[0] * u[0] + u[2] * u[2]);
        u[0] /= ul; u[2] /= ul;
        const float v[3] = { d[1] * u[2] - d[2] * u[1], d[2] * u[0] - d[0] * u[2], d[0] * u[1] - d[1] * u[0] };
        const auto slab = [&](const float o[3], float half, float &tEnter) {
            float t0 = -1e30f, t1 = 1e30f;
            for (int k = 0; k < 3; ++k) {
                const float lo = (c[k] - half - o[k]) / d[k], hi = (c[k] + half - o[k]) / d[k];
                t0 = std::max(t0, std::min(lo, hi));
                t1 = std::min(t1, std::max(lo, hi));
            }
            tEnter = t0;
            return t0 <= t1 && t1 > 0.0f;
        };
        for (int arm = 0; arm < 2; ++arm) {
            show(arm == 0, arm == 1);
            render(e, 6);
            std::vector<float> in, out;
            std::vector<std::array<float, 3>> origins;
            const unsigned m = kRayMaskNearField;
            float mbits;
            std::memcpy(&mbits, &m, sizeof(mbits));
            for (int j = 0; j < 64; ++j)
                for (int i = 0; i < 64; ++i) {
                    const float a = -0.95f + 1.9f * (float(i) + 0.5f) / 64.0f;
                    const float b = -0.95f + 1.9f * (float(j) + 0.5f) / 64.0f;
                    std::array<float, 3> o;
                    for (int k = 0; k < 3; ++k) o[size_t(k)] = c[k] - 4.0f * d[k] + a * u[k] + b * v[k];
                    origins.push_back(o);
                    in.insert(in.end(), { o[0], o[1], o[2], 0.0f, d[0], d[1], d[2], 10.0f, mbits, 0, 0, 0 });
                }
            const bool traced = s->traceRays(in, out) && out.size() >= origins.size() * 4u;
            int interior = 0, missed = 0, wrongT = 0;
            for (size_t k = 0; traced && k < origins.size(); ++k) {
                float tIn = 0.0f, tFull = 0.0f;
                if (!slab(origins[k].data(), 0.5f - 0.005f, tIn)) continue;
                slab(origins[k].data(), 0.5f, tFull);
                ++interior;
                const bool hit = out[k * 4u + 3u] > 0.5f;
                if (!hit) ++missed;
                else if (std::fabs(out[k * 4u] - tFull) > 2e-3f) ++wrongT;
            }
            CHECK_MSG(traced && interior > 1000 && missed == 0 && wrongT == 0,
                      "(a) the %s crate is watertight: a 64x64 oblique ray sheet, %d rays through its interior, "
                      "%d missed it, %d hit it off the analytic entry", arm == 0 ? "STATIC" : "MOVER", interior,
                      missed, wrongT);
        }
        show(false, false);
    }
    s->setNodeVisible(floorN, true);
    // THE BAR: the static control's fraction, less one point for the silhouette's
    // edge (a few pixels of a ~90 px perimeter land on either side of the trace's
    // own sample). THE MISSES INSIDE IT have an ABSOLUTE bar, never the control's
    // count (a control that scores 0 would fail the mover on its own luck): the
    // sheet above finds 0 rays through the crate (1840 of 1840 at the analytic
    // entry), so a miss 2 px inside the silhouette is the mirror picture's own —
    // measured 2 in each arm alike — and the bar is 1 % of the eroded silhouette
    // (~370 px: 3). A leaking seam misses a LINE of pixels along an edge, a face's
    // width (~20 px at this framing), which this bar refuses.
    // ...AND HALF A POINT MORE FOR THE CONTROL'S SOFT FOOT (PHOTON-CARDS-5,
    // measured): since the card read restores the lobe's view term the floor
    // around the crates reflects at the grazing raster's brightness (1.6-2x its
    // head-on value), and the STATIC control's captured PSSM shadow, SOFT at the
    // crate's foot, crosses the masks' 0.01 threshold on 48 more pixels — a 1-3
    // px line along the bottom edge of the silhouette (rows 225-227 of 203-227;
    // spikes/photon-cards-5/hit-a-*.ppm). The MOVER's shadow on the cards is the
    // traced movers' term (PHOTON-CARDS-4), HARD, so it has no such penumbra: the
    // control rose 92.5 -> 93.8 %, the mover 92.6 -> 92.8 % with its interior
    // misses 2 -> 1. The mover's coverage did not fall; the control's edge grew.
    const unsigned missBar = std::max(1u, coreN[1] / 100u);
    CHECK_MSG(frac[1] >= frac[0] - 0.015f && interiorMiss[1] <= missBar,
              "(a) the MOVER's reflection covers %.1f %% of its raster silhouette, the static control's %.1f %% "
              "(bar: less 1.5 points on the edge and the control's soft foot); misses inside it %u of %u px (bar %u, 1 %%; the control's %u) "
              "(before this lane: 0 %% — a mover's hit was handed back to the probe)",
              100.0f * frac[1], 100.0f * frac[0], interiorMiss[1], coreN[1], missBar, interiorMiss[0]);
    // THE BAR: the reflection is the decode of the SAME surface point the raster
    // shades, with the same lighting text; what may differ is the history's
    // RGBA16F quantum (2^-11), the sun's term (the shadow ray against the map —
    // both 1 on a lit face) and the ambient read at a hit vs a pixel (the same
    // world point) — 2 % of the radiance.
    CHECK_MSG(relDiff(moverRefl, moverRast) < 0.02f,
              "(a) the MOVER's reflected COLOUR is its raster's: (%.4f %.4f %.4f) against (%.4f %.4f %.4f), "
              "worst channel %.2f %% (bar 2 %%)",
              moverRefl.r, moverRefl.g, moverRefl.b, moverRast.r, moverRast.g, moverRast.b,
              100.0f * relDiff(moverRefl, moverRast));
    {
        // The counters are read back several frames late: read them after a
        // MIRROR shot (the raster from the reflected camera traces no mirror).
        show(false, true);
        shot(true);
        const RayQueryStatus st = s->rayQueryStatus();
        std::printf("   records: %llu appended, %llu dropped, capacity %llu, %d decode draws\n", st.hitRecords,
                    st.hitDropped, st.hitCapacity, st.hitDecodeDraws);
        CHECK_MSG(st.hitRecords > 0 && st.hitDropped == 0 && st.hitDecodeDraws >= 1,
                  "(a) the hit list carried the mover's records (%llu) and dropped none", st.hitRecords);
    }

    // ---- (d) a hit outside the frustum has no Forward+ cell -----------------
    std::printf("\n== (d) a hit OUTSIDE the frustum: no clustered light reaches it ==\n");
    show(false, true);
    // GI OFF for this arm: the lamp's light would otherwise reach the crate a
    // second way — injected into the voxels and bounced off the floor — in the
    // reflection and the raster alike, and the arm compares DIRECT light.
    {
        GiParams off = gi;
        off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
    }
    show(false, false);
    const ImageF mNoneD = shot(true), rNoneD = shot(false);
    // WHO ANSWERS THE FLOOR'S HITS: the mirror shot above sees the floor and no
    // crate — every hit it traced is a card's (an out-of-frustum card hit writes
    // no record); the hit list must be empty.
    const unsigned long long floorRecords = s->rayQueryStatus().hitRecords;
    show(false, true);
    const NodeId lamp = s->createNode();
    enginetest::setNodePosition(s, lamp, Vec3(1.2f, 1.3f, -6.8f));
    LightDesc ld;
    ld.type = LightType::Point;
    ld.colour = Colour(0.2f, 0.4f, 1.0f);
    ld.intensity = 6.0f;
    ld.range = 6.0f;
    ld.castShadows = false;   // a CLUSTERED light (a shadow caster rides the forward list)
    s->setLight(lamp, ld);
    const ImageF mLampOn = shot(true), rLampOn = shot(false);
    ld.intensity = 0.0f;
    s->setLight(lamp, ld);
    const ImageF rLampOff = shot(false), mLampOff = shot(true);
    {
        const Mask refl = diffMask(mLampOn, mNoneD, false, 0.01f);
        const Mask rast = diffMask(rLampOff, rNoneD, true, 0.01f);
        Mask in;
        in.m.assign(refl.m.size(), 0u);
        for (size_t i = 0; i < refl.m.size(); ++i)
            if (refl.m[i] && rast.m[i]) { in.m[i] = 1u; ++in.n; }
        const Mask core = erode(in, kSize, kSize, 2);
        const Colour cm = maskMean(mLampOn, core, false), cOff = maskMean(rLampOff, core, true),
                     cOn = maskMean(rLampOn, core, true);
        std::printf("   interior %u px: reflection with the lamp ON (%.4f %.4f %.4f), raster lamp OFF (%.4f %.4f "
                    "%.4f), raster lamp ON (%.4f %.4f %.4f)\n",
                    core.n, cm.r, cm.g, cm.b, cOff.r, cOff.g, cOff.b, cOn.r, cOn.g, cOn.b);
        CHECK_MSG(relDiff(cOn, cOff) > 0.1f,
                  "(d) the lamp lights the crate's raster (lamp on vs off: %.1f %% in the worst channel) — the "
                  "fixture can see a clustered light", 100.0f * relDiff(cOn, cOff));
        // THE TWO ANSWERERS. The CRATE's pixels (its silhouette in the raster
        // with the floor hidden) are DECODE hits — a mover carries no cards, a
        // record each — and the decode has no Forward+ cell off-screen: no
        // clustered light, the lamp-OFF raster. The FLOOR the lamp lights (the
        // raster's lamp-ON minus lamp-OFF, off the crate) is answered by the
        // CARDS, which hold the light they CAPTURED, the lamp's included: the
        // lamp-ON raster (physics: the lamp lights that floor from wherever it is
        // seen). The earlier single mask mixed the two at the crate's foot — the
        // blue channel 3.88 % off the lamp-OFF raster was the carded floor there.
        //
        // THE CARDS' HALF IS THE GRAZING RASTER (PHOTON-CARDS-5). The mirror sees
        // this floor at N.V ~ 0.08, where HlmsPbs's Disney diffuse is up to
        // 1.6-2x its head-on value (the view term: the retro-reflection of a
        // light behind the eye). A card stores the head-on value and the read
        // restores the view term for its ray (jah_card_view.glsl) from ONE mean
        // light direction per texel: EXACT for one light — the lamp-OFF floor
        // (the sun + the ambient) — and, for the sun + the lamp together, off by
        // the spread of their view terms per channel (the mean direction is
        // luminance-weighted, and the lamp is blue while the sun is white). That
        // error is PREDICTED here per pixel from the closed form (the lobe, the
        // lights, the crate's shadow, the camera's rays) and the measurement is
        // held to the prediction. Before PHOTON-CARDS-5: 41.6 % off, the card
        // stored the head-on value and was read as it (and held no ambient at GI
        // off).
        s->setNodeVisible(floorN, false);
        show(false, false);
        const ImageF rBare = shot(false);
        show(false, true);
        const Mask crate = diffMask(shot(false), rBare, true, 0.01f);
        s->setNodeVisible(floorN, true);
        const Mask lampLit = diffMask(rLampOn, rLampOff, true, 0.01f);
        Mask onCrate, onFloor;
        onCrate.m.assign(core.m.size(), 0u);
        onFloor.m.assign(core.m.size(), 0u);
        for (size_t i = 0; i < core.m.size(); ++i) {
            if (core.m[i] && crate.m[i]) { onCrate.m[i] = 1u; ++onCrate.n; }
            if (lampLit.m[i] && !crate.m[i]) { onFloor.m[i] = 1u; ++onFloor.n; }
        }
        const Mask dec = erode(onCrate, kSize, kSize, 2), crd = erode(onFloor, kSize, kSize, 2);
        const Colour dm = maskMean(mLampOn, dec, false), dOff = maskMean(rLampOff, dec, true);
        const Colour fm = maskMean(mLampOn, crd, false), fOn = maskMean(rLampOn, crd, true),
                     fOff = maskMean(rLampOff, crd, true);
        const Colour fmOff = maskMean(mLampOff, crd, false);
        std::printf("   the decode's half (the crate, %u px): reflection (%.4f %.4f %.4f), raster lamp OFF (%.4f %.4f "
                    "%.4f); the cards' half (the lamp-lit floor, %u px): reflection (%.4f %.4f %.4f), raster "
                    "lamp ON (%.4f %.4f %.4f), lamp OFF (%.4f %.4f %.4f); floor-only records %llu\n",
                    dec.n, dm.r, dm.g, dm.b, dOff.r, dOff.g, dOff.b, crd.n, fm.r, fm.g, fm.b, fOn.r, fOn.g, fOn.b,
                    fOff.r, fOff.g, fOff.b, floorRecords);
        CHECK_MSG(floorRecords == 0ull,
                  "(d) the floor's hits are the CARDS' (the mirror seeing the floor alone appended %llu records)",
                  floorRecords);
        CHECK_MSG(dec.n > 50 && relDiff(dm, dOff) < 0.02f,
                  "(d) the crate's REFLECTION (a decode hit outside the frustum: no cell) is the lamp-OFF raster "
                  "within %.2f %% (bar 2 %%): the sun, the field, the probes and the sky, no clustered light",
                  100.0f * relDiff(dm, dOff));
        const Colour cardLamp(fm.r - fmOff.r, fm.g - fmOff.g, fm.b - fmOff.b),
                     rastLamp(fOn.r - fOff.r, fOn.g - fOff.g, fOn.b - fOff.b);
        std::printf("   the cards' half: mirror lamp OFF (%.4f %.4f %.4f); the lamp's share, mirror (%.4f %.4f %.4f) "
                    "against the raster's (%.4f %.4f %.4f); the mirror %.1f %% off the lamp-ON raster\n",
                    fmOff.r, fmOff.g, fmOff.b, cardLamp.r, cardLamp.g, cardLamp.b, rastLamp.r, rastLamp.g,
                    rastLamp.b, 100.0f * relDiff(fm, fOn));
        // THE PREDICTION, per floor pixel of the mask: the raster camera's ray
        // through the pixel (the mask is the mirror's frame, the raster its
        // mirror image) to the floor's top, the view V back up it, and the two
        // lights' closed forms — the sun (power 3: albedo x 3 / pi x N.L x the
        // lobe, 0 where the crate shadows it) and the lamp (albedo x colour x 6 x
        // 1 / (0.5 + (0.5 / R^2) d^2) x (R - d) / R x N.L x the lobe) — plus the
        // ambient pair's upper colour x albedo x A(N.V, 1) (the read restores it
        // exactly). EXACT = each light's lobe at V; READ = each light's lobe at
        // V = N summed, times the lobe ratio at the luminance-weighted mean
        // direction (jah_card_view.glsl's arithmetic).
        double predicted[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };   // [lamp off, on][channel]: read / exact
        {
            const double kPiD = 3.14159265358979323846;
            const double albedo = 0.3, kLampR = 6.0;
            const double lampPos[3] = { 1.2, 1.3, -6.8 }, lampCol[3] = { 0.2, 0.4, 1.0 };
            const double amb[3] = { 0.05, 0.05, 0.06 };
            double sunL[3] = { 0.25, 1.0, 1.05 };
            {
                const double l = std::sqrt(sunL[0] * sunL[0] + sunL[1] * sunL[1] + sunL[2] * sunL[2]);
                for (double &c : sunL) c /= l;
            }
            const auto lobe = [](const double L[3], const double V[3]) {
                // jahDisneyDiffuse x N.L with N = +Y (the energy factor at r = 1).
                double H[3] = { L[0] + V[0], L[1] + V[1], L[2] + V[2] };
                const double hl = std::sqrt(H[0] * H[0] + H[1] * H[1] + H[2] * H[2]);
                const double VdotH = (V[0] * H[0] + V[1] * H[1] + V[2] * H[2]) / hl;
                const double NdotL = std::max(0.0, L[1]), NdotV = std::max(1e-4, V[1]);
                const double fd90 = 0.5 + 2.0 * VdotH * VdotH;
                return NdotL * (1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotL, 5.0)) *
                       (1.0 + (fd90 - 1.0) * std::pow(1.0 - NdotV, 5.0)) / 1.51;
            };
            const double N[3] = { 0.0, 1.0, 0.0 };
            // The raster camera (testCameraDescLookAt's frame), 45 degrees vertical.
            const double eye[3] = { kCamR.x, kCamR.y, kCamR.z };
            double f[3] = { 0.0 - eye[0], 1.0 - eye[1], -20.0 - eye[2] };
            {
                const double l = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
                for (double &c : f) c /= l;
            }
            double r[3] = { -f[2], 0.0, f[0] };   // f x +Y
            {
                const double l = std::sqrt(r[0] * r[0] + r[2] * r[2]);
                r[0] /= l; r[2] /= l;
            }
            const double u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
            const double th = std::tan(0.5 * 45.0 * kPiD / 180.0);
            // The crate (a unit cube at (0.3, 0, -8)) shadows the sun: a slab test.
            const auto sunVisible = [&](const double P[3]) {
                const double c[3] = { 0.3, 0.0, -8.0 };
                double t0 = 1e-4, t1 = 1e30;
                for (int k = 0; k < 3; ++k) {
                    const double lo = (c[k] - 0.5 - P[k]) / sunL[k], hi = (c[k] + 0.5 - P[k]) / sunL[k];
                    t0 = std::max(t0, std::min(lo, hi));
                    t1 = std::min(t1, std::max(lo, hi));
                }
                return !(t0 <= t1);
            };
            double sumRead[2][3] = {}, sumExact[2][3] = {};
            for (unsigned y = 0; y < kSize; ++y)
                for (unsigned x = 0; x < kSize; ++x) {
                    if (!crd.m[size_t(y) * kSize + x]) continue;
                    const unsigned rx = kSize - 1u - x;   // the raster's own pixel
                    const double ndx = (2.0 * (double(rx) + 0.5) / kSize - 1.0) * th;
                    const double ndy = (1.0 - 2.0 * (double(y) + 0.5) / kSize) * th;
                    double d[3];
                    for (int k = 0; k < 3; ++k) d[k] = f[k] + ndx * r[k] + ndy * u[k];
                    if (d[1] >= 0.0) continue;
                    const double t = (-0.5 - eye[1]) / d[1];
                    const double P[3] = { eye[0] + t * d[0], -0.5, eye[2] + t * d[2] };
                    double V[3] = { eye[0] - P[0], eye[1] - P[1], eye[2] - P[2] };
                    {
                        const double l = std::sqrt(V[0] * V[0] + V[1] * V[1] + V[2] * V[2]);
                        for (double &c : V) c /= l;
                    }
                    double lampL[3] = { lampPos[0] - P[0], lampPos[1] - P[1], lampPos[2] - P[2] };
                    const double dist = std::sqrt(lampL[0] * lampL[0] + lampL[1] * lampL[1] + lampL[2] * lampL[2]);
                    for (double &c : lampL) c /= dist;
                    const double att = dist < kLampR ? 1.0 / (0.5 + (0.5 / (kLampR * kLampR)) * dist * dist) *
                                                           (kLampR - dist) / kLampR
                                                     : 0.0;
                    const double sunE = sunVisible(P) ? albedo * 3.0 / kPiD : 0.0;
                    const double Aview = enginetest::disneyDiffuseAlbedo(V[1], 1.0);
                    for (int on = 0; on < 2; ++on) {
                        // Each light's share at V = N (what the card stores), per channel.
                        double dS[3], dL[3];
                        for (int k = 0; k < 3; ++k) {
                            dS[k] = sunE * lobe(sunL, N);
                            dL[k] = on ? albedo * lampCol[k] * 6.0 * att * lobe(lampL, N) : 0.0;
                        }
                        const double wS = 0.2126 * dS[0] + 0.7152 * dS[1] + 0.0722 * dS[2];
                        const double wL = 0.2126 * dL[0] + 0.7152 * dL[1] + 0.0722 * dL[2];
                        double Lm[3];
                        for (int k = 0; k < 3; ++k) Lm[k] = sunL[k] * wS + lampL[k] * wL;
                        const double lm = std::sqrt(Lm[0] * Lm[0] + Lm[1] * Lm[1] + Lm[2] * Lm[2]);
                        double factor = 1.0;
                        if (lm > 1e-12) {
                            for (double &c : Lm) c /= lm;
                            factor = lobe(Lm, V) / lobe(Lm, N);
                        }
                        const double viewS = lobe(sunL, N) > 0.0 ? lobe(sunL, V) / lobe(sunL, N) : 0.0;
                        const double viewL = lobe(lampL, N) > 0.0 ? lobe(lampL, V) / lobe(lampL, N) : 0.0;
                        for (int k = 0; k < 3; ++k) {
                            const double ambient = amb[k] * albedo * Aview;
                            sumRead[on][k] += (dS[k] + dL[k]) * factor + ambient;
                            sumExact[on][k] += dS[k] * viewS + dL[k] * viewL + ambient;
                        }
                    }
                }
            for (int on = 0; on < 2; ++on)
                for (int k = 0; k < 3; ++k)
                    predicted[on][k] = sumExact[on][k] > 0.0 ? sumRead[on][k] / sumExact[on][k] : 1.0;
        }
        // THE BAR, per channel: the store's quanta — half an R11G11B10F step of the
        // radiance read (2^-7 red and green, 2^-6 blue, relative) and half an 8-bit
        // step of the card's kD (0.5 / (255 x 0.3 / pi) = 2.05 %) — plus the card's
        // resolution edge, 1 % (one 23 cm texel of the 30 m floor against the
        // raster's pixel; the lamp-lit floor's gradient across it).
        const double kdHalf = 0.5 / (255.0 * 0.3 / 3.14159265358979323846);
        const double bar[3] = { 1.0 / 128.0 + kdHalf + 0.01, 1.0 / 128.0 + kdHalf + 0.01, 1.0 / 64.0 + kdHalf + 0.01 };
        const float mOffv[3] = { fmOff.r, fmOff.g, fmOff.b }, rOffv[3] = { fOff.r, fOff.g, fOff.b };
        const float mOnv[3] = { fm.r, fm.g, fm.b }, rOnv[3] = { fOn.r, fOn.g, fOn.b };
        for (int on = 0; on < 2; ++on)
            for (int k = 0; k < 3; ++k) {
                const double measured = double(on ? mOnv[k] : mOffv[k]) / double(on ? rOnv[k] : rOffv[k]);
                const double off = std::fabs(measured / predicted[on][k] - 1.0);
                CHECK_MSG(crd.n > 50 && off <= bar[k],
                          "(d) the cards' half, lamp %s, channel %d: the mirror / the grazing raster %.4f against"
                          " the view term's prediction %.4f (%s) — %.2f %% apart (bar %.2f %%: the store's quanta"
                          " + the texel)", on ? "ON" : "OFF", k, measured, predicted[on][k],
                          on ? "the sun + the lamp through ONE mean direction: the multi-light error"
                             : "one light: exact",
                          100.0 * off, 100.0 * bar[k]);
            }
        CHECK_MSG(crd.n > 50 && relDiff(fOn, fOff) > 0.1f && relDiff(fm, fmOff) > 0.1f,
                  "(d) the floor's REFLECTION (a card hit outside the frustum) CARRIES the lamp: it moves %.0f %% "
                  "with it (the raster %.0f %%) — the card holds the light it captured",
                  100.0f * relDiff(fm, fmOff), 100.0f * relDiff(fOn, fOff));
    }
    // ...AND A HIT INSIDE THE FRUSTUM FINDS ITS CELL (the hook's other half): the
    // mover in FRONT of the camera, off-axis so its reflection is not hidden
    // behind it, the lamp between it and the mirror lighting the face the mirror
    // sees. Its reflection must carry the lamp, as its raster does.
    {
        // The floor hidden (the crate's sun shadow on the floor the camera sees
        // DIRECTLY would enter the difference masks), the lamp off to the side and
        // above (its highlight on the mirror must not overlay the reflected face).
        // ...and NO SUN: this crate stands between the camera and the mirror, where
        // the mirror wall — hidden for the raster shots — shades it from the sun
        // in the mirror shots only.
        s->setNodeVisible(floorN, false);
        s->removeNode(sun);
        // ...and RAYS ONLY: this crate is ON SCREEN, and the screen march finds
        // its camera-facing side (a thickness guess) for part of the mirror's
        // pixels — the march's answer, not the hit's.
        PostFxDesc raysOnly = fx;
        raysOnly.ssrScreenMarch = false;
        view->setPostFx(raysOnly);
        // Below and beside the eye line: the crate seen DIRECTLY (11-23 degrees
        // down) does not overlap its own reflection (5-10 degrees down), and the
        // face the mirror sees (10-21 degrees off axis) is inside the 22.5-degree
        // half-angle of the frustum; the lamp's highlight on the mirror falls
        // clear of the reflected face.
        enginetest::setNodePosition(s, mov, Vec3(1.4f, -0.5f, 1.8f));
        enginetest::setNodePosition(s, lamp, Vec3(2.4f, 0.6f, 2.9f));
        ld.intensity = 6.0f;
        s->setLight(lamp, ld);
        show(false, false);
        const ImageF mNo = shot(true), rNo = shot(false);
        show(false, true);
        const ImageF mOn = shot(true), rOn = shot(false);
        ld.intensity = 0.0f;
        s->setLight(lamp, ld);
        const ImageF rDark = shot(false);
        // A DIAGNOSTIC for the sub-arm: the SAME crate with a directional light
        // only (no lamp) — what the mirror's own composite does to a reflection
        // at this screen position, independent of any cell.
        const NodeId glow = s->createNode();
        s->setNodeMovable(glow, true);
        s->attachMesh(glow, cube, matte(s, Colour(0, 0, 0), Colour(0.3f, 0.3f, 0.3f)));
        enginetest::setNodePosition(s, glow, Vec3(1.4f, -0.5f, 1.8f));
        s->setNodeVisible(mov, false);
        const ImageF mKey = shot(true), rKey = shot(false);
        s->removeNode(glow);
        s->setNodeVisible(mov, true);
        const Mask refl = diffMask(mOn, mNo, false, 0.01f);
        const Mask rast = diffMask(rOn, rNo, true, 0.01f);
        Mask in;
        in.m.assign(refl.m.size(), 0u);
        for (size_t i = 0; i < refl.m.size(); ++i)
            if (refl.m[i] && rast.m[i]) { in.m[i] = 1u; ++in.n; }
        const Mask core = erode(in, kSize, kSize, 2);
        const Colour cm = maskMean(mOn, core, false), cr = maskMean(rOn, core, true),
                     cd = maskMean(rDark, core, true);
        if (std::getenv("JAH_HIT_DUMP")) {
            FILE *f = std::fopen("hit-d2.ppm", "wb");
            if (f) {
                std::fprintf(f, "P6\n%u %u\n255\n", kSize * 2u, kSize);
                for (unsigned y = 0; y < kSize; ++y)
                    for (unsigned x = 0; x < kSize * 2u; ++x) {
                        const Colour c = x < kSize ? mOn.at(x, y) : rOn.at(kSize - 1u - (x - kSize), y);
                        const bool m = core.m[size_t(y) * kSize + (x % kSize)] != 0;
                        const unsigned char px[3] = { (unsigned char)std::min(255.0f, c.r * 200.0f),
                                                      (unsigned char)std::min(255.0f, c.g * 200.0f),
                                                      (unsigned char)(m ? 255 : std::min(255.0f, c.b * 200.0f)) };
                        std::fwrite(px, 1, 3, f);
                    }
                std::fclose(f);
            }
        }
        {
            const Colour km = maskMean(mKey, core, false), kr = maskMean(rKey, core, true);
            std::printf("   IN the frustum, an EMISSIVE crate (0.3) in its place: reflection (%.4f %.4f %.4f), raster (%.4f "
                        "%.4f %.4f) — ratio %.3f\n", km.r, km.g, km.b, kr.r, kr.g, kr.b, km.r / std::max(kr.r, 1e-6f));
        }
        std::printf("   IN the frustum, interior %u px: reflection lamp ON (%.4f %.4f %.4f), raster lamp ON (%.4f "
                    "%.4f %.4f), raster lamp OFF (%.4f %.4f %.4f)\n",
                    core.n, cm.r, cm.g, cm.b, cr.r, cr.g, cr.b, cd.r, cd.g, cd.b);
        CHECK_MSG(core.n > 50 && relDiff(cr, cd) > 0.1f && relDiff(cm, cr) < 0.02f,
                  "(d) a hit INSIDE the frustum finds its Forward+ cell: its reflection carries the clustered lamp "
                  "(%.2f %% from the lamp-ON raster, bar 2 %%; the lamp moves the raster %.0f %%)",
                  100.0f * relDiff(cm, cr), 100.0f * relDiff(cr, cd));
        enginetest::setNodePosition(s, mov, Vec3(0.3f, 0.0f, -8.0f));
        s->setNodeVisible(floorN, true);
        sun = enginetest::addDirectionalLight(s, Vec3(-0.25f, -1.0f, -1.05f), 3.0f);
        view->setPostFx(fx);
    }
    s->removeNode(lamp);
    s->setGlobalIllumination(gi);

    // ---- (e) the shadow ray ---------------------------------------------------
    std::printf("\n== (e) THE SHADOW RAY: a mover under a caster reflects dark in its shadow ==\n");
    show(false, false);
    // A MOVER WALL facing the mirror (its front face at z = -7.8), and a STATIC
    // overhang above its front that shades the wall's upper half under the sun.
    const NodeId moverWall = s->createNode(), overhang = s->createNode();
    s->setNodeMovable(moverWall, true);
    s->attachMesh(moverWall, cube, crateMat);
    s->setNodeTransform(moverWall, Vec3(0.0f, 1.0f, -8.0f), Quat(), Vec3(3.0f, 3.0f, 0.4f));
    s->attachMesh(overhang, cube, matte(s, Colour(0.2f, 0.2f, 0.2f)));
    s->setNodeTransform(overhang, Vec3(0.0f, 2.6f, -7.0f), Quat(), Vec3(3.4f, 0.1f, 1.8f));
    const ImageF mBase = shot(true), rBase = shot(false);
    s->setNodeVisible(moverWall, false);
    const ImageF mNoWall = shot(true), rNoWall = shot(false);
    s->setNodeVisible(moverWall, true);
    {
        const Mask refl = diffMask(mBase, mNoWall, false, 0.005f);
        const Mask rast = diffMask(rBase, rNoWall, true, 0.005f);
        Mask in;
        in.m.assign(refl.m.size(), 0u);
        for (size_t i = 0; i < refl.m.size(); ++i)
            if (refl.m[i] && rast.m[i]) { in.m[i] = 1u; ++in.n; }
        const Mask core = erode(in, kSize, kSize, 3);
        // THE TWO CLASSES by the RASTER's own luminance (the map's shadow): the
        // midpoint between the wall's darkest and brightest interior pixels.
        float lo = 1e9f, hi = 0.0f;
        for (unsigned y = 0; y < kSize; ++y)
            for (unsigned x = 0; x < kSize; ++x) {
                if (!core.m[size_t(y) * kSize + x]) continue;
                const Colour c = rBase.at(kSize - 1u - x, y);
                const float l = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                lo = std::min(lo, l);
                hi = std::max(hi, l);
            }
        Mask dark, lit;
        dark.m.assign(core.m.size(), 0u);
        lit.m.assign(core.m.size(), 0u);
        const float mid = 0.5f * (lo + hi);
        for (unsigned y = 0; y < kSize; ++y)
            for (unsigned x = 0; x < kSize; ++x) {
                const size_t i = size_t(y) * kSize + x;
                if (!core.m[i]) continue;
                const Colour c = rBase.at(kSize - 1u - x, y);
                const float l = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
                if (l < mid) { dark.m[i] = 1u; ++dark.n; }
                else { lit.m[i] = 1u; ++lit.n; }
            }
        const Mask darkCore = erode(dark, kSize, kSize, 2), litCore = erode(lit, kSize, kSize, 2);
        const Colour md = maskMean(mBase, darkCore, false), ml = maskMean(mBase, litCore, false);
        const Colour rd = maskMean(rBase, darkCore, true), rl = maskMean(rBase, litCore, true);
        const auto lum = [](const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; };
        std::printf("   the wall: %u interior px, shadow %u / lit %u (cores %u / %u); reflection shadow %.4f lit "
                    "%.4f (ratio %.3f), raster shadow %.4f lit %.4f (ratio %.3f)\n",
                    core.n, dark.n, lit.n, darkCore.n, litCore.n, lum(md), lum(ml), lum(md) / std::max(lum(ml), 1e-6f),
                    lum(rd), lum(rl), lum(rd) / std::max(lum(rl), 1e-6f));
        CHECK_MSG(darkCore.n > 30 && litCore.n > 30 && lum(md) < 0.5f * lum(ml),
                  "(e) the mover's reflection is DARK where the overhang's shadow falls (%.4f) and lit beside it "
                  "(%.4f): the sun's term at the hit is the shadow ray", lum(md), lum(ml));
        CHECK_MSG(std::fabs(lum(md) / std::max(lum(ml), 1e-6f) - lum(rd) / std::max(lum(rl), 1e-6f)) < 0.05f,
                  "(e) ...in the raster's proportion (shadow / lit %.3f against %.3f, bar 0.05)",
                  lum(md) / std::max(lum(ml), 1e-6f), lum(rd) / std::max(lum(rl), 1e-6f));
    }

    // ---- (g) a SAME-SLOT TEXTURE SWAP reaches the hits ----------------------
    // A decode twin is a CLONE of its PBS datablock (HlmsJson, textures resolved
    // by name). Swapping the albedo texture of a live material keeps the Hlms
    // hash (the property vector does not change), so a witness of the hash alone
    // kept the OLD texture in every hit until the twin died for another reason
    // (audit F6). The witness carries the datablock's texture set too.
    std::printf("\n== (g) a MOVER's albedo texture swapped on its live material: the reflection follows ==\n");
    s->setNodeVisible(moverWall, false);
    s->setNodeVisible(overhang, false);
    s->setNodeVisible(floorN, false);
    show(false, false);
    {
        GiParams off = gi;
        off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
    }
    const auto solid = [&](unsigned char r, unsigned char g, unsigned char b) {
        std::vector<unsigned char> px(4u * 4u * 4u);
        for (size_t i = 0; i < px.size(); i += 4u) { px[i] = r; px[i + 1] = g; px[i + 2] = b; px[i + 3] = 255; }
        return s->createTexture(4, 4, px.data(), true);
    };
    const TextureId texRed = solid(230, 40, 30), texGreen = solid(30, 220, 40);
    const MaterialId texMat = matte(s, Colour(1, 1, 1));
    s->setPbrTexture(texMat, PbrTextureSlot::Albedo, texRed);
    const NodeId texCrate = s->createNode();
    s->setNodeMovable(texCrate, true);
    s->attachMesh(texCrate, cube, texMat);
    enginetest::setNodePosition(s, texCrate, Vec3(0.3f, 0.0f, -8.0f));
    s->setNodeVisible(texCrate, false);
    {
        // The raster first, the mirror last: the swap below happens in the
        // mirror's pose with its history settled on the red crate.
        const ImageF rNo = shot(false), mNo = shot(true);
        s->setNodeVisible(texCrate, true);
        const ImageF rRed = shot(false), mRed = shot(true);
        const Mask refl = diffMask(mRed, mNo, false, 0.01f);
        const Mask rast = diffMask(rRed, rNo, true, 0.01f);
        Mask in;
        in.m.assign(refl.m.size(), 0u);
        for (size_t i = 0; i < refl.m.size(); ++i)
            if (refl.m[i] && rast.m[i]) { in.m[i] = 1u; ++in.n; }
        const Mask core = erode(in, kSize, kSize, 2);
        const Colour redRefl = maskMean(mRed, core, false), redRast = maskMean(rRed, core, true);
        std::printf("   RED: interior %u px: reflection (%.4f %.4f %.4f), raster (%.4f %.4f %.4f)\n", core.n,
                    redRefl.r, redRefl.g, redRefl.b, redRast.r, redRast.g, redRast.b);
        // THE SWAP, in the mirror's pose (the last shot's), then frame by frame.
        s->setPbrTexture(texMat, PbrTextureSlot::Albedo, texGreen);
        int firstGreen = -1;
        Colour after[8];
        for (int f = 0; f < 8; ++f) {
            render(e, 1);
            ImageF img;
            view->readPixelsHdr(img);
            after[f] = maskMean(img, core, false);
            if (firstGreen < 0 && after[f].g > after[f].r) firstGreen = f + 1;
            std::printf("   frame %d after the swap: reflection (%.4f %.4f %.4f)\n", f + 1, after[f].r, after[f].g,
                        after[f].b);
        }
        const ImageF mGreen = shot(true), rGreen = shot(false);
        const Colour greenRefl = maskMean(mGreen, core, false), greenRast = maskMean(rGreen, core, true);
        std::printf("   GREEN (settled): reflection (%.4f %.4f %.4f), raster (%.4f %.4f %.4f)\n", greenRefl.r,
                    greenRefl.g, greenRefl.b, greenRast.r, greenRast.g, greenRast.b);
        CHECK_MSG(core.n > 50 && redRefl.r > 2.0f * redRefl.g && greenRast.g > 2.0f * greenRast.r,
                  "(g) the fixture: the red crate reflects red (%u px) and the green one rasters green", core.n);
        CHECK_MSG(firstGreen == 1,
                  "(g) the FIRST frame after the swap reflects the new texture (green > red from frame %d; "
                  "before the witness carried the texture set: never, the twin kept the red one)", firstGreen);
        // Against the raster's BRIGHTEST channel: the green texture's red and blue
        // are ~0.006, where a 2e-4 difference is 4 % of the channel and nothing of
        // the colour.
        const float gPeak = std::max(greenRast.r, std::max(greenRast.g, greenRast.b));
        const float gDiff = std::max(std::fabs(greenRefl.r - greenRast.r),
                                     std::max(std::fabs(greenRefl.g - greenRast.g), std::fabs(greenRefl.b - greenRast.b)));
        CHECK_MSG(gDiff < 0.02f * gPeak,
                  "(g) ...and settles on the green raster's colour: (%.4f %.4f %.4f) against (%.4f %.4f %.4f), "
                  "worst channel %.2f %% of the brightest (bar 2 %%, arm (a)'s)", greenRefl.r, greenRefl.g,
                  greenRefl.b, greenRast.r, greenRast.g, greenRast.b, 100.0f * gDiff / std::max(gPeak, 1e-6f));
    }
    s->removeNode(texCrate);

    // ---- (h) a GLOSSY mover: the specular environment at a hit --------------
    // The eye vector at a hit is the reversed ray (HlmsAtom's hit-mode
    // custom_ps_preLights), and so must be EVERYTHING the lighting header derived
    // from the camera's pinhole: the reflection vector every specular environment
    // read takes (the probes' localCorrect, the PCC/VCT blend, the cubemap, the
    // VCT specular cone). Arms (a)-(e) are MATTE (F0 = 0) and could not see a
    // wrong one (audit F1). THE FIXTURE: a metal crate at roughness 0.1 BESIDE the
    // camera and behind it, in a sky whose colour turns once around the horizon
    // (R = 0.5 + 0.4 cos(az), G = 0.5 + 0.4 sin(az) over the equirect's u), GI off
    // (the mirror-fixture rule) — its reflection is the sky's colour in the
    // reflection vector's azimuth.
    //   THE ARITHMETIC: the face the mirror sees (+Z) at (1.8, 0.8, -3.7). The
    //   reversed ray points at the reflected camera (0, 1, 12.7): v = (-1.8, 0.2,
    //   16.4)/16.5, reflected about +Z -> azimuth 6.3 deg off +Z. The camera's
    //   pinhole is (0, 1, -3): v = (-1.8, 0.2, 0.7)/1.94 -> azimuth 68.7 deg. The
    //   sky's (R, G) turns by 62 deg between them: |d(R,G)| = 2 x 0.4 x sin(31 deg)
    //   = 0.41 of a ~0.9 channel, ~45 % — the pinhole's reflection vector fails
    //   the bar by twenty times (measured before the fix: 71 % in the worst channel,
    //   the crate's -X face turning further). The raster from the reflected camera (NO ray
    //   reflections: the crate's own environment is then the cube in both) reads
    //   the same cube texel at the same LOD (the prefiltered mip is picked by the
    //   roughness, not by derivatives), so what may differ is the reflection
    //   history's RGBA16F quantum (2^-11 relative for a mean) and the mirror's own
    //   composite, which arm (a) measures at 0.15-0.21 % (this arm: 0.18 %). The
    //   bar is arm (a)'s 2 %. (The reflection's difference mask also holds ~1300
    //   scattered pixels of the mirror's own sky, the ray reflection's frame-to-frame
    //   noise above the 0.01 threshold; the interior is the two masks' overlap.)
    std::printf("\n== (h) a GLOSSY mover (metal, roughness 0.1): its specular environment at the hit ==\n");
    {
        const unsigned kSkyW = 64u, kSkyH = 32u;
        std::vector<unsigned char> px(size_t(kSkyW) * kSkyH * 4u);
        for (unsigned y = 0; y < kSkyH; ++y)
            for (unsigned x = 0; x < kSkyW; ++x) {
                const float a = 6.2831853f * (float(x) + 0.5f) / float(kSkyW);
                unsigned char *p = &px[(size_t(y) * kSkyW + x) * 4u];
                p[0] = (unsigned char)std::lround(255.0f * (0.5f + 0.4f * std::cos(a)));
                p[1] = (unsigned char)std::lround(255.0f * (0.5f + 0.4f * std::sin(a)));
                p[2] = (unsigned char)77;
                p[3] = 255;
            }
        SkyDesc sky;
        sky.mode = SkyMode::Equirectangular;
        sky.equirect = s->createTexture(kSkyW, kSkyH, px.data(), false);
        CHECK(sky.equirect && s->setSky(sky), "(h) the turning sky binds");
    }
    PbrParams gp;
    gp.albedo = Colour(0.95f, 0.95f, 0.95f);
    gp.metalness = 1.0f;
    gp.roughness = 0.1f;
    const NodeId glossy = s->createNode();
    s->setNodeMovable(glossy, true);
    s->attachMesh(glossy, cube, s->createPbrMaterial(gp));
    enginetest::setNodePosition(s, glossy, Vec3(1.8f, 0.8f, -4.2f));
    s->setNodeVisible(glossy, false);
    {
        PostFxDesc noRays = fx;
        noRays.ssr = 0;
        const auto rasterShot = [&]() {
            view->setPostFx(noRays);
            const ImageF img = shot(false);
            view->setPostFx(fx);
            return img;
        };
        const ImageF mNo = shot(true), rNo = rasterShot();
        s->setNodeVisible(glossy, true);
        const ImageF mOn = shot(true), rOn = rasterShot();
        const Mask refl = diffMask(mOn, mNo, false, 0.01f);
        const Mask rast = diffMask(rOn, rNo, true, 0.01f);
        Mask in;
        in.m.assign(refl.m.size(), 0u);
        for (size_t i = 0; i < refl.m.size(); ++i)
            if (refl.m[i] && rast.m[i]) { in.m[i] = 1u; ++in.n; }
        const Mask core = erode(in, kSize, kSize, 2);
        const Colour cm = maskMean(mOn, core, false), cr = maskMean(rOn, core, true);
        if (std::getenv("JAH_HIT_DUMP")) {
            // Evidence: the reflection mask (red), the raster mask (green), both = yellow;
            // then the mirror shot and the flipped raster, side by side.
            FILE *f = std::fopen("hit-h.ppm", "wb");
            if (f) {
                std::fprintf(f, "P6\n%u %u\n255\n", kSize * 3u, kSize);
                for (unsigned y = 0; y < kSize; ++y)
                    for (unsigned x = 0; x < kSize * 3u; ++x) {
                        const unsigned xx = x % kSize;
                        const size_t i = size_t(y) * kSize + xx;
                        unsigned char px[3];
                        if (x < kSize) {
                            px[0] = refl.m[i] ? 255 : 0; px[1] = rast.m[i] ? 255 : 0; px[2] = core.m[i] ? 255 : 0;
                        } else {
                            const Colour c = x < 2u * kSize ? mOn.at(xx, y) : rOn.at(kSize - 1u - xx, y);
                            px[0] = (unsigned char)std::min(255.0f, c.r * 255.0f);
                            px[1] = (unsigned char)std::min(255.0f, c.g * 255.0f);
                            px[2] = (unsigned char)std::min(255.0f, c.b * 255.0f);
                        }
                        std::fwrite(px, 1, 3, f);
                    }
                std::fclose(f);
            }
        }
        std::printf("   reflection %u px, raster %u px, interior %u px: reflection (%.4f %.4f %.4f), raster (%.4f "
                    "%.4f %.4f)\n", refl.n, rast.n, core.n, cm.r, cm.g, cm.b, cr.r, cr.g, cr.b);
        CHECK_MSG(core.n > 50 && relDiff(cm, cr) < 0.02f,
                  "(h) the GLOSSY mover's reflection is its raster from the reflected camera: worst channel %.2f %% "
                  "(bar 2 %%; the camera's reflection vector would be ~45 %% off)", 100.0f * relDiff(cm, cr));
    }
    s->removeNode(glossy);
    e->destroyView(view);
    e->destroyScene(s);
    return 0;
}

// ---------------------------------------------------------------------------
// (b) THE GATHER, gi.gather_reference's instrument: a top-down orthographic view
// of a matte floor under a rectangular emitter that is a MOVER.
static const unsigned kGSize = 512u;
static const float kOrthoHalf = 8.0f;

static double projectedSolidAngle(const double p[3], const double n[3], const double verts[][3], int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        const int j = (i + 1) % count;
        double a[3], b[3];
        for (int k = 0; k < 3; ++k) { a[k] = verts[i][k] - p[k]; b[k] = verts[j][k] - p[k]; }
        const double la = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
        const double lb = std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
        if (la < 1e-9 || lb < 1e-9) continue;
        for (int k = 0; k < 3; ++k) { a[k] /= la; b[k] /= lb; }
        double c = std::max(-1.0, std::min(1.0, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]));
        const double theta = std::acos(c);
        double cr[3] = { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
        const double lc = std::sqrt(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
        if (lc < 1e-9) continue;
        for (int k = 0; k < 3; ++k) cr[k] /= lc;
        sum += theta * (n[0] * cr[0] + n[1] * cr[1] + n[2] * cr[2]);
    }
    return std::fabs(0.5 * sum);
}

static void worldToPixel(float wx, float wz, double &px, double &py)
{
    px = (double(wx) / kOrthoHalf * 0.5 + 0.5) * kGSize;
    py = (double(wz) / kOrthoHalf * 0.5 + 0.5) * kGSize;
}

static int gatherArm(Engine *e)
{
    std::printf("\n== (b) THE GATHER: a MOVER emitter over a matte floor, against the closed form ==\n");
    View *view = e->createOffscreenView("hitgather", kGSize, kGSize, Colour(0, 0, 0));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 0;
    fx.hdrReadback = true;
    view->setPostFx(fx);
    view->setShadows(true);
    Scene *s = e->createScene("hitgather");
    view->setScene(s);
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    CameraDesc c;
    c.position = Vec3(0.0f, 6.0f, 0.0f);
    c.orientation = Quat(-0.70710678f, 0.0f, 0.0f, 0.70710678f);
    c.orthographic = true;
    c.orthoSize = kOrthoHalf;
    c.farClip = 200.0f;
    view->setCamera(c);
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const float kAlbedo = 0.9f;
    const NodeId floorN = s->createNode();
    s->attachMesh(floorN, cube, matte(s, Colour(kAlbedo, kAlbedo, kAlbedo)));
    s->setNodeTransform(floorN, Vec3(0.0f, -0.25f, 0.0f), Quat(), Vec3(60.0f, 0.5f, 60.0f));
    // THE EMITTER, a MOVER: 2 x 2 m, 0.1 m thick, its bottom face at y = 1.5 —
    // red (0.9, 0.1, 0.1), black albedo (it bounces nothing back).
    const float kHalf = 1.0f, kBottom = 1.5f, kThick = 0.1f;
    const float kL[3] = { 0.9f, 0.1f, 0.1f };
    const NodeId panel = s->createNode();
    // `JAH_HIT_STATIC_PANEL=1` (a measurement switch, not a mode): the same panel
    // STATIC — the gather's own estimate of a voxelised emitter, for the A/B.
    const bool staticPanel = std::getenv("JAH_HIT_STATIC_PANEL") != nullptr;
    s->setNodeMovable(panel, !staticPanel);
    s->attachMesh(panel, cube, matte(s, Colour(0, 0, 0), Colour(kL[0], kL[1], kL[2])));
    s->setNodeTransform(panel, Vec3(0.0f, kBottom + 0.5f * kThick, 0.0f), Quat(),
                        Vec3(2.0f * kHalf, kThick, 2.0f * kHalf));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    gi.ddgi = GiToggle::Off;
    gi.gather = GiToggle::On;
    gi.numBounces = 1;
    gi.cascades = true;
    s->setGlobalIllumination(gi);
    GatherTuning t;
    t.restOff = true;   // the estimator's mean over frames, not one held draw
    s->setGatherTuning(t);
    render(e, 60);

    // THE ENVIRONMENT PATH'S CURRENCY, measured (gi.gather_reference's
    // calibration 3): a floor under a flat ambient A renders albedo x factor x A/pi.
    double envScale = 1.0;
    {
        GiParams off = gi;
        off.mode = GiMode::Off;
        off.gather = GiToggle::Off;   // an ON row gathers with the GI mode off too
        s->setGlobalIllumination(off);
        s->setAmbient(Colour(0.25f, 0.25f, 0.25f), Colour(0.25f, 0.25f, 0.25f));
        render(e, 20);
        ImageF img;
        view->readPixelsHdr(img);
        double px, py;
        worldToPixel(5.0f, 5.0f, px, py);
        double m = 0.0;
        int n = 0;
        for (int y = int(py) - 6; y <= int(py) + 6; ++y)
            for (int x = int(px) - 6; x <= int(px) + 6; ++x) { m += img.at(unsigned(x), unsigned(y)).r; ++n; }
        m /= n;
        envScale = m / (double(kAlbedo) * 0.25 / 3.14159265358979323846);
        std::printf("   the environment path: albedo x factor = %.4f (a pixel of the floor is that x E/pi)\n",
                    envScale * double(kAlbedo));
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
        s->setGlobalIllumination(gi);
        render(e, 60);
    }
    // THE POINTS: beside the panel's footprint (the camera sees the floor there),
    // where the panel's side and bottom are seen. The closed form over the probe
    // cell (16 px at High), every face that faces the point.
    struct Face { double v[4][3]; double n[3]; };
    const double b0 = kBottom, b1 = kBottom + kThick, h = kHalf;
    const Face faces[5] = {
        { { { -h, b0, -h }, { h, b0, -h }, { h, b0, h }, { -h, b0, h } }, { 0, -1, 0 } },
        { { { h, b0, -h }, { h, b1, -h }, { h, b1, h }, { h, b0, h } }, { 1, 0, 0 } },
        { { { -h, b0, -h }, { -h, b1, -h }, { -h, b1, h }, { -h, b0, h } }, { -1, 0, 0 } },
        { { { -h, b0, h }, { h, b0, h }, { h, b1, h }, { -h, b1, h } }, { 0, 0, 1 } },
        { { { -h, b0, -h }, { h, b0, -h }, { h, b1, -h }, { -h, b1, -h } }, { 0, 0, -1 } },
    };
    const double up[3] = { 0, 1, 0 };
    const double cellWorld = 16.0 * (2.0 * kOrthoHalf) / double(kGSize);
    const float xs[4] = { 1.4f, 1.8f, 2.2f, 2.6f };
    double analytic[4], omegaFrac[4];
    for (int i = 0; i < 4; ++i) {
        double acc = 0.0;
        int n = 0;
        for (int sy = 0; sy < 8; ++sy)
            for (int sx = 0; sx < 8; ++sx) {
                const double p[3] = { double(xs[i]) + ((sx + 0.5) / 8.0 - 0.5) * cellWorld, 0.0,
                                      ((sy + 0.5) / 8.0 - 0.5) * cellWorld };
                for (const Face &f : faces) {
                    double d = 0.0;
                    for (int k = 0; k < 3; ++k) d += f.n[k] * (p[k] - f.v[0][k]);
                    if (d > 0.0) acc += projectedSolidAngle(p, up, f.v, 4);
                }
                ++n;
            }
        omegaFrac[i] = acc / n / 3.14159265358979323846;   // the cosine-weighted fraction of the hemisphere
        analytic[i] = omegaFrac[i];                          // E / (pi L)
    }
    // THE MEASUREMENT: frames averaged in linear units (the estimate moves per frame
    // by construction — the probe's jitter and its rays are keyed on the frame).
    const int kFrames = 96;
    double sum[4] = { 0, 0, 0, 0 };
    for (int f = 0; f < kFrames; ++f) {
        render(e, 1);
        ImageF img;
        view->readPixelsHdr(img);
        for (int i = 0; i < 4; ++i) {
            double px, py;
            worldToPixel(xs[i], 0.0f, px, py);
            double m = 0.0;
            int n = 0;
            for (int y = int(py) - 5; y <= int(py) + 5; ++y)
                for (int x = int(px) - 5; x <= int(px) + 5; ++x) { m += img.at(unsigned(x), unsigned(y)).r; ++n; }
            sum[i] += m / n;
        }
    }
    // eps FROM THE PROBE COUNT: a probe traces 64 stratified rays, of which a
    // fraction p (the cosine-weighted fraction the panel covers) hit it; one
    // frame's estimate has a relative standard deviation sqrt((1 - p) / (64 p)),
    // the mean of F frames sqrt(F) less, and the bar is three of them.
    bool allOk = true;
    double worstRatio = 1e9;
    for (int i = 0; i < 4; ++i) {
        const double measured = sum[i] / kFrames / (envScale * double(kAlbedo));   // E/pi, red
        const double closed = analytic[i] * double(kL[0]);
        const double p = omegaFrac[i];
        const double eps = 3.0 * std::sqrt((1.0 - p) / (64.0 * p) / double(kFrames));
        const double ratio = measured / std::max(closed, 1e-9);
        worstRatio = std::min(worstRatio, ratio);
        std::printf("   x = %.1f m: the floor's red E/pi %.5f against the closed form %.5f (ratio %.3f); the panel "
                    "covers %.3f of the cosine hemisphere, eps %.3f\n", xs[i], measured, closed, ratio, p, eps);
        if (!(ratio >= 1.0 - eps)) allOk = false;
    }
    CHECK_MSG(allOk, "(b) the floor's bounce off a MOVER emitter is at least the closed form x (1 - eps) at every "
                     "point (worst ratio %.3f) — the gather's hit on a mover is shaded by the decode (before this "
                     "lane: BLACK)", worstRatio);
    const RayQueryStatus st = s->rayQueryStatus();
    std::printf("   records: %llu appended, %llu dropped (capacity %llu)\n", st.hitRecords, st.hitDropped,
                st.hitCapacity);
    e->destroyView(view);
    e->destroyScene(s);
    return 0;
}

// ---------------------------------------------------------------------------
// `--cost`: THE HIT DECODE AT 1920x1080, paired arms in ONE process — High
// (ssr 1: the half-resolution trace) and Epic (ssr 2 and the Epic gather row),
// with 0 / 1 / 30 MOVERS in front of a glossy floor (a mover's hits always
// decode; a static crate's never do). Reported: the frame's passes' GPU ms, the
// decode pass's, the write-back's, and the records each arm appended.
static int costMain(Engine *e)
{
    View *view = e->createOffscreenView("hitcost", 1920, 1080, Colour(0.45f, 0.55f, 0.70f));
    view->setOffscreenContract(OffscreenContract::StillPicture);
    Scene *s = e->createScene("hitcost");
    if (!view || !s || !view->setScene(s)) { std::printf("FAIL: view/scene\n"); return 1; }
    view->setShadows(true);
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const NodeId floorN = s->createNode();
    {
        PbrParams p;
        p.albedo = Colour(0.5f, 0.5f, 0.5f);
        p.metalness = 1.0f;
        p.roughness = 0.1f;
        s->attachMesh(floorN, cube, s->createPbrMaterial(p));
    }
    s->setNodeTransform(floorN, Vec3(0.0f, -0.5f, 0.0f), Quat(), Vec3(40.0f, 1.0f, 40.0f));
    enginetest::addDirectionalLight(s, Vec3(-0.4f, -1.0f, 0.3f), 3.0f);
    std::vector<NodeId> movers;
    for (int i = 0; i < 30; ++i) {
        const NodeId n = s->createNode();
        s->setNodeMovable(n, true);
        s->attachMesh(n, cube, matte(s, Colour(0.2f + 0.02f * float(i), 0.5f, 0.8f - 0.02f * float(i))));
        enginetest::setNodePosition(s, n, Vec3(-7.0f + float(i % 10) * 1.6f, 0.5f, -2.0f - float(i / 10) * 2.5f));
        movers.push_back(n);
    }
    enginetest::testCameraLookAt(view, Vec3(0.0f, 3.0f, 9.0f), Vec3(0.0f, 0.3f, -3.0f));
    e->setFrameMonitor(MonitorLevel::Review);
    struct Arm { const char *name; bool epic; int movers; };
    const Arm arms[] = { { "High, 0 movers", false, 0 },  { "High, 1 mover", false, 1 },
                         { "High, 30 movers", false, 30 }, { "Epic, 0 movers", true, 0 },
                         { "Epic, 1 mover", true, 1 },    { "Epic, 30 movers", true, 30 } };
    const int kArms = int(sizeof(arms) / sizeof(arms[0]));
    std::vector<double> frameSum(kArms, 0.0), decodeSum(kArms, 0.0), compSum(kArms, 0.0);
    std::vector<int> frameN(kArms, 0), decodeN(kArms, 0), compN(kArms, 0);
    std::vector<unsigned long long> records(kArms, 0ull);
    for (int round = 0; round < 4; ++round) {
        for (int a = 0; a < kArms; ++a) {
            GiParams gi;
            gi.mode = GiMode::Vct;
            gi.quality = GiQuality::High;
            gi.epicTier = arms[a].epic;
            gi.numBounces = 1;
            s->setGlobalIllumination(gi);
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = arms[a].epic ? 2 : 1;
            view->setPostFx(fx);
            for (int i = 0; i < 30; ++i) s->setNodeVisible(movers[size_t(i)], i < arms[a].movers);
            render(e, 40);
            std::vector<FrameRecord> drop;
            e->takeFrameRecords(drop);
            render(e, 40);
            std::vector<FrameRecord> recs;
            for (int k = 0; k < 8; ++k) { render(e, 1); e->takeFrameRecords(recs); }
            for (const FrameRecord &r : recs) {
                if (r.gpuMs > 0.0f) { frameSum[a] += r.gpuMs; ++frameN[a]; }
                for (const FramePass &p : r.passes)
                    if (p.pass == "Jahshaka hit decode" && p.gpuMs >= 0.0f) { decodeSum[a] += p.gpuMs; ++decodeN[a]; }
                for (const CacheWork &w : r.cacheWork)
                    if (w.detail == "hit.composite" && w.gpuMs >= 0.0f) { compSum[a] += w.gpuMs; ++compN[a]; }
            }
            records[size_t(a)] = s->rayQueryStatus().hitRecords;
        }
    }
    e->setFrameMonitor(MonitorLevel::Off);
    std::printf("    arm                 frame GPU ms   decode GPU ms   write-back GPU ms   records   (frames)\n");
    for (int a = 0; a < kArms; ++a)
        std::printf("    %-18s %12.4f   %13.4f   %17.4f   %7llu   (%d / %d / %d)\n", arms[a].name,
                    frameN[a] ? frameSum[a] / frameN[a] : -1.0, decodeN[a] ? decodeSum[a] / decodeN[a] : -1.0,
                    compN[a] ? compSum[a] / compN[a] : -1.0, records[size_t(a)], frameN[a], decodeN[a], compN[a]);
    return 0;
}

int main(int argc, char **argv)
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    const bool cost = argc > 1 && std::strcmp(argv[1], "--cost") == 0;
    cfg.logFile = cost ? "test-gi-hit-shade-cost-ogre.log" : "test-gi-hit-shade-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    {
        // The device exists with the first view (the startup-order law).
        View *probe = e->createOffscreenView("hitprobe", 16, 16, Colour(0, 0, 0));
        const bool rays = e->rayQueryAvailable() && e->rayTracing();
        e->destroyView(probe);
        if (!rays) {
            std::printf("ok: no ray queries on this machine — gi.hit_shade skips\n");
            return 0;
        }
    }
    if (cost) return costMain(e);
    mirrorArms(e);
    gatherArm(e);
    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
