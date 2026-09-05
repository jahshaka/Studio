// commands.structural_undo — UNDO v1.5 (SPECS/SCENEGRAPH_SPEC.md §3, "v1.5 —
// undo redesign"; scene-graph audit F5; scripting audit F3).
//
// The contract this suite pins is one sentence: AN UNDO PUTS THE DOCUMENT BACK,
// including the two things that used to be silently lost.
//
//   * THE SIBLING INDEX. Sibling order is real semantics — the outliner shows
//     it and the serializer writes it — and every structural command except
//     delete used `addChild`, which APPENDS. Undoing a reparent therefore moved
//     the node to the bottom of its old parent's children; redoing it moved it
//     to the bottom of the new parent's. The user's scene was quietly reordered
//     by an operation whose whole promise is that nothing changed.
//
//   * THE SCENE_STATIC CLASSIFICATION. A transform write demotes a static
//     subtree by design (SCENEGRAPH_SPEC §6 rule 4) — and an undo of a move is
//     itself a transform write, so it demoted the subtree a second time and
//     nothing ever put it back. One nudge-and-undo dropped the ground, the
//     architecture and every imported prop out of the static memory manager for
//     the rest of the session, and — since the user override is what the
//     serializer persists — for every session after it.
//
// Framework-free (printf + a failure counter), offscreen, and DISPLAY-FREE: the
// document graph boots Ogre's NULL render system (tests/support/documentgraph.h).
//
// THE LINK STUBS (test_stubs.cpp) are the tree's idiom for this: the command
// TUs reference SceneEditService/SelectionService to raise UI refreshes, and
// pulling those in would drag the database, the io/ layer and Sql into a suite
// about pointer bookkeeping. The stubs also let the suite CONTROL the fragment
// pair, which is how the rebuild fallback below gets exercised at all.

#include <QGuiApplication>
#include <QUndoStack>
#include <QVector3D>
#include <cstdio>

#include "commands/addscenenodecommand.h"
#include "commands/deletescenenodecommand.h"
#include "commands/reparentscenenodecommand.h"
#include "commands/setnodepropertycommand.h"
#include "commands/staticstate.h"
#include "commands/structuralundo.h"
#include "commands/transformscenenodecommand.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/services.h"

#include "../support/documentgraph.h"
#include "test_stubs.h"

