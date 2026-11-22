// atom.decode_parity.{materials,lights} — THE DECODE IS STOCK PBS (ATOM-TESTS-BACK-1 stage 3; SPECS/ATOM_TESTS_BACK_1_SPEC.md,
// ruling 5). HlmsAtom is a derived HlmsPbs that replaces one piece (LoadMaterial: the interpolants rebuilt from the id
// image's slot, cluster and triangle) and runs every other lighting piece of PBS unchanged (HlmsAtom.h; the decode's piece
// irisgl/engine/media/Hlms/Atom/Any/800.Atom_piece_ps.any). So the same surfaces, shaded by the decode and by stock PBS,
// are THE SAME DISPLAY CODES, except where the two rasterisations legitimately part:
//   THE NAMED CLASS (ruling 5, made exact from the id image): a pixel whose 3 x 3 neighbourhood in the Atom arm's id image
//   holds another (x, y) word - a triangle edge, a silhouette, a cut-out's hole edge (the mask's edge changes the id). Its
//   other half, "a mip-level boundary within 1 px", is EMPTY BY CONSTRUCTION here: every texture is single-level (no mip
//   to cross) but the one minified cell's, a plane PARALLEL to the image plane (its screen scale constant, its UV affine
//   in screen, so the rasteriser's quad derivatives are exact) at lambda = log2(512 texels / 107.63 px) = 2.25: both
//   routes pick level 2 (nearest rounds at 2.5) or blend 2 and 3 at 0.25 (trilinear), never a boundary (2.0 or 2.5).
//   Its px: the eye at the origin looking down -Z, vertical fov 45, 540 px tall, the cell at z = -6: 540 / (2 x 6 x
//   tan 22.5) = 108.64 px a metre, the plane 0.9907 m = 107.63 px.
// THE LAW (the lead's ruling, 2026-11-22, its form from the measured run): the picture with the visibility buffer's door
// OPEN (Scene::setAtomDrawEnabled(true): the decode) against the door SHUT (false: every item through stock PBS,
// Engine.h:941-944), in LINEAR radiance (readPixelsHdr), at every pixel outside the class: |decode - stock| <= eps G + q
// per channel (law() below: eps the decode's sub-pixel snap, q one fp16 quantum of the target); the display corollary
// beside it (a code moves by at most 1, and only where the linear values differ).
// The open arm proves every item rides Atom (pbsItems 0, the masked and two-sided cells counted); the shut arm proves it
// has no id pass (readAtomIds false). Each picture is read until two reads 2 frames apart are byte-equal (frames, never time).
// THE ROWS (one per proof; every row builds the same scene, the law counts its own cells):
//   materials: one sun; cells dielectric rough, metal, smooth (a highlight), emissive, normal-mapped (magnified), the
//     minified plane above, a two-sided open plane seen from its BACK, a 1-texel cut-out checker (magnified 8.7x, its UVs inset).
//     NEGATIVE CONTROL: the shut arm with the rough cell's albedo one code (1/255) up - its pixels break the bound.
//     The law counts every cell but the normal map (its own row).
//   normal_map: the normal-mapped cell alone, under the sun, a point and a spot (DECLARED RED, SPECS/OPEN_REDS.md, 3.6).
//   point / spot: every cell but the normal map under that lamp alone.
//   area: every cell under the area lamp (LTC) alone (DECLARED RED, SPECS/OPEN_REDS.md, 3.6).
// GI OFF, route `none` DECLARED on frame 1 (the lead's call C4). lodBias 0: both routes draw level 0, the same triangles.
// SHADOWS OFF, A NAMED GAP (the lead's ruling, 2026-11-22): under Atom the sun's maps are drawn by the caster cut (out of
// this spec: ruling 6), so a map's content would differ by the caster route, not by the decode. THE DECODE'S SHADOW
// RECEIVE PATH (the same map sampled by the decode and by stock PBS) IS UNPROVEN BY ANY ROW; its row belongs to the caster
// cut's item, where the two maps are first proven equal.
#include "atom_fixture.h"

#include "EnginePrivate.h"
#include "GpuScene.h"
#include "HlmsAtom.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "jahshaka/engine/Atom.h"
#include "jahshaka/engine/GiModes.h"
#include "jahshaka/engine/GiParams.h"
#include "jahshaka/engine/Ids.h"
#include "jahshaka/engine/Lights.h"
#include "jahshaka/engine/Materials.h"
#include "jahshaka/engine/Math.h"
#include "jahshaka/engine/Mesh.h"
#include "jahshaka/engine/Pictures.h"
#include "jahshaka/engine/PostFx.h"

using namespace jahshaka::engine;
using jahshaka::engine::detail::AtomId;
using jahshaka::engine::detail::OgreScene;

