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
#include "scenegraph/particlesystemnode.h"
#include "graphics/particle.h"
#include "graphics/mesh.h"
#include "graphics/skeleton.h"
#include "graphics/vertexlayout.h"
#include "graphics/vertexbuffer.h"     // VertexBuffer / IndexBuffer (CPU copies)
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
    for (HighlightShell &s : mHighlightShells) if (s.node) mTarget->removeNode(s.node);
    mHighlightShells.clear();
    if (mHighlightMaterial) { mTarget->destroyMaterial(mHighlightMaterial); mHighlightMaterial = 0; }
    for (MeshId m : mMeshes) mTarget->destroyMesh(m);
    mMeshes.clear();
    mSkins.clear();
    for (MaterialId m : mMaterials) mTarget->destroyMaterial(m);
    mMaterials.clear();
    for (TextureId t : mTextures) mTarget->destroyTexture(t);
    mTextures.clear();
    for (TextureId t : mIconTextures) mTarget->destroyTexture(t);
    mIconTextures.clear();
    mTarget->setSky(SkyMode::NoSky, 0);   // also clears the engine's reflection cubemap
    for (TextureId &t : mSkyFaceTextures)  { if (t) mTarget->destroyTexture(t); t = 0; }
    for (TextureId &t : mReflFaceTextures) { if (t) mTarget->destroyTexture(t); t = 0; }
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
    mAnyShadowCaster = false;
    mShadowFilter = ShadowFilter::Hard;
    mMaxShadowResolution = 0;
    for (auto &child : mSource->getRootNode()->children)
        visit(child, 0, seen);
    removeMissing(seen);
    reclaimUnused();
    syncSkinnedMeshes();
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

void SceneMirror::setHighlightWireframe(bool on)
{
    mHighlightWireframe = on;
}

void SceneMirror::collectHighlightMeshes(const iris::SceneNodePtr &node,
                                         std::vector<std::pair<iris::MeshNode *, MeshId>> &out)
{
    if (!node || !node->isVisible()) return;
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto meshNode = static_cast<iris::MeshNode *>(node.data());
        if (iris::Mesh *mesh = meshNode->getMesh().data())
            if (MeshId m = engineMesh(mesh)) out.emplace_back(meshNode, m);
    }
    for (auto &child : node->children) collectHighlightMeshes(child, out);
}

void SceneMirror::syncHighlight()
{
    // Every mesh under the highlighted node, the node itself included: selecting
    // an asset's ROOT (or any group) outlines the whole asset, not just one part.
    std::vector<std::pair<iris::MeshNode *, MeshId>> targets;
    if (mHighlighted) collectHighlightMeshes(mHighlighted, targets);
    if (targets.empty()) {
        for (HighlightShell &s : mHighlightShells) { if (s.node) mTarget->setNodeVisible(s.node, false); s.mesh = 0; }
        return;
    }
    // The user's outline colour preference lives on the document
    // (scene->outlineColor, filled from Preferences by MainWindow::
    // updateSceneSettings — legacy reads it the same way). Fall back to the
    // historical selection yellow when the document never got one.
    const QColor pref = mSource ? mSource->outlineColor : QColor();
    const Colour kSelection = pref.isValid()
        ? Colour(float(pref.redF()), float(pref.greenF()), float(pref.blueF()))
        : Colour(1.0f, 0.85f, 0.1f);
    MaterialId mat;
    if (mHighlightWireframe) {
        if (!mHighlightMaterial)
            mHighlightMaterial = mTarget->createUnlitMaterial(kSelection, false, true);   // on top, wireframe
        mat = mHighlightMaterial;
    } else {
        if (!mOutlineMaterial)
            mOutlineMaterial = mTarget->createOutlineMaterial(kSelection);
        mat = mOutlineMaterial;
    }
    // Live colour changes (preference edited with a selection active): both
    // highlight materials are unlit, so one setter updates each in place.
    if (pref != mHighlightColourApplied) {
        mHighlightColourApplied = pref;
        if (mHighlightMaterial) mTarget->setUnlitMaterial(mHighlightMaterial, kSelection);
        if (mOutlineMaterial)   mTarget->setUnlitMaterial(mOutlineMaterial, kSelection);
    }
    if (!mat) return;
    // One pooled shell per target mesh; extra shells from a previous (larger)
    // selection are hidden, not destroyed.
    if (mHighlightShells.size() < targets.size()) mHighlightShells.resize(targets.size());
    for (size_t i = 0; i < targets.size(); ++i) {
        iris::MeshNode *meshNode = targets[i].first;
        const MeshId m = targets[i].second;
        HighlightShell &s = mHighlightShells[i];
        if (!s.node) s.node = mTarget->createNode();
        if (!s.node) continue;
        if (s.mesh != m || s.wireframe != mHighlightWireframe) {
            if (mTarget->attachMesh(s.node, m, mat)) {
                s.mesh = m;
                s.wireframe = mHighlightWireframe;
            }
        }
        // The outline is the same mesh scaled up slightly around the node's pivot:
        // only the band where the shell pokes out past the original is visible.
        QMatrix4x4 t = meshNode->globalTransform;
        if (!mHighlightWireframe) t.scale(1.04f);
        pushTransform(mTarget, s.node, t);
        mTarget->setNodeVisible(s.node, true);
    }
    for (size_t i = targets.size(); i < mHighlightShells.size(); ++i) {
        HighlightShell &s = mHighlightShells[i];
        if (s.node) mTarget->setNodeVisible(s.node, false);
        s.mesh = 0;
    }
}

// ---- light wires ---------------------------------------------------------------

void SceneMirror::setLightWires(bool on)
{
    mLightWires = on;
}

