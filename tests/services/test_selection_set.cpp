/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// services.selection_set — the selection SET's contracts, on REAL document
// nodes (EDITOR_MULTISELECT_SPEC §2.1, gate table §5).
//
// services.core already covers the single-selection behaviour with null
// pointers; the set's rules cannot be tested that way, because every one of
// them is about document ORDER: the tail is kept in pre-order, and removing the
// primary promotes THE TOPMOST REMAINING member (D2) — not the most recently
// added. So this suite builds a small hierarchy on the headless document graph
// and drives the service against it.
//
// Signal counts are asserted too, and they are the §3.3 rule: a Ctrl+click
// storm that never changes the primary must not re-emit selectionChanged (the
// properties panel rebuilds on that signal), while selectionSetChanged fires on
// every set change.

#include <QGuiApplication>

#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/selectionservice.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

namespace {

QStringList names(const QList<iris::SceneNodePtr> &nodes)
{
    QStringList out;
    for (const auto &n : nodes) out.append(n ? n->getName() : QStringLiteral("<null>"));
    return out;
}

void run()
{
    // root
    //  +- a
    //  |   +- a1
    //  +- b
    //  +- c
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();
    auto a  = iris::SceneNode::create(); a->setName("a");
    auto a1 = iris::SceneNode::create(); a1->setName("a1");
    auto b  = iris::SceneNode::create(); b->setName("b");
    auto c  = iris::SceneNode::create(); c->setName("c");
    root->addChild(a);
    a->addChild(a1);
    root->addChild(b);
    root->addChild(c);

    SelectionService sel;
    int primaryEmissions = 0, setEmissions = 0;
    QObject::connect(&sel, &SelectionService::selectionChanged,
                     [&](iris::SceneNodePtr) { ++primaryEmissions; });
    QObject::connect(&sel, &SelectionService::selectionSetChanged,
                     [&](const QList<iris::SceneNodePtr> &) { ++setEmissions; });

    // ---- document order -------------------------------------------------
    const auto ordered = SelectionService::sortDocumentOrder({ c, a1, b, a });
    CHECK(names(ordered) == QStringList({ "a", "a1", "b", "c" }),
          "selection_set: sortDocumentOrder is pre-order (ancestor before descendant)");
    CHECK(SelectionService::topmost({ c, b })->getName() == QStringLiteral("b"),
          "selection_set: topmost is the first in document order");

    // ---- replace --------------------------------------------------------
    sel.select(c);
    CHECK(sel.selected() == c && sel.count() == 1, "selection_set: select(node) replaces with one");
    sel.select(QList<iris::SceneNodePtr>({ c, a, b }));
    CHECK(sel.selected() == c, "selection_set: select(list) makes the FIRST id the primary");
    CHECK(names(sel.selectedSet()) == QStringList({ "c", "a", "b" }),
          "selection_set: the tail is stored in document pre-order");

    // ---- add ------------------------------------------------------------
    const int before = primaryEmissions;
    CHECK(sel.add(a1), "selection_set: add() reports a change");
    CHECK(sel.selected() == a1, "selection_set: the last added node becomes the primary");
    CHECK(names(sel.selectedSet()) == QStringList({ "a1", "a", "b", "c" }),
          "selection_set: add keeps the tail in document order");
    CHECK(primaryEmissions == before + 1, "selection_set: add emits selectionChanged (primary moved)");
    const int afterAdd = primaryEmissions;
    CHECK(!sel.add(a1), "selection_set: re-adding the primary changes nothing");
    CHECK(primaryEmissions == afterAdd,
          "selection_set: re-adding the primary does NOT re-emit selectionChanged (the panel-rebuild rule)");

    // ---- toggle + D2 promotion -----------------------------------------
    CHECK(!sel.toggle(a1), "selection_set: toggling a member removes it");
    CHECK(sel.selected() == a,
          "selection_set: removing the primary promotes the TOPMOST remaining member (D2)");
    CHECK(sel.toggle(a1), "selection_set: toggling a non-member adds it");
    CHECK(sel.selected() == a1, "selection_set: the toggled-in node becomes the primary");

    // a secondary removal must NOT touch the primary signal
    const int primaryBefore = primaryEmissions, setBefore = setEmissions;
    CHECK(sel.remove(c), "selection_set: remove() drops a member");
    CHECK(primaryEmissions == primaryBefore,
          "selection_set: removing a SECONDARY leaves selectionChanged quiet");
    CHECK(setEmissions == setBefore + 1, "selection_set: every set change emits selectionSetChanged");
    CHECK(!sel.remove(c), "selection_set: removing a non-member is a no-op");
    CHECK(!sel.isSelected(c) && sel.isSelected(a), "selection_set: isSelected answers membership");

    // ---- clear ----------------------------------------------------------
    sel.clear();
    CHECK(sel.count() == 0 && !sel.selected(), "selection_set: clear empties the set");

    // ---- the single-selection contract still holds ----------------------
    const int reEmitBefore = primaryEmissions;
    sel.select(b);
    sel.select(b);
    CHECK(primaryEmissions == reEmitBefore + 2,
          "selection_set: a REPLACE re-emits even for the same node (the panel-refresh contract)");

    // ---- re-entrancy -----------------------------------------------------
    SelectionService guarded;
    int outer = 0;
    QObject::connect(&guarded, &SelectionService::selectionChanged,
                     [&guarded, &outer, &a](iris::SceneNodePtr) {
        ++outer;
        guarded.add(a);            // the viewport echo, in its set-shaped form
    });
    guarded.select(b);
    CHECK(outer == 1 && guarded.count() == 1,
          "selection_set: the re-entrancy guard swallows echoes from the fan-out");

    scene.reset();
}

} // namespace

int main(int argc, char *argv[])
{
    enginetest::DocumentGraph graph("selection-set-ogre.log");
    QGuiApplication app(argc, argv);
    run();
    std::printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
