// PHOTON-4's two rows (stage 3), revived from the METAL-BOUNCE-1 file whose rows TEST-CUT-1 archived
// (archive/tests/gi: gi.metal_bounce, gi.metal_bounce_route, gi.metal_bounce_gate - their arms are deleted from this
// file; the route arm's plate, its sun and its held gather are what the first row is built from).
//
// gi.workflow_lobe (--workflow) - EVERY SURFACE'S SPECULAR LOBE REACHES THE GI, WHATEVER ITS WORKFLOW.
//   HlmsPbs gives every workflow a specular lobe kS ( F0 ( 1 - Fc ) + Fc ) D Vis (800.PixelShader: the metallic
//   workflow's F0 = lerp( 0.04, base, m ), the specular workflows' the datablock's Fresnel), and every GI route carries
//   it as F0 kS and the Fresnel-90 weight kS (DOCS/traps/ENGINE.md, "F0 AND w ON EVERY ROUTE"). THE FIXTURE: the route
//   arm's plate (2 x 0.1 x 2 m, top at y = 0.1) on a carded cube under a sun straight down (power 3), no sky, one 12 m
//   cascade, NO BOUNCE (the store holds the direct light alone, so no route answers with light another route lacks),
//   the screen-probe gather with its sample sequence held; a polished white metal MIRROR (a mover: in no voxel and no
//   card) looking down at the plate from beyond its far edge, the view's camera looking at the mirror. One view reads
//   BOTH RAY KINDS at the mirror's pixels: the gather's irradiance there (a GATHER ray's hit on the plate - the lobe's
//   integral, the PHOTON-3 ruling) and the traced reflection's radiance (a REFLECTION ray's hit - the lobe toward the
//   mirror). Each material is read on its three LEGS: still on the carded cube (its CARD answers), still on the
//   uncarded cube (the STORE: the voxel row and the hit's direct light), a MOVER (the DECODE). The arm
//   reflect.metalDecode is 0, so a still metal's reflection hit takes the card or the store, never the decode.
//   THE LAW: a SpecularAsFresnel and a Specular material of F0 (1, 0.6, 0.3), kS 1, no diffuse read what their metallic
//   twin (base (1, 0.6, 0.3), metalness 1) reads on every leg of both kinds, per channel within 0.4 % (the archived
//   gi.metal_bounce_route's bar: its legs agreed within 0.4 %, the card's 8-bit Specular layer included - the twins
//   share that layer's code, and the store's 10-bit F0 lane is 1 / 2046 of F0, 0.16 % of the darkest channel's lobe);
//   and a SPEC-GLOSS DIELECTRIC (the Specular workflow, F0 0.04, kS 1, black) carries its 0.04 lobe: against the
//   twin's red channel (F0 1, F = 1 exactly) it reads ( 0.04 A + B ) / ( A + B ) on the gather (every plate point sees
//   the sun at cosine 1, so the view factor cancels) and 0.04 ( 1 - Fc ) + Fc on the reflection (the same D Vis toward
//   the mirror), within 0.4 %. THE BASE (Studio f4ad56a90): the specular workflows' lobe was zero on the card and the
//   store on both kinds and on the decode's gather (every writer held the metallic workflow alone).
//   `--workflow --metallic` reads the metallic twin alone (PHOTON-4 stage 1's acceptance: its legs before and after).
//
// gi.hit_view_term (--view-term) - A STORE-ANSWERED HIT SENDS ITS DIFFUSE LOBE TOWARD ITS VIEWER.
//   HlmsPbs's diffuse is the normalised Disney lobe at ( L, V ): fd90 = 0.5 r + 2 r ( V.H )^2, the light and view
//   scatter 1 + ( fd90 - 1 ) ( 1 - cos )^5, the energy factor lerp( 1, 1 / 1.51, r ) (JahBrdf_piece_all.any; Lagarde
//   and de Rousiers). THE FIXTURE: a matte floor (albedo 0.6, r 1, no specular lobe), uncarded, seen in a polished
//   mirror (a mover) by an ORTHOGRAPHIC camera, so every reflection ray leaves the floor toward the mirror at
//   N.V = 0.15 exactly; the sun "behind the eye" - 30 degrees up, 30 degrees round from the viewer's azimuth (clear of
//   the mirror), N.L = 0.5. The still floor's hits are answered by the store (its row and the hit's direct light), a
//   mover's by the decode (HlmsPbs at the hit). THE LAW: both read rho / pi E N.L Dd( L, V ) - the closed form below,
//   1.564 times the view-averaged rho / pi E N.L A( N.L, r ) the store's hit read at the base (A the lobe's directional
//   albedo by direct integration; PHOTON-CARDS-5 measured 1.581 for its own grazing pair) - the store against the
//   decode and against the closed form, each within 1 % (the RGBA16F reflection target's half step is 0.05 %, the
//   decode's raster shadow and the hit's traced one both see an open floor). The settle is in counted frames: the GI
//   at rest, then 30 - and the reflection's history then still holds ~5 % of the previous leg's answer (measured at
//   the base: the decode leg read 0.3075 against its closed form 0.3129, 5 % of the store's 0.107 gap), so the
//   store-against-decode bar sees a route split above ~1.05 %; the store against the closed form is the clean guard
//   (its leg is the first, with no earlier answer in the history).
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

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

