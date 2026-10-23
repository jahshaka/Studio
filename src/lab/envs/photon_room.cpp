// photon-room — Photon's closed-form room (LAB-1 stage 2), and its reflective sibling photon-glass (PHOTON-2 stage 0,
// below). Not dollied (their laws do not move with the distance); `--env-list` prints the law at every stop, and the
// Photon specs measure them (the stillness door reads them under motion).
//
// photon-room — AN ENCLOSED ROOM. An 8 m cube room (walls 0.3 m thick, inside x, z in [-4, 4], y in [0, 8]) with ONE
// 1 x 1 m opening in its +Z wall, a lamp (kDefaultLampIntensity) 6 m up, every surface matte at albedo rho = 0.5.
// THE EXPECTATION. (1) THE BOUNCE RESIDUAL (ENGINE.md PHOTON-5: "bounces run to a residual"): the light volume's energy
// ratio between two passes r = E2 / E1 is, for an enclosed diffuse room, the fraction of each bounce's light that
// lands on a wall again and is re-emitted: r = rho (1 - A_open / A_inside) = 0.5 x (1 - 1 / 384) = 0.4987, and the
// injection runs n = ceil(log(0.02 (1 - r)) / log r) = 7 passes (at most 8) — the residual of the geometric series
// then under 2 %; the multi-bounce gain 1 / (1 - r) = 1.995. (2) THE PROBE KEEP RULE (kProbeFacesOfGeometry = 2 of six
// faces): every probe inside a sealed room photographs geometry in six directions — ALL of them are kept.
//
#include "envbuild.h"

#include "irisgl/document/scenegraph/cameralens.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include <cmath>

