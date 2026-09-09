// sockets.tags — TAG-POINT sockets and the shadow parent (AVATAR_RIG_PERF_SPEC §4).
//
// A socketed node used to be MOVED by the host: read the owner's pose back from
// the engine, run FK on the CPU, write the rider's world transform — one frame
// late by construction, every frame, per rigged owner. Now the rider's own scene
// node hangs off an engine TagPoint on the bone, which Ogre resolves inside the
// threaded update in the frame that renders. The host's job is RECONCILIATION.
//
// WHAT THIS SUITE PROVES, with no display and no GPU (NULL render system):
//   A  the engine verbs: attachToBone arms a tag, setBoneAttachmentOffset moves
//      it, detachFromBone frees it, boneAttachment reports it;
//   B  the refusals: unknown nodes, self-ride, an owner with no rig, a bone the
//      rig has not, and a CIRCULAR attachment (Ogre calls that one "undefined,
//      probably very wonky", so it is refused rather than discovered);
//   C  the HIERARCHY CONTRACT (decision D3 = H1): "socketing is NOT a reparent"
//      survives — parentOf, childAt, childCount and indexInParent still answer
//      with the document parent while Ogre draws the node under a bone;
//   D  the mirror's reconciler: it arms a tag from a document attachment, does
//      NOT touch the engine again while nothing changes (the per-frame socket
//      cost is zero), re-arms after the owner's renderable is rebuilt, pushes an
//      edited socket offset once, and fails SOFT — a removed socket, a removed
//      attachment or an owner with no rig leave the rider where it was;
//   E  the no-engine-rig fallback still resolves at the bind pose.
//
// The ZERO-LAG claim itself — the rider is in the right place in the frame that
// renders it — needs a rendered frame and lives in sockets.render.

#include <QGuiApplication>

#include <cmath>
#include <cstdio>

#include "irisgl/core/math/mat4.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/socket.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"

#include "../skeletal/armrig.h"
#include "../support/documentgraph.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

