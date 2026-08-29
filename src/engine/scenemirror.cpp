#include "scenemirror.h"

#include <QQuaternion>
#include <QMatrix3x3>
#include <QMatrix4x4>
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
#include "materials/custommaterial.h"
#include "core/property.h"
#include "graphics/texture2d.h"
#include "graphics/shadowmap.h"
#include <QFileInfo>
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
    for (const Entry &e : mEntries) {
        if (e.wireNode) mTarget->removeNode(e.wireNode);
        if (e.wireMaterial) mTarget->destroyMaterial(e.wireMaterial);
        if (e.node) mTarget->removeNode(e.node);
    }
    mEntries.clear();
    for (MeshId &m : mWireMeshes) { if (m) mTarget->destroyMesh(m); m = 0; }
    mHighlighted.clear();
    if (mHighlightNode) { mTarget->removeNode(mHighlightNode); mHighlightNode = 0; }
    if (mHighlightMaterial) { mTarget->destroyMaterial(mHighlightMaterial); mHighlightMaterial = 0; }
    mHighlightMesh = 0;
    for (MeshId m : mMeshes) mTarget->destroyMesh(m);
    mMeshes.clear();
    for (MaterialId m : mMaterials) mTarget->destroyMaterial(m);
    mMaterials.clear();
    for (TextureId t : mTextures) mTarget->destroyTexture(t);
    mTextures.clear();
    mTarget->setSky(SkyMode::NoSky, 0);
    for (TextureId &t : mSkyFaceTextures) { if (t) mTarget->destroyTexture(t); t = 0; }
    mSkySignature.clear();
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
    reclaimUnused();
    syncHighlight();
    return seen.size();
}

MeshId SceneMirror::engineMesh(iris::Mesh *mesh) const
{
    auto it = mMeshes.constFind(mesh);
    return it == mMeshes.constEnd() ? 0 : it.value();
}

void SceneMirror::pushTransform(Scene *scene, NodeId node, const QMatrix4x4 &t)
{
    const QVector3D cx = t.column(0).toVector3D(), cy = t.column(1).toVector3D(), cz = t.column(2).toVector3D();
    const QVector3D scale(cx.length(), cy.length(), cz.length());
    const QVector3D pos = t.column(3).toVector3D();
    const float sx = scale.x() > 1e-8f ? scale.x() : 1.0f, sy = scale.y() > 1e-8f ? scale.y() : 1.0f, sz = scale.z() > 1e-8f ? scale.z() : 1.0f;
    float m[9] = { cx.x() / sx, cy.x() / sy, cz.x() / sz,
                   cx.y() / sx, cy.y() / sy, cz.y() / sz,
                   cx.z() / sx, cy.z() / sy, cz.z() / sz };
    const QQuaternion rot = QQuaternion::fromRotationMatrix(QMatrix3x3(m));
    scene->setNodeTransform(node, Vec3(pos.x(), pos.y(), pos.z()),
                            Quat(rot.x(), rot.y(), rot.z(), rot.scalar()),
                            Vec3(scale.x(), scale.y(), scale.z()));
}

// ---- selection highlight -------------------------------------------------------

void SceneMirror::setHighlightedNode(iris::SceneNodePtr node)
{
    mHighlighted = node;
}

void SceneMirror::syncHighlight()
{
    iris::MeshNode *meshNode = (mHighlighted && mHighlighted->getSceneNodeType() == iris::SceneNodeType::Mesh)
                                   ? static_cast<iris::MeshNode *>(mHighlighted.data()) : nullptr;
    iris::Mesh *mesh = meshNode ? meshNode->getMesh().data() : nullptr;
    MeshId m = mesh ? engineMesh(mesh) : 0;
    if (!m) {
        if (mHighlightNode) mTarget->setNodeVisible(mHighlightNode, false);
        mHighlightMesh = 0;
        return;
    }
    if (!mHighlightMaterial)
        mHighlightMaterial = mTarget->createUnlitMaterial(Colour(1.0f, 0.85f, 0.1f), false, true);   // on top, wireframe
    if (!mHighlightNode) mHighlightNode = mTarget->createNode();
    if (!mHighlightNode || !mHighlightMaterial) return;
    if (mHighlightMesh != m) {
        if (mTarget->attachMesh(mHighlightNode, m, mHighlightMaterial)) mHighlightMesh = m;
    }
    pushTransform(mTarget, mHighlightNode, meshNode->globalTransform);
    mTarget->setNodeVisible(mHighlightNode, true);
}

