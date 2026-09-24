// gi.ddgi_ambient — THE SKY INSIDE THE IRRADIANCE FIELD, end to end.
//
// SINCE PHOTON-ENV-1 THE FIELD CARRIES THE SKY ITSELF: every probe ray that
// escapes the voxels reads the one environment in its direction, and the
// cosine integration puts it in the irradiance atlas beside the bounce. The
// read-time "ambient x sky visibility" term this suite was written for, and its
// dial (ddgiAmbient, whose 0 was "DDGI before the fix"), are deleted, so the
// A/Bs below compare the field against the cone reference and the ANALYTIC sky
// visibility, and the sealed-room invariance is stated as "the sky on against
// the sky off" (setAmbient black), which is the physics the dial stood in for.
// The history below is kept because it is why each case exists.
//
// WHAT WAS WRONG, in one paragraph, because the mechanism is three upstream
// facts stacked on each other (SPECS/OGRE_UPSTREAM_ISSUES.md; GI_UNIFIED_SPEC.md
// ADDENDUM CORRECTION):
//
//   1. Every PBS ambient term is wrapped in `if( vctSpecular.w == 0 )` —
//      "only use ambient lighting if the object is outside any VCT probe"
//      (AmbientLighting_piece_ps.any:63,84,123);
//   2. the volume test that would set `vctSpecular.w` to zero is COMMENTED OUT
//      upstream (`float blend = 1.0f;` in both branches,
//      Vct_piece_ps.any:325-331), so the gate never fires and PBS ambient is
//      dead scene-wide in any VCT scene;
//   3. the only live ambient was therefore the cone-traced diffuse's own
//      `light.xyz += ambient * light.w` — ambient times the cones' ESCAPE
//      FRACTION, i.e. ambient weighted by visible sky — and binding an
//      irradiance field sets `VctDisableDiffuse`, which deletes that branch.
//
// So with DDGI on, ambient light inside the lit volume came from NOWHERE. It
// reads as flatness, not as breakage: sky-facing mid-ground loses 15-25%, and a
// sealed room — which has no sky to see — loses nothing at all.
//
// THE FIX, and what this suite exists to prove about it: the field's DEPTH
// atlas already stores, per probe and per octahedral direction, how far the
// generation rays travelled — as a cosine-lobe MEAN and MEAN-SQUARE over the
// hemisphere (upstream's Integration/Depth job convolves it; the first build
// of this fix believed a texel was one ray, and its single tap over-brightened
// a wall foot by +17 points for exactly that reason). Rays that hit nothing
// left the march at its limit, so the escape distance is known per direction,
// and a Chebyshev test at that distance (the DDGI paper's own visibility test
// shape) turns the moment pair into an escape fraction per cage probe, and
// ambient times the cage-weighted fraction is the term the cone diffuse used
// to add (media/Hlms/Jahshaka/JahIfd_piece_ps.any). No Ogre patch.
//
// THE TWO ASSERTIONS THAT MATTER, and they are a PAIR — either alone would pass
// for a wrong fix:
//   * RECOVERY (case 1): on an open scene, DDGI + the fix lands within 10% of
//     the ambient reading VCT gave before DDGI replaced it. A fix that simply
//     ungated PBS ambient (the option this lane did NOT take) would also pass
//     this.
//   * SEALED-ROOM INVARIANCE (case 5): inside a closed room, every pixel moves
//     by at most 2/255 (the voxel reader's measured residual, named at the check)
//     between the sky off and the sky on. This is the one that
//     separates a visibility-weighted term from a blanket ambient — ungating
//     PBS ambient would flood a sealed room with light that has no way in.
//
// Plus: the bounce must survive the fix (case 3), and THE CORNER (case 4) —
// a floor patch at the foot of a wall must be darkened as the fixture's ANALYTIC
// says (the sky's share alone: measured with no bounce - skyIrradiance), within a bar
// derived from the voxel envelope and the field's converged error. It used to be
// compared with the cone reference, which over-reads a corner (+11.7 points): the
// cone's corner is the target row gi.cone_corner_target (PHOTON-FIELD-ROTATE-1).
//
// Its own binary, like every GI suite here. Determinism discipline is gi.ddgi's: fixed frame delta, no
// wall clock, and a rebuild-determinism control before any A/B is believed.
// PHOTON-GATHER-1d: THE GATHER PINNED OFF. Since 1d the screen-probe gather is
// the diffuse at every ray tier (GiToggle::Auto resolves on at Medium and above);
// this suite measures the voxel chain / the field / the cones / the probes, which
// it pins, so its numbers stay about them. The gather has its own suites
// (gi.gather_*).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "../support/voxel_lab.h"
#include "../support/voxeldump.h"

#include <algorithm>
#include <cmath>
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

static void render(Engine *e, int frames = 4)
{
    for (int i = 0; i < frames; ++i) e->renderOneFrame();
}

static float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

static void show(const char *what, const Colour &c)
{
    std::printf("   %-34s r=%.4f g=%.4f b=%.4f  (lum %.4f)\n", what, c.r, c.g, c.b, lum(c));
}

/// The largest per-channel difference between two frames, in 8-bit steps —
/// the unit the sealed-room invariance assertion is stated in.
static float maxChannelDelta(const Image &a, const Image &b, unsigned *outX = nullptr,
                             unsigned *outY = nullptr)
{
    float worst = 0.0f;
    if (a.width != b.width || a.height != b.height) return 1e9f;
    for (unsigned y = 0; y < a.height; ++y) {
        for (unsigned x = 0; x < a.width; ++x) {
            const Colour ca = a.at(x, y), cb = b.at(x, y);
            const float d = std::max(std::max(std::fabs(ca.r - cb.r), std::fabs(ca.g - cb.g)),
                                     std::fabs(ca.b - cb.b));
            if (d > worst) { worst = d; if (outX) *outX = x; if (outY) *outY = y; }
        }
    }
    return worst;
}

/// One wall/floor slab, the shape every scene here is built out of.
static NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId n = enginetest::addTestCube(s, albedo, 0.0f, 0.9f);
    enginetest::setNodePosition(s, n, pos);
    enginetest::setNodeScale(s, n, scale);
    return n;
}

// The ambient every scene here uses: a genuine hemisphere pair (upper != lower)
// in RADIANCE units, which is the non-degenerate case both the SH path and the
// VCT path are written for (the flat case takes Scene::setAmbient's 1/pi
// branch, which would compare two different conventions instead of two
// techniques).
static const Colour kAmbientUpper(0.40f, 0.40f, 0.44f);
static const Colour kAmbientLower(0.10f, 0.10f, 0.12f);

