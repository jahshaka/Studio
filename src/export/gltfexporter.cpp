/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/gltfexporter.h"

#include "export/walkers/scenewalker.h"
#include "export/walkers/meshbufferreader.h"
#include "export/walkers/materialtexturereader.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuaternion>
#include <QVector3D>

#include <cmath>
#include <cstring>
#include <functional>
#include <vector>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/viewernode.h"
#include "irisgl/document/scenegraph/shadowmap.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/core/properties/property.h"

namespace {

// glTF component types
constexpr int GLTF_FLOAT = 5126;
constexpr int GLTF_UNSIGNED_INT = 5125;
constexpr int GLTF_UNSIGNED_SHORT = 5123;

// Light intensity calibration (audit §1 "Lights" row): the document's intensity
// is a raw legacy uniform; the engine renders intensity*pi because HlmsPbs
// divides by pi. The viewer's three.js lights are fed the same product, so the
// one constant keeps engine viewport and web viewer in the same brightness family.
constexpr float kLightIntensityScale = 3.14159265358979f;

// Textures above this edge are downscaled at export (audit §3 size ceiling).
constexpr int kMaxTextureEdge = 2048;

struct BinBuilder
{
    QByteArray bytes;

    void align(int n = 4)
    {
        while (bytes.size() % n) bytes.append('\0');
    }
    // returns byte offset
    int append(const void *data, int size)
    {
        align(4);
        const int off = bytes.size();
        bytes.append(reinterpret_cast<const char *>(data), size);
        return off;
    }
};

struct Ctx
{
    QJsonArray nodes, meshes, accessors, bufferViews, materials, textures,
        images, samplers, cameras, lights, skins, animations;
    BinBuilder bin;
    QStringList warnings;
    QStringList extensions;
    // dedupe
    QHash<iris::Mesh *, int> meshIndex;
    QHash<iris::Material *, int> materialIndex;
    QHash<QString, int> imageIndex;      // signature -> images[] index
    QHash<QString, int> textureIndex;    // image index+transform -> textures[]
    int samplerIndex = -1;

    void useExtension(const QString &name)
    {
        if (!extensions.contains(name)) extensions.append(name);
    }
};

int addBufferView(Ctx &c, const void *data, int size, int target = 0)
{
    const int off = c.bin.append(data, size);
    QJsonObject bv;
    bv["buffer"] = 0;
    bv["byteOffset"] = off;
    bv["byteLength"] = size;
    if (target) bv["target"] = target;
    c.bufferViews.append(bv);
    return c.bufferViews.size() - 1;
}

int addAccessor(Ctx &c, const std::vector<float> &data, int comps, const char *type,
                bool withMinMax = false)
{
    const int count = int(data.size()) / comps;
    const int bv = addBufferView(c, data.data(), int(data.size() * sizeof(float)), 34962);
    QJsonObject acc;
    acc["bufferView"] = bv;
    acc["componentType"] = GLTF_FLOAT;
    acc["count"] = count;
    acc["type"] = type;
    if (withMinMax && count > 0) {
        std::vector<float> mn(comps, 3e38f), mx(comps, -3e38f);
        for (int i = 0; i < count; ++i)
            for (int k = 0; k < comps; ++k) {
                mn[k] = std::min(mn[k], data[size_t(i) * comps + k]);
                mx[k] = std::max(mx[k], data[size_t(i) * comps + k]);
            }
        QJsonArray amin, amax;
        for (int k = 0; k < comps; ++k) { amin.append(mn[k]); amax.append(mx[k]); }
        acc["min"] = amin;
        acc["max"] = amax;
    }
    c.accessors.append(acc);
    return c.accessors.size() - 1;
}

int addIndexAccessor(Ctx &c, const std::vector<unsigned> &indices)
{
    const int bv = addBufferView(c, indices.data(), int(indices.size() * sizeof(unsigned)), 34963);
    QJsonObject acc;
    acc["bufferView"] = bv;
    acc["componentType"] = GLTF_UNSIGNED_INT;
    acc["count"] = int(indices.size());
    acc["type"] = "SCALAR";
    c.accessors.append(acc);
    return c.accessors.size() - 1;
}

int addUShortAccessor(Ctx &c, const std::vector<unsigned short> &data, int comps, const char *type)
{
    const int bv = addBufferView(c, data.data(), int(data.size() * sizeof(unsigned short)), 34962);
    QJsonObject acc;
    acc["bufferView"] = bv;
    acc["componentType"] = GLTF_UNSIGNED_SHORT;
    acc["count"] = int(data.size()) / comps;
    acc["type"] = type;
    c.accessors.append(acc);
    return c.accessors.size() - 1;
}

// ---- mesh conversion -------------------------------------------------------
// MeshBuffers extraction and the normal/tangent generators moved to the shared
// walker layer (export/walkers/meshbufferreader.*) — every exporter reads the
// same geometry the same way.

using exportwalk::MeshBuffers;
using exportwalk::extractMeshBuffers;
using exportwalk::generateNormals;
using exportwalk::generateTangents;

// ---- images / textures -----------------------------------------------------

QImage loadDocumentImage(const QString &source, Ctx &c)
{
    if (source.isEmpty()) return QImage();
    // QImage reads Qt resource paths (":/...") too — presets export fine even
    // though they are not files on disk (an improvement over the mirror).
    QImage img(source);
    if (img.isNull()) {
        c.warnings.append(QStringLiteral("texture not readable, skipped: %1").arg(source));
        return QImage();
    }
    if (img.width() > kMaxTextureEdge || img.height() > kMaxTextureEdge)
        img = img.scaled(kMaxTextureEdge, kMaxTextureEdge, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return img;
}

QByteArray encodeImage(const QImage &img, const char *format, int quality = -1)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, format, quality);
    return bytes;
}

int addImage(Ctx &c, const QString &signature, const QImage &img, bool preferJpeg)
{
    auto it = c.imageIndex.constFind(signature);
    if (it != c.imageIndex.constEnd()) return it.value();
    if (img.isNull()) return -1;
    const bool jpeg = preferJpeg && !img.hasAlphaChannel();
    const QByteArray bytes = jpeg ? encodeImage(img, "JPEG", 88) : encodeImage(img, "PNG");
    if (bytes.isEmpty()) return -1;
    const int bv = addBufferView(c, bytes.constData(), bytes.size());
    QJsonObject image;
    image["bufferView"] = bv;
    image["mimeType"] = jpeg ? "image/jpeg" : "image/png";
    c.images.append(image);
    const int idx = c.images.size() - 1;
    c.imageIndex.insert(signature, idx);
    return idx;
}

int ensureSampler(Ctx &c)
{
    if (c.samplerIndex >= 0) return c.samplerIndex;
    QJsonObject s;
    s["magFilter"] = 9729;   // LINEAR
    s["minFilter"] = 9987;   // LINEAR_MIPMAP_LINEAR
    s["wrapS"] = 10497;      // REPEAT
    s["wrapT"] = 10497;
    c.samplers.append(s);
    c.samplerIndex = c.samplers.size() - 1;
    return c.samplerIndex;
}

