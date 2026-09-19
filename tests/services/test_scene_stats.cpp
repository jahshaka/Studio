/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// services.scene_stats — the three leaves behind the F3 readout and
// `app.renderStats().sceneTriangles` (owner review 2026-09-18, answer Q3).
//
//   scenestats::sceneGeometry   WHAT IS IN THE SCENE, counted on the document
//   framewindows::EventWindow   a count over a ROLLING window — the shape that
//                               replaced two lifetime totals
//   statsrows::compose          the readout's WORDING, which is the thing the
//                               owner's complaint was actually about
//
// TIME IS AN ARGUMENT to the window, and that is why this suite can prove a
// minute is forgotten without waiting a minute: "a wall-clock settle measures
// nothing" in this tree, so the stamps here are numbers the test chooses.
//
// No engine beyond the headless document graph (the nodes ARE Ogre scene
// nodes since the scene-graph swap), no display, no pixels.

#include <QGuiApplication>

#include <cstdio>

#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "services/scenestats.h"
#include "viewport/framewindows.h"
#include "viewport/statsrows.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("PASS %s\n", name); } \
    else { std::printf("FAIL %s\n", name); ++failures; } \
} while (0)

namespace {

/// A mesh with a chosen face count. Document meshes get theirs from the
/// importer or the .jmb bake; a test states it.
iris::MeshPtr mesh(int faces, iris::PrimitiveMode mode = iris::PrimitiveMode::Triangles)
{
    auto m = iris::Mesh::create();
    m->numFaces = faces;
    m->setPrimitiveMode(mode);
    return m;
}

iris::MeshNodePtr meshNode(const char *name, int faces,
                           iris::PrimitiveMode mode = iris::PrimitiveMode::Triangles)
{
    auto node = iris::MeshNode::create();
    node->setName(name);
    node->setMesh(mesh(faces, mode));
    return node;
}

void runSceneCensus()
{
    // root
    //  +- ground (100 tris)
    //  +- group            <- an empty; hiding it hides its subtree
    //  |   +- cube  (12)
    //  |   +- lines (5, a LINE mesh)
    //  +- plain            <- a SceneNode with no mesh at all
    auto scene = iris::Scene::create();
    auto root = scene->getRootNode();
    auto ground = meshNode("ground", 100);
    auto group = iris::SceneNode::create(); group->setName("group");
    auto cube = meshNode("cube", 12);
    auto lines = meshNode("lines", 5, iris::PrimitiveMode::Lines);
    auto plain = iris::SceneNode::create(); plain->setName("plain");
    root->addChild(ground);
    root->addChild(group);
    group->addChild(cube);
    group->addChild(lines);
    root->addChild(plain);

    {
        const auto g = scenestats::sceneGeometry(scene);
        CHECK(g.triangles == 112,
              "scene_stats: the visible mesh nodes' authored triangles, counted once each");
        CHECK(g.meshNodes == 2,
              "scene_stats: a LINE mesh contributes no triangles and is not counted as a mesh "
              "(Ogre's face metric agrees: a line primitive is zero faces)");
        CHECK(g.hiddenTriangles == 0, "scene_stats: nothing hidden yet");
    }

    // A SECOND NODE ON THE SAME MESH IS A SECOND OBJECT. The load cache hands
    // every Cube in a world the same MeshPtr, and the user sees two cubes.
    auto cube2 = iris::MeshNode::create();
    cube2->setName("cube2");
    cube2->setMesh(cube->getMesh());
    root->addChild(cube2);
    CHECK(scenestats::sceneGeometry(scene).triangles == 124,
          "scene_stats: two nodes sharing one mesh count twice");

    // HIDING A NODE takes its triangles out of the scene count and puts them in
    // the hidden column — an empty world and a world with everything hidden are
    // different facts.
    cube2->setVisible(false);
    {
        const auto g = scenestats::sceneGeometry(scene);
        CHECK(g.triangles == 112, "scene_stats: a hidden node leaves the visible count");
        CHECK(g.hiddenTriangles == 12 && g.hiddenMeshNodes == 1,
              "scene_stats: …and lands in the hidden column");
    }

    // A HIDDEN ANCESTOR hides its subtree without touching the child's own flag
    // (SceneNode::isVisibleInScene is the one rule every consumer applies).
    group->setVisible(false);
    {
        const auto g = scenestats::sceneGeometry(scene);
        CHECK(g.triangles == 100, "scene_stats: a hidden parent removes its children's triangles");
        CHECK(cube->isVisible(), "scene_stats: …while the child's own flag is untouched");
        CHECK(g.hiddenTriangles == 24, "scene_stats: the subtree is charged as hidden");
    }
    group->setVisible(true);
    cube2->setVisible(true);
    CHECK(scenestats::sceneGeometry(scene).triangles == 124,
          "scene_stats: showing the parent brings the subtree back");

    // A mesh node with NO mesh (a node whose asset failed to load) counts
    // nothing and crashes nothing.
    auto empty = iris::MeshNode::create();
    empty->setName("no mesh");
    root->addChild(empty);
    CHECK(scenestats::sceneGeometry(scene).triangles == 124,
          "scene_stats: a mesh node with no mesh contributes nothing");

    CHECK(scenestats::sceneGeometry(iris::ScenePtr()).triangles == 0,
          "scene_stats: no scene is an honest zero, not a crash");
}

void runWindow()
{
    // THE ROLLING MINUTE, in simulated time. Sixty seconds are handed to the
    // window as numbers; nothing sleeps.
    framewindows::EventWindow slow(framewindows::kSlowFrameWindowMs);
    slow.add(1000.0);           // a hitch one second in
    slow.add(30000.0);          // another at thirty
    CHECK(slow.count(31000.0) == 2, "framewindows: both hitches are inside the minute");
    CHECK(slow.count(61500.0) == 1,
          "framewindows: the first hitch is FORGOTTEN once it is a minute old");
    CHECK(slow.count(91000.0) == 0, "framewindows: and so is the second");
    // …and the window never reports a frame it has already forgotten, however
    // long the app then idles.
    CHECK(slow.count(1.0e9) == 0, "framewindows: an idle hour reports zero, not a stale total");

    // THE DRAWN RATE. Sixty frames spread over one second read 60 fps; the same
    // sixty frames a second later read 0 — a live readout must be able to say
    // "nothing is being drawn".
    // (The window is half-open — (now - windowMs, now] — so a frame stamped
    // exactly one window ago is already out. The stamps below sit strictly
    // inside it.)
    framewindows::EventWindow drawn(framewindows::kDrawnRateWindowMs);
    for (int i = 1; i <= 60; ++i) drawn.add(1000.0 + i * (1000.0 / 60.0));
    CHECK(drawn.count(2000.0) == 60, "framewindows: sixty frames in the last second");
    CHECK(qFuzzyCompare(drawn.perSecond(2000.0), 60.0), "framewindows: …reported as 60 per second");
    CHECK(drawn.count(3500.0) == 0, "framewindows: a loop that stopped drawing reads zero");

    // The cap: a pathological producer cannot grow the window without bound.
    framewindows::EventWindow big(1.0e12);
    for (int i = 0; i < framewindows::EventWindow::kMaxEvents + 500; ++i) big.add(double(i));
    CHECK(big.held() == framewindows::EventWindow::kMaxEvents,
          "framewindows: the hard cap holds, oldest first out");
}

void runRows()
{
    statsrows::Input in;
    in.drawing = true;
    in.fpsDrawn = 62.4;
    in.workMs = 4.15;
    in.sceneTriangles = 2178;
    in.submittedTriangles = 4611;
    in.draws = 31;
    in.slowFramesLastMinute = 2;
    const QStringList rows = statsrows::compose(in);

    CHECK(rows.size() == 4, "statsrows: four rows");
    // THE FOUR THINGS THE OWNER'S ANSWER ASKED FOR, each by its own words.
    CHECK(rows.at(0).startsWith(QStringLiteral("drawing")),
          "statsrows: the state comes first and says 'drawing'");
    CHECK(rows.at(0).contains(QStringLiteral("62 fps drawn")),
          "statsrows: frames ACTUALLY DRAWN per second, labelled 'drawn'");
    CHECK(rows.at(1).contains(QStringLiteral("4.2 ms/frame")),
          "statsrows: what a frame's work costs");
    CHECK(rows.at(2) == QStringLiteral("2,178 scene tris   4,611 submitted"),
          "statsrows: BOTH triangle numbers, both labelled — the row the review was about");
    CHECK(rows.at(3) == QStringLiteral("31 draws   2 slow frames (last min)"),
          "statsrows: draws, and the hitch count with its WINDOW named");
    for (const QString &row : rows)
        CHECK(!row.contains(QStringLiteral("skipped")),
              "statsrows: the word 'skipped' is gone from the readout");

    in.drawing = false;
    in.fpsDrawn = 0.0;
    const QStringList idle = statsrows::compose(in);
    CHECK(idle.at(0).startsWith(QStringLiteral("idle")),
          "statsrows: a loop with nothing to draw says 'idle', it does not count up a total");

    // A million-triangle scene reads as a million, grouped, in every locale.
    in.sceneTriangles = 1234567;
    CHECK(statsrows::compose(in).at(2).startsWith(QStringLiteral("1,234,567 scene tris")),
          "statsrows: counts are grouped, and in the C locale on every machine");
}

}   // namespace

int main(int argc, char **argv)
{
    // FIRST, so it outlives every node (documentgraph.h's lifetime rule).
    enginetest::DocumentGraph graph("scene-stats-ogre.log");
    QGuiApplication app(argc, argv);

    runSceneCensus();
    runWindow();
    runRows();

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
