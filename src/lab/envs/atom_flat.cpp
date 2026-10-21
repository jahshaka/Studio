// atom-flat — a 200 m plaza of 2-triangle slabs with 10,000 pavement stones (LAB-1 stage 2; Bistro's ground made clean).
//
// THE GEOMETRY. 16 tiles of 50 m (four variants, the seed scatters each one's stones; the tiles cycle them), each ONE
// mesh of 25 slabs (10 m quads, 2 triangles each, separate) and 625 stones (closed boxes 0.3 x 0.3 m, 0.08 m tall,
// 12 triangles each) standing on them — 10,000 stones, 120,000 + 800 triangles.
//
// THE EXPECTATION. A stone's error is its height off the slab it stands on (the island-capped distance: 0.08 m, under
// its 0.43 m diagonal), so a stone draws while 0.08 x 1303.7 / D >= tolerance: at Epic the stones vanish past 104 m.
// A slab is a separate flat quad: removing it is a hole (its error is its own 14 m extent), and two triangles cannot
// simplify — THE SLABS STAY 2 TRIANGLES EVERYWHERE. The dolly: eye (0, 8, 100 + d) looking at the plaza's centre.
// THE SPEC'S "stones vanish by 300 m at 1 px" is the stone's WIDTH (0.3 m: 0.3 x 1303.7 = 391 m, ~300 m at the
// diagonal's half); the bake's metric is the height a dropped stone moves the surface by, which is what this predicts.
// WHAT IT DOES NOT MODEL: a DAG group of stones and slabs merging (a group's border holds slab edges: the stones of a
// group may outlive their own error until the group's), grazing-angle foreshortening of the stones' height.
#include "envbuild.h"

#include <cmath>

namespace lab {
namespace envs {

namespace {

constexpr float kTile = 50.0f, kSlab = 10.0f, kStoneHalf = 0.15f, kStoneH = 0.08f;
constexpr int kStonesPerTile = 625, kVariants = 4;

Built build(unsigned seed, bool bake)
{
    Builder b("atom-flat", seed, bake);
    std::vector<std::vector<iris::Vec3>> stones(kVariants);
    std::vector<iris::MeshPtr> meshes;
    for (int v = 0; v < kVariants; ++v) {
        Lcg r(seed * 7919u + unsigned(v) * 104729u + 3u);
        Arrays a;
        for (int sz = 0; sz < 5; ++sz)
            for (int sx = 0; sx < 5; ++sx) {
                const float x0 = -kTile * 0.5f + kSlab * float(sx), z0 = -kTile * 0.5f + kSlab * float(sz);
                a.quad(iris::Vec3(x0, 0, z0 + kSlab), iris::Vec3(x0 + kSlab, 0, z0 + kSlab), iris::Vec3(x0 + kSlab, 0, z0),
                       iris::Vec3(x0, 0, z0), iris::Vec3(0, 1, 0));
            }
        for (int s = 0; s < kStonesPerTile; ++s) {
            const iris::Vec3 c(r.range(-kTile * 0.5f + 0.2f, kTile * 0.5f - 0.2f), kStoneH * 0.5f,
                               r.range(-kTile * 0.5f + 0.2f, kTile * 0.5f - 0.2f));
            a.box(c, iris::Vec3(kStoneHalf, kStoneH * 0.5f, kStoneHalf));
            stones[size_t(v)].push_back(c);
        }
        meshes.push_back(b.mesh(a));
    }
    const auto paving = b.material(0.55f, 0.53f, 0.5f);
    for (int t = 0; t < 16; ++t) {
        const iris::Vec3 pos(-75.0f + kTile * float(t % 4), 0, -75.0f + kTile * float(t / 4));
        const int v = (t + int(seed)) % kVariants;
        const int o = b.instance(meshes[size_t(v)], QStringLiteral("tile-%1").arg(t), QStringLiteral("subject"), pos,
                                 0.0f, iris::Vec3(1, 1, 1), paving, true);
        for (int sz = 0; sz < 5; ++sz)
            for (int sx = 0; sx < 5; ++sx) {
                Feature f;
                f.kind = Feature::Fixed;
                f.object = o;
                f.centre = pos + iris::Vec3(-kTile * 0.5f + kSlab * (float(sx) + 0.5f), 0, -kTile * 0.5f + kSlab * (float(sz) + 0.5f));
                f.radius = kSlab * 0.7072f;
                f.triangles = 2;
                b.feature(f);
            }
        for (const iris::Vec3 &c : stones[size_t(v)]) {
            Feature f;
            f.kind = Feature::Shell;
            f.object = o;
            f.centre = pos + c;
            f.radius = iris::Vec3(kStoneHalf, kStoneH * 0.5f, kStoneHalf).length();
            f.error = kStoneH;
            f.triangles = 12;
            b.feature(f);
        }
    }
    return b.finish();
}

void stop(double d, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 8, 100.0f + float(d));
    target = iris::Vec3(0, 0, 0);
}

std::vector<Prediction> expect(const Built &b, const Optics &o)
{
    return { { "triangles", predictedTriangles(b, o), "slabs 2 each always + stones (0.08 m high) over tolerance px" } };
}

std::string describe()
{
    return "atom-flat: a 200 m plaza of 400 two-triangle slabs and 10,000 pavement stones of 0.3 m (0.08 m high). "
           "Predicts the drawn triangles: the slabs stay 2 triangles everywhere; a stone draws while its height off the "
           "slab projects to >= tolerance px (at Epic the stones vanish past 104 m; the spec's 300 m is the stone's "
           "width, not the bake's metric).";
}

}   // namespace

Env atomFlat() { return Env{ "atom-flat", describe, build, stop, expect, 900.0f, true }; }

}   // namespace envs
}   // namespace lab
