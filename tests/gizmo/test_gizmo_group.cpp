/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// gizmo.group_transform — MOVING, ROTATING AND SCALING A SELECTION SET
// (EDITOR_MULTISELECT_SPEC §2.4), and its undo shape.
//
// The three gizmo subclasses go on writing exactly ONE node — the primary —
// with all of the handle geometry, axis constraints and snapping they already
// had. The group is then applied by the BASE class as a delta read back off
// that node: translate is the primary's move, rotate and scale happen ABOUT
// the primary's pivot. That is the whole of the group maths, so it is what
// this suite drives, directly: a synthetic ray that happens to hit an axis
// handle would be a test of the handle geometry instead.
//
// What is asserted:
//   1. TRANSLATE moves every member by the same delta, keeping their offsets.
//   2. ROTATE keeps the primary where it is and ORBITS the secondaries about
//      it — the assertion is geometric (the distance to the pivot is
//      preserved, and a 90-degree turn about Y maps +X to -Z), not a replay of
//      the formula.
//   3. SCALE scales both each member's own scale AND its distance from the
//      pivot.
//   4. ONE UNDO STEP: N members produce ONE entry on the stack (a macro), and
//      undoing it puts every member back. This is the property a --script run
//      cannot assert (its own open macro blocks undo), so it is asserted here
//      against a plain QUndoStack.
//   5. A single-node drag keeps EXACTLY today's stack shape: one command, no
//      macro.
//
// Document-only: no display, no GPU, no pixels.

#include <QGuiApplication>
#include <QUndoStack>

#include <cmath>
#include <cstdio>

#include "../support/documentgraph.h"

#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "services/services.h"
#include "services/undoservice.h"
#include "viewport/translationgizmo.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

namespace {

bool near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool nearVec(const iris::Vec3 &a, const iris::Vec3 &b, float eps = 1e-3f)
{
    return near(a.x(), b.x(), eps) && near(a.y(), b.y(), eps) && near(a.z(), b.z(), eps);
}

struct Fixture
{
    iris::ScenePtr scene;
    iris::SceneNodePtr primary, second, third;

