// sockets.document — CAMERAS_SPEC §5 (owner decision D9), the document half.
//
// A socket is {name, bone, offset} on a skinned MeshNode; a node attached to
// one is driven every frame to `boneWorld * offset`. NONE of that needs an
// engine — the pose is an INJECTED source (iris::BonePoseSource), which is
// precisely what makes the maths assertable here against matrices written down
// by hand instead of against pixels.
//
// Sections:
//   A  the socket maths: boneWorld * offset, resolved onto the attached node,
//      against a hand-computed expectation
//   B  the bind-pose fallback — a host with no engine still resolves sockets
//   C  refusals and dangling: unrigged mesh, wrong bone, duplicate name, a
//      non-mesh owner, a socket that does not exist, a cycle, and the
//      re-import-renamed-a-bone case (fail soft, counted, never thrown)
//   D  duplication: sockets travel with a copied node, and a copied SUBTREE
//      re-points its riders at the COPY's owner
//   E  the scene's attachment registry across add/remove/detach
//   F  the avatar module's built-in bone-name mapping (avatarsockets.cpp)
//
// Pure iris:: + avatar::sockets. No engine, no window, offscreen QPA.

#include <QGuiApplication>
#include <cmath>
#include <cstdio>

#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/socket.h"

#include "modules/avatar/avatarsockets.h"

#include "../skeletal/armrig.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool nearVec(const iris::Vec3 &a, const iris::Vec3 &b, float eps = 1e-4f)
{
    return std::fabs(a.x() - b.x()) < eps && std::fabs(a.y() - b.y()) < eps
        && std::fabs(a.z() - b.z()) < eps;
}

static bool nearMat(const iris::Mat4 &a, const iris::Mat4 &b, float eps = 1e-4f)
{
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (std::fabs(a(r, c) - b(r, c)) > eps) return false;
    return true;
}

/// A scene with one rigged arm under the root, plus a camera beside it.
struct Fixture
{
    iris::ScenePtr scene;
    iris::MeshNodePtr arm;
    iris::CameraNodePtr camera;
};

static Fixture makeFixture()
{
    Fixture f;
    f.scene = iris::Scene::create();
    f.arm = armrig::buildArmNode(armrig::buildArmMesh(), "arm");
    f.scene->getRootNode()->addChild(f.arm);
    f.camera = iris::CameraNode::create();
    f.camera->setName("cam");
    f.scene->getRootNode()->addChild(f.camera);
    f.scene->getRootNode()->update(0.0f);
    return f;
}