MeshId SceneMirror::wireMeshFor(int kind)
{
    if (kind < 0 || kind > 3) return 0;
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
    } else if (kind == 3) {                // area: unit rectangle in XZ + a short normal tick
                                           // down -Y (the emit direction, like the arrow)
        const Vec3 c0(-0.5f, 0, -0.5f), c1(0.5f, 0, -0.5f), c2(0.5f, 0, 0.5f), c3(-0.5f, 0, 0.5f);
        pts.push_back(c0); pts.push_back(c1);
        pts.push_back(c1); pts.push_back(c2);
        pts.push_back(c2); pts.push_back(c3);
        pts.push_back(c3); pts.push_back(c0);
        pts.push_back(Vec3(0, 0, 0)); pts.push_back(Vec3(0, -0.4f, 0));
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
        // Hides the wire lines AND the icon billboard set riding on wireNode
        // (the engine toggles a set's visibility flags with its owning node).
        if (e.wireNode) mTarget->setNodeVisible(e.wireNode, false);
        return;
    }
    int kind = 1;
    if (light->lightType == iris::LightType::Directional) kind = 0;
    else if (light->lightType == iris::LightType::Spot) kind = 2;
    else if (light->lightType == iris::LightType::Area) kind = 3;
    // Attenuation volumes only for the HIGHLIGHTED light (Unreal convention):
    // an unselected point light shows just its icon, an unselected spot light
    // just the direction arrow. The directional arrow and the area rectangle
    // (the light's physical shape, not a falloff volume) stay on for every
    // light; icons are always-on with the helpers toggle.
    const bool selected = mHighlighted &&
                          mHighlighted.data() == static_cast<iris::SceneNode *>(light);
    int shape = kind;
    if (!selected) {
        if (kind == 1) shape = -1;        // point: rings are the falloff volume
        else if (kind == 2) shape = 0;    // spot: keep the arrow, drop the cone
    }
    if (!e.wireNode) e.wireNode = mTarget->createNode(e.node);
    if (!e.wireNode) return;
    if (shape < 0) {
        if (e.wireKind != -1) { mTarget->detachMesh(e.wireNode); e.wireKind = -1; }
        mTarget->setNodeVisible(e.wireNode, true);   // the icon set rides this node
        syncLightIcon(e, light);
        return;
    }
    MeshId m = wireMeshFor(shape);
    if (!m) return;
    if (!e.wireMaterial) e.wireMaterial = mTarget->createUnlitMaterial(Colour(1, 1, 1), false);
    if (!e.wireMaterial) return;
    if (e.wireKind != shape) { if (mTarget->attachMesh(e.wireNode, m, e.wireMaterial)) e.wireKind = shape; }
    const QColor c = light->color;
    mTarget->setUnlitMaterial(e.wireMaterial, Colour(c.redF(), c.greenF(), c.blueF(), 1.0f));
    // Wires live in the light node's local space; undo the node's own scale, and
    // size the shape by the light's range so the wire shows the actual falloff
    // volume (the meshes are authored at ring radius 0.5, cone depth 1.5 /
    // base radius 0.6 — see wireMeshFor). Directional lights have no range.
    float rx = 1.0f, ry = 1.0f, rz = 1.0f;
    const float range = std::max(0.01f, light->distance);
    if (shape == 1) {
        rx = ry = rz = range / 0.5f;               // rings at radius = range
    } else if (shape == 2) {
        ry = range / 1.5f;                         // cone reaches down to range
        const float half = qDegreesToRadians(std::min(std::max(light->spotCutOff, 1.0f), 89.0f));
        rx = rz = range * std::tan(half) / 0.6f;   // base radius = range * tan(cutoff)
    } else if (shape == 3) {
        rx = std::max(light->rectWidth, 0.01f);    // unit rect scaled to the emitting rectangle
        rz = std::max(light->rectHeight, 0.01f);   // (width = local X, height = local Z; tick stays)
    }
    const QVector3D s = light->getLocalScale();
    mTarget->setNodeTransform(e.wireNode, Vec3(), Quat(),
                              Vec3(rx * (s.x() > 1e-6f ? 1.0f / s.x() : 1.0f),
                                   ry * (s.y() > 1e-6f ? 1.0f / s.y() : 1.0f),
                                   rz * (s.z() > 1e-6f ? 1.0f / s.z() : 1.0f)));
    mTarget->setNodeVisible(e.wireNode, true);
    syncLightIcon(e, light);
}

// The icon billboard: one camera-facing glyph at the light's position (sun for
// directional, bulb for point, spotlight for spot — like Unreal's sprites). It
// rides the wireNode so the light-wires toggle and node teardown govern it, but
// instance positions are world-space (the set hangs off the engine's static
// root). Engine-side only: document picking never sees it.
void SceneMirror::syncLightIcon(Entry &e, iris::LightNode *light)
{
    if (!e.wireNode) return;
    // The document loads a per-light icon (mainwindow/scenereader); its source
    // path doubles as the image path. Fall back by light type.
    QString path = light->icon ? light->icon->getSource() : QString();
    if (path.isEmpty()) {
        switch (light->lightType) {
        case iris::LightType::Directional: path = QStringLiteral(":/icons/light.png"); break;    // the sun glyph
        case iris::LightType::Spot:        path = QStringLiteral(":/icons/spotlight.png"); break;
        // No bundled area glyph: a sentinel key makes iconTextureFor draw a
        // procedural rounded-rect panel (the Unreal-style rect-light sprite).
        case iris::LightType::Area:        path = QStringLiteral("jah://area-light-glyph"); break;
        default:                           path = QStringLiteral(":/icons/bulb.png"); break;
        }
    }
    if (!e.hasIcon || e.iconSignature != path) {
        if (!mTarget->createBillboardSet(e.wireNode, iconTextureFor(path), false, 1))
            return;
        e.hasIcon = true;
        e.iconSignature = path;
    }
    BillboardInstance b;
    const QVector3D p = light->getGlobalPosition();
    b.position = Vec3(p.x(), p.y(), p.z());
    b.size = light->iconSize > 0.0f ? light->iconSize : 0.5f;
    mTarget->setBillboards(e.wireNode, &b, 1);
}