int addTexture(Ctx &c, int imageIdx)
{
    if (imageIdx < 0) return -1;
    const QString key = QString::number(imageIdx);
    auto it = c.textureIndex.constFind(key);
    if (it != c.textureIndex.constEnd()) return it.value();
    QJsonObject t;
    t["source"] = imageIdx;
    t["sampler"] = ensureSampler(c);
    c.textures.append(t);
    const int idx = c.textures.size() - 1;
    c.textureIndex.insert(key, idx);
    return idx;
}

QJsonObject textureRef(Ctx &c, int texIdx, float uvScale)
{
    QJsonObject ref;
    ref["index"] = texIdx;
    if (uvScale > 0.0f && std::fabs(uvScale - 1.0f) > 1e-5f) {
        c.useExtension("KHR_texture_transform");
        QJsonObject xf;
        QJsonArray sc; sc.append(uvScale); sc.append(uvScale);
        xf["scale"] = sc;
        QJsonObject ext; ext["KHR_texture_transform"] = xf;
        ref["extensions"] = ext;
    }
    return ref;
}

// Per-slot texture reads go through the shared walker layer.
using exportwalk::textureSlotSource;

QJsonArray colorArray(float r, float g, float b, float a = -1.0f)
{
    QJsonArray arr;
    arr.append(double(r)); arr.append(double(g)); arr.append(double(b));
    if (a >= 0.0f) arr.append(double(a));
    return arr;
}

// ---- materials -------------------------------------------------------------

int convertPbrMaterial(Ctx &c, iris::PbrMaterial *pbr, iris::FaceCullingMode cullMode)
{
    QJsonObject m;
    m["name"] = QStringLiteral("pbr");
    QJsonObject mr;

    const float uvScale = pbr->textureScale;
    const QColor bc = pbr->baseColor;
    const float bf = pbr->baseColorFactor;
    // Translucent (2) and Additive (4) carry alpha into baseColorFactor.A
    // (three's AdditiveBlending is SrcAlpha/One, so alpha scales the glow —
    // matching the engine's Fade-scaled SBT_ADD). Modulate ignores alpha.
    const float alpha = (pbr->alphaMode == 2 || pbr->alphaMode == 4) ? pbr->alpha : 1.0f;
    mr["baseColorFactor"] = colorArray(float(bc.redF()) * bf, float(bc.greenF()) * bf,
                                       float(bc.blueF()) * bf, alpha);

    const QString baseSrc = textureSlotSource(pbr, "u_baseColorMap");
    if (!baseSrc.isEmpty()) {
        const QImage img = loadDocumentImage(baseSrc, c);
        const int tex = addTexture(c, addImage(c, "src:" + baseSrc, img, true));
        if (tex >= 0) mr["baseColorTexture"] = textureRef(c, tex, uvScale);
    }

    // Metal/rough: glTF packs both in one image (G=roughness, B=metallic). The
    // document's per-texel roughness remap (mix(lower, upper, factor*sample),
    // audit §1 "Roughness remap") has no glTF equivalent — bake it into the
    // packed texture and set the scalar factors to 1.
    const QString metalSrc = textureSlotSource(pbr, "u_metallicMap");
    const QString roughSrc = textureSlotSource(pbr, "u_roughnessMap");
    if (!metalSrc.isEmpty() || !roughSrc.isEmpty()) {
        const QImage metalImg = metalSrc.isEmpty() ? QImage() : loadDocumentImage(metalSrc, c);
        QImage roughImg = roughSrc.isEmpty() ? QImage() : loadDocumentImage(roughSrc, c);
        int w = 0, h = 0;
        for (const QImage &i : { metalImg, roughImg }) {
            if (!i.isNull()) { w = std::max(w, i.width()); h = std::max(h, i.height()); }
        }
        if (w > 0 && h > 0) {
            const QImage ms = metalImg.isNull() ? QImage()
                : metalImg.scaled(w, h).convertToFormat(QImage::Format_RGBA8888);
            const QImage rs = roughImg.isNull() ? QImage()
                : roughImg.scaled(w, h).convertToFormat(QImage::Format_RGBA8888);
            const float lo = pbr->roughnessLowerBound, hi = pbr->roughnessUpperBound;
            QImage packed(w, h, QImage::Format_RGB888);
            for (int y = 0; y < h; ++y) {
                uchar *dst = packed.scanLine(y);
                const uchar *mline = ms.isNull() ? nullptr : ms.constScanLine(y);
                const uchar *rline = rs.isNull() ? nullptr : rs.constScanLine(y);
                for (int x = 0; x < w; ++x) {
                    float rough;
                    if (rline) {
                        const float sampled = rline[x * 4] / 255.0f;    // R channel
                        const float t = std::min(1.0f, std::max(0.0f, sampled * pbr->roughnessFactor));
                        rough = lo + (hi - lo) * t;                     // deliberate inversion allowed
                    } else {
                        rough = pbr->roughnessFactor;
                    }
                    const float metal = mline ? (mline[x * 4] / 255.0f) * pbr->metallicFactor
                                              : pbr->metallicFactor;
                    dst[x * 3] = 0;
                    dst[x * 3 + 1] = uchar(std::min(1.0f, std::max(0.0f, rough)) * 255.0f + 0.5f);
                    dst[x * 3 + 2] = uchar(std::min(1.0f, std::max(0.0f, metal)) * 255.0f + 0.5f);
                }
            }
            const QString sig = QStringLiteral("mr:%1|%2|%3|%4|%5|%6")
                .arg(metalSrc, roughSrc).arg(lo).arg(hi).arg(pbr->roughnessFactor).arg(pbr->metallicFactor);
            const int tex = addTexture(c, addImage(c, sig, packed, false));
            if (tex >= 0) {
                mr["metallicRoughnessTexture"] = textureRef(c, tex, uvScale);
                mr["metallicFactor"] = 1.0;
                mr["roughnessFactor"] = 1.0;
            }
        }
    }
    if (!mr.contains("metallicFactor")) {
        mr["metallicFactor"] = double(pbr->metallicFactor);
        // No map: approximate the remap by clamping the scalar into the
        // (order-normalised) bounds — exactly what the engine mirror does.
        const float lo = std::min(pbr->roughnessLowerBound, pbr->roughnessUpperBound);
        const float hi = std::max(pbr->roughnessLowerBound, pbr->roughnessUpperBound);
        mr["roughnessFactor"] = double(std::max(lo, std::min(pbr->roughnessFactor, hi)));
    }
    m["pbrMetallicRoughness"] = mr;

    const QString normalSrc = textureSlotSource(pbr, "u_normalMap");
    if (!normalSrc.isEmpty()) {
        const QImage img = loadDocumentImage(normalSrc, c);
        const int tex = addTexture(c, addImage(c, "src:" + normalSrc, img, false));
        if (tex >= 0) {
            QJsonObject ref = textureRef(c, tex, uvScale);
            ref["scale"] = double(pbr->normalFactor);
            m["normalTexture"] = ref;
        }
    }

    // AO: in the document but deliberately dropped by the engine — web export
    // carries what the engine drops (audit §1 "Occlusion").
    const QString aoSrc = textureSlotSource(pbr, "u_occlusionMap");
    if (!aoSrc.isEmpty()) {
        const QImage img = loadDocumentImage(aoSrc, c);
        const int tex = addTexture(c, addImage(c, "src:" + aoSrc, img, true));
        if (tex >= 0) {
            QJsonObject ref = textureRef(c, tex, uvScale);
            ref["strength"] = double(std::min(1.0f, std::max(0.0f, pbr->occlusionFactor)));
            m["occlusionTexture"] = ref;
        }
    }

    const QColor ec = pbr->emissiveColor;
    const float ei = pbr->emissiveIntensity;
    const bool emissiveOn = ei > 0.0f && (ec.redF() + ec.greenF() + ec.blueF()) > 0.0;
    if (emissiveOn) {
        m["emissiveFactor"] = colorArray(float(ec.redF()), float(ec.greenF()), float(ec.blueF()));
        if (ei > 1.0f) {
            c.useExtension("KHR_materials_emissive_strength");
            QJsonObject es; es["emissiveStrength"] = double(ei);
            QJsonObject ext = m["extensions"].toObject();
            ext["KHR_materials_emissive_strength"] = es;
            m["extensions"] = ext;
        } else if (ei < 1.0f) {
            m["emissiveFactor"] = colorArray(float(ec.redF()) * ei, float(ec.greenF()) * ei,
                                             float(ec.blueF()) * ei);
        }
    }
    const QString emSrc = textureSlotSource(pbr, "u_emissiveMap");
    if (!emSrc.isEmpty()) {
        const QImage img = loadDocumentImage(emSrc, c);
        const int tex = addTexture(c, addImage(c, "src:" + emSrc, img, true));
        if (tex >= 0) {
            m["emissiveTexture"] = textureRef(c, tex, uvScale);
            if (!m.contains("emissiveFactor"))
                m["emissiveFactor"] = colorArray(1.0f, 1.0f, 1.0f);
        }
    }

    switch (pbr->alphaMode) {
    case 1:
        m["alphaMode"] = "MASK";
        m["alphaCutoff"] = double(pbr->alphaCutoff);
        break;
    case 2:
        m["alphaMode"] = "BLEND";
        break;
    case 3: {
        // Glass — KHR_materials_transmission: fade diffuse, keep specular, the
        // same semantics as the engine's PbrAlphaMode::Glass (audit §1).
        c.useExtension("KHR_materials_transmission");
        QJsonObject tr;
        tr["transmissionFactor"] = double(std::min(1.0f, std::max(0.0f, 1.0f - pbr->alpha)));
        QJsonObject ext = m["extensions"].toObject();
        ext["KHR_materials_transmission"] = tr;
        m["extensions"] = ext;
        break;
    }
    // Additive/Modulate (IMAGE_PLANE_SPEC §9): no additive/modulate in core
    // glTF — alphaMode falls back to BLEND so any stock viewer still renders
    // a transparent surface, and the real mode rides extras.jah.blendMode for
    // our viewer (three.js AdditiveBlending/MultiplyBlending, viewer.js).
    case 4:
    case 5:
        m["alphaMode"] = "BLEND";
        break;
    default: break;   // OPAQUE is the glTF default
    }

    const bool matTwoSided = pbr->renderStates.rasterState.cullMode == iris::CullMode::None;
    if (matTwoSided || cullMode == iris::FaceCullingMode::None) m["doubleSided"] = true;

    QJsonObject jah;
    jah["useIbl"] = pbr->useIbl;
    jah["iblIntensity"] = double(pbr->iblIntensity);
    if (pbr->alphaMode == 4) jah["blendMode"] = "additive";
    else if (pbr->alphaMode == 5) jah["blendMode"] = "modulate";
    QJsonObject extras; extras["jah"] = jah;
    m["extras"] = extras;

    c.materials.append(m);
    return c.materials.size() - 1;
}

