// gi.hit_specular_mobility (PHOTON-7 11-2 (3), C2: THE HIT'S OWN SPECULAR) - A SURFACE SEEN IN A REFLECTION REFLECTS
// THE WORLD THE SAME WHETHER IT IS STILL OR A MOVER, AND ITS REFLECTED RADIANCE IS CONTINUOUS IN ITS METALNESS.
//
// A reflection ray's hit on a still surface was answered by the radiance store (the card, else the cascades, plus the
// direct light at the hit): the store holds a surface's INDIRECT specular only as its lobe's INTEGRAL, re-emitted the
// same way to every direction - so a glossy slab under a bright emitter, seen in a mirror, showed no reflection of the
// emitter. The same slab as a MOVER is a record, shaded by HlmsAtom's decode with its full lobe: the two pictures of one
// surface differed by the slab's mobility. And the metal switch (kGpuMetalFloor 0.5, kReflectMetalDecodeRoughness 0.30)
// sent a sharp reflector's hit on a metal from metalness 0.5 to the decode: a step on the alpha -> F0 continuum.
// C2 traces ONE secondary ray at the hit - the hit's own VNDF draw (the centre ray at the packing floor), shaded by the
// store with no recursion, weighted by the hit's split DFG (Russian roulette on its largest channel) - in place of the
// store's indirect-specular integral, and deletes the switch.
// THE FIXTURE: a matte floor; a glossy SLAB on it (a dielectric, roughness 0.05, a dark base); a bright EMISSIVE far
// wall at x = -5 facing it; a vertical MIRROR (a metal, roughness 0) at x = 2; the camera between, looking at the mirror,
// which shows the slab's top face at a grazing angle - its specular direction meets the emissive wall (Fresnel ~0.3
// there), while the wall covers a small part of the slab's hemisphere (its lobe's integral reads little of it). Each arm is its own scene in one process, at rest.
// THE LAWS: (1) THE MOBILITY: the slab's mean in the mirror, still against the same slab as a MOVER, within q + 3 SE
// (RED before C2: the still slab lacked its reflection of the panel). (2) THE CONTINUUM: the slab as a metal at 0.49
// and at 0.51 (base 0.6 grey), its mean in the mirror changing by no more than the material's own change predicts plus
// q + 3 SE (the split DFG's F0 moves 2 % of the way from 0.04 to the base: |m51 - m49| <= 0.05 |m49| + q + 3 SE, a
// bound five times the linear change; RED before C2: 0.51 decoded, 0.49 the store's). (3) (1) again through a
// ROUGH mirror (0.12: its draw a VNDF sample the denoiser averages - the case that convicted C2's roulette, (3b)).
// A fixture (the engine alone, one offscreen view a scene). Counted in frames.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        if (cond) { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); }                              \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); ++failures; }                       \
    } while (0)

static const unsigned kWidth = 256, kHeight = 144;

