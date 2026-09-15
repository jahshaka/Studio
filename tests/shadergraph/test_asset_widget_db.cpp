/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// shadergraph.asset_widget_db — THE MATERIALS PAGE'S SHADER LIST KEEPS THE
// HANDLE IT IS GIVEN (lane DBPTR-1).
//
// ShaderAssetWidget::setUpDatabase used to store the handle ONLY when a scene
// happened to be open at that moment:
//
//     if (sceneOpenProbe && sceneOpenProbe()) { this->db = db; ... }
//
// and its one caller is EffectsPage's CONSTRUCTOR — MaterialsModule builds the
// page and installs the scene-open probe afterwards, so the probe was always
// empty there and `db` (declared without an initialiser) was NEVER assigned at
// all. Every switch into the Materials space calls refreshShaderGraph() ->
// refresh() -> updateAssetView() -> `db->fetchChildAssets(...)`, i.e. a call
// through a wild pointer, for the life of the process. It survived because
// that query touches no member of Database — the accident that hid
// AssetPanel::handle until CLOSE-2 put a member read in that funnel.
//
// Two claims, in the order they broke:
//   1. the handle is stored whatever the scene-open probe says;
//   2. a refresh with NO library lists nothing and shows the "no scene" page
//      instead of dereferencing — the truthful empty state.
//
// Offscreen QPA, no display, no document: widgets and a library handle.

#include <QApplication>
#include <QListWidget>
#include <QStackedWidget>

#include <cstdio>

#include "data/database/database.h"
#include "data/project.h"
#include "modules/materials/widgets/shaderassetwidget.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// Reaches the private handle the way the widget's own code does — through the
/// one behaviour that depends on it. `fetchChildAssets` is stubbed to record
/// the pointer it was called through (test_asset_widget_db_stubs.cpp).
extern Database *gLastFetchChildAssetsHandle;
extern int gFetchChildAssetsCalls;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    Project project;
    project.setProjectGuid(QStringLiteral("project-under-test"));
    Database db;

    // ---- 1. NO SCENE OPEN AT THE MOMENT THE HANDLE ARRIVES ------------------
    {
        ShaderAssetWidget widget;
        widget.project = &project;
        // exactly the state EffectsPage's constructor is in: no probe yet
        widget.sceneOpenProbe = std::function<bool()>();

        gFetchChildAssetsCalls = 0;
        gLastFetchChildAssetsHandle = nullptr;

        widget.setUpDatabase(&db);
        CHECK(gFetchChildAssetsCalls == 0,
              "no scene open: taking the handle lists nothing (the page is not "
              "showing a project)");

        // ...and the scene opens later, which is when the page refreshes.
        bool sceneOpen = true;
        widget.sceneOpenProbe = [&sceneOpen]() { return sceneOpen; };
        widget.refresh();
        CHECK(gFetchChildAssetsCalls == 1,
              "the handle was KEPT: the later refresh queries the library");
        CHECK(gLastFetchChildAssetsHandle == &db,
              "...through the very handle setUpDatabase was given");
    }

    // ---- 2. A REFRESH WITH NO LIBRARY AT ALL --------------------------------
    {
        ShaderAssetWidget widget;
        widget.project = &project;
        bool sceneOpen = true;
        widget.sceneOpenProbe = [&sceneOpen]() { return sceneOpen; };

        gFetchChildAssetsCalls = 0;
        widget.refresh();               // never given a library
        CHECK(gFetchChildAssetsCalls == 0,
              "no library: the refresh queries nothing");
        CHECK(widget.assetViewWidget->count() == 0,
              "no library: ...and the list is empty, which is the truth");
    }

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