/// The VCT parameters every case starts from — gi.ddgi's, so the numbers in
/// this file and the ones in that one are about comparable pictures.
static GiParams vctBase()
{
    GiParams gi;
    gi.gather = GiToggle::Off;   // PHOTON-GATHER-1d (the header)
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::Medium;      // 64^3 voxels
    gi.numBounces = 2;
    gi.testBoundsMin = Vec3(-9.0f, -1.5f, -9.0f);
    gi.testBoundsMax = Vec3(9.0f, 7.5f, 9.0f);
    return gi;
}


// ---------------------------------------------------------------------------
// THE OPEN SCENE, shared by cases 1 and 2: a big floor with one wall standing
// on it, an ambient hemisphere pair, and NO LIGHTS AT ALL. With nothing but
// ambient in the scene, every number below is a measurement of the ambient term
// rather than a comparison of two whole lighting solutions (gi.ambient's
// premise), and with no light injected the field's own irradiance is zero — so
// whatever the fix adds is unambiguously the recovered ambient.
//
// The wall earns its place twice: it darkens its own corner in the cone-traced
// reference (the cones see it), which is the ONLY thing case 1's corner
// measurement can be compared against.
struct OpenScene {
    View  *view = nullptr;
    Scene *scene = nullptr;
};
static const unsigned kOpenX = 64, kOpenY = 118;      // floor, well clear of the wall
static const unsigned kCornerX = 64, kCornerY = 74;   // floor, at the wall's foot
static const Vec3 kCamPos(0.0f, 3.0f, 7.0f), kCamTarget(0.0f, 0.0f, -1.0f);

/// THE ANALYTIC CORNER (PHOTON-FIELD-ROTATE-1, F2): the irradiance an upward floor
/// point receives from the hemisphere ambient with the wall in the way, as a
/// fraction of the same point's with nothing in the way... computed for the two
/// pixels and returned as corner / open. Cosine-weighted directions over the upper
/// hemisphere, the ambient's radiance lerp( lower, upper, 0.5 + 0.5 y ) in
/// luminance, and a ray that meets the wall reads NOTHING - the SKY'S share alone. The
/// engine does put the sky into the voxels (the bounce injection's escaping cones read it,
/// PHOTON-ENV-1), so the wall reflects the sky back onto the floor; case 1/4 therefore
/// measures with NO bounce (numBounces 0), where this analytic is the whole physics
/// (PHOTON-VOXEL-4: at two bounces the field's corner read 91.1 % against the sky-only
/// quadrature's 81.5 - the wall's bounce; at none, 83.6). `grow` inflates the wall by that
/// many metres on every face (the voxel envelope's one cell).
static double skyIrradiance(const Vec3 &p, float grow)
{
    const enginetest::AnalyticBox wall{ Vec3(-6.0f - grow, 0.0f - grow, -2.2f - grow),
                                        Vec3(6.0f + grow, 4.0f + grow, -1.8f + grow) };
    const int N = 360;
    double sum = 0.0;
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            const double u = (i + 0.5) / N, v = (j + 0.5) / N;
            const double r = std::sqrt(u), phi = 2.0 * M_PI * v;
            const Vec3 d(float(r * std::cos(phi)), float(std::sqrt(1.0 - u)), float(r * std::sin(phi)));
            if (enginetest::rayHitsBox(p, d, wall)) continue;
            const double t = 0.5 + 0.5 * d.y;
            sum += (1.0 - t) * lum(kAmbientLower) + t * lum(kAmbientUpper);
        }
    return sum / double(N * N);
}

static double analyticCorner(float grow)
{
    const Vec3 corner = enginetest::groundPointForPixel(kCamPos, kCamTarget, kCornerX, kCornerY, 128);
    const Vec3 open = enginetest::groundPointForPixel(kCamPos, kCamTarget, kOpenX, kOpenY, 128);
    return skyIrradiance(corner, grow) / skyIrradiance(open, grow);
}

/// THE FIELD'S OWN QUADRATURE OF THE ANALYTIC CORNER (PHOTON-VOXEL-3 round 9). The field
/// does not integrate the sky AT the floor point: it integrates it at its PROBES and the
/// pixel blends the eight around it. So the corner the field can show is the analytic
/// irradiance at each probe of the corner's cage (and the open floor's), blended by the
/// pixel's own weights - the placement (OgreScene::ifdProbeCounts' fit over the volume,
/// upstream's one-block border: origin - S/N, spacing (S + 2 S/N) / N), the sample bias
/// (JahIfd: 0.05 spacings along the normal, 0.20 towards the eye), a probe behind the
/// tangent plane silenced (saturate(200 cos)), the crush below 0.2 and the trilinear
/// weights - with the Chebyshev visibility left out (the analytic probes see the true
/// geometry; nothing in this cage is behind the wall). `grow` as analyticCorner's.
static double fieldCornerQuadrature(const GiStatus &st, float grow)
{
    const double S[3] = { st.ifdMax.x - st.ifdMin.x, st.ifdMax.y - st.ifdMin.y, st.ifdMax.z - st.ifdMin.z };
    unsigned n[3] = { 2u, 2u, 2u };
    { static const int order[3] = { 0, 2, 1 };
      for (unsigned spent = 3u; spent < 13u; ++spent) {
          int best = -1; double worst = -1.0;
          for (int i = 0; i < 3; ++i) { const int ax = order[i]; if (n[ax] >= 128u) continue;
              const double sp = std::max(std::fabs(S[ax]), 1e-4) / n[ax]; if (sp > worst) { worst = sp; best = ax; } }
          if (best < 0) break;
          n[best] *= 2u;
      } }
    double O[3], B[3];
    const double mn[3] = { st.ifdMin.x, st.ifdMin.y, st.ifdMin.z };
    for (int a = 0; a < 3; ++a) { const double b0 = S[a] / n[a]; O[a] = mn[a] - b0; B[a] = (S[a] + 2.0 * b0) / n[a]; }
    const auto field = [&](const Vec3 &pt) {
        const double P[3] = { pt.x, pt.y, pt.z }, cam[3] = { kCamPos.x, kCamPos.y, kCamPos.z };
        double v[3], vl = 0.0;
        for (int a = 0; a < 3; ++a) { v[a] = cam[a] - P[a]; vl += v[a] * v[a]; }
        vl = std::sqrt(vl);
        double g[3];
        for (int a = 0; a < 3; ++a) g[a] = (P[a] - O[a]) / B[a] + (a == 1 ? 0.05 : 0.0) + 0.20 * (v[a] / vl) / B[a];
        double sum = 0.0, sumW = 0.0;
        for (int i = 0; i < 8; ++i) {
            const int off[3] = { i & 1, (i >> 1) & 1, (i >> 2) & 1 };
            double po[3], dir[3], r = 0.0, tri = 1.0;
            for (int a = 0; a < 3; ++a) {
                po[a] = std::trunc(g[a]) + off[a]; dir[a] = po[a] - g[a]; r += dir[a] * dir[a];
                const double f = std::min(std::max(g[a] - std::trunc(g[a]), 0.0), 1.0);
                tri *= off[a] ? f : 1.0 - f;
            }
            r = std::max(std::sqrt(r), 1e-6);
            double w = std::min(std::max(dir[1] / r * 200.0, 0.0), 1.0);   // the grid normal is +y
            w = std::max(w, 1e-6);
            if (w < 0.2) w *= w * w / 0.04;
            w *= tri;
            const Vec3 probe(float(O[0] + po[0] * B[0]), float(O[1] + po[1] * B[1]), float(O[2] + po[2] * B[2]));
            sum += w * skyIrradiance(probe, grow);
            sumW += w;
        }
        return sum / std::max(sumW, 1e-12);
    };
    const Vec3 corner = enginetest::groundPointForPixel(kCamPos, kCamTarget, kCornerX, kCornerY, 128);
    const Vec3 open = enginetest::groundPointForPixel(kCamPos, kCamTarget, kOpenX, kOpenY, 128);
    return field(corner) / field(open);
}