namespace {
NodeId box(Scene *s, const Vec3 &at, const Vec3 &size, const PbrParams &p, bool mover = false) {
    const NodeId node = s->createNode();
    if (node && mover) s->setNodeMovable(node, true);   // (BEFORE the geometry: no GI rebuild)
    const MeshId mesh = s->createMesh(enginetest::unitCubeMesh());
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    enginetest::poseRegistry()[s][node] = enginetest::NodePose{};
    enginetest::setNodePosition(s, node, at);
    enginetest::setNodeScale(s, node, size);
    return node;
}
float lum(const Colour &c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

// THE SLAB's top face: x -1.6 .. 0.4, z -2 .. 2, at y 0.2 (a 0.2 m slab on the floor)
const double kSlabX0 = -1.6, kSlabX1 = 0.4, kSlabZ = 2.0, kSlabTop = 0.2;
const Vec3 kEye(1.0f, 1.0f, 0.0f), kAt(2.0f, 0.5f, 0.0f);

struct Reading {
    bool ok = false;
    double mean = 0.0, se = 0.0;
    size_t n = 0;
    int rest = -1;
};

/// ONE ARM: its own scene and view, the slab's material and mobility as given, run to the GI's rest and the
/// reflection's samples, the slab's pixels in the mirror read (their floor point found through the mirror plane x = 2)
Reading arm(Engine *e, int &frames, const char *name, const PbrParams &slabMat, bool mover, float mirrorRough = 0.0f) {
    Reading r;
    Scene *s = e->createScene(name);
    View *v = e->createOffscreenView(name, kWidth, kHeight, Colour(0, 0, 0), OffscreenContract::StillPicture);
    if (!s || !v) return r;
    v->setScene(s);
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    fx.reflectSources = ReflectSources::Rays;
    fx.hdrReadback = true;
    v->setPostFx(fx);
    PbrParams matte;
    matte.albedo = Colour(0.5f, 0.5f, 0.5f);
    matte.roughness = 0.9f;
    PbrParams mirror;
    mirror.albedo = Colour(0.95f, 0.95f, 0.95f);
    mirror.metalness = 1.0f;
    mirror.roughness = mirrorRough;
    PbrParams panel;
    panel.albedo = Colour(0.0f, 0.0f, 0.0f);
    panel.roughness = 1.0f;
    panel.emissive = Colour(4.0f, 4.0f, 4.0f);
    box(s, Vec3(-2.0f, -0.05f, 0.0f), Vec3(8.0f, 0.1f, 12.0f), matte);                 // the floor, x -6 .. 2
    box(s, Vec3(2.05f, 1.0f, 0.0f), Vec3(0.1f, 2.0f, 12.0f), mirror);                   // the mirror at x 2
    box(s, Vec3(-0.6f, 0.1f, 0.0f), Vec3(2.0f, 0.2f, 4.0f), slabMat, mover);            // the slab, top at 0.2
    box(s, Vec3(-5.05f, 1.5f, 0.0f), Vec3(0.1f, 3.0f, 12.0f), panel);                    // the emissive far wall
    s->setAmbient(Colour(0.0f, 0.0f, 0.0f), Colour(0.0f, 0.0f, 0.0f));
    GiParams gi;
    gi.mode = GiMode::Vct;
    gi.quality = GiQuality::High;
    if (!s->setGlobalIllumination(gi)) { e->destroyView(v); e->destroyScene(s); return r; }
    enginetest::testCameraLookAt(v, kEye, kAt);
    e->waitForAsyncShaders();
    for (int f = 0, quiet = 0; f < 400; ++f) {
        enginetest::frames(e, 1);
        ++frames;
        const GiStatus g = s->published().gi;
        quiet = !g.chainOwed && !g.feedbackOwed && !g.cascades.empty() ? quiet + 1 : 0;
        if (quiet >= 12) { r.rest = f + 1 - quiet; break; }
    }
    enginetest::frames(e, 96);   // the reflection's rest samples at the GI's rest
    frames += 96;
    ImageF img;
    if (v->readPixelsHdr(img) && img.width == kWidth && img.height == kHeight && r.rest >= 0) {
        const double fx0 = kAt.x - kEye.x, fy0 = kAt.y - kEye.y, fz0 = kAt.z - kEye.z;
        const double fl = std::sqrt(fx0 * fx0 + fy0 * fy0 + fz0 * fz0);
        const double F[3] = { fx0 / fl, fy0 / fl, fz0 / fl };
        double R[3] = { -F[2], 0.0, F[0] };
        const double rl = std::sqrt(R[0] * R[0] + R[2] * R[2]);
        R[0] /= rl;
        R[2] /= rl;
        const double U[3] = { R[1] * F[2] - R[2] * F[1], R[2] * F[0] - R[0] * F[2], R[0] * F[1] - R[1] * F[0] };
        const double tanY = std::tan(22.5 * 3.14159265358979 / 180.0), tanX = tanY * double(kWidth) / double(kHeight);
        std::vector<double> vals;
        for (unsigned y = 0; y < kHeight; ++y)
            for (unsigned x = 0; x < kWidth; ++x) {
                const double nx = (2.0 * (x + 0.5) / kWidth - 1.0) * tanX, ny = (1.0 - 2.0 * (y + 0.5) / kHeight) * tanY;
                double d[3] = { F[0] + R[0] * nx + U[0] * ny, F[1] + R[1] * nx + U[1] * ny, F[2] + R[2] * nx + U[2] * ny };
                if (!(d[0] > 0.0)) continue;
                const double tm = (2.0 - kEye.x) / d[0];
                const double my = kEye.y + d[1] * tm, mz = kEye.z + d[2] * tm;
                if (my < 0.0 || my > 2.0 || std::fabs(mz) > 6.0) continue;   // not the mirror
                d[0] = -d[0];
                if (!(d[1] < 0.0)) continue;
                const double tf = (kSlabTop - my) / d[1];
                const double px = 2.0 + d[0] * tf, pz = mz + d[2] * tf;
                // the slab's top face, a margin inside its edges (no edge pixel)
                if (px < kSlabX0 + 0.1 || px > kSlabX1 - 0.1 || std::fabs(pz) > kSlabZ - 0.1) continue;
                vals.push_back(lum(img.at(x, y)));
            }
        double sum = 0.0;
        for (const double vv : vals) sum += vv;
        r.n = vals.size();
        r.mean = r.n ? sum / double(r.n) : 0.0;
        double d2 = 0.0;
        for (const double vv : vals) d2 += (vv - r.mean) * (vv - r.mean);
        r.se = r.n > 1 ? std::sqrt(d2 / double(r.n - 1) / double(r.n)) : 0.0;
        r.ok = r.n > 500u;
    }
    v->setScene(nullptr);
    e->destroyView(v);
    e->destroyScene(s);
    enginetest::frames(e, 2);
    frames += 2;
    return r;
}
double quantum(double m) { return m > 0.0 ? std::ldexp(1.0, int(std::floor(std::log2(m))) - 10) : 0.0; }
}   // namespace

int main()
{
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-hit-specular-mobility-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    Engine *e = engine.get();
    int frames = 1;
    e->submitFrame(FrameInput::delta(1.0f / 60.0f));
    {
        // (the device's ray query is known once a view exists)
        Scene *ps = e->createScene("spec_probe");
        View *pv = e->createOffscreenView("spec_probe", 16, 16, Colour(0, 0, 0), OffscreenContract::StillPicture);
        if (ps && pv) pv->setScene(ps);
        const bool rq = e->rayQueryAvailable() && e->rayTracing();
        if (pv) { pv->setScene(nullptr); e->destroyView(pv); }
        if (ps) e->destroyScene(ps);
        if (!rq) {
            std::printf("SKIP: no ray-query device here - the reflection's hits are the ray tier's\n");
            return 77;
        }
    }
    PbrParams lacquer;
    lacquer.albedo = Colour(0.12f, 0.03f, 0.03f);
    lacquer.metalness = 0.0f;
    lacquer.roughness = 0.05f;
    const Reading still = arm(e, frames, "spec_still", lacquer, false);
    const Reading moving = arm(e, frames, "spec_mover", lacquer, true);
    PbrParams m49;
    m49.albedo = Colour(0.6f, 0.6f, 0.6f);
    m49.roughness = 0.05f;
    m49.metalness = 0.49f;
    PbrParams m51 = m49;
    m51.metalness = 0.51f;
    const Reading r49 = arm(e, frames, "spec_m49", m49, false);
    const Reading r51 = arm(e, frames, "spec_m51", m51, false);
    // (3) THE SAME THROUGH A ROUGH MIRROR (0.12: a VNDF draw, not the centre ray - the denoiser averages it): a Russian
    // roulette on C2 read the still slab 20 % dark here (0.897 against 1.127 always traced: Jensen through the
    // compressive denoiser space, the lead's ruling (3b))
    const Reading roughStill = arm(e, frames, "spec_rough_still", lacquer, false, 0.12f);
    const Reading roughMover = arm(e, frames, "spec_rough_mover", lacquer, true, 0.12f);
    for (const auto &[name, r] : { std::pair<const char *, const Reading &>{ "the lacquer, still", still },
                                   { "the lacquer, a mover", moving },
                                   { "metalness 0.49, still", r49 },
                                   { "metalness 0.51, still", r51 },
                                   { "the lacquer, still, a rough mirror", roughStill },
                                   { "the lacquer, a mover, a rough mirror", roughMover } })
        std::printf("   %-24s GI at rest after %d frames; the slab in the mirror %zu px, mean %.6f (SE %.6f)\n", name,
                    r.rest, r.n, r.mean, r.se);
    CHECK(still.ok && moving.ok && r49.ok && r51.ok && roughStill.ok && roughMover.ok,
          "premise: every arm rests and the mirror shows the slab");
    const double se1 = std::sqrt(still.se * still.se + moving.se * moving.se);
    const double q1 = quantum(std::max(still.mean, moving.mean));
    CHECK(std::fabs(still.mean - moving.mean) <= q1 + 3.0 * se1,
          "(1) THE MOBILITY: the slab's reflection in the mirror the same still and as a mover (%+.6f, %.2f SE; bar q + 3 "
          "SE)", still.mean - moving.mean, se1 > 0 ? std::fabs(still.mean - moving.mean) / se1 : 0.0);
    const double se2 = std::sqrt(r49.se * r49.se + r51.se * r51.se);
    const double q2 = quantum(std::max(r49.mean, r51.mean));
    CHECK(std::fabs(r51.mean - r49.mean) <= 0.05 * std::fabs(r49.mean) + q2 + 3.0 * se2,
          "(2) THE CONTINUUM: metalness 0.49 -> 0.51 moves the slab's reflection by %+.6f (%.2f %% of 0.49's; bar 5 %% + q + "
          "3 SE)", r51.mean - r49.mean, r49.mean > 0 ? 100.0 * (r51.mean - r49.mean) / r49.mean : 0.0);
    const double se3 = std::sqrt(roughStill.se * roughStill.se + roughMover.se * roughMover.se);
    const double q3 = quantum(std::max(roughStill.mean, roughMover.mean));
    CHECK(std::fabs(roughStill.mean - roughMover.mean) <= q3 + 3.0 * se3,
          "(3) THE MOBILITY THROUGH A ROUGH MIRROR (0.12): still against a mover (%+.6f, %.2f SE; bar q + 3 SE)",
          roughStill.mean - roughMover.mean, se3 > 0 ? std::fabs(roughStill.mean - roughMover.mean) / se3 : 0.0);
    std::printf("\ngi.hit_specular_mobility: %d frames, %s\n", frames, failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
