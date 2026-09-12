// document.mobility — THE RESOLUTION RULE (SPECS/REALTIME_REFLECTIONS_SPEC.md
// §3.3.2), node by node, driver by driver.
//
// Mobility is the one classification the renderer reads to decide what may be
// baked into the room's lighting (reflection probes, bounce light, cached
// shadow maps) and what has to be handled per frame. It REPLACED
// iris::StaticOverride, so this suite is also the characterisation of what
// replaced it: the setting, what it resolves to, why, and what a move does and
// does not do to it.
//
// THE THREE CLAIMS THAT COST REAL MONEY IF THEY BREAK:
//
//  1. THE RULE IS PREDICTIVE. It reads drivers (physics, avatar, socket,
//     animation, rig clip, particles) and the parent chain — never "did this
//     move". Flipping an object's GI class costs a from-scratch GI rebuild
//     (OgreScene.cpp:390-404), so a promotion during an editor drag would put a
//     half-second freeze in the middle of the gesture.
//  2. A MOVE DOES NOT REWRITE THE USER'S SETTING. Rule 4 (a transform write
//     demotes a static subtree) clears the GRAPH class and nothing else. Until
//     mobility it also wiped the user's override, which made the setting
//     impossible to keep.
//  3. NOTHING GOES BACK TO STATIC BY ITSELF. Not after N still seconds, not at
//     a play stop: each hard flip is a rebuild, and a resting physics body
//     wakes on the next contact.
//
// Document only: the headless NULL render system, no display, no pixels.
#include <QGuiApplication>
#include <cstdio>

#include "irisgl/irisglfwd.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/assets/skeleton.h"
#include "irisgl/document/physics/avatarmovement.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

using iris::Mobility;
using iris::MobilityReason;

/// The resolution AND its reason, as one readable string: "movable/physics".
static QString say(const iris::SceneNodePtr &n)
{
    MobilityReason why = MobilityReason::Default;
    const Mobility m = n->resolvedMobility(&why);
    return QString::fromLatin1(iris::mobilityName(m)) + QLatin1Char('/')
           + QString::fromLatin1(iris::mobilityReasonName(why));
}