namespace lab {
namespace envs {

namespace {

constexpr float kRho = 0.5f;

// ---- photon-room ---------------------------------------------------------------------------------------------------

/// THE ROOM'S SHELL (photon-room's, and photon-glass's around its fixtures): the eight walls, the opening, the lamp.
void roomShell(Builder &b, const iris::MeshPtr &unit, const iris::PbrMaterialPtr &matte)
{
    using V = iris::Vec3;
    const float t = 0.3f, h = 4.0f, top = 8.0f;
    fixedBox(b, unit, "floor", V(-h - t, -t, -h - t), V(h + t, 0, h + t), matte);
    fixedBox(b, unit, "ceiling", V(-h - t, top, -h - t), V(h + t, top + t, h + t), matte);
    fixedBox(b, unit, "wall-x0", V(-h - t, 0, -h), V(-h, top, h), matte);
    fixedBox(b, unit, "wall-x1", V(h, 0, -h), V(h + t, top, h), matte);
    fixedBox(b, unit, "wall-z0", V(-h, 0, -h - t), V(h, top, -h), matte);
    // the +Z wall around its 1 x 1 m opening (x in [-0.5, 0.5], y in [3.5, 4.5])
    fixedBox(b, unit, "wall-z1-left", V(-h, 0, h), V(-0.5f, top, h + t), matte);
    fixedBox(b, unit, "wall-z1-right", V(0.5f, 0, h), V(h, top, h + t), matte);
    fixedBox(b, unit, "wall-z1-below", V(-0.5f, 0, h), V(0.5f, 3.5f, h + t), matte);
    fixedBox(b, unit, "wall-z1-above", V(-0.5f, 4.5f, h), V(0.5f, top, h + t), matte);
    auto lamp = iris::LightNode::create();
    lamp->setLightType(iris::LightType::Point);
    lamp->setName(QStringLiteral("lamp"));
    b.built().doc->getRootNode()->addChild(lamp);
    lamp->setLocalPos(V(0, 6, 0));
    lamp->setPropertyValue(QStringLiteral("intensity"), iris::lens::kDefaultLampIntensity);
    lamp->setPropertyValue(QStringLiteral("distance"), 20.0f);
}

Built buildRoom(unsigned seed, bool bake)
{
    Builder b("photon-room", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    roomShell(b, unit, b.material(kRho, kRho, kRho, 1.0f));
    return b.finish();
}

// ---- photon-glass (PHOTON-2 stage 0) --------------------------------------------------------------------------------
//
// THE REFLECTIVE FIXTURE OF THE STILLNESS DOOR, in photon-room's sealed shell — a sibling, not photon-room itself:
// photon-room's closed form (the bounce residual r = rho (1 - A_open / A)) holds only while every surface is matte
// rho; a metal pane voids it. What each piece is there for (SPECS/PHOTON_2_SPEC.md stage 0, the preflight's §2):
//   * THE GLOSSY PANE PAIR — two metal floor panes, perceptual roughness 0.1 and 0.3 (both under the 0.4 cut where
//     the ray tier owns the glossy term): the reflection history's motion rules act on them;
//   * FOUR MATTE PILLARS standing on the panes 1.4 m in front of the far wall — THE REFLECTED DEPTH STRUCTURE: a
//     moving eye's reflection history is right only where its virtual point (the surface plus the mean ray length,
//     rq_reflect_temporal.comp's hit history) names the same reflected thing, and the neighbourhood clamp answers
//     where it does not — a pane reflecting one flat wall never tests either; the pillars' silhouettes over the wall
//     8 m behind them do (fixture B's shape: a glossy floor with occluders standing on it);
//   * A MIRROR on the far wall facing the room (metal, roughness 0): a mirror takes each frame whole, so every
//     stochastic term of its hit shading shows raw;
//   * ONE DECLARED MOVER (Movable, standing still): the ray tier's mover gate opens only in a scene with movers
//     (OgreRayQuery.cpp rayMoversOf), and with it the per-frame-rotated mover-visibility rays a mirror shows; a still
//     mover keeps every cache's rest (a moving one would re-trace the cards' movers term every frame).
// THE EXPECTATION: the keep rule only — every probe of the sealed shell sees geometry in >= 2 faces.
Built buildGlass(unsigned seed, bool bake)
{
    Builder b("photon-glass", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    const auto matte = b.material(kRho, kRho, kRho, 1.0f);
    roomShell(b, unit, matte);
    using V = iris::Vec3;
    fixedBox(b, unit, "pane-r0.1", V(-3.6f, 0, -3.6f), V(-0.2f, 0.02f, 1.0f), b.material(0.9f, 0.9f, 0.9f, 0.1f, 1.0f));
    fixedBox(b, unit, "pane-r0.3", V(0.2f, 0, -3.6f), V(3.6f, 0.02f, 1.0f), b.material(0.9f, 0.9f, 0.9f, 0.3f, 1.0f));
    const float xs[4] = { -2.6f, -1.3f, 1.3f, 2.6f };
    for (int i = 0; i < 4; ++i)
        fixedBox(b, unit, QStringLiteral("pillar-%1").arg(i), V(xs[i] - 0.15f, 0.02f, -2.75f),
                 V(xs[i] + 0.15f, 2.6f, -2.45f), matte);
    fixedBox(b, unit, "mirror", V(-0.8f, 0.4f, -4.0f), V(0.8f, 2.4f, -3.97f), b.material(0.95f, 0.95f, 0.95f, 0.0f, 1.0f));
    const int mover = fixedBox(b, unit, "mover", V(2.7f, 0.02f, -0.6f), V(3.3f, 0.62f, 0.0f),
                               b.material(0.8f, 0.3f, 0.2f, 0.5f));
    b.setMovable(mover);
    return b.finish();
}

// ---- photon-ramp (PHOTON-2 stage 1 fix round: F4's ramp, F2's highlights) -------------------------------------------
//
// photon-room's sealed shell with THE ROUGHNESS RAMP — sixteen metal floor strips, 0.25 m apart side by side across
// the room's middle 4 m, perceptual roughness 0.00, 0.01 ... 0.15 — reflecting THE EDGE, a white emitter PANEL on the
// far wall from y 3.3 m up to 7.9 m (above the stop's view — the eye 3 m up looking down at the strips — so the screen
// march cannot answer it and the band is the ray tier's alone): its LOWER EDGE is a true HALF-PLANE in every strip's
// reflection (PHOTON-2 stage 1b, V7: the 0.3 m bar it replaces was as narrow as the blur from roughness 0.08 up, so a
// 10-90 taken against its falling peak read low). The edge's reflected 10-90 width is the lobe's, against the closed
// form Delta-theta(10-90) = (16/3) alpha t / (d + t) seen from the eye (the GGX slope marginal's 10-90 is (8/3) alpha,
// doubled in the reflected direction; d the eye's distance to the reflection point, t the point's to the edge): it
// must rise with the roughness and step nowhere (fix F4: the fixed estimator, the filter's reach and the hit read's
// mirror cut follow the lobe's footprint at its hit, never an alpha). And THE HIGHLIGHTS: two emitter cubes 0.5 m above the 0.06 strip (a
// lobe ~1.6 trace texels wide at that throw: a stochastic texel whose rays mostly find the spark), 5 and 10 mm
// across — a ~1 px and a ~2 x 2 px image at 5.4 m from the stop's eye — whose reflections the moving history must
// keep (fix F2: the clamp's box holds the texel's own sample). A sibling, not photon-glass itself: the glass
// fixture's measured base (stage 0) stays comparable.
Built buildRamp(unsigned seed, bool bake)
{
    Builder b("photon-ramp", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    const auto matte = b.material(kRho, kRho, kRho, 1.0f);
    roomShell(b, unit, matte);
    using V = iris::Vec3;
    for (int i = 0; i < 16; ++i) {
        const float x0 = -2.0f + 0.25f * float(i);
        fixedBox(b, unit, QStringLiteral("strip-%1").arg(i, 2, 10, QLatin1Char('0')), V(x0, 0, -3.6f),
                 V(x0 + 0.24f, 0.02f, 1.6f), b.material(0.9f, 0.9f, 0.9f, 0.01f * float(i), 1.0f));
    }
    fixedBox(b, unit, "edge", V(-3.9f, 3.3f, -3.99f), V(3.9f, 7.9f, -3.95f), b.emitter(1.0f));
    const float hx = -2.0f + 0.25f * 6.0f + 0.12f;   // the 0.06 strip's centre line
    fixedBox(b, unit, "spark-1px", V(hx - 0.05f - 0.0025f, 0.4975f, -1.5025f), V(hx - 0.05f + 0.0025f, 0.5025f, -1.4975f),
             b.emitter(10.0f));
    fixedBox(b, unit, "spark-2px", V(hx + 0.05f - 0.005f, 0.495f, -1.505f), V(hx + 0.05f + 0.005f, 0.505f, -1.495f),
             b.emitter(10.0f));
    // THE CONTACT LINES (PHOTON-2 round 2, G5): a 10 cm emitter post STANDING on each strip near the far end, 60 cm
    // tall - its reflection starts at the post's foot (the throw 0) and its two vertical edges blur with the throw, so
    // the fixed cut, which the throw decides (the lobe's footprint at its hit: 4 alpha t against the texel there),
    // crosses every strip from 0.04 up inside the reflection; the picture may not step there. Seen from the contact
    // pose (stopRamp's D = 1); behind the edge bar's band and the sparks from the stop's pose (their rays pass above).
    // THE CONTACT MARK (PHOTON-2 stage 1b, C3's row): the 0.00 strip's post stands on a 10 cm MATTE BLACK FOOT (0.02 to
    // 0.12 m), the emitter above it - a uniform emitter hides the origin's seam (a ray lifted b off the strip shows the
    // face from height b at the contact, and a uniform face looks the same from any height); the foot's reflected top
    // edge must lie where the mirror puts it (y 0.02 - 0.10 under the strip's plane), and its displacement in rows is
    // the seam (the 1/2-cell bias shows the foot b shorter in the reflection).
    for (int i = 0; i < 16; ++i) {
        const float xc = -2.0f + 0.25f * float(i) + 0.12f;
        const QString name = QStringLiteral("post-%1").arg(i, 2, 10, QLatin1Char('0'));
        if (i == 0) {
            fixedBox(b, unit, name + QStringLiteral("-foot"), V(xc - 0.05f, 0.02f, -3.15f), V(xc + 0.05f, 0.12f, -3.0f),
                     b.material(0.02f, 0.02f, 0.02f, 1.0f));
            fixedBox(b, unit, name, V(xc - 0.05f, 0.12f, -3.15f), V(xc + 0.05f, 0.62f, -3.0f), b.emitter(1.0f));
            continue;
        }
        fixedBox(b, unit, name, V(xc - 0.05f, 0.02f, -3.15f), V(xc + 0.05f, 0.62f, -3.0f), b.emitter(1.0f));
    }
    return b.finish();
}

void stopRamp(double d, iris::Vec3 &eye, iris::Vec3 &target)
{
    // D = 2 and 3: THE CONTACT ROW (PHOTON-2 stage 1b, C3's ray origin): the 0.00 strip's post foot from 2.4 m (D = 2:
    // 1.0 m up, 2.2 m in front) and from 1 m (D = 3: 0.6 m up, 0.8 m in front), looking at the foot - the rows between
    // the foot line and its reflection's first row are the origin's seam (the 1/2-cell bias read ~37 px at 2.4 m and ~88
    // px at 1 m to first order: the preflight's estimate).
    const float post00 = -2.0f + 0.12f;   // the 0.00 strip's centre line (its post's)
    if (d >= 2.5) {
        eye = iris::Vec3(post00, 0.62f, -2.2f);
        target = iris::Vec3(post00, 0.1f, -3.0f);
        return;
    }
    if (d >= 1.5) {
        eye = iris::Vec3(post00, 1.02f, -0.8f);
        target = iris::Vec3(post00, 0.1f, -3.0f);
        return;
    }
    if (d >= 0.5) {
        // D = 1: THE CONTACT POSE (G5) - a metre up, 2.2 m in front of the posts, looking down at their feet
        eye = iris::Vec3(0, 1.0f, -0.8f);
        target = iris::Vec3(0, 0.1f, -3.0f);
        return;
    }
    // looking down at the strips: the bar above the view, its reflection in the strips' middle
    eye = iris::Vec3(0, 3.0f, 3.4f);
    target = iris::Vec3(0, 0, -1.0f);
}

std::vector<Prediction> expectRamp(const Built &, const Optics &)
{
    return { { "probesKeptShare", 1.0, "every probe of the sealed shell sees >= 2 faces of geometry: all kept" } };
}

std::string describeRamp()
{
    return "photon-ramp: photon-room's sealed shell with sixteen metal floor strips at perceptual roughness 0.00 to "
           "0.15 reflecting the lower edge of a white emitter panel on the far wall - a half-plane (the lobe's blur per "
           "strip against (16/3) alpha t / (d + t): PHOTON-2 fix F4, stage 1b V7), two "
           "5 / 10 mm emitter sparks above the 0.06 strip (the moving history's highlights: fix F2) and an emitter post "
           "standing on each strip (the contact lines: the fixed cut the throw decides, round 2's G5; envcam 1 looks "
           "at their feet; envcam 2 / 3 at the 0.00 strip's post foot from 2.4 m / 1 m: stage 1b's contact row). "
           "Predicts THE KEEP RULE only: every probe sees >= 2 faces, all kept.";
}

// ---- photon-contact (PHOTON-7 stage 6: the ray origin on CHAINED content, and at a crease) --------------------------
//
// photon-ramp's contact mark - the 10 cm matte black FOOT (0.02 to 0.12 m) under an emitter post, at the 0.00 strip's
// place - standing on a MIRROR FLOOR that is a CHAINED mesh, NON-PLANAR (READ_A finding 2: a flat grid's levels are all
// the same plane, so Atom's cut equals level 0 exactly and the arm could not fail): one welded 32 x 32 grid over the
// room's 7.8 m (2,048 triangles, 24 cm cells - the bake builds its level chain and its cluster DAG; 3.3 s, the zigzag
// being every vertex a feature) CORRUGATED - each vertex kCorrugation above or below y 0.02, a checkerboard, so level 0
// is a fine zigzag whose simplifications lie within about kCorrugation of it (0.2 mm: a fifth of a pixel at 1 m, so
// Atom's cut draws coarse clusters wherever their group falls under the view's tolerance). Where the drawn surface lies
// BELOW level 0, which every ray traces (ONE RAY LEVEL PER MESH), an origin lifted by the float bound alone starts
// inside level 0 and hits it from below -
// the origin law's drawn-cut term is what clears it (the row's negative control turns it off: rays.originCut 0). The
// shading normal is the plane's (+Y), so the zigzag moves no reflection. On it stands the foot (its contact line within
// the corrugation's 0.2 mm of y 0.02): its reflection must meet it at the contact line within a pixel, from 2.4 m
// (envcam 1) and 1 m (envcam 2). AN EMITTER PANEL over the far wall (y 0.8 .. 7.9) gives the floor a uniformly bright
// reflection over a wide band, where a ray that hits its own floor reads dark. And THE CREASE (READ_A finding 1): a
// BRIGHT MATTE BLOCK (albedo 0.8, card-lit) standing on the same floor (its face at z -2.17: a stretch the cut draws
// coarse from envcam 3 - measured: the base's side pick drew its line on 132 of 747 columns there), whose base, seen
// from the foreshortened side (envcam 3, 3.4 m away, 1.3 m up), the floor's first rows must reflect as the rows below
// them do - a ray whose lift went sideways (the depth's nearer neighbour is across the crease) keeps the drawn surface
// under level 0 and hits its own floor: a one-row line along the crease. Predicts THE KEEP RULE only.
constexpr float kCorrugation = 0.0002f;

Built buildContact(unsigned seed, bool bake)
{
    Builder b("photon-contact", seed, bake);
    const iris::MeshPtr unit = unitBox(b);
    roomShell(b, unit, b.material(kRho, kRho, kRho, 1.0f));
    using V = iris::Vec3;
    Arrays grid;
    const int nx = 32, nz = 32;
    const float x0 = -3.9f, x1 = 3.9f, z0 = -3.9f, z1 = 3.9f;
    for (int j = 0; j <= nz; ++j)
        for (int i = 0; i <= nx; ++i) {
            const float x = x0 + (x1 - x0) * float(i) / float(nx), z = z0 + (z1 - z0) * float(j) / float(nz);
            grid.p.insert(grid.p.end(), { x, 0.02f + (((i + j) & 1) ? kCorrugation : -kCorrugation), z });
            grid.n.insert(grid.n.end(), { 0.0f, 1.0f, 0.0f });
            grid.uv.insert(grid.uv.end(), { float(i) / float(nx), float(j) / float(nz) });
        }
    for (int j = 0; j < nz; ++j)
        for (int i = 0; i < nx; ++i) {
            const unsigned a = unsigned(j * (nx + 1) + i), c = a + 1u, d = a + unsigned(nx + 1), e = d + 1u;
            // counter-clockwise seen from +Y (x right, -z up): (i, j+1) (i+1, j+1) (i+1, j) / (i, j+1) (i+1, j) (i, j)
            grid.idx.insert(grid.idx.end(), { d, e, c, d, c, a });
        }
    b.instance(b.mesh(grid), "floor-chained", "subject", V(0, 0, 0), 0.0f, V(1, 1, 1),
               b.material(0.9f, 0.9f, 0.9f, 0.0f, 1.0f));
    const float xc = -2.0f + 0.12f;   // photon-ramp's 0.00 strip's centre line
    fixedBox(b, unit, "post-foot", V(xc - 0.05f, 0.02f, -3.15f), V(xc + 0.05f, 0.12f, -3.0f),
             b.material(0.02f, 0.02f, 0.02f, 1.0f));
    fixedBox(b, unit, "post", V(xc - 0.05f, 0.12f, -3.15f), V(xc + 0.05f, 0.62f, -3.0f), b.emitter(1.0f));
    fixedBox(b, unit, "panel", V(-3.9f, 0.8f, -3.99f), V(3.9f, 7.9f, -3.95f), b.emitter(1.0f));
    // the crease: a bright matte block standing on the chained mirror (its base meets the floor within the corrugation)
    fixedBox(b, unit, "crease-block", V(1.0f, 0.02f, -2.57f), V(2.8f, 0.8f, -2.17f), b.material(0.8f, 0.8f, 0.8f, 0.9f));
    return b.finish();
}

void stopContact(double d, iris::Vec3 &eye, iris::Vec3 &target)
{
    // D = 1: the foot from 2.4 m, D = 2: from 1 m (photon-ramp's D = 2 / 3); D = 3: the crease from 3.4 m; else the
    // room from its middle
    const float post = -2.0f + 0.12f;
    target = iris::Vec3(post, 0.1f, -3.0f);
    if (d >= 2.5) { eye = iris::Vec3(1.9f, 1.3f, 0.45f); target = iris::Vec3(1.9f, 0.1f, -2.75f); return; }
    if (d >= 1.5) { eye = iris::Vec3(post, 0.62f, -2.2f); return; }
    if (d >= 0.5) { eye = iris::Vec3(post, 1.02f, -0.8f); return; }
    eye = iris::Vec3(0, 3.0f, 3.4f);
    target = iris::Vec3(0, 0, -1.0f);
}

std::vector<Prediction> expectContact(const Built &, const Optics &)
{
    return { { "probesKeptShare", 1.0, "every probe of the sealed shell sees >= 2 faces of geometry: all kept" } };
}

std::string describeContact()
{
    return "photon-contact: photon-room's sealed shell with a CHAINED, CORRUGATED mirror floor (a welded 32 x 32 grid, "
           "each vertex 0.2 mm above or below: its level chain and cluster DAG built, its coarse plane under the zigzag's "
           "peaks) under an emitter panel over the far wall, photon-ramp's contact mark on it (a 10 cm matte black foot "
           "under an emitter post) and a bright matte block standing on it - the crease "
           "(PHOTON-7 stage 6, the ray origin: the foot's reflection meets the foot within a pixel, no ray hits its own "
           "floor, the crease's first rows reflect the block; envcam 1 / 2 the foot from 2.4 m / 1 m, envcam 3 the crease "
           "from 3.4 m). Predicts THE KEEP RULE only: every probe sees >= 2 faces, all kept.";
}

void stopGlass(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 1.8f, 3.4f);
    target = iris::Vec3(0, 0.5f, -3.0f);
}

std::vector<Prediction> expectGlass(const Built &, const Optics &)
{
    return { { "probesKeptShare", 1.0, "every probe of the sealed shell sees >= 2 faces of geometry: all kept" } };
}

std::string describeGlass()
{
    return "photon-glass: photon-room's sealed shell with two metal floor panes (roughness 0.1 and 0.3), four matte "
           "pillars standing on them, a mirror on the far wall and one declared (still) mover - the stillness door's "
           "reflective fixture (PHOTON-2). Predicts THE KEEP RULE only: every probe sees >= 2 faces, all kept (the "
           "metal voids photon-room's diffuse residual).";
}

void stopRoom(double, iris::Vec3 &eye, iris::Vec3 &target)
{
    eye = iris::Vec3(0, 2, 3);
    target = iris::Vec3(0, 3, -4);
}

std::vector<Prediction> expectRoom(const Built &, const Optics &)
{
    const double inside = 6.0 * 64.0, open = 1.0;
    const double r = kRho * (1.0 - open / inside);
    const double n = std::ceil(std::log(0.02 * (1.0 - r)) / std::log(r));
    return { { "bounceRatio", r, "r = rho (1 - A_open / A_inside): the energy ratio of two passes" },
             { "passes", std::min(8.0, n), "n = ceil(log(0.02 (1 - r)) / log r), at most 8: the residual under 2 %" },
             { "multiBounceGain", 1.0 / (1.0 - r), "1 / (1 - r): the enclosed room's light over its first bounce" },
             { "probesKeptShare", 1.0, "every probe of a sealed room sees geometry in >= 2 faces: all kept" } };
}

std::string describeRoom()
{
    return "photon-room: an enclosed 8 m room (one 1 x 1 m opening, a lamp, matte walls of albedo 0.5). Predicts the "
           "BOUNCE RESIDUAL — r = rho (1 - A_open/A_inside) = 0.4987, n = ceil(log(0.02 (1-r)) / log r) = 7 passes, "
           "the gain 1/(1-r) = 1.995 — and THE KEEP RULE: every probe inside sees >= 2 faces of geometry, all kept.";
}

}   // namespace

Env photonRoom() { return Env{ "photon-room", describeRoom, buildRoom, stopRoom, expectRoom, 500.0f, false }; }
Env photonGlass() { return Env{ "photon-glass", describeGlass, buildGlass, stopGlass, expectGlass, 500.0f, false }; }
Env photonRamp() { return Env{ "photon-ramp", describeRamp, buildRamp, stopRamp, expectRamp, 500.0f, false }; }
Env photonContact()
{
    return Env{ "photon-contact", describeContact, buildContact, stopContact, expectContact, 500.0f, false };
}

}   // namespace envs
}   // namespace lab
