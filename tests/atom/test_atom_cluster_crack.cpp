// atom.cluster_crack — THE CLUSTER CUT, RENDERED (ATOM stage 2, lane
// ATOM-CLUSTER-1; SPECS/atom/B2_CLUSTER_DAG_DESIGN.md §3). Offscreen, plain grade.
//
// The rule is proven to select ONE cut combinatorially by atom.cluster_cut; this
// suite proves the cut is CRACK-FREE on the rasteriser, where a crack actually
// lives (a T-junction or a moved boundary vertex between two clusters of
// different depth), and measures what the per-cluster cut buys. The draws go
// through the TEST HARNESS (cluster_draw.h, route B2: a rewritten BT_DEFAULT
// index buffer + setPrimitiveRange) — the product draws no cut in stage 2.
//
//   0. THE UPLOAD: the engine's cluster stream (MeshRec::clusterStream), read
//      back from the GPU, is the mirror's expansion of the bake, index for index.
//   A. THE CRACK SWEEP, per fixture (the 20k sphere, the shipped high-poly models
//      and three primitives): the object is drawn TWICE — lit, and wound
//      backwards in an unlit INSIDE colour, so the inside copy shows exactly
//      where a view ray has passed THROUGH the surface. A CRACK is a pixel inside
//      the level-0 silhouette — eroded by the cut's own allowed deviation in
//      pixels plus one, so a silhouette that legitimately moves inward is not
//      counted — that shows the background or the inside colour. ZERO, not few,
//      at every threshold of a 16-step sweep from level 0 to the root, and at
//      four view-dependent tolerances.
//   B. THE DOLLY THROUGH THE CUT: atom.dolly_gate's walk (40 m -> 2 m, one frame
//      per 0.25 m) on the 20k sphere with the per-view cluster cut at the view's
//      pixel budget, against the level-0 control at the same poses — the excess
//      of every step over the control <= 3x the walk's ordinary step. The chain
//      is walked over the same poses for the comparison the design asks to quote.
//   C. THE NUMBERS: a 40 m bar seen end-on — the DAG's triangles against the
//      chain's at the same allowed error (the chain has to pick ONE level for the
//      whole bar from its nearest point). The DAG must draw fewer.
//
// THE INSTRUMENT SATURATES AT 1.0 (PFG_RGBA8_UNORM), so the lit fixture sits
// mid-range: mid-grey, one moderate directional light, an ambient floor.
#include "cluster_draw.h"
#include "cluster_fixtures.h"
#include "../support/enginetesthelpers.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <map>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, ...)                                                                   \
    do {                                                                                   \
        if (cond) { std::printf("ok: "); std::printf(__VA_ARGS__); }                       \
        else { std::printf("FAIL: "); std::printf(__VA_ARGS__); ++failures; }              \
        std::printf("\n");                                                                 \
    } while (0)

static const unsigned kSize = 1024;
static const Colour kInside(0.85f, 0.0f, 0.85f);

enum Px : unsigned char { Background = 0, Surface = 1, Inside = 2 };

static std::vector<unsigned char> classify(const Image &img)
{
    std::vector<unsigned char> out(size_t(img.width) * img.height, Background);
    for (unsigned y = 0; y < img.height; ++y)
        for (unsigned x = 0; x < img.width; ++x) {
            const size_t i = (size_t(y) * img.width + x) * 4u;
            const int r = img.rgba[i], g = img.rgba[i + 1], b = img.rgba[i + 2];
            unsigned char k = Background;
            if (r > 120 && b > 120 && 2 * g < std::min(r, b) && std::abs(r - b) < 40) k = Inside;
            else if (std::max(r, std::max(g, b)) > 3) k = Surface;
            out[size_t(y) * img.width + x] = k;
        }
    return out;
}

/// Chebyshev distance (in pixels) from every mask pixel to the nearest non-mask
/// pixel — the erosion radius a pixel survives.
static std::vector<int> insideDistance(const std::vector<unsigned char> &mask, unsigned w, unsigned h)
{
    const int big = 1 << 20;
    std::vector<int> d(mask.size());
    for (size_t i = 0; i < mask.size(); ++i) d[i] = mask[i] ? big : 0;
    auto at = [&](int x, int y) -> int & { return d[size_t(y) * w + size_t(x)]; };
    for (int y = 0; y < int(h); ++y)
        for (int x = 0; x < int(w); ++x) {
            if (!at(x, y)) continue;
            int v = at(x, y);
            if (x == 0 || y == 0) v = 1;
            else v = std::min({ v, at(x - 1, y) + 1, at(x, y - 1) + 1, at(x - 1, y - 1) + 1,
                                x + 1 < int(w) ? at(x + 1, y - 1) + 1 : 1 });
            at(x, y) = v;
        }
    for (int y = int(h) - 1; y >= 0; --y)
        for (int x = int(w) - 1; x >= 0; --x) {
            if (!at(x, y)) continue;
            int v = at(x, y);
            if (x == int(w) - 1 || y == int(h) - 1) v = 1;
            else v = std::min({ v, at(x + 1, y) + 1, at(x, y + 1) + 1, at(x + 1, y + 1) + 1,
                                x > 0 ? at(x - 1, y + 1) + 1 : 1 });
            at(x, y) = v;
        }
    return d;
}

struct Rig
{
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *scene = nullptr;
    MaterialId lit = 0, inside = 0;
    float p11 = 0.0f, height = float(kSize);
    Vec3 eye;
};

static bool frame(Rig &r, Image &img)
{
    r.e->renderOneFrame();
    return r.view->readPixels(img);
}

/// The world size of one pixel at `dist` metres, from this view's projection.
static float footprint(const Rig &r, float dist) { return sampleFootprintPerspective(dist, r.p11, r.height); }

