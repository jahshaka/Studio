#ifndef BONEOVERLAY_H
#define BONEOVERLAY_H

// BoneOverlay — a skeleton drawn as engine geometry, outside the mirrored tree.
//
// GizmoOverlay's shape exactly (AVATAR_MODULE_SPEC §0.6 D0.3 A): a small set of
// cached unit meshes, an unlit material, and a pool of engine nodes that are
// only ever given a new transform. Nothing is created or destroyed per frame —
// which matters here more than for gizmos: engine meshes are BT_IMMUTABLE, so
// "rebuild the skeleton each frame" would mean destroyMesh + createMesh per
// frame, and destroyMesh calls invalidateGiCaches() (§0.5.2).
//
// THE LOOK (Unreal's classic bone viz): a bone is an OCTAHEDRON — a stretched
// double pyramid running parent joint -> child joint, its ring 15% along from
// the parent end — and every joint carries a small octahedral marker. Both are
// ONE authored unit mesh each, shaped per bone purely by a transform
// (translate to the parent joint, rotate +Y onto the bone, scale
// (girth, length, girth)) — the light-wires pattern, no engine additions.
// Girth follows bone length and is clamped against the rig's limb scale, so
// finger bones stay visible slivers and a long spine bone does not swallow the
// character.
//
// Bones with no child (leaf bones) get a stub in the bone's OWN axis. Which
// axis that is was measured, not assumed: on the synthetic fixture and on a
// Mixamo character (Ely, 67 bones) the direction from a bone to its child is
// the bone's local +Y for 64 of 66 bones (the two exceptions are the hips'
// splayed leg children), and a leaf's own local +Y is within 7 degrees of the
// direction it came from for 13 of 15 leaves (the eyes, which branch sideways
// off the head, are the outliers — which is exactly why the stub uses the
// leaf's own axis and not the incoming direction).
//
// DEPTH: bones are ordinary depth-tested geometry — solid shapes need to
// occlude each other, and with the mesh visible the skeleton is inside the
// character and reads as hidden. An X-ray mode (skeleton through the mesh, the
// pre-3D-bones behaviour) is FUTURE WORK: it needs more than a depth-test flag
// to look right (bones would have to depth-test against each other but not
// against the mesh), i.e. an engine-side render-queue/depth-priority
// addition. Not built here.
//
// Geometry is WORLD-space, so the overlay's nodes hang off the engine scene
// root — the same space SceneMirror puts the document's top-level nodes in.
// Deliberately knows nothing about avatars: the caller hands it segments, so
// the editor viewport can reuse it (Part 1+) and Part 3's prop-onto-a-bone UI
// can share the joint markers.
//
// Studio code: iris + the engine abstraction, never Ogre.
#include <QColor>
#include <QVector>
#include <QVector3D>
#include "jahshaka/engine/Engine.h"

/// One bone: parent joint -> child joint, plus what the overlay needs to draw
/// the child end when the child has no children of its own.
struct BoneOverlaySegment
{
    QVector3D from;                 ///< the parent joint (the octahedron's wide end)
    QVector3D to;                   ///< the child joint (the octahedron's point)
    QVector3D tipAxis;              ///< world-space bone axis of the CHILD bone (its own
                                    ///< local +Y). Null falls back to the bone's direction.
    bool      tipIsLeaf = false;    ///< the child bone has no bone children: draw a stub
};

class BoneOverlay
{
public:
    explicit BoneOverlay(jahshaka::engine::Scene *target);
    ~BoneOverlay();

    /// Bone and marker colour (one unlit material for both). Cheap per frame.
    void setColour(const QColor &colour);
    /// Joint markers as a fraction of the rig's limb scale (the 75th-percentile
    /// bone length — see boneoverlay.cpp). 0 turns them off.
    void setMarkerScale(float fraction) { mMarkerScale = fraction; }

    /// Pushes this frame's skeleton. `visible == false` (or no segments) hides
    /// every slot without destroying anything.
    void update(const QVector<BoneOverlaySegment> &segments, bool visible);
    /// Destroys every node, mesh and material this overlay made.
    void clear();

    /// Bones drawn from the caller's segments (leaf stubs are NOT counted here).
    int visibleSegments() const { return mVisibleSegments; }
    /// Extra octahedra drawn past leaf joints.
    int visibleStubs() const { return mVisibleStubs; }
    /// Joint markers drawn (one per distinct joint position).
    int visibleJoints() const { return mVisibleJoints; }

private:
    bool ensureAssets();
    /// Grows `pool` as needed and returns the node for `index`, attaching `mesh`
    /// the first time. 0 when the engine refuses.
    jahshaka::engine::NodeId slot(QVector<jahshaka::engine::NodeId> &pool, int index,
                                  jahshaka::engine::MeshId mesh);
    static void hideFrom(jahshaka::engine::Scene *scene,
                         const QVector<jahshaka::engine::NodeId> &pool, int first);

    jahshaka::engine::Scene *mTarget = nullptr;
    jahshaka::engine::MeshId mBoneMesh = 0;      ///< unit octahedron, (0,0,0) -> (0,1,0)
    jahshaka::engine::MeshId mMarkerMesh = 0;    ///< unit octahedron centred on the origin
    jahshaka::engine::MaterialId mMaterial = 0;
    QVector<jahshaka::engine::NodeId> mBoneNodes;    ///< segments, then leaf stubs
    QVector<jahshaka::engine::NodeId> mJointNodes;
    QColor mColour = QColor(60, 255, 90);
    float mMarkerScale = 0.13f;
    int mVisibleSegments = 0;
    int mVisibleStubs = 0;
    int mVisibleJoints = 0;
};

#endif // BONEOVERLAY_H