int convertDefaultMaterial(Ctx &c, iris::DefaultMaterial *def, iris::FaceCullingMode cullMode)
{
    // Legacy Blinn-Phong: diffuse -> albedo, shininess -> roughness — the same
    // conversion the mirror applies (scenemirror.cpp).
    QJsonObject m;
    m["name"] = QStringLiteral("default");
    QJsonObject mr;
    const QColor dc = def->getDiffuseColor();
    mr["baseColorFactor"] = colorArray(float(dc.redF()), float(dc.greenF()), float(dc.blueF()), 1.0f);
    mr["metallicFactor"] = 0.0;
    const float shin = std::max(0.0f, std::min(def->getShininess(), 128.0f));
    mr["roughnessFactor"] = double(1.0f - std::sqrt(shin / 128.0f) * 0.9f);
    const QString diffSrc = textureSlotSource(def, "u_diffuseTexture");
    const float uvScale = def->getTextureScale();
    if (!diffSrc.isEmpty()) {
        const QImage img = loadDocumentImage(diffSrc, c);
        const int tex = addTexture(c, addImage(c, "src:" + diffSrc, img, true));
        if (tex >= 0) mr["baseColorTexture"] = textureRef(c, tex, uvScale);
    }
    m["pbrMetallicRoughness"] = mr;
    const QString normalSrc = textureSlotSource(def, "u_normalTexture");
    if (!normalSrc.isEmpty()) {
        const QImage img = loadDocumentImage(normalSrc, c);
        const int tex = addTexture(c, addImage(c, "src:" + normalSrc, img, false));
        if (tex >= 0) m["normalTexture"] = textureRef(c, tex, uvScale);
    }
    if (cullMode == iris::FaceCullingMode::None) m["doubleSided"] = true;
    c.materials.append(m);
    return c.materials.size() - 1;
}