static OpenScene buildOpenScene(Engine *e, const char *name)
{
    OpenScene o;
    o.view = e->createOffscreenView(name, 128, 128, Colour(0, 0, 0));
    o.scene = e->createScene(name);
    o.view->setScene(o.scene);
    o.scene->setAmbient(kAmbientUpper, kAmbientLower);
    addSlab(o.scene, Colour(0.9f, 0.9f, 0.9f), Vec3(0.0f, -0.05f, 0.0f), Vec3(16.0f, 0.1f, 16.0f));
    addSlab(o.scene, Colour(0.9f, 0.9f, 0.9f), Vec3(0.0f, 2.0f, -2.0f), Vec3(12.0f, 4.0f, 0.4f));
    enginetest::testCameraLookAt(o.view, kCamPos, kCamTarget);
    return o;
}

// ---------------------------------------------------------------------------
// THE CONE SET ON THE TWO REFERENCES (PHOTON-VOXEL-4).
//
// The shared four-cone set (jah_voxel_cones.glsl: axes at 45 degrees about the floor's normal,
// tan of the half angle 0.98269, 0.25 each) marched from the floor by the engine's own reader
// (Engine::voxelReaderParity's compute arm, bit-identical to the pixel's:
// engine.voxel_reader_parity), each cone's escape the pixel's 1 - min(1, alpha / 0.95) - and on
// the CPU by the voxel lab (tests/support/voxel_lab.h), the same march with the same three rules
// (jahConeBelow, jahVoxelFirstPlane, jahVoxelKernelLevel):
//   (a) THE READ IS THE RULE: the lab over the split store read back from the GPU; every cone's
//       alpha within 0.01 (the lab matches the GPU to about 0.004 - the 10-bit store's quantum
//       and the 8-bit filter weights - with room).
//   BAR 1 = the read against THE CONE-TRACE REFERENCE (voxlab::refAlpha: each cone's rays
//       through the true boxes, the occluders entered counted) - THE STORE'S ERROR. Its bar is
//       derived here, not chosen: the lab's analytic store of this fixture (voxlab::synthStore,
//       the boxes voxelised exactly at the GPU store's own lattice) read by the same rules gives
//       the reader's own residual on a perfect store, and the GPU may add the store's quantum
//       (0.01) to it. At the wall foot that residual is -0.066 at 64 x 32 x 64: THE NEAR-FIELD
//       RESIDUAL OF THIS SET AND READER - the wall inside the wide cones' first two planes,
//       read through a kernel wider than the stretch the wall occupies (the same geometry as
//       the sealed room's A/C class). The owed refinement is VOXEL-5 (a lateral-only mip
//       family per axis); a wider set is not it (gi.voxel_lab's split arm: six -0.022,
//       Fibonacci-12 +0.006 there, at 3x the cones).
//   BAR 2 = the reference against the truth (the cosine-weighted sky visibility) - THE SET'S
//       ERROR, a property of four wide cones on a hemisphere with a wall in part of one or two
//       of them. Printed, and fenced at 6 points (a changed set or weight moves it).
static const double kConeTan = 0.98269;
static const double kConeAxes[4][3] = { { 0.707107, 0.707107, 0 }, { 0, 0.707107, 0.707107 },
                                        { -0.707107, 0.707107, 0 }, { 0, 0.707107, -0.707107 } };
static const enginetest::AnalyticBox kWall{ Vec3(-6.0f, 0.0f, -2.2f), Vec3(6.0f, 4.0f, -1.8f) };
static const std::vector<voxlab::Box> kLabWall = { { { -6.0, 0.0, -2.2 }, { 6.0, 4.0, -1.8 } } };
static const std::vector<voxlab::Box> kLabScene = { { { -8.0, -0.1, -8.0 }, { 8.0, 0.0, 8.0 } }, kLabWall[0] };

/// The four-cone set marched from the floor point `p` through the scene's bound store by
/// the engine's reader: each cone's alpha, and the set's kept share as the pixel weights it.
static bool measureConeSet(Engine *e, Scene *s, const Vec3 &p, double alpha[4], double &keep)
{
    GiVoxelVolume v;
    if (!s->giVoxelVolume(0, v) || !v.available) return false;
    const double size[3] = { double(v.cell[0]) * v.width, double(v.cell[1]) * v.height, double(v.cell[2]) * v.depth };
    const double pw[3] = { p.x, p.y, p.z };
    std::vector<VoxelReaderCone> cones;
    for (int k = 0; k < 4; ++k) {
        VoxelReaderCone c;
        double ls[3], d[3], l = 0.0;
        for (int a = 0; a < 3; ++a) {
            ls[a] = (pw[a] - v.origin[a]) / size[a];
            d[a] = kConeAxes[k][a] / size[a];     // the pixel's jahCubeToVctProbeSpaceDir
            l += d[a] * d[a];
        }
        l = std::sqrt(l);
        // the pixel's start: jahConeStart, one cell of cascade 0 along the normal
        c.posLS = Vec3(float(ls[0]), float(ls[1] + 1.0 / v.height), float(ls[2]));
        c.dirLS = Vec3(float(d[0] / l), float(d[1] / l), float(d[2] / l));
        c.biasDirLS = Vec3(0.0f, 1.0f, 0.0f);
        c.tanHalfAngle = float(kConeTan);
        c.flags = 0u;
        cones.push_back(c);
    }
    std::vector<VoxelReaderAnswer> frag, comp;
    if (!e->voxelReaderParity(s, cones, frag, comp) || comp.size() != 4) return false;
    keep = 0.0;
    for (int k = 0; k < 4; ++k) {
        alpha[k] = comp[size_t(k)].march[3];
        keep += 0.25 * voxlab::keepOf(alpha[k]);
    }
    return true;
}