static int failures = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) std::printf("ok: %s\n", msg);                                 \
        else { std::printf("FAIL: %s\n", msg); ++failures; }                    \
    } while (0)
#define CHECK_MSG(cond, fmt, ...)                                               \
    do {                                                                        \
        char buf_[640];                                                         \
        std::snprintf(buf_, sizeof(buf_), fmt, __VA_ARGS__);                    \
        CHECK(cond, buf_);                                                      \
    } while (0)

static const unsigned kW = 960, kH = 540;
static const float kCellZ = -6.0f;
/// The minified plane's side (the header's derivation: lambda 2.25 of a 512 texture).
static const float kMinifiedSide = 0.9907f;
/// A picture is still when two reads this many frames apart are byte-equal; at most kSettleCap frames.
static const int kSettleStep = 2, kSettleCap = 120;

static MeshData uvSphere(int seg, int ring, float r)
{
    MeshData d;
    for (int j = 0; j <= ring; ++j) {
        const float th = float(j) / float(ring) * 3.14159265f;
        for (int i = 0; i <= seg; ++i) {
            const float ph = float(i) / float(seg) * 6.28318531f;
            const float x = std::sin(th) * std::cos(ph), y = std::cos(th), z = -std::sin(th) * std::sin(ph);
            d.positions.insert(d.positions.end(), { r * x, r * y, r * z });
            d.normals.insert(d.normals.end(), { x, y, z });
            d.uvs.insert(d.uvs.end(), { float(i) / float(seg), float(j) / float(ring) });
        }
    }
    for (int j = 0; j < ring; ++j)
        for (int i = 0; i < seg; ++i) {
            const unsigned a = unsigned(j * (seg + 1) + i), b = a + 1u;
            const unsigned c = unsigned((j + 1) * (seg + 1) + i), e = c + 1u;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
        }
    return d;
}

/// A square in XY of side `size`, n x n quads, its front (normal) +Z, UVs 0..1 left to right, top to bottom.
static MeshData plane(int n, float size)
{
    MeshData d;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            const float u = float(i) / float(n), v = float(j) / float(n);
            d.positions.insert(d.positions.end(), { (u - 0.5f) * size, (0.5f - v) * size, 0.0f });
            d.normals.insert(d.normals.end(), { 0.0f, 0.0f, 1.0f });
            d.uvs.insert(d.uvs.end(), { u, v });
        }
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const unsigned a = unsigned(j * (n + 1) + i), b = a + 1u, c = a + unsigned(n + 1), e = c + 1u;
            d.indices.insert(d.indices.end(), { a, c, b, b, c, e });
        }
    return d;
}

static unsigned char code(float v) { return (unsigned char)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255.0f); }

/// Smooth bands (albedo), gentle bumps (a tangent-space normal map) or high-frequency stripes (the minified cell).
static std::vector<unsigned char> texture(int n, int kind)
{
    std::vector<unsigned char> px(size_t(n) * n * 4u);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const float u = float(x) / float(n) * 6.28318531f, v = float(y) / float(n) * 6.28318531f;
            unsigned char *p = &px[(size_t(y) * n + size_t(x)) * 4u];
            float r, g, b;
            if (kind == 1) {
                const float nx = 0.35f * std::cos(u * 3.0f), ny = 0.35f * std::cos(v * 3.0f);
                const float nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
                r = nx * 0.5f + 0.5f; g = ny * 0.5f + 0.5f; b = nz * 0.5f + 0.5f;
            } else if (kind == 2) {
                r = 0.5f + 0.4f * std::sin(u * 40.0f);
                g = 0.5f + 0.4f * std::sin(v * 28.0f + 1.0f);
                b = 0.35f + 0.3f * std::cos((u + v) * 12.0f);
            } else {
                r = 0.55f + 0.30f * std::sin(u * 2.0f);
                g = 0.50f + 0.25f * std::sin(v * 3.0f + 1.0f);
                b = 0.45f + 0.30f * std::cos(u + v);
            }
            p[0] = code(r); p[1] = code(g); p[2] = code(b); p[3] = 255;
        }
    return px;
}

struct Cell
{
    const char *name;
    NodeId node = 0;
    unsigned slot = 0xFFFFFFFFu;
};

struct Fixture
{
    atomfix::Rig rig;
    Engine *e = nullptr;
    View *view = nullptr;
    Scene *scene = nullptr;
    std::vector<Cell> cells;
    NodeId rough = 0;
    MaterialId roughMat = 0, roughMatPlusOne = 0;
    /// THE ROW'S CELLS (one row per proof, the lead's ruling 2026-11-22): every row builds the SAME scene and the law
    /// counts only its own cells - "all", every cell but the normal map ("no_normal_map"), or the normal map alone.
    std::string scope = "all";
    bool inScope(const char *cell) const
    {
        const bool nm = std::string(cell) == "normal_map";
        return scope == "all" || (scope == "normal_map" ? nm : !nm);
    }
};