struct Sphere { float c[3]; float r; };
static Sphere boundsOf(const MeshData &d)
{
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (size_t v = 0; v + 2 < d.positions.size(); v += 3)
        for (int k = 0; k < 3; ++k) {
            lo[k] = std::min(lo[k], d.positions[v + size_t(k)]);
            hi[k] = std::max(hi[k], d.positions[v + size_t(k)]);
        }
    Sphere s;
    for (int k = 0; k < 3; ++k) s.c[k] = 0.5f * (lo[k] + hi[k]);
    const float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    s.r = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
    return s;
}

// ---- THE FOLD TEST: is an inside-colour pixel a hole, or the surface folded? ------
//
// Through a CLOSED surface no view ray can reach a back face without first
// crossing a front face — unless the surface FOLDS (a simplified sheet crossing
// another: a region of winding number -1). Through a HOLE it can. The two look the
// same on the screen (the inside copy's colour), so each such pixel is decided on
// the CPU: cast the pixel's own ray WATERTIGHT against the cut's triangles, take
// the first hit, and evaluate the GENERALISED WINDING NUMBER of the cut just in
// FRONT of it and just BEHIND it. The ray has crossed nothing before its first
// hit, so in front of it the winding is the eye's (0) — unless the ray got inside
// without a crossing, through a HOLE (then ~1). Crossing a back face from the
// eye's region steps DOWN by one: -1 behind it is a FOLD. A front and a back
// sheet crossed at (nearly) one depth — a contour, an intersection curve — is a
// TIE the rasteriser broke the other way. The cut being closed (atom.cluster_cut
// counts its open edges: zero) makes the winding an integer, so none of this is
// a judgement call.
struct RayCam { Vec3 eye, f, r, u; float p11 = 1.0f; unsigned w = 1, h = 1; };

static RayCam rayCam(const Vec3 &eye, const Vec3 &target, float p11, unsigned w, unsigned h)
{
    RayCam c;
    c.eye = eye; c.p11 = p11; c.w = w; c.h = h;
    const float fx = target.x - eye.x, fy = target.y - eye.y, fz = target.z - eye.z;
    const float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    c.f = Vec3(fx / fl, fy / fl, fz / fl);
    Vec3 r(c.f.y * 0.0f - c.f.z * 1.0f, c.f.z * 0.0f - c.f.x * 0.0f, c.f.x * 1.0f - c.f.y * 0.0f);   // f x up
    const float rl = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    c.r = Vec3(r.x / rl, r.y / rl, r.z / rl);
    c.u = Vec3(c.r.y * c.f.z - c.r.z * c.f.y, c.r.z * c.f.x - c.r.x * c.f.z, c.r.x * c.f.y - c.r.y * c.f.x);
    return c;
}

