/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.properties_filter — THE FILTER BOX INSIDE EACH TAB (PROPERTY_FILTER_SPEC,
// lane PROPERTY-FILTER-1; owner decision 2026-09-15 "the box belongs to its
// tab").
//
// The owner's ask was "a box that filters the right column by the NAMES of the
// fields — it will help when I need to look for things like SSR". So the rules
// this suite pins are the ones that ask can fail on:
//
//   * every row has an identity — a live label, a stable key, curated keywords
//     — registered at the two choke points, so nothing in the column is
//     unreachable by text ("ssr" finds Screen-Space Reflections by its KEY; its
//     label does not contain those three letters anywhere);
//   * a section whose rows all hide goes with them, and one with a match opens;
//   * clearing the box puts every row AND every section's expand state back;
//   * the two boxes are independent and survive a trip to the other tab;
//   * the filter is re-applied on every mount (a pick, an undo, a tab switch),
//     so a filtered column never flashes back to the unfiltered one;
//   * a PANEL-hidden row stays hidden when the filter clears (the two-input
//     visibility law) and a panel refresh under a live filter cannot put a
//     filtered-out row back on screen;
//   * a keystroke costs a walk and a layout pass, never a rebuild.
//
// The real panel, the real strip, the real blades, the real Qlementine style,
// offscreen QPA — the ui.properties_tabs shape, same source set.

#include <QApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTabBar>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/materials/pbrmaterial.h"

#include "data/project.h"
#include "data/settingsmanager.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "ui/controls/accordionbladewidget.h"
#include "ui/controls/propertiestabstrip.h"
#include "ui/controls/rowfit.h"
#include "ui/panels/propertyrows.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/style/panelmetrics.h"
#include "ui/style/thememanager.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("ok:   %s\n", name); } \
    else { std::printf("FAIL: %s\n", name); ++failures; } \
} while (0)

