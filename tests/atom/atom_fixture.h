#pragma once
// atom_fixture — THE ONE HELPER OF THE ATOM ROWS AND THEIR VIEW LIST (ATOM-TESTS-BACK-1, SPECS/ATOM_TESTS_BACK_1_SPEC.md "THE ONE
// HELPER AND THE VIEW LIST"). Two halves, one definition each:
//   THE VIEW LIST (CPU): a view is a LENS (its frame's half-angle tangents, its target, perspective or orthographic) and an
//   AIM (the NDC point the instance is placed on); the eye at the origin looking down -Z. Each view's ClusterCutView, a
//   group's flip placement (placeFlip) and its switch by the independent measure (readFlip, tests/support/
//   surfacedistance.h). Every law takes the views as a list and asserts PER VIEW: plan 3.6 adds an eye and rewrites no law.
//   THE RIG (card): one engine through enginetest::testEngineConfig (FIXTURE-CONFIG-1), N offscreen views of ONE scene with
//   the REQUIRED OffscreenContract (Engine.h:2049-2055; ROUTE-ROWS-1 deletes the argument: one line here), each view's
//   camera placed from a Placement, and readAtomIds per view. No row reads the cut probe or the status but for the first
//   view (Engine.h:914-919: they read the first view only).
#include "../support/enginetesthelpers.h"
#include "../support/surfacedistance.h"
#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Mesh.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace atomfix {

using namespace jahshaka::engine;
namespace sd = surfacedistance;

// ---- THE VIEWS -----------------------------------------------------------------------------------------------------
// A view is a LENS (its tangents at the frame's edges, its target size, perspective or orthographic) and an AIM (the NDC
// point of the frame the instance is placed on). The eye sits at the origin looking down -Z.

/// THE HEADSET EYE'S CORNER (the lead's call C2): tangents (1.0, 1.0) at the eye buffer's corner = a 90 x 90 degree eye,
/// corner ratio 1 + tx^2 + ty^2 = 3.0. UNVERIFIED until plan 3.6 measures a headset's eye (its own projection).
constexpr double kEyeCornerTan[2] = { 1.0, 1.0 };

struct Lens
{
    const char *name;
    double tanX, tanY;     ///< the tangents of the half-angles at the frame's right and top edges
    double width, height;  ///< the target, pixels (square pixels: width / height = tanX / tanY)
    bool ortho;
    double aimX, aimY;     ///< NDC point the instance is placed on
};
/// The desktop view: 45 degrees vertical, 16:9, 1920 x 1080.
constexpr double kDeskTanY = 0.41421356237309503;   // tan(22.5 deg)
inline const Lens kCentre = { "centre", kDeskTanY * 16.0 / 9.0, kDeskTanY, 1920.0, 1080.0, false, 0.0, 0.0 };
inline const Lens kCorner = { "corner", kDeskTanY * 16.0 / 9.0, kDeskTanY, 1920.0, 1080.0, false, 1.0, 1.0 };
/// The eye buffer's size is not a law term (every ratio here is size-free); 1080 x 1080 for readable numbers.
inline const Lens kEyeCorner = { "eye_corner", kEyeCornerTan[0], kEyeCornerTan[1], 1080.0, 1080.0, false, 1.0, 1.0 };
/// The orthographic view: a window 4 extents tall (projScaleY = 2 / window: the footprint is window / height).
inline const Lens kOrtho = { "ortho", 0.0, 0.0, 1920.0, 1080.0, true, 0.0, 0.0 };

/// The unit direction through the lens's aim point.
inline void aimDirection(const Lens &l, double d[3])
{
    d[0] = l.aimX * l.tanX;
    d[1] = l.aimY * l.tanY;
    d[2] = -1.0;
    const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (int k = 0; k < 3; ++k) d[k] /= n;
}

/// The cut's view of ONE instance: identity rotation, scale 1, translated by `t`; the lens's projection.
inline ClusterCutView cutView(const Lens &l, const double t[3], float tolerance, float orthoWindow)
{
    ClusterCutView v;
    for (int r = 0; r < 3; ++r) v.worldRow[r][3] = float(t[r]);
    v.scale = 1.0f;
    v.tolerance = tolerance;
    v.orthographic = l.ortho;
    v.projScaleY = l.ortho ? 2.0f / orthoWindow : float(1.0 / l.tanY);
    v.viewportHeight = float(l.height);
    return v;
}

/// The translation that puts the mesh's box centre `c` at distance `dist` along the lens's aim.
inline void placeAt(const Lens &l, const float c[3], double dist, double t[3])
{
    double d[3];
    aimDirection(l, d);
    for (int k = 0; k < 3; ++k) t[k] = d[k] * dist - double(c[k]);
}

