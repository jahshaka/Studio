// atom-arcade — a 120 m curved arcade (LAB-1 stage 2; the plaza's strip facades made clean).
//
// THE GEOMETRY. A 40 m radius arc of 3 radians (120 m of facade, its middle at (0, 0, 40), its front facing OUT, +radial)
// of 30 bays (4 m each): ONE mesh of
//   * the facade's face: a 10 m tall strip of 240 authored chords (0.5 m each; sagitta 0.8 mm) — 480 triangles;
//   * three mouldings (at 3, 6 and 9.5 m), each a curved band 0.3 m tall standing 0.1 m proud: its front, top and
//     bottom faces, 240 chords each — 3 x 1,440 triangles;
//   * 31 pilasters (the vertical strips at the bay joints): closed boxes 0.5 wide, 10 tall, 0.15 deep — 12 each.
// A ground quad (2 triangles).
//
// THE EXPECTATION. (1) THE CHORD RULE: a circle of radius R held within a sagitta e needs one chord per
// 2 acos(1 - e / R) ~ 2 sqrt(2 e / R) radians, so a curved face of angle theta needs n(e) = theta / (2 acos(1 - e/R))
// chords — triangles ~ 1 / sqrt(e), and e = tolerance x D / 1303.7 grows with the distance D: the face's triangles
// fall as 1 / sqrt(D) (2 per chord, evaluated chord by chord at each chord's own distance, never above authored).
// (2) A PROTRUDING BAND goes whole where its depth is under the view's error: the mouldings (0.1 m) past
// 130 m at Epic, the pilasters — "no vertical strip survives past the distance where a bay's depth is sub-pixel" —
// past 0.15 x 1303.7 = 196 m. The dolly stands on the arc's axis, eye (0, 5, 40 + d), looking at its middle.
// WHAT IT DOES NOT MODEL: a DAG group simplifies the face and the bands together (their chords share group borders);
// the strip's two triangles per chord are its floor (a strip cannot lose height); the end chords' grazing view.
#include "envbuild.h"

#include <cmath>
#include <cstdio>

