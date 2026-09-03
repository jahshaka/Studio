// Web export tests (WEB_EXPORT_AUDIT phases 0-1): document scene -> GLB via
// the hand-rolled GltfExporter, and the full export folder via ExportService.
// Structural glTF assertions run against the writer's own JSON; the GLB
// container bytes are validated by hand. No Ogre, no GL, no assimp export —
// runs offscreen like the document suite.

#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/animation/skeletalanimation.h"

#include "export/gltfexporter.h"
#include "export/exportservice.h"
#include "export/previewlauncher.h"

static int failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) { std::printf("ok: %s\n", msg); }                          \
        else      { std::printf("FAIL: %s\n", msg); ++failures; }            \
    } while (0)

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");

    // ---- build a document scene that exercises the coverage table ----
    auto scene = iris::Scene::create();

    // textured PBR cube (tests tangents, texture transform, metal/rough pack)
    auto cube = iris::MeshNode::create();
    cube->setName("cube");
    cube->setMesh(":assets/models/cube.obj");
    CHECK(!!cube->getMesh(), "cube.obj loaded into the document");
    auto pbr = iris::PbrMaterial::create();
    pbr->setBaseColor(QColor(200, 40, 40));
    pbr->setTextureScale(2.0f);
    pbr->setEmissiveColor(QColor(0, 255, 0));
    pbr->setEmissiveIntensity(3.0f);
    {
        // a real roughness map on disk so the channel-pack path runs
        QImage rough(8, 8, QImage::Format_RGB888);
        rough.fill(QColor(128, 128, 128));
        const QString roughPath = tmp.filePath("rough.png");
        rough.save(roughPath);
        pbr->setRoughnessMap(iris::Texture2D::load(roughPath));
        pbr->roughnessFactor = 1.0f;
        pbr->roughnessLowerBound = 0.2f;
        pbr->roughnessUpperBound = 0.8f;
    }
    cube->setMaterial(pbr);
    scene->rootNode->addChild(cube);

    // glass sphere (transmission)
    auto glass = iris::MeshNode::create();
    glass->setName("glass");
    glass->setMesh(":assets/models/cube.obj");
    auto glassMat = iris::PbrMaterial::create();
    glassMat->setAlphaMode(3);
    glassMat->setAlpha(0.2f);
    glass->setMaterial(glassMat);
    scene->rootNode->addChild(glass);

    // additive + modulate (Unreal-parity blend modes, IMAGE_PLANE_SPEC §9:
    // no core glTF equivalent — alphaMode falls back to BLEND, the real mode
    // rides extras.jah.blendMode for the viewer)
    auto additive = iris::MeshNode::create();
    additive->setName("additive");
    additive->setMesh(":assets/models/cube.obj");
    auto additiveMat = iris::PbrMaterial::create();
    additiveMat->setAlphaMode(4);
    additiveMat->setAlpha(0.6f);
    additive->setMaterial(additiveMat);
    scene->rootNode->addChild(additive);

    auto modulate = iris::MeshNode::create();
    modulate->setName("modulate");
    modulate->setMesh(":assets/models/cube.obj");
    auto modulateMat = iris::PbrMaterial::create();
    modulateMat->setAlphaMode(5);
    modulateMat->setAlpha(0.3f);   // ignored: modulate has no alpha semantics
    modulate->setMaterial(modulateMat);
    scene->rootNode->addChild(modulate);

    // lights: point + spot (+softness) + area
    auto point = iris::LightNode::create();
    point->setName("point");
    point->setLightType(iris::LightType::Point);
    point->intensity = 2.0f;
    point->distance = 30.0f;
    scene->rootNode->addChild(point);

    auto spot = iris::LightNode::create();
    spot->setName("spot");
    spot->setLightType(iris::LightType::Spot);
    spot->spotCutOff = 30.0f;
    spot->spotCutOffSoftness = 10.0f;
    scene->rootNode->addChild(spot);

    auto area = iris::LightNode::create();
    area->setName("area");
    area->setLightType(iris::LightType::Area);
    area->rectWidth = 2.0f;
    area->rectHeight = 1.0f;
    scene->rootNode->addChild(area);

    // camera
    auto cam = iris::CameraNode::create();
    cam->setName("camera");
    scene->rootNode->addChild(cam);

    // environment: gradient sky + fog
    scene->skyType = iris::SkyType::GRADIENT;
    scene->gradientTop = QColor(10, 20, 80);
    scene->gradientMid = QColor(120, 140, 200);
    scene->gradientBot = QColor(240, 230, 200);
    scene->gradientOffset = 0.5f;
    scene->fogEnabled = true;
    scene->fogColor = QColor(180, 180, 190);
    scene->fogStart = 10.0f;
    scene->fogEnd = 200.0f;

    // ---- run the writer ----
    const auto g = GltfExporter::exportScene(scene, "Test Scene");
    CHECK(g.ok, "exportScene ok");
    CHECK(!g.glb.isEmpty(), "GLB bytes produced");

    // GLB container: magic, version, declared length
    CHECK(g.glb.size() >= 20 && std::memcmp(g.glb.constData(), "glTF", 4) == 0, "GLB magic");
    {
        quint32 version = 0, total = 0;
        std::memcpy(&version, g.glb.constData() + 4, 4);
        std::memcpy(&total, g.glb.constData() + 8, 4);
        CHECK(version == 2, "GLB version 2");
        CHECK(qint64(total) == g.glb.size(), "GLB total length matches");
    }

    const QJsonObject root = g.json;
    CHECK(root["asset"].toObject()["version"].toString() == "2.0", "asset.version 2.0");

    // nodes: 2 meshes + 2 punctual shims + 2 punctual lights... count explicitly:
    // cube, glass, point(+shim), spot(+shim), area(+shim), camera = 9
    const QJsonArray nodes = root["nodes"].toArray();
    CHECK(nodes.size() == 11, "11 nodes (8 document + 3 orientation shims)");
    CHECK(root["meshes"].toArray().size() == 4, "4 meshes");
    CHECK(root["materials"].toArray().size() == 4, "4 materials");

    // additive/modulate ride extras.jah.blendMode with a BLEND core fallback
    {
        bool foundAdditive = false, foundModulate = false;
        for (const auto &mv : root["materials"].toArray()) {
            const QJsonObject m = mv.toObject();
            const QString blend = m["extras"].toObject()["jah"].toObject()["blendMode"].toString();
            const QJsonArray bcf = m["pbrMetallicRoughness"].toObject()["baseColorFactor"].toArray();
            if (blend == "additive") {
                foundAdditive = true;
                CHECK(m["alphaMode"].toString() == "BLEND", "additive: core alphaMode falls back to BLEND");
                CHECK(bcf.size() == 4 && std::abs(bcf[3].toDouble() - 0.6) < 0.001,
                      "additive: alpha carries into baseColorFactor.A (scales the glow)");
            } else if (blend == "modulate") {
                foundModulate = true;
                CHECK(m["alphaMode"].toString() == "BLEND", "modulate: core alphaMode falls back to BLEND");
                CHECK(bcf.size() == 4 && std::abs(bcf[3].toDouble() - 1.0) < 0.001,
                      "modulate: alpha ignored (baseColorFactor.A stays 1)");
            }
        }
        CHECK(foundAdditive, "additive material writes extras.jah.blendMode");
        CHECK(foundModulate, "modulate material writes extras.jah.blendMode");
    }
    CHECK(root["cameras"].toArray().size() == 1, "1 camera");
    CHECK(g.lightCount == 3, "3 lights counted");

    // extensions
    const QJsonArray used = root["extensionsUsed"].toArray();
    auto hasExt = [&used](const char *name) {
        for (const auto &v : used) if (v.toString() == name) return true;
        return false;
    };
    CHECK(hasExt("KHR_lights_punctual"), "KHR_lights_punctual used");
    CHECK(hasExt("KHR_materials_transmission"), "KHR_materials_transmission used (glass)");
    CHECK(hasExt("KHR_materials_emissive_strength"), "KHR_materials_emissive_strength used");
    CHECK(hasExt("KHR_texture_transform"), "KHR_texture_transform used (textureScale=2)");

    const QJsonArray lights = root["extensions"].toObject()["KHR_lights_punctual"]
                                  .toObject()["lights"].toArray();
    CHECK(lights.size() == 2, "2 punctual lights (area rides extras)");
    bool spotOk = false;
    for (const auto &lv : lights) {
        const QJsonObject l = lv.toObject();
        if (l["type"].toString() == "spot") {
            const QJsonObject sp = l["spot"].toObject();
            spotOk = sp["innerConeAngle"].toDouble() < sp["outerConeAngle"].toDouble();
        }
    }
    CHECK(spotOk, "spot inner < outer cone (softness applied)");

    // area light + shadow settings live on the shim nodes' extras
    bool foundArea = false, foundShadow = false, foundShim = false;
    for (const auto &nv : nodes) {
        const QJsonObject n = nv.toObject();
        const QJsonObject jah = n["extras"].toObject()["jah"].toObject();
        if (jah.contains("areaLight")) {
            foundArea = jah["areaLight"].toObject()["width"].toDouble() == 2.0;
        }
        if (jah.contains("shadow")) foundShadow = true;
        if (n.contains("rotation") && n["name"].toString().endsWith(".orient")) foundShim = true;
    }
    CHECK(foundArea, "area light in extras (width=2)");
    CHECK(foundShadow, "per-light shadow settings in extras");
    CHECK(foundShim, "-Y to -Z orientation shim nodes present");

    // mesh attributes: TANGENT float4 generated, POSITION has min/max
    {
        const QJsonObject prim = root["meshes"].toArray().first().toObject()["primitives"]
                                     .toArray().first().toObject();
        const QJsonObject attrs = prim["attributes"].toObject();
        CHECK(attrs.contains("POSITION") && attrs.contains("NORMAL") && attrs.contains("TANGENT"),
              "POSITION/NORMAL/TANGENT attributes");
        const QJsonArray accessors = root["accessors"].toArray();
        const QJsonObject posAcc = accessors.at(attrs["POSITION"].toInt()).toObject();
        CHECK(posAcc.contains("min") && posAcc.contains("max"), "POSITION accessor min/max");
        // every accessor's bufferView must exist and stay inside the BIN chunk
        const QJsonArray bufferViews = root["bufferViews"].toArray();
        CHECK(!bufferViews.isEmpty(), "bufferViews array present");
        const qint64 binLen = root["buffers"].toArray().first().toObject()["byteLength"].toInteger();
        bool bvOk = !accessors.isEmpty();
        for (const auto &av : accessors) {
            const int bvIdx = av.toObject()["bufferView"].toInt(-1);
            if (bvIdx < 0 || bvIdx >= bufferViews.size()) { bvOk = false; break; }
            const QJsonObject bv = bufferViews.at(bvIdx).toObject();
            if (bv["byteOffset"].toInteger() + bv["byteLength"].toInteger() > binLen) { bvOk = false; break; }
        }
        CHECK(bvOk, "all accessors resolve to in-bounds bufferViews");
        const QJsonObject tanAcc = accessors.at(attrs["TANGENT"].toInt()).toObject();
        CHECK(tanAcc["type"].toString() == "VEC4", "TANGENT is VEC4 (handedness)");
        CHECK(prim.contains("indices"), "indexed primitive");
        CHECK(prim.contains("material"), "primitive has material");
    }

    // materials: packed metal/rough texture with factors reset to 1
    {
        const QJsonArray mats = root["materials"].toArray();
        bool packed = false, transmission = false;
        for (const auto &mv : mats) {
            const QJsonObject m = mv.toObject();
            const QJsonObject mr = m["pbrMetallicRoughness"].toObject();
            if (mr.contains("metallicRoughnessTexture") &&
                mr["roughnessFactor"].toDouble() == 1.0) packed = true;
            if (m["extensions"].toObject().contains("KHR_materials_transmission")) transmission = true;
        }
        CHECK(packed, "metal/rough channel-packed texture (remap baked, factor=1)");
        CHECK(transmission, "glass material carries transmission");
    }

    // scene extras: sky + fog sidecar
    {
        const QJsonObject jah = root["scenes"].toArray().first().toObject()["extras"]
                                    .toObject()["jah"].toObject();
        const QJsonObject sky = jah["sky"].toObject();
        CHECK(sky["type"].toString() == "equirect" && sky["source"].toString() == "gradient",
              "gradient sky baked to equirect extras");
        CHECK(sky["image"].toString().startsWith("data:image/"), "sky image is a data URI");
        CHECK(jah["fog"].toObject()["end"].toDouble() == 200.0, "fog extras");
    }

    // ---- ExportService: the full folder ----
    const QString outDir = tmp.filePath("web");
    const auto r = ExportService::exportWeb(scene, "Test Scene", outDir);
    CHECK(r.ok, "exportWeb ok");
    CHECK(QFile::exists(r.indexHtml), "index.html written");
    CHECK(QFile::exists(r.viewerHtml), "viewer.html written");
    CHECK(QFile::exists(r.glbPath), "scene.glb written");
    CHECK(QFile::exists(QDir(outDir).filePath("README.txt")), "README.txt written");
    CHECK(r.inlined, "scene inlined (under the 75MB ceiling)");
    {
        QFile f(r.indexHtml);
        f.open(QIODevice::ReadOnly);
        const QByteArray html = f.readAll();
        CHECK(html.contains("Z2xURg"), "index.html embeds the GLB (base64 'glTF' magic)");
        CHECK(html.contains("WebGPURenderer"), "index.html embeds the three.js WebGPU bundle");
        CHECK(html.contains("navigator.gpu"), "index.html embeds the viewer (WebGPU gate)");
        CHECK(html.contains("Test Scene"), "index.html carries the scene title");
        CHECK(qint64(html.size()) == r.indexSize, "reported index size matches");
    }
    {
        QFile f(r.glbPath);
        f.open(QIODevice::ReadOnly);
        CHECK(f.size() == r.glbSize && f.size() == g.glb.size(), "scene.glb size matches the writer");
        f.close();
    }
    {
        QFile f(r.viewerHtml);
        f.open(QIODevice::ReadOnly);
        const QByteArray html = f.readAll();
        // the literal "glbBase64" appears in viewer.js code; the payload does not
        CHECK(!html.contains("Z2xURg"), "viewer.html fetches (no embedded scene payload)");
    }

    // ---- skeletal export (audit phase 2): programmatic two-bone arm ----
    // Mirrors the skeletal suite's arm: 6 verts, 2 bones, a 1s swing on the tip.
    {
        const float hw = 0.15f;
        const float positions[] = { -hw,0,0, hw,0,0, -hw,1,0, hw,1,0, -hw,2,0, hw,2,0 };
        const float normals[]   = { 0,0,1, 0,0,1, 0,0,1, 0,0,1, 0,0,1, 0,0,1 };
        const float boneIdx[]   = { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 1,0,0,0, 1,0,0,0 };
        const float boneW[]     = { 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0, 1,0,0,0 };
        const unsigned indices[] = { 0,1,3, 0,3,2, 2,3,5, 2,5,4 };

        auto armMesh = iris::Mesh::create();
        auto addBuf = [&armMesh](iris::VertexAttribUsage usage, const float *data, int floats, int comps) {
            iris::VertexLayout layout;
            layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
            auto vb = iris::VertexBuffer::create(layout);
            vb->setData(const_cast<float *>(data), unsigned(floats * sizeof(float)));
            armMesh->addVertexBuffer(vb);
        };
        addBuf(iris::VertexAttribUsage::Position,    positions, 18, 3);
        addBuf(iris::VertexAttribUsage::Normal,      normals,   18, 3);
        addBuf(iris::VertexAttribUsage::BoneIndices, boneIdx,   24, 4);
        addBuf(iris::VertexAttribUsage::BoneWeights, boneW,     24, 4);
        auto ib = iris::IndexBuffer::create();
        ib->setData(const_cast<unsigned *>(indices), sizeof(indices));
        armMesh->setIndexBuffer(ib);
        armMesh->setVertexCount(6);

        auto skel = iris::Skeleton::create();
        auto rootBone = iris::Bone::create("jointRoot");
        auto tipBone = iris::Bone::create("jointTip");
        tipBone->meshSpacePoseMatrix.translate(0, 1, 0);
        tipBone->inverseMeshSpacePoseMatrix.translate(0, -1, 0);
        tipBone->bindingPos = QVector3D(0, 1, 0);
        tipBone->bindingScale = QVector3D(1, 1, 1);
        rootBone->bindingScale = QVector3D(1, 1, 1);
        skel->addBone(rootBone);
        skel->addBone(tipBone);
        rootBone->addChild(tipBone);
        armMesh->setSkeleton(skel);

        auto anim = iris::SkeletalAnimation::create();
        anim->name = "swing";
        auto *boneAnim = new iris::BoneAnimation();
        boneAnim->posKeys->addKey(QVector3D(0, 1, 0), 0.0);
        boneAnim->posKeys->addKey(QVector3D(0, 1, 0), 1.0);
        boneAnim->rotKeys->addKey(QQuaternion(), 0.0);
        boneAnim->rotKeys->addKey(QQuaternion::fromAxisAndAngle(0, 0, 1, -90), 1.0);
        boneAnim->scaleKeys->addKey(QVector3D(1, 1, 1), 0.0);
        boneAnim->scaleKeys->addKey(QVector3D(1, 1, 1), 1.0);
        anim->addBoneAnimation("jointTip", boneAnim);
        armMesh->addSkeletalAnimation("swing", anim);

        auto armScene = iris::Scene::create();
        auto armNode = iris::MeshNode::create();
        armNode->setName("arm");
        armNode->setMesh(armMesh);
        armScene->rootNode->addChild(armNode);

        const auto ga = GltfExporter::exportScene(armScene, "Arm");
        CHECK(ga.ok, "skeletal exportScene ok");
        CHECK(ga.skinCount == 1, "1 skin");
        CHECK(ga.animationCount == 1, "1 animation");
        const QJsonObject aroot = ga.json;
        const QJsonObject skin = aroot["skins"].toArray().first().toObject();
        CHECK(skin["joints"].toArray().size() == 2, "skin has 2 joints");
        const QJsonArray aaccs = aroot["accessors"].toArray();
        const QJsonObject ibm = aaccs.at(skin["inverseBindMatrices"].toInt()).toObject();
        CHECK(ibm["type"].toString() == "MAT4" && ibm["count"].toInt() == 2,
              "inverseBindMatrices: 2 MAT4");
        const QJsonObject aprim = aroot["meshes"].toArray().first().toObject()["primitives"]
                                      .toArray().first().toObject();
        const QJsonObject aattrs = aprim["attributes"].toObject();
        CHECK(aattrs.contains("JOINTS_0") && aattrs.contains("WEIGHTS_0"),
              "JOINTS_0/WEIGHTS_0 attributes");
        CHECK(aaccs.at(aattrs["JOINTS_0"].toInt()).toObject()["componentType"].toInt() == 5123,
              "JOINTS_0 converted float -> ushort");
        const QJsonObject animObj = aroot["animations"].toArray().first().toObject();
        CHECK(animObj["name"].toString() == "swing", "animation named");
        CHECK(animObj["channels"].toArray().size() == 3, "T/R/S channels for the tip bone");
        bool rotChannelOk = false;
        for (const auto &chv : animObj["channels"].toArray()) {
            const QJsonObject ch = chv.toObject();
            if (ch["target"].toObject()["path"].toString() == "rotation") {
                const QJsonObject sampler = animObj["samplers"].toArray()
                                                .at(ch["sampler"].toInt()).toObject();
                rotChannelOk = sampler["interpolation"].toString() == "LINEAR" &&
                               aaccs.at(sampler["output"].toInt()).toObject()["type"].toString() == "VEC4";
            }
        }
        CHECK(rotChannelOk, "rotation channel: LINEAR VEC4 sampler");
        // joint nodes must be wired into the node graph
        const QJsonArray anodes = aroot["nodes"].toArray();
        bool jointsInGraph = false;
        for (const auto &nv : anodes) {
            if (nv.toObject()["name"].toString() == "jointRoot") jointsInGraph = true;
        }
        CHECK(jointsInGraph, "joint nodes present in the node graph");
    }

    // ---- F1: exported joints carry a REAL bind pose ------------------------
    // ANIMATION_ENGINE_MIGRATION_SPEC §1.5 F1 / §8.5. The exporter writes every
    // joint's TRS from Bone::bindingPos/bindingRot/bindingScale
    // (gltfexporter.cpp), and NOTHING on the live import path ever wrote those
    // fields — the only writers were in irisgl/import/modelloader.cpp, which
    // the editor does not go through. So every rig ever exported to the web came
    // out structurally right and BIND-POSED WRONG: translation (0,0,0), identity
    // rotation, and a scale saved only by a zero-guard in the writer.
    //
    // The block above cannot catch that: it builds its skeleton in code and
    // assigns bindingPos by hand. This one goes through the REAL importer.
    {
        auto fragment = iris::MeshNode::loadAsSceneFragment(
            QStringLiteral(JAHSHAKA_TEST_SOURCE_DIR "/tests/avatar/fixtures/rig2.glb"),
            [](iris::MeshPtr, iris::MeshMaterialData &) -> iris::MaterialPtr {
                return iris::PbrMaterial::create();
            },
            nullptr, nullptr, tmp.path());
        CHECK(!fragment.isNull(), "the generated rig fixture imports");
        if (!fragment.isNull()) {
            auto rigScene = iris::Scene::create();
            rigScene->rootNode->addChild(fragment);
            const auto gr = GltfExporter::exportScene(rigScene, "Rig");
            CHECK(gr.ok && gr.skinCount == 1, "an imported rig exports with a skin");
            double tipY = 0.0;
            bool foundTip = false;
            for (const auto &nv : gr.json["nodes"].toArray()) {
                const QJsonObject n = nv.toObject();
                if (n["name"].toString() != QStringLiteral("jointTip")) continue;
                foundTip = true;
                tipY = n["translation"].toArray().at(1).toDouble();
            }
            CHECK(foundTip, "the exported node graph has a jointTip joint");
            std::printf("    exported jointTip bind translation.y = %.4f\n", tipY);
            // jointTip binds one unit above jointRoot. Before the fix this read
            // 0.0 for every joint of every rig, on every platform.
            CHECK(std::fabs(tipY - 1.0) < 1e-4,
                  "the exported joint carries its real bind translation, not identity");
        }
    }

    // ---- PreviewLauncher: detection only (never spawn a browser in tests) ----
    const QString browser = PreviewLauncher::findChromiumBrowser();
    std::printf("    detected chromium-family browser: %s\n",
                browser.isEmpty() ? "(none)" : qPrintable(browser));
    CHECK(true, "findChromiumBrowser probed without side effects");

    std::printf(failures ? "FAILED: %d checks\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}
