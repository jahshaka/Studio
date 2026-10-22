// atom-forest — 100 card-soup trees of two kinds over 200 m (LAB-1 stage 2; Bistro's foliage made clean).
//
// THE GEOMETRY. A 10 x 10 grid at a 20 m pitch (x, z in [-90, 90]). Two kinds, a checkerboard: kind A a canopy of
// 3,000 cards of 0.4 m in a 2.5 m sphere 5 m up; kind B 8,000 cards of 1.2 m in a 5 m sphere 8 m up (tests/meshbake's
// cardSoup, copied and parametrised: two-sided cards scattered and turned in a sphere) — 6,000 and 16,000 triangles —
// each on a trunk (a 12-triangle box). One canopy mesh per kind (the seed scatters its cards), one trunk mesh; each
// tree turned by the seed. A 220 m ground quad.
//
// THE EXPECTATION — THE REPRESENTED-SURFACE RULE IN A DENSE SOUP (the stage-2 read's correction). The bake charges a
// removed card its distance to ANY remaining surface — in a dense canopy the gap to its nearest neighbour, centimetres —
// not its own size: a card goes when that gap is under the view's error. So a canopy draws
//   T(D) = 2 x #{cards whose nearest-neighbour gap >= allowed(D)}
// (the gaps measured at build: each card's centre to the nearest other card's centre, a grid query — an upper bound of
// the surface gap), monotone in D: NEVER A STEP is the check. The owner's quarter-per-doubling (merged cards of one
// pixel) needs a MERGING stage the bake does not have; a wholly dropped card is charged its extent (env.h).
// A canopy STRADDLING the frustum's edge is left out of both columns (the cut culls it per cluster; the closed form
// would count it whole), its trunk with it. The dolly: eye (0, 12, 90 + d) looking at (0, 6, 0).
// WHAT IT DOES NOT MODEL: inter-tree occlusion (soups are porous: the depth test rarely proves one hidden), the DAG's
// extent charge on a wholly dropped card, the trunks' simplification (12 triangles: nothing to lose).
#include "envbuild.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace lab {
namespace envs {

namespace {

/// each card's nearest-neighbour gap (centre to the nearest other centre, metres), sorted ascending: a uniform grid
/// of cells the canopy's mean spacing wide, searched ring by ring until the nearest found cannot be beaten
std::shared_ptr<const std::vector<float>> nearestGaps(const Arrays &a)
{
    const size_t n = a.p.size() / 12;   // four vertices a card
    std::vector<iris::Vec3> c(n);
    for (size_t i = 0; i < n; ++i) {
        iris::Vec3 s(0, 0, 0);
        for (int k = 0; k < 4; ++k) s += iris::Vec3(a.p[(i * 4 + k) * 3], a.p[(i * 4 + k) * 3 + 1], a.p[(i * 4 + k) * 3 + 2]);
        c[i] = s * 0.25f;
    }
    iris::Vec3 mn(1e30f, 1e30f, 1e30f), mx(-1e30f, -1e30f, -1e30f);
    for (const iris::Vec3 &v : c) {
        mn = iris::Vec3(std::min(mn.x(), v.x()), std::min(mn.y(), v.y()), std::min(mn.z(), v.z()));
        mx = iris::Vec3(std::max(mx.x(), v.x()), std::max(mx.y(), v.y()), std::max(mx.z(), v.z()));
    }
    const iris::Vec3 ext = mx - mn;
    const float cell = std::max(1e-3f, std::cbrt(ext.x() * ext.y() * ext.z() / float(std::max<size_t>(1, n))));
    const int nx = int(ext.x() / cell) + 1, ny = int(ext.y() / cell) + 1, nz = int(ext.z() / cell) + 1;
    std::vector<std::vector<unsigned>> grid(size_t(nx) * ny * nz);
    const auto cellOf = [&](const iris::Vec3 &v, int &x, int &y, int &z) {
        x = std::min(nx - 1, int((v.x() - mn.x()) / cell)); y = std::min(ny - 1, int((v.y() - mn.y()) / cell));
        z = std::min(nz - 1, int((v.z() - mn.z()) / cell));
    };
    for (size_t i = 0; i < n; ++i) { int x, y, z; cellOf(c[i], x, y, z); grid[(size_t(z) * ny + y) * nx + x].push_back(unsigned(i)); }
    auto gaps = std::make_shared<std::vector<float>>(n, 0.0f);
    for (size_t i = 0; i < n; ++i) {
        int x, y, z; cellOf(c[i], x, y, z);
        float best = 1e30f;
        for (int r = 0; r < std::max(nx, std::max(ny, nz)); ++r) {
            if (best < float(r - 1) * cell) break;   // nothing in ring r can be nearer than (r - 1) cells
            for (int dz = -r; dz <= r; ++dz) for (int dy = -r; dy <= r; ++dy) for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::max(std::abs(dy), std::abs(dz))) != r) continue;
                const int X = x + dx, Y = y + dy, Z = z + dz;
                if (X < 0 || Y < 0 || Z < 0 || X >= nx || Y >= ny || Z >= nz) continue;
                for (unsigned j : grid[(size_t(Z) * ny + Y) * nx + X])
                    if (j != i) best = std::min(best, (c[j] - c[i]).length());
            }
        }
        (*gaps)[i] = best;
    }
    std::sort(gaps->begin(), gaps->end());
    return gaps;
}

