/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.panel_lifetime — A PANEL NEVER TOUCHES A ROW IT HAS RETIRED (lane
// PANEL-LIFETIME-1).
//
// THE CLASS. A blade retires a row (AccordianBladeWidget::clearPanel) by
// taking it out of the content pane, hiding it and calling deleteLater(). The
// row is therefore alive for a while and dead afterwards, and WHEN it dies is
// not the panel's decision — it dies at the next turn of the event loop. The
// panels used to keep RAW pointers to those rows and the file that retires
// them stated, in words, the assumption that made that safe: "EVERY script- or
// MCP-driven scene build … is one call that never yields".
//
// That assumption is false on this tree. A script run is a worker thread and a
// nested event loop on this one (the scripting bridge), so the loop turns
// between every verb; the THREADED scene open runs its install stages one per
// turn (services/sceneopenrunner.h); the sky panel defers its own rebuild by a
// turn on purpose. So a retired row can be destroyed at any point, and a raw
// pointer to one is a pointer whose validity depends on what the rest of the
// application does next.
//
// THE INVARIANT THAT REPLACED IT: a panel holds a row as a RowPtr
// (ui/controls/bladerow.h), which reads NULL from the moment the blade retires
// the row — before its destruction, not after it. Two mistakes become
// impossible: writing into a row that has left the panel, and dereferencing
// one the event loop has already destroyed.
//
// The cases below are that invariant, driven through the two panels where the
// raw pointers were real: the SKY blade, which rebuilds its rows on every sky
// type and left the previous type's handles behind, and the MATERIAL blade,
// whose refill path asks its handles whether they can show another material.
// Every case except the last two FAILS on the base binary — cases 1c and 2c
// are the ones to look at in a base log.
//
// Offscreen QPA, no display, no rendering: panels and a document.

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QUndoStack>

#include <cstdio>

#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/scene.h"

#include "services/services.h"
#include "services/undoservice.h"
#include "ui/controls/bladerow.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui/panels/propertyrows.h"
#include "ui/panels/propertywidget.h"
#include "ui/panels/propertywidgets/materialpropertywidget.h"
#include "ui/panels/propertywidgets/skypropertywidget.h"
#include "ui/panels/propertywidgets/worldpostfxpropertywidget.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// ONE TURN OF THE EVENT LOOP — the thing the old assumption said would not
/// happen. Everything a retired row is owed is delivered here.
static void pump()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/// How many rows the property-row registry says are on this blade — i.e. how
/// many rows a user would see. A retired row leaves the registry when it is
/// retired, so this is a LIVE count and not a child count.
static int liveRows(QWidget *panel)
{
    return PropertyRows::registry().list(panel).size();
}

static TexturePickerWidget *anyPicker(QWidget *panel)
{
    const auto pickers = panel->findChildren<TexturePickerWidget *>();
    return pickers.isEmpty() ? nullptr : pickers.first();
}

// ---------------------------------------------------------------------------
// 1. THE SKY BLADE — the rows of the sky type it is no longer showing.
//
// The panel rebuilds itself on every sky type, and each type's rows have their
// own handles (`equiTexture`, `colorTop`, `cubeMapWidget`, …). A switch away
// from a type retires that type's rows and reassigns only the NEW type's
// handles, so every other one was left pointing at a retired row — and
// setEquiMap / setSkyMap (the modeless texture picker's double-click after the row was retired) test
// exactly those handles for null and then write through them.
static void testSkyRowsOfAnotherType(const iris::ScenePtr &scene, StudioServices *services)
{
    SkyPropertyWidget panel;
    panel.setServices(services);

    scene->skyType = iris::SkyType::EQUIRECTANGULAR;
    panel.setScene(scene);
    pump();

    QPointer<TexturePickerWidget> equi = anyPicker(&panel);
    CHECK(equi != nullptr, "sky: the equirectangular sky has an image row");
    if (!equi) return;

    // Away from that type: the row is RETIRED — off the layout, hidden, its
    // deleteLater posted, and still alive (the blade's retired ring).
    scene->skyType = iris::SkyType::SINGLE_COLOR;
    panel.setScene(scene);
    CHECK(!equi.isNull(), "sky: the retired image row is still alive (the ring)");
    CHECK(equi && !equi->isVisibleTo(&panel), "sky: ...and hidden");
    CHECK(bladerow::isRetired(equi.data()), "sky: ...and marked retired");

    // 1c. A WRITE THAT ARRIVES AFTER THE REBUILD — the modeless texture
    // picker's double-click after the row was retired. On the base binary the panel's `equiTexture` still
    // points at this row and the write lands on it; with the handle there is
    // nothing to write to.
    //
    // The observable is the row's own thumbnail label, because that is what
    // the write touches: TexturePickerWidget::setTexture CLEARS it for a path
    // that is not a file, which is every path this call can produce here (the
    // panel has no project, so the guid resolves to nothing).
    QLabel *thumb = equi->findChild<QLabel *>(QStringLiteral("texture"));
    CHECK(thumb != nullptr, "sky: the image row has its thumbnail");
    const QString sentinel = QStringLiteral("pl1-sentinel");
    if (thumb) thumb->setText(sentinel);
    QMetaObject::invokeMethod(&panel, "setEquiMap", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("some-sky-guid")));
    CHECK(thumb && thumb->text() == sentinel,
          "sky: a sky-image write does NOT reach the retired row (base: it did)");

    // 1d. AND AFTER THE LOOP TURNS, when the row is gone for good: the same
    // call must be a no-op rather than a read of freed memory (this is the
    // case an ASan build reports on the base binary).
    pump();
    CHECK(equi.isNull(), "sky: one turn of the loop and the retired row is destroyed");
    QMetaObject::invokeMethod(&panel, "setEquiMap", Qt::DirectConnection,
                              Q_ARG(QString, QStringLiteral("some-sky-guid")));
    QMetaObject::invokeMethod(&panel, "setSkyMap", Qt::DirectConnection,
                              Q_ARG(QJsonObject, QJsonObject()));
    // (reaching this line alive IS the assertion — the base binary crashes above)
}

