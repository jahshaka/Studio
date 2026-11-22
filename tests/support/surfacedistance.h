#pragma once
// surfacedistance — THE INDEPENDENT MEASURE OF A SWITCH (ATOM-TESTS-BACK-1 stage 2, the lead's ruling 1 and call C3): how far
// a cluster group's flip moves the surface, measured WITHOUT the bake's own measure (irisgl/import/atomdag/measure.h is never
// included here: a row that reads its subject's measure cannot see a term missing from it). Header-only, CPU, double
// precision, no engine, no Qt: its own closest-point routine (Ericson, Real-Time Collision Detection 5.1.5), its own grid,
// its own islands.
//
// THE QUANTITY, for an input triangle set A (a group's members) and an output set B (the clusters it produced):
//   (1) THE SURFACE TERM, each side against the other: max over p in A of w(p) d(p, B), and over p in B of w(p) d(p, A) -
//       d the EXACT point-to-triangle-set distance, its maximum over every vertex, every edge and every facet interior found
//       by a certified branch and bound (below);
//   (2) THE DROPPED ISLAND: every island of A (its connected pieces, by shared position) with no vertex of B at any of its
//       positions is gone at the flip; it costs min(its box diagonal, its farthest point's distance to the NEW surface the
//       step added - B's islands that hold no position of A: a card group's quads, a replaced sheet). WHY THE MIN (the
//       lead's ruling, 2026-11-22; the spec's "its box diagonal" alone failed the spec's own check): a replaced island's
//       true change is its distance to what replaced it, never more than everything gone. A closest-point distance to all of B cannot see a dropped island:
//       a thin box flush on a surface the output KEPT reads its thickness, never its size (the flush shutter); and the
//       diagonal alone would charge a sheet replaced by its parallel twin d away its whole size (the parallel planes).
// In WORLD units w = 1. In PIXELS (a view), w(p) is the view's scale ACROSS THE RAY at p: a point at distance D off the
// eye and angle theta off the axis, moved delta across its ray, moves (P11 H / 2) delta / (D cos^2 theta) px on a planar
// image (the spec's premise) - the projection Jacobian's largest singular value, (H / (2 tanY)) |p - e| / z^2. The pixel
// reading is that worst direction, an upper bound of the true |proj(p) - proj(q)| to first order in d / z (the literal
// projected pair's largest value over every sampled point is kept beside it, and the rows assert it never passes the
// certified upper bound); only points that project inside the view's WINDOW count.
//
// THE BRANCH AND BOUND (the facet-interior maximum, exact to a stated tolerance): d(., T) is convex for one triangle T and
// d(., B) = min over T, so over a piece S of a facet, d <= max over S's corners of d(corner, T*) for ANY T* (the nearest
// triangle of S's centre is used), and d <= d(centre) + radius (1-Lipschitz); the tighter is the piece's bound. Pieces are
// split at their longest edge until their bound is within the tolerance of the best value found, or cannot pass it.
// Reading::value is a value ATTAINED at a point (a lower bound of the true maximum); Reading::upper the certified upper
// bound, within (1 + rel) of the value (or of the floor where the floor closed the walk); both are printed.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace surfacedistance {

using V3 = std::array<double, 3>;
struct Tri { V3 p[3]; };

