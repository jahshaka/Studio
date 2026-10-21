// atom-forest — 100 card-soup trees of two kinds over 200 m (LAB-1 stage 2; Bistro's foliage made clean).
//
// THE GEOMETRY. A 10 x 10 grid at a 20 m pitch (x, z in [-90, 90]). Two kinds, a checkerboard: kind A a canopy of
// 3,000 cards of 0.4 m in a 2.5 m sphere 5 m up; kind B 8,000 cards of 1.2 m in a 5 m sphere 8 m up (tests/meshbake's
// cardSoup, copied and parametrised: two-sided cards scattered and turned in a sphere) — 6,000 and 16,000 triangles —
// each on a trunk (a 12-triangle box). One canopy mesh per kind (the seed scatters its cards), one trunk mesh; each
// tree turned by the seed. A 220 m ground quad.
//
// THE EXPECTATION — THE MERGED-CARD LAW. While a card's size c projects to >= tolerance pixels every card stays:
// 2 N triangles. Past d_c = c x 1303.7 / tolerance a card is sub-pixel and the cut may merge cards into cards of one
// tolerance pixel; a canopy of fixed area then needs (d_c / D)^2 of them — T(D) = 2 N min(1, (d_c / D)^2): a quarter
// per doubling of distance once past the card size, continuous at d_c — NEVER A STEP. At Epic d_c = 521 m (0.4 m cards)
// and 1,564 m (1.2 m cards): inside this dolly's 354 m the law predicts NO reduction at all, and every triangle the
// measured curve sheds before d_c is the simplifier's own (overlapping cards whose removal moves the surface by less
// than a pixel) — the number this baseline exists to show. The dolly: eye (0, 12, 90 + d) looking at (0, 6, 0).
// WHAT IT DOES NOT MODEL: inter-tree occlusion (soups are porous: the depth test rarely proves one hidden) and the
// trunks' own simplification (they are 12 triangles: nothing to lose).
#include "envbuild.h"

#include <cstdio>

namespace lab {
namespace envs {

namespace {

struct Kind { unsigned cards = 0; float half = 0.0f, radius = 0.0f, y = 0.0f; };
constexpr Kind kKinds[2] = { { 3000, 0.2f, 2.5f, 5.0f }, { 8000, 0.6f, 5.0f, 8.0f } };

Built build(unsigned seed, bool bake)
{
    Builder b("atom-forest", seed, bake);
    Lcg r(seed);
    iris::MeshPtr canopy[2];
    for (int k = 0; k < 2; ++k) {
        Arrays a;
        cardSoup(a, kKinds[k].cards, kKinds[k].half, kKinds[k].radius, iris::Vec3(0, kKinds[k].y, 0), seed * 31u + unsigned(k) + 1u);
        canopy[k] = b.mesh(a);
    }
    Arrays trunkA;
    trunkA.box(iris::Vec3(0, 2.0f, 0), iris::Vec3(0.25f, 2.0f, 0.25f));
    const iris::MeshPtr trunk = b.mesh(trunkA);
    const auto leaves = b.material(0.18f, 0.32f, 0.12f, 0.8f), bark = b.material(0.30f, 0.22f, 0.15f);
    for (int i = 0; i < 100; ++i) {
        const int gx = i % 10, gz = i / 10, k = (gx + gz) % 2;
        const iris::Vec3 pos(-90.0f + 20.0f * float(gx), 0.0f, -90.0f + 20.0f * float(gz));
        const float yaw = r.range(0.0f, 360.0f);
        const int t = b.instance(trunk, QStringLiteral("trunk-%1").arg(i), QStringLiteral("trunk"), pos, yaw,
                                 iris::Vec3(1, 1, 1), bark);
        Feature tf;
        tf.kind = Feature::Fixed;
        tf.object = t;
        tf.centre = pos + iris::Vec3(0, 2, 0);
        tf.radius = 2.1f;
        tf.triangles = 12;
        b.feature(tf);
        const int c = b.instance(canopy[k], QStringLiteral("canopy-%1").arg(i), QStringLiteral("subject"), pos, yaw,
                                 iris::Vec3(1, 1, 1), leaves, true, true);
        Feature f;
        f.kind = Feature::Canopy;
        f.object = c;
        f.centre = pos + iris::Vec3(0, kKinds[k].y, 0);
        f.radius = kKinds[k].radius + kKinds[k].half * 1.415f;
        f.cards = kKinds[k].cards;
        f.cardSize = 2.0f * kKinds[k].half;
        b.feature(f);
    }
    Arrays g;
    g.quad(iris::Vec3(-110, 0, 110), iris::Vec3(110, 0, 110), iris::Vec3(110, 0, -110), iris::Vec3(-110, 0, -110),
           iris::Vec3(0, 1, 0));
    const int go = b.instance(b.mesh(g), QStringLiteral("ground"), QStringLiteral("ground"), iris::Vec3(0, 0, 0), 0.0f,
                              iris::Vec3(1, 1, 1), b.material(0.25f, 0.22f, 0.18f), false);
    Feature gf;
    gf.kind = Feature::Fixed;
    gf.object = go;
    gf.centre = iris::Vec3(0, 0, 0);
    gf.radius = 156.0f;
    gf.triangles = 2;
    b.feature(gf);
    return b.finish();
}

void stop(double d, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 12, 90.0f + float(d));
    target = iris::Vec3(0, 6, 0);
}

std::vector<Prediction> expect(const Built &b, const Optics &o)
{
    char note[160];
    std::snprintf(note, sizeof note, "the merged-card law: 2N min(1,(dc/D)^2); dc = %.0f m (0.4 m cards), %.0f m (1.2 m)",
                  0.4 * o.pixelScale() / o.tolerance, 1.2 * o.pixelScale() / o.tolerance);
    return { { "triangles", predictedTriangles(b, o), note } };
}

std::string describe()
{
    return "atom-forest: 100 card-soup trees of two kinds over 200 m (3,000 cards of 0.4 m / 8,000 cards of 1.2 m). "
           "Predicts the drawn triangles by THE MERGED-CARD LAW: every card stays while its size projects to >= "
           "tolerance px; past d_c = c x 1303.7 / tolerance the canopy needs 2N (d_c/D)^2 — a quarter per doubling, "
           "never a step (at Epic d_c = 521 m and 1,564 m: no reduction is owed inside 354 m).";
}

}   // namespace

Env atomForest() { return Env{ "atom-forest", describe, build, stop, expect, 900.0f, true }; }

}   // namespace envs
}   // namespace lab