namespace sd = surfacedistance;

/// The window's radius: this fraction of the frame's half-height about the aim's pixel ("within 10 % of the frame
/// centre", the spec's centre row; the same disc at a corner, its in-frame quarter).
constexpr double kWindowFraction = 0.10;

/// THE MEASURE's PRECISION in pixels: 1 % of the reading, and nothing under a hundredth of a pixel resolved (neither can
/// move a law whose bars are 1 px and its multiples); in world units (the bake's step beside it): 0.1 %, and a millionth of
/// the extent (under the bake's own floor, 1e-5 of its largest axis).
constexpr double kPixelRel = 1e-2, kPixelFloor = 1e-2, kWorldRel = 1e-3, kWorldFloorOfExtent = 1e-6;

/// Group g's members (outputs false) or the clusters it produced (true) as triangles, the mesh translated by `t`.
inline std::vector<sd::Tri> groupTris(const MeshData &d, int g, bool outputs, const double t[3])
{
    std::vector<sd::Tri> out;
    for (const MeshCluster &c : d.clusters) {
        if ((outputs ? c.refined : c.group) != g) continue;
        for (unsigned i = 0; i + 2 < c.indexCount; i += 3) {
            sd::Tri tri;
            for (int k = 0; k < 3; ++k) {
                const unsigned v = d.clusterIndices[c.firstIndex + i + unsigned(k)];
                for (int a = 0; a < 3; ++a) tri.p[k][size_t(a)] = double(d.positions[size_t(v) * 3 + size_t(a)]) + t[a];
            }
            out.push_back(tri);
        }
    }
    return out;
}

inline sd::View pixelView(const Lens &l)
{
    sd::View v;
    v.tanX = l.tanX;
    v.tanY = l.tanY;
    v.width = l.width;
    v.height = l.height;
    v.winX = (l.aimX * 0.5 + 0.5) * l.width;
    v.winY = (l.aimY * 0.5 + 0.5) * l.height;
    v.winR = kWindowFraction * 0.5 * l.height;
    return v;
}

/// ONE FLIP of one group in one view, read by the independent measure.
struct Flip
{
    int group = -1, depth = 0;
    double t = 0.0;          ///< the flip's tolerance by the product's own footprint (clusterGroupFootprint), px
    double px = 0.0;         ///< switch_px: the attained reading (surface term, dropped islands), px
    double upper = 0.0;      ///< its certified upper bound, px - what the no-pop laws judge
    double surface = 0.0, islands = 0.0;
    double literal = 0.0;    ///< the largest literal |proj(p) - proj(q)| over every point sampled, px
    bool counted = false;    ///< some point of the switch projected in the window
};

/// Where one group flips at `tol` in a lens: the translation carrying mesh space into the eye's (the instance at the
/// origin, identity, scale 1; its sphere's centre on the aim's ray at D_c = r + error P11 H / (2 tol)), and the flip's t by
/// the product's own footprint (Mesh.h clusterGroupFootprint: t*_g = error x s / F_g).
struct Placement
{
    double translate[3] = { 0.0, 0.0, 0.0 };
    double t = 0.0;
};
inline Placement placeFlip(const MeshClusterGroup &gr, const Lens &l, double tol)
{
    Placement p;
    double dir[3];
    aimDirection(l, dir);
    const double dc = double(gr.radius) + double(gr.error) * (1.0 / l.tanY) * l.height / (2.0 * tol);
    for (int k = 0; k < 3; ++k) p.translate[k] = dir[k] * dc - double(gr.centre[k]);
    const ClusterCutView cv = cutView(l, p.translate, float(tol), 0.0f);
    const float F = clusterGroupFootprint(gr, cv);
    p.t = F > 0.0f ? double(gr.error) * double(cv.scale) / double(F) : 0.0;
    return p;
}

/// Group g's flip at `tol` in a lens, read by the independent measure.
inline Flip readFlip(const MeshData &d, int g, const Lens &l, double tol)
{
    const MeshClusterGroup &gr = d.clusterGroups[size_t(g)];
    const Placement p = placeFlip(gr, l, tol);
    const sd::View view = pixelView(l);
    Flip fl;
    fl.group = g;
    fl.depth = gr.depth;
    fl.t = p.t;
    const sd::Switch s = sd::measure(groupTris(d, g, false, p.translate), groupTris(d, g, true, p.translate), &view,
                                     kPixelRel, kPixelFloor);
    fl.surface = s.surface();
    fl.islands = s.islands;
    fl.px = s.total();
    fl.upper = s.upper();
    fl.counted = s.aToB.any || s.bToA.any || s.islands > 0.0;
    fl.literal = std::max(s.aToB.literal, s.bToA.literal);
    return fl;
}

