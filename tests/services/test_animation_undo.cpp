// services.animation_undo — undo for the keyframe domain (verb-coverage audit
// F16).
//
// Every anim.* write said "Not undoable" in its own doc string, and the
// Timeline panel's insert-key / delete-property / delete-animation buttons had
// no undo either. Both callers already went through the animedits:: service, so
// undo is a property of THAT service's edits: animedits::snapshotTrack /
// restoreTrack are the one primitive, and the five commands in
// src/commands/animationcommands.h are the record.
//
// WHY THIS SUITE AND NOT THE SCRIPT ONE. A --script run is ONE undo macro, and
// QUndoStack refuses to undo into a macro that is still being composed — so
// scripting.e2e.anim can prove the commands were PUSHED (editor.undoState()
// .pushes) but can never call undo on them. The restore semantics have to be
// asserted where there is a plain stack, which is here.
//
// Document only: no engine, no display, no database.

#include <QCoreApplication>
#include <QUndoStack>
#include <QVector3D>
#include <cmath>
#include <cstdio>

#include "commands/animationcommands.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeanimation.h"
#include "irisgl/document/animation/propertyanim.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/animationedits.h"
#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static bool near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

/// The X channel's keys as (time, value) pairs, for readable assertions.
static QVector<QPair<double, float>> xKeys(const iris::AnimationPtr &anim, const QString &prop)
{
    QVector<QPair<double, float>> out;
    if (!anim || !anim->hasPropertyAnim(prop)) return out;
    auto *track = anim->getPropertyAnim(prop);
    if (!track) return out;
    const auto frames = track->getKeyFrames();
    if (frames.isEmpty() || !frames[0].keyFrame) return out;
    for (const auto *key : frames[0].keyFrame->keys) out.append({ key->time, key->value });
    return out;
}

