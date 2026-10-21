// jahlab — THE TEST ENVIRONMENTS (LAB-1 stage 2; SPECS/LAB_1_SPEC.md stage 2, the owner: "clean geometry whose right
// answer is known ... then Bistro as a final test at the end").
//
// THE LAW OF AN ENVIRONMENT: generated, deterministic (a seed), in metres at the default scene scale, with its ANALYTIC
// EXPECTATION beside it — the number the one-pixel rule predicts — so a lab run against it is a test of a LAW, not of
// agreement with a picture. Each environment is a generator in envs/<name>.cpp: build(seed) -> an iris::Scene (built in
// memory: no file, every mesh through the bake's own stages — MeshBake::buildLodChain / buildCards / buildSdf /
// buildClusterDag, the order and the calls of meshbake.cpp bakeStages), expect(view) -> the closed form at a camera,
// describe() -> the catalogue's text (`jahlab --env-list`).
//
// THE ONE-PIXEL RULE, as the cut states it (Types.h clusterCut; JahCullCut_cs.glsl jahClusterGroupAllowed): a view
// affords a group the error `allowed = tolerance x (distance / pixelScale)` metres, where pixelScale = H / (2 tan(fovY/2))
// pixels per metre at one metre (1303.7 at 1080 rows and the free camera's 45 degrees) and tolerance = the World Mode's
// atomPixelError (Epic 1, High 2, Medium 3, Low 4). A piece of geometry the bake can REMOVE at error e is therefore
// drawn while  e x pixelScale / distance >= tolerance  — the piece's error projects to at least `tolerance` pixels.
//
// WHAT "e" IS, from the bake's own metric (meshbake.h LodLevelTerms / clusterdag stats: the removed-vertex distance,
// ISLAND-CAPPED, and the dropped facets' distance): removing a separate shell moves the surface by the shell's distance
// to what remains beside it, never more than the shell's own extent. So a shell's e = min(its separation from the
// surface it stands on — a protrusion's depth, a free bar's stand-off — , its bounding-box diagonal); a whole object
// (nothing remains beside it: the DAG's empty top) has e = its bounding-box diagonal. Each generator writes its feature
// table with these e's (Feature below) and the closed form is the sum over it. What the closed form does NOT model is
// said per environment (a group of clusters merging pieces, the occlusion test's texel band, the sun maps).
#pragma once

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"

#include <QByteArray>
#include <QString>

#include <string>
#include <vector>

namespace lab {
namespace envs {

/// The camera and its optics at one dolly stop: what the closed form is evaluated for.
struct Optics
{
    iris::Vec3 eye, target;
    float fovYDegrees = 45.0f;
    float aspect = 16.0f / 9.0f;
    float heightPixels = 1080.0f;
    float tolerance = 1.0f;          ///< the World Mode's atomPixelError
    float farClip = 2000.0f;
    /// pixels per metre at one metre: H / (2 tan(fovY / 2))
    double pixelScale() const;
    /// the world-space error a view affords at `distance` metres (metres)
    double allowed(double distance) const { return double(tolerance) * distance / pixelScale(); }
    /// true when a sphere (centre, radius) reaches the view frustum
    bool inFrustum(const iris::Vec3 &centre, float radius) const;
};

/// One removable piece of an environment, in WORLD space (see the header: e is the bake's error of removing it).
struct Feature
{
    enum Kind {
        Shell,        ///< drawn whole (`triangles`) while e projects to >= tolerance pixels, else nothing
        Fixed,        ///< always drawn (a wall, a slab: nothing simplifies it away without a visible hole)
        CurvedFace,   ///< a face of `segments` authored chords over `theta` radians of radius `arcRadius` (2 tris each);
                      ///< `error` > 0 = a protruding band's depth: a chord drops where the depth is under the view's error
        Canopy,       ///< a soup of `cards` cards of size `cardSize` (2 tris each)
    };
    Kind kind = Shell;
    int object = -1;                 ///< the instance it belongs to (Built::objects)
    iris::Vec3 centre;
    float error = 0.0f;              ///< e, metres
    float radius = 0.0f;             ///< the bounding sphere's radius (the frustum test)
    unsigned triangles = 0;          ///< Shell / Fixed
    // CurvedFace: the arc's centre is `arcCentre`, angles [angle0, angle0 + theta] in the XZ plane from +X towards +Z
    iris::Vec3 arcCentre;
    float arcRadius = 0.0f, angle0 = 0.0f, theta = 0.0f, faceY = 0.0f;
    unsigned segments = 0;
    // Canopy
    unsigned cards = 0;
    float cardSize = 0.0f;
};

/// One drawable instance (a MeshNode) of an environment: its node's name and the role the measurement sums by.
struct Object
{
    QString name;
    QString role;                    ///< "subject" (measured), "ground", "wall", "building", "prop", ...
    iris::Vec3 boxMin, boxMax;       ///< world bounds
};

/// What one build made.
struct Built
{
    iris::ScenePtr doc;
    std::vector<Feature> features;
    std::vector<Object> objects;
    QByteArray hash;                 ///< sha256 over every mesh's arrays, every instance's transform and material
    unsigned long long triangles = 0;   ///< level-0 triangles over every instance
    unsigned meshes = 0, instances = 0;
    double buildMs = 0.0, bakeMs = 0.0;
};

/// One predicted quantity at a stop.
struct Prediction
{
    std::string quantity;            ///< "triangles", "objects", "visibleObjects", "hiddenDrawn@4", ...
    double value = 0.0;
    std::string note;
};

/// What the lab's probe measured at a stop, per object name (the measurement side of a Prediction).
struct ObjectReading
{
    QString name;
    unsigned long long drawnTriangles = 0;
    unsigned pixels = 0, drawnClusters = 0;
};

struct Env
{
    const char *name = nullptr;
    /// the catalogue's text: what it is, what it predicts, the derivation in one paragraph
    std::string (*describe)() = nullptr;
    /// `bake`: run the bake's stages on every mesh (false = the geometry and its hash only: the determinism check)
    Built (*build)(unsigned seed, bool bake) = nullptr;
    /// the dolly's camera at stop `d` metres (eye, target) — the far clip the stop needs is the env's
    void (*stop)(double d, iris::Vec3 &eye, iris::Vec3 &target) = nullptr;
    /// the closed form at the camera
    std::vector<Prediction> (*expect)(const Built &b, const Optics &o) = nullptr;
    float farClip = 2000.0f;
    bool atom = true;                ///< an Atom environment (dollied); false = Photon's (its expectation is the law's)
};

const std::vector<Env> &catalogue();
const Env *find(const std::string &name);
/// The dolly's stops (SPECS/LAB_1_SPEC.md: the flythrough's law stops, the smoke CAM's distance x 1, 4, 8, 16).
const std::vector<double> &stops();
/// The measured value of `quantity` from the probe's per-object readings (sums over the objects whose role the
/// quantity names; see each env's expect for which).
double measured(const Built &b, const std::string &quantity, const std::vector<ObjectReading> &readings);
/// The standard closed form: the sum over the feature table (Shell / Fixed / CurvedFace / Canopy), frustum-culled.
double predictedTriangles(const Built &b, const Optics &o, const QString &role = QString());

}   // namespace envs
}   // namespace lab