TextureId SceneMirror::iconTextureFor(const QString &path)
{
    auto it = mIconTextures.constFind(path);
    if (it != mIconTextures.constEnd()) return it.value();
    // Qt resource or file path; the engine can't read resources, so upload the
    // pixels ourselves. Icons are forced to white glyphs (alpha kept) so every
    // icon reads the same regardless of the source image's colour.
    QImage img(path);
    if (path == QStringLiteral("jah://area-light-glyph")) {
        // Procedural white rounded-rect panel for area lights (no bundled glyph).
        img = QImage(32, 32, QImage::Format_RGBA8888);
        img.fill(Qt::transparent);
        const float r = 5.0f;                    // corner radius
        const float x0 = 4, x1 = 27, y0 = 7, y1 = 24;  // wider than tall: a panel
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                if (x < x0 || x > x1 || y < y0 || y > y1) continue;
                const float cx = std::min(std::max(float(x), x0 + r), x1 - r);
                const float cy = std::min(std::max(float(y), y0 + r), y1 - r);
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r)
                    img.setPixelColor(x, y, QColor(255, 255, 255, 255));
            }
    } else if (img.isNull()) {
        // No image (e.g. resources absent in tests): a plain white disc.
        img = QImage(32, 32, QImage::Format_RGBA8888);
        img.fill(Qt::transparent);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                if ((x - 15.5f) * (x - 15.5f) + (y - 15.5f) * (y - 15.5f) <= 14.0f * 14.0f)
                    img.setPixelColor(x, y, QColor(255, 255, 255, 255));
    }
    img = img.convertToFormat(QImage::Format_RGBA8888);
    uchar *bits = img.bits();
    const qsizetype n = img.width() * qsizetype(img.height());
    for (qsizetype i = 0; i < n; ++i) { bits[i * 4 + 0] = 255; bits[i * 4 + 1] = 255; bits[i * 4 + 2] = 255; }
    TextureId id = mTarget->createTexture(unsigned(img.width()), unsigned(img.height()), img.constBits(), true);
    mIconTextures.insert(path, id);   // cache failures (0) too: don't retry every frame
    return id;
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

    if (node->getSceneNodeType() == iris::SceneNodeType::ParticleSystem)
        syncParticles(e, static_cast<iris::ParticleSystemNode *>(node.data()));

    if (node->getSceneNodeType() == iris::SceneNodeType::Light) {
        // The light rides on the mirrored node: position and direction follow the document.
        auto light = node.staticCast<iris::LightNode>();
        if (mTarget->setLight(e.node, toLightDesc(light.data()))) e.hasLight = true;
        // The document's per-light shadow type (Hard/Soft/VerySoft) has no per-light
        // engine equivalent — the filter is global. Accumulate the strongest request;
        // applyEnvironment pushes it (iris::ShadowMapType orders None<Hard<Soft<VerySoft).
        if (light->lightType != iris::LightType::Area &&   // area lights cannot shadow
            light->shadowMap && light->shadowMap->shadowType != iris::ShadowMapType::None) {
            ShadowFilter f = ShadowFilter::Hard;
            if (light->shadowMap->shadowType == iris::ShadowMapType::Soft)          f = ShadowFilter::Soft;
            else if (light->shadowMap->shadowType == iris::ShadowMapType::VerySoft) f = ShadowFilter::VerySoft;
            if (!mAnyShadowCaster || int(f) > int(mShadowFilter)) mShadowFilter = f;
            mAnyShadowCaster = true;
            // Shadow Size is global too (one atlas): the largest request wins.
            if (light->shadowMap->resolution > 0)
                mMaxShadowResolution = std::max(mMaxShadowResolution,
                                                unsigned(light->shadowMap->resolution));
        }
        syncLightWires(e, light.data());
    }

    // `e` is a reference into a QHash: the recursion inserts entries and QHash does not
    // keep value references stable across inserts (use-after-free under ASan). Copy first.
    const NodeId self = e.node;
    for (auto &child : node->children)
        visit(child, self, seen);
}

// ---- particles ------------------------------------------------------------------
// The document simulates (ParticleSystemNode::update, CPU, world-space); the
// mirror pushes the live particle list into the node's engine billboard set each
// sync. The engine frees the set with the node (removeNode / scene teardown).
void SceneMirror::syncParticles(Entry &e, iris::ParticleSystemNode *ps)
{
    if (!e.node) return;
    const QString texPath = ps->texture ? ps->texture->getSource() : QString();
    const QString sig = (ps->useAdditive ? QStringLiteral("add|") : QStringLiteral("alpha|")) + texPath;
    if (!e.hasBillboards || e.billboardSignature != sig) {
        // maxParticles is the document's (unenforced) cap; 0 means none was set.
        const unsigned capacity = ps->maxParticles > 0 ? unsigned(ps->maxParticles) : 4096u;
        // Colour map -> srgb. Qt resource textures (":...") are not files the
        // engine can read; textureFor returns 0 and the quads render white.
        TextureId tex = texPath.isEmpty() ? 0 : textureFor(texPath, true);
        if (!mTarget->createBillboardSet(e.node, tex, ps->useAdditive, capacity))
            return;
        e.hasBillboards = true;
        e.billboardSignature = sig;
    }
    std::vector<BillboardInstance> instances;
    instances.reserve(ps->particles.size());
    for (const iris::Particle *p : ps->particles) {
        BillboardInstance b;
        b.position = toVec3(p->position);                    // already world-space
        b.size = 2.0f * p->scale;                            // legacy quad spans +/- scale
        b.rotationRadians = qDegreesToRadians(p->rotation);  // legacy stores degrees
        instances.push_back(b);
    }
    mTarget->setBillboards(e.node, instances.data(), instances.size());
}