/// +1 when the ray reached the INSIDE before its first hit (winding ~1 in front
/// of it: a HOLE), -1 when its first hit is a back face with winding -1 behind it
/// (a FOLD), 0 when the ray hits a FRONT face first, or a front and a back sheet
/// at (nearly) the same depth — the surface IS there along the pixel's ray,
/// and the rasteriser's back-face fragment won a depth tie the ray does not see
/// (at a contour, where a front and a back triangle meet at the rim at equal
/// depth, or on an intersection curve of two interpenetrating parts).
static int foldOrHole(const RayCam &c, unsigned px, unsigned py, const std::vector<float> &pos,
                      const std::vector<unsigned> &idx, float extent, double *windingOut)
{
    const float nx = (2.0f * (float(px) + 0.5f) / float(c.w) - 1.0f) / c.p11;
    const float ny = (1.0f - 2.0f * (float(py) + 0.5f) / float(c.h)) / c.p11;
    double d[3] = { c.f.x + c.r.x * nx + c.u.x * ny, c.f.y + c.r.y * nx + c.u.y * ny,
                    c.f.z + c.r.z * nx + c.u.z * ny };
    const double dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (double &v : d) v /= dl;
    const double o[3] = { c.eye.x, c.eye.y, c.eye.z };
    // WATERTIGHT ray-triangle intersection (Woop, Benthin & Wald 2013): a shared
    // edge's function is computed from the same two vertices on both sides, so it
    // is exactly negated and a ray through the edge hits exactly one of the two
    // triangles. A plain Moller-Trumbore can let a ray that grazes a CONTOUR slip
    // between the front and the back triangle sharing the rim edge, and then reads
    // a closed surface as open — which is what the first cut of this test did.
    int kz = 0;
    if (std::fabs(d[1]) > std::fabs(d[kz])) kz = 1;
    if (std::fabs(d[2]) > std::fabs(d[kz])) kz = 2;
    int kx = (kz + 1) % 3, ky = (kx + 1) % 3;
    if (d[kz] < 0.0) std::swap(kx, ky);
    const double Sx = d[kx] / d[kz], Sy = d[ky] / d[kz], Sz = 1.0 / d[kz];
    double bestT = 1e300; int bestSide = 0;
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const float *a = &pos[idx[t] * 3], *b = &pos[idx[t + 1] * 3], *cc = &pos[idx[t + 2] * 3];
        const double A[3] = { a[0] - o[0], a[1] - o[1], a[2] - o[2] };
        const double B[3] = { b[0] - o[0], b[1] - o[1], b[2] - o[2] };
        const double C[3] = { cc[0] - o[0], cc[1] - o[1], cc[2] - o[2] };
        const double Ax = A[kx] - Sx * A[kz], Ay = A[ky] - Sy * A[kz];
        const double Bx = B[kx] - Sx * B[kz], By = B[ky] - Sy * B[kz];
        const double Cx = C[kx] - Sx * C[kz], Cy = C[ky] - Sy * C[kz];
        const double U = Cx * By - Cy * Bx, V = Ax * Cy - Ay * Cx, W = Bx * Ay - By * Ax;
        if ((U < 0.0 || V < 0.0 || W < 0.0) && (U > 0.0 || V > 0.0 || W > 0.0)) continue;
        const double det = U + V + W;
        if (det == 0.0) continue;
        const double T = U * (Sz * A[kz]) + V * (Sz * B[kz]) + W * (Sz * C[kz]);
        const double tt = T / det;
        if (!(tt > 0.0) || tt >= bestT) continue;
        bestT = tt;
        // The FRONT (CCW) side faces the ray when the geometric normal opposes it.
        const double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
        const double e2[3] = { cc[0] - a[0], cc[1] - a[1], cc[2] - a[2] };
        const double n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                              e1[0] * e2[1] - e1[1] * e2[0] };
        bestSide = (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) < 0.0 ? 1 : -1;
    }
    if (bestT >= 1e300 || bestSide > 0) { if (windingOut) *windingOut = 0.0; return 0; }
    const double eps = 1e-4 * double(extent);
    const auto windingAt = [&](double tt) {
        const double x[3] = { o[0] + d[0] * tt, o[1] + d[1] * tt, o[2] + d[2] * tt };
        double w = 0.0;   // Van Oosterom & Strackee solid angles, / 4 pi
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            double A[3], B[3], C[3];
            for (int k = 0; k < 3; ++k) {
                A[k] = pos[idx[t] * 3 + k] - x[k];
                B[k] = pos[idx[t + 1] * 3 + k] - x[k];
                C[k] = pos[idx[t + 2] * 3 + k] - x[k];
            }
            const double la = std::sqrt(A[0] * A[0] + A[1] * A[1] + A[2] * A[2]);
            const double lb = std::sqrt(B[0] * B[0] + B[1] * B[1] + B[2] * B[2]);
            const double lc = std::sqrt(C[0] * C[0] + C[1] * C[1] + C[2] * C[2]);
            const double det = A[0] * (B[1] * C[2] - B[2] * C[1]) - A[1] * (B[0] * C[2] - B[2] * C[0]) +
                               A[2] * (B[0] * C[1] - B[1] * C[0]);
            const double den = la * lb * lc + (A[0] * B[0] + A[1] * B[1] + A[2] * B[2]) * lc +
                               (A[0] * C[0] + A[1] * C[1] + A[2] * C[2]) * lb +
                               (B[0] * C[0] + B[1] * C[1] + B[2] * C[2]) * la;
            w += 2.0 * std::atan2(det, den);
        }
        return w / (4.0 * 3.14159265358979323846);
    };
    // THE WINDING JUST IN FRONT OF the first hit decides a HOLE: the ray has
    // crossed nothing yet, so in front of the hit it is still in the eye's region
    // (winding 0) unless it got INSIDE without a crossing — through a hole (then
    // it is ~1 there). Behind the hit, -1 is a FOLD. Anything else is a front and
    // a back sheet crossed at (nearly) the same depth — a tie.
    const double before = windingAt(std::max(bestT - eps, 0.0));
    const double after = windingAt(bestT + eps);
    if (windingOut) *windingOut = before;
    if (before > 0.5) return 1;
    return after < -0.5 ? -1 : 0;
}

/// Writes an 8x enlargement of the 40x40 pixels around (cx, cy) as a PPM.
static void dumpCrop(const Image &img, unsigned cx, unsigned cy, const std::string &path)
{
    const int half = 20, zoom = 8;
    FILE *fp = std::fopen(path.c_str(), "wb");
    if (!fp) return;
    std::fprintf(fp, "P6 %d %d 255\n", 2 * half * zoom, 2 * half * zoom);
    for (int y = 0; y < 2 * half * zoom; ++y)
        for (int x = 0; x < 2 * half * zoom; ++x) {
            const int sx = int(cx) - half + x / zoom, sy = int(cy) - half + y / zoom;
            unsigned char rgb[3] = { 0, 0, 0 };
            if (sx >= 0 && sy >= 0 && sx < int(img.width) && sy < int(img.height)) {
                const size_t i = (size_t(sy) * img.width + size_t(sx)) * 4u;
                rgb[0] = img.rgba[i]; rgb[1] = img.rgba[i + 1]; rgb[2] = img.rgba[i + 2];
            }
            // Mark the centre pixel's outline in green.
            if ((x / zoom == half && y / zoom == half) && (x % zoom == 0 || y % zoom == 0 ||
                x % zoom == zoom - 1 || y % zoom == zoom - 1)) { rgb[0] = 0; rgb[1] = 255; rgb[2] = 0; }
            std::fwrite(rgb, 1, 3, fp);
        }
    std::fclose(fp);
}

// ---- A. the crack sweep -------------------------------------------------------
struct SweepResult { size_t worstCracks = 0; std::string worstAt; size_t evaluatedSteps = 0;
                     size_t folds = 0, holes = 0, unexplained = 0; bool controlDone = false; };

