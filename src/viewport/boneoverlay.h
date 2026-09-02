#ifndef BONEOVERLAY_H
#define BONEOVERLAY_H

// BoneOverlay — a skeleton drawn as engine lines, outside the mirrored tree.
//
// GizmoOverlay's shape exactly (AVATAR_MODULE_SPEC §0.6 D0.3 A): ONE cached
// unit segment mesh (0,0,0) -> (0,1,0) and ONE cached joint-marker mesh, an
// unlit depth-test-off material, and a pool of engine nodes that are only ever
// given a new transform. Nothing is created or destroyed per frame — which
// matters here more than for gizmos: createLineMesh makes a BT_IMMUTABLE
// buffer that updateMeshVertices refuses (OgreMesh.cpp), so "rebuild the line
// list each frame" would mean destroyMesh + createLineMesh per frame, and
// destroyMesh calls invalidateGiCaches() (§0.5.2).
//
// Segments are WORLD-space, so the overlay's nodes hang off the engine scene
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

struct BoneOverlaySegment
{
    QVector3D from;
    QVector3D to;
};

class BoneOverlay
{
public:
    explicit BoneOverlay(jahshaka::engine::Scene *target);
    ~BoneOverlay();

    /// Line and marker colour (one unlit material for both). Cheap per frame.
    void setColour(const QColor &colour);
    /// Joint markers as a fraction of the median segment length. 0 turns them
    /// off; they exist because a 1-px line is a thin pixel target offscreen.
    void setMarkerScale(float fraction) { mMarkerScale = fraction; }

    /// Pushes this frame's skeleton. `visible == false` (or no segments) hides
    /// every slot without destroying anything.
    void update(const QVector<BoneOverlaySegment> &segments, bool visible);
    /// Destroys every node, mesh and material this overlay made.
    void clear();

    int visibleSegments() const { return mVisibleSegments; }

private:
    struct Slot
    {
        jahshaka::engine::NodeId line = 0;
        jahshaka::engine::NodeId marker = 0;
        bool shown = false;
    };
    bool ensureAssets();

    jahshaka::engine::Scene *mTarget = nullptr;
    jahshaka::engine::MeshId mLineMesh = 0;
    jahshaka::engine::MeshId mMarkerMesh = 0;
    jahshaka::engine::MaterialId mMaterial = 0;
    QVector<Slot> mSlots;
    QColor mColour = QColor(60, 255, 90);
    float mMarkerScale = 0.16f;
    int mVisibleSegments = 0;
};

#endif // BONEOVERLAY_H
