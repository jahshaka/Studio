// GA-FAR (lanes MEASURE-1b, PHOTON-GAFAR-1) — a TOOL, not a suite. It answers
// with numbers how long the screen-probe gather's rays need to be.
//
// THE CODE UNDER MEASUREMENT:
//   irisgl/engine/src/rayquery/rq_probe_gather.comp — maxT = p.knobs.y; origin =
//       posW + n*bias; the NEAR query (mask 0x0F) is gl_RayFlagsOpaqueEXT over
//       [bias, maxT]; a ray that escapes it runs the FAR query (mask 0x10, the
//       coarse copies, ATOM-FARBLAS-1) over [maxT, far plane]; a committed
//       triangle is shaded from the cascades (black where they cannot shade it),
//       a miss of both reads the SKY.
//   irisgl/engine/src/OgreScreenProbeGather.cpp (`reach`) — maxT = the outer
//       cascade's HALF extent (the lit volume's inscribed radius) under the
//       camera's far plane, unless tuning.rayLength overrides.
//
// THE HISTORY THE NUMBERS SETTLED: maxT used to be the outer box's full
// DIAGONAL and a missed ray read the outer cascades at its end point (a "far
// term") before the sky. MEASURE-1b showed that end point lay in no cascade, so
// the term never fired (spikes/measure-1bd/FINDINGS.md); PHOTON-GAFAR-1's sweep
// (`GAFAR_SWEEP=1`, below) showed that at a length where it could fire it moved
// the picture AWAY from the long-ray reference, and that the ray's length costs
// nothing measurable — so the term was deleted and the length set to the half
// extent (spikes/photon-gafar-1).
//
// THE INSTRUMENT. The gather's rays are traced in a compute shader whose atlas
// is a raw VkImage with no readback path, so this tool does not read the
// gather's own rays: it REPRODUCES them on the CPU and traces them against THE
// SAME acceleration structure through `Engine::traceRays` (Engine.h:1017 — 12
// floats in, 4 out, distance < 0 = miss). Every stochastic input is copied
// bit-for-bit out of jah_probe_params.glsl (jahPcg / jahSample2 / jahOctPoint /
// jahTangentFrame), so the direction set is the shader's own. The probe SET is
// reproduced geometrically: one probe per stride x stride pixel block, at the
// surface the centre pixel sees, with the normal from a 3-ray finite difference
// (probes whose three rays hit different nodes are dropped — an edge pixel's
// normal would be invented). The gather jitters the probe inside its cell and
// this tool does not; a fraction over thousands of probes does not depend on it.
//
// DEFAULT MODE: per fixture and tier, the else-branch %, where a miss's end
// point lies, the hit deciles of maxT, and the cross-check that rendering with
// tuning.rayLength forced to the maxT this tool derives is pixel-identical to
// the engine's own derivation (freezeFrameIndex on). SWEEP MODE: see sweepMain.
// FAR-BLAS MODE (`FARBLAS=1`, ATOM-FARBLAS-1): see farBlasMain.
//
// Build: `ninja -C build-linux gather_far_measure`; run on a rig display with
// DISPLAY set (a Vulkan engine cannot boot without one).
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

static const unsigned kW = 640u, kH = 360u;   // 16:9, the framing hold's own aspect
static const float PI = 3.14159265358979323846f;

static void render(Engine *e, int frames) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

// ---------------------------------------------------------------------------
// THE SHADER'S OWN ARITHMETIC (jah_probe_params.glsl:124-170), in C++.
static unsigned jahPcg(unsigned v)
{
    unsigned state = v * 747796405u + 2891336453u;
    unsigned word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}
static void jahSample2(int cx, int cy, unsigned index, unsigned frame, float &u, float &v)
{
    const unsigned h = jahPcg(unsigned(cx) * 1973u + unsigned(cy) * 9277u + index * 20749u +
                              frame * 26699u);
    u = float(h & 0xFFFFu) / 65536.0f;
    v = float(h >> 16) / 65536.0f;
}
static void jahOctPoint(float u, float v, float o[3])
{
    const float fx = u * 2.0f - 1.0f, fy = v * 2.0f - 1.0f;
    const float dx = (fx + fy) * 0.5f, dy = (fx - fy) * 0.5f;
    o[0] = dx; o[1] = dy; o[2] = 1.0f - std::fabs(dx) - std::fabs(dy);
}
static void jahTangentFrame(const float n[3], float t[3], float b[3])
{
    const float sg = n[2] >= 0.0f ? 1.0f : -1.0f;
    const float a0 = -1.0f / (sg + n[2]);
    const float b0 = n[0] * n[1] * a0;
    t[0] = 1.0f + sg * n[0] * n[0] * a0; t[1] = sg * b0; t[2] = -sg * n[0];
    b[0] = b0; b[1] = sg + n[1] * n[1] * a0; b[2] = -n[1];
}