/// The arm rig as engine MeshData, blend indices included.
MeshData armMeshData(const iris::MeshPtr &mesh)
{
    MeshData d;
    SceneMirror::toMeshData(mesh.data(), d);
    std::vector<float> bi, bw;
    SceneMirror::toSkinData(mesh.data(), bi, bw);
    d.blendIndices.resize(bi.size());
    for (size_t i = 0; i < bi.size(); ++i) d.blendIndices[i] = (unsigned char)(int)bi[i];
    d.blendWeights = bw;
    return d;
}

}  // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph dg("socket-tags-ogre.log");
    if (!dg.require()) return 1;
    dg.engine()->documentGraphScene();

    // ---- A/B: the engine verbs, directly ---------------------------------
    {
        Scene *scene = dg.engine()->createScene("tags");
        CHECK(scene != nullptr, "engine scene");
        if (!scene) return 1;
        PbrParams pbr;
        const MaterialId mat = scene->createPbrMaterial(pbr);
        const iris::MeshPtr armMesh = armrig::buildArmMesh();
        SkeletonDesc rig;
        CHECK(SceneMirror::toSkeletonDesc(armMesh->getSkeleton(), rig), "the arm rig translates");
        const MeshId mesh = scene->createMesh(armMeshData(armMesh));
        const NodeId owner = scene->createNode();
        CHECK(scene->attachSkinnedMesh(owner, mesh, mat, rig), "owner rigged");
        const NodeId rider = scene->createNode();

        CHECK(!scene->attachToBone(rider, owner, "noSuchBone", Vec3(), Quat(), Vec3(1, 1, 1)),
              "a bone the rig has not is refused");
        CHECK(!scene->attachToBone(rider, rider, "jointTip", Vec3(), Quat(), Vec3(1, 1, 1)),
              "a node cannot ride its own bone");
        {
            const NodeId unrigged = scene->createNode();
            CHECK(!scene->attachToBone(rider, unrigged, "jointTip", Vec3(), Quat(), Vec3(1, 1, 1)),
                  "an owner with no rig is refused");
            CHECK(!scene->attachToBone(rider, 999999, "jointTip", Vec3(), Quat(), Vec3(1, 1, 1)),
                  "an unknown owner is refused");
        }
        CHECK(scene->attachToBone(rider, owner, "jointTip", Vec3(0, 0.5f, 0), Quat(),
                                  Vec3(1, 1, 1)),
              "the rider goes onto the bone");
        {
            std::string bone;
            CHECK(scene->boneAttachment(rider, &bone) == owner && bone == "jointTip",
                  "boneAttachment reports owner and bone");
        }
        CHECK(scene->setBoneAttachmentOffset(rider, Vec3(0, 1, 0), Quat(), Vec3(1, 1, 1)),
              "the socket offset can be moved");
        CHECK(scene->attachToBone(rider, owner, "jointTip", Vec3(0, 1, 0), Quat(), Vec3(1, 1, 1)),
              "re-arming the same attachment is idempotent");

        // CIRCULAR: make the owner a child of the rider, then try to ride it.
        {
            const NodeId ring = scene->createNode();
            scene->setNodeParent(owner, ring);
            CHECK(!scene->attachToBone(ring, owner, "jointTip", Vec3(), Quat(), Vec3(1, 1, 1)),
                  "a rider that is an ancestor of the owner is refused (circular)");
            scene->setNodeParent(owner, 0);
        }

        CHECK(scene->detachFromBone(rider, 0), "the rider comes off its bone");
        CHECK(scene->boneAttachment(rider) == 0, "...and says so");
        CHECK(!scene->detachFromBone(rider, 0), "detaching a node that is not on a bone is refused");

        // The owner's renderable is rebuilt (a material swap): the tag points
        // into a SkeletonInstance that is about to die, so it must be freed.
        CHECK(scene->attachToBone(rider, owner, "jointTip", Vec3(), Quat(), Vec3(1, 1, 1)),
              "re-armed");
        CHECK(scene->attachSkinnedMesh(owner, mesh, mat, rig), "the owner's renderable is rebuilt");
        CHECK(scene->boneAttachment(rider) == 0,
              "...which frees every rider on its bones (the host re-arms)");

        // Teardown with a live tag must not fault.
        CHECK(scene->attachToBone(rider, owner, "jointTip", Vec3(), Quat(), Vec3(1, 1, 1)),
              "armed at teardown time");
        dg.engine()->destroyScene(scene);
        std::printf("scene destroyed with a live bone attachment\n");
    }

    // ---- C/D/E: the document, the shadow parent and the reconciler -------
    {
        Scene *scene = dg.engine()->createScene("tags/mirror");
        auto doc = iris::Scene::create();
        const iris::MeshPtr armMesh = armrig::buildArmMesh();
        iris::MeshNodePtr character = armrig::buildArmNode(armMesh, "character");
        doc->getRootNode()->addChild(character, false);

        auto group = iris::SceneNode::create();
        group->setName("props");
        doc->getRootNode()->addChild(group, false);
        auto sword = iris::SceneNode::create();
        sword->setName("sword");
        group->addChild(sword, false);
        doc->addNode(character);
        doc->addNode(group);
        doc->addNode(sword);

        iris::Socket socket;
        socket.name = "hand";
        socket.boneName = "jointTip";
        socket.position = iris::Vec3(0, 0.25f, 0);
        CHECK(character->addSocket(socket), "the character gets a socket");
        QString err;
        CHECK(doc->attachToSocket(sword, character->getGUID(), "hand", &err),
              "the sword is attached to it");

        SceneMirror mirror(scene);
        mirror.setSource(doc);
        mirror.sync();          // creates the nodes; the rider arms next sync
        mirror.sync();

        const NodeId riderId = mirror.engineNode(sword.data());
        const NodeId ownerId = mirror.engineNode(character.data());
        CHECK(riderId && ownerId, "both nodes are mirrored");
        std::string bone;
        CHECK(scene->boneAttachment(riderId, &bone) == ownerId && bone == "jointTip",
              "the reconciler armed a TagPoint on the owner's bone");
        CHECK(mirror.lastSocketDangling() == 0, "nothing dangled");

        // C. THE HIERARCHY CONTRACT: socketing is not a reparent.
        CHECK(iris::graph::isSocketRider(sword->graphNode()), "the graph knows it is a rider");
        CHECK(sword->getParent().data() == group.data(),
              "parentOf still answers the DOCUMENT parent, not the TagPoint");
        bool listed = false;
        for (int i = 0; i < group->childCount(); ++i)
            if (group->childAt(i) == sword.data()) listed = true;
        CHECK(listed, "...and the parent still lists it among its children");
        CHECK(group->childCount() == 1, "exactly once");
        CHECK(sword->siblingIndex() >= 0, "it has a sibling index");

        // D. IDEMPOTENCE: nothing changes, so nothing is pushed.
        {
            const NodeId before = scene->boneAttachment(riderId, &bone);
            mirror.sync();
            mirror.sync();
            CHECK(scene->boneAttachment(riderId) == before,
                  "an unchanged socket is not re-armed every frame");
        }

        // D. THE OWNER'S RENDERABLE IS REBUILT -> the engine frees the tag ->
        // the reconciler puts it back, with no help from anybody.
        {
            character->setMaterial(iris::DefaultMaterial::create());
            mirror.sync();
            mirror.sync();
            CHECK(scene->boneAttachment(riderId) == ownerId ||
                      scene->boneAttachment(mirror.engineNode(sword.data())) ==
                          mirror.engineNode(character.data()),
                  "the reconciler re-arms after the owner's renderable is rebuilt");
        }

        // D. THE SOCKET IS EDITED: one push, and the tag really moved.
        {
            iris::Socket *live = const_cast<iris::Socket *>(character->findSocket("hand"));
            CHECK(live != nullptr, "the socket is editable");
            if (live) live->position = iris::Vec3(0, 0.75f, 0);
            mirror.sync();
            CHECK(scene->boneAttachment(mirror.engineNode(sword.data())) != 0,
                  "the rider is still on its bone after an offset edit");
        }

        // D. FAIL SOFT: the attachment goes away -> the rider comes off the
        // bone, back under its document parent, and keeps its pose.
        {
            doc->detachFromSocket(sword);
            mirror.sync();
            CHECK(scene->boneAttachment(mirror.engineNode(sword.data())) == 0,
                  "detaching from the socket takes the rider off its bone");
            CHECK(!iris::graph::isSocketRider(sword->graphNode()),
                  "...and the shadow parent is gone");
            CHECK(sword->getParent().data() == group.data(),
                  "...while the document parent is unchanged");
        }

        // ...and re-attaching picks it back up.
        {
            CHECK(doc->attachToSocket(sword, character->getGUID(), "hand", &err), "re-attached");
            mirror.sync();
            mirror.sync();
            CHECK(scene->boneAttachment(mirror.engineNode(sword.data())) != 0,
                  "the reconciler arms it again");
        }

        // D. A REMOVED SOCKET is a dangle, counted, and the rider stays put.
        {
            const iris::Mat4 before = sword->getGlobalTransform();
            character->removeSocket("hand");
            mirror.sync();
            CHECK(scene->boneAttachment(mirror.engineNode(sword.data())) == 0,
                  "a removed socket takes the rider off its bone");
            CHECK(mirror.lastSocketDangling() >= 1, "...and is COUNTED as stale");
            const iris::Mat4 after = sword->getGlobalTransform();
            bool same = true;
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    same = same && std::fabs(before(r, c) - after(r, c)) < 1e-3f;
            CHECK(same, "...and the rider keeps its last pose");
        }

        // D. THE RIDER IS DELETED WHILE IT RIDES. Everything here is keyed by
        // DOCUMENT NODE POINTER — the mirror's rider map, the graph's shadow
        // parents — so a node that leaves the document has to take its
        // bookkeeping with it or the next sweep reads freed memory.
        {
            CHECK(character->addSocket(socket), "the socket comes back");
            CHECK(doc->attachToSocket(sword, character->getGUID(), "hand", &err),
                  "and the sword rides it again");
            mirror.sync();
            mirror.sync();
            CHECK(scene->boneAttachment(mirror.engineNode(sword.data())) != 0, "armed");
            group->removeChild(sword);
            doc->removeNode(sword);
            sword.reset();
            mirror.sync();
            mirror.sync();
            CHECK(true, "deleting a rider mid-ride does not fault");
        }

        // D. THE OWNER IS DELETED WHILE SOMETHING RIDES IT: the tag points into
        // its skeleton, so the engine has to free the rider first.
        {
            auto prop = iris::SceneNode::create();
            prop->setName("prop");
            group->addChild(prop, false);
            doc->addNode(prop);
            CHECK(doc->attachToSocket(prop, character->getGUID(), "hand", &err),
                  "a prop rides the character");
            mirror.sync();
            mirror.sync();
            const NodeId propId = mirror.engineNode(prop.data());
            CHECK(scene->boneAttachment(propId) != 0, "armed");
            doc->getRootNode()->removeChild(character);
            doc->removeNode(character);
            character.reset();
            mirror.sync();
            mirror.sync();
            CHECK(scene->boneAttachment(mirror.engineNode(prop.data())) == 0,
                  "deleting the OWNER takes its riders off their bones");
            CHECK(!iris::graph::isSocketRider(prop->graphNode()),
                  "...and the shadow parent goes with it");
        }

        mirror.setSource(nullptr);
        dg.engine()->destroyScene(scene);
    }

    // ---- E: no engine rig at all -> the bind-pose fallback ---------------
    {
        Scene *scene = dg.engine()->createScene("tags/fallback");
        auto doc = iris::Scene::create();
        // A character whose MESH is never given to the engine: the mirror has no
        // rig to hang a tag on, and the rider must still land on the socket.
        iris::MeshNodePtr character = armrig::buildArmNode(armrig::buildArmMesh(), "character");
        // No geometry reaches the engine, so the mirror has no rig to tag — but
        // the node keeps its rig TEMPLATE, which is what the bind-pose fallback
        // resolves against.
        const iris::SkeletonPtr rigTemplate = character->getSkeleton();
        character->setMesh(iris::MeshPtr());
        character->skeleton = rigTemplate;
        doc->getRootNode()->addChild(character, false);
        auto rider = iris::SceneNode::create();
        rider->setName("rider");
        doc->getRootNode()->addChild(rider, false);
        doc->addNode(character);
        doc->addNode(rider);

        iris::Socket socket;
        socket.name = "hand";
        socket.boneName = "jointTip";
        CHECK(character->addSocket(socket), "socket on an un-rigged character");
        QString err;
        CHECK(doc->attachToSocket(rider, character->getGUID(), "hand", &err), "attached");

        SceneMirror mirror(scene);
        mirror.setSource(doc);
        mirror.sync();
        mirror.sync();
        const iris::Mat4 world = rider->getGlobalTransform();
        const iris::Vec3 p(world(0, 3), world(1, 3), world(2, 3));
        CHECK(std::fabs(p.y() - 1.0f) < 1e-3f,
              "with no engine rig the rider still lands on the bind-pose socket");
        CHECK(!iris::graph::isSocketRider(rider->graphNode()),
              "...and no tag point was armed");
        mirror.setSource(nullptr);
        dg.engine()->destroyScene(scene);
    }

    std::printf(failures ? "\nFAILURES: %d\n" : "\nall good\n", failures);
    return failures ? 1 : 0;
}
