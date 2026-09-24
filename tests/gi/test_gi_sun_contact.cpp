// HARD SUN CONTACT SHADOWS (PHOTON P5, RY-R3; lane PHOTON-RAYS-1) —
// `gi.sun_contact` and `gi.sun_contact_norays`.
//
// WHAT IS WRONG WITHOUT IT: a shadow map is rendered with a depth BIAS (or every
// lit surface shadows itself), and the bias leaves a band of LIGHT where an
// object meets the ground — the crate that floats. The contact job traces one
// hardware ray per texel from the prepass' surface towards the sun, out to the
// contact range, and the PBS pass folds the answer in as min( map, ray ).
//
// THE FIXTURE: a 2 cm BOARD (1 x 1 m, standing, broadside to the sun) on a matte
// floor under a LOW sun (15 degrees, from -X), so its shadow runs 3.7 m along
// +X, and a camera on the +Z side looking across the whole shadow: the board's
// shaded face meets the floor at x = 0.5, the shadow ends at x = 4.23.
//
// WHY A BOARD AND NOT THE DESIGN'S SOLID CRATE, measured (spikes/photon-rays-1):
// under a 1 m crate the map does NOT leak at all — 0.0 mm at 5, 15, 30 and 60 m
// — because the occluder the map holds for a floor point beside the crate is the
// crate's SUNLIT face, a metre further up the light ray than the floor, far past
// any bias. The leak is the thin caster's: the stored occluder is the caster's
// own thickness away from the floor, and the bias (constant + normal offset,
// both in shadow texels) exceeds it — 44 mm at this 2 cm board from 5.4 m, 106 to
// 165 mm for 2-10 cm boards seen from 15-30 m (the PSSM split's texel grows with
// distance). Panels, planks, table tops, a crate's lid: the everyday contact.
//
// FIX ROUND (the lead's F3): that leak was upstream's constant bias (0.01 x 1 in
// world units along the light, x the split's auto factor) — cut to 0.3 for the sun (the lowest without acne on a glossy plane)
// (OgreShadow.cpp) it is 0.0 mm here, and the board now GUARDS the cut.
// `gi.sun_contact_both` (`--both`) then draws a sphere LATTICE as the process's
// SECOND scene beside the board: the map's acne and its contact on curved, LOD'd
// casters, the ray's acne there, and a glass pane over the contact. (The "grazing
// sun's contact the map misses, a metre downstream" this arm was written for was
// the coarse LOD levels' garbage caster geometry — PHOTON-SCENE-SWITCH-1; the map
// now shadows every contact pixel.)
//
// THE NUMBERS (every one in FRAMES and ground metres, never time):
//   1. THE GAP: the widest band of LIT floor next to the board's shaded face,
//      in metres and in texels of the ray job: at most ONE texel with the row
//      off (the bias cut) and with the row on (full, then half).
//   2. THE FAR SHADOW: floor whose sun ray meets the board beyond the 2 m range
//      is the map's alone — it must read the same with the row on as with a
//      range too short to reach it (both arms carry the prepass, so the only
//      difference between them is the ray's answer).
//   3. NO ACNE: the sunlit floor reads the same with the ray as without it (a
//      self-intersecting ray would darken it).
//   4. THE ROW'S CONTRACT: the status says running, the divisor the resolution
//      asked for, the sun direction the light's.
//   5. THE SEAM at the range's end (printed) and the MAP'S ACNE on the sunlit
//      floor against the no-caster frame (asserted: the bias cut's other edge).
//
// THE NO-RAYS ENTRY runs the same binary under JAHSHAKA_NO_RAY_QUERY: the row
// on must then render EXACTLY the picture of the row off, byte for byte, and
// say it is not on (a machine without the hardware renders the map alone).
//
// `--cost` (not a ctest row): the job's GPU milliseconds against the frame's,
// at 1920x1080, paired arms in ONE process (the lead's measurement law) — run
// it under scripts/gpu-exclusive.sh.
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, ...)                                                    \
    do {                                                                        \
        std::printf((cond) ? "ok: " : "FAIL: ");                                 \
        std::printf(__VA_ARGS__);                                                \
        std::printf("\n");                                                       \
        if (!(cond)) ++failures;                                                 \
    } while (0)

static const unsigned kSize = 640;          // square: groundPointForPixel's frame
static const int kSettleFrames = 12;        // no GI: the shadow map is the whole settle
static const float kPi = 3.14159265358979f;
static float kSunElevationDeg = 15.0f;
static float kSunPower = 8.0f;
static float kBoard = 0.02f;                // the caster's thickness along the sun
static float kFov = 45.0f;
static Vec3 kCamPos(2.2f, 3.0f, 4.2f);
static Vec3 kCamTarget(1.8f, 0.0f, 0.0f);
static const float kFaceX = 0.5f;           // the board's shaded face

static void render(Engine *e, int n) { for (int i = 0; i < n; ++i) e->renderOneFrame(); }

static float luma(const Image &img, unsigned x, unsigned y)
{
    const size_t i = (size_t(y) * img.width + x) * 4u;
    return 0.2126f * img.rgba[i] + 0.7152f * img.rgba[i + 1] + 0.0722f * img.rgba[i + 2];
}

/// One pixel's floor point (every pixel is asked; the caller filters by region).
struct Sample { unsigned x, y; Vec3 ground; };
static std::vector<Sample> gFloor;

static void buildFloorSamples()
{
    gFloor.clear();
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Vec3 g = enginetest::groundPointForPixel(kCamPos, kCamTarget, x, y, kSize, 0.0f, kFov);
            gFloor.push_back({ x, y, g });
        }
}

/// The mean luma over the floor points inside [x0,x1] x [z0,z1].
static float regionMean(const Image &img, float x0, float x1, float z0, float z1, unsigned *n = nullptr)
{
    double sum = 0.0;
    unsigned count = 0;
    for (const Sample &s : gFloor)
        if (s.ground.x >= x0 && s.ground.x <= x1 && s.ground.z >= z0 && s.ground.z <= z1) {
            sum += luma(img, s.x, s.y);
            ++count;
        }
    if (n) *n = count;
    return count ? float(sum / count) : -1.0f;
}

/// The mean absolute difference over the same region (codes, 8-bit luma).
static float regionDiff(const Image &a, const Image &b, float x0, float x1, float z0, float z1)
{
    double sum = 0.0;
    unsigned count = 0;
    for (const Sample &s : gFloor)
        if (s.ground.x >= x0 && s.ground.x <= x1 && s.ground.z >= z0 && s.ground.z <= z1) {
            sum += std::fabs(luma(a, s.x, s.y) - luma(b, s.x, s.y));
            ++count;
        }
    return count ? float(sum / count) : 1e9f;
}

/// THE GAP: the farthest LIT floor point from the board's shaded face, within
/// the first metre of the shadow and away from its penumbral sides (|z| < 0.3).
/// "Lit" is above the midpoint between the sunlit floor and the deep shadow.
static float contactGap(const Image &img, float threshold)
{
    float gap = 0.0f;
    for (const Sample &s : gFloor)
        if (s.ground.x >= kFaceX && s.ground.x <= kFaceX + 1.0f && std::fabs(s.ground.z) < 0.3f &&
            luma(img, s.x, s.y) > threshold)
            gap = std::max(gap, s.ground.x - kFaceX);
    return gap;
}

/// The world size of one pixel at the contact (the camera's own footprint at
/// the face's foot): the unit a gap is stated in.
static float pixelFootprintAtContact()
{
    const float dx = kFaceX - kCamPos.x, dy = -kCamPos.y, dz = -kCamPos.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    return dist * 2.0f * std::tan(0.5f * kFov * kPi / 180.0f) / float(kSize);
}

static bool savePpm(const Image &img, const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
    for (size_t i = 0; i < size_t(img.width) * img.height; ++i)
        std::fwrite(&img.rgba[i * 4u], 1, 3, f);
    std::fclose(f);
    return true;
}