// ===========================================================================
int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    // ---- A: the socket maths ---------------------------------------------
    std::printf("\n-- A: boneWorld * offset --\n");
    {
        Fixture f = makeFixture();
        // The rig itself is placed somewhere non-trivial: the owner's own world
        // transform is part of the answer, and a fixture at the origin with an
        // identity rotation cannot tell a correct implementation from one that
        // forgot the mesh node entirely.
        f.arm->setLocalPos(iris::Vec3(3, 0, -2));
        f.arm->setLocalRot(iris::Quat::fromAxisAndAngle(0, 1, 0, 90));
        f.scene->getRootNode()->update(0.0f);

        iris::Socket socket;
        socket.name = "head";
        socket.boneName = "jointTip";
        socket.position = iris::Vec3(0, 0.25f, 0.5f);
        socket.rotation = iris::Quat::fromAxisAndAngle(1, 0, 0, 30);
        QString error;
        CHECK(f.arm->addSocket(socket, &error), "a socket lands on a bone of the node's rig");

        // The POSE SOURCE: jointTip is at y=2 in the rig's own space, turned
        // 45 degrees about Z. Written out by hand — nothing in the document
        // computes it, which is the whole point of the injected source.
        iris::Mat4 tipWorldLocal;                       // in the arm's space
        tipWorldLocal.translate(iris::Vec3(0, 2, 0));
        tipWorldLocal.rotate(iris::Quat::fromAxisAndAngle(0, 0, 1, 45));
        const iris::Mat4 tipWorld = f.arm->getGlobalTransform() * tipWorldLocal;

        iris::BonePoseSource source = [&](iris::MeshNode *node, QHash<QString, iris::Mat4> &out) {
            out.clear();
            if (node != f.arm.data()) return false;
            out.insert("jointRoot", f.arm->getGlobalTransform());
            out.insert("jointTip", tipWorld);
            return true;
        };

        iris::Mat4 socketWorld;
        CHECK(iris::socketWorldTransform(f.arm.data(), "head", source, socketWorld),
              "socketWorldTransform resolves through the injected pose");
        CHECK(nearMat(socketWorld, tipWorld * socket.offsetMatrix()),
              "…and it is exactly boneWorld * offset");

        QString attachError;
        CHECK(f.scene->attachToSocket(f.camera, f.arm->getGUID(), "head", &attachError),
              "the camera attaches to the socket");

        iris::SocketResolver resolver;
        resolver.setPoseSource(source);
        CHECK(resolver.resolve(f.scene.data()) == 1, "one node resolved");
        CHECK(resolver.lastDangling() == 0, "…and nothing dangled");
        CHECK(nearMat(f.camera->getGlobalTransform(), socketWorld),
              "the attached node's WORLD transform is the socket's");

        // The rig moves: the rider follows, with no second attach.
        iris::Mat4 movedTip;
        movedTip.translate(iris::Vec3(0, 2, 0));
        movedTip.rotate(iris::Quat::fromAxisAndAngle(0, 0, 1, -60));
        const iris::Vec3 before = f.camera->getGlobalPosition();
        const iris::Mat4 saved = tipWorld;
        (void)saved;
        tipWorldLocal = movedTip;
        const iris::Mat4 tipWorld2 = f.arm->getGlobalTransform() * movedTip;
        iris::BonePoseSource moved = [&](iris::MeshNode *node, QHash<QString, iris::Mat4> &out) {
            out.clear();
            if (node != f.arm.data()) return false;
            out.insert("jointTip", tipWorld2);
            return true;
        };
        resolver.setPoseSource(moved);
        resolver.resolve(f.scene.data());
        CHECK(!nearVec(f.camera->getGlobalPosition(), before, 1e-3f),
              "a moved bone MOVES its rider (the whole point of a socket)");
        CHECK(nearMat(f.camera->getGlobalTransform(), tipWorld2 * socket.offsetMatrix()),
              "…to exactly the new boneWorld * offset");

        // The rider keeps its place in the hierarchy — a socket is not a
        // reparent, which is what lets a camera stay a top-level scene citizen.
        CHECK(f.camera->getParent() == f.scene->getRootNode(),
              "attaching did NOT reparent the rider");
    }

    // A2: an attached node under a MOVED parent still lands on the bone — the
    // local transform is written through the parent's inverse, and a fixture
    // with an identity parent would never catch a missed one.
    {
        Fixture f = makeFixture();
        auto group = iris::SceneNode::create();
        group->setName("group");
        group->setLocalPos(iris::Vec3(-7, 4, 11));
        group->setLocalRot(iris::Quat::fromAxisAndAngle(0, 0, 1, 33));
        group->setLocalScale(iris::Vec3(2, 2, 2));
        f.scene->getRootNode()->addChild(group);
        group->addChild(f.camera, false);
        f.scene->getRootNode()->update(0.0f);

        iris::Socket socket;
        socket.name = "head";
        socket.boneName = "jointTip";
        socket.position = iris::Vec3(0, 0, 1);
        f.arm->addSocket(socket);
        f.scene->attachToSocket(f.camera, f.arm->getGUID(), "head");

        iris::Mat4 tipWorld;
        tipWorld.translate(iris::Vec3(1, 2, 3));
        iris::SocketResolver resolver;
        resolver.setPoseSource([&](iris::MeshNode *, QHash<QString, iris::Mat4> &out) {
            out.clear(); out.insert("jointTip", tipWorld); return true;
        });
        resolver.resolve(f.scene.data());
        CHECK(nearVec(f.camera->getGlobalPosition(), iris::Vec3(1, 2, 4)),
              "a rider under a scaled+rotated parent still lands on the bone");
    }

    // ---- B: the bind-pose fallback ---------------------------------------
    std::printf("\n-- B: no engine, no pose source: the bind pose --\n");
    {
        Fixture f = makeFixture();
        f.arm->setLocalPos(iris::Vec3(0, 5, 0));
        f.scene->getRootNode()->update(0.0f);

        iris::Socket socket;
        socket.name = "head";
        socket.boneName = "jointTip";
        f.arm->addSocket(socket);
        f.scene->attachToSocket(f.camera, f.arm->getGUID(), "head");

        QHash<QString, iris::Mat4> bind;
        CHECK(iris::bindPoseWorldTransforms(f.arm.data(), bind),
              "the rig's bind pose is available with no engine at all");
        CHECK(bind.size() == 2, "…one entry per bone");

        iris::SocketResolver resolver;               // NO pose source installed
        CHECK(!resolver.hasPoseSource(), "the resolver has no pose source");
        CHECK(resolver.resolve(f.scene.data()) == 1, "…and still resolves the attachment");
        // jointTip binds one unit up the arm, and the arm sits at y=5.
        CHECK(nearVec(f.camera->getGlobalPosition(), iris::Vec3(0, 6, 0)),
              "the rider sits on the BIND pose of the bone (arm at y=5, tip +1)");
    }

    // ---- C: refusals and dangling ----------------------------------------
    std::printf("\n-- C: refusals --\n");
    {
        Fixture f = makeFixture();
        QString error;

        auto plain = iris::MeshNode::create();       // a mesh with NO rig
        plain->setName("rock");
        f.scene->getRootNode()->addChild(plain);
        iris::Socket socket;
        socket.name = "head";
        socket.boneName = "jointTip";
        error.clear();
        CHECK(!plain->addSocket(socket, &error) && error.contains("no rig"),
              "an UNRIGGED mesh refuses a socket, and says why");

        error.clear();
        iris::Socket wrongBone;
        wrongBone.name = "head";
        wrongBone.boneName = "mixamorig:Head";
        CHECK(!f.arm->addSocket(wrongBone, &error) && error.contains("no bone named"),
              "a bone the rig does not have is refused");

        CHECK(f.arm->addSocket(socket), "the good socket goes on");
        error.clear();
        CHECK(!f.arm->addSocket(socket, &error) && error.contains("already has a socket"),
              "a duplicate socket NAME is refused");

        iris::Socket unnamed;
        unnamed.boneName = "jointTip";
        error.clear();
        CHECK(!f.arm->addSocket(unnamed, &error), "an unnamed socket is refused");

        error.clear();
        CHECK(!f.scene->attachToSocket(f.camera, plain->getGUID(), "head", &error)
                  && error.contains("no socket named"),
              "attaching to a socket that does not exist is refused");
        error.clear();
        CHECK(!f.scene->attachToSocket(f.camera, "not-a-guid", "head", &error)
                  && error.contains("no node with id"),
              "attaching to a node that does not exist is refused");

        auto light = iris::LightNode::create();
        light->setName("lamp");
        f.scene->getRootNode()->addChild(light);
        error.clear();
        CHECK(!f.scene->attachToSocket(f.camera, light->getGUID(), "head", &error)
                  && error.contains("not a mesh"),
              "a LIGHT carries no sockets");

        // The cycle: the owner sits inside the rider's own subtree.
        auto holder = iris::SceneNode::create();
        holder->setName("holder");
        f.scene->getRootNode()->addChild(holder);
        holder->addChild(f.arm, false);
        error.clear();
        CHECK(!f.scene->attachToSocket(holder, f.arm->getGUID(), "head", &error)
                  && error.contains("inside"),
              "a socket may not drive its own owner's ancestor");
        error.clear();
        CHECK(!f.scene->attachToSocket(f.arm, f.arm->getGUID(), "head", &error),
              "…nor itself");
    }

    // C2: the dangling cases — a renamed bone, a removed socket, a deleted
    // owner. None of them throws; all of them simply stop moving the rider.
    {
        Fixture f = makeFixture();
        iris::Socket socket;
        socket.name = "head";
        socket.boneName = "jointTip";
        f.arm->addSocket(socket);
        f.scene->attachToSocket(f.camera, f.arm->getGUID(), "head");

        iris::SocketResolver resolver;
        resolver.resolve(f.scene.data());
        const iris::Vec3 placed = f.camera->getGlobalPosition();
        CHECK(nearVec(placed, iris::Vec3(0, 1, 0)), "the rider is on the bind-pose bone");

        // A re-import renames the bone. The socket stays in the document.
        f.arm->getSkeleton()->bones[1]->name = "mixamorig:Head";
        f.arm->getSkeleton()->boneMap.clear();
        f.arm->getSkeleton()->boneMap.insert("jointRoot", 0);
        f.arm->getSkeleton()->boneMap.insert("mixamorig:Head", 1);
        CHECK(resolver.resolve(f.scene.data()) == 0, "a renamed bone moves nothing");
        CHECK(resolver.lastDangling() == 1, "…and is COUNTED as dangling");
        CHECK(nearVec(f.camera->getGlobalPosition(), placed),
              "…leaving the rider exactly where it was, never at the origin");

        // The socket itself goes away.
        f.arm->getSkeleton()->bones[1]->name = "jointTip";
        f.arm->getSkeleton()->boneMap.clear();
        f.arm->getSkeleton()->boneMap.insert("jointRoot", 0);
        f.arm->getSkeleton()->boneMap.insert("jointTip", 1);
        CHECK(resolver.resolve(f.scene.data()) == 1, "the bone is back, so the rider moves again");
        CHECK(f.arm->removeSocket("head"), "the socket is removed");
        CHECK(resolver.resolve(f.scene.data()) == 0 && resolver.lastDangling() == 1,
              "a removed socket dangles rather than detaching its rider");
        CHECK(f.camera->isSocketAttached(),
              "…the attachment RECORD survives, so re-adding the socket picks it back up");
        f.arm->addSocket(socket);
        CHECK(resolver.resolve(f.scene.data()) == 1, "…which it does");

        // The owner is deleted. Same rule.
        f.scene->getRootNode()->removeChild(f.arm);
        CHECK(resolver.resolve(f.scene.data()) == 0 && resolver.lastDangling() == 1,
              "a deleted owner dangles; the rider keeps its pose");
    }

    // ---- D: duplication ---------------------------------------------------
    std::printf("\n-- D: duplication --\n");
    {
        Fixture f = makeFixture();
        iris::Socket socket;
        socket.name = "head";
        socket.boneName = "jointTip";
        socket.position = iris::Vec3(0, 0, 2);
        f.arm->addSocket(socket);

        auto copy = f.arm->duplicate().staticCast<iris::MeshNode>();
        CHECK(!copy.isNull(), "the rigged node duplicates");
        CHECK(copy->getSockets().size() == 1 && copy->findSocket("head"),
              "the copy carries its own sockets");
        CHECK(copy->getGUID() != f.arm->getGUID(), "…under a new guid");
        CHECK(nearVec(copy->findSocket("head")->position, iris::Vec3(0, 0, 2)),
              "…offset and all");

        // A camera CHILD of the rig, riding the rig's socket: duplicating the
        // subtree must give the COPY's camera the COPY's head. Anything else
        // and every duplicate of a character rides the first character forever.
        auto childCam = iris::CameraNode::create();
        childCam->setName("headcam");
        f.arm->addChild(childCam);
        f.scene->attachToSocket(childCam, f.arm->getGUID(), "head");

        auto copy2 = f.arm->duplicate();
        iris::SceneNodePtr copiedCam;
        for (const auto &child : copy2->children)
            if (child->getName() == "headcam") copiedCam = child;
        CHECK(!copiedCam.isNull(), "the copied subtree carries the camera");
        CHECK(copiedCam->socketOwnerGuid == copy2->getGUID(),
              "…re-pointed at the COPY's rig, not the original's");
        CHECK(copiedCam->socketName == "head", "…same socket name");

        // A rider OUTSIDE the copied subtree keeps its owner: duplicating a
        // camera that rides someone else's head is a second camera on that
        // same head, which is the useful reading.
        f.scene->attachToSocket(f.camera, f.arm->getGUID(), "head");
        auto camCopy = f.camera->duplicate();
        CHECK(camCopy->socketOwnerGuid == f.arm->getGUID(),
              "a duplicated rider keeps riding the SAME owner");
    }

    // ---- E: the scene's attachment registry -------------------------------
    std::printf("\n-- E: the registry --\n");
    {
        Fixture f = makeFixture();
        iris::Socket socket;
        socket.name = "head";
        socket.boneName = "jointTip";
        f.arm->addSocket(socket);
        CHECK(f.scene->socketAttachments.isEmpty(), "nothing is attached to start with");

        f.scene->attachToSocket(f.camera, f.arm->getGUID(), "head");
        CHECK(f.scene->socketAttachments.value(f.arm->getGUID()).size() == 1,
              "the registry is keyed by OWNER (one pose read per rig)");

        auto second = iris::CameraNode::create();
        f.scene->getRootNode()->addChild(second);
        f.scene->attachToSocket(second, f.arm->getGUID(), "head");
        CHECK(f.scene->socketAttachments.value(f.arm->getGUID()).size() == 2,
              "two riders share one bucket");

        CHECK(f.scene->detachFromSocket(second), "detach");
        CHECK(!second->isSocketAttached(), "…clears the record");
        CHECK(f.scene->socketAttachments.value(f.arm->getGUID()).size() == 1,
              "…and the registry");
        CHECK(!f.scene->detachFromSocket(second), "detaching twice is false, not a crash");

        f.scene->getRootNode()->removeChild(f.camera);
        CHECK(f.scene->socketAttachments.isEmpty(),
              "removing the last rider empties its bucket (no strong reference left behind)");
    }

    // A node read from a file arrives with its attachment already set; addNode
    // is what has to register it, or a saved socketed camera comes back
    // stationary.
    {
        Fixture f = makeFixture();
        iris::Socket socket;
        socket.name = "head";
        socket.boneName = "jointTip";
        f.arm->addSocket(socket);

        auto loaded = iris::CameraNode::create();
        loaded->setSocketAttachment(f.arm->getGUID(), "head");    // the reader's path
        f.scene->getRootNode()->addChild(loaded);
        CHECK(f.scene->socketAttachments.value(f.arm->getGUID()).contains(loaded),
              "addNode registers an attachment the node already carried (the reader's path)");
        iris::SocketResolver resolver;
        CHECK(resolver.resolve(f.scene.data()) == 1, "…and it resolves");
    }

    // ---- F: the avatar module's built-in mapping --------------------------
    std::printf("\n-- F: avatar built-ins --\n");
    {
        // A synthetic Mixamo-shaped rig. Real Mixamo files are megabytes; the
        // thing under test is a NAME TABLE, and names are all it needs.
        const auto rigWith = [](const QStringList &names) {
            auto skel = iris::Skeleton::create();
            for (const QString &n : names) skel->addBone(iris::Bone::create(n));
            return skel;
        };

        auto mixamo = rigWith({ "mixamorig:Hips", "mixamorig:Spine", "mixamorig:Neck",
                                "mixamorig:Head", "mixamorig:HeadTop_End",
                                "mixamorig:LeftShoulder", "mixamorig:RightShoulder" });
        CHECK(avatar::sockets::mapBone(mixamo, "head") == "mixamorig:Head",
              "mixamorig:Head is the head");
        CHECK(avatar::sockets::mapBone(mixamo, "shoulder") == "mixamorig:RightShoulder",
              "the RIGHT shoulder wins (the third-person convention)");

        auto biped = rigWith({ "Bip01 Pelvis", "Bip01 Head", "Bip01 R Clavicle",
                               "Bip01 L Clavicle" });
        CHECK(avatar::sockets::mapBone(biped, "head") == "Bip01 Head",
              "3ds Max Biped's spelling maps too (separators and case are normalized)");
        CHECK(avatar::sockets::mapBone(biped, "shoulder") == "Bip01 R Clavicle",
              "…and its clavicle is the shoulder");

        auto leftOnly = rigWith({ "Hips", "head", "LeftShoulder" });
        CHECK(avatar::sockets::mapBone(leftOnly, "head") == "head", "bare lowercase names map");
        CHECK(avatar::sockets::mapBone(leftOnly, "shoulder") == "LeftShoulder",
              "a rig with only a left shoulder still gets a shoulder socket");

        auto tipOnly = rigWith({ "Hips", "HeadTop_End" });
        CHECK(avatar::sockets::mapBone(tipOnly, "head").isEmpty(),
              "HeadTop_End is the skull TIP marker and is NOT the head "
              "(a 'contains head' match would take it and put the camera above the hair)");

        auto none = rigWith({ "Bone_A", "Bone_B" });
        CHECK(avatar::sockets::mapBone(none, "head").isEmpty(), "an unknown rig maps nothing");

        // installBuiltIns on a real node, fail-soft reporting included.
        auto meshWithMixamo = iris::MeshNode::create();
        {
            auto mesh = armrig::buildArmMesh();
            auto skel = iris::Skeleton::create();
            skel->addBone(iris::Bone::create("mixamorig:Hips"));
            skel->addBone(iris::Bone::create("mixamorig:Head"));
            mesh->setSkeleton(skel);
            meshWithMixamo->setMesh(mesh);
            meshWithMixamo->setName("character");
        }
        auto report = avatar::sockets::installBuiltIns(meshWithMixamo);
        CHECK(report.size() == 2, "two built-ins are always reported");
        CHECK(report[0].socket == "head" && report[0].mapped
                  && report[0].bone == "mixamorig:Head",
              "head installed, and the report says which bone it mapped to");
        CHECK(report[1].socket == "shoulder" && !report[1].mapped && report[1].bone.isEmpty(),
              "shoulder FAILS SOFT on a rig with no shoulder — reported, not thrown");
        CHECK(meshWithMixamo->getSockets().size() == 1, "…and only the head socket exists");
        CHECK(meshWithMixamo->findSocket("head")->builtIn,
              "a built-in is marked as one (provenance for the UI and the file)");

        // Re-installing must not discard an authored offset.
        auto authored = *meshWithMixamo->findSocket("head");
        meshWithMixamo->removeSocket("head");
        authored.position = iris::Vec3(0, 0.1f, 0.2f);
        meshWithMixamo->addSocket(authored);
        auto again = avatar::sockets::installBuiltIns(meshWithMixamo);
        CHECK(again[0].existed, "re-installing reports the socket as already there");
        CHECK(nearVec(meshWithMixamo->findSocket("head")->position, iris::Vec3(0, 0.1f, 0.2f)),
              "…and leaves the authored offset alone");

        auto unrigged = iris::MeshNode::create();
        unrigged->setName("rock");
        auto rockReport = avatar::sockets::installBuiltIns(unrigged);
        CHECK(rockReport.size() == 2 && !rockReport[0].mapped && !rockReport[1].mapped,
              "an unrigged node reports two unmapped rows and changes nothing");
        CHECK(unrigged->getSockets().isEmpty(), "…literally nothing");
    }

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
                failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