// ---------------------------------------------------------------------------
// 2. THE MATERIAL BLADE — the refill's handles (ADD-1 review F6/F7).
//
// `rebindTo` points the rows already on screen at another mesh's material
// instead of rebuilding them, and it decides whether it can by asking its
// handles: `if (!materialPropWidget || !materialSelector) return false`. That
// test used to mean "this panel built rows once", which is not the question —
// the question is whether those rows are still ON the panel. Anything that
// clears the blade (clearPanel is public, and a sub-section's owner clears it
// from outside) left the handles non-null and pointing at retired rows, so the
// next pick REFILLED rows nobody can see.
static void testMaterialRefillAfterAClear(StudioServices *services)
{
    MaterialPropertyWidget panel;
    panel.setServices(services);
    panel.setDatabase(nullptr);

    auto matA = iris::PbrMaterial::create();
    auto nodeA = iris::MeshNode::create();
    nodeA->setMaterial(matA);
    auto matB = iris::PbrMaterial::create();
    auto nodeB = iris::MeshNode::create();
    nodeB->setMaterial(matB);

    panel.setSceneNode(nodeA);
    CHECK(panel.rebuildCount() == 1, "material: the first pick builds the rows");
    CHECK(panel.materialCombo() != nullptr, "material: ...including the Material combo");
    CHECK(liveRows(&panel) > 0, "material: ...and the blade is showing them");

    // The rows go, by a route that is not the panel's own clearShownRows.
    panel.clearPanel();

    // 2c. THE HANDLE ANSWERS ABOUT THE PANEL. On the base binary this is a
    // pointer to a retired row and reads non-null.
    CHECK(panel.materialCombo() == nullptr,
          "material: after a clear the Material combo handle is null (base: it was not)");
    CHECK(liveRows(&panel) == 0, "material: and the blade shows no rows");

    pump();   // the retired rows are destroyed here

    // 2d. THE NEXT PICK. It must REBUILD: a refill would point rows that are
    // no longer on the panel (and, after the turn above, no longer alive) at
    // this material.
    panel.setSceneNode(nodeB);
    CHECK(panel.rebuildCount() == 2 && panel.refillCount() == 0,
          "material: a pick after a clear REBUILDS, it does not refill (base: refilled)");
    CHECK(panel.materialCombo() != nullptr && liveRows(&panel) > 0,
          "material: ...so the user sees rows again");

    // AND THE REFILL STILL WORKS where it is legal: same shape, nothing
    // cleared in between (ADD-1's whole point — this must not regress).
    panel.setSceneNode(nodeA);
    CHECK(panel.refillCount() == 1 && panel.rebuildCount() == 2,
          "material: an ordinary same-shape pick still refills");
}

