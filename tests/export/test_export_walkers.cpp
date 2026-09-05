// Shared export walkers (ASSET_PIPELINE_SPEC §3.3 phase-5 front half):
// traversal order + skip semantics + node classification, the scene inventory
// (nodes/materials/textures), and the mesh buffer readers — the document-
// walking core every exporter drives. Document-only, runs offscreen.

#include <QGuiApplication>
#include <QColor>
#include <QImage>
#include <QTemporaryDir>

#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/viewernode.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/texture2d.h"

#include "export/walkers/scenewalker.h"
#include "export/walkers/meshbufferreader.h"
#include "export/walkers/materialtexturereader.h"

#include "../support/documentgraph.h"
static int failures = 0;
#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        if (cond) { std::printf("ok: %s\n", msg); }                          \
        else      { std::printf("FAIL: %s\n", msg); ++failures; }            \
    } while (0)

using namespace exportwalk;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    // v1 INTERIM (SPECS/SCENEGRAPH_SPEC.md §3): a document node IS an engine
    // node now, so even a document-only suite needs an engine. Declared here,
    // before anything builds a document, and destroyed last.
    enginetest::DocumentGraph graph("export-walkers-ogre.log");
    if (!graph.require()) return 1;
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir");

    // ---- build a document scene ----
    // root
    //  ├─ cube (mesh, PBR + roughness map)
    //  │   └─ childCube (mesh, shares the same iris::Mesh)
    //  ├─ hidden (mesh, exportable = false)  -> skipped
    //  ├─ point light
    //  ├─ camera
    //  ├─ viewer
    //  └─ group (empty)
    auto scene = iris::Scene::create();

    auto cube = iris::MeshNode::create();
    cube->setName("cube");
    cube->setMesh(":assets/models/cube.obj");
    CHECK(!!cube->getMesh(), "cube.obj loaded into the document");
    auto pbr = iris::PbrMaterial::create();
    pbr->setBaseColor(QColor(180, 60, 40));
    QString roughPath;
    {
        QImage rough(8, 8, QImage::Format_RGB888);
        rough.fill(QColor(120, 120, 120));
        roughPath = tmp.filePath("rough.png");
        rough.save(roughPath);
        pbr->setRoughnessMap(iris::Texture2D::load(roughPath));
    }
    cube->setMaterial(pbr);
    scene->rootNode->addChild(cube);

    auto childCube = iris::MeshNode::create();
    childCube->setName("childCube");
    childCube->setMesh(":assets/models/cube.obj");
    childCube->setMaterial(pbr);
    cube->addChild(childCube);

    auto hidden = iris::MeshNode::create();
    hidden->setName("hidden");
    hidden->setMesh(":assets/models/cube.obj");
    hidden->exportable = false;
    scene->rootNode->addChild(hidden);

    auto light = iris::LightNode::create();
    light->setName("light");
    light->setLightType(iris::LightType::Point);
    scene->rootNode->addChild(light);

    auto cam = iris::CameraNode::create();
    cam->setName("camera");
    scene->rootNode->addChild(cam);

    auto viewer = iris::ViewerNode::create();
    viewer->setName("viewer");
    scene->rootNode->addChild(viewer);

    auto group = iris::SceneNode::create();
    group->setName("group");
    scene->rootNode->addChild(group);

    // ---- classification ----
    CHECK(classifyNode(cube) == NodeKind::Mesh, "classify mesh");
    CHECK(classifyNode(light) == NodeKind::Light, "classify light");
    CHECK(classifyNode(cam) == NodeKind::Camera,
          "classify camera off the TYPE ENUM (CAMERAS_SPEC phase 1: the CameraNode "
          "constructor sets it, so the dynamic_cast this walker needed is gone)");
    CHECK(classifyNode(viewer) == NodeKind::Viewer, "classify viewer");
    CHECK(classifyNode(group) == NodeKind::Empty, "classify empty");

    // ---- skip semantics ----
    CHECK(shouldSkipForExport(hidden), "non-exportable MESH is skipped");
    CHECK(!shouldSkipForExport(light),
          "light is never skipped (constructors hard-code exportable=false)");
    CHECK(!shouldSkipForExport(cam), "camera is never skipped");

    // ---- traversal: post-order, handles flow child -> parent ----
    {
        QStringList visited;
        int handle = 100;
        QVector<int> cubeChildHandles;
        const auto roots = walkScene(scene, [&](const iris::SceneNodePtr &node,
                                                const QVector<int> &childHandles) -> int {
            visited.append(node->getName());
            if (node->getName() == "cube") cubeChildHandles = childHandles;
            return handle++;
        });
        CHECK(visited.size() == 6, "6 nodes visited (root and skipped mesh excluded)");
        CHECK(!visited.contains("hidden"), "hidden mesh not visited");
        CHECK(visited.indexOf("childCube") < visited.indexOf("cube"),
              "post-order: child visited before parent");
        CHECK(cubeChildHandles.size() == 1 && cubeChildHandles.first() == 100,
              "parent receives its child's handle");
        CHECK(roots.size() == 5, "5 root handles (cube, light, camera, viewer, group)");
        CHECK(!roots.contains(100), "child handle is not a root handle");
    }

    // ---- negative handles are dropped from child lists ----
    {
        const auto roots = walkScene(scene, [](const iris::SceneNodePtr &node,
                                               const QVector<int> &childHandles) -> int {
            if (node->getName() == "childCube") return -1;
            if (node->getName() == "cube") return childHandles.isEmpty() ? 7 : -7;
            return 0;
        });
        CHECK(roots.contains(7), "a -1 handle never reaches the parent's child list");
    }

    // ---- inventory ----
    {
        const SceneInventory inv = collectInventory(scene);
        CHECK(inv.totalNodes == 6, "inventory total = 6");
        CHECK(inv.meshNodes == 2, "inventory meshes = 2 (skipped mesh excluded)");
        CHECK(inv.lights == 1, "inventory lights = 1");
        CHECK(inv.cameras == 1, "inventory cameras = 1");
        CHECK(inv.viewers == 1, "inventory viewers = 1");
        CHECK(inv.empties == 1, "inventory empties = 1");
        CHECK(inv.materials.size() == 1, "shared material counted once");
        CHECK(inv.textureSources.contains(roughPath), "roughness map in texture sources");
        CHECK(inv.textureSources.size() == 1, "texture sources deduped");
    }

    // ---- material texture slot enumeration ----
    {
        const auto found = materialTextureSlots(pbr.data());
        CHECK(found.size() == 1, "one texture slot on the PBR material");
        CHECK(!found.isEmpty() && found.first().source == roughPath, "slot source path");
        CHECK(textureSlotSource(pbr.data(), "u_roughnessMap") == roughPath,
              "textureSlotSource reads the named slot");
        CHECK(textureSlotSource(pbr.data(), "u_normalMap").isEmpty(),
              "absent slot reads empty");
    }

    // ---- mesh buffers ----
    {
        MeshBuffers mb;
        CHECK(extractMeshBuffers(cube->getMesh().data(), mb), "extractMeshBuffers ok");
        const size_t nv = mb.vertexCount();
        CHECK(nv > 0, "positions extracted");
        CHECK(mb.indices.size() >= 36, "cube indices extracted");
        CHECK(mb.uvs.size() == nv * 2, "uvs narrowed to 2 floats per vertex");
        if (mb.normals.empty()) generateNormals(mb);
        CHECK(mb.normals.size() == nv * 3, "normals present/generated");
        generateTangents(mb);
        CHECK(mb.tangents.size() == nv * 4, "float4 tangents generated");
        bool unitW = true;
        for (size_t v = 0; v < nv; ++v) {
            const float w = mb.tangents[v * 4 + 3];
            if (w != 1.0f && w != -1.0f) unitW = false;
        }
        CHECK(unitW, "tangent w is pure handedness (+/-1)");
    }

    std::printf(failures ? "test_export_walkers: %d FAILURES\n"
                         : "test_export_walkers: ALL OK\n", failures);
    return failures ? 1 : 0;
}