namespace lab {
namespace envs {

namespace {

constexpr float kR = 40.0f, kTheta = 3.0f, kHeight = 10.0f;
constexpr unsigned kChords = 240;
constexpr int kBays = 30;
constexpr float kAngle0 = float(M_PI) * 0.5f - kTheta * 0.5f;

iris::Vec3 onArc(float r, float a, float y) { return iris::Vec3(std::cos(a) * r, y, std::sin(a) * r); }

/// a curved face: chords between radii r0 (at y0) and r1 (at y1), its normal `nrm(angle)`
void curvedFace(Arrays &a, float r0, float y0, float r1, float y1, int normalKind)
{
    const float dth = kTheta / float(kChords);
    for (unsigned i = 0; i < kChords; ++i) {
        const float t0 = kAngle0 + float(i) * dth, t1 = t0 + dth, tm = 0.5f * (t0 + t1);
        const iris::Vec3 radial(std::cos(tm), 0, std::sin(tm));
        const iris::Vec3 n = normalKind == 0 ? radial : normalKind == 1 ? iris::Vec3(0, 1, 0) : iris::Vec3(0, -1, 0);
        a.quad(onArc(r0, t0, y0), onArc(r0, t1, y0), onArc(r1, t1, y1), onArc(r1, t0, y1), n);
    }
}

Built build(unsigned seed, bool bake)
{
    Builder b("atom-arcade", seed, bake);
    Lcg r(seed);
    Arrays a;
    curvedFace(a, kR, 0.0f, kR, kHeight, 0);                                     // the face
    const float bandY[3] = { 3.0f, 6.0f, 9.5f };
    for (float y : bandY) {
        const float jitter = r.range(-0.1f, 0.1f);                                // the seed moves the mouldings
        const float y0 = y + jitter - 0.15f, y1 = y + jitter + 0.15f, rf = kR + 0.1f;
        curvedFace(a, rf, y0, rf, y1, 0);                                         // its front
        curvedFace(a, kR, y1, rf, y1, 1);                                         // its top
        curvedFace(a, rf, y0, kR, y0, 2);                                         // its bottom
    }
    std::vector<iris::Vec3> pilasters;
    for (int i = 0; i <= kBays; ++i) {
        const float t = kAngle0 + kTheta * float(i) / float(kBays);
        const iris::Vec3 w(std::cos(t), 0, std::sin(t)), u(-std::sin(t), 0, std::cos(t)), v(0, 1, 0);
        const iris::Vec3 c = onArc(kR + 0.075f, t, kHeight * 0.5f);
        a.orientedBox(c, u, v, w, iris::Vec3(0.25f, kHeight * 0.5f, 0.075f));
        pilasters.push_back(c);
    }
    const int obj = b.instance(b.mesh(a), QStringLiteral("arcade"), QStringLiteral("subject"), iris::Vec3(0, 0, 0), 0.0f,
                               iris::Vec3(1, 1, 1), b.material(0.78f, 0.72f, 0.64f));
    const auto face = [&](float r0, float y, float err, float halfHeight) {
        Feature f;
        f.kind = Feature::CurvedFace;
        f.object = obj;
        f.arcCentre = iris::Vec3(0, 0, 0);
        f.arcRadius = r0;
        f.angle0 = kAngle0;
        f.theta = kTheta;
        f.segments = kChords;
        f.faceY = y;
        const float halfChord = 0.5f * kTheta * kR / float(kChords);
        f.radius = std::sqrt(halfChord * halfChord + halfHeight * halfHeight);   // one chord's frustum sphere
        f.error = err;
        f.centre = onArc(r0, float(M_PI) * 0.5f, y);
        b.feature(f);
    };
    face(kR, kHeight * 0.5f, 0.0f, kHeight * 0.5f);
    for (float y : bandY) for (int k = 0; k < 3; ++k) face(kR + 0.1f, y, 0.1f, 0.15f);
    for (const iris::Vec3 &c : pilasters) {
        Feature f;
        f.kind = Feature::Shell;
        f.object = obj;
        f.centre = c;
        f.radius = iris::Vec3(0.25f, kHeight * 0.5f, 0.075f).length();
        f.error = 0.15f;
        f.triangles = 12;
        b.feature(f);
    }
    Arrays g;
    g.quad(iris::Vec3(-60, 0, 120), iris::Vec3(60, 0, 120), iris::Vec3(60, 0, -60), iris::Vec3(-60, 0, -60), iris::Vec3(0, 1, 0));
    const int go = b.instance(b.mesh(g), QStringLiteral("ground"), QStringLiteral("ground"), iris::Vec3(0, 0, 0), 0.0f,
                              iris::Vec3(1, 1, 1), b.material(0.35f, 0.35f, 0.33f), false);
    Feature gf;
    gf.kind = Feature::Fixed;
    gf.object = go;
    gf.centre = iris::Vec3(0, 0, 30);
    gf.radius = 100.0f;
    gf.triangles = 2;
    b.feature(gf);
    return b.finish();
}

void stop(double d, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 5, kR + float(d));
    target = iris::Vec3(0, 5, kR);
}

std::vector<Prediction> expect(const Built &b, const Optics &o)
{
    const double pil = 0.15 * o.pixelScale() / o.tolerance, mould = 0.1 * o.pixelScale() / o.tolerance;
    char note[160];
    std::snprintf(note, sizeof note, "the chord rule (tris ~ 1/sqrt(e)); mouldings gone past %.0f m, pilasters past %.0f m",
                  mould, pil);
    return { { "triangles", predictedTriangles(b, o), note } };
}

std::string describe()
{
    return "atom-arcade: a 120 m curved arcade (a 40 m radius arc of 30 bays: a 240-chord face, three 0.1 m mouldings, "
           "31 pilasters 0.15 m deep). Predicts the drawn triangles by THE CHORD RULE — a curve held within the error "
           "e needs theta / (2 acos(1 - e/R)) chords, so its triangles go as 1/sqrt(e) ~ 1/sqrt(distance) — and no "
           "vertical strip past the distance where its 0.15 m depth is under tolerance px (196 m at Epic; the "
           "mouldings 130 m).";
}

}   // namespace

Env atomArcade() { return Env{ "atom-arcade", describe, build, stop, expect, 900.0f, true }; }

}   // namespace envs
}   // namespace lab