void SceneMirror::reclaimUnused()
{
    QSet<MeshId> usedMeshes; QSet<MaterialId> usedMaterials;
    for (const Entry &e : mEntries) { if (e.mesh) usedMeshes.insert(e.mesh); if (e.material) usedMaterials.insert(e.material); }
    for (const HighlightShell &s : mHighlightShells) if (s.mesh) usedMeshes.insert(s.mesh);
    for (auto it = mMeshes.begin(); it != mMeshes.end();) {
        if (usedMeshes.contains(it.value())) { ++it; continue; }
        mSkins.remove(it.key());
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
    // A mesh with a skeleton AND bone vertex data is CPU-skinned: the engine mesh
    // is created updatable and syncSkinnedMeshes pushes posed vertices each time
    // the document's boneTransforms change. Static meshes keep the immutable path.
    SkinRec skin;
    const bool skinned = mesh->hasSkeleton() &&
                         toSkinData(mesh, skin.boneIndices, skin.boneWeights) &&
                         skin.boneIndices.size() == data.vertexCount() * 4;
    data.dynamic = skinned;
    MeshId id = mTarget->createMesh(data);
    if (id) {
        mMeshes.insert(mesh, id);
        if (skinned) {
            skin.bindPositions = data.positions;
            skin.bindNormals   = data.normals;
            mSkins.insert(mesh, std::move(skin));
        }
    }
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
        case 3:  out.alphaMode = PbrAlphaMode::Glass;  break;   // fades diffuse, keeps reflections
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
    case iris::LightType::Area:        d.type = LightType::Area; break;
    case iris::LightType::Point: default: d.type = LightType::Point; break;
    }
    d.colour = Colour(light->color.redF(), light->color.greenF(), light->color.blueF(), 1.0f);
    d.intensity = light->intensity;
    d.range = light->distance;
    d.spotAngleDegrees = light->spotCutOff;
    d.spotSoftness = light->spotCutOffSoftness;
    d.rectWidth = light->rectWidth;
    d.rectHeight = light->rectHeight;
    d.doubleSided = light->doubleSided;
    d.accurate = light->accurate;
    // Area lights never cast shadows (Ogre-Next limitation; the engine enforces
    // it too — this keeps the mirror's shadow-filter bookkeeping honest).
    d.castShadows = light->lightType != iris::LightType::Area &&
                    light->shadowMap && light->shadowMap->shadowType != iris::ShadowMapType::None;
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

// ---- CPU skinning -----------------------------------------------------------------
// The document already computes per-bone skin matrices (Skeleton::boneTransforms,
// filled by SceneNode::updateAnimation during play). The legacy GL renderer handed
// those to a skinning vertex shader; on the engine the mirror applies the identical
// math on the CPU and pushes the posed vertices through Scene::updateMeshVertices.

bool SceneMirror::toSkinData(iris::Mesh *mesh, std::vector<float> &boneIndices,
                             std::vector<float> &boneWeights)
{
    boneIndices.clear(); boneWeights.clear();
    if (!mesh) return false;
    for (const auto &vb : mesh->getVertexBuffers()) {
        if (!vb || !vb->data) continue;
        const QList<iris::VertexAttribute> attribs = vb->vertexLayout.getAttribs();
        if (attribs.isEmpty()) continue;
        // Both buffers are 4 floats per vertex (mesh.cpp MAX_BONE_INDICES; indices
        // are stored as floats — the GL shader cast them back with int()).
        const float *f = reinterpret_cast<const float *>(vb->data);
        const int floats = vb->dataSize / int(sizeof(float));
        switch (attribs.first().usage) {
        case iris::VertexAttribUsage::BoneIndices: boneIndices.assign(f, f + floats); break;
        case iris::VertexAttribUsage::BoneWeights: boneWeights.assign(f, f + floats); break;
        default: break;
        }
    }
    return !boneIndices.empty() && boneIndices.size() == boneWeights.size();
}

void SceneMirror::skinVertices(const QVector<QMatrix4x4> &boneTransforms,
                               const std::vector<float> &bindPositions,
                               const std::vector<float> &bindNormals,
                               const std::vector<float> &boneIndices,
                               const std::vector<float> &boneWeights,
                               std::vector<float> &outPositions,
                               std::vector<float> &outNormals)
{
    const size_t nv = bindPositions.size() / 3;
    const bool haveNormals = bindNormals.size() == bindPositions.size();
    outPositions = bindPositions;
    outNormals = haveNormals ? bindNormals : std::vector<float>();
    if (boneTransforms.isEmpty() || boneIndices.size() < nv * 4 || boneWeights.size() < nv * 4)
        return;
    // Flatten each bone's skin matrix to row-major 3x4 (QMatrix4x4 stores
    // column-major). Row-major keeps the per-vertex loop cache-friendly.
    const int nb = boneTransforms.size();
    std::vector<float> mats(size_t(nb) * 12);
    for (int b = 0; b < nb; ++b) {
        const float *m = boneTransforms[b].constData();   // column-major
        float *d = &mats[size_t(b) * 12];
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c) d[r * 4 + c] = m[c * 4 + r];
    }
    for (size_t v = 0; v < nv; ++v) {
        const float *bi = &boneIndices[v * 4];
        const float *bw = &boneWeights[v * 4];
        const float wsum = bw[0] + bw[1] + bw[2] + bw[3];
        if (wsum <= 1e-6f) continue;                      // unweighted: stay at bind pose
        // Weighted sum of bone matrices, then transform — exactly the GL shader
        // (pbr_material.vert): boneMatrix = sum(u_bones[idx] * weight).
        float B[12] = { 0 };
        for (int k = 0; k < 4; ++k) {
            const int idx = int(bi[k]);
            if (bw[k] == 0.0f || idx < 0 || idx >= nb) continue;
            const float w = bw[k];
            const float *m = &mats[size_t(idx) * 12];
            for (int j = 0; j < 12; ++j) B[j] += m[j] * w;
        }
        const float px = bindPositions[v*3], py = bindPositions[v*3+1], pz = bindPositions[v*3+2];
        outPositions[v*3]   = B[0]*px + B[1]*py + B[2]*pz  + B[3];
        outPositions[v*3+1] = B[4]*px + B[5]*py + B[6]*pz  + B[7];
        outPositions[v*3+2] = B[8]*px + B[9]*py + B[10]*pz + B[11];
        if (haveNormals) {
            const float nx = bindNormals[v*3], ny = bindNormals[v*3+1], nz = bindNormals[v*3+2];
            float ox = B[0]*nx + B[1]*ny + B[2]*nz;
            float oy = B[4]*nx + B[5]*ny + B[6]*nz;
            float oz = B[8]*nx + B[9]*ny + B[10]*nz;
            const float len = std::sqrt(ox*ox + oy*oy + oz*oz);
            if (len > 1e-8f) { ox /= len; oy /= len; oz /= len; }
            outNormals[v*3] = ox; outNormals[v*3+1] = oy; outNormals[v*3+2] = oz;
        }
    }
}

