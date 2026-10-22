// jahlab environments — the shared builder (LAB-1 stage 2): geometry arrays, the bake's stages on an in-memory mesh,
// the instances, the materials, the scene hash, the world every environment stands in. Internal to src/lab/envs.
#pragma once

#include "env.h"

#include "irisgl/core/math/quat.h"

#include <QCryptographicHash>

#include <cstdint>
#include <vector>

namespace lab {
namespace envs {

/// Plain arrays, the shape every generator writes (tests/meshbake meshFromArrays' input): positions xyz, normals xyz,
/// uvs uv, triangle indices (counter-clockwise seen from outside — Ogre's front face).
struct Arrays
{
    std::vector<float> p, n, uv;
    std::vector<unsigned> idx;
    unsigned triangles() const { return unsigned(idx.size() / 3); }
    /// a closed axis-aligned box (12 triangles, 24 vertices: each face its own normal), outward winding
    void box(const iris::Vec3 &centre, const iris::Vec3 &half);
    /// a closed box along the orthonormal axes u, v, w (half extents h along each), outward winding
    void orientedBox(const iris::Vec3 &centre, const iris::Vec3 &u, const iris::Vec3 &v, const iris::Vec3 &w,
                     const iris::Vec3 &h);
    /// one quad (2 triangles) with corners a, b, c, d counter-clockwise seen from the side `normal` points to
    void quad(const iris::Vec3 &a, const iris::Vec3 &b, const iris::Vec3 &c, const iris::Vec3 &d, const iris::Vec3 &normal);
};

/// The deterministic generator every environment draws from (tests/meshbake's fixtures' LCG: seed * 1664525 +
/// 1013904223, 24 bits of it as [0, 1)).
struct Lcg
{
    explicit Lcg(unsigned s) : seed(s) {}
    unsigned seed;
    float next() { seed = seed * 1664525u + 1013904223u; return float(seed >> 8) / float(1u << 24); }
    float range(float lo, float hi) { return lo + (hi - lo) * next(); }
    int irange(int lo, int hi) { return lo + int(next() * float(hi - lo + 1)) % (hi - lo + 1); }
};

/// tests/meshbake/test_mesh_bake.cpp cardSoup() (:2310), COPIED (its TU's includes are Qt + assimp + SQL) and
/// parametrised: `cards` two-triangle cards of side 2 x `half`, scattered and turned inside a sphere of `radius`.
void cardSoup(Arrays &a, unsigned cards, float half, float radius, const iris::Vec3 &centre, unsigned seed);

class Builder
{
public:
    Builder(const char *env, unsigned seed, bool bake);
    /// tests/meshbake meshFromArrays (:2285), COPIED: the arrays as an iris::Mesh (its bounds set), then — when the build
    /// bakes — the bake's stages in meshbake.cpp bakeStages' order. Hashed.
    iris::MeshPtr mesh(const Arrays &a);
    iris::PbrMaterialPtr material(float r, float g, float b, float roughness = 0.9f);
    /// One instance: a MeshNode of `mesh` at pos / yaw (degrees about +Y) / scale, recorded as an Object. Returns its
    /// index in Built::objects.
    int instance(const iris::MeshPtr &m, const QString &name, const QString &role, const iris::Vec3 &pos,
                 float yawDegrees, const iris::Vec3 &scale, const iris::PbrMaterialPtr &mat,
                 bool castsShadow = true, bool twoSided = false);
    void feature(const Feature &f) { mBuilt.features.push_back(f); }
    /// the world (the Basic template's sky and lights, Epic), the static defaults, the hash; the build's time
    Built finish();
    Built &built() { return mBuilt; }

private:
    QCryptographicHash mHash;
    QCryptographicHash mBakeHash;
    Built mBuilt;
    bool mBake;
    double mStartMs;
};

/// THE EMPTY TOP'S ERROR for a whole object (env.h): the largest WELDED island's bounding-box diagonal — vertices
/// welded by position, triangles joined through shared vertices — of these arrays, unscaled.
float largestIslandDiagonal(const Arrays &a);
/// a unit box mesh (x, z in [-0.5, 0.5], y in [0, 1]) for scaled instances
iris::MeshPtr unitBox(Builder &b);
/// an instance of `unit` filling the world box [mn, mx], role "subject", recorded as a Fixed 12-triangle feature
int fixedBox(Builder &b, const iris::MeshPtr &unit, const QString &name, const iris::Vec3 &mn, const iris::Vec3 &mx,
             const iris::PbrMaterialPtr &mat);
/// world-space bounds of an axis-aligned box after yaw + scale + translation
void boxBounds(const iris::Vec3 &centre, const iris::Vec3 &half, const iris::Vec3 &pos, float yawDegrees,
               const iris::Vec3 &scale, iris::Vec3 &mn, iris::Vec3 &mx);
/// a local point through yaw + scale + translation
iris::Vec3 place(const iris::Vec3 &local, const iris::Vec3 &pos, float yawDegrees, const iris::Vec3 &scale);

}   // namespace envs
}   // namespace lab
