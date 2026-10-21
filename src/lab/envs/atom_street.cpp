// atom-street — a 400 m straight street of 40 kit-bashed facades (LAB-1 stage 2; the Bistro kit-bash shape made clean).
//
// THE GEOMETRY. The street runs from z = 0 to z = -400 between two rows of 20 facades (20 m wide, 12 m tall, their
// fronts at x = -8 and x = +8, facing the street). A facade is ONE mesh of 24-50 SEPARATE closed shells (each a 12-
// triangle box): the wall (20 x 12 x 0.5), a door (1.4 x 2.6, 0.2 deep), 12 window reveals (3 storeys x 4; 1.2 x 1.6,
// 0.15 deep), shutters of 0.6 x 1.6 (0.05 deep) on 4-12 of the windows (a pair each), a cornice (20 x 0.2, 0.4 deep),
// and a railing standing 0.6 m off the wall on the first storey: a top rail and 0-10 bars of 0.05 m. Eight facade
// variants (the seed picks each one's shutters and bars), the 40 instances cycling them. A 24 x 420 m road (2 triangles).
//
// THE EXPECTATION (env.h's one-pixel rule over the feature table). A shell's error e is its separation from the wall
// (the bake's island-capped term): door 0.2, reveal 0.15, shutter 0.05, cornice 0.4, rail and bars 0.6 (their stand-off;
// under their extents). A facade at distance D therefore draws its wall (12, always), plus 12 triangles per shell
// with e x 1303.7 / D >= tolerance (at 1080 rows, 45 degrees); at Epic (tolerance 1) the shells go in this order:
// shutters past 65 m, reveals past 196 m, the door past 261 m, the cornice past 521 m, the railing past 782 m — per
// facade a STAIRCASE in D, and the street's total, summed over facades from D to D + 400, the staircase smeared by the
// street's depth. The prediction is that sum, frustum-culled (the dolly backs the camera out of the street's near end,
// eye (0, 3, d), looking down it).
// WHAT IT DOES NOT MODEL: the cut draws CLUSTERS of up to 128 triangles grouped by the DAG, not shells — a group that
// holds a shutter and a reveal keeps both until the larger error drops; a facade's coarsest cut is the DAG's root, not
// its wall's 12 triangles. The 1/d^2 the spec names for the far half is the area law of a continuous surface; a facade
// of discrete shells follows its staircase instead — the measured curve against this one is the finding.
#include "envbuild.h"

#include <cstdio>