static void crackSweep(Rig &r, const clusterfix::Fixture &f, const float dirIn[3], SweepResult &total,
                       const float *eyeIn = nullptr, const float *targetIn = nullptr)
{
    std::string err;
    clusterdraw::ClusterDraw body, back;
    if (!body.create(r.e, r.scene, f.data, r.lit, false, err) ||
        !back.create(r.e, r.scene, f.data, r.inside, true, err)) {
        CHECK(false, "%s: the harness draws it (%s)", f.name.c_str(), err.c_str());
        return;
    }
    const Sphere s = boundsOf(f.data);
    float dir[3] = { dirIn[0], dirIn[1], dirIn[2] };
    const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    for (float &v : dir) v /= dl;
    const float fovHalf = std::atan(1.0f / r.p11);
    float dist = s.r / std::sin(fovHalf) * 1.05f;
    r.eye = Vec3(s.c[0] + dir[0] * dist, s.c[1] + dir[1] * dist, s.c[2] + dir[2] * dist);
    Vec3 target(s.c[0], s.c[1], s.c[2]);
    if (eyeIn && targetIn) {
        // An explicit pose (the bars: a 40 m object framed whole is a hair).
        r.eye = Vec3(eyeIn[0], eyeIn[1], eyeIn[2]);
        target = Vec3(targetIn[0], targetIn[1], targetIn[2]);
        const float dx = s.c[0] - eyeIn[0], dy = s.c[1] - eyeIn[1], dz = s.c[2] - eyeIn[2];
        dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    enginetest::testCameraLookAt(r.view, r.eye, target);
    for (int i = 0; i < 3; ++i) r.e->renderOneFrame();   // settle the pose, in frames

    Image img;
    const bool read = frame(r, img);
    CHECK(read && img.width == kSize, "%s: the level-0 picture reads back", f.name.c_str());
    if (const char *dump = std::getenv("CLUSTER_CRACK_DUMP")) {
        const std::string path = std::string(dump) + "/" + f.name + ".ppm";
        if (FILE *fp = std::fopen(path.c_str(), "wb")) {
            std::fprintf(fp, "P6 %u %u 255\n", img.width, img.height);
            for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) std::fwrite(&img.rgba[i], 1, 3, fp);
            std::fclose(fp);
        }
    }
    const std::vector<unsigned char> level0 = classify(img);
    std::vector<unsigned char> mask(level0.size());
    size_t inside0 = 0, surface0 = 0;
    for (size_t i = 0; i < level0.size(); ++i) {
        mask[i] = level0[i] == Surface ? 1 : 0;
        inside0 += level0[i] == Inside;
        surface0 += level0[i] == Surface;
    }
    const std::vector<int> depthIn = insideDistance(mask, img.width, img.height);
    std::printf("\n-- %s: level 0 covers %zu px (%zu px of inside colour at level 0)\n",
                f.name.c_str(), surface0, inside0);
    // WATERTIGHT or not decides what the INSIDE colour may mean: through a closed
    // level 0 no view ray can meet a back face, so inside colour within the
    // silhouette is a hole; through an OPEN one (the teapot's lid gap, a plane's
    // rim) a simplified border legitimately exposes the far side, and only the
    // design's own signal — the BACKGROUND inside the silhouette — is a crack.
    const bool watertight = [&] {
        std::map<std::array<float, 3>, unsigned> first;
        std::vector<unsigned> canon(f.data.positions.size() / 3);
        for (size_t v = 0; v < canon.size(); ++v)
            canon[v] = first.emplace(std::array<float, 3>{ f.data.positions[v * 3], f.data.positions[v * 3 + 1],
                                                           f.data.positions[v * 3 + 2] }, unsigned(v)).first->second;
        std::map<std::pair<unsigned, unsigned>, int> uses;
        const auto &idx = f.data.indices;
        for (size_t t = 0; t + 2 < idx.size(); t += 3) {
            const unsigned a = canon[idx[t]], b = canon[idx[t + 1]], c = canon[idx[t + 2]];
            if (a == b || b == c || a == c) continue;
            for (auto e : { std::make_pair(a, b), std::make_pair(b, c), std::make_pair(c, a) })
                ++uses[std::minmax(e.first, e.second)];
        }
        for (const auto &kv : uses) if (kv.second != 2) return false;
        return true;
    }();
    std::printf("   level 0 is %s\n", watertight ? "WATERTIGHT (inside colour counts)" : "OPEN (background only)");
    std::printf("   %-12s %-10s %-9s %-10s %-9s %-9s %-9s %s\n", "allowed", "triangles", "erode px",
                "evaluated", "inside", "bg-crack", "in-crack", "(folds/holes/other)");
    const RayCam cam = rayCam(r.eye, target, r.p11, kSize, kSize);

    // The nearest the surface comes to the eye, for the erosion radius.
    const float dNear = std::max(dist - s.r, 1e-3f);
    const auto &groups = f.data.clusterGroups;
    const auto &clusters = f.data.clusters;
    float minErr = FLT_MAX, maxErr = 0.0f;
    for (const MeshClusterGroup &g : groups)
        if (g.error < FLT_MAX) { minErr = std::min(minErr, g.error); maxErr = std::max(maxErr, g.error); }

    struct Step { std::string label; std::vector<unsigned> cut; size_t tris; int erode; };
    std::vector<Step> steps;
    std::vector<unsigned> cut;
    for (int i = 0; i < 16; ++i) {
        const float allowed = i == 0 ? 0.0f
                                     : minErr * 0.5f * std::pow((maxErr * 2.0f) / (minErr * 0.5f), float(i - 1) / 14.0f);
        const size_t t = clusterCutAtAllowed(groups, clusters, allowed, cut);
        const float px = allowed > 0.0f ? allowed / footprint(r, dNear) : 0.0f;
        char label[64];
        std::snprintf(label, sizeof(label), "%.5g", double(allowed));
        steps.push_back({ label, cut, t, int(std::ceil(px)) + 1 });
    }
    // ...and the VIEW-DEPENDENT cut from this pose, at four pixel tolerances.
    for (float tol : { 0.5f, 1.0f, 4.0f, 16.0f }) {
        ClusterCutView v;
        v.eye[0] = r.eye.x; v.eye[1] = r.eye.y; v.eye[2] = r.eye.z;
        v.tolerance = tol;
        v.projScaleY = r.p11;
        v.viewportHeight = r.height;
        const size_t t = clusterCut(groups, clusters, v, cut);
        char label[64];
        std::snprintf(label, sizeof(label), "view %.1fpx", double(tol));
        steps.push_back({ label, cut, t, int(std::ceil(tol)) + 1 });
    }

    size_t worst = 0;
    size_t nonVacuous = 0;
    for (const Step &st : steps) {
        body.setCut(st.cut);
        back.setCut(st.cut);
        frame(r, img);
        const std::vector<unsigned char> px = classify(img);
        size_t evaluated = 0, bgCracks = 0, inCracks = 0, insideTotal = 0;
        std::vector<size_t> inPixels;
        for (size_t i = 0; i < px.size(); ++i) {
            insideTotal += px[i] == Inside;
            if (!mask[i] || depthIn[i] <= st.erode) continue;
            ++evaluated;
            if (px[i] == Background) ++bgCracks;
            // NEW against level 0 by construction: `mask` is level 0's SURFACE,
            // so a pixel that already showed the inside colour at level 0 (the
            // Physics model's interpenetrating parts) is not in it.
            if (px[i] == Inside) { ++inCracks; inPixels.push_back(i); }
        }
        // THE GATE. On an OPEN level 0 only the background inside the silhouette
        // is a crack (a simplified rim legitimately exposes the far side). On a
        // CLOSED one the background is unreachable — a crack there shows the far
        // side's INSIDE — so the inside colour is the signal, and each such pixel
        // is decided by the pixel's own WATERTIGHT ray against the cut: a HOLE (a
        // back face first, winding back near 0 behind it — the ray got inside
        // without crossing the surface) is a crack; a FOLD (winding -1 behind: a
        // simplified sheet crossing another, inside the group's measured error)
        // and a DEPTH TIE (a front face first: the surface is there along the ray,
        // and the rasteriser's back fragment won a tie at a contour or on an
        // intersection curve) are not, and are counted.
        size_t folds = 0, holes = 0, other = 0;
        if (watertight && !inPixels.empty()) {
            std::vector<unsigned> cutIdx;
            for (unsigned c : st.cut)
                cutIdx.insert(cutIdx.end(), f.data.clusterIndices.begin() + f.data.clusters[c].firstIndex,
                              f.data.clusterIndices.begin() + f.data.clusters[c].firstIndex +
                                  f.data.clusters[c].indexCount);
            for (size_t k = 0; k < inPixels.size(); ++k) {
                const unsigned x = unsigned(inPixels[k] % kSize), y = unsigned(inPixels[k] / kSize);
                double w = 0.0;
                const int v = foldOrHole(cam, x, y, f.data.positions, cutIdx, f.extent, &w);
                if (v < 0) ++folds; else if (v > 0) ++holes; else ++other;
                if (k < 2) std::printf("      pixel (%u, %u): %s, winding %.3f in front of the first hit\n", x, y,
                                       v < 0 ? "FOLD" : (v > 0 ? "HOLE" : "TIE (front and back sheet at one depth)"), w);
                if (k == 0)
                    if (const char *dump = std::getenv("CLUSTER_CRACK_DUMP"))
                        dumpCrop(img, x, y, std::string(dump) + "/" + f.name + "-" + st.label + "-crop.ppm");
            }
        }
        const size_t cracks = bgCracks + (watertight ? holes : 0u);
        total.folds += folds;
        total.holes += holes;
        total.unexplained += other;
        if (evaluated > 1000) ++nonVacuous;
        worst = std::max(worst, cracks);
        if (cracks > total.worstCracks) {
            total.worstCracks = cracks;
            total.worstAt = f.name + " @ " + st.label;
        }
        std::printf("   %-12s %-10zu %-9d %-10zu %-9zu %-9zu %-9zu %zu/%zu/%zu\n", st.label.c_str(), st.tris,
                    st.erode, evaluated, insideTotal, bgCracks, inCracks, folds, holes, other);
    }
    total.evaluatedSteps += nonVacuous;

    // THE DETECTOR'S POSITIVE CONTROL, once, on a closed fixture: a cut with ONE
    // leaf cluster facing the eye REMOVED is a real hole, and the test above must
    // call its pixels HOLES — or its zero on every real cut proves nothing.
    if (watertight && !total.controlDone) {
        std::vector<unsigned> leaves;
        int nearest = -1;
        float nearestD = FLT_MAX;
        for (size_t c = 0; c < clusters.size(); ++c) {
            if (clusters[c].refined >= 0) continue;
            leaves.push_back(unsigned(c));
            const float dx = clusters[c].centre[0] - r.eye.x, dy = clusters[c].centre[1] - r.eye.y,
                        dz = clusters[c].centre[2] - r.eye.z;
            const float dd = dx * dx + dy * dy + dz * dz;
            if (dd < nearestD) { nearestD = dd; nearest = int(c); }
        }
        std::vector<unsigned> holed, cutIdx;
        for (unsigned c : leaves)
            if (int(c) != nearest) {
                holed.push_back(c);
                cutIdx.insert(cutIdx.end(), f.data.clusterIndices.begin() + clusters[c].firstIndex,
                              f.data.clusterIndices.begin() + clusters[c].firstIndex + clusters[c].indexCount);
            }
        body.setCut(holed);
        back.setCut(holed);
        frame(r, img);
        const std::vector<unsigned char> px = classify(img);
        size_t candidates = 0, holes = 0, judged = 0;
        for (size_t i = 0; i < px.size(); ++i) {
            if (!mask[i] || px[i] != Inside) continue;
            ++candidates;
            if (candidates % 7 != 1 || judged >= 40) continue;   // a sample: the winding is O(triangles)
            ++judged;
            double w = 0.0;
            if (foldOrHole(cam, unsigned(i % kSize), unsigned(i / kSize), f.data.positions, cutIdx, f.extent,
                           &w) > 0)
                ++holes;
        }
        CHECK(candidates > 0 && judged > 0 && holes == judged,
              "%s: THE POSITIVE CONTROL — one leaf cluster removed shows %zu inside-colour px, and all %zu "
              "judged are HOLES (%zu)", f.name.c_str(), candidates, judged, holes);
        total.controlDone = true;
    }
    CHECK(worst == 0, "%s: NO CRACK at any of the %zu cuts (worst %zu px; %s)", f.name.c_str(),
          steps.size(), worst, watertight ? "closed: background + inside colour that is not a fold"
                                          : "open: background");
    CHECK(nonVacuous >= 8, "%s: %zu of the cuts were judged over more than 1,000 interior pixels",
          f.name.c_str(), nonVacuous);
    body.destroy();
    back.destroy();
}