static unsigned slotOf(Scene *s, NodeId n)
{
    auto *os = static_cast<OgreScene *>(s);
    os->ensureGpuScene(false);
    const detail::GpuScene &gs = os->gpuScene();
    for (unsigned k = 0; k < gs.slotCount(); ++k)
        if (os->nodeOfGpuKey(gs.entry(k).ids[0]) == n) return k;
    return 0xFFFFFFFFu;
}

static NodeId addCell(Fixture &F, const char *name, MeshId mesh, MaterialId mat, const Vec3 &at, const Quat &q = Quat())
{
    const NodeId n = F.scene->createNode();
    F.scene->attachMesh(n, mesh, mat);
    F.scene->setNodeTransform(n, at, q, Vec3(1, 1, 1));
    Cell c;
    c.name = name;
    c.node = n;
    F.cells.push_back(c);
    return n;
}

static void build(Fixture &F)
{
    Scene *s = F.scene;
    s->setLodBias(0.0f);
    {
        GiParams gi;
        gi.mode = GiMode::Off;
        s->setGlobalIllumination(gi);
    }
    s->setAmbient(Colour(0.10f, 0.11f, 0.13f), Colour(0.05f, 0.05f, 0.05f));
    const MeshId sphere = s->createMesh(uvSphere(48, 24, 0.6f));
    const MeshId square = s->createMesh(plane(8, 1.2f));
    const MeshId minified = s->createMesh(plane(8, kMinifiedSide));
    // THE BACKDROP: a matte wall behind every cell (every pixel a surface).
    {
        PbrParams w;
        w.albedo = Colour(0.45f, 0.45f, 0.42f);
        w.roughness = 0.9f;
        const NodeId n = s->createNode();
        s->attachMesh(n, s->createMesh(plane(4, 30.0f)), s->createPbrMaterial(w));
        s->setNodeTransform(n, Vec3(0.0f, 0.0f, kCellZ - 2.0f), Quat(), Vec3(1, 1, 1));
        Cell c;
        c.name = "backdrop";
        c.node = n;
        F.cells.push_back(c);
    }
    const float y0 = 0.9f, y1 = -0.9f, xs[4] = { -3.3f, -1.1f, 1.1f, 3.3f };
    PbrParams p;
    p.albedo = Colour(0.70f, 0.45f, 0.30f);
    p.roughness = 0.6f;
    F.roughMat = s->createPbrMaterial(p);
    PbrParams p1 = p;   // THE NEGATIVE CONTROL's twin: one code of albedo up
    p1.albedo = Colour(p.albedo.r + 1.0f / 255.0f, p.albedo.g + 1.0f / 255.0f, p.albedo.b + 1.0f / 255.0f);
    F.roughMatPlusOne = s->createPbrMaterial(p1);
    F.rough = addCell(F, "rough", sphere, F.roughMat, Vec3(xs[0], y0, kCellZ));
    PbrParams m;
    m.albedo = Colour(0.90f, 0.75f, 0.45f);
    m.metalness = 1.0f;
    m.roughness = 0.3f;
    addCell(F, "metal", sphere, s->createPbrMaterial(m), Vec3(xs[1], y0, kCellZ));
    PbrParams sm;
    sm.albedo = Colour(0.20f, 0.35f, 0.65f);
    sm.roughness = 0.1f;
    addCell(F, "smooth", sphere, s->createPbrMaterial(sm), Vec3(xs[2], y0, kCellZ));
    PbrParams em;
    em.albedo = Colour(0.2f, 0.2f, 0.2f);
    em.emissive = Colour(0.6f, 0.3f, 0.1f);
    addCell(F, "emissive", sphere, s->createPbrMaterial(em), Vec3(xs[3], y0, kCellZ));
    // single-level textures, magnified: no mip to cross
    const std::vector<unsigned char> albedoPx = texture(64, 0), normalPx = texture(64, 1), stripesPx = texture(512, 2);
    PbrParams nm;
    nm.albedo = Colour(0.8f, 0.8f, 0.8f);
    nm.roughness = 0.5f;
    const MaterialId nmMat = s->createPbrMaterial(nm);
    s->setPbrTexture(nmMat, PbrTextureSlot::Albedo, s->createTexture(64, 64, albedoPx.data(), true));
    s->setPbrTexture(nmMat, PbrTextureSlot::Normal, s->createTexture(64, 64, normalPx.data(), false));
    addCell(F, "normal_map", sphere, nmMat, Vec3(xs[0], y1, kCellZ));
    PbrParams mi;
    mi.roughness = 0.7f;
    const MaterialId miMat = s->createPbrMaterial(mi);
    s->setPbrTexture(miMat, PbrTextureSlot::Albedo, s->createTexture(512, 512, stripesPx.data(), true, true));
    addCell(F, "minified", minified, miMat, Vec3(xs[1], y1, kCellZ));
    PbrParams ts;
    ts.albedo = Colour(0.35f, 0.60f, 0.70f);
    ts.roughness = 0.45f;
    // its front (+Z) turned away: the camera sees its BACK (180 degrees about Y)
    const NodeId back = addCell(F, "two_sided_back", square, s->createPbrMaterial(ts), Vec3(xs[2], y1, kCellZ),
                                Quat(0.0f, 1.0f, 0.0f, 0.0f));
    s->setNodeFaceCull(back, FaceCull::TwoSided);
    // THE CUT-OUT: a PURE 16 x 16 1-texel checker, alpha 0 / 255, on 1.2 m = 130 px, its UVs INSET HALF A TEXEL (uvScale
    // 15/16, uvOffset 0.5/16: 15 texels across, 8.7 px each; the lead's ruling 2026-11-22). WHY A CHECKER, AND WHY INSET: the
    // stock alpha test samples BILINEAR (OgreMaterials.cpp materialSamplerblock) and the mask NEAREST (jah_alpha_mask.glsl:
    // 82-95), so they draw the same hole only where the bilinear alpha's 0.5 line is the texel edge. Between four texel
    // centres with alphas (a, b, c, d) the bilinear alpha is bilinear in (s, t); at a checker's SADDLE (0, 1, 1, 0) it is
    // s + t - 2st, which is 0.5 exactly on s = 1/2 and t = 1/2 - the texel edges. At a CONVEX corner (one hole among three
    // solid, 1 - (1 - s)(1 - t)) the 0.5 line is the hyperbola (1 - s)(1 - t) = 1/2, cutting the hole's corner by 0.293
    // texel along the diagonal (2.5 px at this size: past the 1-px class) - the mask's nearest rule, atom.masked_coverage's
    // residue, never the decode's. Every corner of a checker is a saddle, and with the UVs inset neither rule reads across
    // the UV border (the plane's edge sits on texel centres), so no pixel outside the id class can differ by coverage.
    std::vector<unsigned char> ck(16u * 16u * 4u);
    for (unsigned y = 0; y < 16u; ++y)
        for (unsigned x = 0; x < 16u; ++x) {
            unsigned char *q = &ck[(y * 16u + x) * 4u];
            q[0] = 210; q[1] = 140; q[2] = 70;
            q[3] = ((x + y) & 1u) ? 255 : 0;
        }
    PbrParams co;
    co.alphaMode = PbrAlphaMode::Cutout;
    co.alphaCutoff = 0.5f;
    co.roughness = 0.6f;
    co.uvScale[0] = co.uvScale[1] = 15.0f / 16.0f;
    co.uvOffset[0] = co.uvOffset[1] = 0.5f / 16.0f;
    const MaterialId coMat = s->createPbrMaterial(co);
    s->setPbrTexture(coMat, PbrTextureSlot::Albedo, s->createTexture(16, 16, ck.data(), true));
    addCell(F, "cutout", square, coMat, Vec3(xs[3], y1, kCellZ));
}

