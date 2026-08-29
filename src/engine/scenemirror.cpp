#include "scenemirror.h"

#include <QQuaternion>
#include <QVector3D>
#include <cstring>
#include <algorithm>
#include <cmath>

#include "scenegraph/scene.h"
#include "scenegraph/scenenode.h"
#include "scenegraph/meshnode.h"
#include "scenegraph/lightnode.h"
#include "scenegraph/cameranode.h"
#include "graphics/mesh.h"
#include "graphics/vertexlayout.h"
#include "graphics/graphicsdevice.h"   // VertexBuffer / IndexBuffer (CPU copies)
#include "graphics/material.h"
#include "materials/pbrmaterial.h"
#include "materials/defaultmaterial.h"
#include <QtMath>

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
    for (const Entry &e : mEntries)
        if (e.node) mTarget->removeNode(e.node);
    mEntries.clear();
    for (MeshId m : mMeshes) mTarget->destroyMesh(m);
    mMeshes.clear();
    for (MaterialId m : mMaterials) mTarget->destroyMaterial(m);
    mMaterials.clear();
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

    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto meshNode = node.staticCast<iris::MeshNode>();
        iris::Mesh *mesh = meshNode->getMesh().data();
        iris::Material *material = meshNode->getMaterial().data();
        if (mesh && (!e.hasMesh || e.materialPtr != material)) {
            MeshId m = meshFor(mesh);
            MaterialId mat = materialFor(material);
            if (m && mat && mTarget->attachMesh(e.node, m, mat)) {
                e.hasMesh = true; e.material = mat; e.materialPtr = material;
            }
        } else if (e.hasMesh && e.material && material) {
            // Parameters may change every frame from the property panel: push them.
            PbrParams p;
            if (toPbrParams(material, p)) mTarget->setPbrMaterial(e.material, p);
        }
    }

    if (node->getSceneNodeType() == iris::SceneNodeType::Light) {
        // The light rides on the mirrored node: position and direction follow the document.
        auto light = node.staticCast<iris::LightNode>();
        if (mTarget->setLight(e.node, toLightDesc(light.data()))) e.hasLight = true;
    }

    for (auto &child : node->children)
        visit(child, e.node, seen);
}

void SceneMirror::removeMissing(const QSet<long> &seen)
{
    for (auto it = mEntries.begin(); it != mEntries.end();) {
        if (seen.contains(it.key())) { ++it; continue; }
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

MaterialId SceneMirror::materialFor(iris::Material *material)
{
    PbrParams p;
    if (!material || !toPbrParams(material, p)) {
        // Unknown material kinds (CustomMaterial shader graphs, matcap, glass...) get
        // one shared neutral material until they have an engine equivalent.
        if (!mDefaultMaterial) {
            PbrParams d; d.albedo = Colour(0.8f, 0.8f, 0.8f); d.metalness = 0.0f; d.roughness = 0.6f;
            mDefaultMaterial = mTarget->createPbrMaterial(d);
        }
        return mDefaultMaterial;
    }
    auto it = mMaterials.constFind(material);
    if (it != mMaterials.constEnd()) return it.value();
    MaterialId id = mTarget->createPbrMaterial(p);
    if (id) mMaterials.insert(material, id);
    return id;
}

bool SceneMirror::toPbrParams(iris::Material *material, PbrParams &out)
{
    if (!material) return false;
    if (auto *pbr = dynamic_cast<iris::PbrMaterial *>(material)) {
        const QColor c = pbr->baseColor;
        const float f = pbr->baseColorFactor;
        out.albedo    = Colour(c.redF() * f, c.greenF() * f, c.blueF() * f, 1.0f);
        out.metalness = pbr->metallicFactor;
        out.roughness = pbr->roughnessFactor;
        const QColor e = pbr->emissiveColor;
        out.emissive  = Colour(e.redF() * pbr->emissiveIntensity, e.greenF() * pbr->emissiveIntensity,
                               e.blueF() * pbr->emissiveIntensity, 1.0f);
        return true;
    }
    if (auto *def = dynamic_cast<iris::DefaultMaterial *>(material)) {
        // Legacy Blinn-Phong material: diffuse -> albedo, shininess -> roughness.
        const QColor c = def->getDiffuseColor();
        out.albedo    = Colour(c.redF(), c.greenF(), c.blueF(), 1.0f);
        out.metalness = 0.0f;
        const float shin = std::max(0.0f, std::min(def->getShininess(), 128.0f));
        out.roughness = 1.0f - std::sqrt(shin / 128.0f) * 0.9f;
        out.emissive  = Colour(0, 0, 0);
        return true;
    }
    return false;
}

LightDesc SceneMirror::toLightDesc(iris::LightNode *light)
{
    LightDesc d;
    switch (light->lightType) {
    case iris::LightType::Directional: d.type = LightType::Directional; break;
    case iris::LightType::Spot:        d.type = LightType::Spot; break;
    case iris::LightType::Point: default: d.type = LightType::Point; break;
    }
    d.colour = Colour(light->color.redF(), light->color.greenF(), light->color.blueF(), 1.0f);
    d.intensity = light->intensity;
    d.range = light->distance;
    d.spotAngleDegrees = light->spotCutOff;
    d.spotSoftness = light->spotCutOffSoftness;
    return d;
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
    CameraDesc c;
    c.position     = toVec3(camera->getGlobalPosition());
    c.orientation  = toQuat(camera->getGlobalRotation());
    c.fovDegrees   = camera->angle > 0.0f ? camera->angle : 45.0f;
    c.nearClip     = camera->nearClip;
    c.farClip      = camera->farClip;
    c.orthographic = !camera->isPerspective;
    c.orthoSize    = camera->orthoSize;
    view->setCamera(c);
}