// ---------------------------------------------------------------------------
// 3. THE POST-CHAIN'S LOOKS SECTION — a panel that clears a blade it does not
// own the rows of. rebuildLooks() retires the sub-section's rows on every
// structural change; a turn of the loop between two of them must not leave the
// panel reading anything it retired.
static void testLooksSectionRebuild(const iris::ScenePtr &scene, StudioServices *services)
{
    WorldPostFxPropertyWidget panel;
    panel.setServices(services);
    panel.setScene(scene);
    pump();
    const int rows = liveRows(&panel);
    CHECK(rows > 0, "postfx: the effect rows are on the blade");

    // Two rebuilds with a turn between them (the shape a structural looks edit
    // makes while a threaded open is running).
    panel.setScene(scene);
    pump();
    panel.setScene(scene);
    pump();
    CHECK(liveRows(&panel) == rows, "postfx: a rebind does not multiply or lose rows");
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 4. THE LAW ITSELF, on the blade: every row a clear retires is marked, and a
// handle to one reads null while the row is still alive.
static void testRetirementMarksTheRow()
{
    AccordianBladeWidget blade;
    ComboBoxWidget *combo = blade.addComboBox(QStringLiteral("Row"));
    RowPtr<ComboBoxWidget> handle = combo;
    CHECK(handle != nullptr && handle == combo, "blade: a handle to a live row is that row");
    CHECK(!bladerow::isRetired(combo), "blade: a live row is not retired");

    blade.clearPanel();
    CHECK(bladerow::isRetired(combo), "blade: a cleared row is marked retired");
    CHECK(handle == nullptr, "blade: ...and every handle to it reads null AT ONCE");
    CHECK(handle.retiredOrLive() == combo, "blade: ...while the row itself is still alive");
    CHECK(blade.retiredRowCount() == 1, "blade: the retired ring holds it");

    QPointer<ComboBoxWidget> alive = combo;
    pump();
    CHECK(alive.isNull(), "blade: one turn of the loop and it is gone");
    CHECK(handle == nullptr, "blade: the handle still reads null");
}

// ---------------------------------------------------------------------------
// 4. THE DOCUMENT POINTERS A PROPERTY PANEL HOLDS (lane OPEN-FRAMES-1).
//
// RowPtr guards the ROW WIDGETS. PropertyWidget holds something else as well:
// a bare `QList<iris::Property *>` INTO the document's material, filled when
// the rows are built and nulled by nothing. Every row handler reads it, and
// canRebind() DEREFERENCES the stored side — comparing names, display names and
// a ListProperty's label vocabulary — so a stale entry is a read through freed
// memory and, through a value-changed handler, a write through it. One Qt
// assert of exactly that shape (`str || !len` in QStringView, out of canRebind)
// was caught with a witness on 2026-09-15.
//
// THE RULE IS THE SAME ONE: a retired panel remembers nothing. The retirement
// mark the blade sets reaches this widget too, so the moment its row leaves the
// content pane the document pointers go with it.
static void testPropertyWidgetForgetsOnRetirement()
{
    AccordianBladeWidget blade;
    PropertyWidget *rows = blade.addPropertyWidget();

    auto material = iris::PbrMaterial::create();
    QList<iris::Property *> props;
    for (auto *prop : material->properties)
        if (prop) props.append(prop);
    CHECK(!props.isEmpty(), "properties: the fixture material declares rows");

    rows->setProperties(props);
    CHECK(rows->getProperties().size() == props.size(),
          "properties: the panel is holding the material's property list");
    CHECK(rows->canRebind(props), "properties: ...and would refill itself with the same shape");

    blade.clearPanel();

    // BEFORE the destruction turn: the mark is set synchronously, and this is
    // the window in which the old code could still be asked (and answer yes).
    CHECK(bladerow::isRetired(rows), "properties: the retired panel carries the mark");
    CHECK(rows->getProperties().isEmpty(),
          "properties: a retired panel holds NO document pointers (base: it held them all)");
    CHECK(!rows->canRebind(props),
          "properties: ...and refuses to refill, so the caller rebuilds (base: it agreed)");

    // AND THE DOCUMENT MAY NOW DIE. The list the panel was holding points into
    // this material; dropping it here is the case the guard exists for. The
    // panel outlives it (it is in the retired ring) and must not read it.
    material.clear();
    CHECK(rows->getProperties().isEmpty(), "properties: still nothing held after the material dies");
    CHECK(!rows->canRebind(QList<iris::Property *>()),
          "properties: an empty list never refills either");

    pump();   // the retired panel is destroyed here, having touched nothing
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-panel-lifetime-ogre.log");
    if (!graph.require()) return 1;

    QUndoStack stack;
    UndoService undo(&stack);
    StudioServices services;
    services.undo = &undo;

    auto scene = iris::Scene::create();

    testRetirementMarksTheRow();
    testSkyRowsOfAnotherType(scene, &services);
    testMaterialRefillAfterAClear(&services);
    testLooksSectionRebuild(scene, &services);
    testPropertyWidgetForgetsOnRetirement();

    std::printf(failures ? "ui.panel_lifetime: %d failure(s)\n" : "ui.panel_lifetime: ok\n",
                failures);
    return failures ? 1 : 0;
}