/// Frames until two reads kSettleStep frames apart are byte-equal.
static bool settle(Engine *e, View *v, Image &img, int &frames)
{
    Image prev;
    frames = 0;
    while (frames < kSettleCap) {
        enginetest::frames(e, kSettleStep);
        frames += kSettleStep;
        if (!v->readPixels(img)) return false;
        if (!prev.rgba.empty() && prev.rgba == img.rgba) return true;
        prev = img;
    }
    return false;
}

struct Tally
{
    size_t px = 0, inClass = 0, outside = 0;
    int worstOutside = 0, worstIn = 0;
};

/// The Chebyshev distance from pixel (x, y) to the nearest pixel whose id word differs from its own (<= 8 searched; 9 = none).
static int idChangeDistance(const atomfix::IdImage &ids, unsigned x, unsigned y)
{
    const size_t p = size_t(y) * kW + x;
    for (int r = 1; r <= 8; ++r)
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const int qx = int(x) + dx, qy = int(y) + dy;
                if (qx < 0 || qy < 0 || qx >= int(kW) || qy >= int(kH)) continue;
                const size_t q = size_t(qy) * kW + size_t(qx);
                if (ids.words[2 * q] != ids.words[2 * p] || ids.words[2 * q + 1] != ids.words[2 * p + 1]) return r;
            }
    return 9;
}

