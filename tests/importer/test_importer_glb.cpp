// GLB importer regression suite (importer fix phases 0-2).
//
// Fixtures (tests/importer/fixtures, generated tiny GLBs):
//   mislabeled_embedded.glb - embedded image is PNG bytes DECLARED image/jpeg
//     (assimp reports achFormatHint "jpg"): convicts the extension-mislabel
//     write defect and the real-PbrMaterial import (factors 0.75/0.3).
//   ticks_anim.glb - a 2-second animation; assimp hands it over as 2000 ticks
//     at mTicksPerSecond 1000: convicts raw-tick extraction (S2) and the
//     clip-name collapse (S1).
//
// Sections:
//   1. Magic-byte sniffing (unit).
//   2. The REAL import path (AssetHelper::extractTexturesAndMaterialFromMesh)
//      on the mislabeled fixture: extension follows the bytes, material is a
//      PbrMaterial with the authored factors, texture file decodable.
//   3. Tangent pass-through: document tangent buffers reach MeshData as
//      float4 (they used to be dropped and regenerated).
//   4. Animation extraction: seconds, kept clip name (S1/S2); child nodes
//      sample the original scene time (S5).
//   5. Skeletal source persistence: SceneWriter writes a RELATIVE source.
//   6. Engine tolerant texture reads: PNG bytes under a .jpg name load
//      (heals assets imported before the write-side fix).
//   7. Double-import root-scale stability (phase 2): importing the same
//      multi-mesh GLB twice in one process yields identical root transforms.
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>

#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"
#include "assimp/scene.h"

#include "irisgl/irisglfwd.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/import/materialhelper.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

#include "io/scenewriter.h"
#include "services/assethelper.h"
#include "services/sceneeditservice.h"
#include "ui/panels/transformeditor.h"

#include "../support/documentgraph.h"
// Link stub: TransformSceneNodeCommand (compiled for the panel-echo test)
// notifies the scene-edit service; the real service drags in the whole editor
// graph, and this suite never instantiates it.
void SceneEditService::notifyTransformChanged() {}

using namespace jahshaka::engine;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static QString fixture(const char *name)
{
    return QString(JAHSHAKA_TEST_SOURCE_DIR "/tests/importer/fixtures/") + name;
}

static bool nearly(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }

