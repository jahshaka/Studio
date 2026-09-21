#pragma once
// Test-side stand-ins for the legacy convenience verbs the engine boundary
// dropped (IRISGL_ARCHITECTURE_AUDIT §4.2: addTestCube/addDirectionalLight/
// setNodePosition/setNodeScale/setCameraPosition/lookAt were selftest-era
// surface with no Studio callers). The suites keep their call shapes; the
// helpers speak only the real verbs, so the tests now pin the production path.
//
// Pixel-compat notes:
//  - the cube geometry and PBR params are byte-identical to the deleted
//    Scene::addTestCube (buildCubeV2's 24-vertex unit cube: positions +
//    per-face normals, no uvs — and createPbrMaterial applies exactly the
//    workflow/fog/albedo/metal/rough sequence addTestCube inlined);
//  - testCameraAt reproduces setCameraPosition-without-lookAt (identity
//    orientation, i.e. looking down -Z, default 45° vertical fov);
//  - testCameraLookAt reproduces setCameraPosition+lookAt (+Y up), and
//    testCameraDescLookAt hands back the same CameraDesc unpushed, for a
//    suite that needs to vary one field of it (projection, clips, lens).

#include <cmath>
#include <map>
#include <vector>

#include "jahshaka/engine/Engine.h"
#include "jahshaka/engine/Types.h"

namespace enginetest {

using namespace jahshaka::engine;

/// The deleted addTestCube's exact geometry (OgreEngine buildCubeV2): a unit
/// cube, 24 vertices, positions + per-face normals, no uvs.
inline MeshData unitCubeMesh()
{
    MeshData d;
    const float h = 0.5f;
    const float fn[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    const float fv[6][4][3] = {
        {{-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}},
        {{ h,-h,-h},{-h,-h,-h},{-h, h,-h},{ h, h,-h}},
        {{ h,-h, h},{ h,-h,-h},{ h, h,-h},{ h, h, h}},
        {{-h,-h,-h},{-h,-h, h},{-h, h, h},{-h, h,-h}},
        {{-h, h, h},{ h, h, h},{ h, h,-h},{-h, h,-h}},
        {{-h,-h,-h},{ h,-h,-h},{ h,-h, h},{-h,-h, h}} };
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < 4; ++v) {
            d.positions.insert(d.positions.end(), { fv[f][v][0], fv[f][v][1], fv[f][v][2] });
            d.normals.insert(d.normals.end(), { fn[f][0], fn[f][1], fn[f][2] });
        }
        const unsigned b = unsigned(f * 4);
        d.indices.insert(d.indices.end(), { b, b + 1, b + 2, b, b + 2, b + 3 });
    }
    return d;
}

/// Per-node transform state so position/scale/rotation helpers compose the way
/// the deleted verbs did (each verb overwrote only its component).
struct NodePose { Vec3 pos{0,0,0}; Quat rot; Vec3 scale{1,1,1}; };   // Quat defaults to identity
inline std::map<const Scene *, std::map<NodeId, NodePose>> &poseRegistry()
{
    static std::map<const Scene *, std::map<NodeId, NodePose>> reg;
    return reg;
}

