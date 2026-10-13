/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// mirror.attach_budget — A FRAME BUILDS ITS SHARE OF NEW MESHES, NEVER ALL OF
// THEM (BISTRO-ENGINE-1 stage 5, finding 9).
//
// The owner's Bistro open blocked the UI thread 2.2 s in SceneMirror::toMeshData
// on one frame, and a placement 2.4 s in OgreScene::buildMeshV2 via
// SceneMirror::meshFor: the mirror built every new mesh of a world in the sync
// that first saw it. SceneMirror::setAttachBudget gives ONE sync a share — at
// most M new engine meshes and V of their vertices (the first always) — and a
// node left without its mesh is brought back by the change list on the next
// sync. The editor's DRIVER ticks ask for it; every other sync is unbounded.
//
// COUNTS: the deferrals (attachesOwed), the attaches (meshAttachCount) and the
// syncs it takes, against the closed forms ceil(N / M) and the vertex cap; the
// partial world is the document's (the full walk after the last sync pushes
// nothing); a mesh already built is never deferred; a sync nobody budgeted
// builds everything.

#include <QGuiApplication>
#include <cstdio>
#include <memory>
#include <vector>

#include "../support/testmesh.h"
#include "irisgl/document/assets/mesh.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/mirror/scenemirror.h"
#include "jahshaka/engine/Engine.h"
#include "../support/enginetesthelpers.h"

using namespace jahshaka::engine;
static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

struct World {
    Scene *target = nullptr;
    iris::ScenePtr doc;
    std::unique_ptr<SceneMirror> mirror;
    std::vector<iris::SceneNodePtr> keep;
};

/// `distinct` nodes, each with its OWN mesh (a fresh parse), and `shared` more
/// that all wear the first one's.
void build(World &w, Engine *engine, const char *name, int distinct, int shared)
{
    w.target = engine->createScene(name);
    w.doc = iris::Scene::create();
    iris::MeshPtr first;
    for (int i = 0; i < distinct + shared; ++i) {
        const iris::MeshPtr mesh = (i < distinct || !first)
                                       ? testmesh::load(QStringLiteral(":assets/models/cube.obj"))
                                       : first;
        if (!first) first = mesh;
        auto node = iris::MeshNode::create();
        node->setName(QStringLiteral("part %1").arg(i));
        node->setMesh(mesh);
        node->setMaterial(iris::PbrMaterial::create());
        node->setLocalPos(iris::Vec3(float(i % 10) * 2.0f, 0.0f, float(i / 10) * 2.0f));
        w.doc->getRootNode()->addChild(node);
        w.keep.push_back(node);
    }
    w.mirror.reset(new SceneMirror(w.target));
    w.mirror->setVerifierBudget(0);
    w.mirror->setSource(w.doc);
}

}   // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const auto cfg = enginetest::testEngineConfig("test_mirror_attach_budget-ogre.log");
    std::string err;
    auto engine = Engine::create(cfg, err);
    CHECK(engine != nullptr, "engine created");
    if (!engine) { std::printf("    %s\n", err.c_str()); return 1; }
    View *view = engine->createOffscreenView("budget", 64, 64, Colour(0, 0, 0));
    if (!view) { std::printf("FAIL: no offscreen view\n"); return 1; }

    // ---- THE MESH CAP: 40 distinct meshes at 8 a sync = ceil(40/8) = 5 syncs.
    {
        World w;
        build(w, engine.get(), "budget-meshes", 40, 0);
        view->setScene(w.target);
        int syncs = 0;
        quint64 owedAfterFirst = 0;
        do {
            w.mirror->setAttachBudget(8, std::size_t(1) << 30);
            w.mirror->sync();
            if (++syncs == 1) owedAfterFirst = w.mirror->attachesOwed();
        } while (w.mirror->attachesOwed() > 0 && syncs < 100);
        std::printf("info: 40 meshes at 8 a sync: %d syncs, %llu owed after the first, %llu attaches\n",
                    syncs, (unsigned long long)owedAfterFirst,
                    (unsigned long long)w.mirror->meshAttachCount());
        CHECK(owedAfterFirst == 32, "the first budgeted sync attaches 8 and leaves 32 owed");
        CHECK(syncs == 5, "the world is in after ceil(40 / 8) = 5 syncs");
        CHECK(w.mirror->meshAttachCount() == 40, "...with every node attached exactly once");
        CHECK(w.mirror->attachesDeferredTotal() == 32 + 24 + 16 + 8,
              "...and each deferral is one node waiting one sync (32+24+16+8)");
        CHECK(w.mirror->verifyAgainstFullWalk() == 0,
              "the streamed world IS the document: the full walk after it pushes nothing");
        w.mirror->sync();
        CHECK(w.mirror->attachesOwed() == 0 && w.mirror->visitedCount() == 0,
              "a still sync after it owes nothing and visits nothing");
        view->setScene(nullptr);
        engine->destroyScene(w.target);
    }

    // ---- THE VERTEX CAP: room for two cubes' vertices = 2 a sync; a cap below
    //      one mesh still builds the first (a huge mesh is never starved).
    {
        World w;
        build(w, engine.get(), "budget-vertices", 6, 0);
        view->setScene(w.target);
        w.mirror->setAttachBudget(100, 1);
        w.mirror->sync();
        const std::size_t cubeVerts = w.mirror->lastSyncBuiltVertices();
        CHECK(cubeVerts > 0 && w.mirror->meshAttachCount() == 1 && w.mirror->attachesOwed() == 5,
              "a cap below one mesh still builds the first of the sync (1 of 6)");
        w.mirror->setAttachBudget(100, 2 * cubeVerts);
        w.mirror->sync();
        CHECK(w.mirror->meshAttachCount() == 3 && w.mirror->attachesOwed() == 3,
              "a vertex cap of two cubes builds two more (3 of 6)");
        // ...and a sync nobody budgeted takes the rest.
        w.mirror->sync();
        CHECK(w.mirror->meshAttachCount() == 6 && w.mirror->attachesOwed() == 0,
              "the next UNBUDGETED sync builds everything left (the budget is one shot)");
        view->setScene(nullptr);
        engine->destroyScene(w.target);
    }

    // ---- A BUILT MESH IS NEVER DEFERRED: 1 distinct + 30 sharing it, at 1 a
    //      sync, are in after ONE sync (the 30 wear the mesh the first built).
    {
        World w;
        build(w, engine.get(), "budget-shared", 1, 30);
        view->setScene(w.target);
        w.mirror->setAttachBudget(1, std::size_t(1) << 30);
        w.mirror->sync();
        CHECK(w.mirror->attachesOwed() == 0 && w.mirror->meshAttachCount() == 31,
              "31 nodes on one mesh attach in one sync at a budget of one mesh");
        view->setScene(nullptr);
        engine->destroyScene(w.target);
    }

    // ---- UNBUDGETED: the contract every other caller keeps.
    {
        World w;
        build(w, engine.get(), "budget-none", 40, 0);
        view->setScene(w.target);
        w.mirror->sync();
        CHECK(w.mirror->attachesOwed() == 0 && w.mirror->meshAttachCount() == 40,
              "a sync nobody budgeted builds all 40 (scripts, screenshots, thumbnails)");
        view->setScene(nullptr);
        engine->destroyScene(w.target);
    }

    std::printf("%s (%d failure(s))\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