// ---- B. the dolly ---------------------------------------------------------------
static double maskedMeanAbsDiff(const Image &a, const Image &b)
{
    if (a.width != b.width || a.height != b.height) return 1.0e9;
    double sum = 0.0;
    size_t n = 0;
    for (size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        const int la = std::max({ a.rgba[i], a.rgba[i + 1], a.rgba[i + 2] });
        const int lb = std::max({ b.rgba[i], b.rgba[i + 1], b.rgba[i + 2] });
        if (la <= 3 && lb <= 3) continue;
        ++n;
        sum += std::abs(int(a.rgba[i]) - int(b.rgba[i])) + std::abs(int(a.rgba[i + 1]) - int(b.rgba[i + 1])) +
               std::abs(int(a.rgba[i + 2]) - int(b.rgba[i + 2]));
    }
    return n ? sum / (3.0 * 255.0 * double(n)) : 0.0;
}

struct Walk { std::vector<float> dist; std::vector<double> delta; std::vector<bool> switched; std::vector<size_t> tris; };

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test-atom-cluster-crack-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Rig r;
    r.e = engine.get();
    r.view = r.e->createOffscreenView("crack", kSize, kSize, Colour(0, 0, 0));
    r.scene = r.e->createScene("crack");
    if (!r.view || !r.scene) { std::printf("FAIL: view/scene\n"); return 1; }
    r.view->setScene(r.scene);
    r.scene->setAmbient(Colour(0.10f, 0.10f, 0.12f), Colour(0.06f, 0.06f, 0.08f));
    enginetest::addDirectionalLight(r.scene, Vec3(-0.5f, -0.6f, -0.62f), 2.2f);
    PbrParams p;
    p.albedo = Colour(0.55f, 0.55f, 0.55f);
    p.roughness = 0.55f;
    p.metalness = 0.0f;
    r.lit = r.scene->createPbrMaterial(p);
    // THE INSIDE COPY IS A PBR MATERIAL, black and EMISSIVE, not an unlit one:
    // the unlit family draws both faces (it is the overlay's), and a copy that
    // is not back-face culled covers the whole object instead of only its holes.
    PbrParams q;
    q.albedo = Colour(0.0f, 0.0f, 0.0f);
    q.emissive = kInside;
    q.roughness = 1.0f;
    q.metalness = 0.0f;
    q.receiveShadows = false;
    r.inside = r.scene->createPbrMaterial(q);
    enginetest::testCameraLookAt(r.view, Vec3(0, 0, 5), Vec3(0, 0, 0));
    for (int i = 0; i < 3; ++i) r.e->renderOneFrame();
    GpuCullRequest vr;
    if (!r.e->fillCullView(r.view, vr)) { std::printf("FAIL: fillCullView\n"); return 1; }
    r.p11 = vr.projScaleY;
    r.height = vr.viewportHeight;
    std::printf("the view: %.0f px tall, proj11 %.4f\n", double(r.height), double(r.p11));

    // The fixtures.
    std::vector<clusterfix::Named> named =
        clusterfix::shippedMeshes(JAHSHAKA_TEST_SOURCE_DIR, CLUSTER_FIXTURE_DIR);
    std::vector<clusterfix::Fixture> fx;
    for (const auto &n : named) {
        if (n.name != "uv-sphere-20k" && n.name != "matcaps-dragon" && n.name != "physics-model" &&
            n.name != "torus.obj" && n.name != "teapot.obj" && n.name != "capsule.obj")
            continue;
        clusterfix::Fixture f;
        if (clusterfix::bake(f, n.mesh, n.name) && !f.data.clusters.empty()) fx.push_back(std::move(f));
    }
    CHECK(fx.size() == 6u, "six fixtures carry a DAG (%zu)", fx.size());

    // ---- 0. the upload ----------------------------------------------------------
    {
        const clusterfix::Fixture *dragon = nullptr;
        for (const auto &f : fx) if (f.name == "matcaps-dragon") dragon = &f;
        if (dragon) {
            const MeshId m = r.scene->createMesh(dragon->data);
            std::vector<unsigned> stream;
            const bool read = m && clusterdraw::readClusterStream(r.scene, m, stream, err);
            CHECK(read && stream == dragon->data.clusterIndices,
                  "THE UPLOAD: the dragon's cluster stream on the GPU is the mirror's expansion "
                  "of the bake, index for index (%zu indices, %zu clusters)",
                  stream.size(), dragon->data.clusters.size());
            if (m) r.scene->destroyMesh(m);
        }
        MeshData small = enginetest::unitCubeMesh();
        const MeshId m = r.scene->createMesh(small);
        std::vector<unsigned> none;
        CHECK(m && !clusterdraw::readClusterStream(r.scene, m, none, err),
              "a mesh with no DAG gets no cluster stream");
        if (m) r.scene->destroyMesh(m);
    }

    // ---- A. the crack sweep ---------------------------------------------------
    SweepResult sweep;
    const float dirs[][3] = { { 0.45f, 0.35f, 1.0f }, { -0.8f, 0.5f, -0.3f } };
    for (const auto &f : fx)
        for (int d = 0; d < 2; ++d) crackSweep(r, f, dirs[d], sweep);
    std::printf("\n   THE CRACK SWEEP'S WORST STEP: %zu crack px (%s); inside-colour pixels on the closed "
                "fixtures: %zu folds, %zu holes, %zu ties (a front and a back sheet at one depth)\n", sweep.worstCracks,
                sweep.worstAt.empty() ? "none" : sweep.worstAt.c_str(), sweep.folds, sweep.holes,
                sweep.unexplained);

    // ---- B. the dolly through the cut -------------------------------------------
    const clusterfix::Fixture *sphere = nullptr;
    for (const auto &f : fx) if (f.name == "uv-sphere-20k") sphere = &f;
    if (sphere) {
        const float kFar = 40.0f, kNear = 2.0f, kStep = 0.25f;
        clusterdraw::ClusterDraw body;
        if (!body.create(r.e, r.scene, sphere->data, r.lit, false, err)) {
            CHECK(false, "the dolly's harness: %s", err.c_str());
        } else {
            std::vector<unsigned> leaves;
            for (size_t c = 0; c < sphere->data.clusters.size(); ++c)
                if (sphere->data.clusters[c].refined < 0) leaves.push_back(unsigned(c));
            auto walk = [&](bool cutArm) {
                Walk w;
                enginetest::testCameraLookAt(r.view, Vec3(0, 0, kFar), Vec3(0, 0, 0));
                body.setCut(leaves);
                for (int i = 0; i < 30; ++i) r.e->renderOneFrame();
                Image prev, img;
                std::vector<unsigned> prevCut, cut = leaves;
                for (float dist = kFar; dist >= kNear - 1.0e-4f; dist -= kStep) {
                    enginetest::testCameraLookAt(r.view, Vec3(0, 0, dist), Vec3(0, 0, 0));
                    size_t tris = sphere->triangles;
                    if (cutArm) {
                        ClusterCutView v;
                        v.eye[2] = dist;
                        v.tolerance = kLodBudgetPixels;
                        v.projScaleY = r.p11;
                        v.viewportHeight = r.height;
                        tris = clusterCut(sphere->data.clusterGroups, sphere->data.clusters, v, cut);
                        body.setCut(cut);
                    }
                    frame(r, img);
                    if (prev.width) {
                        w.dist.push_back(dist);
                        w.delta.push_back(maskedMeanAbsDiff(prev, img));
                        w.switched.push_back(cutArm && cut != prevCut);
                        w.tris.push_back(tris);
                    }
                    prev = img;
                    prevCut = cut;
                }
                return w;
            };
            const Walk ref = walk(false);
            const Walk cw = walk(true);
            body.destroy();

            // THE CHAIN over the same poses: the product mesh, its chain, the
            // shipped strategy (hysteresis off: this is a capture view).
            Walk chain;
            {
                const MeshId m = r.scene->createMesh(sphere->data);
                const NodeId n = r.scene->createNode();
                r.scene->attachMesh(n, m, r.lit);
                enginetest::testCameraLookAt(r.view, Vec3(0, 0, kFar), Vec3(0, 0, 0));
                for (int i = 0; i < 30; ++i) r.e->renderOneFrame();
                Image prev, img;
                unsigned prevLevel = 0xFFFFFFFFu;
                for (float dist = kFar; dist >= kNear - 1.0e-4f; dist -= kStep) {
                    enginetest::testCameraLookAt(r.view, Vec3(0, 0, dist), Vec3(0, 0, 0));
                    frame(r, img);
                    std::vector<ObjectLodDesc> lods;
                    r.scene->objectLods(lods);
                    unsigned level = 0, tris = 0;
                    for (const auto &d : lods) if (d.node == n) { level = d.level; tris = unsigned(d.triangles); }
                    if (prev.width) {
                        chain.dist.push_back(dist);
                        chain.delta.push_back(maskedMeanAbsDiff(prev, img));
                        chain.switched.push_back(level != prevLevel);
                        chain.tris.push_back(tris);
                    }
                    prev = img;
                    prevLevel = level;
                }
                r.scene->removeNode(n);
                r.scene->destroyMesh(m);
            }

            auto judge = [&](const Walk &w, const char *what, bool gate) {
                double ordSum = 0.0; unsigned ordN = 0, switches = 0;
                for (size_t i = 0; i < w.delta.size(); ++i) {
                    if (w.switched[i]) ++switches; else { ordSum += w.delta[i]; ++ordN; }
                }
                const double ordinary = ordN ? ordSum / ordN : 0.0;
                unsigned over = 0, overExcess = 0;
                double worstSwitchExcess = 0.0, worstExcess = 0.0; float worstAt = 0.0f;
                for (size_t i = 0; i < w.delta.size() && i < ref.delta.size(); ++i) {
                    const double bar = 3.0 * std::max(ordinary, ref.delta[i]);
                    const double excess = w.delta[i] - ref.delta[i];
                    if (w.delta[i] > bar) ++over;
                    if (excess > 3.0 * ordinary) ++overExcess;
                    if (excess > worstExcess) { worstExcess = excess; worstAt = w.dist[i]; }
                    if (w.switched[i]) worstSwitchExcess = std::max(worstSwitchExcess, excess);
                }
                std::printf("   %-26s %3u switches, ordinary step %.2f codes, worst excess %.2f codes at "
                            "%.2f m (worst at a switch %.2f), bar %.2f codes; %zu..%zu triangles\n",
                            what, switches, ordinary * 255.0, worstExcess * 255.0, worstAt,
                            worstSwitchExcess * 255.0, 3.0 * ordinary * 255.0,
                            w.tris.empty() ? 0 : *std::min_element(w.tris.begin(), w.tris.end()),
                            w.tris.empty() ? 0 : *std::max_element(w.tris.begin(), w.tris.end()));
                if (gate) {
                    CHECK(switches >= 3u, "%s: the walk crosses %u cut changes", what, switches);
                    CHECK(over == 0u, "%s: no step exceeds 3x the ordinary movement at its own pose (%u over)",
                          what, over);
                    CHECK(overExcess == 0u, "%s: THE POP — no step adds more than 3x the ordinary step "
                          "over the level-0 control (%u over; worst %.2f codes of %.2f)", what, overExcess,
                          worstExcess * 255.0, 3.0 * ordinary * 255.0);
                }
            };
            std::printf("\n== B. the dolly, 40 m -> 2 m, one frame per 0.25 m (the 20k sphere) ==\n");
            CHECK(cw.delta.size() >= 150u && cw.delta.size() == ref.delta.size(),
                  "%zu compared pairs, the same poses in every arm", cw.delta.size());
            judge(cw, "the CLUSTER CUT", true);
            judge(chain, "the chain (for comparison)", false);
        }
    }

    // ---- C. the numbers: a 40 m bar, end-on ----------------------------------
    //
    // THREE BARS, and two of them are findings. The BUMPY bar has real detail
    // along its length and around it, so every coarser level of the chain costs
    // real error everywhere and the chain must hold a fine level for the whole
    // bar while the near end needs it; the DAG coarsens the far end. That is the
    // case the design names, and it is asserted. The ROUND and the BOX bar are
    // printed, not asserted: along their length they simplify LOSSLESSLY (a
    // straight cylinder's rings and a box's coplanar faces), so the chain's
    // levels carry the arithmetic floor as their bound and it drops to a coarse
    // level at almost any distance — while the DAG keeps every group border
    // locked through the build and clamps a group whose sphere reaches the eye
    // to distance zero, so it can draw MORE there.
    auto barNumbers = [&](clusterfix::Fixture &bar, bool gate) {
        const Sphere s = boundsOf(bar.data);
        std::printf("\n== C. %s seen end-on (%zu triangles, %zu clusters, %zu chain levels, "
                    "level-1 bound %.3g) ==\n", bar.name.c_str(), bar.triangles, bar.data.clusters.size(),
                    bar.data.lodIndices.size(), bar.data.lodBounds.empty() ? 0.0 : double(bar.data.lodBounds[0]));
        std::printf("   %-24s %-10s %-10s %-8s\n", "eye", "DAG tris", "chain tris", "chain/DAG");
        const float eyes[][3] = { { 0.0f, 0.0f, 0.5f }, { 0.0f, 0.0f, 2.0f }, { 0.0f, 0.3f, 5.0f },
                                  { 0.4f, 0.4f, 10.0f }, { 0.0f, 1.0f, 20.0f } };
        std::vector<unsigned> cut;
        bool fewer = true;
        double worstRatio = 1e30, bestRatio = 0.0;
        for (const auto &e : eyes) {
            ClusterCutView v;
            v.eye[0] = e[0]; v.eye[1] = e[1]; v.eye[2] = e[2];
            v.tolerance = kLodBudgetPixels;
            v.projScaleY = r.p11;
            v.viewportHeight = r.height;
            const size_t dagTris = clusterCut(bar.data.clusterGroups, bar.data.clusters, v, cut);
            // THE CHAIN'S ANSWER in the same currency: ONE level for the whole
            // object, from Ogre's own distance to its bounding sphere.
            const float dx = s.c[0] - e[0], dy = s.c[1] - e[1], dz = s.c[2] - e[2];
            const float d = std::max(0.0f, std::sqrt(dx * dx + dy * dy + dz * dz) - s.r);
            const float allowed = allowedWorldError(kLodBudgetPixels, footprint(r, d), 1.0f);
            const size_t level = lodLevelForWorldError(bar.data.lodBounds, allowed, bar.data.lodIndices.size());
            const size_t chainTris = level == 0 ? bar.triangles : bar.data.lodIndices[level - 1].size() / 3;
            const double ratio = dagTris ? double(chainTris) / double(dagTris) : 0.0;
            worstRatio = std::min(worstRatio, ratio);
            bestRatio = std::max(bestRatio, ratio);
            if (dagTris >= chainTris) fewer = false;
            char eyeStr[64];
            std::snprintf(eyeStr, sizeof(eyeStr), "(%.1f, %.1f, %.1f)", e[0], e[1], e[2]);
            std::printf("   %-24s %-10zu %-10zu %-8.2f\n", eyeStr, dagTris, chainTris, ratio);
        }
        if (gate)
            CHECK(fewer, "%s: THE DAG DRAWS FEWER TRIANGLES than the chain at every end-on pose "
                  "(chain/DAG %.2fx .. %.2fx)", bar.name.c_str(), worstRatio, bestRatio);
        else
            std::printf("   (printed, not asserted: chain/DAG %.2fx .. %.2fx)\n", worstRatio, bestRatio);
    };
    {
        clusterfix::Fixture bumpy, round, box;
        clusterfix::bake(bumpy, clusterfix::roundBar(40.0f, 0.25f, 400, 32, 0.08f), "bumpy-bar-40m");
        clusterfix::bake(round, clusterfix::roundBar(40.0f, 0.25f, 400, 32), "round-bar-40m");
        clusterfix::bake(box, clusterfix::bar(40.0f, 0.5f, 400, 4), "box-bar-40m");
        barNumbers(bumpy, true);
        barNumbers(round, false);
        barNumbers(box, false);
        // ...and the bumpy bar's cut is crack-free on the rasteriser, end-on.
        const float along[3] = { 0.05f, 0.08f, 1.0f };
        const float eye[3] = { 0.55f, 0.4f, 1.2f }, target[3] = { 0.0f, 0.0f, -6.0f };
        crackSweep(r, bumpy, along, sweep, eye, target);
    }

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