/// addTestCube(albedo, metalness, roughness), rebuilt on the real verbs.
inline NodeId addTestCube(Scene *s, const Colour &albedo, float metalness, float roughness)
{
    const NodeId node = s->createNode();
    if (!node) return 0;
    const MeshId mesh = s->createMesh(unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.metalness = metalness;
    p.roughness = roughness;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    // Start from a clean pose: the registry is keyed by Scene*, and a destroyed
    // Engine's Scene address can be reused by the next one (test_engine_recreate),
    // which would otherwise leave a stale transform under the same NodeId.
    poseRegistry()[s][node] = NodePose{};
    return node;
}

/// addDirectionalLight(direction, power): a node whose -Y is aimed along
/// `direction`, carrying a directional light (the engine's light convention).
inline NodeId addDirectionalLight(Scene *s, const Vec3 &direction, float power)
{
    const NodeId node = s->createNode();
    if (!node) return 0;
    // Rotate -Y onto `direction`.
    const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    Vec3 d{ direction.x / len, direction.y / len, direction.z / len };
    const Vec3 from{ 0.f, -1.f, 0.f };
    const Vec3 cross{ from.y * d.z - from.z * d.y, from.z * d.x - from.x * d.z, from.x * d.y - from.y * d.x };
    const float dot = from.x * d.x + from.y * d.y + from.z * d.z;
    Quat q;
    if (dot < -0.999999f) { q = Quat{ 0.f, 0.f, 1.f, 0.f }; }         // 180° about Z
    else {
        const float w = 1.f + dot;
        const float n = std::sqrt(w * w + cross.x * cross.x + cross.y * cross.y + cross.z * cross.z);
        q = Quat{ cross.x / n, cross.y / n, cross.z / n, w / n };      // Quat is (x,y,z,w)
    }
    s->setNodeTransform(node, Vec3{0,0,0}, q, Vec3{1,1,1});
    LightDesc l;
    l.type = LightType::Directional;
    l.colour = Colour(1.f, 1.f, 1.f);
    // setLight applies powerScale = intensity * pi; the deleted verb applied
    // `power` as the scale directly.
    l.intensity = power / 3.14159265358979323846f;
    if (!s->setLight(node, l)) return 0;
    poseRegistry()[s][node] = NodePose{ Vec3{0,0,0}, q, Vec3{1,1,1} };
    return node;
}

inline void setNodePosition(Scene *s, NodeId n, const Vec3 &pos)
{
    NodePose &p = poseRegistry()[s][n];
    p.pos = pos;
    s->setNodeTransform(n, p.pos, p.rot, p.scale);
}

inline void setNodeScale(Scene *s, NodeId n, const Vec3 &scale)
{
    NodePose &p = poseRegistry()[s][n];
    p.scale = scale;
    s->setNodeTransform(n, p.pos, p.rot, p.scale);
}

/// setCameraPosition without lookAt: identity orientation (looking down -Z),
/// engine-default projection — exactly what the deleted View verb left behind.
inline void testCameraAt(View *v, const Vec3 &pos)
{
    CameraDesc c;
    c.position = pos;               // orientation stays identity: looking down -Z
    v->setCamera(c);
}

/// setCameraPosition + lookAt(target), +Y up.
/// The DESCRIPTION testCameraLookAt pushes, so a suite that needs a variant of
/// it (an ORTHOGRAPHIC camera at the same pose, tests/ssr's ortho fixture) can
/// build one without a second copy of the quaternion.
inline CameraDesc testCameraDescLookAt(const Vec3 &pos, const Vec3 &target)
{
    const Vec3 f0{ target.x - pos.x, target.y - pos.y, target.z - pos.z };
    const float fl = std::sqrt(f0.x * f0.x + f0.y * f0.y + f0.z * f0.z);
    const Vec3 f{ f0.x / fl, f0.y / fl, f0.z / fl };          // forward (camera -Z)
    const Vec3 upW{ 0.f, 1.f, 0.f };
    Vec3 r{ f.y * upW.z - f.z * upW.y, f.z * upW.x - f.x * upW.z, f.x * upW.y - f.y * upW.x }; // right = f x up
    const float rl = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
    r = Vec3{ r.x / rl, r.y / rl, r.z / rl };
    const Vec3 u{ r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x };       // up = r x f
    // Rotation matrix columns: x=r, y=u, z=-f  ->  quaternion.
    const float m00 = r.x, m01 = u.x, m02 = -f.x;
    const float m10 = r.y, m11 = u.y, m12 = -f.y;
    const float m20 = r.z, m21 = u.z, m22 = -f.z;
    const float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0.f) {
        const float sq = std::sqrt(tr + 1.f) * 2.f;
        q = Quat{ (m21 - m12) / sq, (m02 - m20) / sq, (m10 - m01) / sq, 0.25f * sq };
    } else if (m00 > m11 && m00 > m22) {
        const float sq = std::sqrt(1.f + m00 - m11 - m22) * 2.f;
        q = Quat{ 0.25f * sq, (m01 + m10) / sq, (m02 + m20) / sq, (m21 - m12) / sq };
    } else if (m11 > m22) {
        const float sq = std::sqrt(1.f + m11 - m00 - m22) * 2.f;
        q = Quat{ (m01 + m10) / sq, 0.25f * sq, (m12 + m21) / sq, (m02 - m20) / sq };
    } else {
        const float sq = std::sqrt(1.f + m22 - m00 - m11) * 2.f;
        q = Quat{ (m02 + m20) / sq, (m12 + m21) / sq, 0.25f * sq, (m10 - m01) / sq };
    }
    CameraDesc c;
    c.position = pos;
    c.orientation = q;
    return c;
}

inline void testCameraLookAt(View *v, const Vec3 &pos, const Vec3 &target)
{
    v->setCamera(testCameraDescLookAt(pos, target));
}