namespace lab {
namespace envs {

namespace {

constexpr int kFacades = 40, kVariants = 8;
constexpr float kWidth = 20.0f, kHeight = 12.0f, kStreetHalf = 8.0f;

struct Shell { iris::Vec3 c, h; float error; };

std::vector<Shell> facadeShells(unsigned seed)
{
    Lcg r(seed);
    std::vector<Shell> s;
    s.push_back({ iris::Vec3(0, kHeight * 0.5f, -0.25f), iris::Vec3(kWidth * 0.5f, kHeight * 0.5f, 0.25f), 0.0f });  // wall
    s.push_back({ iris::Vec3(-6.0f, 1.3f, 0.1f), iris::Vec3(0.7f, 1.3f, 0.1f), 0.2f });                                 // door
    const float rows[3] = { 4.5f, 7.5f, 10.5f }, cols[4] = { -7.5f, -2.5f, 2.5f, 7.5f };
    std::vector<int> windows;
    for (float y : rows)
        for (float x : cols) {
            s.push_back({ iris::Vec3(x, y, 0.075f), iris::Vec3(0.6f, 0.8f, 0.075f), 0.15f });                          // reveal
            windows.push_back(int(s.size()) - 1);
        }
    const int shuttered = r.irange(4, 12);
    for (int i = 0; i < shuttered; ++i) {
        const Shell &w = s[size_t(windows[size_t(i)])];
        for (float side : { -1.0f, 1.0f })
            s.push_back({ iris::Vec3(w.c.x() + side * 0.95f, w.c.y(), 0.025f), iris::Vec3(0.3f, 0.8f, 0.025f), 0.05f });
    }
    s.push_back({ iris::Vec3(0, kHeight - 0.1f, 0.2f), iris::Vec3(kWidth * 0.5f, 0.1f, 0.2f), 0.4f });              // cornice
    s.push_back({ iris::Vec3(3.0f, 4.2f, 0.6f), iris::Vec3(3.0f, 0.025f, 0.025f), 0.6f });                           // top rail
    const int bars = r.irange(0, 10);
    for (int i = 0; i < bars; ++i)
        s.push_back({ iris::Vec3(0.25f + 0.55f * float(i), 3.7f, 0.6f), iris::Vec3(0.025f, 0.5f, 0.025f), 0.6f });   // bar
    return s;
}

Built build(unsigned seed, bool bake)
{
    Builder b("atom-street", seed, bake);
    std::vector<std::vector<Shell>> variants;
    std::vector<iris::MeshPtr> meshes;
    for (int v = 0; v < kVariants; ++v) {
        variants.push_back(facadeShells(seed * 977u + unsigned(v) * 131u + 7u));
        Arrays a;
        for (const Shell &sh : variants.back()) a.box(sh.c, sh.h);
        meshes.push_back(b.mesh(a));
    }
    const auto stone = b.material(0.62f, 0.58f, 0.52f), plaster = b.material(0.75f, 0.70f, 0.62f);
    for (int i = 0; i < kFacades; ++i) {
        const bool left = i % 2 == 0;
        const int along = i / 2;
        const iris::Vec3 pos(left ? -kStreetHalf : kStreetHalf, 0.0f, -kWidth * 0.5f - kWidth * float(along));
        const float yaw = left ? 90.0f : -90.0f;
        const int v = (i + int(seed)) % kVariants;
        const int obj = b.instance(meshes[size_t(v)], QStringLiteral("facade-%1").arg(i), QStringLiteral("subject"), pos,
                                   yaw, iris::Vec3(1, 1, 1), (i / 2) % 2 ? stone : plaster);
        for (const Shell &sh : variants[size_t(v)]) {
            Feature f;
            f.kind = sh.error > 0.0f ? Feature::Shell : Feature::Fixed;
            f.object = obj;
            f.centre = place(sh.c, pos, yaw, iris::Vec3(1, 1, 1));
            f.radius = sh.h.length();
            f.error = sh.error > 0.0f ? std::min(sh.error, 2.0f * sh.h.length()) : 0.0f;
            f.triangles = 12;
            b.feature(f);
        }
    }
    Arrays road;
    road.quad(iris::Vec3(-12, 0, 10), iris::Vec3(12, 0, 10), iris::Vec3(12, 0, -410), iris::Vec3(-12, 0, -410),
              iris::Vec3(0, 1, 0));
    const int g = b.instance(b.mesh(road), QStringLiteral("road"), QStringLiteral("ground"), iris::Vec3(0, 0, 0), 0.0f,
                             iris::Vec3(1, 1, 1), b.material(0.3f, 0.3f, 0.3f), false);
    Feature f;
    f.kind = Feature::Fixed;
    f.object = g;
    f.centre = iris::Vec3(0, 0, -200);
    f.radius = 210.0f;
    f.triangles = 2;
    b.feature(f);
    return b.finish();
}

void stop(double d, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 3, float(d));
    target = iris::Vec3(0, 3, -400);
}

std::vector<Prediction> expect(const Built &b, const Optics &o)
{
    return { { "triangles", predictedTriangles(b, o), "the feature table's shells over tolerance px + the walls + the road" } };
}

std::string describe()
{
    return "atom-street: a 400 m street of 40 kit-bashed facades (24-50 separate 12-triangle shells each: wall, door, "
           "12 reveals, 0.6 x 1.6 shutters, a 0.2 m cornice, 0.05 m railing bars). Predicts the drawn triangles: per "
           "facade its wall plus every shell whose error (its depth off the wall: shutter 0.05, reveal 0.15, door 0.2, "
           "cornice 0.4, railing 0.6 m) projects to >= tolerance px — at Epic shutters go past 65 m, reveals past 196 m, "
           "the door past 261 m, the cornice past 521 m — summed over the street seen from its near end.";
}

}   // namespace

Env atomStreet() { return Env{ "atom-street", describe, build, stop, expect, 900.0f, true }; }

}   // namespace envs
}   // namespace lab