int convertCustomMaterial(Ctx &c, iris::CustomMaterial *custom, iris::FaceCullingMode cullMode)
{
    // Shader-graph material: best-effort PBR fallback exactly like the mirror
    // (colour + shininess/roughness properties, first albedo-ish texture).
    // Full fidelity arrives with the Effects bake phase 2 (audit §1).
    QJsonObject m;
    m["name"] = QStringLiteral("custom");
    QJsonObject mr;
    float r = 0.8f, g = 0.8f, b = 0.8f, metalness = 0.0f, roughness = 0.6f, shininess = -1.0f;
    float uvScale = 1.0f;
    QString albedoSrc, normalSrc;
    for (iris::Property *prop : custom->properties) {
        if (!prop) continue;
        const QVariant v = prop->getValue();
        if (prop->type == iris::PropertyType::Color &&
            (prop->name == "diffuseColor" || prop->name == "color" || prop->name == "albedo" ||
             prop->name == "baseColor")) {
            const QColor col = v.value<QColor>();
            r = float(col.redF()); g = float(col.greenF()); b = float(col.blueF());
        } else if (prop->type == iris::PropertyType::Float && prop->name == "shininess") {
            shininess = v.toFloat();
        } else if (prop->type == iris::PropertyType::Float &&
                   (prop->name == "roughness" || prop->name == "roughnessFactor")) {
            roughness = v.toFloat();
        } else if (prop->type == iris::PropertyType::Float &&
                   (prop->name == "metallic" || prop->name == "metalness")) {
            metalness = v.toFloat();
        } else if (prop->type == iris::PropertyType::Float && prop->name == "textureScale") {
            uvScale = v.toFloat();
        } else if (prop->type == iris::PropertyType::Texture) {
            const QString path = v.toString();
            if (path.isEmpty()) continue;
            if (prop->name == "diffuseTexture" || prop->name == "baseColorMap" || prop->name == "albedoMap")
                albedoSrc = path;
            else if (prop->name == "normalTexture" || prop->name == "normalMap")
                normalSrc = path;
        }
    }
    if (shininess >= 0.0f) {
        const float shin = std::max(0.0f, std::min(shininess, 128.0f));
        roughness = 1.0f - std::sqrt(shin / 128.0f) * 0.9f;
    }
    mr["baseColorFactor"] = colorArray(r, g, b, 1.0f);
    mr["metallicFactor"] = double(metalness);
    mr["roughnessFactor"] = double(roughness);
    if (!albedoSrc.isEmpty()) {
        const QImage img = loadDocumentImage(albedoSrc, c);
        const int tex = addTexture(c, addImage(c, "src:" + albedoSrc, img, true));
        if (tex >= 0) mr["baseColorTexture"] = textureRef(c, tex, uvScale);
    }
    m["pbrMetallicRoughness"] = mr;
    if (!normalSrc.isEmpty()) {
        const QImage img = loadDocumentImage(normalSrc, c);
        const int tex = addTexture(c, addImage(c, "src:" + normalSrc, img, false));
        if (tex >= 0) m["normalTexture"] = textureRef(c, tex, uvScale);
    }
    if (cullMode == iris::FaceCullingMode::None) m["doubleSided"] = true;
    c.materials.append(m);
    return c.materials.size() - 1;
}

int materialFor(Ctx &c, iris::Material *material, iris::FaceCullingMode cullMode)
{
    if (material) {
        auto it = c.materialIndex.constFind(material);
        if (it != c.materialIndex.constEnd()) return it.value();
    }
    int idx = -1;
    if (auto *pbr = dynamic_cast<iris::PbrMaterial *>(material))
        idx = convertPbrMaterial(c, pbr, cullMode);
    else if (auto *custom = dynamic_cast<iris::CustomMaterial *>(material))
        idx = convertCustomMaterial(c, custom, cullMode);
    else if (auto *def = dynamic_cast<iris::DefaultMaterial *>(material))
        idx = convertDefaultMaterial(c, def, cullMode);
    else {
        // Unknown material kinds get one neutral grey — the mirror's fallback.
        QJsonObject m;
        m["name"] = QStringLiteral("fallback");
        QJsonObject mr;
        mr["baseColorFactor"] = colorArray(0.8f, 0.8f, 0.8f, 1.0f);
        mr["metallicFactor"] = 0.0;
        mr["roughnessFactor"] = 0.6;
        m["pbrMetallicRoughness"] = mr;
        c.materials.append(m);
        idx = c.materials.size() - 1;
    }
    if (material && idx >= 0) c.materialIndex.insert(material, idx);
    return idx;
}

// ---- sky bakes -------------------------------------------------------------

// Bake the 3-stop gradient to a narrow equirect strip — the identical ramp the
// engine mirror bakes (scenemirror.cpp applySky, gradient branch).
QImage bakeGradientSky(const iris::ScenePtr &scene)
{
    const float middle = std::min(0.99f, std::max(0.01f, scene->gradientOffset));
    const QColor top = scene->gradientTop, mid = scene->gradientMid, bot = scene->gradientBot;
    const int H = 256, W = 4;
    QImage strip(W, H, QImage::Format_RGB888);
    for (int rIdx = 0; rIdx < H; ++rIdx) {
        const float offset = 1.0f - float(rIdx) / (H - 1);
        float t; const QColor *c0, *c1;
        if (offset <= middle) { t = offset / middle;                   c0 = &bot; c1 = &mid; }
        else                  { t = (offset - middle) / (1 - middle);  c0 = &mid; c1 = &top; }
        const int rr = int(std::min(255.0f, std::max(0.0f, float(c0->redF()   + (c1->redF()   - c0->redF())   * t) * 255.0f)));
        const int gg = int(std::min(255.0f, std::max(0.0f, float(c0->greenF() + (c1->greenF() - c0->greenF()) * t) * 255.0f)));
        const int bb = int(std::min(255.0f, std::max(0.0f, float(c0->blueF()  + (c1->blueF()  - c0->blueF())  * t) * 255.0f)));
        uchar *line = strip.scanLine(rIdx);
        for (int x = 0; x < W; ++x) { line[x * 3] = uchar(rr); line[x * 3 + 1] = uchar(gg); line[x * 3 + 2] = uchar(bb); }
    }
    return strip;
}

// Stitch six cube faces (+X,-X,+Y,-Y,+Z,-Z) into one equirect image so the
// viewer has a single sky path (audit §1 "Sky: cubemap" — pre-stitch, simpler
// viewer). Face basis matches the engine's cubemap sky quads.
QImage stitchCubemapToEquirect(const QImage faces[6], int W = 1024, int H = 512)
{
    QImage sides[6];
    for (int i = 0; i < 6; ++i) {
        if (faces[i].isNull()) return QImage();
        sides[i] = faces[i].convertToFormat(QImage::Format_RGBA8888);
    }
    QImage out(W, H, QImage::Format_RGB888);
    for (int y = 0; y < H; ++y) {
        const float phi = (y + 0.5f) / H * 3.14159265f;      // 0 at zenith
        const float dy = std::cos(phi);
        const float sp = std::sin(phi);
        uchar *line = out.scanLine(y);
        for (int x = 0; x < W; ++x) {
            const float theta = (1.0f - (x + 0.5f) / W) * 6.2831853f;
            const float dx = sp * std::cos(theta);
            const float dz = sp * std::sin(theta);
            // pick dominant axis -> face, project
            const float ax = std::fabs(dx), ay = std::fabs(dy), az = std::fabs(dz);
            int f; float u, v;
            if (ax >= ay && ax >= az) {
                if (dx > 0) { f = 0; u = -dz / ax; v = dy / ax; }
                else        { f = 1; u =  dz / ax; v = dy / ax; }
            } else if (ay >= ax && ay >= az) {
                if (dy > 0) { f = 2; u = dx / ay; v = -dz / ay; }
                else        { f = 3; u = dx / ay; v =  dz / ay; }
            } else {
                if (dz > 0) { f = 4; u =  dx / az; v = dy / az; }
                else        { f = 5; u = -dx / az; v = dy / az; }
            }
            const QImage &face = sides[f];
            const int px = std::min(face.width() - 1,  std::max(0, int((u * 0.5f + 0.5f) * face.width())));
            const int py = std::min(face.height() - 1, std::max(0, int((0.5f - v * 0.5f) * face.height())));
            const uchar *sp2 = face.constScanLine(py) + px * 4;
            line[x * 3] = sp2[0]; line[x * 3 + 1] = sp2[1]; line[x * 3 + 2] = sp2[2];
        }
    }
    return out;
}