/// The law over one pair: per cell (by the open arm's slot), pixels outside the class must be code-equal. Every differing
/// pixel outside the class is NAMED (at most 12 an arm; `skip`'s cell - the negative control's - is not): where, both
/// codes, and its distance to the nearest id change (what a wider class would have to be).
static std::vector<Tally> compare(const Fixture &F, const Image &open, const Image &shut, const atomfix::IdImage &ids,
                                  size_t &unowned, const char *arm, NodeId skip)
{
    int named = 0;
    std::vector<Tally> t(F.cells.size());
    unowned = 0;
    if (open.rgba.size() != size_t(kW) * kH * 4u || shut.rgba.size() != open.rgba.size() || !ids.ok ||
        ids.width != kW || ids.height != kH)
        return t;
    for (unsigned y = 0; y < kH; ++y)
        for (unsigned x = 0; x < kW; ++x) {
            const size_t p = size_t(y) * kW + x;
            const uint32_t wx = ids.words[2 * p], wy = ids.words[2 * p + 1];
            bool edge = false;
            for (int dy = -1; dy <= 1 && !edge; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int qx = int(x) + dx, qy = int(y) + dy;
                    if (qx < 0 || qy < 0 || qx >= int(kW) || qy >= int(kH)) continue;
                    const size_t q = size_t(qy) * kW + size_t(qx);
                    if (ids.words[2 * q] != wx || ids.words[2 * q + 1] != wy) { edge = true; break; }
                }
            int d = 0;
            for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(int(open.rgba[p * 4 + c]) - int(shut.rgba[p * 4 + c])));
            size_t k = 0;
            for (; k < F.cells.size(); ++k)
                if (wx != AtomId::kEmpty && (wx & AtomId::kSlotMask) == F.cells[k].slot) break;
            if (k == F.cells.size()) { unowned += d > 0; continue; }
            Tally &a = t[k];
            ++a.px;
            if (edge) { ++a.inClass; a.worstIn = std::max(a.worstIn, d); }
            else if (d > 0) {
                ++a.outside;
                a.worstOutside = std::max(a.worstOutside, d);
                if (F.cells[k].node != skip && named < 12) {
                    ++named;
                    const unsigned char *o = &open.rgba[p * 4], *sh = &shut.rgba[p * 4];
                    // the 5 x 5 neighbourhood's distinct (x, y) words: a triangle edge inside one cluster changes y
                    // (cluster << 8 | triangle), so the class sees it
                    std::vector<uint64_t> seen;
                    for (int dy = -2; dy <= 2; ++dy)
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int qx = int(x) + dx, qy = int(y) + dy;
                            if (qx < 0 || qy < 0 || qx >= int(kW) || qy >= int(kH)) continue;
                            const size_t q = size_t(qy) * kW + size_t(qx);
                            const uint64_t w = (uint64_t(ids.words[2 * q]) << 32) | ids.words[2 * q + 1];
                            if (std::find(seen.begin(), seen.end(), w) == seen.end()) seen.push_back(w);
                        }
                    std::printf("     [%s] OUTSIDE %s (%u, %u): decode %u %u %u, stock %u %u %u, the nearest id change %d px%s;"
                                " ids x %08x y %08x (cluster %u triangle %u), %zu distinct words in 5x5\n",
                                arm, F.cells[k].name, x, y, o[0], o[1], o[2], sh[0], sh[1], sh[2],
                                idChangeDistance(ids, x, y), idChangeDistance(ids, x, y) > 8 ? " (none within 8)" : "",
                                wx, wy, wy >> 8, wy & 0xFFu, seen.size());
                }
            }
        }
    return t;
}

/// THE LAW, IN LINEAR RADIANCE (the lead's ruling, 2026-11-22, from the measured run atb1-decode-hdr). Each arm's stored
/// value is its float32 result ROUNDED to the radiance target's quantum (an fp16 target: the measured differences were whole
/// fp16 steps - 2^-15 at 0.062, 2^-13 at 0.126, 2^-18 at 4.6e-3), and the decode evaluates the same pieces at a point at most
/// EPS from the rasteriser's: it re-projects each vertex and snaps it to the rasteriser's 1/256-px sub-pixel grid
/// (800.Atom_piece_ps.any:290-300), so a vertex - and with it every point of the triangle - can sit one snap step off. Per
/// channel, at every pixel outside the class:
///     |decode - stock| <= EPS x G + q(stock),   EPS = 1/256 px (DERIVED, never fitted), G = the stock picture's largest
///     difference to a 3 x 3 neighbour (its gradient per pixel), q = ONE fp16 quantum at the stock value (each arm rounds by
///     at most half of it).
/// THE DISPLAY COROLLARY beside it: outside the class a display code differs by at most 1, and only where the linear values
/// differ. Printed: the pixels over the bound, the worst excess in quanta ((|D| - EPS G) / q) and in EPS ((|D| - q) / G).
static const double kEps = 1.0 / 256.0;