// ---------------------------------------------------------------------------
// THE LEAK ROOM — ONE fixture, two suites (GATHER-0 fix round, B1).
//
// A sealed 10 m room with one lamp INSIDE it (pure green, so the red channel
// is 100 % the other lamp) and one OUTSIDE it (red, a metre off the -Z wall).
// Shadows on, the direct term through the wall is exactly zero at every
// thickness, so every red pixel inside the room arrived through global
// illumination and that number IS the leak.
//
// It lived in tests/gi/test_gi_leak_room.cpp and was copied verbatim into
// gi.gather, which is one fixture too many: the two suites compare their
// numbers against each other (the gather's field arm reproduces gi.leak_room's
// chain arm to within 0.7 %) and that comparison is only worth anything while
// the rooms are the SAME room. It is here so they cannot drift.
namespace leakroom {

/// One axis-aligned slab of the shell.
inline NodeId addSlab(Scene *s, const Colour &albedo, const Vec3 &pos, const Vec3 &scale)
{
    const NodeId node = s->createNode();
    const MeshId mesh = s->createMesh(unitCubeMesh());
    PbrParams p;
    p.albedo = albedo;
    p.metalness = 0.0f;
    p.roughness = 0.9f;
    const MaterialId mat = s->createPbrMaterial(p);
    if (!node || !mesh || !mat || !s->attachMesh(node, mesh, mat)) return 0;
    s->setNodeTransform(node, pos, Quat(), scale);
    return node;
}

/// What `build` hands back: the two lamps, and the outside one's description
/// so a caller can darken and relight it (which is how the leak is isolated).
struct Room {
    NodeId inside = 0;
    NodeId outside = 0;
    LightDesc outsideLight;
};

/// The room at wall thickness `T`, its two lamps, and the camera at the pose
/// both suites measure from. The interior (x,z in [-5,5], y in [0,4]) is
/// identical at every thickness — only the barrier's depth in voxels changes.
inline Room build(Scene *s, View *view, float T)
{
    s->setAmbient(Colour(0, 0, 0), Colour(0, 0, 0));
    const Colour white(0.8f, 0.8f, 0.8f);
    const float ho = 5.0f + T * 0.5f;
    const float span = 10.0f + 2.0f * T;
    addSlab(s, white, Vec3(0, -T * 0.5f, 0), Vec3(span, T, span));            // floor
    addSlab(s, white, Vec3(0, 4.0f + T * 0.5f, 0), Vec3(span, T, span));      // ceiling
    addSlab(s, white, Vec3(0, 2, -ho), Vec3(span, 4.0f, T));                  // -Z: THE wall
    addSlab(s, white, Vec3(0, 2,  ho), Vec3(span, 4.0f, T));                  // +Z
    addSlab(s, white, Vec3(-ho, 2, 0), Vec3(T, 4.0f, span));                  // -X
    addSlab(s, white, Vec3( ho, 2, 0), Vec3(T, 4.0f, span));                  // +X

    Room r;
    // The inside lamp: PURE GREEN, so the red channel is entirely the outside
    // lamp's and no threshold has to separate them.
    r.inside = s->createNode();
    s->setNodeTransform(r.inside, Vec3(0.0f, 3.0f, 2.5f), Quat(), Vec3(1, 1, 1));
    LightDesc li;
    li.type = LightType::Point;
    li.colour = Colour(0.0f, 1.0f, 0.0f);
    li.intensity = 3.0f;
    li.range = 14.0f;
    li.castShadows = true;
    s->setLight(r.inside, li);

    // The outside lamp: RED, a metre beyond the outer face of the -Z wall.
    r.outside = s->createNode();
    s->setNodeTransform(r.outside, Vec3(0.0f, 2.0f, -(ho + T * 0.5f + 1.0f)), Quat(), Vec3(1, 1, 1));
    r.outsideLight.type = LightType::Point;
    r.outsideLight.colour = Colour(1.0f, 0.0f, 0.0f);
    r.outsideLight.intensity = 25.0f;
    r.outsideLight.range = 12.0f;
    r.outsideLight.castShadows = true;
    s->setLight(r.outside, r.outsideLight);

    // The camera looks at the inner face of the -Z wall from THREE METRES
    // away, off to +X so nothing else is in the shot. Three metres, and not
    // the original spike's six, for one reason: under a chain the irradiance
    // field rides cascade 0 — a 10 m box centred on the camera — and the wall
    // being measured has to be inside it, or the suite grades the ring.
    if (view) testCameraLookAt(view, Vec3(3.6f, 2.0f, -2.0f), Vec3(3.6f, 2.0f, -5.0f));
    return r;
}

/// The lamp on or off, for the two halves of one leak reading.
inline void setOutsideIntensity(Scene *s, Room &r, float intensity)
{
    r.outsideLight.intensity = intensity;
    s->setLight(r.outside, r.outsideLight);
}

/// Mean red and green of the centred block both suites measure — expressed as
/// FRACTIONS of the image so the two may use different resolutions.
inline void meanRG(const Image &img, float &r, float &g)
{
    double sr = 0.0, sg = 0.0; int n = 0;
    const unsigned x0 = img.width * 5u / 16u, x1 = img.width * 11u / 16u;
    const unsigned y0 = img.height * 5u / 16u, y1 = img.height * 11u / 16u;
    for (unsigned y = y0; y < y1; ++y)
        for (unsigned x = x0; x < x1; ++x) { const Colour c = img.at(x, y); sr += c.r; sg += c.g; ++n; }
    r = float(n ? sr / n : 0.0); g = float(n ? sg / n : 0.0);
}

}  // namespace leakroom

}  // namespace enginetest