/// Every finite group's flip at `tol` in a lens.
inline std::vector<Flip> flipsAt(const MeshData &d, const Lens &l, double tol)
{
    std::vector<Flip> out;
    for (size_t g = 0; g < d.clusterGroups.size(); ++g) {
        const MeshClusterGroup &gr = d.clusterGroups[g];
        if (!(gr.error > 0.0f) || gr.error >= kClusterTerminal) continue;
        out.push_back(readFlip(d, int(g), l, tol));
    }
    return out;
}

/// A lens at another target size (the same angles).
inline Lens sized(const Lens &l, double width, double height)
{
    Lens o = l;
    o.width = width;
    o.height = height;
    return o;
}

// ---- THE RIG (card rows) --------------------------------------------------------------------------------------------
struct RigView
{
    Lens lens;
    View *view = nullptr;
};
struct Rig
{
    std::unique_ptr<Engine> engine;
    Scene *scene = nullptr;
    std::vector<RigView> views;
    std::string error;
};

/// The engine, one offscreen view a lens (its target the lens's size; the views FIRST: Ogre's startup order), one scene
/// all of them show, the post chain allowed offscreen (the id pass's chain). False with `error` set.
inline bool boot(Rig &r, const std::string &logFile, const std::vector<Lens> &lenses)
{
    const auto cfg = enginetest::testEngineConfig(logFile);
    r.engine = Engine::create(cfg, r.error);
    if (!r.engine) return false;
    for (const Lens &l : lenses) {
        RigView v;
        v.lens = l;
        v.view = r.engine->createOffscreenView(std::string("atom-") + l.name, unsigned(l.width), unsigned(l.height),
                                               Colour(0.0f, 0.0f, 0.0f), OffscreenContract::Live);
        if (!v.view) { r.error = std::string("no offscreen view ") + l.name; return false; }
        r.views.push_back(v);
    }
    r.scene = r.engine->createScene("atom-rig");
    if (!r.scene) { r.error = "no scene"; return false; }
    for (RigView &v : r.views) {
        v.view->setScene(r.scene);
        PostFxDesc fx;
        fx.allowOffscreen = true;
        v.view->setPostFx(fx);
    }
    return true;
}

/// The camera of view `v` for a placement: the instance sits at the origin (identity), so the eye stands at -translate,
/// looking down -Z (identity orientation), the lens's vertical fov.
inline void placeCamera(RigView &v, const Placement &p)
{
    CameraDesc c;
    c.position = Vec3(float(-p.translate[0]), float(-p.translate[1]), float(-p.translate[2]));
    c.orientation = Quat();
    c.fovDegrees = float(2.0 * std::atan(v.lens.tanY) * 180.0 / 3.14159265358979323846);
    c.nearClip = 0.01f;
    c.farClip = 1000.0f;
    v.view->setCamera(c);
}

/// One view's id image (Engine.h readAtomIds: two words a pixel, x = slot | depth << 24, y = cluster << 8 | triangle),
/// row-major. WHERE NOTHING DREW, x is AtomId::kEmpty = 0xFFFFFFFF and y is 0 (the pass's clear: OgreAtomIdPass.cpp:964-965,
/// HlmsAtom.cpp:201-202) - so coverage is read from x, never y (a y of 0 is also cluster 0's triangle 0). y >> 8 is the
/// MESH-LOCAL cluster index (MeshData::clusters' order): read on this row's first card run, 2026-11-22 - the group's
/// members 345, 346 before and its output 347 after.
struct IdImage
{
    std::vector<uint32_t> words;
    unsigned width = 0, height = 0;
    bool ok = false;
    bool covered(size_t i) const { return words[2 * i] != 0xFFFFFFFFu; }
    uint32_t cluster(size_t i) const { return words[2 * i + 1] >> 8; }
};
inline IdImage readIds(Rig &r, const RigView &v)
{
    IdImage im;
    im.ok = r.engine->readAtomIds(v.view, im.words, im.width, im.height) && im.words.size() == size_t(im.width) * im.height * 2;
    return im;
}

inline void shutdown(Rig &r)
{
    for (RigView &v : r.views) r.engine->destroyView(v.view);
    r.views.clear();
    if (r.scene) r.engine->destroyScene(r.scene);
    r.scene = nullptr;
    r.engine.reset();
}

}   // namespace atomfix