QString imageToDataUri(const QImage &img, bool preferJpeg)
{
    if (img.isNull()) return QString();
    const bool jpeg = preferJpeg && !img.hasAlphaChannel();
    const QByteArray bytes = jpeg ? encodeImage(img, "JPEG", 85) : encodeImage(img, "PNG");
    return QStringLiteral("data:image/%1;base64,%2")
        .arg(jpeg ? "jpeg" : "png", QString::fromLatin1(bytes.toBase64()));
}

QJsonObject buildSkyExtras(const iris::ScenePtr &scene, Ctx &c)
{
    QJsonObject sky;
    switch (scene->skyType) {
    case iris::SkyType::SINGLE_COLOR:
        sky["type"] = "color";
        sky["color"] = scene->skyColor.name();
        break;
    case iris::SkyType::GRADIENT:
        sky["type"] = "equirect";
        sky["source"] = "gradient";
        sky["image"] = imageToDataUri(bakeGradientSky(scene), false);
        break;
    case iris::SkyType::EQUIRECTANGULAR: {
        sky["type"] = "equirect";
        sky["source"] = "equirect";
        const QString src = scene->skyTexture ? scene->skyTexture->source : QString();
        const QImage img = loadDocumentImage(src, c);
        if (!img.isNull()) sky["image"] = imageToDataUri(img, true);
        else { sky["type"] = "color"; sky["color"] = scene->skyColor.name();
               c.warnings.append(QStringLiteral("equirect sky image missing: %1").arg(src)); }
        break;
    }
    case iris::SkyType::CUBEMAP: {
        sky["type"] = "equirect";
        sky["source"] = "cubemap";
        QImage stitched;
        if (scene->skyTexture && scene->skyTexture->isCubeMap() && scene->skyTexture->cubeFaces()) {
            stitched = stitchCubemapToEquirect(scene->skyTexture->cubeFaces());
        } else {
            QImage faces[6];
            bool ok = true;
            for (int i = 0; i < 6 && ok; ++i) {
                faces[i] = QImage(scene->skyBoxTextures[i]);
                ok = !faces[i].isNull();
            }
            if (ok) stitched = stitchCubemapToEquirect(faces);
        }
        if (!stitched.isNull()) sky["image"] = imageToDataUri(stitched, true);
        else { sky["type"] = "color"; sky["color"] = scene->skyColor.name();
               c.warnings.append(QStringLiteral("cubemap sky faces missing — exported flat colour")); }
        break;
    }
    case iris::SkyType::REALISTIC: {
        // Parameters ride in extras: three.js's SkyMesh is the same zz85
        // Preetham model with the same parameter names (audit §1).
        const iris::SkyRealistic &s = scene->skyRealistic;
        sky["type"] = "realistic";
        sky["luminance"] = double(s.luminance);
        sky["rayleigh"] = double(s.reileigh);
        sky["mieCoefficient"] = double(s.mieCoefficient);
        sky["mieDirectionalG"] = double(s.mieDirectionalG);
        sky["turbidity"] = double(s.turbidity);
        QJsonArray sun; sun.append(double(s.sunPosX)); sun.append(double(s.sunPosY)); sun.append(double(s.sunPosZ));
        sky["sunPosition"] = sun;
        break;
    }
    case iris::SkyType::MATERIAL:
    default:
        sky["type"] = "color";
        sky["color"] = scene->skyColor.name();
        break;
    }
    return sky;
}

// ---- node helpers ----------------------------------------------------------

void writeTrs(QJsonObject &n, const iris::SceneNodePtr &node)
{
    const QVector3D p = node->getLocalPos();
    if (!p.isNull()) {
        QJsonArray t; t.append(p.x()); t.append(p.y()); t.append(p.z());
        n["translation"] = t;
    }
    const QQuaternion q = node->getLocalRot().normalized();
    if (std::fabs(q.x()) > 1e-7f || std::fabs(q.y()) > 1e-7f || std::fabs(q.z()) > 1e-7f ||
        std::fabs(q.scalar() - 1.0f) > 1e-7f) {
        QJsonArray r; r.append(q.x()); r.append(q.y()); r.append(q.z()); r.append(q.scalar());
        n["rotation"] = r;
    }
    const QVector3D s = node->getLocalScale();
    if (std::fabs(s.x() - 1) > 1e-6f || std::fabs(s.y() - 1) > 1e-6f || std::fabs(s.z() - 1) > 1e-6f) {
        QJsonArray sc; sc.append(s.x()); sc.append(s.y()); sc.append(s.z());
        n["scale"] = sc;
    }
}

QJsonObject shadowExtras(iris::LightNode *light)
{
    QJsonObject sh;
    const iris::ShadowMap *sm = light->shadowMap;
    QString filter = "none";
    if (sm) {
        switch (sm->shadowType) {
        case iris::ShadowMapType::Hard:     filter = "hard"; break;
        case iris::ShadowMapType::Soft:     filter = "soft"; break;
        case iris::ShadowMapType::VerySoft: filter = "verysoft"; break;
        default: break;
        }
    }
    sh["castShadow"] = light->lightType != iris::LightType::Area && filter != "none";
    sh["filter"] = filter;
    sh["mapSize"] = sm ? sm->resolution : 1024;
    sh["bias"] = sm ? double(sm->bias) : 0.0015;
    return sh;
}

// The -Y -> -Z orientation shim: document lights emit along -Y, glTF/three
// lights along -Z. A child node pitched -90 deg about X carries the light —
// the engine mirror's identical trick (audit §1 "Light direction convention").
QJsonObject orientationShimNode(const QString &name)
{
    QJsonObject n;
    n["name"] = name;
    QJsonArray r;
    const float s = 0.70710678f;
    r.append(-s); r.append(0.0); r.append(0.0); r.append(s);   // quat (x,y,z,w): -90 deg about X
    n["rotation"] = r;
    return n;
}

// Traversal, skip semantics (the legacy mesh-only exportable flag) and node
// classification (the CameraNode enum quirk) live in the shared walker layer
// (export/walkers/scenewalker.*) — this writer is one visitor driving it.

} // namespace

// ---- the exporter ----------------------------------------------------------