// ---------------------------------------------------------------------------
struct V3 { float x = 0, y = 0, z = 0; };
static V3 operator+(const V3 &a, const V3 &b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static V3 operator-(const V3 &a, const V3 &b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static V3 operator*(const V3 &a, float s) { return { a.x * s, a.y * s, a.z * s }; }
static float dot(const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 cross(const V3 &a, const V3 &b)
{
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
static float len(const V3 &a) { return std::sqrt(dot(a, a)); }
static V3 norm(const V3 &a) { const float l = len(a); return l > 0 ? a * (1.0f / l) : a; }
/// Rotate by a quaternion (Vec3/Quat are plain structs on the boundary).
static V3 rotate(const Quat &q, const V3 &v)
{
    const V3 u{ q.x, q.y, q.z };
    const float s = q.w;
    return u * (2.0f * dot(u, v)) + v * (s * s - dot(u, u)) + cross(u, v) * (2.0f * s);
}

/// THE MASK WORD IS BIT-COPIED, NOT CONVERTED (tests/gi/test_gi_rayquery.cpp's
/// pushRay): it travels through a float array and the shader reads its bits, so
/// 255.0f would arrive as 0x437F0000 and match nothing we mean.
static float maskBits(unsigned m)
{
    float f;
    std::memcpy(&f, &m, sizeof(f));
    return f;
}

struct Probe { V3 p; V3 n; int cx = 0, cy = 0; };

/// Camera rays through a pixel grid; the surface point and its normal from a
/// 3-ray finite difference. `hits` come back from the SAME TLAS the gather uses.
static std::vector<Probe> placeProbes(Scene *s, const CameraDesc &cam, unsigned stride,
                                      unsigned &pixelsTried, unsigned &dropped)
{
    const float tanV = std::tan(cam.fovDegrees * 0.5f * PI / 180.0f);
    const float tanH = tanV * (float(kW) / float(kH));
    std::vector<float> rays;
    std::vector<std::pair<int,int>> cells;
    const V3 camPos{ cam.position.x, cam.position.y, cam.position.z };
    pixelsTried = 0;
    for (unsigned py = stride / 2u; py < kH; py += stride) {
        for (unsigned px = stride / 2u; px < kW; px += stride) {
            ++pixelsTried;
            for (int k = 0; k < 3; ++k) {
                const float fx = float(px) + 0.5f + (k == 1 ? 1.0f : 0.0f);
                const float fy = float(py) + 0.5f + (k == 2 ? 1.0f : 0.0f);
                const float ndcX = 2.0f * fx / float(kW) - 1.0f;
                const float ndcY = 1.0f - 2.0f * fy / float(kH);
                const V3 dCam{ ndcX * tanH, ndcY * tanV, -1.0f };
                const V3 d = norm(rotate(cam.orientation, dCam));
                rays.insert(rays.end(), { camPos.x, camPos.y, camPos.z, cam.nearClip,
                                          d.x, d.y, d.z, cam.farClip,
                                          maskBits(0xFFu), 0.0f, 0.0f, 0.0f });
            }
            cells.push_back({ int(px / stride), int(py / stride) });
        }
    }
    std::vector<float> hits;
    std::vector<Probe> out;
    if (!s->traceRays(rays, hits) || hits.size() != rays.size() / 3) {
        std::printf("    !! traceRays refused the camera batch (%zu rays)\n", rays.size() / 12);
        return out;
    }
    dropped = 0;
    for (size_t i = 0; i < cells.size(); ++i) {
        const float *h0 = &hits[(i * 3 + 0) * 4], *hx = &hits[(i * 3 + 1) * 4],
                    *hy = &hits[(i * 3 + 2) * 4];
        if (h0[0] < 0.0f || hx[0] < 0.0f || hy[0] < 0.0f) { ++dropped; continue; }
        if (h0[1] != hx[1] || h0[1] != hy[1]) { ++dropped; continue; }   // an edge: no normal
        const float *r0 = &rays[(i * 3 + 0) * 12], *rx = &rays[(i * 3 + 1) * 12],
                    *ry = &rays[(i * 3 + 2) * 12];
        const V3 o{ r0[0], r0[1], r0[2] };
        const V3 p0 = o + V3{ r0[4], r0[5], r0[6] } * h0[0];
        const V3 pX = o + V3{ rx[4], rx[5], rx[6] } * hx[0];
        const V3 pY = o + V3{ ry[4], ry[5], ry[6] } * hy[0];
        V3 n = cross(pX - p0, pY - p0);
        if (len(n) <= 1e-9f) { ++dropped; continue; }
        n = norm(n);
        if (dot(n, o - p0) < 0.0f) n = n * -1.0f;      // face the eye, like the prepass normal
        out.push_back({ p0, n, cells[i].first, cells[i].second });
    }
    return out;
}

struct Box { V3 lo, hi; };
static bool inside(const Box &b, const V3 &p)
{
    return p.x >= b.lo.x && p.x <= b.hi.x && p.y >= b.lo.y && p.y <= b.hi.y &&
           p.z >= b.lo.z && p.z <= b.hi.z;
}

struct RayStats {
    unsigned long long rays = 0, miss = 0, hit = 0;
    unsigned long long endpointInOuter = 0, endpointInAny = 0;   // the geometric farOk candidates
    unsigned long long hitsLast10 = 0;                           // hits in the last 10 % of maxT
    unsigned long long bins[10] = { 0 };
    double meanHitT = 0.0, maxHitT = 0.0;
};

/// The gather's ray set, reproduced, traced, counted.
static RayStats traceProbeRays(Scene *s, const std::vector<Probe> &probes, unsigned octRes,
                               float maxT, float bias, const std::vector<Box> &cascades)
{
    RayStats st;
    const unsigned lanes = octRes * octRes;
    std::vector<float> rays;
    std::vector<V3> dirs, origins;
    rays.reserve(probes.size() * lanes * 12);
    for (const Probe &pr : probes) {
        float T[3], B[3];
        const float nn[3] = { pr.n.x, pr.n.y, pr.n.z };
        jahTangentFrame(nn, T, B);
        const V3 origin = pr.p + pr.n * bias;
        for (unsigned lane = 0; lane < lanes; ++lane) {
            const int tx = int(lane) % int(octRes), ty = int(lane) / int(octRes);
            float ju, jv;
            jahSample2(pr.cx, pr.cy, lane + 1u, 0u, ju, jv);   // frame 0 = freezeFrameIndex
            const float u = (float(tx) + ju) / float(octRes);
            const float v = (float(ty) + jv) / float(octRes);
            float o[3];
            jahOctPoint(u, v, o);
            const float invR = 1.0f / std::max(std::sqrt(o[0]*o[0] + o[1]*o[1] + o[2]*o[2]), 1e-6f);
            const float lx = o[0] * invR, ly = o[1] * invR, lz = o[2] * invR;
            const V3 dir = norm(V3{ T[0]*lx + B[0]*ly + nn[0]*lz,
                                    T[1]*lx + B[1]*ly + nn[1]*lz,
                                    T[2]*lx + B[2]*ly + nn[2]*lz });
            rays.insert(rays.end(), { origin.x, origin.y, origin.z, bias,
                                      dir.x, dir.y, dir.z, maxT,
                                      maskBits(0xFFu), 0.0f, 0.0f, 0.0f });
            dirs.push_back(dir);
            origins.push_back(origin);
        }
    }
    // The batch is large (2k probes x 64 rays); trace it in chunks so one
    // submission never holds a huge staging buffer.
    const size_t kChunk = 65536;
    std::vector<float> chunk, hits;
    size_t base = 0;
    while (base < dirs.size()) {
        const size_t n = std::min(kChunk, dirs.size() - base);
        chunk.assign(rays.begin() + long(base * 12), rays.begin() + long((base + n) * 12));
        if (!s->traceRays(chunk, hits) || hits.size() != n * 4) {
            std::printf("    !! traceRays refused a probe-ray chunk of %zu\n", n);
            return st;
        }
        for (size_t i = 0; i < n; ++i) {
            const float t = hits[i * 4];
            ++st.rays;
            if (t >= 0.0f) {
                ++st.hit;
                st.meanHitT += double(t);
                st.maxHitT = std::max(st.maxHitT, double(t));
                const int bin = std::min(9, int(10.0f * t / std::max(maxT, 1e-6f)));
                ++st.bins[bin];
                if (t >= 0.9f * maxT) ++st.hitsLast10;
            } else {
                ++st.miss;
                const V3 end = origins[base + i] + dirs[base + i] * maxT;
                if (!cascades.empty() && inside(cascades.back(), end)) ++st.endpointInOuter;
                for (const Box &b : cascades)
                    if (inside(b, end)) { ++st.endpointInAny; break; }
            }
        }
        base += n;
    }
    if (st.hit) st.meanHitT /= double(st.hit);
    return st;
}

// ---------------------------------------------------------------------------
/// `meanAll` is the worst-channel error averaged over EVERY pixel (moved or not):
/// the sweep compares two arms' distance from one reference by it, because a
/// "px moved" count cannot say which of two pictures is NEARER the reference.
struct Delta { unsigned moved = 0, total = 0, worst = 0; double meanMoved = 0, meanAll = 0; };
static Delta deltaOf(const Image &a, const Image &b)
{
    Delta d;
    if (a.width != b.width || a.height != b.height) return d;
    d.total = a.width * a.height;
    double sum = 0.0;
    for (size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        unsigned w = 0;
        for (int c = 0; c < 3; ++c)
            w = std::max(w, unsigned(std::abs(int(a.rgba[i + c]) - int(b.rgba[i + c]))));
        if (w) { ++d.moved; sum += w; d.worst = std::max(d.worst, w); }
    }
    d.meanMoved = d.moved ? sum / d.moved : 0.0;
    d.meanAll = d.total ? sum / d.total : 0.0;
    return d;
}

static void armChain(View *view, int ssrRow)
{
    PostFxDesc fx;
    fx.allowOffscreen = true;
    fx.ssr = ssrRow;       // the tier's own SSR row (the gather's stride is epicTier's)
    view->setPostFx(fx);
}

/// THE TIER AXIS IS STUDIO'S OWN TABLE, and `GiQuality` is only part of it
/// (src/services/worldmodes.cpp — Epic shares High's resolution dial and
/// differs in the bounce count, the SSR row and the gather's density). The
/// gather's stride is the engine's tier table's (`giQualityFacts`, keyed on
/// GiParams::epicTier: 8 at Epic, 16 below), never the SSR row.
struct Tier { const char *name; GiQuality quality; GiMode mode; int bounces; int ssrRow; bool epic; };
static const Tier kTiers[3] = {
    { "Medium", GiQuality::Medium, GiMode::Vct,         1, 0, false },
    { "High",   GiQuality::High,   GiMode::VctPccHybrid, 1, 1, false },
    { "Epic",   GiQuality::High,   GiMode::VctPccHybrid, 3, 2, true },
};

static GiParams giAt(const Tier &t)
{
    GiParams gi;
    gi.mode = t.mode;
    gi.quality = t.quality;
    gi.cascades = true;
    gi.ddgi = GiToggle::Off;          // STATED: the field is off; this is the gather's own ray
    gi.numBounces = t.bounces;
    gi.epicTier = t.epic;
    gi.gather = GiToggle::On;
    return gi;
}
static void arm(Scene *s, const Tier &tier, float rayLength)
{
    s->setGlobalIllumination(giAt(tier));
    GatherTuning t;                    // every zero = what the tier derives
    t.freezeFrameIndex = true;         // a still frame is byte-deterministic
    t.rayLength = rayLength;
    s->setGatherTuning(t);
}

// ---------------------------------------------------------------------------
// THE FIXTURES.
static NodeId slab(Scene *s, const Colour &c, const V3 &pos, const V3 &scale, float rough = 0.9f)
{
    const NodeId n = s->createNode();
    const MeshId m = s->createMesh(enginetest::unitCubeMesh());
    PbrParams p; p.albedo = c; p.metalness = 0.0f; p.roughness = rough;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!n || !m || !mat || !s->attachMesh(n, m, mat)) return 0;
    s->setNodeTransform(n, Vec3{ pos.x, pos.y, pos.z }, Quat(), Vec3{ scale.x, scale.y, scale.z });
    return n;
}
static void sunAndSky(Scene *s, bool sky)
{
    enginetest::addDirectionalLight(s, Vec3{ -0.4f, -1.0f, -0.55f }, 4.0f);
    SkyDesc d;
    if (sky) { d.mode = SkyMode::Atmosphere; }
    s->setSky(d);
    s->setAmbient(Colour(0.05f, 0.05f, 0.06f), Colour(0.03f, 0.03f, 0.035f));
}

/// (A) THE OPEN SCENE — the shipped default project's shape: one large matte
/// ground, one cube on it, a sun and an atmosphere sky. Half the hemisphere of
/// every ground probe points at the sky.
static CameraDesc buildOpen(Scene *s)
{
    slab(s, Colour(0.45f, 0.45f, 0.45f), { 0, -0.1f, 0 }, { 200, 0.2f, 200 });
    slab(s, Colour(0.6f, 0.25f, 0.2f), { 0, 0.5f, 0 }, { 1, 1, 1 });
    sunAndSky(s, true);
    return enginetest::testCameraDescLookAt(Vec3{ 4.0f, 2.2f, 6.0f }, Vec3{ 0.0f, 0.6f, 0.0f });
}
/// (B) THE CLOSED BOX — a sealed 10 m room, walls 0.5 m, camera inside. No ray
/// can leave the lit world, so "no hit inside maxT" can only mean "maxT is
/// shorter than the room".
static CameraDesc buildBox(Scene *s)
{
    const float H = 5.0f, T = 0.5f;
    slab(s, Colour(0.6f, 0.6f, 0.6f), { 0, -T * 0.5f, 0 }, { 2*H, T, 2*H });      // floor
    slab(s, Colour(0.6f, 0.6f, 0.6f), { 0, H + T * 0.5f, 0 }, { 2*H, T, 2*H });   // ceiling
    slab(s, Colour(0.55f, 0.55f, 0.6f), { -H, H*0.5f, 0 }, { T, H, 2*H });
    slab(s, Colour(0.55f, 0.55f, 0.6f), {  H, H*0.5f, 0 }, { T, H, 2*H });
    slab(s, Colour(0.6f, 0.55f, 0.55f), { 0, H*0.5f, -H }, { 2*H, H, T });
    slab(s, Colour(0.6f, 0.55f, 0.55f), { 0, H*0.5f,  H }, { 2*H, H, T });
    slab(s, Colour(0.7f, 0.3f, 0.3f), { 1.5f, 0.6f, -1.0f }, { 1.2f, 1.2f, 1.2f });
    const NodeId ln = s->createNode();
    if (ln) {
        s->setNodeTransform(ln, Vec3{ 0.0f, 3.6f, 0.0f }, Quat(), Vec3{ 1, 1, 1 });
        LightDesc lamp;
        lamp.type = LightType::Point;
        lamp.colour = Colour(1.0f, 1.0f, 1.0f);
        lamp.intensity = 12.0f;
        lamp.range = 20.0f;
        s->setLight(ln, lamp);
    }
    SkyDesc none; s->setSky(none);
    s->setAmbient(Colour(0.02f, 0.02f, 0.02f), Colour(0.01f, 0.01f, 0.01f));
    return enginetest::testCameraDescLookAt(Vec3{ 3.0f, 1.7f, 3.0f }, Vec3{ -1.0f, 1.2f, -1.0f });
}
/// (C) SHOWROOM-SHAPED — four walls, no ceiling, forty props, a sun and a sky:
/// the shape sc1b_measure.cpp uses for "Showroom 2 density" (a Studio PROJECT
/// cannot be opened by an engine-level tool; stated in the findings).
static CameraDesc buildShowroom(Scene *s)
{
    slab(s, Colour(0.5f, 0.5f, 0.5f), { 0, -0.1f, 0 }, { 60, 0.2f, 60 });
    const float H = 20.0f, wallH = 6.0f, T = 0.4f;
    slab(s, Colour(0.6f, 0.6f, 0.6f), { -H, wallH*0.5f, 0 }, { T, wallH, 2*H });
    slab(s, Colour(0.6f, 0.6f, 0.6f), {  H, wallH*0.5f, 0 }, { T, wallH, 2*H });
    slab(s, Colour(0.6f, 0.6f, 0.6f), { 0, wallH*0.5f, -H }, { 2*H, wallH, T });
    slab(s, Colour(0.6f, 0.6f, 0.6f), { 0, wallH*0.5f,  H }, { 2*H, wallH, T });
    unsigned k = 0;
    for (int gx = 0; gx < 8; ++gx)
        for (int gz = 0; gz < 5; ++gz, ++k) {
            const float x = -14.0f + float(gx) * 4.0f, z = -8.0f + float(gz) * 4.0f;
            const float h = 0.8f + 0.4f * float((k * 7u) % 5u);
            slab(s, Colour(0.55f, 0.5f + 0.02f * float(k % 5), 0.45f),
                 { x, h * 0.5f, z }, { 1.4f, h, 1.4f });
        }
    sunAndSky(s, true);
    return enginetest::testCameraDescLookAt(Vec3{ 12.0f, 2.0f, 14.0f }, Vec3{ -2.0f, 1.2f, -2.0f });
}

// ---------------------------------------------------------------------------
// THE RAY-LENGTH SWEEP (lane PHOTON-GAFAR-1, `GAFAR_SWEEP=1`). The question the
// cross-checks above leave open: how long does a gather ray NEED to be? Three
// lengths per fixture and tier — the outer cascade's HALF extent (the shipped
// derivation), half its diagonal, and the diagonal (the old one) — in this
// process, at this pose, the GI configuration pushed ONCE
// (a tuning push re-voxelises nothing, so every arm reads the same volumes and
// the same TLAS; `arm()` above re-pushes the configuration and is NOT used here).
//
// Per arm:
//   (a) THE PICTURE against the diagonal (the longest-ray reference): px moved,
//       mean over moved px, worst, and the mean over ALL px (`meanAll`) — the
//       number that says which of two arms is NEARER the reference;
//   (b) THE COST: the gather's own GPU timestamps (place + trace + integrate),
//       on a second 1920x1080 view of the same scene (the 640x360 picture view
//       stands down — one gathering view at a time, GATHER-0's D3), 120 frames
//       of warm-up, then the arms INTERLEAVED in ten rounds of 36 frames with the
//       first six of each block dropped (the timestamps come back frames late),
//       300 frames per arm. CLOCKS ARE NOT LOCKED: the number is the RATIO to
//       the diagonal's arm in the same rounds, never a millisecond;
//   (c) THE RAYS on the CPU (the shader's own ray set against the same TLAS): the
//       else-branch %, the % of a miss's end points inside a cascade box, and
//       the hits the length loses against the diagonal.
// Every picture is frozen (`freezeFrameIndex`), and the instrument's own floor
// is measured first: the reference arm rendered twice must move 0 px.
static void tune(Scene *s, float rayLength)
{
    GatherTuning t;
    t.freezeFrameIndex = true;
    t.rayLength = rayLength;
    s->setGatherTuning(t);
}

static int farBlasMain(Engine *e, View *view);

static int sweepMain(Engine *e, View *view)
{
    View *big = e->createOffscreenView("gafar-cost", 1920u, 1080u, Colour(0, 0, 0));
    if (big) big->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!big) { std::printf("FAIL: cost view: %s\n", e->lastError().c_str()); return 1; }
    big->setShadows(true);
    big->setEnabled(false);

    struct Fix { const char *name; CameraDesc (*build)(Scene *); };
    const Fix fixtures[3] = { { "open-sky", buildOpen }, { "closed-box", buildBox },
                              { "showroom-shaped", buildShowroom } };
    // 0 = the outer half extent, 1 = half the diagonal, 2 = the diagonal.
    const char *armName[3] = { "half", "hdiag", "diag" };
    std::printf("== GAFAR-1 SWEEP: ray length, frozen frame, paired in one process ==\n");
    int bad = 0;
    for (const Fix &f : fixtures) {
        for (int ti = 0; ti < 3; ++ti) {
            const Tier &tier = kTiers[ti];
            Scene *s = e->createScene(std::string("sweep-") + f.name + "-" + tier.name);
            if (!s) { std::printf("FAIL: scene\n"); return 1; }
            view->setEnabled(true);
            view->setScene(s);
            armChain(view, tier.ssrRow);
            const CameraDesc cam = f.build(s);
            view->setCamera(cam);
            s->setGlobalIllumination(giAt(tier));
            tune(s, 0.0f);
            render(e, 90);

            const GiStatus gs = s->giStatus();
            std::vector<Box> boxes;
            for (const auto &c : gs.cascades) {
                Box b;
                b.lo = { c.centre.x - c.halfSize, c.centre.y - c.halfSize, c.centre.z - c.halfSize };
                b.hi = { c.centre.x + c.halfSize, c.centre.y + c.halfSize, c.centre.z + c.halfSize };
                boxes.push_back(b);
            }
            const float outerHalf = gs.cascades.empty() ? 0.0f : gs.cascades.back().halfSize;
            const float reach = std::sqrt(3.0f) * 2.0f * outerHalf;
            const float len[3] = { outerHalf, 0.5f * reach, reach };
            const GatherStatus gst = gs.gather;
            // THE RAY START, as the gather now makes it (the fix round): an
            // epsilon off the surface, never half a voxel.
            const float bias = 0.001f;
            std::printf("\n-- %s %s: %zu cascades, outer half %.2f m; lengths %.2f / %.2f / %.2f m; "
                        "gather on=%d running=%d stride %u octRes %u\n", f.name, tier.name,
                        boxes.size(), double(outerHalf), double(len[0]), double(len[1]),
                        double(len[2]), int(gst.on), int(gst.running), gst.stride, gst.octRes);

            // ---- (a) the pictures -------------------------------------------
            Image derived, ref, ref2, img[3];
            tune(s, 0.0f);   render(e, 4); view->readPixels(derived);
            for (int a = 0; a < 3; ++a) {
                tune(s, len[a]);
                render(e, 4);
                view->readPixels(img[a]);
            }
            tune(s, len[2]); render(e, 4); view->readPixels(ref2);
            ref = img[2];
            const Delta floor = deltaOf(ref, ref2), der = deltaOf(img[0], derived);
            std::printf("   instrument floor: the diagonal's arm twice %u/%u px; derived vs forced "
                        "half extent %u px\n", floor.moved, floor.total, der.moved);
            if (floor.moved * 100u > floor.total) {
                std::printf("   !! the frozen instrument moved more than 1 %% on its own — "
                            "this row is VOID\n");
                ++bad;
            }

            // ---- (c) the rays -----------------------------------------------
            RayStats rs[3];
            unsigned tried = 0, dropped = 0;
            const std::vector<Probe> probes =
                placeProbes(s, cam, gst.stride ? gst.stride : 16u, tried, dropped);
            for (int l = 0; l < 3; ++l)
                rs[l] = traceProbeRays(s, probes, gst.octRes ? gst.octRes : 8u, len[l], bias, boxes);

            // ---- (b) the cost -----------------------------------------------
            view->setEnabled(false);
            big->setScene(s);
            armChain(big, tier.ssrRow);
            big->setCamera(cam);
            big->setEnabled(true);
            tune(s, len[2]);
            render(e, 120);
            std::vector<float> ms[3];
            for (int round = 0; round < 10; ++round)
                for (int a = 0; a < 3; ++a) {
                    tune(s, len[a]);
                    for (int fr = 0; fr < 36; ++fr) {
                        e->renderOneFrame();
                        if (fr < 6) continue;
                        const GatherStatus q = s->giStatus().gather;
                        if (q.placeMs >= 0.0f && q.traceMs >= 0.0f && q.integrateMs >= 0.0f)
                            ms[a].push_back(q.placeMs + q.traceMs + q.integrateMs);
                    }
                }
            big->setEnabled(false);
            float med[3];
            for (int a = 0; a < 3; ++a) {
                std::vector<float> v = ms[a];
                std::sort(v.begin(), v.end());
                med[a] = v.empty() ? -1.0f : v[v.size() / 2];
            }

            std::printf("   %-6s %7s %7s %6s %5s %8s | %9s | %7s %10s %9s\n", "arm", "len m",
                        "moved", "mean", "worst", "meanAll", "ms ratio", "else%", "endIn%else",
                        "hitsLost%");
            for (int a = 0; a < 3; ++a) {
                const Delta d = deltaOf(ref, img[a]);
                const RayStats &r = rs[a];
                const double rays = double(r.rays ? r.rays : 1);
                std::printf("   %-6s %7.2f %7u %6.2f %5u %8.4f | %9.3f | %7.2f %10.3f %9.4f"
                            "   [n=%zu med %.4f ms]\n",
                            armName[a], double(len[a]), d.moved, d.meanMoved, d.worst, d.meanAll,
                            med[2] > 0 ? double(med[a] / med[2]) : -1.0,
                            100.0 * double(r.miss) / rays,
                            r.miss ? 100.0 * double(r.endpointInAny) / double(r.miss) : 0.0,
                            100.0 * (double(rs[2].hit) - double(r.hit)) /
                                double(rs[2].rays ? rs[2].rays : 1),
                            ms[a].size(), double(med[a]));
            }
            std::printf("   hit deciles of the DIAGONAL: ");
            for (int b = 0; b < 10; ++b)
                std::printf("%.2f%% ", 100.0 * double(rs[2].bins[b]) / double(rs[2].rays ? rs[2].rays : 1));
            std::printf("(max hit %.2f m)\n", rs[2].maxHitT);
            std::fflush(stdout);
            e->destroyScene(s);
        }
    }
    return bad ? 1 : 0;
}

// ---------------------------------------------------------------------------
int main()
{
    std::string err;
    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "gather-far-measure-ogre.log";
    auto engine = Engine::create(cfg, err);
    if (!engine) { std::printf("FAIL: engine create: %s\n", err.c_str()); return 1; }
    engine->setFixedFrameDelta(1.0f / 60.0f);
    Engine *e = engine.get();
    View *view = e->createOffscreenView("gafar", kW, kH, Colour(0, 0, 0));
    if (view) view->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!view) { std::printf("FAIL: view: %s\n", e->lastError().c_str()); return 1; }
    view->setShadows(true);
    if (!e->rayQueryAvailable() || !e->rayTracing()) {
        std::printf("FAIL: this machine has no ray queries — GA-FAR cannot be measured here\n");
        return 1;
    }
    if (std::getenv("GAFAR_SWEEP")) return sweepMain(e, view);
    if (std::getenv("FARBLAS")) return farBlasMain(e, view);

    struct Fix { const char *name; CameraDesc (*build)(Scene *); };
    const Fix fixtures[3] = { { "open-sky", buildOpen }, { "closed-box", buildBox },
                              { "showroom-shaped", buildShowroom } };

    std::printf("== GA-FAR: the gather's else-branch, per tier and scene ==\n");
    std::printf("%-16s %-7s %4s %8s %8s %9s %7s %8s %8s %8s %8s %8s\n",
                "scene", "tier", "casc", "outerHS", "reach", "maxT", "probes",
                "rays", "miss%", "endIn%", "last10%", "meanHit");

    struct Row {
        std::string scene, tier;
        unsigned cascades = 0, probes = 0;
        float outerHalf = 0, reach = 0, maxT = 0, cell = 0;
        RayStats st;
        Delta maxTab;
        unsigned stride = 0, octRes = 0, gatherProbes = 0;
    };
    std::vector<Row> rows;

    for (const Fix &f : fixtures) {
        for (int ti = 0; ti < 3; ++ti) {
            const Tier &tier = kTiers[ti];
            Scene *s = e->createScene(std::string(f.name) + "-" + tier.name);
            if (!s) { std::printf("FAIL: scene\n"); return 1; }
            view->setScene(s);
            armChain(view, tier.ssrRow);
            const CameraDesc cam = f.build(s);
            view->setCamera(cam);
            arm(s, tier, 0.0f);
            render(e, 90);                      // cascades build one per frame; then settle

            Row r;
            r.scene = f.name; r.tier = tier.name;
            const GiStatus gs = s->giStatus();
            std::vector<Box> boxes;
            for (const auto &c : gs.cascades) {
                Box b;
                b.lo = { c.centre.x - c.halfSize, c.centre.y - c.halfSize, c.centre.z - c.halfSize };
                b.hi = { c.centre.x + c.halfSize, c.centre.y + c.halfSize, c.centre.z + c.halfSize };
                boxes.push_back(b);
            }
            r.cascades = unsigned(boxes.size());
            if (!gs.cascades.empty()) {
                r.outerHalf = gs.cascades.back().halfSize;
                r.cell = gs.cascades.back().cell;
                const float sz = 2.0f * r.outerHalf;
                r.reach = std::sqrt(3.0f) * sz;             // |voxelSize| of the outer cascade
            }
            // THE ENGINE'S DERIVATION (OgreScreenProbeGather.cpp `reach`): the
            // outer half extent under the far plane, 50 m with no cascades.
            r.maxT = std::min(cam.farClip > 0 ? cam.farClip : 1000.0f,
                              r.outerHalf > 0.0f ? r.outerHalf : 50.0f);
            const GatherStatus gst = gs.gather;
            r.stride = gst.stride; r.octRes = gst.octRes; r.gatherProbes = gst.probes;
            // THE RAY START, as the gather now makes it (the fix round): an
            // epsilon off the surface, never half a voxel.
            const float bias = 0.001f;

            unsigned tried = 0, dropped = 0;
            const std::vector<Probe> probes =
                placeProbes(s, cam, gst.stride ? gst.stride : 16u, tried, dropped);
            r.probes = unsigned(probes.size());
            r.st = traceProbeRays(s, probes, gst.octRes ? gst.octRes : 8u, r.maxT, bias, boxes);

            // ---- the cross-check, in this process, at this pose.
            Image derived, forced;
            arm(s, tier, 0.0f);      render(e, 4); view->readPixels(derived);
            arm(s, tier, r.maxT);    render(e, 4); view->readPixels(forced);
            r.maxTab = deltaOf(derived, forced);

            const double rays = double(r.st.rays ? r.st.rays : 1);
            std::printf("%-16s %-7s %4u %8.2f %8.2f %9.2f %7u %8llu %8.2f %8.4f %8.3f %8.2f\n",
                        r.scene.c_str(), r.tier.c_str(), r.cascades, double(r.outerHalf),
                        double(r.reach), double(r.maxT), r.probes,
                        (unsigned long long)r.st.rays, 100.0 * double(r.st.miss) / rays,
                        100.0 * double(r.st.endpointInAny) / rays,
                        100.0 * double(r.st.hitsLast10) / rays, r.st.meanHitT);
            std::printf("    gather on=%d running=%d err='%s'\n", int(gst.on), int(gst.running),
                        gst.error.c_str());
            std::printf("    probes: tier stride %u, octRes %u, gather's own probe count %u "
                        "(this tool placed %u of %u pixel blocks; %u dropped at edges)\n",
                        r.stride, r.octRes, r.gatherProbes, r.probes, tried, dropped);
            std::printf("    hit tHit deciles of maxT: ");
            for (int b = 0; b < 10; ++b)
                std::printf("%.2f%% ", 100.0 * double(r.st.bins[b]) / rays);
            std::printf("(max hit %.2f m)\n", r.st.maxHitT);
            std::printf("    A/B maxT(derived vs forced %.2f): %u/%u px moved\n",
                        double(r.maxT), r.maxTab.moved, r.maxTab.total);
            std::fflush(stdout);
            rows.push_back(r);
            e->destroyScene(s);   // the suites destroy the view's scene directly
        }
    }

    std::printf("\n== THE ANSWER ==\n");
    for (const Row &r : rows) {
        const double rays = double(r.st.rays ? r.st.rays : 1);
        std::printf("%-16s %-7s else-branch (the sky) %.2f%% of rays; of those %.4f%% end "
                    "INSIDE a cascade box (outer only: %.4f%%); derived-vs-forced maxT moved "
                    "%u px\n",
                    r.scene.c_str(), r.tier.c_str(), 100.0 * double(r.st.miss) / rays,
                    r.st.miss ? 100.0 * double(r.st.endpointInAny) / double(r.st.miss) : 0.0,
                    r.st.miss ? 100.0 * double(r.st.endpointInOuter) / double(r.st.miss) : 0.0,
                    r.maxTab.moved);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// THE FAR BLAS (lane ATOM-FARBLAS-1, `FARBLAS=1`; A5b §4). The gather's rays
// trace the NEAR copies to the outer half extent and, when they escape, the FAR
// copies (each mesh's coarsest level) on to the far plane. Two questions, per
// fixture and tier (`FARBLAS_TIER=High` picks one), in this process at this pose:
//   (a) THE PICTURE against the old long-ray reference — a NEAR-ONLY trace (the
//       fine geometry) to the outer box's diagonal (207.85 m at a 60 m half
//       extent), the far query off. The shipped arm (half + far) and the
//       pre-lane arm (half + sky) are each differenced against it; the far
//       field must land within the instrument's noise of the reference;
//   (b) THE COST: the gather's own GPU timestamps with the far query ON vs OFF
//       (and the reference), interleaved in 10 rounds of 36 frames with the
//       first six of each block dropped, on a 1920x1080 view — CLOCKS NOT
//       LOCKED, so the RATIO is the number, never a millisecond.
// Every picture is frozen; the instrument floor is the reference arm twice.
// THE FIXTURES' PRIMITIVES CARRY NO CHAIN (enginetest's unit cube), so here the
// coarse copy IS the fine one and (a) isolates the far QUERY; the coarse
// GEOMETRY's own effect is gi.far_blas's analytic case.
static void tuneFar(Scene *s, float rayLength, bool farOff)
{
    GatherTuning t;
    t.freezeFrameIndex = true;
    t.rayLength = rayLength;
    t.farQueryOff = farOff;
    s->setGatherTuning(t);
}

static int farBlasMain(Engine *e, View *view)
{
    View *big = e->createOffscreenView("farblas-cost", 1920u, 1080u, Colour(0, 0, 0));
    if (big) big->setOffscreenContract(OffscreenContract::StillPicture);   // a measured picture
    if (!big) { std::printf("FAIL: cost view: %s\n", e->lastError().c_str()); return 1; }
    big->setShadows(true);
    big->setEnabled(false);
    struct Fix { const char *name; CameraDesc (*build)(Scene *); };
    const Fix fixtures[2] = { { "open-sky", buildOpen }, { "showroom-shaped", buildShowroom } };
    const char *tierOnly = std::getenv("FARBLAS_TIER");
    std::printf("== FARBLAS-1: the far query, frozen frame, paired in one process ==\n");
    int bad = 0;
    for (const Fix &f : fixtures) {
        for (int ti = 0; ti < 3; ++ti) {
            const Tier &tier = kTiers[ti];
            if (tierOnly && std::string(tierOnly) != tier.name) continue;
            Scene *s = e->createScene(std::string("farblas-") + f.name + "-" + tier.name);
            if (!s) { std::printf("FAIL: scene\n"); return 1; }
            view->setEnabled(true);
            view->setScene(s);
            armChain(view, tier.ssrRow);
            const CameraDesc cam = f.build(s);
            view->setCamera(cam);
            s->setGlobalIllumination(giAt(tier));
            tuneFar(s, 0.0f, false);
            render(e, 90);
            const GiStatus gs = s->giStatus();
            const float outerHalf = gs.cascades.empty() ? 0.0f : gs.cascades.back().halfSize;
            const float diag = std::sqrt(3.0f) * 2.0f * outerHalf;
            std::printf("\n-- %s %s: outer half %.2f m, reference length %.2f m, far plane %.1f\n",
                        f.name, tier.name, double(outerHalf), double(diag), double(cam.farClip));

            // ---- (a) the pictures
            Image ship, nofar, ref, ref2;
            tuneFar(s, 0.0f, false); render(e, 4); view->readPixels(ship);
            tuneFar(s, 0.0f, true);  render(e, 4); view->readPixels(nofar);
            tuneFar(s, diag, true);  render(e, 4); view->readPixels(ref);
            tuneFar(s, diag, true);  render(e, 4); view->readPixels(ref2);
            const Delta floor = deltaOf(ref, ref2), dShip = deltaOf(ref, ship),
                        dNo = deltaOf(ref, nofar);
            std::printf("   instrument floor (reference twice) %u/%u px\n", floor.moved, floor.total);
            std::printf("   vs the near-only %.0f m reference: half+FAR %u px (mean %.2f, worst %u, "
                        "meanAll %.4f) | half+SKY %u px (mean %.2f, worst %u, meanAll %.4f)\n",
                        double(diag), dShip.moved, dShip.meanMoved, dShip.worst, dShip.meanAll,
                        dNo.moved, dNo.meanMoved, dNo.worst, dNo.meanAll);
            if (floor.moved) ++bad;

            // ---- (b) the cost
            view->setEnabled(false);
            big->setScene(s);
            armChain(big, tier.ssrRow);
            big->setCamera(cam);
            big->setEnabled(true);
            tuneFar(s, 0.0f, false);
            render(e, 120);
            std::vector<float> ms[3];
            for (int round = 0; round < 10; ++round)
                for (int a = 0; a < 3; ++a) {
                    if (a == 0) tuneFar(s, 0.0f, false);
                    else if (a == 1) tuneFar(s, 0.0f, true);
                    else tuneFar(s, diag, true);
                    for (int fr = 0; fr < 36; ++fr) {
                        e->renderOneFrame();
                        if (fr < 6) continue;
                        const GatherStatus q = s->giStatus().gather;
                        if (q.placeMs >= 0.0f && q.traceMs >= 0.0f && q.integrateMs >= 0.0f)
                            ms[a].push_back(q.placeMs + q.traceMs + q.integrateMs);
                    }
                }
            big->setEnabled(false);
            float med[3];
            for (int a = 0; a < 3; ++a) {
                std::vector<float> v = ms[a];
                std::sort(v.begin(), v.end());
                med[a] = v.empty() ? -1.0f : v[v.size() / 2];
            }
            std::printf("   gather GPU ms (median of %zu/%zu/%zu): far ON %.4f | far OFF %.4f | "
                        "near-only %.0f m %.4f -> ratio ON/OFF %.3f, ON/ref %.3f\n",
                        ms[0].size(), ms[1].size(), ms[2].size(), double(med[0]), double(med[1]),
                        double(diag), double(med[2]), med[1] > 0 ? double(med[0] / med[1]) : -1.0,
                        med[2] > 0 ? double(med[0] / med[2]) : -1.0);
            std::fflush(stdout);
            e->destroyScene(s);
        }
    }
    return bad ? 1 : 0;
}