void SceneMirror::syncSkinnedMeshes()
{
    for (auto it = mSkins.begin(); it != mSkins.end();) {
        iris::Mesh *mesh = it.key();
        const auto midIt = mMeshes.constFind(mesh);
        if (midIt == mMeshes.constEnd()) { it = mSkins.erase(it); continue; }
        iris::SkeletonPtr skel = mesh->getSkeleton();
        if (skel) {
            const QVector<QMatrix4x4> &pose = skel->boneTransforms;
            // Only push when the pose actually changed (paused/edit-mode scenes
            // pay nothing after the first frame).
            if (pose != it->lastPose) {
                std::vector<float> positions, normals;
                skinVertices(pose, it->bindPositions, it->bindNormals,
                             it->boneIndices, it->boneWeights, positions, normals);
                if (mTarget->updateMeshVertices(midIt.value(), positions, normals))
                    it->lastPose = pose;
            }
        }
        ++it;
    }
}

void SceneMirror::applyEnvironment(View *view, Engine *engine)
{
    if (!mSource || !view) return;
    // Shadow filter: the engine has ONE global filter (Engine.h), the document a
    // per-light ShadowMapType. Policy: the strongest (softest) quality any
    // shadow-casting light asked for wins, as accumulated by the last sync().
    // Nothing casting shadows leaves the engine's current filter untouched.
    if (engine && mAnyShadowCaster && engine->shadowFilter() != mShadowFilter)
        engine->setShadowFilter(mShadowFilter);
    // Shadow Size: same policy, largest requested size wins. The engine rebuilds
    // its shadow atlas on change — the compare here is what keeps that rare.
    if (engine && mAnyShadowCaster && mMaxShadowResolution > 0 &&
        engine->shadowResolution() != mMaxShadowResolution)
        engine->setShadowResolution(mMaxShadowResolution);
    // World-panel Ambient Color: flat, exactly like the legacy uniform (the
    // engine viewport used to hardcode the hemisphere — the panel no-op'd).
    const QColor a = mSource->ambientColor;
    mTarget->setAmbient(Colour(a.redF(), a.greenF(), a.blueF(), 1.0f),
                        Colour(a.redF(), a.greenF(), a.blueF(), 1.0f));
    // World-panel Enable Shadows (used to be hardcoded on).
    if (view->shadows() != mSource->shadowEnabled)
        view->setShadows(mSource->shadowEnabled);
    // Fog panel: linear distance fog on lit surfaces (engine keeps unlit overlays
    // and the sky unfogged, like the legacy renderer). Cheap per-frame push.
    const QColor f = mSource->fogColor;
    mTarget->setFog(mSource->fogEnabled, Colour(f.redF(), f.greenF(), f.blueF(), 1.0f),
                    mSource->fogStart, mSource->fogEnd);
    // Global Illumination panel. setGlobalIllumination re-traces, so unlike fog it
    // is NOT free: push only when the document state changed (the per-frame compare
    // is the debounce), and re-trace when the driving light itself moved — Instant
    // Radiosity solves in milliseconds at editor quality, per GI_SPEC.md.
    {
        GiParams gi;
        switch (mSource->giMode) {
        case iris::GiMode::INSTANT_RADIOSITY: gi.mode = GiMode::InstantRadiosity; break;
        case iris::GiMode::VCT:               gi.mode = GiMode::Vct; break;
        case iris::GiMode::VCT_PCC_HYBRID:    gi.mode = GiMode::VctPccHybrid; break;
        case iris::GiMode::OFF: default:      gi.mode = GiMode::Off; break;
        }
        switch (mSource->giQuality) {
        case iris::GiQuality::LOW:             gi.quality = GiQuality::Low; break;
        case iris::GiQuality::HIGH:            gi.quality = GiQuality::High; break;
        case iris::GiQuality::MEDIUM: default: gi.quality = GiQuality::Medium; break;
        }
        gi.boundsMin = toVec3(mSource->giBoundsMin);
        gi.boundsMax = toVec3(mSource->giBoundsMax);
        gi.numBounces = mSource->giNumBounces;
        iris::LightNode *driver = gi.mode == GiMode::InstantRadiosity ? resolveGiLight() : nullptr;
        gi.irLight = driver ? engineNode(driver) : 0;
        const auto same = [](const GiParams &a, const GiParams &b) {
            return a.mode == b.mode && a.quality == b.quality && a.irLight == b.irLight &&
                   a.numBounces == b.numBounces &&
                   a.boundsMin.x == b.boundsMin.x && a.boundsMin.y == b.boundsMin.y &&
                   a.boundsMin.z == b.boundsMin.z && a.boundsMax.x == b.boundsMax.x &&
                   a.boundsMax.y == b.boundsMax.y && a.boundsMax.z == b.boundsMax.z;
        };
        const QMatrix4x4 lightWorld = driver ? driver->globalTransform : QMatrix4x4();
        if (!mGiPushed || !same(gi, mLastGi)) {
            mTarget->setGlobalIllumination(gi);
            mLastGi = gi;
            mGiLightWorld = lightWorld;
            mGiPushed = true;
        } else if (gi.mode == GiMode::InstantRadiosity && mSource->giAutoRefresh &&
                   lightWorld != mGiLightWorld) {
            mGiLightWorld = lightWorld;
            mTarget->refreshGlobalIllumination();
        }
    }
}