// ---- light wires ---------------------------------------------------------------

void SceneMirror::setLightWires(bool on)
{
    mLightWires = on;
}

MeshId SceneMirror::wireMeshFor(int kind)
{
    if (kind < 0 || kind > 2) return 0;
    if (mWireMeshes[kind]) return mWireMeshes[kind];
    std::vector<Vec3> pts;
    auto circle = [&](int axis, float r) {
        const int n = 24;
        for (int i = 0; i < n; ++i) {
            const float a0 = float(i) / n * 6.2831853f, a1 = float(i + 1) / n * 6.2831853f;
            const float c0 = std::cos(a0) * r, s0 = std::sin(a0) * r, c1 = std::cos(a1) * r, s1 = std::sin(a1) * r;
            if (axis == 0)      { pts.push_back(Vec3(0, c0, s0)); pts.push_back(Vec3(0, c1, s1)); }
            else if (axis == 1) { pts.push_back(Vec3(c0, 0, s0)); pts.push_back(Vec3(c1, 0, s1)); }
            else                { pts.push_back(Vec3(c0, s0, 0)); pts.push_back(Vec3(c1, s1, 0)); }
        }
    };
    if (kind == 1) {                       // point: three rings
        circle(0, 0.5f); circle(1, 0.5f); circle(2, 0.5f);
    } else {                               // directional / spot: an arrow down -Y (+ a cone for spot),
                                           // matching the light direction convention (document -Y)
        pts.push_back(Vec3(0, 0, 0)); pts.push_back(Vec3(0, -1.5f, 0));
        for (int i = 0; i < 4; ++i) {
            const float a = float(i) / 4 * 6.2831853f;
            pts.push_back(Vec3(0, -1.5f, 0)); pts.push_back(Vec3(std::cos(a) * 0.15f, -1.2f, std::sin(a) * 0.15f));
        }
        if (kind == 2) { const float r = 0.6f;   // spot cone
            for (int i = 0; i < 8; ++i) {
                const float a0 = float(i) / 8 * 6.2831853f, a1 = float(i + 1) / 8 * 6.2831853f;
                pts.push_back(Vec3(std::cos(a0) * r, -1.5f, std::sin(a0) * r)); pts.push_back(Vec3(std::cos(a1) * r, -1.5f, std::sin(a1) * r));
                if (i % 2 == 0) { pts.push_back(Vec3(0, 0, 0)); pts.push_back(Vec3(std::cos(a0) * r, -1.5f, std::sin(a0) * r)); }
            }
        }
    }
    mWireMeshes[kind] = mTarget->createLineMesh(pts, false);
    return mWireMeshes[kind];
}