static int failures = 0;
static int checks = 0;
#define CHECK(cond, msg) do { ++checks; if (cond) printf("ok:   %s\n", msg); \
    else { printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

/// A scene with `n` named children under the root, in order.
iris::ScenePtr sceneWithChildren(int n, QList<iris::SceneNodePtr> &out)
{
    auto scene = iris::Scene::create();
    for (int i = 0; i < n; ++i) {
        auto node = iris::SceneNode::create();
        node->setName(QStringLiteral("child%1").arg(i));
        scene->getRootNode()->addChild(node, false);
        out.append(node);
    }
    return scene;
}

/// Runs a command through a stack, so it gets `services` stamped on it exactly
/// the way UndoService::push does in the app (the commands null-check it, and a
/// suite that skipped the stamping would test a different code path).
void push(QUndoStack &stack, StudioCommand *cmd, StudioServices *services)
{
    cmd->setServices(services);
    stack.push(cmd);
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    enginetest::DocumentGraph graph("structural-undo-ogre.log");
    if (!graph.require()) return 1;

    StudioServices services;      // every member null: the headless contract

    // ---------------------------------------------------------------------
    // 1. DELETE / UNDO restores the exact slot and the exact identity.
    // ---------------------------------------------------------------------
    {
        QList<iris::SceneNodePtr> kids;
        auto scene = sceneWithChildren(3, kids);
        auto middle = kids[1];
        const QString guid = middle->getGUID();
        const qint64 nodeId = middle->getNodeId();

        QUndoStack stack;
        push(stack, new DeleteSceneNodeCommand(scene->getRootNode(), middle), &services);
        CHECK(scene->getRootNode()->childCount() == 2, "delete: the node left the tree");

        stack.undo();
        CHECK(scene->getRootNode()->childCount() == 3, "delete/undo: the node came back");
        auto *back = scene->getRootNode()->childAt(1);
        CHECK(back != nullptr && back->getGUID() == guid,
              "delete/undo: it came back at ITS OWN sibling index, not appended");
        CHECK(back != nullptr && back->getNodeId() == nodeId,
              "delete/undo: the session node id survived (SceneMirror keys on it)");
        CHECK(middle->getScene() == scene,
              "delete/undo: the node is registered with the scene again");

        stack.redo();
        CHECK(scene->getRootNode()->childCount() == 2, "delete/undo/redo: gone again");
        stack.undo();
        CHECK(scene->getRootNode()->childAt(1) != nullptr
                  && scene->getRootNode()->childAt(1)->getGUID() == guid,
              "delete: a second undo lands in the same slot");
    }

    // ---------------------------------------------------------------------
    // 2. ADD at an index / UNDO / REDO — the duplicate-lands-beside-its-
    //    original case (SceneEditService::duplicateNode passes index+1).
    // ---------------------------------------------------------------------
    {
        QList<iris::SceneNodePtr> kids;
        auto scene = sceneWithChildren(3, kids);
        auto copy = iris::SceneNode::create();
        copy->setName(QStringLiteral("copy"));

        QUndoStack stack;
        push(stack, new AddSceneNodeCommand(scene->getRootNode(), copy, /*position=*/1),
             &services);
        CHECK(scene->getRootNode()->childCount() == 4, "add: the node joined");
        CHECK(copy->siblingIndex() == 1, "add: it landed at the index it was given");

        stack.undo();
        CHECK(scene->getRootNode()->childCount() == 3, "add/undo: it left again");
        stack.redo();
        CHECK(copy->siblingIndex() == 1, "add/undo/redo: back in the SAME slot, not appended");
    }

    // ---------------------------------------------------------------------
    // 3. REPARENT / UNDO — audit F5, the defect this program set out to close.
    // ---------------------------------------------------------------------
    {
        QList<iris::SceneNodePtr> kids;
        auto scene = sceneWithChildren(4, kids);
        auto mover = kids[1];
        auto newParent = kids[3];

        QUndoStack stack;
        push(stack, new ReparentSceneNodeCommand(mover, newParent), &services);
        CHECK(mover->getParent() == newParent, "reparent: moved");
        CHECK(scene->getRootNode()->childCount() == 3, "reparent: the root lost a child");

        stack.undo();
        CHECK(mover->getParent() == scene->getRootNode(), "reparent/undo: back under the root");
        CHECK(mover->siblingIndex() == 1,
              "reparent/undo: back at index 1 — NOT appended (audit F5)");

        stack.redo();
        CHECK(mover->getParent() == newParent, "reparent/redo: moved again");
        CHECK(mover->siblingIndex() == 0, "reparent/redo: at the slot the first redo used");
    }

    // ---------------------------------------------------------------------
    // 4. SCENE_STATIC survives a move and its undo (scripting audit F3).
    // ---------------------------------------------------------------------
    {
        QList<iris::SceneNodePtr> kids;
        auto scene = sceneWithChildren(1, kids);
        auto node = kids[0];
        node->setStaticHint(true);
        CHECK(node->staticHint() && node->isStaticInGraph(),
              "static: an eligible root-level node takes the hint");
        CHECK(node->staticOverride() == iris::StaticOverride::Static,
              "static: an explicit setStaticHint records the USER's decision");

        QUndoStack stack;
        push(stack, new TransformSceneNodeCommand(node, iris::Vec3(5, 0, 0),
                                                 node->getLocalRot(), node->getLocalScale()),
             &services);
        CHECK(!node->staticHint(),
              "static: moving it demotes the subtree (SCENEGRAPH_SPEC §6 rule 4)");

        stack.undo();
        CHECK(node->getLocalPos().x() == 0.0f, "transform/undo: the position came back");
        CHECK(node->staticHint() && node->isStaticInGraph(),
              "transform/undo: SO DID THE STATIC CLASSIFICATION (audit F3)");
        CHECK(node->staticOverride() == iris::StaticOverride::Static,
              "transform/undo: and the user override the serializer writes");

        stack.redo();
        CHECK(!node->staticHint(), "transform/redo: demoted again, as the move did");
    }

    // ---------------------------------------------------------------------
    // 5. The same, through the REFLECTED property route (node.setProperty).
    // ---------------------------------------------------------------------
    {
        QList<iris::SceneNodePtr> kids;
        auto scene = sceneWithChildren(1, kids);
        auto node = kids[0];
        node->setStaticHint(true);

        QUndoStack stack;
        push(stack, new SetNodePropertyCommand(node, QStringLiteral("position"),
                                               QVariant::fromValue(QVector3D(0, 0, 0)),
                                               QVariant::fromValue(QVector3D(0, 3, 0))),
             &services);
        CHECK(!node->staticHint(), "setProperty(position): demoted, like any other move");
        stack.undo();
        CHECK(node->staticHint(),
              "setProperty(position)/undo: the static classification is restored too");
    }

    // ---------------------------------------------------------------------
    // 6. A static subtree survives a reparent and its undo.
    // ---------------------------------------------------------------------
    {
        QList<iris::SceneNodePtr> kids;
        auto scene = sceneWithChildren(2, kids);
        auto branch = kids[0];
        auto leaf = iris::SceneNode::create();
        branch->addChild(leaf, false);
        branch->applyStaticDefaults();
        CHECK(branch->staticHint() && leaf->staticHint(),
              "static: the default policy marks a whole eligible branch");

        QUndoStack stack;
        push(stack, new ReparentSceneNodeCommand(branch, kids[1]), &services);
        stack.undo();
        CHECK(branch->siblingIndex() == 0, "reparent/undo: the branch is back at index 0");
        CHECK(branch->staticHint() && leaf->staticHint(),
              "reparent/undo: the whole branch is static again, root AND leaf");
    }

    // ---------------------------------------------------------------------
    // 7. THE REBUILD FALLBACK. When the live subtree can no longer be
    //    re-attached, `reinstate` rebuilds the captured fragment instead —
    //    the path that makes the snapshot more than bookkeeping.
    // ---------------------------------------------------------------------
    {
        QList<iris::SceneNodePtr> kids;
        auto scene = sceneWithChildren(2, kids);

        SceneFragment fragment;
        fragment.node.insert(QStringLiteral("guid"), QStringLiteral("captured-guid"));
        fragment.siblingIndex = 1;

        auto replacement = iris::SceneNode::create();
        replacement->setGUID(QStringLiteral("captured-guid"));
        teststubs::nextRebuild = replacement;

        // A live node whose guid does NOT match the snapshot: the command is
        // holding something that is no longer what it took out.
        auto stale = iris::SceneNode::create();
        stale->setGUID(QStringLiteral("some-other-guid"));
        CHECK(!structuralundo::liveIsUsable(stale, fragment),
              "reinstate: a live node whose identity drifted is refused");

        StudioServices withEdit;
        withEdit.sceneEdit = teststubs::sceneEditService();
        auto out = structuralundo::reinstate(&withEdit, scene->getRootNode(), stale,
                                             fragment, 1);
        CHECK(out == replacement, "reinstate: it rebuilt the fragment instead");
        CHECK(replacement->siblingIndex() == 1,
              "reinstate: the rebuild landed at the captured sibling index");

        // ...and the live path is preferred when the live node IS the one.
        auto good = iris::SceneNode::create();
        good->setGUID(QStringLiteral("captured-guid"));
        CHECK(structuralundo::liveIsUsable(good, fragment),
              "reinstate: a matching, detached live node is usable");
    }

    if (failures) printf("\n%d of %d checks FAILED\n", failures, checks);
    else printf("\nall %d checks passed\n", checks);
    return failures ? 1 : 0;
}