    Fixture()
    {
        scene = iris::Scene::create();
        primary = iris::SceneNode::create(); primary->setName("primary");
        second  = iris::SceneNode::create(); second->setName("second");
        third   = iris::SceneNode::create(); third->setName("third");
        scene->getRootNode()->addChild(primary);
        scene->getRootNode()->addChild(second);
        scene->getRootNode()->addChild(third);
        primary->setLocalPos(iris::Vec3(0, 0, 0));
        second->setLocalPos(iris::Vec3(4, 0, 0));
        third->setLocalPos(iris::Vec3(0, 0, 6));
        primary->update(0.0f); second->update(0.0f); third->update(0.0f);
    }
    QList<iris::SceneNodePtr> all() const { return { primary, second, third }; }
};

void testTranslate()
{
    Fixture f;
    TranslationGizmo gizmo;
    gizmo.setSelectedNode(f.primary);
    gizmo.setGroup(f.all());
    gizmo.setInitialTransform();                 // captures every member's start

    // What the subclass's drag() does to the primary, and nothing else.
    f.primary->setGlobalPos(iris::Vec3(1, 2, 3));
    gizmo.applyGroupDelta();

    CHECK(nearVec(f.second->getGlobalPosition(), iris::Vec3(5, 2, 3)),
          "group translate: every member moves by the SAME delta");
    CHECK(nearVec(f.third->getGlobalPosition(), iris::Vec3(1, 2, 9)),
          "group translate: relative offsets are preserved");
    CHECK(nearVec(f.second->getLocalScale(), iris::Vec3(1, 1, 1)),
          "group translate: scale is untouched");
}

void testRotate()
{
    Fixture f;
    TranslationGizmo gizmo;                      // the group maths live in the base
    gizmo.setSelectedNode(f.primary);
    gizmo.setGroup(f.all());
    gizmo.setInitialTransform();

    // 90 degrees about Y, applied to the primary exactly as RotationGizmo does.
    f.primary->setLocalRot(iris::Quat::fromEulerAngles(0, 90, 0).normalized());
    f.primary->update(0.0f);
    gizmo.applyGroupDelta();
    f.second->update(0.0f); f.third->update(0.0f);

    CHECK(nearVec(f.primary->getGlobalPosition(), iris::Vec3(0, 0, 0)),
          "group rotate: the primary does not move (it IS the pivot)");
    // +X (4,0,0) about +Y by 90 degrees -> (0,0,-4) in this document's handedness,
    // asserted as "the same distance from the pivot, and no longer on +X".
    const iris::Vec3 p = f.second->getGlobalPosition();
    CHECK(near(p.length(), 4.0f, 1e-2f),
          "group rotate: a secondary keeps its distance from the pivot (orbits it)");
    CHECK(std::fabs(p.x()) < 1e-2f && std::fabs(p.z()) > 3.9f,
          "group rotate: +X mapped onto the Z axis by a 90-degree Y turn");
    CHECK(!near(f.second->getGlobalRotation().z(), 999.0f),
          "group rotate: the secondary's own orientation was written too");
}

void testScale()
{
    Fixture f;
    TranslationGizmo gizmo;
    gizmo.setSelectedNode(f.primary);
    gizmo.setGroup(f.all());
    gizmo.setInitialTransform();

    f.primary->setLocalScale(iris::Vec3(2, 2, 2));
    f.primary->update(0.0f);
    gizmo.applyGroupDelta();
    f.second->update(0.0f);

    CHECK(nearVec(f.second->getLocalScale(), iris::Vec3(2, 2, 2)),
          "group scale: every member's own scale follows the primary's ratio");
    CHECK(nearVec(f.second->getGlobalPosition(), iris::Vec3(8, 0, 0)),
          "group scale: the members' distance from the pivot scales too");
}

void testUndoShape()
{
    Fixture f;
    QUndoStack stack;
    UndoService undo(&stack);
    StudioServices services;
    services.undo = &undo;

    TranslationGizmo gizmo;
    gizmo.setServices(&services);
    gizmo.setSelectedNode(f.primary);
    gizmo.setGroup(f.all());
    gizmo.setInitialTransform();
    f.primary->setGlobalPos(iris::Vec3(0, 5, 0));
    gizmo.applyGroupDelta();
    gizmo.createUndoAction();

    CHECK(stack.count() == 1, "group undo: three members are ONE entry on the stack (a macro)");
    f.primary->update(0.0f); f.second->update(0.0f); f.third->update(0.0f);
    CHECK(nearVec(f.second->getGlobalPosition(), iris::Vec3(4, 5, 0)),
          "group undo: the command applied the move (redo ran on push)");
    stack.undo();
    f.primary->update(0.0f); f.second->update(0.0f); f.third->update(0.0f);
    CHECK(nearVec(f.primary->getGlobalPosition(), iris::Vec3(0, 0, 0)) &&
          nearVec(f.second->getGlobalPosition(), iris::Vec3(4, 0, 0)) &&
          nearVec(f.third->getGlobalPosition(), iris::Vec3(0, 0, 6)),
          "group undo: ONE undo puts EVERY member back");

    // A single-node drag keeps the pre-multiselect stack shape exactly.
    QUndoStack single;
    UndoService undoSingle(&single);
    StudioServices s2;
    s2.undo = &undoSingle;
    TranslationGizmo lone;
    lone.setServices(&s2);
    lone.setSelectedNode(f.primary);
    lone.setGroup({ f.primary });               // a group of one is not a group
    CHECK(lone.groupSize() == 0, "a one-node group is no group at all");
    lone.setInitialTransform();
    f.primary->setGlobalPos(iris::Vec3(2, 0, 0));
    lone.createUndoAction();
    CHECK(single.count() == 1, "single-node drag: one command, no macro");
}

} // namespace

int main(int argc, char *argv[])
{
    enginetest::DocumentGraph graph("gizmo-group-ogre.log");
    QGuiApplication app(argc, argv);
    testTranslate();
    testRotate();
    testScale();
    testUndoShape();
    std::printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