// Expose the protected AssetIOBase::setAssetPath for the writer test.
struct TestSceneWriter : SceneWriter { using AssetIOBase::setAssetPath; };

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // v1 INTERIM (SPECS/SCENEGRAPH_SPEC.md §3): a document node IS an engine
    // node now, so even a document-only suite needs an engine. Declared here,
    // before anything builds a document, and destroyed last.
    enginetest::DocumentGraph graph("importer-glb-ogre.log");
    if (!graph.require()) return 1;
    // ================= 1. magic-byte sniffing =================
    {
        const unsigned char png[] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
        const unsigned char jpg[] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10 };
        const unsigned char junk[] = { 0x00, 0x01, 0x02, 0x03 };
        CHECK(iris::MaterialHelper::sniffImageExtension(png, 8) == "png", "sniff: PNG magic");
        CHECK(iris::MaterialHelper::sniffImageExtension(jpg, 6) == "jpg", "sniff: JPEG magic");
        CHECK(iris::MaterialHelper::sniffImageExtension(junk, 4).isEmpty(), "sniff: unknown bytes -> empty");
        CHECK(iris::MaterialHelper::sniffImageExtension(nullptr, 0).isEmpty(), "sniff: null-safe");
    }

    // ================= 2. the real import path on the mislabeled fixture =================
    iris::MeshPtr importedMesh;   // kept for section 3
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid(), "temp dir for import");
        const QString model = QDir(tmp.path()).filePath("model.glb");
        CHECK(QFile::copy(fixture("mislabeled_embedded.glb"), model), "fixture copied");

        QStringList texNames, texPaths;
        bool hasEmbedded = false;
        auto node = AssetHelper::extractTexturesAndMaterialFromMesh(model, texNames, texPaths,
                                                                    hasEmbedded);
        CHECK(!node.isNull(), "import produced a node");
        if (node.isNull()) return 1;

        // The embedded image was declared image/jpeg but the BYTES are PNG:
        // the written file must be _0.png (sniffed), never _0.jpg.
        const QString png = QDir(tmp.path()).filePath("_0.png");
        const QString jpg = QDir(tmp.path()).filePath("_0.jpg");
        CHECK(QFileInfo::exists(png), "extracted texture written as .png (bytes are PNG)");
        CHECK(!QFileInfo::exists(jpg), "no mislabeled .jpg written");
        QFile f(png);
        f.open(QIODevice::ReadOnly);
        const QByteArray head = f.read(4);
        CHECK(head == QByteArray("\x89PNG", 4), "written file starts with the PNG magic");
        CHECK(!QImage(png).isNull(), "written file decodes");

        // Real PBR import: a PbrMaterial carrying the authored factors.
        auto meshNode = node.dynamicCast<iris::MeshNode>();
        CHECK(!meshNode.isNull(), "single-mesh GLB imports as a MeshNode");
        if (meshNode) {
            importedMesh = meshNode->getMesh();
            auto pbr = meshNode->getMaterial().dynamicCast<iris::PbrMaterial>();
            CHECK(!pbr.isNull(), "glTF material imports as iris::PbrMaterial");
            if (pbr) {
                CHECK(nearly(pbr->metallicFactor, 0.75f), "metallicFactor read from the file (0.75)");
                CHECK(nearly(pbr->roughnessFactor, 0.3f), "roughnessFactor read from the file (0.3)");
                CHECK(pbr->useBaseColorMap, "baseColorMap bound from the embedded texture");
            }
        }
        CHECK(texPaths.contains(png), "texture discovery lists the extracted file");
    }

    // ================= 3. tangent pass-through =================
    {
        CHECK(!importedMesh.isNull(), "mesh available for tangent check");
        if (importedMesh) {
            bool hasTangentBuffer = false, hasBitangentBuffer = false;
            for (const auto &vb : importedMesh->getVertexBuffers()) {
                if (!vb) continue;
                const auto attribs = vb->vertexLayout.getAttribs();
                if (attribs.isEmpty()) continue;
                if (attribs.first().usage == iris::VertexAttribUsage::Tangent) hasTangentBuffer = true;
                if (attribs.first().usage == iris::VertexAttribUsage::BiTangent) hasBitangentBuffer = true;
            }
            CHECK(hasTangentBuffer, "document mesh carries a tangent buffer");
            CHECK(hasBitangentBuffer, "document mesh carries a bitangent buffer (handedness)");

            MeshData md;
            CHECK(SceneMirror::toMeshData(importedMesh.data(), md), "toMeshData succeeds");
            const size_t nv = md.positions.size() / 3;
            CHECK(md.tangents.size() == nv * 4,
                  "document tangents reach MeshData as float4 (were dropped before)");
            if (md.tangents.size() == nv * 4) {
                bool unit = true;
                for (size_t i = 0; i < nv; ++i) {
                    const float x = md.tangents[i*4], y = md.tangents[i*4+1], z = md.tangents[i*4+2];
                    const float len = std::sqrt(x*x + y*y + z*z);
                    if (std::fabs(len - 1.0f) > 0.05f) { unit = false; break; }
                    const float w = md.tangents[i*4+3];
                    if (w != 1.0f && w != -1.0f) { unit = false; break; }
                }
                CHECK(unit, "tangents are unit-length with +/-1 handedness");
            }
        }
    }

    // ================= 3b. UV orientation + tangent handedness =================
    // textured_pbr_quad.glb: a +Z-facing unit quad with authored NORMAL,
    // TANGENT (1,0,0,+1) and TEXCOORD_0 mapping u=(x+1)/2, v=(1-y)/2 (glTF
    // top-left origin). assimp imports V flipped to its GL-style bottom-left
    // convention (the legacy renderer compensated by flipping the texture
    // image at load); the engine samples top-left images unflipped, so the
    // MIRROR must hand the engine glTF-convention UVs (v = 1 - v_document)
    // and the matching handedness — otherwise every imported model renders
    // its textures V-flipped (the "misplaced textures" defect).
    {
        QTemporaryDir tmp;
        const QString model = QDir(tmp.path()).filePath("quad.glb");
        CHECK(QFile::copy(fixture("textured_pbr_quad.glb"), model), "quad fixture copied");
        QStringList texNames, texPaths;
        bool hasEmbedded = false;
        auto node = AssetHelper::extractTexturesAndMaterialFromMesh(model, texNames, texPaths,
                                                                    hasEmbedded, nullptr, tmp.path());
        auto meshNode = node.dynamicCast<iris::MeshNode>();
        CHECK(!meshNode.isNull(), "textured quad imports as a MeshNode");
        if (meshNode) {
            MeshData md;
            CHECK(SceneMirror::toMeshData(meshNode->getMesh().data(), md), "quad toMeshData");
            const size_t nv = md.positions.size() / 3;
            CHECK(nv >= 4, "quad has at least 4 vertices");
            CHECK(md.uvs.size() == nv * 2, "quad has UVs");
            CHECK(md.tangents.size() == nv * 4, "quad has float4 tangents");
            bool uvOk = md.uvs.size() == nv * 2;
            for (size_t i = 0; i < nv && uvOk; ++i) {
                const float x = md.positions[i*3], y = md.positions[i*3+1];
                const float u = md.uvs[i*2], v = md.uvs[i*2+1];
                const float expU = (x + 1.0f) / 2.0f;
                const float expV = (1.0f - y) / 2.0f;
                if (std::fabs(u - expU) > 1e-4f || std::fabs(v - expV) > 1e-4f) {
                    std::printf("    vertex (%.1f,%.1f): uv (%.3f,%.3f), expected (%.3f,%.3f)\n",
                                x, y, u, v, expU, expV);
                    uvOk = false;
                }
            }
            CHECK(uvOk, "engine-facing UVs are glTF convention (v NOT flipped: red quadrant top-left)");
            bool tanOk = md.tangents.size() == nv * 4;
            for (size_t i = 0; i < nv && tanOk; ++i) {
                const float tx = md.tangents[i*4], w = md.tangents[i*4+3];
                if (std::fabs(tx - 1.0f) > 1e-3f || std::fabs(w - 1.0f) > 1e-3f) {
                    std::printf("    tangent[%zu] = (%.3f, %.3f, %.3f, w=%.1f)\n", i,
                                md.tangents[i*4], md.tangents[i*4+1], md.tangents[i*4+2], w);
                    tanOk = false;
                }
            }
            CHECK(tanOk, "engine-facing tangents keep the authored frame (T=+X, w=+1)");
        }
    }

    // ================= 4. animation extraction (S1/S2) + child time (S5) =================
    {
        Assimp::Importer importer;
        const aiScene *scene = importer.ReadFile(fixture("ticks_anim.glb").toStdString(),
                                                 aiProcess_Triangulate);
        CHECK(scene != nullptr, "ticks fixture loads");
        if (scene) {
            CHECK(scene->mNumAnimations == 1, "one animation in the fixture");
            // assimp reports this clip as 2000 ticks at tps=1000.
            CHECK(nearly(float(scene->mAnimations[0]->mTicksPerSecond), 1000.0f),
                  "assimp hands over tps=1000 (defect precondition)");

            auto anims = iris::Mesh::extractAnimations(scene, "src.glb");
            CHECK(anims.contains("swing"), "clip keeps its name (S1: no collapse to \"\")");
            if (anims.contains("swing")) {
                auto skel = anims["swing"];
                CHECK(skel->boneAnimations.contains("tip"), "channel keyed by node name");
                if (skel->boneAnimations.contains("tip")) {
                    auto &keys = skel->boneAnimations["tip"]->posKeys->keys;
                    CHECK(!keys.isEmpty(), "position keys extracted");
                    if (!keys.isEmpty()) {
                        const double last = keys.last()->time;
                        CHECK(nearly(float(last), 2.0f),
                              "key times are SECONDS (2.0, not 2000 ticks) (S2)");
                    }
                }
            }
        }

        // S5: a looping 1s parent clip must not remap the time its 2s child
        // samples. Child key: x ramps 0 -> 5 over [0, 1.5].
        auto scene2 = iris::Scene::create();
        auto parent = iris::SceneNode::create();
        auto child = iris::SceneNode::create();
        scene2->getRootNode()->addChild(parent);
        parent->addChild(child);

        auto parentAnim = iris::Animation::create("parent");
        parentAnim->setLength(1.0f);
        parentAnim->setLooping(true);
        parent->addAnimation(parentAnim);
        parent->setAnimation(parentAnim);

        auto childAnim = iris::Animation::create("child");
        auto *posAnim = new iris::Vector3DPropertyAnim();
        posAnim->setName("position");
        posAnim->getKeyFrame(0)->addKey(0.0f, 0.0);
        posAnim->getKeyFrame(0)->addKey(5.0f, 1.5);
        posAnim->getKeyFrame(1)->addKey(0.0f, 0.0);
        posAnim->getKeyFrame(2)->addKey(0.0f, 0.0);
        childAnim->addPropertyAnim(posAnim);
        childAnim->setLength(2.0f);
        childAnim->setLooping(false);
        child->addAnimation(childAnim);
        child->setAnimation(childAnim);

        parent->updateAnimation(1.5f);
        const float x = child->getLocalPos().x();
        std::printf("    child x at t=1.5: %.3f\n", x);
        CHECK(x > 4.9f, "child samples the ORIGINAL scene time, not the parent's looped time (S5)");
    }

    // ================= 5. skeletal source persists RELATIVE =================
    {
        QTemporaryDir tmp;
        const QString model = QDir(tmp.path()).filePath("anim.glb");
        QFile::copy(fixture("ticks_anim.glb"), model);

        auto ssource = new iris::SceneSource();
        auto node = iris::MeshNode::loadAsSceneFragment(model,
            [](iris::MeshPtr, iris::MeshMaterialData &) {
                return iris::MaterialPtr();
            }, ssource);
        CHECK(!node.isNull(), "animated fixture loads as a scene fragment");
        if (node) {
            // Find a node holding the skeletal animation (import attaches it).
            std::function<iris::SceneNodePtr(iris::SceneNodePtr)> findAnimated =
                [&](iris::SceneNodePtr n) -> iris::SceneNodePtr {
                    if (n->getAnimation() && n->getAnimation()->hasSkeletalAnimation()) return n;
                    for (auto &c : n->children()) { auto r = findAnimated(c); if (r) return r; }
                    return iris::SceneNodePtr();
                };
            auto animated = findAnimated(node);
            CHECK(!animated.isNull(), "an imported node carries the skeletal animation");
            if (animated) {
                CHECK(QFileInfo(animated->getAnimation()->getSkeletalAnimation()->source).isAbsolute(),
                      "in-memory source is absolute at import time (precondition)");
                TestSceneWriter writer;
                writer.setAssetPath(QDir(tmp.path()).filePath("scene.jah"));
                QJsonObject obj;
                SceneWriter::writeSceneNode(obj, animated, false);
                const QJsonArray anims = obj["animations"].toArray();
                CHECK(!anims.isEmpty(), "animation serialized");
                if (!anims.isEmpty()) {
                    const QString src = anims.first().toObject()["skeletalAnimation"]
                                            .toObject()["source"].toString();
                    std::printf("    persisted source: '%s'\n", src.toStdString().c_str());
                    CHECK(!src.isEmpty() && !QFileInfo(src).isAbsolute(),
                          "persisted skeletal source is RELATIVE to the project dir");
                }
            }
        }
    }

    // ================= 6 + 7 need the engine / a temp project dir =================
    // The engine this suite uses is the one the DOCUMENT graph already booted at
    // the top of main (Ogre::Root is a process singleton — a second
    // Engine::create aborts), so there is nothing to create here any more.
    Engine *engine = graph.engine();
    CHECK(engine != nullptr, "engine created");
    if (engine) {
        // NO VIEW. This section only asks the engine to DECODE image files —
        // nothing here renders or reads a pixel — and the fixture's engine is
        // headless (RenderSystem_NULL), which refuses views by design. The
        // Scene alone is what loadTexture needs.
        Scene *s = engine->createScene("imp");
        CHECK(s != nullptr, "engine scene created");
      if (s) {
        QTemporaryDir tmp;
        // Colour PNG bytes under a .jpg name (the historical mislabel).
        {
            QImage img(8, 8, QImage::Format_RGBA8888);
            img.fill(Qt::red);
            const QString sneaky = QDir(tmp.path()).filePath("sneaky.jpg");
            img.save(sneaky, "PNG");
            const TextureId t = s->loadTexture(sneaky.toStdString(), true);
            CHECK(t != 0, "engine loads PNG bytes under a .jpg name (tolerant read)");
        }
        // Grayscale PNG bytes under a .jpg name: exercises the probe path
        // (the grayscale expansion is where the strict load used to throw).
        {
            QImage img(8, 8, QImage::Format_Grayscale8);
            img.fill(128);
            const QString sneaky = QDir(tmp.path()).filePath("gray_sneaky.jpg");
            img.save(sneaky, "PNG");
            const TextureId t = s->loadTexture(sneaky.toStdString(), false);
            CHECK(t != 0, "engine loads grayscale PNG bytes under a .jpg name");
        }
        // An honest file still loads.
        {
            QImage img(8, 8, QImage::Format_RGBA8888);
            img.fill(Qt::green);
            const QString honest = QDir(tmp.path()).filePath("honest.png");
            img.save(honest, "PNG");
            CHECK(s->loadTexture(honest.toStdString(), true) != 0, "honest file still loads");
        }

        engine->destroyScene(s);
      }
    }

    // ================= 7. double-import root-scale stability =================
    {
        // Import the same multi-mesh, root-scaled GLB twice in ONE process and
        // compare every node's local scale. (Phase 2: the audit observed the
        // first import of a session yielding a different root scale.)
        auto importOnce = [](const QString &path) {
            auto ssource = new iris::SceneSource();
            return iris::MeshNode::loadAsSceneFragment(path,
                [](iris::MeshPtr, iris::MeshMaterialData &) { return iris::MaterialPtr(); },
                ssource);
        };
        const QString path = fixture("scaled_two_meshes.glb");
        auto first = importOnce(path);
        auto second = importOnce(path);
        CHECK(!first.isNull() && !second.isNull(), "scaled fixture imports twice");
        if (first && second) {
            std::function<bool(iris::SceneNodePtr, iris::SceneNodePtr)> sameTransforms =
                [&](iris::SceneNodePtr a, iris::SceneNodePtr b) -> bool {
                    if (a->getLocalScale() != b->getLocalScale()) {
                        std::printf("    scale mismatch on '%s': (%.6f) vs (%.6f)\n",
                                    a->name.toStdString().c_str(),
                                    a->getLocalScale().x(), b->getLocalScale().x());
                        return false;
                    }
                    if (a->children().size() != b->children().size()) return false;
                    for (int i = 0; i < a->children().size(); ++i)
                        if (!sameTransforms(a->children()[i], b->children()[i])) return false;
                    return true;
                };
            CHECK(sameTransforms(first, second), "double import: identical transforms");
            // The authored root scale must actually be APPLIED (0.0143).
            std::function<bool(iris::SceneNodePtr)> hasAuthoredScale =
                [&](iris::SceneNodePtr n) -> bool {
                    if (nearly(n->getLocalScale().x(), 0.0143f, 1e-4f)) return true;
                    for (auto &c : n->children()) if (hasAuthoredScale(c)) return true;
                    return false;
                };
            CHECK(hasAuthoredScale(first), "authored root scale 0.0143 survives import");
        }
    }

    // ================= 8. the transform panel must not echo rounded values =================
    {
        // THE actual root cause of the double-import 0.0143 -> 0.01 corruption:
        // populating the transform spinboxes fired valueChanged (setValue
        // rounds to the field's decimals) which wrote the ROUNDED transform
        // back onto the node. Only the panel's FIRST population changed the
        // spinbox value, which is why only the first import of a session bent.
        auto node = iris::SceneNode::create();
        node->setName("scaled");
        node->name = "scaled";
        node->setLocalScale(iris::Vec3(0.0143f, 0.0143f, 0.0143f));
        node->setLocalRot(iris::Quat::fromEulerAngles(10.5f, 20.25f, 0.125f));
        node->setLocalPos(iris::Vec3(0.00123f, 0, 0));
        const iris::Vec3 scaleBefore = node->getLocalScale();
        const iris::Quat rotBefore = node->getLocalRot();
        const iris::Vec3 posBefore = node->getLocalPos();

        TransformEditor editor(nullptr);
        editor.setSceneNode(node);          // first population of a fresh panel
        editor.setSceneNode(node);          // and again, for good measure

        CHECK(node->getLocalScale() == scaleBefore,
              "selecting a node does not rewrite its scale (0.0143 stays 0.0143)");
        CHECK(node->getLocalRot() == rotBefore,
              "selecting a node does not rewrite its rotation (no euler echo)");
        CHECK(node->getLocalPos() == posBefore,
              "selecting a node does not rewrite its position");
    }

    std::printf(failures ? "FAILED: %d checks\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
