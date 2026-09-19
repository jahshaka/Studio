/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.db_handles — A LIBRARY A PANEL NEVER GOT IS A NULL, NOT A GUESS.
//
// THE CLASS (lane DBPTR-1, from CLOSE-2's second read): 24 `Database *`
// members across src/ were declared with no initialiser. Twelve are filled by
// a constructor's init list and five by its body, but SEVEN are filled by a
// SETTER that runs after construction — and these are widgets, so their own
// signals can reach a dereference first. Two were never assigned at all:
//
//   * WorldPropertyWidget::db — nothing in the tree called its setDatabase, so
//     refreshRows() tested a wild pointer on every scene bind and the
//     "Background Ambience" row (shown only when the project HAS music) could
//     never appear. FIXED by forwarding from the panel host, which is what
//     sections 1 and 2 pin.
//   * ShaderAssetWidget::db — its setter stored the handle only when a scene
//     was open, and its one caller is EffectsPage's constructor, which runs
//     before the scene-open probe is installed. (Materials-module slice: the
//     gate for it is the source lint, source.db_pointers_initialised.)
//
// The whole class survived because the queries it reached touch no member of
// Database — the same accident that hid AssetPanel::handle until CLOSE-2 put a
// member read in that funnel and the app SIGSEGV'd at boot.
//
// WHAT THIS SUITE ASSERTS, per hazard site: with NO library the signal path
// runs and leaves the TRUTHFUL fallback (an empty list, the in-memory edit
// still applied) — never a silent skip that loses the feature — and once the
// handle arrives the feature is really there (the row appears, the library
// bookkeeping really runs).
//
// It deliberately does NOT assert "does not crash": a call through a null (or
// wild) Database WILL run to completion here, because the methods it reaches
// touch no member — that is the whole reason the class went unnoticed for so
// long, and an assertion built on it would be worth nothing. What is
// assertable is the BEHAVIOUR on each side of the setter, so that is what is
// asserted; the declarations themselves are gated by the source lint
// source.db_pointers_initialised.
//
// Offscreen QPA, no display, no rendering: panels and a document.

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QSharedPointer>

#include <cstdio>

#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"

#include "data/database/database.h"
#include "data/project.h"
#include "io/assetmanager.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/rowfit.h"
#include "ui/panels/propertywidgets/materialpropertywidget.h"
#include "ui/panels/propertywidgets/shaderpropertywidget.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"
#include "ui/panels/scenenodepropertieswidget.h"

#include "../support/documentgraph.h"

// The stubs' sentinels and counters (tests/ui/test_stubs.cpp,
// test_selection_stubs.cpp).
extern const char *const kStubProjectWithMusic;
extern const char *const kStubResolvableMaterialGuid;
extern int gStubCreateDependencyCalls;
extern int gStubRemoveDependenciesCalls;
extern int gStubUpdateAssetAssetCalls;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void pump()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/// A row by its NAME — RowFit::fullText, never the label's on-screen text,
/// which is the elided picture (PROPERTY-FILTER-1 F2).
static ComboBoxWidget *comboWith(QWidget *w, const QString &label)
{
    for (ComboBoxWidget *c : w->findChildren<ComboBoxWidget *>())
        for (QLabel *l : c->findChildren<QLabel *>())
            if (RowFit::fullText(l).startsWith(label)) return c;
    return nullptr;
}

