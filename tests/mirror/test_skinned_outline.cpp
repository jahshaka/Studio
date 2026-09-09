// mirror.skinned_outline — a SELECTED skinned character must not leave a
// second body behind while it animates.
//
// THE DEFECT (found 2026-09-06 on the Mixamo Beta fixture, reported as "a
// skinned character renders TWICE during play"): the selection highlight is an
// INVERTED HULL — the same mesh, attached a second time to an engine-owned node
// with the outline datablock and scaled up a couple of percent, so only the
// band poking out past the original shows. That shell went on through
// attachMesh with an UNLIT datablock, and HlmsUnlit has no skeletal path at all
// in this engine (`hlms_skeleton` lives only in the Pbs templates). The shell
// therefore drew the character's BIND pose, forever, while the real Item played
// the clip: the moment a character moved, its "outline" peeled off and stood
// there as a solid selection-yellow twin, roughly doubling the triangle count.
//
// It hid for as long as it did because a character only leaves bind pose while
// something plays it, and until this fixture every rig we drove was either
// selected-and-still or playing-and-unselected.
//
// THE GATE, and why it is shaped this way: a hull is invisible on a flat sheet
// (culling front faces leaves nothing), so the rig here is a closed TUBE, and
// the outline width is turned up so the band is worth several pixels. Then one
// probe point above the character's head does all the work:
//
//   * unselected, bind pose  -> empty (the body ends below it)
//   * selected, bind pose    -> selection-yellow (the enlarged hull reaches it,
//                               which is what proves the shell exists at all)
//   * selected, BENT 90 deg  -> empty again; a shell that did not follow the
//                               pose is still standing there.
//
// Plus the structural half: the character's engine node carries exactly ONE
// renderable, stopped and playing — an attach path that orphaned an Item would
// show up there before any pixel did.
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/assets/vertexbuffer.h"
#include "irisgl/document/assets/vertexlayout.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

// ---------------------------------------------------------------------------
/// A two-bone skinned TUBE standing from y=0 to y=2: three rings of four
/// corners, four side quads per segment, outward CCW winding (so an inverted
/// hull has back faces to draw). The top ring rides bone 1 ("jointTip", bound
/// at y=1); everything else rides bone 0 at the origin — the same rig shape as
/// tests/skeletal/armrig.h, given a volume so a silhouette is visible.
static iris::MeshPtr buildTubeMesh()
{
    const float r = 0.15f;
    const float ringY[3] = { 0.0f, 1.0f, 2.0f };
    const float cx[4] = { -r,  r,  r, -r };
    const float cz[4] = {  r,  r, -r, -r };

    std::vector<float> positions, normals, boneIdx, boneW;
    for (int ring = 0; ring < 3; ++ring) {
        for (int c = 0; c < 4; ++c) {
            positions.insert(positions.end(), { cx[c], ringY[ring], cz[c] });
            const float len = std::sqrt(cx[c] * cx[c] + cz[c] * cz[c]);
            normals.insert(normals.end(), { cx[c] / len, 0.0f, cz[c] / len });
            const float bone = ring == 2 ? 1.0f : 0.0f;
            boneIdx.insert(boneIdx.end(), { bone, 0, 0, 0 });
            boneW.insert(boneW.end(), { 1, 0, 0, 0 });
        }
    }
    std::vector<unsigned> indices;
    for (unsigned seg = 0; seg < 2; ++seg) {
        for (unsigned c = 0; c < 4; ++c) {
            const unsigned lo0 = seg * 4 + c, lo1 = seg * 4 + (c + 1) % 4;
            const unsigned hi0 = lo0 + 4, hi1 = lo1 + 4;
            indices.insert(indices.end(), { lo0, lo1, hi1, lo0, hi1, hi0 });
        }
    }

    auto mesh = iris::Mesh::create();
    auto addBuf = [&mesh](iris::VertexAttribUsage usage, const std::vector<float> &data, int comps) {
        iris::VertexLayout layout;
        layout.addAttrib(usage, iris::AttribTypeFloat, comps, comps * int(sizeof(float)));
        auto vb = iris::VertexBuffer::create(layout);
        vb->setData(const_cast<float *>(data.data()), unsigned(data.size() * sizeof(float)));
        mesh->addVertexBuffer(vb);
    };
    addBuf(iris::VertexAttribUsage::Position, positions, 3);
    addBuf(iris::VertexAttribUsage::Normal, normals, 3);
    addBuf(iris::VertexAttribUsage::BoneIndices, boneIdx, 4);
    addBuf(iris::VertexAttribUsage::BoneWeights, boneW, 4);
    auto ib = iris::IndexBuffer::create();
    ib->setData(indices.data(), unsigned(indices.size() * sizeof(unsigned)));
    mesh->setIndexBuffer(ib);
    mesh->setVertexCount(12);

    auto skel = iris::Skeleton::create();
    auto root = iris::Bone::create("jointRoot");
    auto tip = iris::Bone::create("jointTip");
    tip->meshSpacePoseMatrix.translate(0, 1, 0);
    tip->inverseMeshSpacePoseMatrix.translate(0, -1, 0);
    skel->addBone(root);
    skel->addBone(tip);
    root->addChild(tip);
    mesh->setSkeleton(skel);
    return mesh;
}