iris::LightNode *SceneMirror::resolveGiLight() const
{
    if (!mSource) return nullptr;
    if (!mSource->giLightGuid.isEmpty()) {
        auto it = mSource->lights.constFind(mSource->giLightGuid);
        if (it != mSource->lights.constEnd() && !it.value().isNull()) return it.value().data();
    }
    // QHash order is arbitrary: pick deterministically by creation order (nodeId).
    iris::LightNode *directional = nullptr, *any = nullptr;
    for (const auto &l : mSource->lights) {
        if (l.isNull()) continue;
        if (l->lightType == iris::LightType::Directional &&
            (!directional || l->nodeId < directional->nodeId)) directional = l.data();
        if (!any || l->nodeId < any->nodeId) any = l.data();
    }
    return directional ? directional : any;
}

void SceneMirror::applySky(View *view)
{
    if (!mSource || !view) return;
    QString signature;
    if (mSource->skyType == iris::SkyType::EQUIRECTANGULAR && mSource->skyTexture)
        signature = "equirect:" + mSource->skyTexture->source;
    else if (mSource->skyType == iris::SkyType::CUBEMAP && mSource->skyTexture && mSource->skyTexture->isCubeMap())
        signature = "cubemap:" + QString::number(reinterpret_cast<quintptr>(mSource->skyTexture.data()));
    else if (mSource->skyType == iris::SkyType::GRADIENT)
        signature = QString("gradient:%1/%2/%3/%4").arg(mSource->gradientTop.name(), mSource->gradientMid.name(),
                                                        mSource->gradientBot.name()).arg(mSource->gradientOffset);
    else if (mSource->skyType == iris::SkyType::REALISTIC) {
        const iris::SkyRealistic &s = mSource->skyRealistic;
        signature = QString("realistic:%1/%2/%3/%4/%5/%6/%7/%8")
                        .arg(s.luminance).arg(s.reileigh).arg(s.mieCoefficient).arg(s.mieDirectionalG)
                        .arg(s.turbidity).arg(s.sunPosX).arg(s.sunPosY).arg(s.sunPosZ);
    }
    if (signature != mSkySignature) {
        // Debounce the realistic bake: a slider drag changes the 8 parameters on
        // every event, and the Preetham bake is per-pixel CPU math. Re-bake at
        // most every 150 ms — applySky recomputes the signature next frame, so
        // the final value always lands once the slider settles.
        if (signature.startsWith("realistic:") && mSkySignature.startsWith("realistic:") &&
            mRealisticBakeTimer.isValid() && mRealisticBakeTimer.elapsed() < 150)
            return;
        mSkySignature = signature;
        for (TextureId &t : mSkyFaceTextures)  { if (t) mTarget->destroyTexture(t); t = 0; }
        for (TextureId &t : mReflFaceTextures) { if (t) mTarget->destroyTexture(t); t = 0; }
        if (signature.startsWith("equirect:")) {
            TextureId t = textureFor(mSource->skyTexture->source, true);
            mTarget->setSky(t ? SkyMode::Equirectangular : SkyMode::NoSky, t);
            // Cubemap skies feed environment reflections (IBL); give equirect
            // skies the same by resampling the image into six small faces.
            if (t) applySkyReflection(QImage(mSource->skyTexture->source));
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
        } else if (signature.startsWith("gradient:")) {
            // Legacy gradientsky.frag is a pure vertical 3-stop ramp: bake it into a
            // narrow equirect strip (row 0 = zenith) and reuse the equirect sky path.
            const float middle = qBound(0.01f, mSource->gradientOffset, 0.99f);
            const QColor top = mSource->gradientTop, mid = mSource->gradientMid, bot = mSource->gradientBot;
            const int H = 256, W = 4;
            std::vector<unsigned char> px(size_t(W) * H * 4u);
            for (int r = 0; r < H; ++r) {
                const float offset = 1.0f - float(r) / (H - 1);   // 1 at the top row
                float t; const QColor *c0, *c1;
                if (offset <= middle) { t = offset / middle;                 c0 = &bot; c1 = &mid; }
                else                  { t = (offset - middle) / (1 - middle); c0 = &mid; c1 = &top; }
                const unsigned char rr = (unsigned char)qBound(0.0f, (c0->redF()   + (c1->redF()   - c0->redF())   * t) * 255.0f, 255.0f);
                const unsigned char gg = (unsigned char)qBound(0.0f, (c0->greenF() + (c1->greenF() - c0->greenF()) * t) * 255.0f, 255.0f);
                const unsigned char bb = (unsigned char)qBound(0.0f, (c0->blueF()  + (c1->blueF()  - c0->blueF())  * t) * 255.0f, 255.0f);
                for (int x = 0; x < W; ++x) {
                    unsigned char *p = &px[(size_t(r) * W + x) * 4u];
                    p[0] = rr; p[1] = gg; p[2] = bb; p[3] = 255;
                }
            }
            mSkyFaceTextures[0] = mTarget->createTexture(W, H, px.data(), true);
            mTarget->setSky(mSkyFaceTextures[0] ? SkyMode::Equirectangular : SkyMode::NoSky, mSkyFaceTextures[0]);
            if (mSkyFaceTextures[0]) {
                QImage strip(W, H, QImage::Format_RGBA8888);
                for (int r = 0; r < H; ++r)
                    std::memcpy(strip.scanLine(r), &px[size_t(r) * W * 4u], size_t(W) * 4u);
                applySkyReflection(strip);
            }
        } else if (signature.startsWith("realistic:")) {
            // Legacy realisticsky.frag (Preetham-style scattering), CPU-baked to
            // an equirect image and pushed through the same sky path as gradient.
            const QImage baked = bakeRealisticSky(mSource->skyRealistic, 256, 128);
            mRealisticBakeTimer.restart();
            if (!baked.isNull()) {
                mSkyFaceTextures[0] = mTarget->createTexture(unsigned(baked.width()), unsigned(baked.height()),
                                                             baked.constBits(), true);
                mTarget->setSky(mSkyFaceTextures[0] ? SkyMode::Equirectangular : SkyMode::NoSky, mSkyFaceTextures[0]);
                if (mSkyFaceTextures[0]) applySkyReflection(baked);
            } else {
                mTarget->setSky(SkyMode::NoSky, 0);
            }
        } else {
            mTarget->setSky(SkyMode::NoSky, 0);
        }
    }
    if (mSource->skyType == iris::SkyType::SINGLE_COLOR) {
        const QColor c = mSource->skyColor;
        view->setBackground(Colour(c.redF(), c.greenF(), c.blueF(), 1.0f));
    }
}