GltfExporter::Result GltfExporter::exportScene(const iris::ScenePtr &scene, const QString &sceneName)
{
    Result res;
    if (!scene || !scene->rootNode) {
        res.error = QStringLiteral("no scene");
        return res;
    }

    Ctx c;
    QJsonArray sceneRoots;

    // Skeletal skins/animations are collected while walking mesh nodes and
    // resolved after the node array is complete (joint nodes are appended).
    struct PendingSkin
    {
        int meshNodeIndex;
        iris::MeshPtr mesh;
        MeshBuffers buffers;   // holds JOINTS/WEIGHTS float data
    };
    std::vector<PendingSkin> pendingSkins;

    // The walker visits post-order (children first) and hands us the already-
    // written children's node indices; light shim nodes are prepended so the
    // children array keeps its historical order (shim, then walked children).
    const exportwalk::NodeVisitor writeNode =
        [&](const iris::SceneNodePtr &node, const QVector<int> &childHandles) -> int {
        QJsonObject n;
        n["name"] = node->getName().isEmpty() ? QStringLiteral("node") : node->getName();
        writeTrs(n, node);

        QJsonObject jah;
        jah["guid"] = node->getGUID();
        if (!node->isVisible()) jah["visible"] = false;

        int myIndexReserved = -1;   // filled at the end; children need our index order
        QJsonArray children;

        using exportwalk::NodeKind;
        const NodeKind kind = exportwalk::classifyNode(node);
        if (kind == NodeKind::Mesh) {
            auto *meshNode = static_cast<iris::MeshNode *>(node.data());
            iris::MeshPtr mesh = meshNode->getMesh();
            if (mesh) {
                int meshIdx = -1;
                auto found = c.meshIndex.constFind(mesh.data());
                const int matIdx = materialFor(c, meshNode->getMaterial().data(),
                                               meshNode->getFaceCullingMode());
                if (found != c.meshIndex.constEnd()) {
                    meshIdx = found.value();
                } else {
                    MeshBuffers mb;
                    if (extractMeshBuffers(mesh.data(), mb)) {
                        if (mb.normals.empty()) generateNormals(mb);
                        generateTangents(mb);
                        QJsonObject prim;
                        QJsonObject attrs;
                        attrs["POSITION"] = addAccessor(c, mb.positions, 3, "VEC3", true);
                        attrs["NORMAL"] = addAccessor(c, mb.normals, 3, "VEC3");
                        attrs["TANGENT"] = addAccessor(c, mb.tangents, 4, "VEC4");
                        if (!mb.uvs.empty())
                            attrs["TEXCOORD_0"] = addAccessor(c, mb.uvs, 2, "VEC2");
                        const bool skinned = mesh->hasSkeleton() && !mb.boneIndices.empty();
                        if (skinned) {
                            std::vector<unsigned short> joints(mb.boneIndices.size());
                            for (size_t i = 0; i < mb.boneIndices.size(); ++i)
                                joints[i] = static_cast<unsigned short>(std::max(0.0f, mb.boneIndices[i]));
                            attrs["JOINTS_0"] = addUShortAccessor(c, joints, 4, "VEC4");
                            attrs["WEIGHTS_0"] = addAccessor(c, mb.boneWeights, 4, "VEC4");
                        }
                        prim["attributes"] = attrs;
                        prim["indices"] = addIndexAccessor(c, mb.indices);
                        if (matIdx >= 0) prim["material"] = matIdx;
                        QJsonObject gm;
                        gm["name"] = n["name"];
                        QJsonArray prims; prims.append(prim);
                        gm["primitives"] = prims;
                        c.meshes.append(gm);
                        meshIdx = c.meshes.size() - 1;
                        c.meshIndex.insert(mesh.data(), meshIdx);
                        if (skinned)
                            pendingSkins.push_back({ -1 /* patched below */, mesh, std::move(mb) });
                    } else {
                        c.warnings.append(QStringLiteral("mesh '%1' has no exportable geometry")
                                              .arg(node->getName()));
                    }
                }
                if (meshIdx >= 0) n["mesh"] = meshIdx;
            }
        } else if (kind == NodeKind::Light) {
            auto *light = static_cast<iris::LightNode *>(node.data());
            if (light->lightType == iris::LightType::Area) {
                // No ratified glTF area-light extension — extras + a viewer-side
                // RectAreaLight on the orientation shim (audit §1 "Area lights").
                QJsonObject area;
                area["width"] = double(light->rectWidth);
                area["height"] = double(light->rectHeight);
                area["color"] = light->color.name();
                area["intensity"] = double(light->intensity * kLightIntensityScale);
                area["doubleSided"] = light->doubleSided;
                area["accurate"] = light->accurate;
                QJsonObject shim = orientationShimNode(node->getName() + ".orient");
                QJsonObject shimJah; shimJah["areaLight"] = area;
                QJsonObject shimExtras; shimExtras["jah"] = shimJah;
                shim["extras"] = shimExtras;
                c.nodes.append(shim);
                children.append(c.nodes.size() - 1);
                res.lightCount++;
            } else {
                c.useExtension("KHR_lights_punctual");
                QJsonObject l;
                switch (light->lightType) {
                case iris::LightType::Directional: l["type"] = "directional"; break;
                case iris::LightType::Spot:        l["type"] = "spot"; break;
                default:                           l["type"] = "point"; break;
                }
                l["color"] = colorArray(float(light->color.redF()), float(light->color.greenF()),
                                        float(light->color.blueF()));
                l["intensity"] = double(light->intensity * kLightIntensityScale);
                if (light->lightType != iris::LightType::Directional && light->distance > 0.0f)
                    l["range"] = double(light->distance);
                if (light->lightType == iris::LightType::Spot) {
                    QJsonObject spot;
                    const float inner = std::max(0.1f, light->spotCutOff);
                    const float outer = std::min(89.9f, inner + std::max(0.0f, light->spotCutOffSoftness));
                    spot["innerConeAngle"] = double(qDegreesToRadians(std::min(inner, outer - 0.01f)));
                    spot["outerConeAngle"] = double(qDegreesToRadians(outer));
                    l["spot"] = spot;
                }
                c.lights.append(l);
                const int lightIdx = c.lights.size() - 1;
                QJsonObject shim = orientationShimNode(node->getName() + ".orient");
                QJsonObject shimExt;
                QJsonObject ref; ref["light"] = lightIdx;
                shimExt["KHR_lights_punctual"] = ref;
                shim["extensions"] = shimExt;
                QJsonObject shimJah; shimJah["shadow"] = shadowExtras(light);
                QJsonObject shimExtras; shimExtras["jah"] = shimJah;
                shim["extras"] = shimExtras;
                c.nodes.append(shim);
                children.append(c.nodes.size() - 1);
                res.lightCount++;
            }
        } else if (kind == NodeKind::Camera) {
            // classifyNode uses dynamic_cast, not the type enum: CameraNode
            // never writes sceneNodeType (document quirk — nothing in irisgl
            // sets SceneNodeType::Camera), so the enum still reads Empty.
            auto *cam = static_cast<iris::CameraNode *>(node.data());
            QJsonObject gc;
            gc["name"] = n["name"];
            if (cam->isPerspective) {
                gc["type"] = "perspective";
                QJsonObject p;
                p["yfov"] = double(qDegreesToRadians(std::max(1.0f, cam->angle)));
                p["znear"] = double(std::max(0.001f, cam->nearClip));
                p["zfar"] = double(std::max(cam->nearClip + 0.01f, cam->farClip));
                gc["perspective"] = p;
            } else {
                gc["type"] = "orthographic";
                QJsonObject o;
                o["xmag"] = double(std::max(0.01f, cam->orthoSize));
                o["ymag"] = double(std::max(0.01f, cam->orthoSize));
                o["znear"] = double(std::max(0.001f, cam->nearClip));
                o["zfar"] = double(std::max(cam->nearClip + 0.01f, cam->farClip));
                gc["orthographic"] = o;
            }
            c.cameras.append(gc);
            n["camera"] = c.cameras.size() - 1;
            res.cameraCount++;
        } else if (kind == NodeKind::ParticleSystem) {
            auto *ps = static_cast<iris::ParticleSystemNode *>(node.data());
            QJsonObject p;
            p["particlesPerSecond"] = double(ps->particlesPerSecond);
            p["speed"] = double(ps->speed);
            p["lifeLength"] = double(ps->lifeLength);
            p["particleScale"] = double(ps->particleScale);
            p["gravityComplement"] = double(ps->gravityComplement);
            p["speedError"] = double(ps->speedError);
            p["lifeError"] = double(ps->lifeError);
            p["scaleError"] = double(ps->scaleError);
            p["randomRotation"] = ps->randomRotation;
            p["dissipate"] = ps->dissipate;
            p["dissipateInv"] = ps->dissipateInv;
            p["useAdditive"] = ps->useAdditive;
            p["maxParticles"] = ps->maxParticles;
            if (ps->texture && !ps->texture->source.isEmpty()) {
                const QImage img = loadDocumentImage(ps->texture->source, c);
                if (!img.isNull()) p["texture"] = imageToDataUri(img, false);
            }
            jah["particles"] = p;
        } else if (kind == NodeKind::Viewer) {
            jah["viewpoint"] = true;
        }

        for (int ci : childHandles) children.append(ci);

        if (!children.isEmpty()) n["children"] = children;
        QJsonObject extras; extras["jah"] = jah;
        n["extras"] = extras;
        c.nodes.append(n);
        myIndexReserved = c.nodes.size() - 1;
        // Patch the skin record with the mesh node's index (added just now).
        for (auto &ps : pendingSkins)
            if (ps.meshNodeIndex < 0 && kind == NodeKind::Mesh)
                ps.meshNodeIndex = myIndexReserved;
        return myIndexReserved;
    };

    for (int idx : exportwalk::walkScene(scene, writeNode))
        sceneRoots.append(idx);

    // ---- skins + skeletal animations (phase 2) ----
    for (auto &ps : pendingSkins) {
        if (ps.meshNodeIndex < 0 || !ps.mesh->hasSkeleton()) continue;
        iris::SkeletonPtr skel = ps.mesh->getSkeleton();
        if (!skel || skel->bones.isEmpty()) continue;

        // One glTF node per bone, carrying its BIND local TRS (Bone::binding*,
        // which Mesh::extractSkeleton fills). The animation channels below are
        // bone-local and ABSOLUTE, which is glTF's own convention — deliberately
        // not the bind-relative delta form an Ogre skeleton wants; the two live
        // in different places and must not be confused.
        QHash<QString, int> boneNode;
        QJsonArray joints;
        std::vector<float> ibm;
        ibm.reserve(size_t(skel->bones.size()) * 16);
        for (const auto &bone : skel->bones) {
            QJsonObject bn;
            bn["name"] = bone->name;
            const QVector3D bp = bone->bindingPos;
            const QQuaternion br = bone->bindingRot.normalized();
            const QVector3D bs = bone->bindingScale;
            QJsonArray t; t.append(bp.x()); t.append(bp.y()); t.append(bp.z());
            bn["translation"] = t;
            QJsonArray r; r.append(br.x()); r.append(br.y()); r.append(br.z()); r.append(br.scalar());
            bn["rotation"] = r;
            QJsonArray s; s.append(bs.x() == 0 ? 1.0f : bs.x());
            s.append(bs.y() == 0 ? 1.0f : bs.y());
            s.append(bs.z() == 0 ? 1.0f : bs.z());
            bn["scale"] = s;
            c.nodes.append(bn);
            const int bIdx = c.nodes.size() - 1;
            boneNode.insert(bone->name, bIdx);
            joints.append(bIdx);
            const float *m = bone->inverseMeshSpacePoseMatrix.constData();   // column-major, glTF order
            for (int k = 0; k < 16; ++k) ibm.push_back(m[k]);
        }
        // Wire the bone hierarchy; roots attach under the mesh node's parent
        // frame — glTF only requires joints to be in the scene graph.
        QJsonArray skinRoots;
        for (const auto &bone : skel->bones) {
            const int bIdx = boneNode.value(bone->name);
            if (bone->parentBone) {
                const int pIdx = boneNode.value(bone->parentBone->name, -1);
                if (pIdx >= 0) {
                    QJsonObject parent = c.nodes.at(pIdx).toObject();
                    QJsonArray kids = parent["children"].toArray();
                    kids.append(bIdx);
                    parent["children"] = kids;
                    c.nodes.replace(pIdx, parent);
                    continue;
                }
            }
            skinRoots.append(bIdx);
        }
        {
            QJsonObject meshNode = c.nodes.at(ps.meshNodeIndex).toObject();
            QJsonArray kids = meshNode["children"].toArray();
            for (const auto &r : skinRoots) kids.append(r);
            meshNode["children"] = kids;
            QJsonObject skin;
            skin["inverseBindMatrices"] = addAccessor(c, ibm, 16, "MAT4");
            skin["joints"] = joints;
            if (!skinRoots.isEmpty()) skin["skeleton"] = skinRoots.first();
            c.skins.append(skin);
            meshNode["skin"] = c.skins.size() - 1;
            c.nodes.replace(ps.meshNodeIndex, meshNode);
        }

        // Skeletal animations: LINEAR samplers per bone T/R/S — matches the
        // document's interpolation (audit §1 "Skeletal animations"; keys are
        // read from the LIVE document because SceneWriter persists only refs).
        const auto anims = ps.mesh->getSkeletalAnimations();
        for (auto it = anims.constBegin(); it != anims.constEnd(); ++it) {
            const iris::SkeletalAnimationPtr &anim = it.value();
            if (!anim) continue;
            QJsonArray channels, samplers;
            for (auto bIt = anim->boneAnimations.constBegin();
                 bIt != anim->boneAnimations.constEnd(); ++bIt) {
                const int nodeIdx = boneNode.value(bIt.key(), -1);
                if (nodeIdx < 0) continue;
                const auto &ba = bIt.value();
                auto addChannel = [&](const char *path, const std::vector<float> &times,
                                      const std::vector<float> &values, int comps) {
                    if (times.empty()) return;
                    QJsonObject sampler;
                    sampler["input"] = addAccessor(c, times, 1, "SCALAR", true);
                    sampler["output"] = addAccessor(c, values, comps,
                                                    comps == 4 ? "VEC4" : "VEC3");
                    sampler["interpolation"] = "LINEAR";
                    samplers.append(sampler);
                    QJsonObject channel;
                    channel["sampler"] = samplers.size() - 1;
                    QJsonObject target;
                    target["node"] = nodeIdx;
                    target["path"] = path;
                    channel["target"] = target;
                    channels.append(channel);
                };
                if (ba->posKeys && !ba->posKeys->keys.isEmpty()) {
                    std::vector<float> t, v;
                    for (const auto *k : ba->posKeys->keys) {
                        t.push_back(float(k->time));
                        v.push_back(k->value.x()); v.push_back(k->value.y()); v.push_back(k->value.z());
                    }
                    addChannel("translation", t, v, 3);
                }
                if (ba->rotKeys && !ba->rotKeys->keys.isEmpty()) {
                    std::vector<float> t, v;
                    for (const auto *k : ba->rotKeys->keys) {
                        t.push_back(float(k->time));
                        const QQuaternion q = k->value.normalized();
                        v.push_back(q.x()); v.push_back(q.y()); v.push_back(q.z()); v.push_back(q.scalar());
                    }
                    addChannel("rotation", t, v, 4);
                }
                if (ba->scaleKeys && !ba->scaleKeys->keys.isEmpty()) {
                    std::vector<float> t, v;
                    for (const auto *k : ba->scaleKeys->keys) {
                        t.push_back(float(k->time));
                        v.push_back(k->value.x()); v.push_back(k->value.y()); v.push_back(k->value.z());
                    }
                    addChannel("scale", t, v, 3);
                }
            }
            if (!channels.isEmpty()) {
                QJsonObject a;
                a["name"] = it.key().isEmpty() ? QStringLiteral("animation") : it.key();
                a["channels"] = channels;
                a["samplers"] = samplers;
                c.animations.append(a);
            }
        }
    }

    // ---- scene-level extras (jah sidecar) ----
    QJsonObject jahScene;
    jahScene["sky"] = buildSkyExtras(scene, c);
    if (scene->fogEnabled) {
        QJsonObject fog;
        fog["color"] = scene->fogColor.name();
        fog["start"] = double(scene->fogStart);
        fog["end"] = double(scene->fogEnd);
        jahScene["fog"] = fog;
    }
    jahScene["shadowEnabled"] = scene->shadowEnabled;
    jahScene["ambientColor"] = scene->ambientColor.name();
    jahScene["antiAliasing"] = scene->antiAliasing;
    if (scene->giMode != iris::GiMode::OFF)
        jahScene["gi"] = QStringLiteral("engine-only (mode %1)").arg(int(scene->giMode));
    if (!scene->ambientMusicPath.isEmpty()) {
        QJsonObject audio;
        audio["file"] = QFileInfo(scene->ambientMusicPath).fileName();
        audio["volume"] = double(scene->ambientMusicVolume);
        jahScene["audio"] = audio;
        res.audioSourcePath = scene->ambientMusicPath;
    }

    // ---- assemble the JSON document ----
    QJsonObject root;
    QJsonObject asset;
    asset["version"] = "2.0";
    asset["generator"] = "Jahshaka Web Export";
    root["asset"] = asset;

    QJsonObject gscene;
    gscene["name"] = sceneName.isEmpty() ? QStringLiteral("scene") : sceneName;
    gscene["nodes"] = sceneRoots;
    QJsonObject sceneExtras; sceneExtras["jah"] = jahScene;
    gscene["extras"] = sceneExtras;
    QJsonArray scenes; scenes.append(gscene);
    root["scenes"] = scenes;
    root["scene"] = 0;

    if (!c.nodes.isEmpty()) root["nodes"] = c.nodes;
    if (!c.meshes.isEmpty()) root["meshes"] = c.meshes;
    if (!c.accessors.isEmpty()) root["accessors"] = c.accessors;
    if (!c.bufferViews.isEmpty()) root["bufferViews"] = c.bufferViews;
    if (!c.materials.isEmpty()) root["materials"] = c.materials;
    if (!c.textures.isEmpty()) root["textures"] = c.textures;
    if (!c.images.isEmpty()) root["images"] = c.images;
    if (!c.samplers.isEmpty()) root["samplers"] = c.samplers;
    if (!c.cameras.isEmpty()) root["cameras"] = c.cameras;
    if (!c.skins.isEmpty()) root["skins"] = c.skins;
    if (!c.animations.isEmpty()) root["animations"] = c.animations;
    if (!c.lights.isEmpty()) {
        QJsonObject punctual; punctual["lights"] = c.lights;
        QJsonObject ext; ext["KHR_lights_punctual"] = punctual;
        root["extensions"] = ext;
    }
    if (!c.extensions.isEmpty()) {
        QJsonArray used;
        for (const QString &e : c.extensions) used.append(e);
        root["extensionsUsed"] = used;
    }

    c.bin.align(4);
    if (!c.bin.bytes.isEmpty()) {
        QJsonObject buffer;
        buffer["byteLength"] = c.bin.bytes.size();
        QJsonArray buffers; buffers.append(buffer);
        root["buffers"] = buffers;
    }

    // ---- GLB container ----
    QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    while (json.size() % 4) json.append(' ');

    QByteArray glb;
    auto appendU32 = [&glb](quint32 v) {
        char b[4] = { char(v & 0xff), char((v >> 8) & 0xff), char((v >> 16) & 0xff), char((v >> 24) & 0xff) };
        glb.append(b, 4);
    };
    const bool hasBin = !c.bin.bytes.isEmpty();
    const quint32 total = 12 + 8 + quint32(json.size()) + (hasBin ? 8 + quint32(c.bin.bytes.size()) : 0);
    appendU32(0x46546C67);   // 'glTF'
    appendU32(2);
    appendU32(total);
    appendU32(quint32(json.size()));
    appendU32(0x4E4F534A);   // 'JSON'
    glb.append(json);
    if (hasBin) {
        appendU32(quint32(c.bin.bytes.size()));
        appendU32(0x004E4942);   // 'BIN\0'
        glb.append(c.bin.bytes);
    }

    res.ok = true;
    res.glb = glb;
    res.json = root;
    res.warnings = c.warnings;
    res.extensionsUsed = c.extensions;
    res.nodeCount = c.nodes.size();
    res.meshCount = c.meshes.size();
    res.materialCount = c.materials.size();
    res.cameraCount = c.cameras.size();
    res.animationCount = c.animations.size();
    res.skinCount = c.skins.size();
    return res;
}