static double fp16Quantum(double v)
{
    v = std::fabs(v);
    if (v < 6.103515625e-05) return 5.960464477539063e-08;   // the subnormal step 2^-24
    int e = 0;
    std::frexp(v, &e);                                        // v in [2^(e-1), 2^e): 10 bits below
    return std::ldexp(1.0, e - 11);
}

struct Law
{
    bool read = false;
    size_t px = 0, over = 0, corollaryBad = 0, controlOver = 0;
    double worstQ = 0.0, worstEps = 0.0;   ///< the worst excess, in quanta and in EPS
    unsigned worstX = 0, worstY = 0;
};

static Law law(const Fixture &F, const char *arm, const ImageF *open, const ImageF *shut, const Image &openCodes,
               const Image &shutCodes, const atomfix::IdImage &ids, NodeId control)
{
    Law L;
    if (!open || open->rgba.size() != size_t(kW) * kH * 4u || shut->rgba.size() != open->rgba.size() || !ids.ok ||
        openCodes.rgba.size() != size_t(kW) * kH * 4u || shutCodes.rgba.size() != openCodes.rgba.size())
        return L;
    L.read = true;
    int named = 0;
    for (unsigned y = 1; y + 1 < kH; ++y)
        for (unsigned x = 1; x + 1 < kW; ++x) {
            const size_t p = size_t(y) * kW + x;
            bool edge = false;
            double G[3] = { 0.0, 0.0, 0.0 };
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const size_t q = size_t(int(y) + dy) * kW + size_t(int(x) + dx);
                    if (ids.words[2 * q] != ids.words[2 * p] || ids.words[2 * q + 1] != ids.words[2 * p + 1]) edge = true;
                    for (int c = 0; c < 3; ++c)
                        G[c] = std::max(G[c], std::fabs(double(shut->rgba[q * 4 + size_t(c)]) - double(shut->rgba[p * 4 + size_t(c)])));
                }
            if (edge) continue;
            size_t k = 0;
            while (k < F.cells.size() && (ids.words[2 * p] & AtomId::kSlotMask) != F.cells[k].slot) ++k;
            if (k < F.cells.size() && !F.inScope(F.cells[k].name)) continue;
            const bool isControl = k < F.cells.size() && F.cells[k].node == control;
            bool over = false, linDiffer = false;
            double xq = 0.0, xe = 0.0;
            for (int c = 0; c < 3; ++c) {
                const double sv = double(shut->rgba[p * 4 + size_t(c)]), dv = std::fabs(double(open->rgba[p * 4 + size_t(c)]) - sv);
                const double q = fp16Quantum(sv), bound = kEps * G[c] + q;
                linDiffer = linDiffer || dv > 0.0;
                if (dv > bound) {
                    over = true;
                    xq = std::max(xq, (dv - kEps * G[c]) / q);
                    xe = std::max(xe, G[c] > 0.0 ? (dv - q) / G[c] / kEps : 1e30);
                }
            }
            int dc = 0;
            for (int c = 0; c < 3; ++c)
                dc = std::max(dc, std::abs(int(openCodes.rgba[p * 4 + size_t(c)]) - int(shutCodes.rgba[p * 4 + size_t(c)])));
            if (isControl) { L.controlOver += over; continue; }
            ++L.px;
            if (dc > 1 || (dc > 0 && !linDiffer)) ++L.corollaryBad;
            if (!over) continue;
            ++L.over;
            if (xq > L.worstQ) { L.worstQ = xq; L.worstX = x; L.worstY = y; }
            L.worstEps = std::max(L.worstEps, xe);
            if (named++ < 12)
                std::printf("     [%s] OVER THE BOUND %s (%u, %u): excess %.2f q, %.2f eps, %d code(s)\n", arm,
                            k < F.cells.size() ? F.cells[k].name : "?", x, y, xq, xe, dc);
        }
    return L;
}