inline V3 sub(const V3 &a, const V3 &b) { return { a[0] - b[0], a[1] - b[1], a[2] - b[2] }; }
inline V3 add(const V3 &a, const V3 &b) { return { a[0] + b[0], a[1] + b[1], a[2] + b[2] }; }
inline V3 mul(const V3 &a, double k) { return { a[0] * k, a[1] * k, a[2] * k }; }
inline double dot(const V3 &a, const V3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline double len(const V3 &a) { return std::sqrt(dot(a, a)); }

/// The closest point of triangle abc to p (Ericson 5.1.5; a degenerate triangle falls to its edges).
inline V3 closestOnTriangle(const V3 &p, const V3 &a, const V3 &b, const V3 &c)
{
    const V3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    const double d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0 && d2 <= 0.0) return a;
    const V3 bp = sub(p, b);
    const double d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0 && d4 <= d3) return b;
    const double vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return add(a, mul(ab, d1 / (d1 - d3)));
    const V3 cp = sub(p, c);
    const double d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0 && d5 <= d6) return c;
    const double vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return add(a, mul(ac, d2 / (d2 - d6)));
    const double va = d3 * d6 - d5 * d4;
    if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0)
        return add(b, mul(sub(c, b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    const double sum = va + vb + vc;
    if (!(sum > 0.0)) {   // degenerate: the nearest of its three edges
        V3 best = a;
        double bd = 1e300;
        const V3 *e[3][2] = { { &a, &b }, { &b, &c }, { &c, &a } };
        for (const auto &s : e) {
            const V3 d = sub(*s[1], *s[0]);
            const double l2 = dot(d, d), t = l2 > 0.0 ? std::clamp(dot(sub(p, *s[0]), d) / l2, 0.0, 1.0) : 0.0;
            const V3 q = add(*s[0], mul(d, t));
            if (len(sub(p, q)) < bd) { bd = len(sub(p, q)); best = q; }
        }
        return best;
    }
    const double v = vb / sum, w = vc / sum;
    return add(a, add(mul(ab, v), mul(ac, w)));
}

/// THE NEAREST POINT OF A TRIANGLE SOUP: a uniform grid of the soup's triangles (each listed in every cell its box meets),
/// searched in growing Chebyshev shells of cells around the query's cell until the best distance is within the shell
/// (every cell within k of p's cell covers the ball of radius k * cell around a point inside the grid's box).
struct Nearest { double d = 1e300; V3 q = { 0, 0, 0 }; int tri = -1; };
struct Soup
{
    std::vector<Tri> tris;
    V3 lo = { 0, 0, 0 };
    double cell = 1.0;
    int n[3] = { 1, 1, 1 };
    std::vector<std::vector<int>> cells;
    mutable std::vector<unsigned> stamp;
    mutable unsigned now = 0;

    /// `lo`/`hi`: a box holding every point that will be queried and every triangle.
    void build(std::vector<Tri> t, const V3 &boxLo, const V3 &boxHi)
    {
        tris = std::move(t);
        lo = boxLo;
        const V3 size = sub(boxHi, boxLo);
        const double longest = std::max({ size[0], size[1], size[2], 1e-12 });
        cell = std::max(longest / 48.0, std::cbrt(std::max(size[0] * size[1] * size[2], 1e-36) / double(std::max<size_t>(tris.size(), 1))));
        for (int k = 0; k < 3; ++k) n[k] = std::max(1, std::min(48, int(std::ceil(size[k] / cell)) + 1));
        cells.assign(size_t(n[0]) * size_t(n[1]) * size_t(n[2]), {});
        for (size_t i = 0; i < tris.size(); ++i) {
            int a[3], b[3];
            for (int k = 0; k < 3; ++k) {
                const double mn = std::min({ tris[i].p[0][k], tris[i].p[1][k], tris[i].p[2][k] });
                const double mx = std::max({ tris[i].p[0][k], tris[i].p[1][k], tris[i].p[2][k] });
                a[k] = coord(mn, k);
                b[k] = coord(mx, k);
            }
            for (int z = a[2]; z <= b[2]; ++z)
                for (int y = a[1]; y <= b[1]; ++y)
                    for (int x = a[0]; x <= b[0]; ++x) cells[index(x, y, z)].push_back(int(i));
        }
        stamp.assign(tris.size(), 0u);
    }
    int coord(double v, int k) const { return std::clamp(int(std::floor((v - lo[k]) / cell)), 0, n[k] - 1); }
    size_t index(int x, int y, int z) const { return (size_t(z) * size_t(n[1]) + size_t(y)) * size_t(n[0]) + size_t(x); }

    Nearest nearest(const V3 &p) const
    {
        Nearest best;
        if (tris.empty()) return best;
        ++now;
        const int c[3] = { coord(p[0], 0), coord(p[1], 1), coord(p[2], 2) };
        const int maxK = std::max({ n[0], n[1], n[2] });
        for (int k = 0; k <= maxK; ++k) {
            for (int z = c[2] - k; z <= c[2] + k; ++z)
                for (int y = c[1] - k; y <= c[1] + k; ++y)
                    for (int x = c[0] - k; x <= c[0] + k; ++x) {
                        if (std::max({ std::abs(x - c[0]), std::abs(y - c[1]), std::abs(z - c[2]) }) != k) continue;
                        if (x < 0 || y < 0 || z < 0 || x >= n[0] || y >= n[1] || z >= n[2]) continue;
                        for (int t : cells[index(x, y, z)]) {
                            if (stamp[size_t(t)] == now) continue;
                            stamp[size_t(t)] = now;
                            const Tri &T = tris[size_t(t)];
                            const V3 q = closestOnTriangle(p, T.p[0], T.p[1], T.p[2]);
                            const double d = len(sub(p, q));
                            if (d < best.d) best = { d, q, t };
                        }
                    }
            if (best.tri >= 0 && best.d <= double(k) * cell) break;
        }
        return best;
    }
};

/// A VIEW for the pixel reading: the eye at the origin looking down -Z, the frame's half-angle tangents and its size in
/// pixels (square pixels), and the WINDOW - a disc of the frame (centre and radius in pixels) whose points count.
struct View
{
    double tanX = 1.0, tanY = 1.0, width = 1.0, height = 1.0;
    double winX = 0.0, winY = 0.0, winR = 1e300;   ///< pixel coordinates: (0, 0) the frame's lower-left corner
    /// The pixel position of p (false behind the eye).
    bool pixel(const V3 &p, double &x, double &y) const
    {
        const double z = -p[2];
        if (!(z > 0.0)) return false;
        x = (p[0] / z / tanX * 0.5 + 0.5) * width;
        y = (p[1] / z / tanY * 0.5 + 0.5) * height;
        return true;
    }
    /// Pixels per metre ACROSS THE RAY at p: (H / (2 tanY)) |p| / z^2.
    double scale(const V3 &p) const
    {
        const double z = -p[2];
        return z > 0.0 ? height / (2.0 * tanY) * len(p) / (z * z) : 1e300;
    }
    bool inWindow(double x, double y) const
    {
        return x >= 0.0 && y >= 0.0 && x <= width && y <= height && std::hypot(x - winX, y - winY) <= winR;
    }
};

struct Reading
{
    double value = 0.0;   ///< attained at `p` (its nearest point `q` on the other side)
    double upper = 0.0;   ///< the certified upper bound
    V3 p = { 0, 0, 0 }, q = { 0, 0, 0 };
    bool any = false;     ///< some point of the side counted (in the view's window)
    size_t pieces = 0;    ///< the branch and bound's pieces walked (its cost)
    double literal = 0.0; ///< with a view: the largest LITERAL |proj(p) - proj(q)| over every point sampled - never past
                          ///< `upper` (the rows assert it: the first-order bound is shown to be a bound, not only argued)
};

/// The farthest point of `from` against `to`, weighted by `view`'s scale across the ray when `view` is given (and only
/// inside its window), plain distance when not. A piece closes when its bound is within max(floor, the running maximum) x
/// (1 + `rel`): `rel` the stated relative precision (the bound's overshoot falls only LINEARLY in the piece's size where
/// the other side bends under it - a removed vertex above a chord, exactly where a maximum sits - so an absolute gap
/// splits thousands of pieces a facet); `floor` the RESOLUTION, in the reading's units: a piece whose bound is under it
/// cannot move the reading's law (a flat stretch retriangulated reads ~0).
inline Reading farthest(const std::vector<Tri> &from, const Soup &to, const View *view, double rel, double floor)
{
    Reading r;
    if (to.tris.empty()) return r;
    struct Piece { V3 a, b, c; int level; };
    const auto weightAt = [&](const V3 &p, const V3 &q) {
        return view ? std::max(view->scale(p), view->scale(q)) : 1.0;
    };
    const auto counts = [&](const V3 &p) {
        if (!view) return true;
        double x, y;
        return view->pixel(p, x, y) && view->inWindow(x, y);
    };
    const auto consider = [&](const V3 &p) {
        if (!counts(p)) return;
        const Nearest n = to.nearest(p);
        const double v = n.d * weightAt(p, n.q);
        r.any = true;
        double px, py, qx, qy;
        if (view && view->pixel(p, px, py) && view->pixel(n.q, qx, qy)) r.literal = std::max(r.literal, std::hypot(px - qx, py - qy));
        if (v > r.value) { r.value = v; r.p = p; r.q = n.q; }
    };
    {   // every vertex first, once each: the running maximum closes most pieces at once
        std::vector<V3> verts;
        for (const Tri &t : from) verts.insert(verts.end(), std::begin(t.p), std::end(t.p));
        std::sort(verts.begin(), verts.end());
        verts.erase(std::unique(verts.begin(), verts.end()), verts.end());
        for (const V3 &v : verts) consider(v);
    }
    std::vector<Piece> stack;
    for (const Tri &t : from) {
        stack.push_back({ t.p[0], t.p[1], t.p[2], 0 });
        while (!stack.empty()) {
            const Piece s = stack.back();
            stack.pop_back();
            ++r.pieces;
            const V3 c = mul(add(add(s.a, s.b), s.c), 1.0 / 3.0);
            double rad = 0.0;
            for (const V3 *k : { &s.a, &s.b, &s.c }) rad = std::max(rad, len(sub(*k, c)));
            const Nearest n = to.nearest(c);
            double ub = n.d + rad;
            if (n.tri >= 0) {
                const Tri &T = to.tris[size_t(n.tri)];
                double convex = n.d;
                for (const V3 *k : { &s.a, &s.b, &s.c })
                    convex = std::max(convex, len(sub(*k, closestOnTriangle(*k, T.p[0], T.p[1], T.p[2]))));
                ub = std::min(ub, convex);
            }
            double wMax = 1.0;
            bool inside = true;
            if (view) {
                // the frame's frustum is convex: a piece wholly behind the eye or wholly past one side plane holds no
                // counted point
                const auto all = [&](auto &&out) { return out(s.a) && out(s.b) && out(s.c); };
                const double tx = view->tanX, ty = view->tanY;
                if (all([](const V3 &p) { return -p[2] <= 0.0; }) || all([&](const V3 &p) { return p[0] > -p[2] * tx; }) ||
                    all([&](const V3 &p) { return -p[0] > -p[2] * tx; }) || all([&](const V3 &p) { return p[1] > -p[2] * ty; }) ||
                    all([&](const V3 &p) { return -p[1] > -p[2] * ty; }))
                    continue;
                // the piece's projection lies in the disc of its projected corners' circumcircle about the centre's
                // pixel; outside the window by more than that radius = no point of it counts
                double cx, cy;
                bool front = view->pixel(c, cx, cy);
                double pr = 0.0;
                for (const V3 *k : { &s.a, &s.b, &s.c }) {
                    double x, y;
                    if (!view->pixel(*k, x, y)) { front = false; break; }
                    pr = std::max(pr, std::hypot(x - cx, y - cy));
                }
                if (front && std::hypot(cx - view->winX, cy - view->winY) > view->winR + pr) inside = false;
                // the scale's bound over the piece and its nearest points: the nearest z, the farthest |p|
                double zMin = 1e300, dMax = 0.0;
                for (const V3 *k : { &s.a, &s.b, &s.c }) { zMin = std::min(zMin, -(*k)[2]); dMax = std::max(dMax, len(*k)); }
                zMin -= ub;
                dMax += ub;
                wMax = zMin > 0.0 ? view->height / (2.0 * view->tanY) * dMax / (zMin * zMin) : 1e300;
            }
            if (!inside) continue;
            consider(c);
            const double bound = ub * wMax;
            if (bound <= std::max(r.value, floor) * (1.0 + rel)) { r.upper = std::max(r.upper, bound); continue; }
            if (s.level >= 30) { r.upper = std::max(r.upper, bound); continue; }
            const double ab = len(sub(s.b, s.a)), bc = len(sub(s.c, s.b)), ca = len(sub(s.a, s.c));
            if (ab >= bc && ab >= ca) {
                const V3 m = mul(add(s.a, s.b), 0.5);
                stack.push_back({ s.a, m, s.c, s.level + 1 });
                stack.push_back({ m, s.b, s.c, s.level + 1 });
            } else if (bc >= ca) {
                const V3 m = mul(add(s.b, s.c), 0.5);
                stack.push_back({ s.a, s.b, m, s.level + 1 });
                stack.push_back({ s.a, m, s.c, s.level + 1 });
            } else {
                const V3 m = mul(add(s.c, s.a), 0.5);
                stack.push_back({ s.a, s.b, m, s.level + 1 });
                stack.push_back({ m, s.b, s.c, s.level + 1 });
            }
        }
    }
    r.upper = std::max(r.upper, r.value);
    return r;
}

/// The islands of `a` (connected by shared POSITION, -0 folded into +0) holding no position of `b`: each one's triangles
/// and box.
struct Island { std::vector<Tri> tris; V3 lo, hi; };
inline std::vector<Island> islandsApart(const std::vector<Tri> &a, const std::vector<Tri> &b)
{
    using Key = std::tuple<double, double, double>;
    const auto key = [](const V3 &p) { return Key(p[0] + 0.0, p[1] + 0.0, p[2] + 0.0); };
    std::map<Key, int> id;
    std::vector<int> parent;
    const auto find = [&](int x) { while (parent[size_t(x)] != x) x = parent[size_t(x)] = parent[size_t(parent[size_t(x)])]; return x; };
    std::vector<std::array<int, 3>> tv;
    for (const Tri &t : a) {
        std::array<int, 3> v;
        for (int k = 0; k < 3; ++k) {
            auto it = id.find(key(t.p[k]));
            if (it == id.end()) { it = id.emplace(key(t.p[k]), int(parent.size())).first; parent.push_back(it->second); }
            v[size_t(k)] = it->second;
        }
        parent[size_t(find(v[1]))] = find(v[0]);
        parent[size_t(find(v[2]))] = find(v[0]);
        tv.push_back(v);
    }
    std::vector<char> kept(parent.size(), 0);
    for (const Tri &t : b)
        for (const V3 &p : t.p) {
            const auto it = id.find(key(p));
            if (it != id.end()) kept[size_t(find(it->second))] = 1;
        }
    std::map<int, Island> out;
    for (size_t i = 0; i < a.size(); ++i) {
        const int root = find(tv[i][0]);
        if (kept[size_t(root)]) continue;
        auto it = out.find(root);
        if (it == out.end()) it = out.emplace(root, Island { {}, a[i].p[0], a[i].p[0] }).first;
        it->second.tris.push_back(a[i]);
        for (const V3 &p : a[i].p)
            for (int k = 0; k < 3; ++k) {
                it->second.lo[size_t(k)] = std::min(it->second.lo[size_t(k)], p[size_t(k)]);
                it->second.hi[size_t(k)] = std::max(it->second.hi[size_t(k)], p[size_t(k)]);
            }
    }
    std::vector<Island> v;
    for (auto &kv : out) v.push_back(std::move(kv.second));
    return v;
}

/// An island's box diagonal - in world units, or projected (the longest of the box's four main diagonals between its
/// corners' pixels) when a view is given and its centre projects in the window (else 0).
inline double diagonalCost(const Island &b, const View *view)
{
    if (!view) return len(sub(b.hi, b.lo));
    double cx, cy;
    if (!view->pixel(mul(add(b.lo, b.hi), 0.5), cx, cy) || !view->inWindow(cx, cy)) return 0.0;
    double best = 0.0;
    for (int k = 0; k < 4; ++k) {
        const V3 p = { (k & 1) ? b.hi[0] : b.lo[0], (k & 2) ? b.hi[1] : b.lo[1], b.lo[2] };
        const V3 q = { (k & 1) ? b.lo[0] : b.hi[0], (k & 2) ? b.lo[1] : b.hi[1], b.hi[2] };
        double px, py, qx, qy;
        if (!view->pixel(p, px, py) || !view->pixel(q, qx, qy)) return 1e300;
        best = std::max(best, std::hypot(px - qx, py - qy));
    }
    return best;
}

/// THE SWITCH: both sides' surface terms and the dropped islands of either side, in one reading.
struct Switch
{
    Reading aToB, bToA;    ///< (1), each side against the other (B empty: none)
    double islands = 0.0;  ///< (2), the largest dropped island's cost
    int dropped = 0;       ///< islands of A with no position in B
    int replaced = 0;      ///< ...of which new surface of B stood nearer than the diagonal
    double surface() const { return std::max(aToB.value, bToA.value); }
    double total() const { return std::max(surface(), islands); }
    double upper() const { return std::max({ aToB.upper, bToA.upper, islands }); }
};
inline Switch measure(const std::vector<Tri> &a, const std::vector<Tri> &b, const View *view, double rel, double floor)
{
    Switch s;
    V3 lo = { 1e300, 1e300, 1e300 }, hi = { -1e300, -1e300, -1e300 };
    for (const auto *set : { &a, &b })
        for (const Tri &t : *set)
            for (const V3 &p : t.p)
                for (int k = 0; k < 3; ++k) { lo[size_t(k)] = std::min(lo[size_t(k)], p[size_t(k)]); hi[size_t(k)] = std::max(hi[size_t(k)], p[size_t(k)]); }
    Soup sa, sb;
    sa.build(a, lo, hi);
    sb.build(b, lo, hi);
    s.aToB = farthest(a, sb, view, rel, floor);
    s.bToA = farthest(b, sa, view, rel, floor);
    std::vector<Tri> fresh;   // B's NEW surface: its islands holding no position of A
    for (const Island &i : islandsApart(b, a)) fresh.insert(fresh.end(), i.tris.begin(), i.tris.end());
    Soup sf;
    if (!fresh.empty()) sf.build(fresh, lo, hi);
    for (const Island &i : islandsApart(a, b)) {
        ++s.dropped;
        double cost = diagonalCost(i, view);
        if (!fresh.empty()) {
            const Reading r = farthest(i.tris, sf, view, rel, floor);
            if (r.any && r.upper < cost) { cost = r.value; ++s.replaced; }
        }
        s.islands = std::max(s.islands, cost);
    }
    return s;
}

}   // namespace surfacedistance