/// jointTip swings `degrees` about Z over one second.
static iris::AnimationPtr buildSwingClip(float degrees)
{
    auto skelAnim = iris::SkeletalAnimation::create();
    auto boneAnim = new iris::BoneAnimation();
    boneAnim->posKeys->addKey(iris::Vec3(0, 1, 0), 0.0);
    boneAnim->posKeys->addKey(iris::Vec3(0, 1, 0), 1.0);
    boneAnim->rotKeys->addKey(iris::Quat(), 0.0);
    boneAnim->rotKeys->addKey(iris::Quat::fromAxisAndAngle(0, 0, 1, degrees), 1.0);
    boneAnim->scaleKeys->addKey(iris::Vec3(1, 1, 1), 0.0);
    boneAnim->scaleKeys->addKey(iris::Vec3(1, 1, 1), 1.0);
    skelAnim->addBoneAnimation("jointTip", boneAnim);
    return iris::Animation::createFromSkeletalAnimation(skelAnim);
}

// ---------------------------------------------------------------------------
/// The camera is a plain look-at from +Z at the same height it looks at, so the
/// projection of a point on the x/y plane through the origin is a straight
/// perspective divide.
static void probeToPixel(float wx, float wy, int W, int H, int &px, int &py)
{
    const float camZ = 6.0f, camY = 1.0f;
    const float fovY = 45.0f * 3.14159265f / 180.0f;
    const float halfH = std::tan(fovY * 0.5f) * camZ;
    const float halfW = halfH * float(W) / float(H);
    px = int((wx / halfW * 0.5f + 0.5f) * float(W));
    py = int((0.5f - (wy - camY) / halfH * 0.5f) * float(H));
    px = std::min(W - 1, std::max(0, px));
    py = std::min(H - 1, std::max(0, py));
}

static bool isLit(const Colour &c) { return c.r > 0.05f || c.g > 0.05f || c.b > 0.05f; }
/// The mirror's fallback selection colour is 1.0 / 0.85 / 0.1.
static bool isSelectionYellow(const Colour &c)
{
    return c.r > 0.25f && c.g > 0.15f && c.b < c.g * 0.7f;
}