namespace {

constexpr double kPi = 3.14159265358979323846;
unsigned long long gRendered = 0;
void run(Engine *e, int n) { enginetest::frames(e, n); gRendered += unsigned(n); }

/// The settle: the GI at rest (the store, the cards and the gather's history), then `extra` counted frames.
void settle(Engine *e, Scene *s, int extra)
{
    run(e, 20);
    for (int f = 0; f < 600 && !s->published().gi.giAtRest; ++f) run(e, 1);
    run(e, extra);
}

/// The normalised Disney diffuse lobe (no albedo, no N.L) - HlmsPbs's BRDF_Default diffuse.
double disney(double nl, double nv, double vh, double r)
{
    const double fd90 = 0.5 * r + 2.0 * vh * vh * r;
    const double ls = 1.0 + (fd90 - 1.0) * std::pow(1.0 - nl, 5.0);
    const double vs = 1.0 + (fd90 - 1.0) * std::pow(1.0 - nv, 5.0);
    return ls * vs * (1.0 + (1.0 / 1.51 - 1.0) * r);
}

/// Rotation about +X by `a` radians (Quat is x, y, z, w).
Quat aboutX(double a) { return Quat(float(std::sin(a * 0.5)), 0.0f, 0.0f, float(std::cos(a * 0.5))); }

void normalise(double v[3])
{
    const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    for (int k = 0; k < 3; ++k) v[k] /= l;
}

/// The mean rgb of a window of a float image (pixels whose alpha is above `minW`; -1 where none).
void windowMean(const ImageF &im, unsigned x0, unsigned y0, unsigned x1, unsigned y1, double out[3], unsigned &n)
{
    double s[3] = {};
    n = 0;
    for (unsigned y = y0; y < y1 && y < im.height; ++y)
        for (unsigned x = x0; x < x1 && x < im.width; ++x) {
            const Colour c = im.at(x, y);
            s[0] += c.r; s[1] += c.g; s[2] += c.b;
            ++n;
        }
    for (int k = 0; k < 3; ++k) out[k] = n ? s[k] / n : -1.0;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// gi.workflow_lobe
// ---------------------------------------------------------------------------------------------------------------------
static int workflowLobe(Engine *e, bool metallicOnly)
{
    Scene *s = e->createScene("workflowlobe");
    float sh[27] = { 0 };
    s->setAmbientSh(sh);
    s->setEnvironmentLight(Colour(0, 0, 0, 1));
    CHECK(enginetest::addDirectionalLight(s, Vec3(0.0f, -1.0f, 0.0f), 3.0f) != 0, "the sun straight down");
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    MeshData cd = enginetest::unitCubeMesh();
    cd.cards = enginetest::boxCards(0.5f);
    const MeshId carded = s->createMesh(cd);

    // THE MATERIALS: the metallic twin, its two specular-workflow twins, the spec-gloss dielectric
    const float tint[3] = { 1.0f, 0.6f, 0.3f };
    PbrParams metal;
    metal.albedo = Colour(tint[0], tint[1], tint[2]);
    metal.metalness = 1.0f;
    metal.roughness = 0.3f;
    PbrParams saf;
    saf.workflow = PbrParams::Workflow::SpecularAsFresnel;
    saf.albedo = Colour(0.0f, 0.0f, 0.0f);
    saf.roughness = 0.3f;
    saf.useFresnelColour = true;
    saf.fresnelColour = Colour(tint[0], tint[1], tint[2]);
    saf.separateFresnel = true;
    saf.specularColour = Colour(1.0f, 1.0f, 1.0f);
    PbrParams spec = saf;
    spec.workflow = PbrParams::Workflow::Specular;
    PbrParams diel = spec;
    diel.fresnelColour = Colour(0.04f, 0.04f, 0.04f);
    diel.separateFresnel = false;
    enum { kDiel, kMetal, kSaf, kSpec, kMats };
    const char *names[kMats] = { "spec-gloss dielectric", "metallic twin", "SpecularAsFresnel", "Specular" };
    const MaterialId mats[kMats] = { s->createPbrMaterial(diel), s->createPbrMaterial(metal), s->createPbrMaterial(saf),
                                     s->createPbrMaterial(spec) };

    const NodeId plate = s->createNode();
    s->attachMesh(plate, carded, mats[metallicOnly ? kMetal : kDiel]);
    s->setNodeTransform(plate, Vec3(0.0f, 0.05f, 0.0f), Quat(), Vec3(2.0f, 0.1f, 2.0f));

    // THE MIRROR, beyond the plate's far edge, a mover (declared before its geometry: no rebuild)
    const double P[3] = { 0.0, 0.1, 0.0 }, M[3] = { 0.0, 1.5, 1.8 }, C[3] = { 0.0, 0.5, -2.0 };
    double toC[3] = { C[0] - M[0], C[1] - M[1], C[2] - M[2] }, toP[3] = { P[0] - M[0], P[1] - M[1], P[2] - M[2] };
    normalise(toC);
    normalise(toP);
    double n[3] = { toC[0] + toP[0], toC[1] + toP[1], toC[2] + toP[2] };
    normalise(n);
    const NodeId mirror = s->createNode();
    s->setNodeMovable(mirror, true);
    PbrParams mp;
    mp.albedo = Colour(1.0f, 1.0f, 1.0f);
    mp.metalness = 1.0f;
    mp.roughness = 0.02f;
    s->attachMesh(mirror, cube, s->createPbrMaterial(mp));
    // the cube's +Y face onto n: about +X by atan2( n.z, n.y )
    s->setNodeTransform(mirror, Vec3(float(M[0]), float(M[1]), float(M[2])), aboutX(std::atan2(n[2], n[1])),
                        Vec3(1.4f, 0.05f, 1.4f));

    View *v = e->createOffscreenView("workflowlobe", 256, 256, Colour(0, 0, 0));
    if (!e->rayQueryAvailable() || !e->rayTracing()) {   // asked once the first view exists (Engine.h)
        std::printf("ok: no ray queries on this machine - the row is about the ray tier's hits; skipping\n");
        return 0;
    }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    v->setPostFx(fx);
    v->setShadows(true);
    v->setOffscreenContract(OffscreenContract::StillPicture);
    CHECK(v->setScene(s), "the view shows the plate's mirror");
    enginetest::testCameraLookAt(v, Vec3(float(C[0]), float(C[1]), float(C[2])), Vec3(float(M[0]), float(M[1]), float(M[2])));
    s->setRayTracing(RayTracingMode::On);
    GiParams g;
    g.mode = GiMode::Vct;
    g.quality = GiQuality::High;
    g.numBounces = 0;
    g.ddgi = GiToggle::Off;
    g.gather = GiToggle::On;
    g.cascadeCount = 1;
    g.cascadeSet[0] = GiParams::GiCascadeDesc{ 6.0f, 64, 0.0f };
    s->setGlobalIllumination(g);
    GatherTuning t;
    t.readback = false;
    t.restOff = true;
    t.freezeFrameIndex = true;
    s->setGatherTuning(t);
    e->setArm("reflect.metalDecode", 0.0);

    enum { kCard, kStore, kDecode, kLegs };
    const char *legNames[kLegs] = { "still, its CARD", "still, NO CARDS (the STORE)", "a MOVER (the DECODE)" };
    double gath[kMats][kLegs][3] = {}, refl[kMats][kLegs][3] = {};
    const auto readLeg = [&](int m, int leg) {
        settle(e, s, 43);
        t.readback = true;   // the readback door for the last frames only (TOOLING-6 item 6)
        s->setGatherTuning(t);
        run(e, 5);
        const GatherStatus st = s->published().gi.gather;
        t.readback = false;
        s->setGatherTuning(t);
        // THE MIRROR'S PIXELS: the window at the view's centre (the camera looks at the mirror's centre)
        double sum[3] = {};
        unsigned px = 0;
        if (!st.irradiance.empty())
            for (unsigned y = 112; y < 144; ++y)
                for (unsigned x = 112; x < 144; ++x) {
                    const float *q = &st.irradiance[(size_t(y) * st.irradianceW + x) * 4u];
                    if (q[3] <= 0.0f) continue;
                    for (int c = 0; c < 3; ++c) sum[c] += q[c];
                    ++px;
                }
        for (int c = 0; c < 3; ++c) gath[m][leg][c] = px ? sum[c] / px : -1.0;
        ImageF r;
        unsigned rn = 0;
        if (v->readReflectionHdr(r)) {
            const unsigned cx = r.width / 2u, cy = r.height / 2u, h = r.width / 16u;
            windowMean(r, cx - h, cy - h, cx + h, cy + h, refl[m][leg], rn);
        } else {
            for (int c = 0; c < 3; ++c) refl[m][leg][c] = -1.0;
        }
        std::printf("the %s, %s: GATHER (the mirror's E/pi) %.6f %.6f %.6f (%u px); REFLECTION %.6f %.6f %.6f (%u px; %ux%u)\n",
                    names[m], legNames[leg], gath[m][leg][0], gath[m][leg][1], gath[m][leg][2], px, refl[m][leg][0],
                    refl[m][leg][1], refl[m][leg][2], rn, r.width, r.height);
    };
    for (int m = 0; m < kMats; ++m) {
        if (metallicOnly && m != kMetal) continue;
        // the card leg: still, carded (a mover from the previous material's last leg turns still)
        CHECK(s->attachMesh(plate, carded, mats[m]), "the plate takes the material on its carded cube");
        s->setNodeMovable(plate, false);
        s->refreshGlobalIllumination();
        readLeg(m, kCard);
        // the store leg: still on the uncarded cube
        CHECK(s->attachMesh(plate, cube, mats[m]), "the plate on the uncarded cube (the store's leg)");
        s->refreshGlobalIllumination();
        readLeg(m, kStore);
        // the decode leg: a mover
        s->setNodeMovable(plate, true);
        s->refreshGlobalIllumination();
        readLeg(m, kDecode);
    }
    e->setArm("reflect.metalDecode", 0.30);

    char msg[400];
    for (int leg = 0; leg < kLegs; ++leg)
        for (int c = 0; c < 3; ++c) {
            std::snprintf(msg, sizeof msg, "the metallic twin's %s answers, channel %d: gather %.6f, reflection %.6f",
                          legNames[leg], c, gath[kMetal][leg][c], refl[kMetal][leg][c]);
            CHECK(gath[kMetal][leg][c] > 0.0 && refl[kMetal][leg][c] > 0.0, msg);
        }
    if (!metallicOnly) {
        // THE TWINS: every leg, both kinds, per channel within 0.4 %
        for (int m = kSaf; m <= kSpec; ++m)
            for (int leg = 0; leg < kLegs; ++leg)
                for (int c = 0; c < 3; ++c) {
                    const double rg = gath[kMetal][leg][c] > 0.0 ? gath[m][leg][c] / gath[kMetal][leg][c] : 0.0;
                    const double rr = refl[kMetal][leg][c] > 0.0 ? refl[m][leg][c] / refl[kMetal][leg][c] : 0.0;
                    std::snprintf(msg, sizeof msg, "THE %s READS ITS METALLIC TWIN, %s, channel %d: GATHER x%.5f, "
                                  "REFLECTION x%.5f (bar 0.4 %%)", names[m], legNames[leg], c, rg, rr);
                    CHECK(std::fabs(rg - 1.0) <= 0.004 && std::fabs(rr - 1.0) <= 0.004, msg);
                }
        // THE DIELECTRIC'S 0.04 LOBE against the twin's red channel (F0 1: F = 1)
        double A = 0.0, B = 0.0;
        enginetest::ggxSpecularAlbedo(1.0, 0.3, A, B, 512);
        const double wantG = (0.04 * A + B) / (A + B);
        double V[3] = { M[0] - P[0], M[1] - P[1], M[2] - P[2] };
        normalise(V);
        double H[3] = { V[0], V[1] + 1.0, V[2] };
        normalise(H);
        const double fc = std::pow(1.0 - (V[0] * H[0] + V[1] * H[1] + V[2] * H[2]), 5.0);
        const double wantR = 0.04 * (1.0 - fc) + fc;
        for (int leg = 0; leg < kLegs; ++leg)
            for (int c = 0; c < 3; ++c) {
                const double rg = gath[kMetal][leg][0] > 0.0 ? gath[kDiel][leg][c] / gath[kMetal][leg][0] : 0.0;
                const double rr = refl[kMetal][leg][0] > 0.0 ? refl[kDiel][leg][c] / refl[kMetal][leg][0] : 0.0;
                std::snprintf(msg, sizeof msg, "THE SPEC-GLOSS DIELECTRIC CARRIES ITS 0.04 LOBE, %s, channel %d: GATHER "
                              "%.5f of the twin's red (want (0.04 A + B) / (A + B) = %.5f), REFLECTION %.5f (want "
                              "0.04 (1 - Fc) + Fc = %.5f; bar 0.4 %%)", legNames[leg], c, rg, wantG, rr, wantR);
                CHECK(std::fabs(rg / wantG - 1.0) <= 0.004 && std::fabs(rr / wantR - 1.0) <= 0.004, msg);
            }
    }
    std::printf("frames rendered: %llu\n", gRendered);
    GiParams off;
    off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    v->setScene(nullptr);
    e->destroyView(v);
    e->destroyScene(s);
    return failures ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------------------------------
// gi.hit_view_term
// ---------------------------------------------------------------------------------------------------------------------
static int hitViewTerm(Engine *e)
{
    Scene *s = e->createScene("hitviewterm");
    float sh[27] = { 0 };
    s->setAmbientSh(sh);
    s->setEnvironmentLight(Colour(0, 0, 0, 1));
    const double kNV = 0.15, elev = std::asin(kNV);
    // V (the floor toward the mirror) and the sun, 30 degrees up and 30 degrees round from V's azimuth
    const double V[3] = { 0.0, kNV, -std::cos(elev) };
    const double L[3] = { -std::sin(kPi / 6) * std::cos(kPi / 6), std::sin(kPi / 6), -std::cos(kPi / 6) * std::cos(kPi / 6) };
    const float kSun = 3.0f;
    CHECK(enginetest::addDirectionalLight(s, Vec3(float(-L[0]), float(-L[1]), float(-L[2])), kSun) != 0,
          "the sun behind the eye");
    const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
    const double kRho = 153.0 / 255.0, kRough = 1.0;
    PbrParams fp;
    fp.albedo = Colour(float(kRho), float(kRho), float(kRho));
    fp.roughness = float(kRough);
    fp.workflow = PbrParams::Workflow::Specular;
    fp.ior = 1.0f;
    fp.specularColour = Colour(0.0f, 0.0f, 0.0f);
    const NodeId floor = s->createNode();
    s->attachMesh(floor, cube, s->createPbrMaterial(fp));
    s->setNodeTransform(floor, Vec3(0.0f, -0.05f, 0.0f), Quat(), Vec3(12.0f, 0.1f, 12.0f));
    // THE MIRROR: vertical, facing +Z, centre (0, 0.6, 0) - a mover
    const NodeId mirror = s->createNode();
    s->setNodeMovable(mirror, true);
    PbrParams mp;
    mp.albedo = Colour(1.0f, 1.0f, 1.0f);
    mp.metalness = 1.0f;
    mp.roughness = 0.02f;
    s->attachMesh(mirror, cube, s->createPbrMaterial(mp));
    s->setNodeTransform(mirror, Vec3(0.0f, 0.6f, -0.025f), Quat(), Vec3(1.0f, 0.6f, 0.05f));
    // THE EYE: orthographic, 2.5 m from the mirror along the mirrored V, looking at its centre
    View *v = e->createOffscreenView("hitviewterm", 128, 128, Colour(0, 0, 0));
    if (!e->rayQueryAvailable() || !e->rayTracing()) {   // asked once the first view exists (Engine.h)
        std::printf("ok: no ray queries on this machine - the row is about the ray tier's hits; skipping\n");
        return 0;
    }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = 2;
    v->setPostFx(fx);
    v->setShadows(true);
    v->setOffscreenContract(OffscreenContract::StillPicture);
    CHECK(v->setScene(s), "the view shows the mirror");
    CameraDesc cam = enginetest::testCameraDescLookAt(
        Vec3(0.0f, float(0.6 + 2.5 * kNV), float(2.5 * std::cos(elev))), Vec3(0.0f, 0.6f, 0.0f));
    cam.orthographic = true;
    cam.orthoSize = 0.08f;
    cam.farClip = 50.0f;
    v->setCamera(cam);
    s->setRayTracing(RayTracingMode::On);
    GiParams g;
    g.mode = GiMode::Vct;
    g.quality = GiQuality::High;
    g.numBounces = 0;
    g.ddgi = GiToggle::Off;
    g.gather = GiToggle::Off;
    g.cascadeCount = 1;
    g.cascadeSet[0] = GiParams::GiCascadeDesc{ 6.0f, 64, 0.0f };
    s->setGlobalIllumination(g);

    double got[2][3] = {};
    const char *legs[2] = { "still (the STORE)", "a MOVER (the DECODE)" };
    for (int leg = 0; leg < 2; ++leg) {
        if (leg == 1) {
            s->setNodeMovable(floor, true);
            s->refreshGlobalIllumination();
        }
        settle(e, s, 30);
        ImageF r;
        unsigned rn = 0;
        CHECK(v->readReflectionHdr(r), "the reflection target reads back");
        const unsigned cx = r.width / 2u, cy = r.height / 2u, h = r.width / 8u;
        windowMean(r, cx - h, cy - h, cx + h, cy + h, got[leg], rn);
        std::printf("the floor %s, toward the mirror at N.V %.2f: %.6f %.6f %.6f (%u px)\n", legs[leg], kNV,
                    got[leg][0], got[leg][1], got[leg][2], rn);
    }
    // THE CLOSED FORM: rho / pi E N.L Dd( L, V ), and the view-averaged form the base's hit read
    double H[3] = { L[0] + V[0], L[1] + V[1], L[2] + V[2] };
    normalise(H);
    const double nl = L[1], nv = V[1], vh = V[0] * H[0] + V[1] * H[1] + V[2] * H[2];
    const double exact = kRho / kPi * kSun * nl * disney(nl, nv, vh, kRough);
    const double averaged = kRho / kPi * kSun * nl * enginetest::disneyDiffuseAlbedo(nl, kRough, 256);
    std::printf("the closed form: rho / pi E N.L Dd( L, V ) = %.6f (N.L %.3f, N.V %.3f, V.H %.4f); the view-averaged "
                "rho / pi E N.L A( N.L, r ) = %.6f - x%.4f\n", exact, nl, nv, vh, averaged, exact / averaged);
    char msg[400];
    for (int c = 0; c < 3; ++c) {
        std::snprintf(msg, sizeof msg, "THE STORE-ANSWERED HIT SENDS ITS DIFFUSE TOWARD ITS VIEWER, channel %d: the store "
                      "%.6f against the decode %.6f (x%.4f; bar 1 %%)", c, got[0][c], got[1][c],
                      got[1][c] > 0.0 ? got[0][c] / got[1][c] : 0.0);
        CHECK(got[1][c] > 0.0 && std::fabs(got[0][c] / got[1][c] - 1.0) <= 0.01, msg);
        std::snprintf(msg, sizeof msg, "...and the closed form, channel %d: the store %.6f, rho / pi E N.L Dd( L, V ) "
                      "%.6f (x%.4f; bar 1 %%)", c, got[0][c], exact, got[0][c] / exact);
        CHECK(std::fabs(got[0][c] / exact - 1.0) <= 0.01, msg);
    }
    std::printf("frames rendered: %llu\n", gRendered);
    GiParams off;
    off.mode = GiMode::Off;
    s->setGlobalIllumination(off);
    v->setScene(nullptr);
    e->destroyView(v);
    e->destroyScene(s);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    const bool metallicOnly = argc > 2 && std::string(argv[2]) == "--metallic";
    if (mode != "--workflow" && mode != "--view-term") {
        std::printf("FAIL: usage: test_gi_metal_bounce --workflow [--metallic] | --view-term\n");
        return 2;
    }
    std::string err;
    const auto cfg = enginetest::testEngineConfig("test-gi-metal-bounce-ogre.log");
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->submitFrame(jahshaka::engine::FrameInput::delta(1.0f / 60.0f));
    Engine *e = engine.get();
    return mode == "--workflow" ? workflowLobe(e, metallicOnly) : hitViewTerm(e);
}