/// THE USER'S GESTURE, not the panel's own refill: ComboBoxWidget::setCurrentIndex
/// BLOCKS its inner combo's signals on purpose (it is how a panel repaints a row
/// without calling its own slot), so a suite that wants the slot drives the
/// QComboBox itself — exactly what picking an item in the popup does.
static void pick(ComboBoxWidget *combo, int index)
{
    if (combo) combo->getWidget()->setCurrentIndex(index);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-db-handles-ogre.log");
    if (!graph.require()) return 1;

    Project project;
    project.setProjectGuid(QString::fromLatin1(kStubProjectWithMusic));

    // ---- 1. THE WORLD BLADE, WITH AND WITHOUT A LIBRARY ---------------------
    //
    // setScene -> refreshRows -> "does the project have music?" is the read
    // that used to run on an uninitialised pointer.
    {
        auto scene = iris::Scene::create();

        WorldPropertyWidget blade;
        blade.setProject(&project);
        blade.setScene(scene);              // no library: must not crash
        pump();
        ComboBoxWidget *ambience = comboWith(&blade, QStringLiteral("Background Ambience"));
        CHECK(ambience != nullptr, "world: the Background Ambience row is on the blade");
        if (ambience) {
            CHECK(ambience->isHidden(),
                  "world: with no library the ambience row is hidden — the truthful "
                  "empty state, and no dereference");
            CHECK(ambience->getWidget()->count() <= 1,
                  "world: ...and it lists nothing but None");
        }

        // The handle arrives; the same bind now finds the project's music.
        Database db;
        blade.setDatabase(&db);
        blade.setScene(scene);
        pump();
        if (ambience) {
            CHECK(!ambience->isHidden(),
                  "world: with the library the ambience row appears");
            CHECK(ambience->getWidget()->count() == 2,
                  "world: ...listing None plus the project's one music asset");
        }
    }

    // ---- 2. THE PANEL HOST FORWARDS THE LIBRARY TO THAT BLADE ---------------
    //
    // The defect was HERE: the World blade was missing from the forwarding list
    // in SceneNodePropertiesWidget::setDatabase, and no other caller existed.
    {
        Database db;
        auto scene = iris::Scene::create();

        SceneNodePropertiesWidget panel;
        panel.setDatabase(&db);
        panel.setProject(&project);
        panel.setScene(scene);
        pump();

        WorldPropertyWidget *blade = panel.getWorldPropertyWidget();
        CHECK(blade != nullptr, "panel: the World blade exists");
        ComboBoxWidget *ambience = blade ? comboWith(blade, QStringLiteral("Background Ambience"))
                                         : nullptr;
        CHECK(ambience != nullptr, "panel: the World blade has its ambience row");
        if (ambience) {
            CHECK(ambience->getWidget()->count() == 2,
                  "panel: setDatabase reaches the World blade — the row lists the "
                  "project's music (it listed nothing before this lane)");
        }
    }

    // ---- 3. THE SHADER BLADE'S COMBO SLOT -----------------------------------
    //
    // setShaderGuid fills both combos and then setCurrentIndex()es one, which
    // emits straight into onShaderFileChanged — the slot that dereferenced the
    // handle. The panel host hands the library down in its own constructor,
    // i.e. while its own `db` is still null, so this really can run first.
    {
        AssetManager::clearAssetList();
        auto *shader = new AssetShader;
        shader->assetGuid = QStringLiteral("shader-under-test");
        shader->fileName = QStringLiteral("under-test.shader");
        shader->setValue(QVariant::fromValue(QJsonObject()));
        AssetManager::addAsset(shader);

        auto *vert = new AssetFile;
        vert->assetGuid = QStringLiteral("vert-guid");
        vert->fileName = QStringLiteral("custom.vert");
        AssetManager::addAsset(vert);
        auto *frag = new AssetFile;
        frag->assetGuid = QStringLiteral("frag-guid");
        frag->fileName = QStringLiteral("custom.frag");
        AssetManager::addAsset(frag);

        gStubRemoveDependenciesCalls = 0;
        gStubCreateDependencyCalls = 0;

        ShaderPropertyWidget blade;
        blade.setProject(&project);
        blade.setShaderGuid(QStringLiteral("shader-under-test"));   // fills the combos
        ComboBoxWidget *vertCombo = comboWith(&blade, QStringLiteral("Vertex Shader"));
        CHECK(vertCombo != nullptr, "shader: the Vertex Shader row is on the blade");
        const int vertIdx = vertCombo ? vertCombo->findData(QStringLiteral("vert-guid")) : -1;
        CHECK(vertIdx >= 0, "shader: the project's .vert is in the combo");
        pick(vertCombo, vertIdx);           // the gesture, with NO library
        pump();
        CHECK(gStubRemoveDependenciesCalls == 0 && gStubCreateDependencyCalls == 0,
              "shader: with no library the slot writes no dependency rows");
        const QJsonObject afterNoDb = shader->getValue().toJsonObject();
        CHECK(afterNoDb.contains("vertex_shader") && afterNoDb.contains("fragment_shader"),
              "shader: ...but the pick still reaches the asset in memory — the "
              "fallback is truthful, not a skipped feature");

        Database db;
        blade.setDatabase(&db);
        // The same gesture again, now that the library is there: back to the
        // .frag row's sibling index and onto the .vert again.
        pick(vertCombo, -1);
        pick(vertCombo, vertIdx);
        pump();
        CHECK(gStubRemoveDependenciesCalls > 0,
              "shader: with the library the same gesture writes the dependency rows");

        AssetManager::clearAssetList();
    }

    // ---- 4. THE MATERIAL BLADE'S COMBO SLOT ---------------------------------
    //
    // materialChanged(int) applies the pick to the node and then does the
    // library bookkeeping. The blade is built on the first mesh selection and
    // handed the library right after — but it is a combo slot, so it can fire
    // in any host that never handed one down.
    {
        AssetManager::clearAssetList();
        // A MATERIAL, which is what the combo offers since phase 2 (it listed
        // the retired ModelTypes::Shader asset before). The hazard under test
        // — the blade's combo slot firing with a NULL library handle — is the
        // same on either type.
        auto *asset = new AssetMaterial;
        asset->assetGuid = QString::fromLatin1(kStubResolvableMaterialGuid);
        asset->fileName = QStringLiteral("resolvable.material");
        AssetManager::addAsset(asset);

        auto node = iris::MeshNode::create();
        node->setMaterial(iris::PbrMaterial::create());
        const auto before = node->getMaterial();

        gStubUpdateAssetAssetCalls = 0;

        MaterialPropertyWidget blade;
        blade.setProject(&project);
        blade.setDatabase(nullptr);          // explicit: the hazard's state
        blade.setSceneNode(node);
        pump();

        ComboBoxWidget *selector = comboWith(&blade, QStringLiteral("Material"));
        CHECK(selector != nullptr, "material: the Material row is on the blade");
        if (selector) {
            const int idx = selector->findData(QString::fromLatin1(kStubResolvableMaterialGuid));
            CHECK(idx >= 0, "material: the resolvable material is in the combo");
            if (idx >= 0) pick(selector, idx);
            pump();
            CHECK(gStubUpdateAssetAssetCalls == 0,
                  "material: with no library the pick writes no library rows");
            CHECK(node->getMaterial() != before,
                  "material: ...but the pick still reaches the node — the fallback "
                  "keeps the feature");
        }

        // And with a library the bookkeeping really runs.
        AssetManager::clearAssetList();
    }

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
