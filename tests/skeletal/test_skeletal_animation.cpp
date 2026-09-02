// Skeletal animation, phase (a): CPU skinning through the mirror.
//
// Three layers, one process (Ogre::Root is a singleton):
//   1. Engine: Scene::updateMeshVertices on a MeshData::dynamic mesh moves pixels;
//      non-dynamic meshes and wrong sizes are refused.
//   2. Document: SceneNode::updateAnimation (via Scene::updateSceneAnimation, the
//      exact call PlayBack::update makes in play mode) fills Skeleton::boneTransforms
//      with no GL — proven on a programmatic two-bone arm.
//   3. Mirror: the skinned mesh mirrors as a dynamic engine mesh; advancing the
//      document bends the arm on screen — a limb pixel flips both ways.
// No window; DISPLAY must be reachable (Vulkan). QT_QPA_PLATFORM=offscreen.
#include <QGuiApplication>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QVector3D>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"
#include "irisgl/mirror/scenemirror.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// The arm is red (emissive or ambient-lit albedo), the background pure blue —
// classify by dominance, not absolute brightness (ambient level is the engine's).
static bool isRed(const Colour &c)  { return c.r > 0.15f && c.r > c.b * 1.5f; }
static bool isBlue(const Colour &c) { return c.b > 0.6f && c.r < 0.1f; }
static void show(const char *tag, const Colour &c) {
    std::printf("    %-34s %3.0f %3.0f %3.0f\n", tag, c.r * 255, c.g * 255, c.b * 255);
}
static void render(Engine *e, int frames = 3) { for (int i = 0; i < frames; ++i) e->renderOneFrame(); }

