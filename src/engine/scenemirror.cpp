#include "scenemirror.h"

#include <QQuaternion>
#include <QVector3D>
#include <cstring>

#include "scenegraph/scene.h"
#include "scenegraph/scenenode.h"
#include "scenegraph/meshnode.h"
#include "scenegraph/lightnode.h"
#include "scenegraph/cameranode.h"
#include "graphics/mesh.h"
#include "graphics/vertexlayout.h"
#include "graphics/graphicsdevice.h"   // VertexBuffer / IndexBuffer (CPU copies)
#include "graphics/material.h"

using namespace jahshaka::engine;

namespace {
inline Vec3 toVec3(const QVector3D &v) { return Vec3(v.x(), v.y(), v.z()); }
inline Quat toQuat(const QQuaternion &q) { return Quat(q.x(), q.y(), q.z(), q.scalar()); }
}

SceneMirror::SceneMirror(Scene *target) : mTarget(target) {}

SceneMirror::~SceneMirror()
{
    // The engine scene may already be gone (Engine destroyed first); only touch it
    // if the caller kept the documented order. Entries are cheap to drop.
}

void SceneMirror::setSource(iris::ScenePtr scene)
{
    for (const Entry &e : mEntries) {
        if (e.light) mTarget->removeNode(e.light);
        if (e.node)  mTarget->removeNode(e.node);
    }
    mEntries.clear();
    for (MeshId m : mMeshes) mTarget->destroyMesh(m);
    mMeshes.clear();
    mSource = scene;
}

NodeId SceneMirror::engineNode(const iris::SceneNode *node) const
{
    if (!node) return 0;
    auto it = mEntries.constFind(node->nodeId);
    return it == mEntries.constEnd() ? 0 : it->node;
}

int SceneMirror::sync()
{
    if (!mSource || !mSource->getRootNode()) return 0;
    // Refresh the document's global transforms (lights and cameras read them).
    mSource->getRootNode()->update(0.0f);

    QSet<long> seen;
    for (auto &child : mSource->getRootNode()->children)
        visit(child, 0, seen);
    removeMissing(seen);
    return seen.size();
}

void SceneMirror::visit(iris::SceneNodePtr node, NodeId parent, QSet<long> &seen)
{
    if (!node) return;
    seen.insert(node->nodeId);

    Entry &e = mEntries[node->nodeId];
    if (!e.node) {
        e.node = mTarget->createNode(parent);
        if (!e.node) return;
    } else {
        // Parent may have changed in the document (drag in the hierarchy widget).
        mTarget->setNodeParent(e.node, parent);
    }

    mTarget->setNodeTransform(e.node, toVec3(node->getLocalPos()), toQuat(node->getLocalRot()), toVec3(node->getLocalScale()));
    mTarget->setNodeVisible(e.node, node->visible);

    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh && !e.hasMesh) {
        auto meshNode = node.staticCast<iris::MeshNode>();
        iris::Mesh *mesh = meshNode->getMesh().data();
        if (mesh) {
            MeshId m = meshFor(mesh);
            MaterialId mat = materialFor(meshNode->getMaterial().data());
            if (m && mat && mTarget->attachMesh(e.node, m, mat)) e.hasMesh = true;
        }
    }

    if (node->getSceneNodeType() == iris::SceneNodeType::Light && !e.light) {
        // v0: every document light becomes a directional light along the node's forward.
        // Point/spot mapping and per-frame light sync arrive in plan step 5.
        auto light = node.staticCast<iris::LightNode>();
        const QVector3D fwd = node->getGlobalRotation().rotatedVector(QVector3D(0, 0, -1));
        e.light = mTarget->addDirectionalLight(toVec3(fwd), light->intensity * 3.14159f);
    }

    for (auto &child : node->children)
        visit(child, e.node, seen);
}

void SceneMirror::removeMissing(const QSet<long> &seen)
{
    for (auto it = mEntries.begin(); it != mEntries.end();) {
        if (seen.contains(it.key())) { ++it; continue; }
        if (it->light) mTarget->removeNode(it->light);
        if (it->node)  mTarget->removeNode(it->node);
        it = mEntries.erase(it);
    }
}

MeshId SceneMirror::meshFor(iris::Mesh *mesh)
{
    auto it = mMeshes.constFind(mesh);
    if (it != mMeshes.constEnd()) return it.value();
    MeshData data;
    if (!toMeshData(mesh, data)) return 0;
    MeshId id = mTarget->createMesh(data);
    if (id) mMeshes.insert(mesh, id);
    return id;
}

MaterialId SceneMirror::materialFor(iris::Material *)
{
    // v0: one shared default material. Plan step 4 maps PbrMaterial/DefaultMaterial.
    if (!mDefaultMaterial) {
        PbrParams p; p.albedo = Colour(0.8f, 0.3f, 0.2f); p.metalness = 0.0f; p.roughness = 0.6f;
        mDefaultMaterial = mTarget->createPbrMaterial(p);
    }
    return mDefaultMaterial;
}

bool SceneMirror::toMeshData(iris::Mesh *mesh, MeshData &out)
{
    if (!mesh) return false;
    out = MeshData();
    for (const auto &vb : mesh->getVertexBuffers()) {
        if (!vb || !vb->data) continue;
        const QList<iris::VertexAttribute> attribs = vb->vertexLayout.getAttribs();
        if (attribs.isEmpty()) continue;
        const iris::VertexAttribute &attr = attribs.first();
        const float *f = reinterpret_cast<const float *>(vb->data);
        const int floats = vb->dataSize / int(sizeof(float));
        switch (attr.usage) {
        case iris::VertexAttribUsage::Position:
            out.positions.assign(f, f + floats); break;
        case iris::VertexAttribUsage::Normal:
            out.normals.assign(f, f + floats); break;
        case iris::VertexAttribUsage::TexCoord0: {
            // assimp stores texcoords as 3 floats; the engine wants 2.
            const int comps = attr.count > 0 ? attr.count : 3;
            for (int i = 0; i + comps <= floats; i += comps) { out.uvs.push_back(f[i]); out.uvs.push_back(f[i+1]); }
            break;
        }
        default: break;
        }
    }
    if (out.positions.empty()) return false;
    const size_t nv = out.positions.size() / 3;
    const iris::IndexBufferPtr ib = mesh->getIndexBuffer();
    if (ib && ib->data && ib->dataSize > 0) {
        const unsigned *idx = reinterpret_cast<const unsigned *>(ib->data);
        out.indices.assign(idx, idx + ib->dataSize / int(sizeof(unsigned)));
    } else {
        out.indices.resize(nv);
        for (size_t i = 0; i < nv; ++i) out.indices[i] = unsigned(i);
    }
    if (out.normals.size() != out.positions.size()) out.normals.clear();
    if (out.uvs.size() != nv * 2) out.uvs.clear();
    return out.indices.size() >= 3;
}

void SceneMirror::applyCamera(iris::CameraNodePtr camera, View *view)
{
    if (!camera || !view) return;
    camera->update(0.0f);
    const QVector3D pos = camera->getGlobalPosition();
    const QVector3D fwd = camera->getGlobalRotation().rotatedVector(QVector3D(0, 0, -1));
    view->setCameraPosition(toVec3(pos));
    view->lookAt(toVec3(pos + fwd));
}