/// The scene's cascade 0 read back into the lab (the split store, its chain as the GPU's
/// autogen builds it), and the analytic store of the fixture on the same lattice.
static bool labStores(Scene *s, bool directional, std::vector<voxlab::Cascade> &gpu, std::vector<voxlab::Cascade> &synth)
{
    GiVoxelVolume v;
    if (!s->giVoxelVolume(0, v) || !v.available) return false;
    const int dims[3] = { v.width, v.height, v.depth };
    const float *cov[2] = { v.coverageP.data(), v.coverageN.data() }, *pos[2] = { v.positionP.data(), v.positionN.data() };
    gpu.assign(1, voxlab::Cascade());
    voxlab::fromSplit(gpu[0], dims, v.origin, v.cell, cov, pos);
    const double org[3] = { v.origin[0], v.origin[1], v.origin[2] };
    const double size[3] = { double(v.cell[0]) * v.width, double(v.cell[1]) * v.height, double(v.cell[2]) * v.depth };
    synth = { voxlab::synthStore(kLabScene, false, 0.0, org, size, dims) };
    if (directional) { voxlab::buildDir(gpu[0]); voxlab::buildDir(synth[0]); }
    return true;
}

/// Case 1's and case 2's gate: the set at the open floor and at the wall foot - (a) the read is
/// the rule, BAR 1 within the lab's own residual on the analytic store plus the quantum, BAR 2
/// printed and fenced.
static void checkConeSet(Engine *e, Scene *s, const char *tier, bool directional)
{
    const struct { const char *name; unsigned x, y; } pts[2] = { { "open floor", kOpenX, kOpenY },
                                                                 { "wall foot", kCornerX, kCornerY } };
    std::vector<voxlab::Cascade> gpu, synth;
    const bool haveStore = labStores(s, directional, gpu, synth);
    CHECK(haveStore, "the split store reads back into the lab");
    if (!haveStore) return;
    std::printf("   %s store %d x %d x %d, cell %.4f m\n", tier, gpu[0].R[0], gpu[0].R[1], gpu[0].R[2], gpu[0].cell[0]);
    for (const auto &pt : pts) {
        const Vec3 g = enginetest::groundPointForPixel(kCamPos, kCamTarget, pt.x, pt.y, 128);
        const Vec3 p(g.x, 1e-4f, g.z);
        const double pw[3] = { p.x, p.y, p.z };
        double alpha[4] = {}, keep = 0.0;
        const bool ok = measureConeSet(e, s, p, alpha, keep);
        double worst = 0.0, keepRef = 0.0, keepSynth = 0.0;
        double lab[4] = {}, ref[4] = {};
        for (int k = 0; k < 4; ++k) {
            lab[k] = voxlab::walkN(gpu, directional, pw, 1, 1.0, kConeAxes[k], kConeTan);
            ref[k] = voxlab::refAlpha(pw, kConeAxes[k], kConeTan, kLabWall);
            keepRef += 0.25 * voxlab::keepOf(ref[k]);
            keepSynth += 0.25 * voxlab::keepOf(voxlab::walkN(synth, directional, pw, 1, 1.0, kConeAxes[k], kConeTan));
            worst = std::max(worst, std::fabs(lab[k] - alpha[k]));
        }
        const double truth = voxlab::truthVis(pw, kLabWall);
        const double bar1 = keep - keepRef, bar1Synth = keepSynth - keepRef, bar2 = keepRef - truth;
        std::printf("   %s CONES, %s (%.2f, 0, %.2f): the read's alphas %.3f %.3f %.3f %.3f | the lab's %.3f %.3f "
                    "%.3f %.3f | the reference's %.3f %.3f %.3f %.3f\n", tier, pt.name, p.x, p.z, alpha[0], alpha[1],
                    alpha[2], alpha[3], lab[0], lab[1], lab[2], lab[3], ref[0], ref[1], ref[2], ref[3]);
        std::printf("      keeps: the read %.4f, the lab on the analytic store %.4f, the reference %.4f, the truth %.4f\n"
                    "      BAR 1 (read - reference) %+.4f [the analytic store's %+.4f]; BAR 2 (reference - truth) %+.4f\n",
                    keep, keepSynth, keepRef, truth, bar1, bar1Synth, bar2);
        char msg[320];
        std::snprintf(msg, sizeof msg, "(a) %s cones at the %s read what the lab's rule says the store holds "
                      "(every cone's alpha within 0.01; worst %.4f)", tier, pt.name, worst);
        CHECK(ok && worst <= 0.01, msg);
        const double tol = std::fabs(bar1Synth) + 0.01;
        std::snprintf(msg, sizeof msg, "BAR 1: %s cones at the %s keep the cone-trace reference within the reader's "
                      "residual on the analytic store plus the quantum (%+.4f; bar +-%.4f)", tier, pt.name, bar1, tol);
        CHECK(ok && std::fabs(bar1) <= tol, msg);
        std::snprintf(msg, sizeof msg, "BAR 2: the four-cone set's reference at the %s is within 6 points of the "
                      "truth (a cone-set property; %+.4f)", pt.name, bar2);
        CHECK(std::fabs(bar2) <= 0.06, msg);
    }
}