/// Pixels matching a predicate inside a rectangle — "did anything move over
/// there", without pinning where a band falls to the pixel.
template <typename Pred>
static int countIn(const Image &img, int x0, int x1, int y0, int y1, Pred p)
{
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (p(img.at(x, y))) ++n;
    return n;
}
static int yellowIn(const Image &i, int x0, int x1, int y0, int y1)
{
    return countIn(i, x0, x1, y0, y1, [](const Colour &c) { return isSelectionYellow(c); });
}
static int litIn(const Image &i, int x0, int x1, int y0, int y1)
{
    return countIn(i, x0, x1, y0, y1, [](const Colour &c) { return isLit(c); });
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    EngineConfig cfg;
    cfg.pluginDir = JAHSHAKA_TEST_PLUGIN_DIR;
    cfg.hlmsMediaDir = JAHSHAKA_TEST_MEDIA_DIR;
    cfg.logFile = "test_skinned_outline-ogre.log";
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }

    const int W = 192, H = 192;
    View *view = engine->createOffscreenView("outline", W, H, Colour(0, 0, 0));
    Scene *scene = engine->createScene("outline");
    if (!view || !scene) { std::printf("FAIL: no view/scene\n"); return 1; }
    view->setScene(scene);
    scene->setAmbient(Colour(1, 1, 1), Colour(1, 1, 1));
    enginetest::testCameraLookAt(view, Vec3(0, 1, 6), Vec3(0, 1, 0));

    // The document: one rigged tube with one clip that bends its top half 90
    // degrees. Nothing else, so every lit pixel belongs to it or to its shell.
    auto doc = iris::Scene::create();
    // A wide band, so the silhouette is worth several pixels at this size. The
    // width is the user's Preferences value; the mirror maps it to a hull scale
    // of 1 + width/150.
    doc->outlineWidth = 60;   // hull scale 1.4
    auto mesh = buildTubeMesh();
    auto body = iris::MeshNode::create();
    body->setName(QStringLiteral("tube"));
    body->setMesh(mesh);
    auto mat = iris::DefaultMaterial::create();
    mat->setDiffuseColor(QColor(200, 0, 0));
    body->setMaterial(mat);
    {   // the bone scene nodes an imported rig carries
        auto jointRoot = iris::SceneNode::create();
        jointRoot->setName(QStringLiteral("jointRoot"));
        body->addChild(jointRoot);
        auto jointTip = iris::SceneNode::create();
        jointTip->setName(QStringLiteral("jointTip"));
        jointTip->setLocalPos(iris::Vec3(0, 1, 0));
        jointRoot->addChild(jointTip);
    }
    auto clip = buildSwingClip(-90.0f);
    clip->setName(QStringLiteral("Swing"));
    clip->setLooping(false);
    body->addAnimation(clip);
    body->setAnimation(clip);
    doc->getRootNode()->addChild(body);

    SceneMirror mirror(scene);
    mirror.setSource(doc);
    mirror.sync();
    engine->renderOneFrame();

    const NodeId bodyNode = mirror.engineNode(body.data());
    CHECK(bodyNode != 0, "the rigged body reached the engine scene");
    CHECK(scene->hasSkeleton(bodyNode), "...and it is GPU-skinned (the whole premise)");
    if (!bodyNode) return 1;

    // ABOVE the character's head (its top is y=2) but inside the enlarged hull
    // (which reaches y=2.8).
    int px = 0, py = 0;
    probeToPixel(0.0f, 2.35f, W, H, px, py);
    std::printf("      probe pixel (%d,%d)\n", px, py);

    Image img;
    // ---- 1. bind pose, NOTHING selected -----------------------------------
    CHECK(view->readPixels(img), "readPixels (bind pose, no selection)");
    const Colour bare = img.at(px, py);
    std::printf("      unselected bind pose: %.2f %.2f %.2f\n",
                double(bare.r), double(bare.g), double(bare.b));
    CHECK(!isLit(bare), "the probe sits ABOVE the character: empty with no selection");

    // ---- 2. SELECT it: the silhouette reaches the probe --------------------
    mirror.setHighlightedNodes({ body });
    mirror.sync();
    engine->renderOneFrame();
    CHECK(view->readPixels(img), "readPixels (selected, bind pose)");
    const Colour selected = img.at(px, py);
    std::printf("      selected   bind pose: %.2f %.2f %.2f\n",
                double(selected.r), double(selected.g), double(selected.b));
    CHECK(isSelectionYellow(selected),
          "the selection silhouette is drawn (the probe turns selection-yellow)");
    const int yellowRightAtBind = yellowIn(img, W / 2 + 10, W, H / 4, 3 * H / 4);

    // ---- 3. PLAY it: the top half swings 90 degrees ------------------------
    // The authored transport is the document clock: the mirror pushes absolute
    // time and the engine samples the clip.
    doc->setPlaying(true);
    doc->updateSceneAnimation(1.0f);
    mirror.sync();
    engine->renderOneFrame();
    engine->renderOneFrame();
    CHECK(view->readPixels(img), "readPixels (selected, bent 90 degrees)");
    const Colour bent = img.at(px, py);
    std::printf("      selected   bent:      %.2f %.2f %.2f\n",
                double(bent.r), double(bent.g), double(bent.b));

    // THE GATE. Nothing of the character is up there any more, so anything
    // drawn here is a copy that did not follow the pose.
    CHECK(!isSelectionYellow(bent),
          "NO selection-yellow bind-pose twin left behind once the character bends");
    CHECK(!isLit(bent), "the space the bind pose occupied is EMPTY");

    // ...and the silhouette went WITH it. The bent half lies out to the right,
    // a region that held nothing at all while the character stood up straight,
    // so counting selection-coloured pixels there says "the outline moved" with
    // no dependence on exactly where the band falls.
    CHECK(litIn(img, W / 2 + 10, W, H / 4, 3 * H / 4) > 20,
          "the character IS drawn in its bent pose (out to the right)");
    CHECK(yellowIn(img, W / 2 + 10, W, H / 4, 3 * H / 4) > 20,
          "...wearing its silhouette, which followed the pose");
    CHECK(yellowRightAtBind == 0,
          "...a region that carried no silhouette at all before the bend");

    // ---- 4. a MULTI-PIECE character: one shell per piece, each following its
    // own piece. Real exports are several skinned meshes sharing one skeleton
    // (Mixamo's Beta is three), and the shells are a POOL — a per-piece pairing
    // that got crossed would show up as one piece outlined twice and one not at
    // all.
    {
        auto group = iris::SceneNode::create();
        group->setName(QStringLiteral("character"));
        doc->getRootNode()->addChild(group);
        group->addChild(body);                       // keeps its world pose
        auto second = iris::MeshNode::create();      // the same mesh asset, offset
        second->setName(QStringLiteral("tube2"));
        second->setMesh(mesh);
        second->setMaterial(mat);
        second->setLocalPos(iris::Vec3(-1.6f, 0, 0));
        second->addAnimation(clip);
        second->setAnimation(clip);
        {
            auto jointRoot = iris::SceneNode::create();
            jointRoot->setName(QStringLiteral("jointRoot"));
            second->addChild(jointRoot);
            auto jointTip = iris::SceneNode::create();
            jointTip->setName(QStringLiteral("jointTip"));
            jointTip->setLocalPos(iris::Vec3(0, 1, 0));
            jointRoot->addChild(jointTip);
        }
        group->addChild(second);
        mirror.setHighlightedNodes({ group });            // the whole character
        doc->updateSceneAnimation(1.0f);
        mirror.sync();
        engine->renderOneFrame();
        engine->renderOneFrame();
        CHECK(view->readPixels(img), "readPixels (two pieces, both bent, both selected)");
        // The second piece bends to the LEFT of centre; its bind pose would have
        // stood straight up at x = -1.6.
        int sx = 0, sy = 0;
        probeToPixel(-1.6f, 2.35f, W, H, sx, sy);
        CHECK(!isSelectionYellow(img.at(sx, sy)),
              "the second piece left no bind-pose twin either");
        CHECK(yellowIn(img, 0, W / 2 - 10, H / 4, 3 * H / 4) > 20,
              "...and IS outlined where it actually bent");
        mirror.setHighlightedNodes({ body });
        mirror.sync();
        engine->renderOneFrame();
    }

    // ---- 5. one renderable per document node, stopped and playing ---------
    CHECK(scene->itemCount(bodyNode) == 1,
          "the character's node holds exactly ONE engine item while playing");
    doc->setPlaying(false);
    doc->updateSceneAnimation(0.0f);
    mirror.sync();
    engine->renderOneFrame();
    CHECK(scene->itemCount(bodyNode) == 1, "...and exactly one when stopped again");

    mirror.setHighlightedNodes({});
    mirror.sync();
    engine->renderOneFrame();

    mirror.setSource(nullptr);
    engine->destroyScene(scene);
    engine->destroyView(view);
    std::printf(failures ? "FAILURES: %d\n" : "all good (%d failures)\n", failures);
    return failures ? 1 : 0;
}