struct Kind { unsigned cards = 0; float half = 0.0f, radius = 0.0f, y = 0.0f; };
constexpr Kind kKinds[2] = { { 3000, 0.2f, 2.5f, 5.0f }, { 8000, 0.6f, 5.0f, 8.0f } };

Built build(unsigned seed, bool bake)
{
    Builder b("atom-forest", seed, bake);
    Lcg r(seed);
    iris::MeshPtr canopy[2];
    std::shared_ptr<const std::vector<float>> gaps[2];
    for (int k = 0; k < 2; ++k) {
        Arrays a;
        cardSoup(a, kKinds[k].cards, kKinds[k].half, kKinds[k].radius, iris::Vec3(0, kKinds[k].y, 0), seed * 31u + unsigned(k) + 1u);
        canopy[k] = b.mesh(a);
        gaps[k] = nearestGaps(a);
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
        f.gaps = gaps[k];
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
    // the trees whose canopy straddles the frustum's edge: out of both columns (the canopy and its trunk)
    QSet<int> out;
    QStringList names;
    for (const Feature &f : b.features) {
        if (f.kind != Feature::Canopy || !o.straddles(f.centre, f.radius)) continue;
        for (int obj : { f.object, f.object - 1 }) {   // the canopy and the trunk instanced just before it
            out.insert(obj);
            names << b.objects[size_t(obj)].name;
        }
    }
    double median = 0.0;
    for (const Feature &f : b.features)
        if (f.kind == Feature::Canopy && f.gaps && !f.gaps->empty()) { median = (*f.gaps)[f.gaps->size() / 2]; break; }
    char note[200];
    std::snprintf(note, sizeof note, "the represented-surface rule: 2 x cards with a nearest-neighbour gap >= allowed(D); "
                  "%d straddling trees left out of both columns (kind A's median gap %.3f m)", int(out.size() / 2), median);
    Prediction p{ "triangles", predictedTriangles(b, o, QString(), out), note, names };
    return { p };
}

std::string describe()
{
    return "atom-forest: 100 card-soup trees of two kinds over 200 m (3,000 cards of 0.4 m / 8,000 cards of 1.2 m). "
           "Predicts the drawn triangles by THE REPRESENTED-SURFACE RULE in a dense soup: a card goes when the gap to "
           "its nearest neighbour is under the view's error, so a canopy draws 2 x #cards with gap >= allowed(D) — "
           "monotone, never a step; trees straddling the frustum's edge left out of both columns.";
}

}   // namespace

Env atomForest() { return Env{ "atom-forest", describe, build, stop, expect, 900.0f, true }; }

}   // namespace envs
}   // namespace lab