/// THE FIELD'S RAYS AT A POINT, AGAINST THE ANALYTIC WALL (PHOTON-VOXEL-4, a measurement):
/// the field's own ray (aperture 0, free space, the volume-space direction, the one-cell start
/// bias along it) marched from `pw` over a cosine-uniform grid of the upper hemisphere, and
/// its escape 1 - min(1, alpha / 0.95) set against the analytic ray's hit of the wall box.
/// Returns the cosine-weighted sky the voxels let through where the wall stands (the leak),
/// and the cosine-weighted sky they stop where nothing stands (the over-occlusion), both as
/// fractions of the hemisphere's irradiance.
static void fieldRayLedger(Engine *e, Scene *s, const Vec3 &pw, double &leak, double &stop)
{
    leak = stop = 0.0;
    GiVoxelVolume v;
    if (!s->giVoxelVolume(0, v) || !v.available) return;
    const double size[3] = { double(v.cell[0]) * v.width, double(v.cell[1]) * v.height, double(v.cell[2]) * v.depth };
    const double minS = std::min(size[0], std::min(size[1], size[2]));
    const int NI = 16, NJ = 32;
    std::vector<VoxelReaderCone> cones;
    std::vector<double> wt;
    std::vector<bool> hit;
    for (int i = 0; i < NI; ++i)
        for (int j = 0; j < NJ; ++j) {
            const double u = (i + 0.5) / NI, vv = (j + 0.5) / NJ;
            const double r = std::sqrt(u), phi = 2.0 * M_PI * vv;
            const double d[3] = { r * std::cos(phi), std::sqrt(1.0 - u), r * std::sin(phi) };   // cosine-distributed
            double dl[3], l = 0.0;
            for (int a = 0; a < 3; ++a) { dl[a] = d[a] * (minS / size[a]); l += dl[a] * dl[a]; }
            l = std::sqrt(l);
            VoxelReaderCone c;
            const double pw3[3] = { pw.x, pw.y, pw.z };
            double ls[3];
            for (int a = 0; a < 3; ++a) { dl[a] /= l; ls[a] = (pw3[a] - v.origin[a]) / size[a] + dl[a] / (a == 0 ? v.width : a == 1 ? v.height : v.depth); }
            c.posLS = Vec3(float(ls[0]), float(ls[1]), float(ls[2]));
            c.dirLS = Vec3(float(dl[0]), float(dl[1]), float(dl[2]));
            c.biasDirLS = Vec3(0, 0, 0);
            c.tanHalfAngle = 0.0f;
            c.flags = 0u;
            cones.push_back(c);
            wt.push_back(1.0);
            hit.push_back(enginetest::rayHitsBox(pw, Vec3(float(d[0]), float(d[1]), float(d[2])), kWall));
        }
    double total = 0.0;
    for (size_t b = 0; b < cones.size(); b += 64) {
        std::vector<VoxelReaderCone> batch(cones.begin() + long(b), cones.begin() + long(std::min(cones.size(), b + 64)));
        std::vector<VoxelReaderAnswer> fr, co;
        if (!e->voxelReaderParity(s, batch, fr, co)) return;
        for (size_t k = 0; k < batch.size(); ++k) {
            const double esc = 1.0 - std::min(1.0, double(co[k].march[3]) / 0.95);
            total += wt[b + k];
            if (hit[b + k]) leak += wt[b + k] * esc;
            else stop += wt[b + k] * (1.0 - esc);
        }
    }
    leak /= total; stop /= total;
}