static std::vector<NodeId> gFixtureNodes;
static void buildFixture(Scene *s, int crates)
{
    gFixtureNodes.clear();
    s->setAmbient(Colour(0.30f, 0.34f, 0.40f), Colour(0.20f, 0.20f, 0.20f));
    const NodeId floor = enginetest::addTestCube(s, Colour(0.7f, 0.7f, 0.7f), 0.0f, 1.0f);
    enginetest::setNodeScale(s, floor, Vec3(60.0f, 0.2f, 60.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    gFixtureNodes.push_back(floor);
    // THE CRATE at the origin (and, for --cost, a field of them).
    for (int i = 0; i < crates; ++i) {
        const NodeId c = enginetest::addTestCube(s, Colour(0.55f, 0.45f, 0.35f), 0.0f, 1.0f);
        gFixtureNodes.push_back(c);
        const int gx = i % 10, gz = i / 10;
        enginetest::setNodePosition(s, c, i == 0 ? Vec3(0.0f, 0.5f, 0.0f)
                                                 : Vec3(-12.0f + 2.6f * float(gx), 0.5f,
                                                        -14.0f + 2.6f * float(gz)));
        // THE BOARD: its shaded face at x = kFaceX, standing on the floor. A
        // thickness of 1 is the solid crate (the no-leak control of the sweep).
        if (i == 0) {
            enginetest::setNodeScale(s, c, Vec3(kBoard, 1.0f, 1.0f));
            enginetest::setNodePosition(s, c, Vec3(kFaceX - 0.5f * kBoard, 0.5f, 0.0f));
        }
    }
    const float e = kSunElevationDeg * kPi / 180.0f;
    enginetest::addDirectionalLight(s, Vec3(std::cos(e), -std::sin(e), 0.0f), kSunPower);
}

// ---------------------------------------------------------------------------
// THE CURVED-CASTER ARM (fix round F2/F3): a lattice of spheres under a 10-degree
// sun, the camera ~26 m away. Two questions the board cannot ask:
//   * MAP ACNE — the shadow map's own self-shadowing on the sunlit hemispheres
//     and the grazing floor, measured against the same frame with every caster
//     OFF (the constant bias's floor: fix round item 1);
//   * RAY ACNE — the contact ray starting under its own (LOD'd, faceted) near
//     copy: the row on must darken NO analytically sunlit pixel beyond the map
//     (the lift's margin: item 3); and the contact under every sphere closed.
// Pixels are classified ANALYTICALLY (the smooth sphere and the plane) with
// margins wide enough for the drawn level's facets, so the masks do not depend
// on the renderer being judged.
namespace lattice {
struct Ball { Vec3 c; float r; };
static std::vector<Ball> gBalls;
static const Vec3 kPos(9.0f, 11.0f, 21.0f), kTarget(0.0f, 0.5f, 0.0f);
static float kLatFov = 40.0f, kLatSunDeg = 10.0f;
static float kLitCos = 0.25f;   // the sunlit class starts this far from the terminator

static Vec3 pixelRay(unsigned px, unsigned py)
{
    Vec3 f(kTarget.x - kPos.x, kTarget.y - kPos.y, kTarget.z - kPos.z);
    float l = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    f = Vec3(f.x / l, f.y / l, f.z / l);
    Vec3 r(-f.z, 0.0f, f.x);
    l = std::sqrt(r.x * r.x + r.z * r.z);
    r = Vec3(r.x / l, 0.0f, r.z / l);
    const Vec3 u(r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x);
    const float t = std::tan(kLatFov * 0.5f * kPi / 180.0f);
    const float nx = 2.0f * (float(px) + 0.5f) / float(kSize) - 1.0f;
    const float ny = 1.0f - 2.0f * (float(py) + 0.5f) / float(kSize);
    return Vec3(f.x + (r.x * nx + u.x * ny) * t, f.y + (r.y * nx + u.y * ny) * t,
                f.z + (r.z * nx + u.z * ny) * t);
}
static float dot3(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 sub(const Vec3 &a, const Vec3 &b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
/// Distance from `c` to the ray o + t d (t >= 0, d unit-less), and the t of approach.
static float lineDist(const Vec3 &o, const Vec3 &d, const Vec3 &c, float &tOut)
{
    const float dd = dot3(d, d);
    const Vec3 oc = sub(c, o);
    tOut = dot3(oc, d) / dd;
    const Vec3 q(o.x + d.x * tOut - c.x, o.y + d.y * tOut - c.y, o.z + d.z * tOut - c.z);
    return std::sqrt(dot3(q, q));
}
static Vec3 toSun()
{
    const float e = kLatSunDeg * kPi / 180.0f;
    return Vec3(-std::cos(e), std::sin(e), 0.0f);
}
/// Does the sun ray from p pass within `margin` of any ball (other than `skip`)?
static bool sunBlocked(const Vec3 &p, float margin, int skip)
{
    const Vec3 L = toSun();
    for (int i = 0; i < int(gBalls.size()); ++i) {
        if (i == skip) continue;
        float t = 0.0f;
        const float d = lineDist(p, L, gBalls[size_t(i)].c, t);
        if (t > 0.0f && d < gBalls[size_t(i)].r + margin) return true;
    }
    return false;
}
enum Kind : unsigned char { None, SphereLit, FloorLit, Contact, PaneContact };
/// THE GLASS PANE (fix round F1): an alpha-blended box standing in front of the
/// middle row's feet, where the map's shadow is displaced and the ray's is
/// not — the one place the two answers differ over a whole band.
static const Vec3 kPaneMin(-1.0f, 0.0f, 3.49f), kPaneMax(3.0f, 1.5f, 3.51f);
static bool rayHitsPane(const Vec3 &o, const Vec3 &d, float &tHit)
{
    float t0 = 0.0f, t1 = 1e30f;
    const float omin[3] = { kPaneMin.x, kPaneMin.y, kPaneMin.z }, omax[3] = { kPaneMax.x, kPaneMax.y, kPaneMax.z };
    const float oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(dd[a]) < 1e-9f) { if (oo[a] < omin[a] || oo[a] > omax[a]) return false; continue; }
        float ta = (omin[a] - oo[a]) / dd[a], tb = (omax[a] - oo[a]) / dd[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    tHit = t0;
    return true;
}
static std::vector<unsigned char> gKind;
static void classify()
{
    gKind.assign(size_t(kSize) * kSize, None);
    const Vec3 L = toSun();
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Vec3 d = pixelRay(x, y);
            // the nearest ball the view ray meets, and how centrally
            int hit = -1; float hitT = 1e30f, hitB = 0.0f; bool nearSilhouette = false;
            for (int i = 0; i < int(gBalls.size()); ++i) {
                float t = 0.0f;
                const float b = lineDist(kPos, d, gBalls[size_t(i)].c, t);
                if (t <= 0.0f) continue;
                if (b < gBalls[size_t(i)].r * 1.15f) nearSilhouette = nearSilhouette || b > gBalls[size_t(i)].r * 0.85f;
                if (b < gBalls[size_t(i)].r && t < hitT) { hit = i; hitT = t; hitB = b; }
            }
            unsigned char k = None;
            if (hit >= 0) {
                if (hitB < gBalls[size_t(hit)].r * 0.85f) {
                    const Ball &bl = gBalls[size_t(hit)];
                    const float dl = std::sqrt(dot3(d, d));
                    const float back = std::sqrt(bl.r * bl.r - hitB * hitB);
                    const float th = hitT - back / dl;
                    const Vec3 p(kPos.x + d.x * th, kPos.y + d.y * th, kPos.z + d.z * th);
                    const Vec3 n(sub(p, bl.c).x / bl.r, sub(p, bl.c).y / bl.r, sub(p, bl.c).z / bl.r);
                    if (dot3(n, L) > kLitCos && !sunBlocked(p, 0.2f, hit)) k = SphereLit;
                }
            } else if (!nearSilhouette && d.y < 0.0f) {
                const float t = -kPos.y / d.y;
                const Vec3 p(kPos.x + d.x * t, 0.0f, kPos.z + d.z * t);
                if (std::fabs(p.x) < 14.0f && std::fabs(p.z) < 14.0f) {
                    // SUNLIT FLOOR, away from every sphere's shadow LANE (the map's
                    // shadow is displaced downstream along it at a grazing sun,
                    // which is the contact question, not acne).
                    bool lane = false;
                    for (const Ball &bl : gBalls)
                        lane = lane || (std::fabs(p.z - bl.c.z) < bl.r + 0.8f && p.x - bl.c.x > -bl.r - 0.8f &&
                                        p.x - bl.c.x < 10.0f);
                    if (!lane && !sunBlocked(p, 0.2f, -1)) k = FloorLit;
                    for (const Ball &bl : gBalls) {
                        float ts = 0.0f;
                        const float ds = lineDist(p, L, bl.c, ts);
                        const float hx = p.x - bl.c.x, hz = p.z - bl.c.z;
                        if (ts > 0.0f && ds < bl.r - 0.15f && std::sqrt(hx * hx + hz * hz) < 1.2f)
                            k = Contact;
                    }
                }
            }
            // A pixel the pane covers belongs to the pane: the contact floor
            // behind it is the glass arm's class, everything else is nobody's.
            float tp = 0.0f;
            if (rayHitsPane(kPos, d, tp)) {
                // a margin off the pane's own outline, where blending is partial
                float tq = 0.0f;
                const Vec3 dx(d.x * 1.01f, d.y * 1.01f, d.z * 1.01f);
                const bool inner = rayHitsPane(kPos, pixelRay(x > 1 ? x - 2 : x, y), tq) &&
                                   rayHitsPane(kPos, pixelRay(x + 2 < kSize ? x + 2 : x, y), tq) &&
                                   rayHitsPane(kPos, pixelRay(x, y > 1 ? y - 2 : y), tq) &&
                                   rayHitsPane(kPos, pixelRay(x, y + 2 < kSize ? y + 2 : y), tq);
                (void)dx;
                k = (k == Contact && inner) ? PaneContact : None;
            }
            gKind[size_t(y) * kSize + x] = k;
        }
}
/// A UV sphere of radius 0.5 (seam welded) with a three-level chain: every
/// 2nd, 4th and 8th ring and segment of the same vertices. The bounds are the
/// chord sag of the coarse facet, r (1 - cos step) — an upper bound.
static MeshData sphereWithChain()
{
    const int rings = 48, segments = 96;
    MeshData d;
    for (int r = 0; r <= rings; ++r) {
        const float th = float(r) / float(rings) * kPi;
        for (int sg = 0; sg < segments; ++sg) {
            const float ph = float(sg) / float(segments) * 2.0f * kPi;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { 0.5f * x, 0.5f * y, 0.5f * z });
            d.normals.insert(d.normals.end(), { x, y, z });
        }
    }
    const auto level = [&](int st) {
        std::vector<unsigned> idx;
        for (int r = 0; r < rings; r += st)
            for (int sg = 0; sg < segments; sg += st) {
                const unsigned a = unsigned(r * segments + sg);
                const unsigned b = unsigned(r * segments + (sg + st) % segments);
                const unsigned c = unsigned((r + st) * segments + sg);
                const unsigned e = unsigned((r + st) * segments + (sg + st) % segments);
                idx.insert(idx.end(), { a, b, c, b, e, c });
            }
        return idx;
    };
    d.indices = level(1);
    for (int st : { 2, 4, 8 }) {
        d.lodIndices.push_back(level(st));
        const float step = float(st) * kPi / float(rings);
        const float bound = 0.5f * (1.0f - std::cos(step));
        d.lodErrors.push_back(bound);
        d.lodBounds.push_back(bound);
    }
    return d;
}
}   // namespace lattice

static void latticeArm(Engine *e, const char *dumpDir)
{
    using namespace lattice;
    // The lattice sweep's knobs (measurement switches): the sun's elevation and
    // how far from the terminator the sunlit class starts.
    if (const char *v = getenv("JAH_SC_LATSUN")) kLatSunDeg = float(atof(v));
    if (const char *v = getenv("JAH_SC_LITCOS")) kLitCos = float(atof(v));
    View *view = e->createOffscreenView("suncontactlattice", kSize, kSize, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("suncontactlattice");
    if (!view || !s || !view->setScene(s)) { std::printf("FAIL: lattice view/scene\n"); ++failures; return; }
    if (!(e->rayQueryAvailable() && e->rayTracing())) {
        std::printf("ok: this machine has no ray queries — the lattice arm skips cleanly\n");
        return;
    }
    view->setShadows(true);
    s->setAmbient(Colour(0.30f, 0.34f, 0.40f), Colour(0.20f, 0.20f, 0.20f));
    std::vector<NodeId> nodes;
    const NodeId floor = enginetest::addTestCube(s, Colour(0.7f, 0.7f, 0.7f), 0.0f, 1.0f);
    enginetest::setNodeScale(s, floor, Vec3(80.0f, 0.2f, 80.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    nodes.push_back(floor);
    // Rows ACROSS the sun (z), rows 8 m apart ALONG it (x): no sphere shadows
    // another and no sunlit sphere has another within the ray's 2 m up-sun.
    gBalls.clear();
    const MeshId mesh = s->createMesh(sphereWithChain());
    PbrParams pp;
    pp.albedo = Colour(0.75f, 0.72f, 0.68f);
    pp.roughness = 0.8f;
    const MaterialId mat = s->createPbrMaterial(pp);
    for (int ix = -1; ix <= 1; ++ix)
        for (int iz = -2; iz <= 2; ++iz) {
            const Vec3 c(8.0f * float(ix), 0.5f, 1.5f * float(iz));
            const NodeId n = s->createNode();
            if (!(n && mesh && mat && s->attachMesh(n, mesh, mat))) { std::printf("FAIL: sphere\n"); ++failures; }
            enginetest::poseRegistry()[s][n] = enginetest::NodePose{};
            enginetest::setNodePosition(s, n, c);
            nodes.push_back(n);
            gBalls.push_back({ c, 0.5f });
        }
    NodeId pane = 0;
    {
        pane = s->createNode();
        PbrParams gp;
        gp.albedo = Colour(0.9f, 0.95f, 1.0f);
        gp.roughness = 0.1f;
        gp.alphaMode = PbrAlphaMode::Blend;
        gp.alpha = 0.35f;
        const MaterialId gm = s->createPbrMaterial(gp);
        const MeshId cube = s->createMesh(enginetest::unitCubeMesh());
        if (!(pane && gm && cube && s->attachMesh(pane, cube, gm))) { std::printf("FAIL: pane\n"); ++failures; }
        enginetest::poseRegistry()[s][pane] = enginetest::NodePose{};
        enginetest::setNodeScale(s, pane, Vec3(kPaneMax.x - kPaneMin.x, kPaneMax.y - kPaneMin.y, kPaneMax.z - kPaneMin.z));
        enginetest::setNodePosition(s, pane, Vec3(0.5f * (kPaneMin.x + kPaneMax.x), 0.5f * (kPaneMin.y + kPaneMax.y),
                                                  0.5f * (kPaneMin.z + kPaneMax.z)));
        s->setNodeCastShadow(pane, false);   // it casts nothing, map or ray
    }
    const float el = kLatSunDeg * kPi / 180.0f;
    enginetest::addDirectionalLight(s, Vec3(std::cos(el), -std::sin(el), 0.0f), kSunPower);
    {
        CameraDesc c = enginetest::testCameraDescLookAt(kPos, kTarget);
        c.fovDegrees = kLatFov;
        view->setCamera(c);
    }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    view->setPostFx(fx);
    classify();
    unsigned nSphere = 0, nFloor = 0, nContact = 0, nPane = 0;
    for (unsigned char k : gKind) {
        nSphere += k == SphereLit; nFloor += k == FloorLit; nContact += k == Contact; nPane += k == PaneContact;
    }

    const auto shot = [&](bool on, SunContactResolution res, Image &out) {
        SunContactDesc sc;
        sc.enabled = on;
        sc.resolution = res;
        s->setSunContact(sc);
        render(e, kSettleFrames);
        view->readPixels(out);
    };
    Image off, noCast, onFull, onHalf, onShort;
    // A NEW SCENE'S FIRST FRAMES carry no sun shadow yet (the view's atlas is
    // (re)planned for the scene's lights): warm up before the first read.
    render(e, 60);
    shot(false, SunContactResolution::Full, off);
    shot(true, SunContactResolution::Full, onFull);
    shot(true, SunContactResolution::Half, onHalf);
    {
        SunContactDesc sc;
        sc.enabled = true;
        sc.range = kSunContactMinRange;
        s->setSunContact(sc);
        render(e, kSettleFrames);
        view->readPixels(onShort);
    }
    for (NodeId n : nodes) s->setNodeCastShadow(n, false);
    shot(false, SunContactResolution::Full, noCast);
    for (NodeId n : nodes) s->setNodeCastShadow(n, true);
    if (dumpDir) {
        savePpm(off, std::string(dumpDir) + "/lattice_off.ppm");
        savePpm(onFull, std::string(dumpDir) + "/lattice_on_full.ppm");
        savePpm(noCast, std::string(dumpDir) + "/lattice_nocast.ppm");
    }
    // DARKENING of `a` below `ref` over one class: pixels beyond 4 codes, and the mean.
    const auto darker = [&](const Image &a, const Image &ref, Kind k, float &mean) {
        unsigned n = 0, count = 0; double sum = 0.0;
        for (unsigned y = 0; y < kSize; ++y)
            for (unsigned x = 0; x < kSize; ++x) {
                if (gKind[size_t(y) * kSize + x] != k) continue;
                const float dl = luma(ref, x, y) - luma(a, x, y);
                sum += std::max(0.0f, dl); ++n;
                if (dl > 4.0f) ++count;
            }
        mean = n ? float(sum / n) : 0.0f;
        return count;
    };
    float m0, m1, m2, m3, m4, m5;
    const unsigned mapAcneSphere = darker(off, noCast, SphereLit, m0);
    const unsigned mapAcneFloor = darker(off, noCast, FloorLit, m1);
    const unsigned rayAcneSphere = darker(onFull, off, SphereLit, m2);
    const unsigned rayAcneFloor = darker(onFull, off, FloorLit, m3);
    const unsigned rayAcneSphereH = darker(onHalf, off, SphereLit, m4);
    const unsigned rayAcneFloorH = darker(onHalf, off, FloorLit, m5);
    std::printf("    LATTICE (15 spheres, camera 25.5 m, sun 10 deg): %u sunlit sphere px, %u sunlit floor px, "
                "%u contact px\n", nSphere, nFloor, nContact);
    std::printf("      map acne  (map vs no casters, off the shadow lanes): sphere %u px (mean %.4f), floor %u px (mean %.4f)\n",
                mapAcneSphere, double(m0), mapAcneFloor, double(m1));
    std::printf("      ray acne  (row on vs map): full sphere %u px (%.4f) floor %u px (%.4f); "
                "half sphere %u px (%.4f) floor %u px (%.4f)\n", rayAcneSphere, double(m2), rayAcneFloor,
                double(m3), rayAcneSphereH, double(m4), rayAcneFloorH, double(m5));
    CHECK_MSG(nSphere > 1000 && nFloor > 10000 && nContact > 300, "the lattice's classes are populated");
    CHECK_MSG(mapAcneSphere + mapAcneFloor <= (nSphere + nFloor) / 1000u,
              "THE MAP HAS NO ACNE at its constant bias: %u of %u sunlit px darker than no-casters by > 4 codes",
              mapAcneSphere + mapAcneFloor, nSphere + nFloor);
    CHECK_MSG(rayAcneSphere == 0 && rayAcneFloor == 0 && rayAcneSphereH == 0 && rayAcneFloorH == 0,
              "THE RAY HAS NO ACNE: no sunlit sphere or floor pixel is darkened by the row (full and half)");
    // THE CONTACT under every sphere: analytically deep-shadowed floor within
    // 1.2 m of each sphere's foot must read as shadow with the row on.
    float litRef = 0.0f, deep = 0.0f; unsigned nl = 0, nd = 0;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const unsigned char k = gKind[size_t(y) * kSize + x];
            if (k == FloorLit) { litRef += luma(noCast, x, y); ++nl; }
            if (k == Contact) { deep += luma(onFull, x, y); ++nd; }
        }
    litRef /= float(std::max(nl, 1u)); deep /= float(std::max(nd, 1u));
    const float thr = 0.5f * (litRef + deep);
    unsigned leakOff = 0, leakOn = 0, leakHalf = 0;
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            if (gKind[size_t(y) * kSize + x] != Contact) continue;
            leakOff += luma(off, x, y) > thr;
            leakOn += luma(onFull, x, y) > thr;
            leakHalf += luma(onHalf, x, y) > thr;
        }
    std::printf("      contact under the spheres (lit %.1f, shadow %.1f): lit px — map %u, row full %u, half %u\n",
                double(litRef), double(deep), leakOff, leakOn, leakHalf);
    // THE MAP DRAWS THE CONTACT (PHOTON-SCENE-SWITCH-1). This row used to assert
    // the opposite — "the map leaves the contact lit at a grazing sun, its
    // normal-offset bias moves the shadow ~1 m downstream" (433 of 433 px) — and
    // that picture was the coarse levels' garbage caster geometry (the mixed
    // shadow-VAO list, buildShadowVaos' note): with every level cast through its
    // own shrunk VAO the map shadows every contact pixel, in either scene order.
    CHECK_MSG(leakOff == 0u,
              "THE MAP DRAWS THE CONTACT SHADOW under every sphere (%u of %u contact px lit): the casters are "
              "each LOD level's own triangles", leakOff, nContact);
    CHECK_MSG(leakOn == 0 && leakHalf == 0, "THE CONTACT UNDER EVERY SPHERE IS CLOSED (full %u, half %u lit px)",
              leakOn, leakHalf);
    // ---- THE GLASS (F1): the pane over the middle row's feet must show the
    // map's answer, never the floor's contact ray read through the glass.
    {
        unsigned darkened = 0; double sum = 0.0;
        for (unsigned y = 0; y < kSize; ++y)
            for (unsigned x = 0; x < kSize; ++x) {
                if (gKind[size_t(y) * kSize + x] != PaneContact) continue;
                // Against the SHORTEST range (both arms carry the prepass, whose
                // own treatment of a blended pane is not this row's): a ray
                // answer read through the glass darkens the pane in the full
                // range arm only.
                const float dl = luma(onShort, x, y) - luma(onFull, x, y);
                sum += std::max(0.0f, dl);
                if (dl > 4.0f) ++darkened;
            }
        std::printf("      glass over the contact: %u pane px, %u darkened by the row > 4 codes (mean %.4f)\n",
                    nPane, darkened, nPane ? sum / nPane : 0.0);
        CHECK_MSG(nPane > 100, "the pane covers the middle row's contact (%u px)", nPane);
        CHECK_MSG(darkened == 0, "GLASS KEEPS THE MAP'S ANSWER: no pane pixel over the contact is darkened by the "
                                 "row (%u)", darkened);
    }
    s->setSunContact(SunContactDesc());
    view->setScene(nullptr);
    e->destroyView(view);
    e->destroyScene(s);
}

static int costMain(Engine *e)
{
    // THE COST AT 1080p: a field of 100 crates, a camera over it. Arms paired in
    // ONE process, alternated, the frame index irrelevant (the job has none).
    View *view = e->createOffscreenView("suncontactcost", 1920, 1080, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("suncontactcost");
    if (!view || !s || !view->setScene(s)) { std::printf("FAIL: view/scene\n"); return 1; }
    buildFixture(s, 100);
    view->setShadows(true);
    enginetest::testCameraLookAt(view, Vec3(0.0f, 9.0f, 16.0f), Vec3(0.0f, 0.0f, -4.0f));
    e->setFrameMonitor(MonitorLevel::Review);
    struct Arm { const char *name; bool contact; int ssr; SunContactResolution res; };
    const Arm arms[] = {
        { "off (no prepass)", false, 0, SunContactResolution::Full },
        { "on, full", true, 0, SunContactResolution::Full },
        { "on, half", true, 0, SunContactResolution::Half },
        { "ssr prepass, off", false, 1, SunContactResolution::Full },
        { "ssr prepass, on full", true, 1, SunContactResolution::Full },
    };
    const int kArms = int(sizeof(arms) / sizeof(arms[0]));
    std::vector<double> frameSum(kArms, 0.0), jobSum(kArms, 0.0);
    std::vector<int> frameN(kArms, 0), jobN(kArms, 0);
    for (int round = 0; round < 4; ++round) {
        for (int a = 0; a < kArms; ++a) {
            PostFxDesc fx;
            fx.allowOffscreen = true;
            fx.ssr = arms[a].ssr;
            view->setPostFx(fx);
            SunContactDesc sc;
            sc.enabled = arms[a].contact;
            sc.resolution = arms[a].res;
            s->setSunContact(sc);
            render(e, 30);                           // warm: the shape rebuilt, the queries back
            std::vector<FrameRecord> drop;
            e->takeFrameRecords(drop);
            render(e, 60);
            std::vector<FrameRecord> recs;
            for (int k = 0; k < 8; ++k) { render(e, 1); e->takeFrameRecords(recs); }
            for (const FrameRecord &r : recs) {
                if (r.gpuMs > 0.0f) { frameSum[a] += r.gpuMs; ++frameN[a]; }
                for (const CacheWork &w : r.cacheWork)
                    if (w.detail == "sun.contact" && w.gpuMs >= 0.0f) { jobSum[a] += w.gpuMs; ++jobN[a]; }
            }
            const SunContactStatus st = s->sunContactStatus();
            if (arms[a].contact && st.gpuMs >= 0.0f && jobN[a] == 0) { jobSum[a] += st.gpuMs; ++jobN[a]; }
        }
    }
    e->setFrameMonitor(MonitorLevel::Off);
    std::printf("    arm                        passes GPU ms   job GPU ms   (frames)\n");
    for (int a = 0; a < kArms; ++a)
        std::printf("    %-26s %10.4f   %10.4f   (%d / %d)\n", arms[a].name,
                    frameN[a] ? frameSum[a] / frameN[a] : -1.0, jobN[a] ? jobSum[a] / jobN[a] : -1.0,
                    frameN[a], jobN[a]);
    const auto mean = [&](int a) { return frameN[a] ? frameSum[a] / frameN[a] : -1.0; };
    const auto job = [&](int a) { return jobN[a] ? jobSum[a] / jobN[a] : -1.0; };
    if (mean(0) > 0.0 && mean(3) > 0.0) {
        std::printf("    RATIOS (paired arms, one process):\n");
        std::printf("      job full / passes (on, full)          %.4f\n", job(1) / mean(1));
        std::printf("      job half / passes (on, half)          %.4f\n", job(2) / mean(2));
        std::printf("      job full / job half                   %.4f\n", job(1) / job(2));
        std::printf("      (passes + job) on full / off          %.4f  (the prepass included)\n",
                    (mean(1) + job(1)) / mean(0));
        std::printf("      (passes + job) on / off under SSR     %.4f  (the prepass already paid)\n",
                    (mean(4) + job(4)) / mean(3));
    }
    return 0;
}


// ---------------------------------------------------------------------------
// THE GAP THE MAP LEAVES BY CONSTRUCTION (PHOTON-SCENE-SWITCH-2) — `gi.sun_contact_legs`
// and `gi.sun_contact_legs_norays` (`--legs`).
//
// After the shadow-VAO fix no other arm has a map gap for the ray to close (the
// board's is 0.0 mm, the lattice's 0 of 433 px), so the ray's benefit was
// unmeasured. This fixture is built so the MAP must miss by arithmetic:
// a 1 x 1 m board 2 cm thick on four 5 cm legs (its top 7 cm above the floor)
// under a GRAZING sun (10 degrees), seen from ~15 m, with a 1024 px shadow atlas.
//
// THE ARITHMETIC. Upstream's receiver-side normal offset
// (ShadowMapping_piece_all.any getNormalOffsetBias) moves the floor's lookup point
// ALONG ITS NORMAL by (1 - N.L) x normalOffsetBias x autoScale / splitTexels
// WORLD UNITS: N.L = sin 10 deg = 0.174, normalOffsetBias = 168 (the pin's
// default), autoScale = 1 + 4 x orthoSize / shadowFar >= 1, and a split has at most
// 1024 texels (the atlas plan: split 0 R, splits 1-2 R/2). So the lift is AT LEAST
// 0.826 x 168 / 1024 = 0.136 m — twice the board's 0.07 m top. A floor point in the
// board's shadow is looked up 13.6+ cm above the floor, where the sun's ray passes
// OVER the board: the map shadows NONE of the board's shadow (prediction: every
// pixel of the analytic shadow class lit, less the class's edge quantum).
//
// THE RAY'S OWN RESIDUAL, by the same arithmetic: the contact ray starts TWO
// full-resolution pixel footprints off the receiver (rq_sun_contact.comp's bias
// rule; OgreRayQuery.cpp kSunContactLiftFootprints = 2, floor kSunContactMinBias
// = 1 mm): 2 x 2 tan(10 deg) / 640 x view depth = 1.65 cm at 15 m. A floor point
// whose LIFTED ray passes over the board's 7 cm top is not closed — the shadow's
// last lift / tan(10 deg) ~= 9.4 cm. So the ray must close EVERY class pixel whose
// lifted ray still meets the board (0 px left lit there), and leave lit exactly
// the pixels the lift predicts (measured on the first run: 501 predicted, 501
// lit, the same pixels).
namespace legs {
static const Vec3 kPos(6.3f, 7.0f, 12.0f), kTarget(0.3f, 0.0f, 0.0f);
static const float kFovL = 20.0f, kElev = 10.0f;
static const float kHalf = 0.5f, kLegH = 0.05f, kBoardT = 0.02f, kLeg = 0.02f;
static bool hitBox(const Vec3 &o, const Vec3 &d, const Vec3 &mn, const Vec3 &mx, float tMax)
{
    float t0 = 0.0f, t1 = tMax;
    const float oo[3] = { o.x, o.y, o.z }, dd[3] = { d.x, d.y, d.z };
    const float lo[3] = { mn.x, mn.y, mn.z }, hi[3] = { mx.x, mx.y, mx.z };
    for (int a = 0; a < 3; ++a) {
        if (std::fabs(dd[a]) < 1e-9f) { if (oo[a] < lo[a] || oo[a] > hi[a]) return false; continue; }
        float ta = (lo[a] - oo[a]) / dd[a], tb = (hi[a] - oo[a]) / dd[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}
}   // namespace legs

static int legsArm(Engine *e, bool raysWanted, const char *dumpDir)
{
    using namespace legs;
    e->setShadowResolution(1024);
    View *view = e->createOffscreenView("sunlegs", kSize, kSize, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("sunlegs");
    if (!view || !s || !view->setScene(s)) { std::printf("FAIL: legs view/scene\n"); return 1; }
    const bool haveRays = e->rayQueryAvailable() && e->rayTracing();
    if (raysWanted && !haveRays) {
        std::printf("ok: no ray queries on this machine — gi.sun_contact_legs skips cleanly\n");
        return 0;
    }
    view->setShadows(true);
    s->setAmbient(Colour(0.30f, 0.34f, 0.40f), Colour(0.20f, 0.20f, 0.20f));
    const NodeId floor = enginetest::addTestCube(s, Colour(0.7f, 0.7f, 0.7f), 0.0f, 1.0f);
    enginetest::setNodeScale(s, floor, Vec3(60.0f, 0.2f, 60.0f));
    enginetest::setNodePosition(s, floor, Vec3(0.0f, -0.1f, 0.0f));
    const NodeId board = enginetest::addTestCube(s, Colour(0.55f, 0.45f, 0.35f), 0.0f, 1.0f);
    enginetest::setNodeScale(s, board, Vec3(2.0f * kHalf, kBoardT, 2.0f * kHalf));
    enginetest::setNodePosition(s, board, Vec3(0.0f, kLegH + 0.5f * kBoardT, 0.0f));
    const float lx[2] = { -kHalf + kLeg, kHalf - kLeg };
    for (float x : lx)
        for (float z : lx) {
            const NodeId leg = enginetest::addTestCube(s, Colour(0.55f, 0.45f, 0.35f), 0.0f, 1.0f);
            enginetest::setNodeScale(s, leg, Vec3(kLeg, kLegH, kLeg));
            enginetest::setNodePosition(s, leg, Vec3(x, 0.5f * kLegH, z));
        }
    const float el = kElev * kPi / 180.0f;
    enginetest::addDirectionalLight(s, Vec3(std::cos(el), -std::sin(el), 0.0f), kSunPower);
    {
        CameraDesc c = enginetest::testCameraDescLookAt(kPos, kTarget);
        c.fovDegrees = kFovL;
        view->setCamera(c);
    }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    view->setPostFx(fx);

    // The analytic classes: floor the camera SEES (its view ray clears the board
    // and the legs, grown 1 cm), SHADOWED when the sun ray from it passes through
    // the board shrunk by 1 cm in x and z, SUNLIT when it clears the board grown
    // by 10 cm.
    const Vec3 toSun(-std::cos(el), std::sin(el), 0.0f);
    const Vec3 bMin(-kHalf, kLegH, -kHalf), bMax(kHalf, kLegH + kBoardT, kHalf);
    std::vector<unsigned> shadowPx, litPx;
    std::vector<unsigned char> liftClears(size_t(kSize) * kSize, 0u);   // the ray's own residual
    const float footprint = 2.0f * 2.0f * std::tan(0.5f * kFovL * kPi / 180.0f) / float(kSize);
    Vec3 fwd(kTarget.x - kPos.x, kTarget.y - kPos.y, kTarget.z - kPos.z);
    {
        const float l = std::sqrt(fwd.x * fwd.x + fwd.y * fwd.y + fwd.z * fwd.z);
        fwd = Vec3(fwd.x / l, fwd.y / l, fwd.z / l);
    }
    for (unsigned y = 0; y < kSize; ++y)
        for (unsigned x = 0; x < kSize; ++x) {
            const Vec3 g = enginetest::groundPointForPixel(kPos, kTarget, x, y, kSize, 0.0f, kFovL);
            const Vec3 v(g.x - kPos.x, g.y - kPos.y, g.z - kPos.z);
            if (hitBox(kPos, v, Vec3(bMin.x - 0.01f, 0.0f, bMin.z - 0.01f),
                       Vec3(bMax.x + 0.01f, bMax.y + 0.01f, bMax.z + 0.01f), 1.0f))
                continue;
            const Vec3 o(g.x, 0.001f, g.z);
            if (hitBox(o, toSun, Vec3(bMin.x + 0.01f, bMin.y, bMin.z + 0.01f),
                       Vec3(bMax.x - 0.01f, bMax.y, bMax.z - 0.01f), 1e30f)) {
                shadowPx.push_back(y * kSize + x);
                const float depth = v.x * fwd.x + v.y * fwd.y + v.z * fwd.z;
                const float lift = std::max(footprint * depth, 0.001f);
                liftClears[size_t(y) * kSize + x] = !hitBox(Vec3(g.x, lift, g.z), toSun, bMin, bMax, 1e30f);
            }
            else if (!hitBox(o, toSun, Vec3(bMin.x - 0.1f, 0.0f, bMin.z - 0.1f),
                             Vec3(bMax.x + 0.1f, bMax.y + 0.1f, bMax.z + 0.1f), 1e30f) &&
                     std::fabs(g.x) < 4.0f && std::fabs(g.z) < 4.0f)
                litPx.push_back(y * kSize + x);
        }
    std::printf("    LEGS (a 1 m board 2 cm thick on 5 cm legs, sun 10 deg, camera %.1f m): %zu shadowed px, "
                "%zu sunlit px\n", double(std::sqrt((kPos.x - kTarget.x) * (kPos.x - kTarget.x) +
                                                      kPos.y * kPos.y + kPos.z * kPos.z)),
                shadowPx.size(), litPx.size());
    CHECK_MSG(shadowPx.size() > 300 && litPx.size() > 10000, "the legs fixture's classes are populated");

    const auto shot = [&](bool on, SunContactResolution res, Image &out) {
        SunContactDesc sc;
        sc.enabled = on;
        sc.resolution = res;
        s->setSunContact(sc);
        render(e, kSettleFrames);
        return view->readPixels(out);
    };
    render(e, 60);   // a new scene's first frames carry no sun shadow yet
    {
        const ShadowStatus st = e->shadowStatus();
        std::printf("    atlas %u px base (%ux%u): the largest split has %u texels, so the floor's lookup is lifted "
                    ">= 0.826 x 168 / %u = %.3f m against the board's %.2f m top\n",
                    st.resolution, st.atlasWidth, st.atlasHeight, st.resolution, st.resolution,
                    double(0.826f * 168.0f / float(std::max(1u, st.resolution))), double(kLegH + kBoardT));
    }
    Image off, onFull, onHalf;
    CHECK(shot(false, SunContactResolution::Full, off), "legs: read back, row off");
    CHECK(shot(true, SunContactResolution::Full, onFull), "legs: read back, row on (full)");
    CHECK(shot(true, SunContactResolution::Half, onHalf), "legs: read back, row on (half)");
    if (dumpDir) {
        savePpm(off, std::string(dumpDir) + "/legs_off.ppm");
        savePpm(onFull, std::string(dumpDir) + "/legs_on_full.ppm");
    }
    if (!raysWanted) {
        CHECK_MSG(off.rgba == onFull.rgba && off.rgba == onHalf.rgba,
                  "legs, no rays: the row on renders EXACTLY the row-off picture");
        std::printf("%s\n", failures ? "gi.sun_contact_legs_norays: FAILED" : "gi.sun_contact_legs_norays: all ok");
        return failures ? 1 : 0;
    }
    const auto mean = [&](const Image &img, const std::vector<unsigned> &px) {
        double sum = 0.0;
        for (unsigned i : px) sum += luma(img, i % kSize, i / kSize);
        return px.empty() ? 0.0f : float(sum / double(px.size()));
    };
    const float lit = mean(off, litPx), deep = mean(onFull, shadowPx);
    const float thr = 0.5f * (lit + deep);
    unsigned nOff = 0, nFull = 0, nHalf = 0, predicted = 0, fullInReach = 0, fullPredicted = 0;
    for (unsigned i : shadowPx) {
        const bool clears = liftClears[i] != 0u;
        const bool fullLit = luma(onFull, i % kSize, i / kSize) > thr;
        nOff += luma(off, i % kSize, i / kSize) > thr;
        nFull += fullLit;
        nHalf += luma(onHalf, i % kSize, i / kSize) > thr;
        predicted += clears;
        fullInReach += fullLit && !clears;
        fullPredicted += fullLit && clears;
    }
    std::printf("    the board's shadow (sunlit floor %.1f, ray-closed shadow %.1f): lit px — map %u of %zu, "
                "row full %u (the lift predicts %u; %u of them lit, %u lit outside it), half %u\n",
                double(lit), double(deep), nOff, shadowPx.size(), nFull, predicted, fullPredicted, fullInReach,
                nHalf);
    CHECK_MSG(lit - deep > 40.0f, "legs: the ray's shadow is a shadow (%.1f vs %.1f codes)", double(lit), double(deep));
    CHECK_MSG(nOff + shadowPx.size() / 50u >= shadowPx.size(),
              "THE MAP LEAVES THE GAP, as the arithmetic predicts: %u of %zu shadowed px lit (all, within the "
              "class's 2 %% edge quantum)", nOff, shadowPx.size());
    CHECK_MSG(fullInReach == 0u,
              "THE RAY CLOSES IT at full resolution: 0 px lit wherever its lifted origin still meets the board "
              "(%u of %zu)", fullInReach, shadowPx.size() - predicted);
    CHECK_MSG(fullPredicted + predicted / 50u >= predicted && nFull <= predicted + predicted / 50u,
              "...and what it leaves lit is exactly its own lift's tail: %u px lit, %u predicted (2 %% quantum)",
              nFull, predicted);
    std::printf("%s\n", failures ? "gi.sun_contact_legs: FAILED" : "gi.sun_contact_legs: all ok");
    return failures ? 1 : 0;
}

// THE BOARD ARM (`gi.sun_contact`, `gi.sun_contact_norays`, and the first half of
// `--both`). Its view and scene stay alive when it returns: under `--both` the
// lattice is drawn beside them, two scenes through two views every frame.
enum BoardResult { BoardRan, BoardDone, BoardFatal };
static BoardResult boardArm(Engine *e, bool raysWanted, const char *dumpDir)
{
    View *view = e->createOffscreenView("suncontact", kSize, kSize, Colour(0.45f, 0.55f, 0.70f));
    Scene *s = e->createScene("suncontact");
    if (!view || !s || !view->setScene(s)) {
        std::printf("FAIL: view/scene: %s\n", e->lastError().c_str());
        return BoardFatal;
    }
    // Asked AFTER the first view exists: the device is made with it.
    const bool haveRays = e->rayQueryAvailable() && e->rayTracing();
    if (raysWanted && !haveRays) {
        std::printf("ok: this build/machine has no ray queries (available=%d wanted=%d) — "
                    "gi.sun_contact is about the ray job and skips cleanly; "
                    "gi.sun_contact_norays covers the fallback picture\n",
                    int(e->rayQueryAvailable()), int(e->rayTracing()));
        return BoardDone;
    }
    buildFixture(s, 1);
    view->setShadows(true);
    {
        CameraDesc c = enginetest::testCameraDescLookAt(kCamPos, kCamTarget);
        c.fovDegrees = kFov;
        view->setCamera(c);
    }
    PostFxDesc fx;
    fx.allowOffscreen = true;
    view->setPostFx(fx);
    buildFloorSamples();

    const auto shot = [&](bool on, SunContactResolution res, float range, Image &out) {
        SunContactDesc sc;
        sc.enabled = on;
        sc.resolution = res;
        sc.range = range;
        s->setSunContact(sc);
        render(e, kSettleFrames);
        return view->readPixels(out);
    };

    // ---- THE DEFAULT IS OFF ------------------------------------------------
    CHECK(!s->sunContact().enabled, "the row is off by default");
    CHECK(!s->sunContactStatus().on, "...and so is the job");

    Image off, onFull, onHalf, onShort, offAgain;
    CHECK(shot(false, SunContactResolution::Full, kSunContactDefaultRange, off), "read back: row off");
    CHECK(shot(true, SunContactResolution::Full, kSunContactDefaultRange, onFull), "read back: row on, full");
    const SunContactStatus stFull = s->sunContactStatus();
    CHECK(shot(true, SunContactResolution::Half, kSunContactDefaultRange, onHalf), "read back: row on, half");
    const SunContactStatus stHalf = s->sunContactStatus();
    CHECK(shot(true, SunContactResolution::Full, kSunContactMinRange, onShort), "read back: row on, shortest range");
    CHECK(shot(false, SunContactResolution::Full, kSunContactDefaultRange, offAgain), "read back: row off again");

    if (dumpDir) {
        const std::string d(dumpDir);
        savePpm(off, d + "/sun_contact_off.ppm");
        savePpm(onFull, d + "/sun_contact_on_full.ppm");
        savePpm(onHalf, d + "/sun_contact_on_half.ppm");
        savePpm(onShort, d + "/sun_contact_on_short.ppm");
    }

    // ---- THE ROW OFF AGAIN IS THE ROW OFF (the job gives everything back) ---
    CHECK_MSG(off.rgba == offAgain.rgba, "turning the row off restores the picture byte for byte");

    if (!raysWanted) {
        // ---- THE NO-RAYS PICTURE ---------------------------------------------
        CHECK(!stFull.on, "without ray queries the row does not resolve on");
        CHECK_MSG(off.rgba == onFull.rgba && off.rgba == onHalf.rgba,
                  "without ray queries the row on renders EXACTLY the row-off picture");
        std::printf("%s\n", failures ? "gi.sun_contact_norays: FAILED" : "gi.sun_contact_norays: all ok");
        return BoardDone;
    }

    // ---- THE ROW'S CONTRACT ------------------------------------------------
    std::printf("    status (full): on=%d running=%d %ux%u divisor %u rays %llu range %.2f "
                "toSun (%.3f, %.3f, %.3f) gpu %.4f ms cpu %.4f ms reason '%s'\n",
                int(stFull.on), int(stFull.running), stFull.width, stFull.height, stFull.divisor,
                stFull.rays, double(stFull.range), double(stFull.toSun[0]), double(stFull.toSun[1]),
                double(stFull.toSun[2]), double(stFull.gpuMs), double(stFull.cpuMs),
                stFull.reason.c_str());
    CHECK(stFull.on && stFull.running, "the job runs for the view with the row on");
    CHECK_MSG(stFull.divisor == 1u && stFull.width == kSize && stFull.height == kSize,
              "full resolution: one ray per pixel (%ux%u, divisor %u)", stFull.width,
              stFull.height, stFull.divisor);
    CHECK_MSG(stHalf.divisor == 2u && stHalf.width == kSize / 2u && stHalf.height == kSize / 2u,
              "half resolution: one ray per 2x2 block (%ux%u, divisor %u)", stHalf.width,
              stHalf.height, stHalf.divisor);
    {
        const float e = kSunElevationDeg * kPi / 180.0f;
        const float want[3] = { -std::cos(e), std::sin(e), 0.0f };   // towards the sun
        float dev = 0.0f;
        for (int i = 0; i < 3; ++i) dev = std::max(dev, std::fabs(stFull.toSun[i] - want[i]));
        CHECK_MSG(dev < 1e-3f, "the rays are cast towards the sun (worst axis off by %.5f)", double(dev));
    }

    // ---- THE REFERENCES ------------------------------------------------------
    unsigned nLit = 0, nShadow = 0;
    const float lit = regionMean(off, 1.0f, 2.5f, 0.8f, 1.5f, &nLit);       // sunlit floor
    const float shadow = regionMean(off, 1.5f, 2.0f, -0.3f, 0.3f, &nShadow);  // deep shadow
    const float threshold = 0.5f * (lit + shadow);
    std::printf("    sunlit floor %.1f (%u px), deep shadow %.1f (%u px), threshold %.1f\n",
                double(lit), nLit, double(shadow), nShadow, double(threshold));
    CHECK_MSG(nLit > 200 && nShadow > 200 && lit - shadow > 40.0f,
              "the fixture draws a sunlit floor and a shadow on it (%.1f vs %.1f codes)",
              double(lit), double(shadow));

    // ---- 1. THE GAP ----------------------------------------------------------
    const float px = pixelFootprintAtContact();
    const float gapOff = contactGap(off, threshold);
    const float gapFull = contactGap(onFull, threshold);
    const float gapHalf = contactGap(onHalf, threshold);
    std::printf("    THE CONTACT GAP (lit floor beside the shadowed face; one pixel = %.2f mm):\n"
                "      the shadow map alone   %.1f mm  (%.2f px)\n"
                "      + rays, full           %.1f mm  (%.2f px, bar 1 texel = %.2f mm)\n"
                "      + rays, half           %.1f mm  (%.2f px, bar 1 texel = %.2f mm)\n",
                double(px * 1000.0f), double(gapOff * 1000.0f), double(gapOff / px),
                double(gapFull * 1000.0f), double(gapFull / px), double(px * 1000.0f),
                double(gapHalf * 1000.0f), double(gapHalf / px), double(2.0f * px * 1000.0f));
    // THE MAP'S OWN LEAK IS GONE (fix round F3): at upstream's constant bias it
    // was 43.9 mm (6.2 px) here; the sun's bias x 0.3 (OgreShadow.cpp) takes it
    // to 0. A bias creeping back up reds this line.
    CHECK_MSG(gapOff <= 1.0f * px + 1e-4f,
              "THE MAP NO LONGER LEAKS at the board: %.1f mm of light at the contact (<= 1 px)",
              double(gapOff * 1000.0f));
    CHECK_MSG(gapFull <= 1.0f * px + 1e-4f,
              "THE GAP CLOSES (full): %.2f mm <= one texel (%.2f mm)", double(gapFull * 1000.0f),
              double(px * 1000.0f));
    CHECK_MSG(gapHalf <= 2.0f * px + 1e-4f,
              "THE GAP CLOSES (half): %.2f mm <= one texel (%.2f mm)", double(gapHalf * 1000.0f),
              double(2.0f * px * 1000.0f));

    // ---- 2. THE FAR SHADOW IS THE MAP'S --------------------------------------
    // Floor whose ray meets the face beyond the 2 m range: (x - 0.5) / cos(15)
    // > 2 for x > 2.44. Both arms carry the prepass, and the shortest range
    // answers nothing there either — so any difference is the ray's.
    const float farDiff = regionDiff(onFull, onShort, 2.6f, 3.8f, -0.3f, 0.3f);
    const float farOffDiff = regionDiff(onFull, off, 2.6f, 3.8f, -0.3f, 0.3f);
    std::printf("    far shadow (x 2.6..3.8): on vs shortest range %.4f codes, on vs row off %.4f codes\n",
                double(farDiff), double(farOffDiff));
    CHECK_MSG(farDiff == 0.0f,
              "BEYOND THE RANGE THE MAP IS ALONE: the far shadow is unchanged (%.4f codes)", double(farDiff));
    CHECK_MSG(regionMean(onFull, 2.6f, 3.8f, -0.3f, 0.3f) < threshold,
              "...and it is still a shadow (the map's)");

    // ---- 3. NO ACNE ------------------------------------------------------------
    const float litDiff = regionDiff(onFull, onShort, 1.0f, 2.5f, 0.8f, 1.5f);
    const float litHalfDiff = regionDiff(onHalf, onShort, 1.0f, 2.5f, 0.8f, 1.5f);
    std::printf("    sunlit floor: full vs shortest %.4f codes, half vs shortest %.4f codes\n",
                double(litDiff), double(litHalfDiff));
    CHECK_MSG(litDiff == 0.0f && litHalfDiff == 0.0f,
              "NO ACNE: the sunlit floor is untouched by the rays (%.4f / %.4f codes)",
              double(litDiff), double(litHalfDiff));
    // FOR THE RECORD, not a bar: what the PREPASS alone moves (the row on gives a
    // view the prepass, whose G-buffer shadow term is quantised) — the same
    // difference any view pays when its SSR row turns on.
    std::printf("    the prepass alone (row on vs row off, sunlit floor): %.4f codes\n",
                double(regionDiff(onShort, off, 1.0f, 2.5f, 0.8f, 1.5f)));

    // ---- 4. THE SEAM AT THE RANGE (fix round F3) -------------------------------
    // Along the shadow's side edge (the strip where the ray's exact outline and
    // the map's differ), in 5 cm bins of the distance a sun ray travels to the
    // board: the largest jump between adjacent bins across the range's end is
    // the step the eye reads. The fade turns it into a ramp.
    {
        const float e = kSunElevationDeg * kPi / 180.0f;
        const auto edgeProfileStep = [&](const Image &img) {
            std::vector<double> sum(80, 0.0); std::vector<unsigned> n(80, 0u);
            for (const Sample &sm : gFloor) {
                if (!(sm.ground.z > 0.40f && sm.ground.z < 0.65f) || sm.ground.x < kFaceX) continue;
                const float d = (sm.ground.x - kFaceX) / std::cos(e);
                const int b = int(d / 0.05f);
                if (b < 0 || b >= 80) continue;
                sum[size_t(b)] += luma(img, sm.x, sm.y); ++n[size_t(b)];
            }
            float step = 0.0f;
            const int b0 = int((kSunContactDefaultRange - 0.6f) / 0.05f), b1 = int((kSunContactDefaultRange + 0.6f) / 0.05f);
            for (int b = b0; b < b1; ++b)
                if (n[size_t(b)] && n[size_t(b + 1)])
                    step = std::max(step, float(std::fabs(sum[size_t(b + 1)] / n[size_t(b + 1)] -
                                                          sum[size_t(b)] / n[size_t(b)])));
            return step;
        };
        std::printf("    THE SEAM at the range's end (largest adjacent 5 cm step on the shadow's edge): "
                    "map alone %.2f codes, row on %.2f codes\n", double(edgeProfileStep(off)),
                    double(edgeProfileStep(onFull)));
    }
    // ---- 5. THE MAP'S OWN ACNE at its constant bias (fix round F3) -------------
    // The sunlit floor with every caster on against the same frame with every
    // caster off: a self-shadowing map darkens it.
    {
        for (NodeId n : gFixtureNodes) s->setNodeCastShadow(n, false);
        Image noCast;
        shot(false, SunContactResolution::Full, kSunContactDefaultRange, noCast);
        for (NodeId n : gFixtureNodes) s->setNodeCastShadow(n, true);
        unsigned acne = 0, total = 0;
        for (const Sample &sm : gFloor) {
            const bool shadowBox = sm.ground.x > kFaceX - 0.2f && sm.ground.x < 4.6f && std::fabs(sm.ground.z) < 0.8f;
            if (shadowBox || std::fabs(sm.ground.x) > 12.0f || std::fabs(sm.ground.z) > 12.0f) continue;
            ++total;
            if (luma(noCast, sm.x, sm.y) - luma(off, sm.x, sm.y) > 4.0f) ++acne;
        }
        std::printf("    the map's acne on the sunlit floor: %u of %u px darker than no-casters by > 4 codes\n",
                    acne, total);
        CHECK_MSG(acne <= total / 1000u, "THE MAP HAS NO ACNE at its constant bias (%u of %u px)", acne, total);
    }

    return BoardRan;
}

int main(int argc, char **argv)
{
    const bool cost = argc > 1 && std::strcmp(argv[1], "--cost") == 0;
    const bool both = argc > 1 && std::strcmp(argv[1], "--both") == 0;
    const bool legsOnly = argc > 1 && std::strcmp(argv[1], "--legs") == 0;
    const char *dumpDir = std::getenv("JAH_SUN_CONTACT_DUMP");   // evidence pictures, a tool
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    const bool raysWanted = !getenv("JAHSHAKA_NO_RAY_QUERY");
    cfg.logFile = raysWanted ? "test-sun-contact-ogre.log" : "test-sun-contact-norays-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    // THE SWEEP'S KNOBS (measurement switches, not modes — the table in
    // spikes/photon-rays-1 was taken with them): the camera's distance from its
    // target along the fixture's own direction, its vertical angle, the
    // caster's thickness (1 = the solid crate), the sun's elevation and power.
    if (const char *v = getenv("JAH_SC_WALL")) kBoard = float(atof(v));
    if (const char *v = getenv("JAH_SC_ELEV")) kSunElevationDeg = float(atof(v));
    if (const char *v = getenv("JAH_SC_POWER")) kSunPower = float(atof(v));
    if (const char *v = getenv("JAH_SC_FOV")) kFov = float(atof(v));
    if (const char *v = getenv("JAH_SC_DIST")) {
        const float d = float(atof(v));
        Vec3 dir(kCamPos.x - kCamTarget.x, kCamPos.y - kCamTarget.y, kCamPos.z - kCamTarget.z);
        const float l = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
        kCamPos = Vec3(kCamTarget.x + dir.x / l * d, kCamTarget.y + dir.y / l * d, kCamTarget.z + dir.z / l * d);
    }
    if (cost) return costMain(e);
    if (legsOnly) return legsArm(e, raysWanted, dumpDir);
    if (both && !raysWanted) { std::printf("FAIL: the lattice arm needs rays\n"); return 1; }

    const BoardResult board = boardArm(e, raysWanted, dumpDir);
    if (board == BoardFatal) return 1;
    if (board == BoardDone) return failures ? 1 : 0;
    std::printf("%s\n", failures ? "gi.sun_contact: FAILED" : "gi.sun_contact: all ok");
    if (both) {
        // THE CURVED CASTERS, AS THE PROCESS'S SECOND SCENE (PHOTON-SCENE-SWITCH-1):
        // the lattice is drawn while the board's view still draws the board, every
        // frame. It used to need a process of its own — after another scene had
        // drawn, its map cast NO shadow — and the cause was not the second scene:
        // the spheres' coarse LOD levels reached the shadow map through the
        // level-0 shrunk vertex layout (a mixed shadow-VAO list; OgreMesh.cpp's
        // buildShadowVaos), garbage whose shape followed where the buffers landed.
        latticeArm(e, dumpDir);
        std::printf("%s\n", failures ? "gi.sun_contact_both: FAILED" : "gi.sun_contact_both: all ok");
    }
    return failures ? 1 : 0;
}