namespace {

using Tab = SceneNodePropertiesWidget::Tab;

/// The blade titles currently ON the panel's layout and visible — what the user
/// sees as sections.
QStringList visibleSections(QWidget *panel)
{
    QStringList out;
    QLayout *layout = panel->layout();
    for (int i = 0; layout && i < layout->count(); ++i) {
        QWidget *w = layout->itemAt(i)->widget();
        if (!w || !w->isVisibleTo(panel)) continue;
        if (auto *blade = qobject_cast<AccordianBladeWidget *>(w))
            out.append(blade->panelTitle());
    }
    return out;
}

bool sectionShown(QWidget *panel, const QString &title)
{
    for (const QString &s : visibleSections(panel))
        if (s.startsWith(title, Qt::CaseInsensitive)) return true;
    return false;
}

/// Every row label in the panel, with whether the user can see it. A row's name
/// lives in a child QLabel (named `label` on the generic controls, the title
/// QLabel on the scrubbable ones), and a row the filter hid — or whose section
/// it closed — is not visibleTo the panel.
QList<QPair<QString, bool>> rowLabels(QWidget *panel)
{
    QList<QPair<QString, bool>> out;
    // ONLY THE BLADES THIS TAB HAS MOUNTED, and their nested sections: the
    // panel owns every blade for the window's lifetime (the selection-cost
    // shape), so a findChildren over all of them would count the decal panel's
    // Roughness row while a mesh is selected.
    QList<AccordianBladeWidget *> blades;
    QLayout *top = panel->layout();
    for (int i = 0; top && i < top->count(); ++i) {
        QWidget *w = top->itemAt(i)->widget();
        if (auto *b = qobject_cast<AccordianBladeWidget *>(w)) {
            blades.append(b);
            blades.append(b->findChildren<AccordianBladeWidget *>());
        }
    }
    for (AccordianBladeWidget *blade : blades) {
        QWidget *pane = blade->findChild<QWidget *>(QStringLiteral("contentpane"));
        QLayout *layout = pane ? pane->layout() : nullptr;
        for (int i = 0; layout && i < layout->count(); ++i) {
            QWidget *row = layout->itemAt(i)->widget();
            if (!row) continue;
            // The material list is a PropertyWidget row holding rows of its own.
            if (QWidget *inner = row->findChild<QWidget *>(QStringLiteral("contentpane"))) {
                QLayout *innerLayout = inner->layout();
                for (int j = 0; innerLayout && j < innerLayout->count(); ++j) {
                    QWidget *sub = innerLayout->itemAt(j)->widget();
                    if (!sub) continue;
                    QLabel *subName = sub->findChild<QLabel *>(QStringLiteral("label"));
                    if (!subName) continue;
                    const QVariant subFull = subName->property(RowFit::kFullTextProperty);
                    const QString subText = subFull.isValid() && !subFull.toString().isEmpty()
                                                ? subFull.toString() : subName->text();
                    out.append({ subText.trimmed(), sub->isVisibleTo(panel) });
                }
                continue;
            }
            QLabel *name = row->findChild<QLabel *>(QStringLiteral("label"));
            if (!name) {
                for (QLabel *l : row->findChildren<QLabel *>())
                    if (!l->text().trimmed().isEmpty()) { name = l; break; }
            }
            if (!name) continue;
            const QVariant full = name->property(RowFit::kFullTextProperty);
            const QString text = full.isValid() && !full.toString().isEmpty()
                                     ? full.toString() : name->text();
            out.append({ text.trimmed(), row->isVisibleTo(panel) });
        }
    }
    return out;
}

/// Is there a row with this label, and is it on screen?
bool rowShown(QWidget *panel, const QString &label)
{
    for (const auto &r : rowLabels(panel))
        if (r.first.compare(label, Qt::CaseInsensitive) == 0
            || r.first.startsWith(label, Qt::CaseInsensitive)) return r.second;
    return false;
}

bool rowExists(QWidget *panel, const QString &label)
{
    for (const auto &r : rowLabels(panel))
        if (r.first.compare(label, Qt::CaseInsensitive) == 0
            || r.first.startsWith(label, Qt::CaseInsensitive)) return true;
    return false;
}

int shownRowCount(QWidget *panel)
{
    int n = 0;
    for (const auto &r : rowLabels(panel)) if (r.second) ++n;
    return n;
}

}   // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-properties-filter-ogre.log");
    if (!graph.require()) return 1;

    auto *settings = SettingsManager::getDefaultManager();
    settings->settings->remove(ThemeManager::settingsKey());
    ThemeManager::applyAtStartup(app);

    QUndoStack stack;
    UndoService undo(&stack);
    StudioServices services;
    services.undo = &undo;

    Project project;
    project.setProjectGuid(QStringLiteral("ui-properties-filter"));

    auto scene = iris::Scene::create();
    auto mesh = iris::MeshNode::create();
    mesh->setName(QStringLiteral("mesh"));
    mesh->setMaterial(iris::PbrMaterial::create());
    auto light = iris::LightNode::create();
    light->setName(QStringLiteral("light"));
    scene->getRootNode()->addChild(mesh.staticCast<iris::SceneNode>());
    scene->getRootNode()->addChild(light.staticCast<iris::SceneNode>());

    // The dock as shell/mainwindow.cpp builds it: the strip (tab bar + filter
    // box) above the scroll area, the panel inside it.
    QWidget host;
    auto *hostLayout = new QVBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    auto *panel = new SceneNodePropertiesWidget;
    panel->setServices(&services);
    panel->setDatabase(nullptr);
    panel->setProject(&project);
    auto *strip = new PropertiesTabStrip(panel, &host);
    auto *scroll = new QScrollArea(&host);
    scroll->setMinimumWidth(PanelMetrics::rightColumnMinWidth);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    hostLayout->addWidget(strip);
    hostLayout->addWidget(scroll);
    host.resize(PanelMetrics::rightColumnWidth, 900);
    host.show();
    QApplication::processEvents();

    QLineEdit *box = strip->filterBox();
    auto turn = []() { QApplication::processEvents(); QApplication::sendPostedEvents(); };

    panel->setScene(scene);
    panel->setSceneNode(scene->getRootNode());          // the scene open: World
    turn();

    // ---- 1. the box is there, and it is a box ------------------------------
    CHECK(box != nullptr, "properties_filter: the strip carries a filter box");
    CHECK(box && box->isClearButtonEnabled(),
          "properties_filter: the box has the style's clear button");
    CHECK(box && !box->placeholderText().isEmpty(),
          "properties_filter: the box says what it is (a placeholder)");
    CHECK(box && box->styleSheet().isEmpty() && strip->styleSheet().isEmpty(),
          "properties_filter: no raw stylesheet on the box or the strip (theme law)");
    // THE COLUMN-WIDTH LAW still holds with the box in the strip.
    const int stripMin = strip->minimumSizeHint().width();
    CHECK(stripMin <= PanelMetrics::rightColumnMinWidth,
          QStringLiteral("properties_filter: the strip still fits the narrowest column "
                         "(%1 px <= %2)").arg(stripMin)
              .arg(PanelMetrics::rightColumnMinWidth).toUtf8().constData());

    // ---- 2. EVERY ROW HAS AN IDENTITY --------------------------------------
    // The registry's account of the column must equal what the column holds:
    // a row that never registered would be invisible to the filter and would
    // simply stay on screen for every search.
    {
        int registered = 0, named = 0, keyed = 0;
        for (const QString &title : visibleSections(panel)) {
            for (AccordianBladeWidget *blade : panel->findChildren<AccordianBladeWidget *>()) {
                if (blade->panelTitle() != title) continue;
                const auto listing = PropertyRows::registry().list(blade);
                registered += listing.size();
                for (const auto &row : listing) {
                    if (!row.label.isEmpty() || !row.keywords.isEmpty()) ++named;
                    if (!row.key.isEmpty()) ++keyed;
                }
            }
        }
        std::printf("      world rows registered=%d named=%d keyed=%d\n",
                    registered, named, keyed);
        CHECK(registered >= 40,
              QStringLiteral("properties_filter: the World tab's rows are registered (%1)")
                  .arg(registered).toUtf8().constData());
        CHECK(named >= registered - 4,
              QStringLiteral("properties_filter: all but the section-bound rows carry a name "
                             "(%1 of %2)").arg(named).arg(registered).toUtf8().constData());
        CHECK(keyed >= 20,
              QStringLiteral("properties_filter: the world rows carry stable keys (%1)")
                  .arg(keyed).toUtf8().constData());

        // THE LISTING THE VERB ANSWERS WITH (editor.properties, §3.5): the same
        // rows, in column order, with both halves of the visibility law.
        const auto listing = panel->propertyRows(Tab::World);
        CHECK(listing.size() == registered,
              QStringLiteral("properties_filter: editor.properties lists the tab's rows "
                             "(%1 of %2)").arg(listing.size()).arg(registered)
                  .toUtf8().constData());
        bool ssrListed = false;
        for (const auto &row : listing)
            if (row.key == QStringLiteral("world.override:ssr")
                && row.label == QStringLiteral("Screen-Space Reflections")
                && !row.sections.isEmpty() && row.visible && !row.filteredOut)
                ssrListed = true;
        CHECK(ssrListed,
              "properties_filter: ...each with its section chain, its key and its verdict");
    }

    // ---- 3. "ssr" — THE OWNER'S OWN EXAMPLE --------------------------------
    // The row is called "Screen-Space Reflections"; only its KEY says ssr.
    const int allRows = shownRowCount(panel);
    panel->setPropertiesFilter(Tab::World, QStringLiteral("ssr"));
    turn();
    CHECK(rowShown(panel, QStringLiteral("Screen-Space Reflections")),
          "properties_filter: \"ssr\" finds Screen-Space Reflections (by its key)");
    CHECK(!rowShown(panel, QStringLiteral("Sky Type")),
          "properties_filter: ...and the Sky rows are gone");
    // D7a (the owner's pick): the header STAYS — greyed and closed — so the
    // column says where its settings are instead of reading as a broken panel.
    {
        AccordianBladeWidget *sky = nullptr;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            if (b->panelTitle() == QStringLiteral("Sky")) sky = b;
        CHECK(sky && sky->isVisibleTo(panel) && !sky->isExpanded() && sky->isHeaderMuted(),
              "properties_filter: a section with no match keeps its header, greyed and "
              "collapsed (D7a)");
        CHECK(!rowShown(panel, QStringLiteral("Sky Color")),
              "properties_filter: ...and none of its rows is on screen");
    }
    CHECK(shownRowCount(panel) < allRows / 2,
          QStringLiteral("properties_filter: the column is a fraction of itself (%1 of %2 rows)")
              .arg(shownRowCount(panel)).arg(allRows).toUtf8().constData());
    {
        const auto counts = panel->filterCounts(Tab::World);
        std::printf("      \"ssr\": visible=%d hidden=%d\n", counts.visible, counts.hidden);
        CHECK(counts.visible >= 1 && counts.hidden > counts.visible,
              "properties_filter: the counts say what the filter did");
    }
    // A MATCHING SECTION IS OPEN — the row is on screen without a click.
    {
        bool postOpen = false;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            if (b->panelTitle().startsWith(QStringLiteral("Post")) && b->isVisibleTo(panel))
                postOpen = b->isExpanded();
        CHECK(postOpen, "properties_filter: the section holding the match is expanded");
    }

    // ---- 4. a synonym, and the section-title rule --------------------------
    panel->setPropertiesFilter(Tab::World, QStringLiteral("reflections"));
    turn();
    CHECK(rowShown(panel, QStringLiteral("Screen-Space Reflections")),
          "properties_filter: \"reflections\" finds SSR too (label and keyword)");
    CHECK(rowShown(panel, QStringLiteral("Sun Disc in Reflections")),
          "properties_filter: ...and every other row that says reflections");

    panel->setPropertiesFilter(Tab::World, QStringLiteral("sun disc"));
    turn();
    CHECK(rowShown(panel, QStringLiteral("Sun Disc"))
              && rowShown(panel, QStringLiteral("Sun Disc Size")),
          "properties_filter: two words = both must match (the three Sun Disc rows)");
    CHECK(!rowShown(panel, QStringLiteral("Gravity")),
          "properties_filter: ...and nothing else");

    panel->setPropertiesFilter(Tab::World, QStringLiteral("fog"));
    turn();
    CHECK(sectionShown(panel, QStringLiteral("Fog")) && rowShown(panel, QStringLiteral("Fog Density")),
          "properties_filter: a matching SECTION TITLE keeps its whole section (D4)");

    // ---- 5. CLEARING PUTS EVERYTHING BACK ----------------------------------
    // Including the expand state: Fog starts COLLAPSED and must be collapsed
    // again after a filter opened it.
    AccordianBladeWidget *fogBlade = nullptr;
    for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
        if (b->panelTitle() == QStringLiteral("Fog")) fogBlade = b;
    CHECK(fogBlade && fogBlade->isExpanded(),
          "properties_filter: the Fog section was opened by its match");
    panel->setPropertiesFilter(Tab::World, QString());
    turn();
    CHECK(shownRowCount(panel) == allRows,
          QStringLiteral("properties_filter: an empty box is every row back (%1 of %2)")
              .arg(shownRowCount(panel)).arg(allRows).toUtf8().constData());
    CHECK(fogBlade && !fogBlade->isExpanded(),
          "properties_filter: ...and every section back to the expand state it had");
    CHECK(sectionShown(panel, QStringLiteral("Sky")),
          "properties_filter: ...and every section back on screen");
    {
        bool anyMuted = false;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            if (b->isVisibleTo(panel) && b->isHeaderMuted()) anyMuted = true;
        CHECK(!anyMuted, "properties_filter: ...and no header is left greyed");
    }

    // ---- 6. THE TWO-INPUT VISIBILITY LAW -----------------------------------
    // A row the PANEL hides (a spot row on a point light, the AA driver
    // readout) must stay hidden when the filter clears, and a filtered-out row
    // must stay hidden when the panel refreshes. Driven here through the same
    // call the 74 panel sites make.
    {
        QWidget *gravity = nullptr;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>()) {
            if (b->panelTitle() != QStringLiteral("World")) continue;
            QWidget *pane = b->findChild<QWidget *>(QStringLiteral("contentpane"));
            QLayout *l = pane ? pane->layout() : nullptr;
            for (int i = 0; l && i < l->count(); ++i) {
                QWidget *row = l->itemAt(i)->widget();
                QLabel *name = row ? row->findChild<QLabel *>(QStringLiteral("label")) : nullptr;
                const QVariant full = name ? name->property(RowFit::kFullTextProperty) : QVariant();
                if (full.toString() == QStringLiteral("Gravity")) gravity = row;
            }
        }
        CHECK(gravity != nullptr, "properties_filter: the Gravity row is there to drive");
        if (gravity) {
            PropertyRows::setPanelVisible(gravity, false);      // the panel's intent
            CHECK(!gravity->isVisibleTo(panel),
                  "properties_filter: the panel can hide a row");
            panel->setPropertiesFilter(Tab::World, QStringLiteral("gravity"));
            turn();
            CHECK(!gravity->isVisibleTo(panel),
                  "properties_filter: a MATCHING row the panel hides stays hidden (two inputs)");
            panel->setPropertiesFilter(Tab::World, QString());
            turn();
            CHECK(!gravity->isVisibleTo(panel),
                  "properties_filter: ...and clearing the filter does not put it back");
            // The other direction: a panel refresh under a live filter must not
            // un-hide a filtered-out row.
            panel->setPropertiesFilter(Tab::World, QStringLiteral("ssr"));
            turn();
            PropertyRows::setPanelVisible(gravity, true);       // the panel shows it again
            CHECK(!gravity->isVisibleTo(panel),
                  "properties_filter: a panel refresh cannot show a row the filter removed");
            panel->setPropertiesFilter(Tab::World, QString());
            turn();
            CHECK(gravity->isVisibleTo(panel),
                  "properties_filter: ...and it comes back when the filter clears");
        }
    }

    // ---- 7. THE SELECTION TAB'S OWN BOX ------------------------------------
    panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
    turn();
    CHECK(panel->propertiesTab() == Tab::Selection,
          "properties_filter: the mesh is picked (Selection tab)");
    const bool hasRoughness = rowExists(panel, QStringLiteral("Roughness"));
    CHECK(hasRoughness, "properties_filter: the mesh's material blade has a Roughness row");
    panel->setPropertiesFilter(Tab::Selection, QStringLiteral("roughness"));
    turn();
    CHECK(rowShown(panel, QStringLiteral("Roughness")),
          "properties_filter: the Selection box finds a row INSIDE the material blade");
    CHECK(!rowShown(panel, QStringLiteral("Cast Shadow")),
          "properties_filter: ...and the mesh's own rows are gone");

    // ---- 8. THE TWO BOXES ARE INDEPENDENT ----------------------------------
    panel->setPropertiesFilter(Tab::World, QStringLiteral("ssr"));
    turn();
    CHECK(panel->propertiesFilter(Tab::Selection) == QStringLiteral("roughness")
              && panel->propertiesFilter(Tab::World) == QStringLiteral("ssr"),
          "properties_filter: each tab keeps its own text");
    CHECK(rowShown(panel, QStringLiteral("Roughness")),
          "properties_filter: setting the OTHER tab's filter does not touch this one's rows");
    panel->setPropertiesTab(Tab::World);
    turn();
    CHECK(box->text() == QStringLiteral("ssr"),
          "properties_filter: the box shows the tab's own text on a switch");
    CHECK(rowShown(panel, QStringLiteral("Screen-Space Reflections"))
              && !rowShown(panel, QStringLiteral("Sky Type")),
          "properties_filter: ...and the World tab is filtered by ITS text");
    panel->setPropertiesTab(Tab::Selection);
    turn();
    CHECK(box->text() == QStringLiteral("roughness") && rowShown(panel, QStringLiteral("Roughness")),
          "properties_filter: and back — the Selection filter is still in force");

    // ---- 9. A PICK RE-APPLIES THE FILTER, with no unfiltered frame ---------
    panel->setSceneNode(light.staticCast<iris::SceneNode>());
    turn();
    CHECK(!rowShown(panel, QStringLiteral("Intensity")),
          "properties_filter: a pick re-applies the Selection filter (the light's rows "
          "do not match \"roughness\")");
    panel->setPropertiesFilter(Tab::Selection, QStringLiteral("shadow"));
    turn();
    CHECK(rowShown(panel, QStringLiteral("Shadow Type")),
          "properties_filter: \"shadow\" finds the light's shadow rows");
    panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
    turn();
    CHECK(rowShown(panel, QStringLiteral("Cast Shadow")) && !rowShown(panel, QStringLiteral("Roughness")),
          "properties_filter: ...and the same text applies to the next object picked");

    // ---- 10. Esc CLEARS THE BOX -------------------------------------------
    {
        box->setFocus();
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(box, &esc);
        turn();
        CHECK(box->text().isEmpty() && panel->propertiesFilter(Tab::Selection).isEmpty(),
              "properties_filter: Esc clears the box and the filter with it");
        CHECK(rowShown(panel, QStringLiteral("Roughness")),
              "properties_filter: ...and the rows come back");
    }

    // ---- 11. A KEYSTROKE IS CHEAP -----------------------------------------
    // Rows are hidden and shown, never rebuilt. The bound is per keystroke on
    // the widest column the editor has (a mesh's Selection tab) and on the
    // World tab's nine sections (VR joined them in lane VR-WORLD-1).
    {
        auto typeCost = [&](Tab tab, const QString &text) {
            panel->setPropertiesFilter(tab, QString());
            turn();
            double worst = 0.0;
            for (int i = 1; i <= text.size(); ++i) {
                QElapsedTimer t; t.start();
                panel->setPropertiesFilter(tab, text.left(i));
                const double ms = t.nsecsElapsed() / 1e6;
                worst = qMax(worst, ms);
            }
            panel->setPropertiesFilter(tab, QString());
            turn();
            return worst;
        };
        panel->setPropertiesTab(Tab::Selection);
        turn();
        const double meshMs = typeCost(Tab::Selection, QStringLiteral("rough"));
        panel->setPropertiesTab(Tab::World);
        turn();
        const double worldMs = typeCost(Tab::World, QStringLiteral("ssr"));
        std::printf("      keystroke: world %.3f ms, mesh selection %.3f ms\n", worldMs, meshMs);
        CHECK(worldMs < 5.0,
              QStringLiteral("properties_filter: a keystroke on the World tab costs "
                             "%1 ms (< 5)").arg(worldMs, 0, 'f', 3).toUtf8().constData());
        CHECK(meshMs < 5.0,
              QStringLiteral("properties_filter: a keystroke on a mesh's Selection tab costs "
                             "%1 ms (< 5)").arg(meshMs, 0, 'f', 3).toUtf8().constData());
    }

    // ---- 11b. THE OTHER TAB'S SECTIONS KEEP THEIR EXPAND STATE -------------
    // isExpanded() used to read isVisible(), which is false for every child of
    // a hidden widget — and a blade IS hidden whenever its tab is not in front.
    // So filtering the World tab while the Selection tab was on screen
    // snapshotted "every section closed" and clearing the box closed them all.
    {
        panel->setPropertiesTab(Tab::Selection);
        turn();
        panel->setPropertiesFilter(Tab::World, QStringLiteral("ssr"));   // not the tab on screen
        turn();
        panel->setPropertiesFilter(Tab::World, QString());
        panel->setPropertiesTab(Tab::World);
        turn();
        CHECK(shownRowCount(panel) > 0,
              QStringLiteral("properties_filter: filtering the tab NOT on screen leaves its "
                             "sections open (%1 rows)").arg(shownRowCount(panel))
                  .toUtf8().constData());
    }

    // ---- 11c. THE SNAPSHOT SURVIVES A REBUILD (F1, second reader) ---------
    // THE USER PATH: Selection tab, type something, pick a second mesh, clear
    // the box. The material blade's "Detail Layers" is a nested SECTION and
    // dies like any row — clearPanel retires it, deleteLater frees it — while
    // the expand snapshot taken by the first keystroke still names it. With raw
    // pointers the restore qobject_cast freed memory.
    //
    // The assertion is deterministic and does not need a sanitizer: the suite
    // holds a QPointer to the very section the snapshot named and proves it is
    // GONE by the time the restore runs (which is what the DeferredDelete flush
    // is for — a script run never turns the event loop, which is why this path
    // looked safe from inside a suite).
    {
        // THE SECOND MESH HAS NO MATERIAL, and that is what makes this a
        // REBUILD (ADD-1, 2026-09-15): showing another mesh's material of the
        // same shape is a refill now — the same rows, pointed at another
        // property list, nothing destroyed — so a second ordinary cube would no
        // longer free the section this case is about. A mesh whose material is
        // gone has no rows at all, which is the shape change a user really
        // produces and the one that still retires them.
        auto second = iris::MeshNode::create();
        second->setName(QStringLiteral("mesh2"));
        scene->getRootNode()->addChild(second.staticCast<iris::SceneNode>());

        panel->setPropertiesFilter(Tab::Selection, QString());
        panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
        turn();
        QPointer<AccordianBladeWidget> nested;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            if (b->panelTitle() == QStringLiteral("Detail Layers")) nested = b;
        CHECK(!nested.isNull(),
              "properties_filter: the material blade has a nested section to lose");

        panel->setPropertiesFilter(Tab::Selection, QStringLiteral("detail"));   // snapshot
        turn();
        panel->setSceneNode(second.staticCast<iris::SceneNode>());              // rebuild
        turn();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);        // free
        CHECK(nested.isNull(),
              "properties_filter: the snapshotted section is FREED by the pick that "
              "rebuilt it (the precondition of the use-after-free)");

        panel->setPropertiesFilter(Tab::Selection, QString());                  // restore
        turn();
        CHECK(rowShown(panel, QStringLiteral("Cast Shadow"))
                  && !rowShown(panel, QStringLiteral("Roughness")),
              "properties_filter: the restore skips what is gone (no material rows on a "
              "bare mesh) and the column is whole (its mesh rows back)");
        // ...and the column comes back whole for a mesh that HAS a material:
        // the blade rebuilds its rows, nested section included.
        panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
        turn();
        bool rebuilt = false;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            if (b->panelTitle() == QStringLiteral("Detail Layers") && b->isVisibleTo(panel))
                rebuilt = true;
        CHECK(rebuilt && rowShown(panel, QStringLiteral("Roughness")),
              "properties_filter: ...and a mesh with a material gets its nested section back");
    }

    // ---- 11d. AN ELIDED SECTION TITLE (F2, second reader) ----------------
    // A section title is fitted like every row (the blade's ctor calls
    // RowFit::fitLabel on it), so when the header is narrower than the name it
    // reads "Photon — Realt…" — and the filter matches section TITLES. Reading
    // text() there loses the section for every word past the ellipsis.
    //
    // The width at which that happens is a font metric, so the suite drives the
    // MECHANISM instead of guessing a dock width: the header label is given a
    // width the name cannot fit in, which is exactly what a 300 px column does.
    {
        panel->setPropertiesTab(Tab::World);
        panel->setPropertiesFilter(Tab::World, QString());
        turn();
        AccordianBladeWidget *photon = nullptr;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            if (b->panelTitle().startsWith(QStringLiteral("Photon"))) photon = b;
        CHECK(photon != nullptr, "properties_filter: the Photon section is there");
        if (photon) {
            QLabel *title = photon->titleLabel();
            // The header label is given a width its name cannot fit in — which
            // is what a narrow dock does to it through the layout. Asserted
            // WITHOUT turning the event loop: the blade's layout would put the
            // width straight back, and RowFit would un-elide with it.
            title->resize(90, title->height());
            QResizeEvent resize(title->size(), QSize(263, title->height()));
            QApplication::sendEvent(title, &resize);
            std::printf("      elided header at %d px: \"%s\" — name \"%s\"\n",
                        title->width(), qPrintable(title->text()),
                        qPrintable(photon->panelTitle()));
            CHECK(title->text() != photon->panelTitle(),
                  "properties_filter: a header too narrow for its name is elided");
            CHECK(photon->panelTitle() ==
                      QStringLiteral("Photon — Realtime Global Illumination"),
                  "properties_filter: ...and the section still KNOWS its name");
            panel->setPropertiesFilter(Tab::World, QStringLiteral("illumination"));
            CHECK(sectionShown(panel, QStringLiteral("Photon")),
                  "properties_filter: ...so a word past the ellipsis still finds it");
            panel->setPropertiesFilter(Tab::World, QString());
            turn();
        }
    }

    // ---- 11b-2. THE "NOTHING SELECTED" LINE IS NOT A SECTION (F8) ---------
    // With nothing picked the Selection tab says so. That line is the panel's
    // message about the SELECTION, not a section with rows — a filter must not
    // take it away and leave a blank, wordless tab.
    {
        panel->setSceneNode(iris::SceneNodePtr());
        panel->setPropertiesTab(Tab::Selection);
        panel->setPropertiesFilter(Tab::Selection, QString());
        turn();
        auto hintShown = [panel]() {
            QLayout *l = panel->layout();
            for (int i = 0; l && i < l->count(); ++i) {
                QWidget *w = l->itemAt(i)->widget();
                if (qobject_cast<AccordianBladeWidget *>(w)) continue;
                if (auto *label = qobject_cast<QLabel *>(w))
                    if (label->isVisibleTo(panel) && !label->text().isEmpty()) return true;
            }
            return false;
        };
        CHECK(hintShown(), "properties_filter: an empty Selection tab says nothing is selected");
        panel->setPropertiesFilter(Tab::Selection, QStringLiteral("roughness"));
        turn();
        CHECK(hintShown(),
              "properties_filter: ...and a filter does not take that line away");
        panel->setPropertiesFilter(Tab::Selection, QString());
        panel->setSceneNode(mesh.staticCast<iris::SceneNode>());
        turn();
    }

    // ---- 11c-2. A FILTER CLEARED OFF SCREEN STILL OWES THE SECTIONS (F7) --
    // A verb (or the other tab's box) can filter and clear a tab that is not in
    // front, where neither the apply nor the restore can run. The clear used to
    // drop the debt: the sections stayed the way the filter had left them, with
    // an empty box, and the next filter overwrote the snapshot with that state.
    {
        panel->setPropertiesTab(Tab::World);
        panel->setPropertiesFilter(Tab::World, QString());
        turn();
        AccordianBladeWidget *sky = nullptr;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            if (b->panelTitle() == QStringLiteral("Sky")) sky = b;
        CHECK(sky && sky->isExpanded(),
              "properties_filter: the Sky section is open before the filter");
        panel->setPropertiesFilter(Tab::World, QStringLiteral("ssr"));   // closes it
        turn();
        CHECK(sky && !sky->isExpanded(), "properties_filter: ...and closed by it");
        panel->setPropertiesTab(Tab::Selection);                         // leave the tab
        turn();
        panel->setPropertiesFilter(Tab::World, QString());               // cleared off screen
        turn();
        panel->setPropertiesTab(Tab::World);                             // come back
        turn();
        CHECK(sky && sky->isExpanded(),
              "properties_filter: a clear that happened off screen is paid at the next "
              "mount — the section is open again");
        CHECK(sky && !sky->isHeaderMuted(),
              "properties_filter: ...and its header is no longer greyed");
    }

    // ---- 11d-2. A ROW RE-LABELLED WHILE ITS TAB IS HIDDEN (F5) ------------
    // Panels rename rows (the sky panel's sun row, a light's Tint). A row whose
    // tab is not on screen gets no paint and no resize, so RowFit never elides
    // the new name — and the stored full text is the OLD one until it does.
    // fullText() has to prefer what is actually on the label in that state.
    {
        panel->setPropertiesTab(Tab::Selection);
        panel->setPropertiesFilter(Tab::World, QString());
        turn();
        QLabel *gravity = nullptr;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>()) {
            if (b->panelTitle() != QStringLiteral("World")) continue;
            for (QLabel *l : b->findChildren<QLabel *>(QStringLiteral("label")))
                if (RowFit::fullText(l) == QStringLiteral("Gravity")) gravity = l;
        }
        CHECK(gravity != nullptr, "properties_filter: a World row to re-label");
        if (gravity) {
            gravity->setText(QStringLiteral("Pull Of The Earth"));   // hidden tab: no elision
            CHECK(RowFit::fullText(gravity) == QStringLiteral("Pull Of The Earth"),
                  "properties_filter: a row re-labelled while hidden reports the NEW name");
            panel->setPropertiesTab(Tab::World);
            panel->setPropertiesFilter(Tab::World, QStringLiteral("pull"));
            turn();
            CHECK(rowShown(panel, QStringLiteral("Pull Of The Earth")),
                  "properties_filter: ...and the filter finds it by that name");
            panel->setPropertiesFilter(Tab::World, QString());
            gravity->setText(QStringLiteral("Gravity"));
            turn();
        }
    }

    // ---- 11e. A BLADE THAT DIES LEAVES NOTHING BEHIND (F4) ----------------
    // The registry keys its row lists by RAW pointer, and a TOP-LEVEL blade is
    // a container that is nobody's row — so nothing but its own destruction can
    // drop its list, and retire() used to return before it got there (the
    // `entries` lookup missed). A stale list keyed by a freed address is
    // inherited by whatever Qt allocates there next.
    {
        auto &registry = PropertyRows::registry();
        const int before = registry.trackedContainers();
        {
            auto *scratch = new AccordianBladeWidget;
            scratch->setPanelTitle(QStringLiteral("Scratch"));
            scratch->addCheckBox(QStringLiteral("Scratch Row"), false);
            auto *nested = scratch->addSection(QStringLiteral("Scratch Section"));
            nested->addCheckBox(QStringLiteral("Nested Row"), false);
            CHECK(registry.trackedContainers() == before + 2,
                  "properties_filter: a blade and its nested section are tracked");
            delete scratch;
        }
        CHECK(registry.trackedContainers() == before,
              QStringLiteral("properties_filter: ...and BOTH are gone when the blade dies "
                             "(%1, was %2)").arg(registry.trackedContainers()).arg(before)
                  .toUtf8().constData());
    }

    // ---- 12. nothing matches, and the box still works ----------------------
    panel->setPropertiesTab(Tab::World);
    panel->setPropertiesFilter(Tab::World, QString());
    turn();
    const int worldRowsNow = shownRowCount(panel);
    panel->setPropertiesFilter(Tab::World, QStringLiteral("zzzznothing"));
    turn();
    CHECK(shownRowCount(panel) == 0,
          "properties_filter: a text nothing matches leaves no ROW on screen (and says so "
          "in the counts), rather than showing everything");
    // TEN since the World panel grew its Clouds section (CLOUDS-2D-1): World,
    // Sky, Clouds, World Mode, Photon, Post Process, Anti-Aliasing, Shadows, VR, Fog.
    CHECK(visibleSections(panel).size() == 10,
          QStringLiteral("properties_filter: ...while every section header stays, greyed "
                         "and closed (%1 of 10)").arg(visibleSections(panel).size())
              .toUtf8().constData());
    panel->setPropertiesFilter(Tab::World, QString());
    turn();
    CHECK(shownRowCount(panel) == worldRowsNow,
          QStringLiteral("properties_filter: ...and clearing it is a full recovery (%1 of %2)")
              .arg(shownRowCount(panel)).arg(worldRowsNow).toUtf8().constData());

    std::printf(failures == 0 ? "ALL PASS\n" : "%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