static animedits::PropertyInfo positionOf(const iris::SceneNodePtr &node)
{
    return animedits::animatableProperty(node, QStringLiteral("position"));
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(argc, argv);

    // A document node IS an engine node (SCENEGRAPH_SPEC D2): without this a
    // bare SceneNode silently drops every setLocalPos, and the value half of
    // every assertion below would be measuring nothing.
    enginetest::DocumentGraph graph;
    if (!graph.ok()) { std::printf("FAIL: headless document graph did not boot\n"); return 1; }

    auto node = iris::SceneNode::create();
    node->setName("keyed");
    auto anim = iris::Animation::create("Clip");
    node->addAnimation(anim);
    node->setAnimation(anim);

    CHECK(positionOf(node).isValid(), "the node reports an animatable `position`");

    QUndoStack stack;

    // ---- snapshot/restore is the primitive ---------------------------------
    {
        const auto empty = animedits::snapshotTrack(anim, QStringLiteral("position"));
        CHECK(!empty.exists, "a snapshot of a property with NO track records that honestly");

        node->setLocalPos(iris::Vec3(1, 2, 3));
        CHECK(animedits::setKeyframe(anim, positionOf(node), 0.0), "key at t=0");
        const auto one = animedits::snapshotTrack(anim, QStringLiteral("position"));
        CHECK(one.exists && one.channels.count() == 3,
              "a vec3 track snapshots as THREE channels");
        CHECK(one.channels[0].keys.count() == 1, "...with one key each");
        CHECK(near(one.channels[0].keys[0].value, 1.0), "...carrying the value");

        CHECK(animedits::restoreTrack(anim, empty), "restoring the EMPTY snapshot");
        CHECK(!anim->hasPropertyAnim(QStringLiteral("position")),
              "F16: ...takes the track away again — which is how undo of the FIRST key works");

        CHECK(animedits::restoreTrack(anim, one), "restoring the one-key snapshot");
        CHECK(xKeys(anim, "position").count() == 1, "...rebuilt the track");
        CHECK(animedits::restoreTrack(anim, one), "restoring it TWICE is idempotent");
        CHECK(xKeys(anim, "position").count() == 1,
              "F16: ...and does not merge into itself — which is what makes redo() safe, "
              "given QUndoStack::push replays it immediately");
    }

    // ---- SetKeyframeCommand ------------------------------------------------
    {
        animedits::restoreTrack(anim, animedits::TrackSnapshot{ false, QStringLiteral("position"), {} });
        const int base = stack.count();

        node->setLocalPos(iris::Vec3(0, 0, 0));
        auto before = animedits::snapshotTrack(anim, QStringLiteral("position"));
        animedits::setKeyframe(anim, positionOf(node), 0.0);
        stack.push(new SetKeyframeCommand(anim, "position", before,
                                          animedits::snapshotTrack(anim, "position")));

        node->setLocalPos(iris::Vec3(10, 0, 0));
        before = animedits::snapshotTrack(anim, QStringLiteral("position"));
        animedits::setKeyframe(anim, positionOf(node), 2.0);
        stack.push(new SetKeyframeCommand(anim, "position", before,
                                          animedits::snapshotTrack(anim, "position")));

        CHECK(stack.count() == base + 2, "two keyframe commands on the stack");
        CHECK(xKeys(anim, "position").count() == 2, "two keys on the track");
        CHECK(near(anim->getLength(), 2.0), "the derived length followed the last key");

        stack.undo();
        CHECK(xKeys(anim, "position").count() == 1, "undo removed the second key");
        CHECK(near(anim->getLength(), 0.0), "F16: ...and the LENGTH came back with it");
        stack.undo();
        CHECK(!anim->hasPropertyAnim(QStringLiteral("position")),
              "F16: undoing the FIRST key removes the track it created");

        stack.redo();
        stack.redo();
        const auto back = xKeys(anim, "position");
        CHECK(back.count() == 2 && near(back[0].second, 0) && near(back[1].second, 10),
              "F16: redo put both keys back, with their values");
    }

    // ---- overwrite-at-the-same-time undoes to the OLD value ----------------
    {
        node->setLocalPos(iris::Vec3(99, 0, 0));
        auto before = animedits::snapshotTrack(anim, QStringLiteral("position"));
        animedits::setKeyframe(anim, positionOf(node), 2.0);
        stack.push(new SetKeyframeCommand(anim, "position", before,
                                          animedits::snapshotTrack(anim, "position")));
        auto keys = xKeys(anim, "position");
        CHECK(keys.count() == 2 && near(keys[1].second, 99),
              "keying an existing time OVERWRITES rather than doubling");
        stack.undo();
        keys = xKeys(anim, "position");
        CHECK(keys.count() == 2 && near(keys[1].second, 10),
              "F16: undo restored the value that key held BEFORE the overwrite");
        stack.redo();
    }

    // ---- SetTangentsCommand ------------------------------------------------
    {
        auto *track = anim->getPropertyAnim(QStringLiteral("position"));
        auto *frame = track->getKeyFrames()[0].keyFrame;
        auto *key = frame->keys.last();
        const auto beforeShape = key->leftTangent;

        auto before = animedits::snapshotTrack(anim, QStringLiteral("position"));
        key->leftTangent = iris::TangentType::Constant;
        key->leftSlope = 0.25f;
        key->handleMode = iris::HandleMode::Broken;
        stack.push(new SetTangentsCommand(anim, "position", before,
                                          animedits::snapshotTrack(anim, "position")));

        stack.undo();
        auto *after = anim->getPropertyAnim(QStringLiteral("position"))
                          ->getKeyFrames()[0].keyFrame->keys.last();
        CHECK(after->leftTangent == beforeShape && near(after->leftSlope, 0.0)
              && after->handleMode == iris::HandleMode::Joined,
              "F16: undo restored the key's CURVE SHAPE, not just its value");
        stack.redo();
        after = anim->getPropertyAnim(QStringLiteral("position"))
                    ->getKeyFrames()[0].keyFrame->keys.last();
        CHECK(after->leftTangent == iris::TangentType::Constant && near(after->leftSlope, 0.25)
              && after->handleMode == iris::HandleMode::Broken,
              "F16: ...and redo put the shape back");
    }

    // ---- RemoveKeyframeCommand + RemovePropertyCommand ---------------------
    {
        auto before = animedits::snapshotTrack(anim, QStringLiteral("position"));
        const int removed = animedits::removeKeyframe(anim, QStringLiteral("position"), 0.0);
        CHECK(removed == 3, "removing a key takes it from all three channels");
        stack.push(new RemoveKeyframeCommand(anim, "position", before,
                                             animedits::snapshotTrack(anim, "position")));
        CHECK(xKeys(anim, "position").count() == 1, "one key left");
        stack.undo();
        CHECK(xKeys(anim, "position").count() == 2, "F16: undo put the removed key back");

        before = animedits::snapshotTrack(anim, QStringLiteral("position"));
        CHECK(animedits::removeTrack(anim, QStringLiteral("position")), "removeTrack");
        stack.push(new RemovePropertyCommand(anim, "position", before));
        CHECK(!anim->hasPropertyAnim(QStringLiteral("position")), "the whole track is gone");
        stack.undo();
        const auto restored = xKeys(anim, "position");
        CHECK(restored.count() == 2 && near(restored[1].second, 99),
              "F16: undo rebuilt the WHOLE track — keys, values and curve shapes");
        auto *shaped = anim->getPropertyAnim(QStringLiteral("position"))
                           ->getKeyFrames()[0].keyFrame->keys.last();
        CHECK(shaped->leftTangent == iris::TangentType::Constant,
              "F16: ...including the tangents on it");
    }

    // ---- RemoveAnimationCommand -------------------------------------------
    {
        auto doomed = iris::Animation::create("Doomed");
        node->addAnimation(doomed);
        node->setAnimation(doomed);
        CHECK(node->getAnimations().count() == 2, "two animations on the node");

        const bool wasActive = (node->getAnimation() == doomed);
        CHECK(animedits::removeAnimation(node, doomed), "removeAnimation");
        stack.push(new RemoveAnimationCommand(node, doomed, wasActive));
        CHECK(node->getAnimations().count() == 1, "one left");
        CHECK(node->getAnimation().isNull(), "...and the node has NO active animation");

        stack.undo();
        CHECK(node->getAnimations().count() == 2, "F16: undo put the animation back");
        CHECK(node->getAnimation() == doomed, "F16: ...and made it active again, as it was");

        stack.redo();
        CHECK(node->getAnimations().count() == 1, "F16: redo removes it again");
    }

    // ---- the command is safe after its node dies ---------------------------
    {
        QUndoStack local;
        auto temp = iris::SceneNode::create();
        auto clip = iris::Animation::create("Temp");
        temp->addAnimation(clip);
        temp->setAnimation(clip);
        animedits::removeAnimation(temp, clip);
        local.push(new RemoveAnimationCommand(temp, clip, true));
        temp.reset();
        local.undo();       // the node is gone; this must be a no-op, not a crash
        local.redo();
        CHECK(true, "F16: a command whose node has died undoes/redoes without dereferencing it");
    }

    if (failures == 0) std::printf("ALL OK\n");
    else std::printf("%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