void SceneMirror::syncLightWires(Entry &e, iris::LightNode *light)
{
    if (!mLightWires) {
        if (e.wireNode) mTarget->setNodeVisible(e.wireNode, false);
        return;
    }
    int kind = 1;
    if (light->lightType == iris::LightType::Directional) kind = 0;
    else if (light->lightType == iris::LightType::Spot) kind = 2;
    MeshId m = wireMeshFor(kind);
    if (!m) return;
    if (!e.wireNode) e.wireNode = mTarget->createNode(e.node);
    if (!e.wireMaterial) e.wireMaterial = mTarget->createUnlitMaterial(Colour(1, 1, 1), false);
    if (!e.wireNode || !e.wireMaterial) return;
    if (e.wireKind != kind) { if (mTarget->attachMesh(e.wireNode, m, e.wireMaterial)) e.wireKind = kind; }
    const QColor c = light->color;
    mTarget->setUnlitMaterial(e.wireMaterial, Colour(c.redF(), c.greenF(), c.blueF(), 1.0f));
    // Wires live in the light node's local space; undo the node's own scale.
    const QVector3D s = light->getLocalScale();
    mTarget->setNodeTransform(e.wireNode, Vec3(), Quat(),
                              Vec3(s.x() > 1e-6f ? 1.0f / s.x() : 1.0f, s.y() > 1e-6f ? 1.0f / s.y() : 1.0f, s.z() > 1e-6f ? 1.0f / s.z() : 1.0f));
    mTarget->setNodeVisible(e.wireNode, true);
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
                e.hasMesh = true; e.material = mat; e.materialPtr = material; e.mesh = m; e.meshPtr = mesh;
                e.textureSignature.clear();
                syncTextures(e, material);
            }
        } else if (e.hasMesh && e.material && material) {
            // Parameters may change every frame from the property panel: push them.
            PbrParams p;
            if (toPbrParams(material, p)) mTarget->setPbrMaterial(e.material, p);
            syncTextures(e, material);
        }
    }

    if (node->getSceneNodeType() == iris::SceneNodeType::Light) {
        // The light rides on the mirrored node: position and direction follow the document.
        auto light = node.staticCast<iris::LightNode>();
        if (mTarget->setLight(e.node, toLightDesc(light.data()))) e.hasLight = true;
        syncLightWires(e, light.data());
    }

    // `e` is a reference into a QHash: the recursion inserts entries and QHash does not
    // keep value references stable across inserts (use-after-free under ASan). Copy first.
    const NodeId self = e.node;
    for (auto &child : node->children)
        visit(child, self, seen);
}

void SceneMirror::reclaimUnused()
{
    QSet<MeshId> usedMeshes; QSet<MaterialId> usedMaterials;
    for (const Entry &e : mEntries) { if (e.mesh) usedMeshes.insert(e.mesh); if (e.material) usedMaterials.insert(e.material); }
    if (mHighlightMesh) usedMeshes.insert(mHighlightMesh);
    for (auto it = mMeshes.begin(); it != mMeshes.end();) {
        if (usedMeshes.contains(it.value())) { ++it; continue; }
        mTarget->destroyMesh(it.value()); it = mMeshes.erase(it);
    }
    for (auto it = mMaterials.begin(); it != mMaterials.end();) {
        if (usedMaterials.contains(it.value())) { ++it; continue; }
        mTarget->destroyMaterial(it.value()); it = mMaterials.erase(it);
    }
}