void SceneMirror::applySkyReflection(const QImage &equirect)
{
    if (equirect.isNull()) return;
    const QImage src = equirect.convertToFormat(QImage::Format_RGBA8888);
    const int W = src.width(), H = src.height();
    if (W <= 0 || H <= 0) return;
    // Face basis identical to the engine's cubemap sky quads (+X,-X,+Y,-Y,+Z,-Z;
    // dir = axis + right*u + up*v with image row 0 at the top), so reflections
    // line up with the sky the camera sees. The equirect mapping mirrors the
    // engine's sky sphere: u = 1 - theta/2pi, v = phi/pi (v = 0 at the zenith).
    static const float ax[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    static const float rt[6][3] = {{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}};
    static const float up[6][3] = {{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}};
    const int N = 64;   // modest: the engine mips it; roughness blurs the rest
    std::vector<unsigned char> face(size_t(N) * N * 4u);
    TextureId ids[6] = { 0, 0, 0, 0, 0, 0 };
    bool ok = true;
    for (int f = 0; f < 6 && ok; ++f) {
        const float *a = ax[f], *r = rt[f], *u = up[f];
        for (int py = 0; py < N; ++py) {
            const float uv = 1.0f - 2.0f * (py + 0.5f) / N;   // up multiplier, row 0 = top
            for (int px = 0; px < N; ++px) {
                const float ur = 2.0f * (px + 0.5f) / N - 1.0f;
                float dx = a[0] + r[0] * ur + u[0] * uv;
                float dy = a[1] + r[1] * ur + u[1] * uv;
                float dz = a[2] + r[2] * ur + u[2] * uv;
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                dx /= len; dy /= len; dz /= len;
                float ut = 1.0f - std::atan2(dz, dx) / 6.2831853f;
                ut -= std::floor(ut);
                const float vt = std::acos(std::min(1.0f, std::max(-1.0f, dy))) / 3.14159265f;
                const int xi = std::min(W - 1, int(ut * W));
                const int yi = std::min(H - 1, int(vt * H));
                std::memcpy(&face[(size_t(py) * N + px) * 4u], src.constScanLine(yi) + size_t(xi) * 4u, 4u);
            }
        }
        ids[f] = mTarget->createTexture(unsigned(N), unsigned(N), face.data(), true);
        if (!ids[f]) ok = false;
    }
    if (ok && mTarget->setSkyReflection(ids)) {
        for (int i = 0; i < 6; ++i) mReflFaceTextures[i] = ids[i];
    } else {
        for (int i = 0; i < 6; ++i) if (ids[i]) mTarget->destroyTexture(ids[i]);
    }
}

// CPU port of irisgl/assets/shaders/realisticsky.frag (a Preetham-style analytic
// scattering shader, Three.js lineage). Faithful to the GLSL — including its
// quirks (the unused ExposureBias, the simplified Rayleigh term) — evaluated per
// equirect texel over the view direction; the sun's disc, colour and haze land
// exactly where the legacy renderer put them.
QImage SceneMirror::bakeRealisticSky(const iris::SkyRealistic &sky, int width, int height)
{
    if (width <= 0 || height <= 0) return QImage();
    struct V3 {
        float x, y, z;
        V3(float v = 0) : x(v), y(v), z(v) {}
        V3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        V3 operator+(const V3 &o) const { return V3(x + o.x, y + o.y, z + o.z); }
        V3 operator-(const V3 &o) const { return V3(x - o.x, y - o.y, z - o.z); }
        V3 operator*(const V3 &o) const { return V3(x * o.x, y * o.y, z * o.z); }
        V3 operator/(const V3 &o) const { return V3(x / o.x, y / o.y, z / o.z); }
        V3 operator*(float s) const { return V3(x * s, y * s, z * s); }
    };
    const auto vpow = [](const V3 &v, float e) {
        return V3(std::pow(std::max(0.0f, v.x), e), std::pow(std::max(0.0f, v.y), e),
                  std::pow(std::max(0.0f, v.z), e));
    };
    const auto vexp = [](const V3 &v) { return V3(std::exp(v.x), std::exp(v.y), std::exp(v.z)); };
    const float pi = 3.14159265358979f;
    // Filmic tonemap constants (Uncharted2), verbatim from the shader.
    const float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f, W = 1000.0f;
    const auto tonemap = [&](const V3 &v) {
        const auto f1 = [&](float x) {
            return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
        };
        return V3(f1(v.x), f1(v.y), f1(v.z));
    };

    // Per-image terms (uniform across directions).
    const float luminance = std::max(0.01f, sky.luminance);
    const float sunfade = 1.0f - std::min(1.0f, std::max(0.0f, 1.0f - std::exp(sky.sunPosY / 450000.0f)));
    const float reileighCoefficient = sky.reileigh - (1.0f * (1.0f - sunfade));
    V3 sunDirection(sky.sunPosX, sky.sunPosY, sky.sunPosZ);
    {
        const float len = std::sqrt(sunDirection.x * sunDirection.x + sunDirection.y * sunDirection.y +
                                    sunDirection.z * sunDirection.z);
        if (len > 1e-6f) sunDirection = sunDirection * (1.0f / len); else sunDirection = V3(0, 1, 0);
    }
    const float cutoffAngle = pi / 1.95f, steepness = 1.5f, EE = 1000.0f;
    const float sunE = EE * std::max(0.0f, 1.0f - std::exp(-((cutoffAngle - std::acos(std::min(1.0f, std::max(-1.0f, sunDirection.y)))) / steepness)));
    const V3 betaR = V3(0.0005f / 94.0f, 0.0005f / 40.0f, 0.0005f / 18.0f) * reileighCoefficient;
    // totalMie(lambda, K, T) * mieCoefficient; lambda/K/v verbatim.
    const V3 lambda(680e-9f, 550e-9f, 450e-9f);
    const V3 K(0.686f, 0.678f, 0.666f);
    const float mieC = (0.2f * sky.turbidity) * 1e-17f;   // (0.2*T)*10E-18 in GLSL
    const V3 betaM = V3(0.434f * mieC * pi * std::pow(2.0f * pi / lambda.x, 2.0f) * K.x,
                        0.434f * mieC * pi * std::pow(2.0f * pi / lambda.y, 2.0f) * K.y,
                        0.434f * mieC * pi * std::pow(2.0f * pi / lambda.z, 2.0f) * K.z) * sky.mieCoefficient;
    const V3 betaRM = betaR + betaM;
    const V3 whiteScale = V3(1, 1, 1) / tonemap(V3(W));
    const float sunAngularDiameterCos = 0.99995667694644844f;
    const float horizonMix = std::min(1.0f, std::max(0.0f, std::pow(std::max(0.0f, 1.0f - sunDirection.y), 5.0f)));
    const float exposure = std::log2(2.0f / std::pow(luminance, 4.0f));
    const float finalGamma = 1.0f / (1.2f + (1.2f * sunfade));

    QImage img(width, height, QImage::Format_RGBA8888);
    for (int row = 0; row < height; ++row) {
        unsigned char *out = img.scanLine(row);
        const float phi = (row + 0.5f) / height * pi;      // 0 at the zenith (sphere v)
        const float sinPhi = std::sin(phi), cosPhi = std::cos(phi);
        for (int col = 0; col < width; ++col) {
            const float theta = (1.0f - (col + 0.5f) / width) * 2.0f * pi;   // sphere u = 1 - theta/2pi
            const V3 dir(sinPhi * std::cos(theta), cosPhi, sinPhi * std::sin(theta));

            const float zenithAngle = std::acos(std::max(0.0f, dir.y));
            const float denom = std::cos(zenithAngle) +
                                0.15f * std::pow(93.885f - zenithAngle * 180.0f / pi, -1.253f);
            const float sR = 8.4e3f / denom, sM = 1.25e3f / denom;
            const V3 Fex = vexp(V3(-(betaR.x * sR + betaM.x * sM), -(betaR.y * sR + betaM.y * sM),
                                   -(betaR.z * sR + betaM.z * sM)));

            const float cosTheta = dir.x * sunDirection.x + dir.y * sunDirection.y + dir.z * sunDirection.z;
            const float rp = cosTheta * 0.5f + 0.5f;
            const float rPhase = (3.0f / (16.0f * pi)) * (1.0f + rp * rp);
            const float g = sky.mieDirectionalG;
            const float mPhase = (1.0f / (4.0f * pi)) *
                ((1.0f - g * g) / std::pow(std::max(1e-6f, 1.0f - 2.0f * g * cosTheta + g * g), 1.5f));
            const V3 betaTheta = betaR * rPhase + betaM * mPhase;
            const V3 ratio = betaTheta / betaRM;

            V3 Lin = vpow(ratio * sunE * (V3(1, 1, 1) - Fex), 1.5f);
            const V3 linB = vpow(ratio * sunE * Fex, 0.5f);
            Lin = Lin * (V3(1.0f - horizonMix) + linB * horizonMix);

            // Night-sky base + the solar disc.
            V3 L0 = Fex * 0.1f;
            const float sundisk = cosTheta <= sunAngularDiameterCos ? 0.0f
                : cosTheta >= sunAngularDiameterCos + 0.00002f ? 1.0f
                : [&] { const float t = (cosTheta - sunAngularDiameterCos) / 0.00002f; return t * t * (3.0f - 2.0f * t); }();
            L0 = L0 + Fex * (sunE * 19000.0f * sundisk);

            V3 texColor = (Lin + L0) * 0.04f + V3(0.0f, 0.001f, 0.0025f) * 0.3f;
            V3 colr = tonemap(texColor * exposure) * whiteScale;
            colr = vpow(colr, finalGamma);

            const auto to8 = [](float v) {
                if (!std::isfinite(v)) v = 0.0f;
                return (unsigned char)std::lround(std::min(1.0f, std::max(0.0f, v)) * 255.0f);
            };
            unsigned char *p = out + size_t(col) * 4u;
            p[0] = to8(colr.x); p[1] = to8(colr.y); p[2] = to8(colr.z); p[3] = 255;
        }
    }
    return img;
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
