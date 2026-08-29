#ifndef SCENEMIRROR_H
#define SCENEMIRROR_H

// SceneMirror — pushes the iris:: scene DOCUMENT into an engine::Scene.
//
// This is the seam decided in VIEWPORT_MIGRATION_PLAN.md: Studio keeps
// iris::Scene/SceneNode/MeshNode/LightNode as its document model (the property
// panels, hierarchy widget, undo commands, reader/writer all talk to it) and the
// engine renders a mirror of it. Every frame sync() walks the document, creates
// engine nodes for new document nodes, removes engine nodes for vanished ones,
// and pushes local transforms and visibility. Meshes are converted once from the
// document's CPU vertex buffers and cached per iris::Mesh.
//
// Studio-side code: includes iris (Qt) and the engine abstraction. Never Ogre.
#include <QHash>
#include <QMatrix4x4>
#include <QSet>
#include "irisglfwd.h"
#include "jahshaka/engine/Engine.h"

namespace iris { class Mesh; class Material; }

class SceneMirror
{
public:
    explicit SceneMirror(jahshaka::engine::Scene *target);
    ~SceneMirror();

    /// Replaces the mirrored document. Clears everything previously mirrored.
    void setSource(iris::ScenePtr scene);
    iris::ScenePtr source() const { return mSource; }

    /// Brings the engine scene up to date with the document. Call once per frame
    /// before Engine::renderOneFrame(). Returns the number of document nodes mirrored.
    int sync();

    /// The engine node mirroring a document node, or 0.
    jahshaka::engine::NodeId engineNode(const iris::SceneNode *node) const;

    /// Points `view`'s camera where the document camera is looking.
    void applyCamera(iris::CameraNodePtr camera, jahshaka::engine::View *view);

    /// Converts a document mesh to engine MeshData. Public so importers and tests
    /// can use the same conversion. Returns false if the mesh has no geometry.
    static bool toMeshData(iris::Mesh *mesh, jahshaka::engine::MeshData &out);
    /// Pushes a world matrix onto an engine node as TRS (used by overlays too).
    static void pushTransform(jahshaka::engine::Scene *scene, jahshaka::engine::NodeId node, const QMatrix4x4 &world);
    /// The engine mesh already created for a document mesh, or 0.
    jahshaka::engine::MeshId engineMesh(iris::Mesh *mesh) const;

    /// Selection highlight: the node's mesh drawn again as an on-top wireframe.
    void setHighlightedNode(iris::SceneNodePtr node);
    /// Light wires: a small shape at every document light (colour = the light's).
    void setLightWires(bool on);
    bool lightWires() const { return mLightWires; }

private:
    struct Entry {
        jahshaka::engine::NodeId node = 0;
        bool hasMesh  = false;
        bool hasLight = false;
        jahshaka::engine::MaterialId material = 0;   // per document material instance
        iris::Material *materialPtr = nullptr;
        jahshaka::engine::NodeId wireNode = 0;       // light wire shape, child of `node`
        jahshaka::engine::MaterialId wireMaterial = 0;
        int wireKind = -1;                           // which shape is attached
    };
    void syncLightWires(Entry &e, iris::LightNode *light);
    void syncHighlight();
    jahshaka::engine::MeshId wireMeshFor(int kind);
    void visit(iris::SceneNodePtr node, jahshaka::engine::NodeId parent, QSet<long> &seen);
    void removeMissing(const QSet<long> &seen);
    jahshaka::engine::MeshId     meshFor(iris::Mesh *mesh);
    jahshaka::engine::MaterialId materialFor(iris::Material *material);
    /// Reads a document material into PBR parameters. Public for tests.
public:
    static bool toPbrParams(iris::Material *material, jahshaka::engine::PbrParams &out);
    static jahshaka::engine::LightDesc toLightDesc(iris::LightNode *light);
private:

    jahshaka::engine::Scene *mTarget;
    iris::ScenePtr           mSource;
    QHash<long, Entry>       mEntries;         // keyed by iris SceneNode::nodeId
    QHash<iris::Mesh *, jahshaka::engine::MeshId> mMeshes;
    QHash<iris::Material *, jahshaka::engine::MaterialId> mMaterials;
    jahshaka::engine::MaterialId mDefaultMaterial = 0;
    bool mLightWires = true;
    jahshaka::engine::MeshId mWireMeshes[3] = { 0, 0, 0 };   // directional, point, spot
    iris::SceneNodePtr mHighlighted;
    jahshaka::engine::NodeId mHighlightNode = 0;
    jahshaka::engine::MaterialId mHighlightMaterial = 0;
    jahshaka::engine::MeshId mHighlightMesh = 0;
};

#endif // SCENEMIRROR_H