// ---- the two-bone arm -------------------------------------------------------------
// A flat quad strip at z=0 facing +Z: rows y=0 and y=1 weighted to bone 0
// ("jointRoot", bind at the origin), row y=2 to bone 1 ("jointTip", bind at y=1).
// The animation swings jointTip -90 degrees about Z over 1s: at t=0.5 the top row
// rotates -45 degrees about the pivot (0,1,0).
static iris::MeshPtr buildArmMesh()
{
    const float hw = 0.15f;
    const float positions[] = { -hw, 0, 0,  hw, 0, 0,  -hw, 1, 0,  hw, 1, 0,  -hw, 2, 0,  hw, 2, 0 };
    const float normals[]   = { 0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1 };
    const float boneIdx[]   = { 0,0,0,0,  0,0,0,0,  0,0,0,0,  0,0,0,0,  1,0,0,0,  1,0,0,0 };
    const float boneW[]     = { 1,0,0,0,  1,0,0,0,  1,0,0,0,  1,0,0,0,  1,0,0,0,  1,0,0,0 };
    const unsigned indices[] = { 0, 1, 3,  0, 3, 2,  2, 3, 5,  2, 5, 4 };   // CCW from +Z

    auto mesh = iris::Mesh::create();
    auto addBuf = [&mesh](iris::VertexAttribUsage usage, const float *data, int floats, int comps) {
        iris::VertexLayout layout;
        layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
        auto vb = iris::VertexBuffer::create(layout);
        vb->setData(const_cast<float *>(data), unsigned(floats * sizeof(float)));
        mesh->addVertexBuffer(vb);
    };
    addBuf(iris::VertexAttribUsage::Position,    positions, 18, 3);
    addBuf(iris::VertexAttribUsage::Normal,      normals,   18, 3);
    addBuf(iris::VertexAttribUsage::BoneIndices, boneIdx,   24, 4);
    addBuf(iris::VertexAttribUsage::BoneWeights, boneW,     24, 4);
    auto ib = iris::IndexBuffer::create();
    ib->setData(const_cast<unsigned *>(indices), sizeof(indices));
    mesh->setIndexBuffer(ib);
    mesh->setVertexCount(6);

    auto skel = iris::Skeleton::create();
    auto root = iris::Bone::create("jointRoot");            // bind: mesh origin
    auto tip = iris::Bone::create("jointTip");              // bind: y=1 in mesh space
    tip->meshSpacePoseMatrix.translate(0, 1, 0);
    tip->inverseMeshSpacePoseMatrix.translate(0, -1, 0);
    skel->addBone(root);                                    // index 0
    skel->addBone(tip);                                     // index 1
    root->addChild(tip);
    mesh->setSkeleton(skel);
    return mesh;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_skeletal-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    // ================= 1. engine: dynamic vertex buffer =================
    {
        View *view = engine->createOffscreenView("dyn", 96, 96, Colour(0, 0, 1));
        Scene *s = engine->createScene("dyn");
        view->setScene(s);
        enginetest::testCameraLookAt(view, Vec3(0, 0, 5), Vec3(0, 0, 0));

        MeshData quad;
        quad.positions = { -1,-1,0,  1,-1,0,  1,1,0,  -1,1,0 };
        quad.normals   = { 0,0,1,  0,0,1,  0,0,1,  0,0,1 };
        quad.indices   = { 0, 1, 2,  0, 2, 3 };
        quad.dynamic   = true;
        MeshId dyn = s->createMesh(quad);
        CHECK(dyn != 0, "dynamic mesh created");
        PbrParams mat; mat.albedo = Colour(0, 0, 0); mat.emissive = Colour(1, 0, 0);
        MaterialId red = s->createPbrMaterial(mat);
        NodeId n = s->createNode();
        CHECK(s->attachMesh(n, dyn, red), "dynamic mesh attached");

        render(engine.get());
        Image img;
        CHECK(view->readPixels(img), "readPixels (dynamic, before)");
        show("dynamic quad centre (before)", img.at(48, 48));
        CHECK(isRed(img.at(48, 48)), "dynamic quad covers the centre");

        // Move every vertex +6 in x: the quad leaves the frustum's centre.
        std::vector<float> moved = quad.positions;
        for (size_t i = 0; i < moved.size(); i += 3) moved[i] += 6.0f;
        CHECK(s->updateMeshVertices(dyn, moved, {}), "updateMeshVertices (positions only)");
        render(engine.get());
        CHECK(view->readPixels(img), "readPixels (dynamic, after)");
        show("dynamic quad centre (after)", img.at(48, 48));
        CHECK(isBlue(img.at(48, 48)), "updated vertices moved the quad off-centre");

        // Refusals: wrong size, and a non-dynamic mesh.
        CHECK(!s->updateMeshVertices(dyn, { 0, 0, 0 }, {}), "size mismatch refused");
        MeshData still = quad; still.dynamic = false;
        MeshId immutable = s->createMesh(still);
        CHECK(immutable != 0, "immutable mesh created");
        CHECK(!s->updateMeshVertices(immutable, still.positions, {}),
              "non-dynamic mesh refuses updates");

        engine->destroyView(view);
        engine->destroyScene(s);
    }

    // ================= 2 + 3. document advance + mirrored skinning =================
    View *view = engine->createOffscreenView("skel", 128, 128, Colour(0, 0, 1));
    Scene *target = engine->createScene("skel");
    view->setScene(target);
    target->setAmbient(Colour(1, 1, 1), Colour(1, 1, 1));
    CameraDesc cam;                                   // at (0,1,5) looking down -Z
    cam.position = Vec3(0, 1, 5);
    view->setCamera(cam);

    // The document: arm MeshNode + the bone scene nodes the FBX path would create.
    auto doc = iris::Scene::create();
    auto armMesh = buildArmMesh();
    auto arm = iris::MeshNode::create();
    arm->setName("arm");
    arm->name = "arm";
    arm->setMesh(armMesh);
    auto mat = iris::DefaultMaterial::create();
    mat->setDiffuseColor(QColor(255, 0, 0));
    arm->setMaterial(mat);
    doc->getRootNode()->addChild(arm);
    auto jointRoot = iris::SceneNode::create();
    jointRoot->setName("jointRoot"); jointRoot->name = "jointRoot";
    arm->addChild(jointRoot);
    auto jointTip = iris::SceneNode::create();
    jointTip->setName("jointTip"); jointTip->name = "jointTip";
    jointTip->setLocalPos(QVector3D(0, 1, 0));
    jointRoot->addChild(jointTip);

    // The animation: jointTip swings -90 degrees about Z over 1 second.
    auto skelAnim = iris::SkeletalAnimation::create();
    auto boneAnim = new iris::BoneAnimation();
    boneAnim->posKeys->addKey(QVector3D(0, 1, 0), 0.0);
    boneAnim->posKeys->addKey(QVector3D(0, 1, 0), 1.0);
    boneAnim->rotKeys->addKey(QQuaternion(), 0.0);
    boneAnim->rotKeys->addKey(QQuaternion::fromAxisAndAngle(0, 0, 1, -90.0f), 1.0);
    boneAnim->scaleKeys->addKey(QVector3D(1, 1, 1), 0.0);
    boneAnim->scaleKeys->addKey(QVector3D(1, 1, 1), 1.0);
    skelAnim->addBoneAnimation("jointTip", boneAnim);
    auto anim = iris::Animation::createFromSkeletalAnimation(skelAnim);
    arm->addAnimation(anim);
    arm->setAnimation(anim);

    // Mirror-level: the bone data extraction the skinning path relies on.
    std::vector<float> bi, bw;
    CHECK(SceneMirror::toSkinData(armMesh.data(), bi, bw), "toSkinData finds bone buffers");
    CHECK(bi.size() == 24 && bw.size() == 24, "bone data is 4 indices + 4 weights per vertex");

    SceneMirror mirror(target);
    mirror.setSource(doc);
    mirror.setLightWires(false);

    // t = 0: bind pose (identity boneTransforms == bind, like the legacy shader).
    doc->updateSceneAnimation(0.0f);
    mirror.sync();
    render(engine.get());
    Image bind;
    CHECK(view->readPixels(bind), "readPixels (bind pose)");
    // World (0, 1.9) is inside the straight arm; world (0.55, 1.55) is beside it.
    // Camera: 45-degree vertical fov at distance 5 -> half-height 2.071 -> 1 world
    // unit = 30.9 px. Tip pixel (64, 36); side pixel (81, 47).
    show("tip pixel (bind)", bind.at(64, 36));
    show("side pixel (bind)", bind.at(81, 47));
    CHECK(isRed(bind.at(64, 36)), "bind pose: straight arm covers the tip pixel");
    CHECK(isBlue(bind.at(81, 47)), "bind pose: nothing beside the arm");

    // Advance the DOCUMENT the way play mode does (PlayBack::update ->
    // Scene::updateSceneAnimation). No GL anywhere in this path.
    doc->updateSceneAnimation(0.5f);
    // The NODE's skeleton, not the mesh asset's (GPU_SKINNING_SPEC §7): the
    // asset's is the shared rig template and is never posed.
    auto skel = arm->getSkeleton();
    QMatrix4x4 identity;
    CHECK(skel->boneTransforms.size() == 2, "skeleton has two bone transforms");
    CHECK(skel->boneTransforms[1] != identity, "document advance poses the tip bone");

    // Mirror-level: skinned positions differ from bind pose where weighted.
    {
        std::vector<float> pos, norm;
        MeshData bindData;
        SceneMirror::toMeshData(armMesh.data(), bindData);
        SceneMirror::skinVertices(skel->boneTransforms, bindData.positions, bindData.normals,
                                  bi, bw, pos, norm);
        const float dx = pos[4 * 3] - bindData.positions[4 * 3];       // vertex 4: (-hw, 2, 0)
        const float dy = pos[4 * 3 + 1] - bindData.positions[4 * 3 + 1];
        std::printf("    top vertex moved by %.3f, %.3f\n", dx, dy);
        CHECK(std::fabs(dx) > 0.1f && std::fabs(dy) > 0.1f, "skinned positions differ from bind pose");
        const float rx = pos[0] - bindData.positions[0];               // vertex 0: root-weighted
        CHECK(std::fabs(rx) < 1e-5f, "root-weighted vertices stay put");
    }

    // Pixels: sync pushes the skinned vertices; the limb pixel flips both ways.
    mirror.sync();
    render(engine.get());
    Image bent;
    CHECK(view->readPixels(bent), "readPixels (bent pose)");
    show("tip pixel (bent)", bent.at(64, 36));
    show("side pixel (bent)", bent.at(81, 47));
    CHECK(isBlue(bent.at(64, 36)), "bent pose: the tip pixel emptied");
    CHECK(isRed(bent.at(81, 47)), "bent pose: the arm swung into the side pixel");

    // Back to t = 0 (what stopping playback does): the arm straightens again.
    doc->updateSceneAnimation(0.0f);
    mirror.sync();
    render(engine.get());
    Image again;
    CHECK(view->readPixels(again), "readPixels (straight again)");
    CHECK(isRed(again.at(64, 36)) && isBlue(again.at(81, 47)), "stop restores the bind-pose picture");

    mirror.setSource(iris::ScenePtr());
    engine->destroyView(view);
    engine->destroyScene(target);
    engine.reset();

    std::printf(failures ? "FAILED: %d checks\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