int main(int argc, char **argv)
{
    // --cone-target: gi.cone_corner_target, the cone reference's corner against the
    // same analytic (a photon-target row: it prints and does not gate).
    const bool coneTarget = argc > 1 && std::string(argv[1]) == "--cone-target";
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-gi-ddgi-ambient-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();

    // =====================================================================
    // CASE 1 — OPEN-SCENE RECOVERY, and CASE 4 — CORNER HONESTY, both on
    // GiQuality::Low.
    //
    // WHY LOW, and this is a finding in itself (case 2 pins it): Low is the
    // only quality whose voxel cone tracing is ISOTROPIC, and it is therefore
    // the only one whose cone-traced diffuse still carries a MEANINGFUL ambient
    // for this fix to be measured against. Above Low the engine builds
    // anisotropic voxels (OgreGi.cpp: `anisotropic = quality != Low`) and the
    // anisotropic march saturates its alpha almost immediately, so the escape
    // fraction that weights the ambient collapses to ~4% and there is no
    // reference left to compare with. Case 2 measures that separately and
    // fences it; the brief's "within 10% of the pre-DDGI VCT ambient reading"
    // is answered here, where the phrase means something.
    // =====================================================================
    std::printf("\n== case 1/4: open-scene recovery + corner honesty (isotropic VCT) ==\n");
    {
        OpenScene o = buildOpenScene(e, "ddgiamb_open");
        Scene *s = o.scene;
        Image img;

        // ---- what the ambient IS, with no GI at all: the PBS ambient term,
        //      unoccluded by anything (it has no occlusion model).
        GiParams off; off.mode = GiMode::Off;
        CHECK(s->setGlobalIllumination(off), "GI off");
        render(e, 6);
        o.view->readPixels(img);
        const Colour giOffOpen = img.at(kOpenX, kOpenY);
        const Colour giOffCorner = img.at(kCornerX, kCornerY);
        show("open   GI off (the raw ambient)", giOffOpen);
        show("corner GI off (the raw ambient)", giOffCorner);
        CHECK(lum(giOffOpen) > 0.05f, "the scene really is lit by its ambient (nothing else can)");

        // ---- THE REFERENCE: VCT with no field. This is where the ambient
        //      lives in a VCT scene — the cone-traced diffuse's own
        //      `ambient * escapeFraction` add.
        GiParams ref;
        ref.gather = GiToggle::Off;   // PHOTON-GATHER-1d (the header)
        ref.mode = GiMode::Vct;
        ref.quality = GiQuality::Low;        // isotropic: see the header above
        // NO BOUNCE: the subject is the SKY the field and the cones see; the sky-lit wall's
        // bounce (the injection reads the sky, PHOTON-ENV-1) is gi.ddgi's and the cards'
        // subject, and it is not in the analytic below (skyIrradiance).
        ref.numBounces = 0;
        ref.testBoundsMin = Vec3(-9.0f, -1.5f, -9.0f);
        ref.testBoundsMax = Vec3(9.0f, 7.5f, 9.0f);
        CHECK(s->setGlobalIllumination(ref), "VCT (isotropic) builds over the open scene");
        render(e, 6);
        o.view->readPixels(img);
        const Colour refOpen = img.at(kOpenX, kOpenY);
        const Colour refCorner = img.at(kCornerX, kCornerY);
        show("open   VCT (the reference)", refOpen);
        show("corner VCT (the reference)", refCorner);
        std::printf("   the cone reference keeps %.1f%% of the raw ambient on open floor "
                    "and %.1f%% in the corner\n",
                    100.0f * lum(refOpen) / lum(giOffOpen),
                    100.0f * lum(refCorner) / lum(giOffCorner));
        checkConeSet(e, s, "isotropic", false);
        enginetest::dumpVoxelStore(s, "amb");

        // ---- THE FIELD. It carries the sky its probes' rays escape to.
        GiParams fix = ref;
        fix.ddgi = GiToggle::On;
        CHECK(s->setGlobalIllumination(fix), "DDGI binds over the open scene");
        render(e, 6);
        {
            // THE CONVERGED FIELD: the one settle predicate, in frames.
            int n = 0;
            for (; n < 4000 && !s->giStatus().giAtRest; ++n) render(e, 1);
            const GiStatus st = s->giStatus();
            CHECK(st.ifdBound && st.vctBound && st.giAtRest,
                  "the field is bound and GI at rest (every refinement paid)");
            std::printf("   (at rest after %d more frames)\n", n);
        }
        o.view->readPixels(img);
        const Colour fixOpen = img.at(kOpenX, kOpenY);
        const Colour fixCorner = img.at(kCornerX, kCornerY);
        show("open   DDGI", fixOpen);
        show("corner DDGI", fixCorner);
        // The field's sky carries the ambient's HUE, not a grey: the pair is
        // blue-tinted (0.44 blue against 0.40 red/green).
        CHECK(fixOpen.b > fixOpen.r * 1.02f,
              "the field's sky carries the AMBIENT's hue (blue-tinted, like the pair)");

        const float recovery = lum(fixOpen) / lum(refOpen);
        std::printf("   RECOVERY: open floor is %.1f%% of the cone-traced (VCT) reading "
                    "(and %.1f%% of the raw ambient)\n",
                    recovery * 100.0f, 100.0f * lum(fixOpen) / lum(giOffOpen));
        CHECK(recovery > 0.90f && recovery < 1.10f,
              "RECOVERY: the field's sky lands within 10% of the cones' — two integrals of one "
              "environment through one voxel reader");

        // ---- CASE 4: THE CORNER, AGAINST THE ANALYTIC THROUGH THE FIELD'S OWN QUADRATURE
        //      (PHOTON-VOXEL-3 round 9; was PHOTON-FIELD-ROTATE-1's F2 bracket, 75.1-85.2 %,
        //      the leaky store's). A floor patch at the foot of the wall has part of its sky
        //      bricked up; the field shows it as its PROBES see it, blended by the pixel's
        //      weights (fieldCornerQuadrature). THE BAR, derived: that quadrature for the
        //      wall as authored and for the wall as the voxels hold it (grown by one cell on
        //      every face - the conservative raster's envelope) bracket what the field can
        //      show, widened by twice the field's own converged standard error (one
        //      sample's 2 % over sqrt(K) samples) and by the reader's half-code quantisation
        //      of the two pixels (0.5/255 over each reading).
        const GiStatus fst = s->giStatus();
        const float cell = fst.voxelMetres;
        const double anaAuthored = analyticCorner(0.0f);
        const double anaEnvelope = analyticCorner(cell);
        const double fqAuthored = fieldCornerQuadrature(fst, 0.0f);
        const double fqEnvelope = fieldCornerQuadrature(fst, cell);
        const double k = std::max(1u, fst.ifdTargetSamples);
        const double quant = 0.5 / 255.0 / lum(fixCorner) + 0.5 / 255.0 / lum(fixOpen);
        const double tol = 2.0 * 0.02 / std::sqrt(k) + quant;
        const double lo = std::min(fqAuthored, fqEnvelope) - tol;
        const double hi = std::max(fqAuthored, fqEnvelope) + tol;
        const float refCornerFrac = lum(refCorner) / lum(refOpen);
        const float fixCornerFrac = lum(fixCorner) / lum(fixOpen);
        {
            const Vec3 cg = enginetest::groundPointForPixel(kCamPos, kCamTarget, kCornerX, kCornerY, 128);
            for (double hy : { 0.3, 0.9, 1.5 }) {
                double leak = 0.0, stop = 0.0;
                fieldRayLedger(e, s, Vec3(cg.x, float(hy), cg.z), leak, stop);
                std::printf("   THE FIELD'S RAYS above the wall foot at %.1f m: the wall's sky let through %.4f, "
                            "open sky stopped %.4f (of the hemisphere, cosine-weighted)\n", hy, leak, stop);
            }
        }
        std::printf("   CORNER: the analytic lights the wall foot at %.1f%% of open floor (the voxel "
                    "envelope, cell %.3f m: %.1f%%); through the field's own quadrature %.1f%% "
                    "(envelope %.1f%%); the field %.1f%% (%+.1f points of its quadrature), the cone "
                    "reference %.1f%%; bar %.1f-%.1f%% (tolerance %.1f points: K %.0f, quantum)\n",
                    anaAuthored * 100.0, cell, anaEnvelope * 100.0, fqAuthored * 100.0,
                    fqEnvelope * 100.0, fixCornerFrac * 100.0f, (fixCornerFrac - fqAuthored) * 100.0,
                    refCornerFrac * 100.0f, lo * 100.0, hi * 100.0, tol * 100.0, k);
        CHECK(fixCornerFrac < 0.95f,
              "the field DARKENS the wall foot against open floor");
        if (coneTarget) {
            CHECK(refCornerFrac >= lo && refCornerFrac <= hi,
                  "CONE CORNER (target): the cone reference's wall-foot darkening lands on the "
                  "analytic within the field's bar");
        } else {
            CHECK(fixCornerFrac >= lo && fixCornerFrac <= hi,
                  "CORNER: the field's wall-foot darkening lands on the analytic corner as the "
                  "field's own probes see it, within the derived bar");
        }

        o.view->setScene(nullptr);
        e->destroyScene(s);
        e->destroyView(o.view);
    }

    if (coneTarget) {
        std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
        return failures ? 1 : 0;
    }

    // =====================================================================
    // CASE 2 — THE SHIPPED TIER, and the finding this lane owes the ledger:
    // THE CONE SET ON THE ANISOTROPIC STORE, against the same quadrature as case 1.
    //
    // The anisotropic tiers (`anisotropic = quality != GiQuality::Low`, OgreGi.cpp) read the
    // directional mips; since PHOTON-VOXEL-3 the escape IS the directional opacity along
    // the cone's axis (the min-over-axes occupancy estimate and patch 0021's one-mip-finer
    // escape are deleted), so the shipped tier's cones answer the same question as the
    // isotropic ones and are held to the same bar. (History: the unpatched march kept 4 % of
    // an open floor's ambient, patch 0021 ~65-81 %; both were properties of that estimate.)
    // =====================================================================
    std::printf("\n== case 2: the shipped tier (anisotropic VCT) ==\n");
    {
        OpenScene o = buildOpenScene(e, "ddgiamb_aniso");
        Scene *s = o.scene;
        Image img;

        GiParams off; off.mode = GiMode::Off;
        s->setGlobalIllumination(off);
        render(e, 6);
        o.view->readPixels(img);
        const Colour giOffOpen = img.at(kOpenX, kOpenY);
        show("open   GI off (the raw ambient)", giOffOpen);

        GiParams ref = vctBase();            // Medium == anisotropic
        CHECK(s->setGlobalIllumination(ref), "anisotropic VCT builds over the same scene");
        render(e, 6);
        o.view->readPixels(img);
        const Colour refOpen = img.at(kOpenX, kOpenY);
        show("open   anisotropic VCT", refOpen);
        const float kept = lum(refOpen) / lum(giOffOpen);
        checkConeSet(e, s, "anisotropic", true);
        enginetest::dumpVoxelStore(s, "amb2");

        GiParams fix = ref;
        fix.ddgi = GiToggle::On;
        CHECK(s->setGlobalIllumination(fix), "DDGI on the shipped tier");
        render(e, 6);
        o.view->readPixels(img);
        const Colour fixOpen = img.at(kOpenX, kOpenY);
        show("open   DDGI", fixOpen);
        const float vsRaw = lum(fixOpen) / lum(giOffOpen);
        std::printf("   the field lands at %.1f%% of the raw ambient (the cones alone %.1f%%)\n",
                    vsRaw * 100.0f, kept * 100.0f);
        // AGAINST THE FIXTURE'S OWN TRUTH, NOT THE RAW AMBIENT (PHOTON-READER-1): the
        // wall really hides part of this floor point's sky, so the correct answer is
        // its cosine-weighted sky visibility, computed here exactly. What the field
        // loses beyond that is voxel cone tracing's cone-vs-edge mechanism - a
        // widening footprint over coarse mips catching the wall's top edge a ray
        // passes over - plus the probe cage's below-floor layer; a 10 % allowance.
        const Vec3 openPoint = enginetest::groundPointForPixel(Vec3(0.0f, 3.0f, 7.0f),
                                                               Vec3(0.0f, 0.0f, -1.0f), kOpenX,
                                                               kOpenY, 128u);
        const float truth = enginetest::cosineSkyVisibilityUp(
            Vec3(openPoint.x, 1e-4f, openPoint.z),
            { { Vec3(-6.0f, 0.0f, -2.2f), Vec3(6.0f, 4.0f, -1.8f) } });
        std::printf("   ANALYTIC: the open floor point (%.2f, 0, %.2f) sees %.3f of the sky; the "
                    "field gives %.3f of the raw ambient = %.3f of the truth\n",
                    openPoint.x, openPoint.z, truth, vsRaw, truth > 0.0f ? vsRaw / truth : 0.0f);
        CHECK(vsRaw >= 0.90f * truth && vsRaw <= 1.05f,
              "on the shipped tier the field lands at >= 90% of the floor's ANALYTIC sky visibility");

        o.view->setScene(nullptr);
        e->destroyScene(s);
        e->destroyView(o.view);
    }

    // =====================================================================
    // CASE 3 — THE BOUNCE SURVIVES. gi.ddgi's lit room with ambient added: the
    // fix must add achromatic ambient WITHOUT eating the red bounce the field
    // is there to carry.
    // =====================================================================
    std::printf("\n== case 3: the bounce survives the sky ==\n");
    {
        View *view = e->createOffscreenView("ddgiamb_lit", 128, 128, Colour(0, 0, 0));
        Scene *s = e->createScene("ddgiamb_lit");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));

        addSlab(s, Colour(1.0f, 1.0f, 1.0f), Vec3(0.0f, -0.05f, 0.0f), Vec3(14.0f, 0.1f, 14.0f));
        addSlab(s, Colour(1.0f, 0.05f, 0.05f), Vec3(0.0f, 3.0f, -3.0f), Vec3(12.0f, 6.0f, 0.9f));
        const NodeId light = s->createNode();
        const float half = 40.0f * 3.14159265f / 180.0f;      // 80 degrees about X
        const Quat atWall(std::sin(half), 0.0f, 0.0f, std::cos(half));
        s->setNodeTransform(light, Vec3(0, 6, 6), atWall, Vec3(1, 1, 1));
        LightDesc l;
        l.type = LightType::Directional;
        l.colour = Colour(1, 1, 1);
        l.intensity = 1.0f;
        l.castShadows = false;
        s->setLight(light, l);
        enginetest::testCameraLookAt(view, Vec3(0.0f, 4.0f, 6.0f), Vec3(0.0f, 0.0f, -0.5f));
        const unsigned fx = 64, fy = 96;      // gi.ddgi's floor probe

        // THE SKY OFF (a black ambient: no environment for any reader), then ON.
        GiParams field = vctBase();
        field.ddgi = GiToggle::On;
        CHECK(s->setGlobalIllumination(field), "the lit room binds a field, no sky");
        render(e, 6);
        Image img; view->readPixels(img);
        const Colour litGap = img.at(fx, fy);
        show("lit floor  DDGI, no sky", litGap);

        s->setAmbient(kAmbientUpper, kAmbientLower);
        render(e, 30);          // the field re-integrates (its rays now see a sky)
        view->readPixels(img);
        const Colour litFix = img.at(fx, fy);
        show("lit floor  DDGI, sky", litFix);

        const float bounceGap = litGap.r - litGap.g;
        const float bounceFix = litFix.r - litFix.g;
        std::printf("   red bounce: %.4f without the sky, %.4f with it (%.1f%%)\n",
                    bounceGap, bounceFix, 100.0f * bounceFix / bounceGap);
        CHECK(bounceGap > 0.02f, "there is a red bounce to preserve");
        // The sky is achromatic here and enters the SAME atlas texel as the
        // bounce: it must add to it, never replace it. 35 % is the build
        // record's allowance (the sky also brightens the wall, whose red bounce
        // then rises a little — a second bounce of the sky, which is physics).
        CHECK(std::fabs(bounceFix - bounceGap) < 0.35f * bounceGap,
              "the sky preserves the red bounce (it adds light, it does not replace GI)");
        CHECK(lum(litFix) > lum(litGap), "and it does brighten the lit room's floor");

        view->setScene(nullptr);
        e->destroyScene(s);
        e->destroyView(view);
    }

    // =====================================================================
    // CASE 5 — SEALED-ROOM INVARIANCE. THE assertion that says the sky enters
    // the field by VISIBILITY and not as a blanket ambient: a closed room has no
    // sky, so turning the sky on must change NOTHING there. Stated over the WHOLE
    // FRAME, in 8-bit steps, because a per-pixel probe could sit on the one
    // surface that happens not to move — and guarded against the vacuous
    // version of itself (a black or a blown-out frame cannot move either).
    // =====================================================================
    std::printf("\n== case 5: sealed-room invariance ==\n");
    {
        View *view = e->createOffscreenView("ddgiamb_room", 128, 128, Colour(0, 0, 0));
        Scene *s = e->createScene("ddgiamb_room");
        view->setScene(s);
        s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));      // the sky OFF first

        // gi.pcc_mirror's closed room: interior x,z in [-4,4], y in [0,5],
        // 0.4-thick shell, and no way in for sky.
        // Mid-grey rather than white, and a dim light: a white box around a
        // bright point light clips most of the frame, and a clipped pixel
        // cannot move — which would make the invariance assertion below
        // vacuous. The non-vacuity check a few lines down is what enforces it.
        const Colour white(0.45f, 0.45f, 0.45f);
        const Colour red(0.55f, 0.02f, 0.02f);
        addSlab(s, white, Vec3(0.0f, -0.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));    // floor
        addSlab(s, white, Vec3(0.0f,  5.2f, 0.0f), Vec3(8.8f, 0.4f, 8.8f));    // ceiling
        addSlab(s, white, Vec3(0.0f,  2.5f, -4.2f), Vec3(8.8f, 5.0f, 0.4f));   // -Z wall
        addSlab(s, white, Vec3(-4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));    // -X wall
        addSlab(s, white, Vec3( 4.2f, 2.5f, 0.0f), Vec3(0.4f, 5.0f, 8.8f));    // +X wall
        addSlab(s, red,   Vec3(0.0f,  2.5f, 4.2f), Vec3(8.8f, 5.0f, 0.4f));    // +Z wall
        // A point light INSIDE the room: a directional one injects nothing into
        // a sealed volume (OGRE_UPSTREAM_ISSUES, VCT light injection), which
        // would leave the field with nothing to carry.
        const NodeId light = s->createNode();
        s->setNodeTransform(light, Vec3(0.0f, 3.5f, 0.0f), Quat(), Vec3(1, 1, 1));
        LightDesc l;
        l.type = LightType::Point;
        l.colour = Colour(1, 1, 1);
        l.intensity = 0.25f;
        l.range = 20.0f;
        l.castShadows = false;
        CHECK(s->setLight(light, l), "the room's point light arms");
        enginetest::testCameraLookAt(view, Vec3(0.0f, 2.0f, 3.4f), Vec3(0.0f, 1.6f, -1.0f));

        GiParams room = vctBase();
        room.testBoundsMin = Vec3(-5.0f, -1.0f, -5.0f);
        room.testBoundsMax = Vec3(5.0f, 6.0f, 5.0f);
        room.ddgi = GiToggle::On;
        CHECK(s->setGlobalIllumination(room), "the sealed room binds a field, no sky");
        render(e, 6);
        Image off1; view->readPixels(off1);

        // NOT VACUOUS: an invariance assertion over a black or a blown-out
        // frame proves nothing, so count the pixels that could actually have
        // moved before believing that none did.
        unsigned movable = 0;
        for (unsigned y = 0; y < off1.height; ++y)
            for (unsigned x = 0; x < off1.width; ++x) {
                const float v = lum(off1.at(x, y));
                if (v > 0.05f && v < 0.90f) ++movable;
            }
        const float movableFrac = float(movable) / float(off1.width * off1.height);
        std::printf("   %.1f%% of the frame is mid-tone (neither black nor clipped)\n",
                    movableFrac * 100.0f);
        CHECK(movableFrac > 0.20f,
              "the sealed room's frame has room to move (the invariance is not vacuous)");

        // THE CONTROL, before any A/B is believed: the same params pushed
        // again must reproduce the frame exactly. Without it, an invariance
        // failure could be rebuild noise rather than the fix leaking light.
        CHECK(s->setGlobalIllumination(room), "the same params rebuild");
        render(e, 6);
        Image off2; view->readPixels(off2);
        const float control = maxChannelDelta(off1, off2);
        std::printf("   control: an identical rebuild moves at most %.5f (%.2f/255)\n",
                    control, control * 255.0f);
        CHECK(control * 255.0f <= 1.0f, "CONTROL: an identical rebuild is stable to 1/255");

        s->setAmbient(kAmbientUpper, kAmbientLower);          // the sky ON
        render(e, 30);           // every probe re-integrated under the sky
        Image on1; view->readPixels(on1);
        unsigned wx = 0, wy = 0;
        const float delta = maxChannelDelta(off2, on1, &wx, &wy);
        std::printf("   INVARIANCE: worst pixel moves %.5f (%.2f/255) at (%u,%u)\n",
                    delta, delta * 255.0f, wx, wy);
        show("sealed room floor, no sky", off2.at(64, 104));
        show("sealed room floor, sky    ", on1.at(64, 104));
        // THE BAR IS THE READER'S MEASURED RESIDUAL, 2/255 (PHOTON-VOXEL-4, the lead's decision),
        // NOT THE QUANTUM. The quantum floor is ~0.3 of a code: the 10-bit coverage (1/1023 a
        // texel) over the ~3 crossings a cone makes of the shell, 0.003 of escape against the
        // sky. What moves a pixel by 2 codes is geometry the four-cone set's plane march does not
        // yet read - gi.voxel_lab's `sealed` arm, this shell voxelised analytically (64^3), the
        // shipped reader, the four-cone set from every inner surface point on a 0.3 m grid
        // (534 of 13176 cones escape; mean point leak 0.0056), by CLASS:
        //   A  a wall within 0.4 m above the floor   54 of  864 cones, worst cone 0.760, point 0.190
        //   C  the floor within 0.4 m of a wall      50 of  800 cones, worst cone 0.021, point 0.005
        //      - THE CREASE: the floor's (the wall's) texel spreads its surface over the part
        //      behind a start 0.1 m up, and the first plane's position rule reads it as behind;
        //   B  the ceiling-wall edge                 62 of 1664 cones, worst cone 0.050, point 0.013
        //   D  elsewhere                            368 of 9848 cones, worst cone 0.257, point 0.064
        //      - THE COARSE EDGE: the plane axis's lateral reach is its own texel, so a cone
        //      whose plane axis crosses the ceiling beyond a coarse texel's reach never reads it.
        // The owed refinement for all four classes is VOXEL-5: a lateral-only mip family per
        // axis. (The integrated minor-axis kernel closes A and C in the lab but breaks the flat
        // wall; it is kept as a lab candidate with its table, not shipped.)
        CHECK(delta * 255.0f <= 2.0f + 1e-3f,
              "SEALED-ROOM INVARIANCE: no pixel moves more than 2/255 with the sky on (the reader's residual)");

        view->setScene(nullptr);
        e->destroyScene(s);
        e->destroyView(view);
    }

    engine.reset();
    std::printf(failures ? "\n%d FAILURES\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