/// ATOM_DECODE_DUMP=<dir> (a diagnosis, never the row's verdict): the arm's two pictures (PPM) and the id image's slot and
/// cluster words as a picture (PPM: R G = the slot, B = the triangle's low byte), for a crop by hand.
static void dump(const char *arm, const Image &open, const Image &shut, const atomfix::IdImage &ids)
{
    const char *dir = std::getenv("ATOM_DECODE_DUMP");
    if (!dir || !*dir || open.rgba.size() != size_t(kW) * kH * 4u || shut.rgba.size() != open.rgba.size() || !ids.ok) return;
    auto write = [&](const char *what, auto pixel) {
        const std::string path = std::string(dir) + "/" + arm + "-" + what + ".ppm";
        FILE *f = std::fopen(path.c_str(), "wb");
        if (!f) return;
        std::fprintf(f, "P6\n%u %u\n255\n", kW, kH);
        for (size_t p = 0; p < size_t(kW) * kH; ++p) {
            unsigned char rgb[3];
            pixel(p, rgb);
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
        std::printf("     [%s] dumped %s\n", arm, path.c_str());
    };
    write("decode", [&](size_t p, unsigned char *c) { std::memcpy(c, &open.rgba[p * 4], 3); });
    write("stock", [&](size_t p, unsigned char *c) { std::memcpy(c, &shut.rgba[p * 4], 3); });
    write("ids", [&](size_t p, unsigned char *c) {
        const uint32_t x = ids.words[2 * p], y = ids.words[2 * p + 1];
        c[0] = (unsigned char)(x * 53u);
        c[1] = (unsigned char)((x >> 8) * 97u);
        c[2] = (unsigned char)(y & 0xFFu);
    });
}

/// One arm: the door open (the decode; its routes and ids), then shut (stock PBS); the law per cell. `control`: the shut
/// arm's rough cell one albedo code up - its outside-class pixels must differ (the comparison sees a code).
static void arm(Fixture &F, const char *name, bool control)
{
    F.scene->setAtomDrawEnabled(true);
    Image open, shut;
    int fo = 0, fs = 0;
    const bool so = settle(F.e, F.view, open, fo);
    ImageF openHdr, shutHdr;
    const bool ho = F.view->readPixelsHdr(openHdr);
    const AtomDrawStatus st = F.scene->atomDrawStatus();
    const atomfix::IdImage ids = atomfix::readIds(F.rig, F.rig.views[0]);
    for (Cell &c : F.cells) c.slot = slotOf(F.scene, c.node);
    std::printf("  [%s] open: on %d atom items %u (two-sided %u, masked %u) pbs %u pending %u alphaTested %u\n", name,
                int(st.on), st.atomItems, st.atomTwoSided, st.atomMasked, st.pbsItems, st.pending, st.alphaTested);
    CHECK_MSG(st.on && st.atomItems == unsigned(F.cells.size()) && st.pbsItems == 0u && st.atomTwoSided == 1u &&
                  st.atomMasked == 1u && ids.ok,
              "[%s] the door open: every cell rides Atom (%u of %zu items, pbs %u, two-sided %u, masked %u), its id image read",
              name, st.atomItems, F.cells.size(), st.pbsItems, st.atomTwoSided, st.atomMasked);
    if (control) F.scene->setNodeMaterial(F.rough, F.roughMatPlusOne);
    F.scene->setAtomDrawEnabled(false);
    const bool ss = settle(F.e, F.view, shut, fs);
    const bool hs = F.view->readPixelsHdr(shutHdr);
    std::vector<uint32_t> none;
    unsigned nw = 0, nh = 0;
    const bool shutIds = F.e->readAtomIds(F.view, none, nw, nh);
    F.scene->setAtomDrawEnabled(true);
    if (control) F.scene->setNodeMaterial(F.rough, F.roughMat);
    CHECK_MSG(so && ss && !shutIds, "[%s] both pictures still (%d / %d frames); the door shut has no id pass (stock PBS)",
              name, fo, fs);
    size_t unowned = 0;
    const std::vector<Tally> t = compare(F, open, shut, ids, unowned, name, control ? F.rough : NodeId(0));
    dump(name, open, shut, ids);
    const Law L = law(F, name, ho && hs ? &openHdr : nullptr, &shutHdr, open, shut, ids, control ? F.rough : NodeId(0));
    for (size_t k = 0; k < t.size(); ++k)
        std::printf("   [%s] %-15s %7zu px, %6zu in the class (worst %3d codes), %6zu outside it differ (worst %3d codes)\n",
                    name, F.cells[k].name, t[k].px, t[k].inClass, t[k].worstIn, t[k].outside, t[k].worstOutside);
    bool everyCellSeen = true;
    for (const Tally &a : t) everyCellSeen = everyCellSeen && a.px > a.inClass;
    CHECK_MSG(everyCellSeen && unowned == 0u, "[%s] every cell has pixels outside the class, every pixel a cell's (%zu unowned)",
              name, unowned);
    std::printf("   [%s] THE LAW: %zu px outside the class, %zu over |D| <= eps G + q (worst excess %.2f q at (%u, %u), "
                "%.2f eps); the display corollary broken on %zu px\n", name, L.px, L.over, L.worstQ, L.worstX, L.worstY,
                L.worstEps, L.corollaryBad);
    if (control)
        CHECK_MSG(L.read && L.controlOver > 0u,
                  "[%s] NEGATIVE CONTROL: the rough cell one albedo code up on the shut arm breaks the bound (%zu px)", name,
                  L.controlOver);
    CHECK_MSG(L.read && L.over == 0u && L.corollaryBad == 0u,
              "[%s] THE DECODE IS STOCK PBS: |decode - stock| <= eps G + q at every pixel outside the class, eps = 1/256 px, "
              "q one fp16 quantum (%zu of %zu over, worst %.2f q / %.2f eps; corollary broken on %zu)", name, L.over, L.px,
              L.worstQ, L.worstEps, L.corollaryBad);
}

int main(int argc, char **argv)
{
    // THE ROWS (one per proof): materials = every cell but the normal map under the sun (+ the negative control);
    // normal_map = the normal-mapped cell under the sun, a point and a spot; point / spot = every cell but the normal map
    // under that lamp; area = every cell under the area lamp (LTC).
    const std::string mode = argc > 1 ? argv[1] : "";
    std::vector<std::string> arms;
    if (mode == "materials") arms = { "sun" };
    else if (mode == "normal_map") arms = { "sun", "point", "spot" };
    else if (mode == "point" || mode == "spot" || mode == "area") arms = { mode };
    else {
        std::printf("FAIL: usage: test_atom_decode_parity materials|normal_map|point|spot|area\n");
        return 2;
    }
    std::printf("== atom.decode_parity.%s: the decode = stock PBS, |decode - stock| <= eps G + q outside the named class\n",
                mode.c_str());
    Fixture F;
    F.scope = mode == "normal_map" ? "normal_map" : mode == "area" ? "all" : "no_normal_map";
    const atomfix::Lens lens = { "decode", 0.41421356237309503 * 16.0 / 9.0, 0.41421356237309503, double(kW), double(kH),
                                 false, 0.0, 0.0 };
    if (!atomfix::boot(F.rig, "test-atom-decode-parity-" + mode + "-ogre.log", { lens })) {
        std::printf("FAIL: boot: %s\n", F.rig.error.c_str());
        return 1;
    }
    F.e = F.rig.engine.get();
    F.view = F.rig.views[0].view;
    F.scene = F.rig.scene;
    F.view->setShadows(false);
    {
        // THE LINEAR PICTURE beside the display codes (readPixelsHdr: the scene radiance before the tonemap)
        PostFxDesc fx;
        fx.allowOffscreen = true;
        fx.hdrReadback = true;
        F.view->setPostFx(fx);
    }
    enginetest::declareRoutes(F.view, F.scene, enginetest::Routes(), "atom.decode_parity's view");
    build(F);
    enginetest::testCameraAt(F.view, Vec3(0.0f, 0.0f, 0.0f));
    enginetest::frames(F.e, 4);
    if (!F.scene->atomDrawOn()) {
        std::printf("FAIL: the visibility buffer is off on this device (no id pass)\n");
        return 1;
    }
    // a lamp's -Y is its axis; 90 degrees about X turns -Y onto -Z (toward the cells)
    const float h = 0.70710678f;
    for (const std::string &a : arms) {
        NodeId n = 0;
        if (a == "sun") {
            n = enginetest::addDirectionalLight(F.scene, Vec3(-0.3f, -0.5f, -0.8f), 2.2f);
        } else {
            const LightType type = a == "point" ? LightType::Point : a == "spot" ? LightType::Spot : LightType::Area;
            n = F.scene->createNode();
            F.scene->setNodeTransform(n, a == "point" ? Vec3(0.0f, 0.5f, kCellZ + 2.5f)
                                         : Vec3(0.0f, 0.0f, kCellZ + (a == "spot" ? 4.0f : 2.0f)),
                                      a == "point" ? Quat() : Quat(h, 0.0f, 0.0f, h), Vec3(1, 1, 1));
            LightDesc d;
            d.type = type;
            d.intensity = type == LightType::Area ? 6.0f : 12.0f;
            d.range = 12.0f;
            d.spotAngleDegrees = 40.0f;
            d.castShadows = false;
            d.rectWidth = 3.0f;
            d.rectHeight = 1.5f;
            F.scene->setLight(n, d);
        }
        arm(F, a.c_str(), mode == "materials");
        F.scene->removeLight(n);
        F.scene->removeNode(n);
    }
    atomfix::shutdown(F.rig);
    std::printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
    return failures ? 1 : 0;
}