static bool is(const iris::SceneNodePtr &n, const char *expected)
{
    const QString got = say(n);
    if (got == QLatin1String(expected)) return true;
    printf("      (resolved %s, expected %s)\n", qUtf8Printable(got), expected);
    return false;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("document-mobility-ogre.log");
    if (!graph.require()) return 1;

    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();

    // ---- the default ------------------------------------------------------
    {
        auto prop = iris::SceneNode::create();
        root->addChild(prop, false);
        CHECK(prop->mobility() == Mobility::Auto, "a fresh node's SETTING is auto");
        CHECK(is(prop, "static/default"), "a plain node resolves static/default");
    }

    // ---- rule 1: one driver at a time, each with its own reason -----------
    {
        auto body = iris::SceneNode::create();
        root->addChild(body, false);
        body->isPhysicsBody = true;
        body->physicsProperty.type = iris::PhysicsType::RigidBody;
        body->physicsProperty.objectMass = 1.0f;
        CHECK(is(body, "movable/physics"), "a SIMULATED physics body resolves movable/physics");

        // AN IMMOVABLE BODY IS NOT A MOVER. The default scene's GROUND is a
        // physics body of type Static — the thing a character walks on — so a
        // rule that read `isPhysicsBody` alone classified the floor of every
        // new project as moving, and in lane R2 that takes the floor out of the
        // reflection probes and the bounce light. (Found by this lane's play
        // suite on the real default scene, 2026-09-12.)
        body->physicsProperty.type = iris::PhysicsType::Static;
        body->physicsProperty.objectMass = 0.0f;
        CHECK(is(body, "static/default"), "a STATIC physics body (the ground) does NOT move");
        body->physicsProperty.type = iris::PhysicsType::RigidBody;
        body->physicsProperty.objectMass = 0.0f;
        CHECK(is(body, "static/default"), "...nor does a zero-mass one (the same thing in Bullet)");
        body->physicsProperty.objectMass = 1.0f;
        body->isPhysicsBody = false;
        CHECK(is(body, "static/default"), "...and static again once it is not one");

        body->setAvatarComponent(iris::AvatarMovementPtr(new iris::AvatarMovement()));
        CHECK(is(body, "movable/avatar"), "an avatar wrapper resolves movable/avatar");
        body->setAvatarComponent(iris::AvatarMovementPtr());

        body->setSocketAttachment("owner-guid", "head");
        CHECK(is(body, "movable/socket"), "a socket rider resolves movable/socket");
        body->setSocketAttachment(QString(), QString());
        CHECK(is(body, "static/default"), "...and static again once it is off the socket");
    }

    // A CHANNEL-LESS animation drives nothing (the 2026-09-06 wipe-out: the
    // animation panel gives every node it is shown an empty Animation and the
    // writer persists it, so most shipped nodes carry one).
    {
        auto idle = iris::SceneNode::create();
        root->addChild(idle, false);
        idle->setAnimation(iris::Animation::create("Animation"));
        CHECK(is(idle, "static/default"),
              "a CHANNEL-LESS animation resolves static (the shipped-content shape)");

        auto anim = iris::Animation::create("Move");
        auto *track = new iris::Vector3DPropertyAnim();
        track->setName("position");
        anim->addPropertyAnim(track);
        idle->setAnimation(anim);
        CHECK(is(idle, "movable/animation"),
              "an animation with a real channel resolves movable/animation");
    }

    // A rig WITH A CLIP moves; a rig without one renders at rest and does not.
    {
        auto rigged = iris::MeshNode::create();
        rigged->setMesh(":assets/models/cube.obj");
        root->addChild(rigged, false);
        rigged->skeleton = iris::Skeleton::create();
        CHECK(is(rigged, "static/default"),
              "a rig with NO clip resolves static (it renders at its rest pose)");
        if (!rigged->mesh.isNull())
            rigged->mesh->addSkeletalAnimation("Walk", iris::SkeletalAnimation::create());
        CHECK(is(rigged, "movable/skeleton"), "a rig WITH a clip resolves movable/skeleton");

        // ...and the same clip carried on an Animation rather than the asset.
        auto other = iris::MeshNode::create();
        root->addChild(other, false);
        auto clip = iris::Animation::create("Clip");
        clip->setSkeletalAnimation(iris::SkeletalAnimation::create());
        other->setAnimation(clip);
        CHECK(is(other, "movable/skeleton"), "a skeletal clip on an Animation resolves the same");
    }

    {
        auto emitter = iris::ParticleSystemNode::create();
        root->addChild(emitter, false);
        CHECK(is(emitter, "movable/particles"), "a particle emitter resolves movable/particles");
    }

    // A light is not a driver: a lamp nobody animates does not move. (It can
    // never be GRAPH-static — a different question, see isStaticEligible.)
    {
        auto lamp = iris::LightNode::create();
        root->addChild(lamp, false);
        CHECK(is(lamp, "static/default"), "an undriven light resolves static/default");
        CHECK(!lamp->isStaticEligible(), "...while never being eligible for the static GRAPH class");
    }

    // ---- rule 2: it travels with its parent -------------------------------
    {
        auto carrier = iris::SceneNode::create();
        auto rider = iris::SceneNode::create();
        auto grandchild = iris::SceneNode::create();
        rider->addChild(grandchild, false);
        carrier->addChild(rider, false);
        root->addChild(carrier, false);
        CHECK(is(rider, "static/default"), "a child of a still parent is static");

        auto anim = iris::Animation::create("Move");
        auto *track = new iris::Vector3DPropertyAnim();
        track->setName("position");
        anim->addPropertyAnim(track);
        carrier->setAnimation(anim);
        CHECK(is(carrier, "movable/animation"), "the carrier is animated");
        CHECK(is(rider, "movable/parent"), "its child resolves movable/parent");
        CHECK(is(grandchild, "movable/parent"), "...and so does the whole branch under it");
    }

    // ---- rule 3: the user's setting, and where it does and does not hold ---
    {
        auto pinned = iris::SceneNode::create();
        root->addChild(pinned, false);
        pinned->setMobility(Mobility::Movable);
        CHECK(is(pinned, "movable/user"), "an explicit Movable on a plain node wins");
        CHECK(!pinned->isStaticInGraph(), "...and takes it out of the static graph half");

        pinned->setMobility(Mobility::Static);
        CHECK(is(pinned, "static/user"), "an explicit Static reads back as the user's");
        CHECK(pinned->isStaticInGraph(), "...and puts it back in the static graph half");

        // A DRIVER BEATS THE SETTING, openly: the setting is recorded, the
        // resolution says physics, and removing the body makes it true.
        pinned->isPhysicsBody = true;
        CHECK(is(pinned, "movable/physics"), "an explicit Static on a physics body does NOT hold");
        CHECK(pinned->mobility() == Mobility::Static,
              "...but the setting is still recorded (it becomes true when the body goes)");
        pinned->isPhysicsBody = false;
        CHECK(is(pinned, "static/user"), "...and it does, the moment the body is removed");
        pinned->setMobility(Mobility::Auto);
        CHECK(is(pinned, "static/default"), "back to auto");
    }

    // ---- AN EDITOR DRAG DOES NOT PROMOTE ----------------------------------
    // The claim the whole design rests on. A drag demotes the cheap GRAPH class
    // (rule 4) and touches nothing else — no promotion, no setting rewritten.
    {
        auto dragged = iris::SceneNode::create();
        root->addChild(dragged, false);
        dragged->applyStaticDefaults();
        CHECK(dragged->isStaticInGraph(), "the classification pass marked it graph-static");
        CHECK(is(dragged, "static/default"), "...and it resolves static");

        dragged->setLocalPos(iris::Vec3(3, 0, 0));
        CHECK(is(dragged, "static/default"),
              "MOVING IT IN THE EDITOR DOES NOT PROMOTE IT (a GI flip costs a full rebuild)");
        CHECK(!dragged->isStaticInGraph(), "...though the graph class did demote (rule 4)");

        // ...and the same with a setting on it: rule 4 used to wipe it.
        auto kept = iris::SceneNode::create();
        root->addChild(kept, false);
        kept->setMobility(Mobility::Static);
        kept->setLocalPos(iris::Vec3(1, 0, 0));
        CHECK(kept->mobility() == Mobility::Static,
              "A MOVE DOES NOT CLEAR THE USER'S SETTING (it used to, with StaticOverride)");
        CHECK(is(kept, "static/user"), "...so it still resolves static/user after the drag");
    }

    // ---- soft promotion: play only, and only for `auto` --------------------
    {
        auto prop = iris::SceneNode::create();
        root->addChild(prop, false);
        prop->_setSoftMovable(true);
        CHECK(is(prop, "movable/play"),
              "a soft promotion resolves movable/play (the surprise mover, O3)");

        auto pinnedStatic = iris::SceneNode::create();
        root->addChild(pinnedStatic, false);
        pinnedStatic->setMobility(Mobility::Static);
        pinnedStatic->_setSoftMovable(true);
        CHECK(is(pinnedStatic, "static/user"),
              "...and it never overrules an explicit Static (the user's ghost is their choice)");

        // PLAY STOP clears it for the whole scene, in the document, so nothing
        // carries a promotion into the next session.
        scene->setPlaying(true);
        scene->setPlaying(false);
        CHECK(!prop->softMovable() && is(prop, "static/default"),
              "play stop clears every soft promotion (Scene::setPlaying)");
    }

    // ---- the classification pass ------------------------------------------
    // A branch with a mover in it: the mover and everything under it stay
    // dynamic, the still siblings are marked, and no reason survives into the
    // file (applyStaticDefaults never records a user decision).
    {
        auto branch = iris::SceneNode::create();
        auto still = iris::SceneNode::create();
        auto mover = iris::SceneNode::create();
        auto underMover = iris::SceneNode::create();
        mover->addChild(underMover, false);
        branch->addChild(still, false);
        branch->addChild(mover, false);
        root->addChild(branch, false);
        mover->isPhysicsBody = true;
        branch->applyStaticDefaults();
        CHECK(branch->isStaticInGraph() && still->isStaticInGraph(),
              "the pass marks the still half of a branch");
        CHECK(!mover->isStaticInGraph() && !underMover->isStaticInGraph(),
              "...and leaves the mover and its subtree alone");
        CHECK(branch->mobility() == Mobility::Auto && still->mobility() == Mobility::Auto,
              "the pass records NO user decision (that would freeze today's rule into the file)");
        CHECK(is(underMover, "movable/parent"), "the child of the mover resolves movable/parent");
    }

    printf(failures ? "document.mobility: %d FAILURES\n" : "document.mobility: all passed\n",
           failures);
    return failures ? 1 : 0;
}