void SceneMirror::removeMissing(const QSet<long> &seen)
{
    for (auto it = mEntries.begin(); it != mEntries.end();) {
        if (seen.contains(it.key())) { ++it; continue; }
        if (it->wireNode) mTarget->removeNode(it->wireNode);
        if (it->wireMaterial) mTarget->destroyMaterial(it->wireMaterial);
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

TextureId SceneMirror::textureFor(const QString &path, bool srgb)
{
    auto it = mTextures.constFind(path);
    if (it != mTextures.constEnd()) return it.value();
    if (!QFileInfo::exists(path)) return 0;      // Qt resources (":/...") are not files the engine can read
    TextureId id = mTarget->loadTexture(path.toStdString(), srgb);
    if (id) mTextures.insert(path, id);
    return id;
}

void SceneMirror::syncTextures(Entry &e, iris::Material *material)
{
    if (!material || !e.material || e.material == mDefaultMaterial) return;
    // Document slot name -> engine slot. PbrMaterial and DefaultMaterial naming.
    // PbrMaterial's "u_occlusionMap" is deliberately NOT mapped: the engine has no
    // ambient-occlusion slot (HlmsPbs limitation, see engine Types.h).
    struct Slot { const char *name; PbrTextureSlot slot; bool srgb; };
    static const Slot kSlots[] = {
        { "u_baseColorMap",  PbrTextureSlot::Albedo,    true  }, { "u_diffuseTexture", PbrTextureSlot::Albedo,    true  },
        { "u_normalMap",     PbrTextureSlot::Normal,    false }, { "u_normalTexture",  PbrTextureSlot::Normal,    false },
        { "u_metallicMap",   PbrTextureSlot::Metalness, false }, { "u_roughnessMap",   PbrTextureSlot::Roughness, false },
        { "u_emissiveMap",   PbrTextureSlot::Emissive,  true  },
    };
    // Resolve every candidate path first: the textures map (Texture2D::source), then
    // shader-graph texture properties (a file path in the property value).
    struct Bind { PbrTextureSlot slot; QString path; bool srgb; };
    QVector<Bind> binds;
    for (const Slot &sl : kSlots) {
        auto it = material->textures.constFind(sl.name);
        if (it != material->textures.constEnd() && it.value() && !it.value()->source.isEmpty())
            binds.append({ sl.slot, it.value()->source, sl.srgb });
    }
    if (auto *custom = dynamic_cast<iris::CustomMaterial *>(material)) {
        for (iris::Property *prop : custom->properties) {
            if (!prop || prop->type != iris::PropertyType::Texture) continue;
            const QString path = prop->getValue().toString();
            if (path.isEmpty()) continue;
            if (prop->name == "diffuseTexture" || prop->name == "baseColorMap" || prop->name == "albedoMap")
                binds.append({ PbrTextureSlot::Albedo, path, true });
            else if (prop->name == "normalTexture" || prop->name == "normalMap")
                binds.append({ PbrTextureSlot::Normal, path, false });
            else if (prop->name == "emissiveMap")
                binds.append({ PbrTextureSlot::Emissive, path, true });
        }
    }
    QString signature;
    for (const Bind &b : binds) signature += QString::number(int(b.slot)) + '=' + b.path + ';';
    if (signature == e.textureSignature) return;
    e.textureSignature = signature;
    bool bound[5] = { false, false, false, false, false };
    for (const Bind &b : binds) {
        if (bound[int(b.slot)]) continue;
        TextureId t = textureFor(b.path, b.srgb);
        if (t && mTarget->setPbrTexture(e.material, b.slot, t)) bound[int(b.slot)] = true;
    }
    for (int i = 0; i < 5; ++i) if (!bound[i]) mTarget->setPbrTexture(e.material, PbrTextureSlot(i), 0);
}

bool SceneMirror::toPbrParams(iris::Material *material, PbrParams &out)
{
    if (!material) return false;
    if (auto *pbr = dynamic_cast<iris::PbrMaterial *>(material)) {
        const QColor c = pbr->baseColor;
        const float f = pbr->baseColorFactor;
        out.albedo    = Colour(c.redF() * f, c.greenF() * f, c.blueF() * f, 1.0f);
        out.metalness = pbr->metallicFactor;
        // The document's roughness remap bounds apply per-texel to a sampled map;
        // the engine has no such remap, so approximate by clamping the scalar
        // factor into the (order-normalised) bounds.
        const float lo = std::min(pbr->roughnessLowerBound, pbr->roughnessUpperBound);
        const float hi = std::max(pbr->roughnessLowerBound, pbr->roughnessUpperBound);
        out.roughness = std::max(lo, std::min(pbr->roughnessFactor, hi));
        const QColor e = pbr->emissiveColor;
        out.emissive  = Colour(e.redF() * pbr->emissiveIntensity, e.greenF() * pbr->emissiveIntensity,
                               e.blueF() * pbr->emissiveIntensity, 1.0f);
        switch (pbr->alphaMode) {
        case 1:  out.alphaMode = PbrAlphaMode::Cutout; break;
        case 2:  out.alphaMode = PbrAlphaMode::Blend;  break;
        default: out.alphaMode = PbrAlphaMode::Opaque; break;
        }
        out.alpha           = pbr->alpha;
        out.alphaCutoff     = pbr->alphaCutoff;
        out.normalMapWeight = pbr->normalFactor;
        out.twoSided        = pbr->renderStates.rasterState.cullMode == iris::CullMode::None;
        // occlusionMap/occlusionFactor: no engine equivalent (HlmsPbs has no AO
        // slot — see Types.h); intentionally dropped, not faked.
        return true;
    }
    if (auto *custom = dynamic_cast<iris::CustomMaterial *>(material)) {
        // Effects-module materials (Default/Flat/... .shader): read the properties the
        // shader graph exposes. Colour → albedo, shininess → roughness. Textures are
        // bound by syncTextures() from the texture properties.
        bool haveColour = false; float shininess = 20.0f;
        out.albedo = Colour(0.8f, 0.8f, 0.8f); out.metalness = 0.0f; out.emissive = Colour(0, 0, 0);
        for (iris::Property *prop : custom->properties) {
            if (!prop) continue;
            const QVariant v = prop->getValue();
            if (prop->type == iris::PropertyType::Color &&
                (prop->name == "diffuseColor" || prop->name == "color" || prop->name == "albedo" || prop->name == "baseColor")) {
                const QColor c = v.value<QColor>();
                out.albedo = Colour(c.redF(), c.greenF(), c.blueF(), 1.0f); haveColour = true;
            } else if (prop->type == iris::PropertyType::Float && prop->name == "shininess") {
                shininess = v.toFloat();
            } else if (prop->type == iris::PropertyType::Float && (prop->name == "roughness" || prop->name == "roughnessFactor")) {
                out.roughness = v.toFloat();
            } else if (prop->type == iris::PropertyType::Float && (prop->name == "metallic" || prop->name == "metalness")) {
                out.metalness = v.toFloat();
            }
        }
        const float shin = std::max(0.0f, std::min(shininess, 128.0f));
        out.roughness = 1.0f - std::sqrt(shin / 128.0f) * 0.9f;
        (void)haveColour;
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
    d.castShadows = light->shadowMap && light->shadowMap->shadowType != iris::ShadowMapType::None;
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

void SceneMirror::applySky(View *view)
{
    if (!mSource || !view) return;
    QString signature;
    if (mSource->skyType == iris::SkyType::EQUIRECTANGULAR && mSource->skyTexture)
        signature = "equirect:" + mSource->skyTexture->source;
    else if (mSource->skyType == iris::SkyType::CUBEMAP && mSource->skyTexture && mSource->skyTexture->isCubeMap())
        signature = "cubemap:" + QString::number(reinterpret_cast<quintptr>(mSource->skyTexture.data()));
    if (signature != mSkySignature) {
        mSkySignature = signature;
        for (TextureId &t : mSkyFaceTextures) { if (t) mTarget->destroyTexture(t); t = 0; }
        if (signature.startsWith("equirect:")) {
            TextureId t = textureFor(mSource->skyTexture->source, true);
            mTarget->setSky(t ? SkyMode::Equirectangular : SkyMode::NoSky, t);
        } else if (signature.startsWith("cubemap:")) {
            // The document keeps the six face images (+X,-X,+Y,-Y,+Z,-Z); upload them.
            const QImage *faces = mSource->skyTexture->cubeFaces();
            bool ok = faces != nullptr;
            for (int i = 0; ok && i < 6; ++i) {
                const QImage img = faces[i].convertToFormat(QImage::Format_RGBA8888);
                if (img.isNull()) { ok = false; break; }
                mSkyFaceTextures[i] = mTarget->createTexture(unsigned(img.width()), unsigned(img.height()), img.constBits(), true);
                if (!mSkyFaceTextures[i]) ok = false;
            }
            if (ok) mTarget->setSkyCubemap(mSkyFaceTextures); else mTarget->setSky(SkyMode::NoSky, 0);
        } else {
            mTarget->setSky(SkyMode::NoSky, 0);
        }
    }
    if (mSource->skyType == iris::SkyType::SINGLE_COLOR) {
        const QColor c = mSource->skyColor;
        view->setBackground(Colour(c.redF(), c.greenF(), c.blueF(), 1.0f));
    }
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
