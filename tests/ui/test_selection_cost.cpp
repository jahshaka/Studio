/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.selection_cost — SELECTING A NODE COSTS THE SAME AT SWITCH 200 AS AT
// SWITCH 1 (critical perf regression, owner session 2026-09-08: the editor fell
// to 1 fps with 2-7 s UI stalls after about an hour of use, and every watchdog
// backtrace stood in
// oclero::qlementine::WidgetWithFocusFrameEventFilter::refreshFocusFrame(),
// called from SceneNodePropertiesWidget::clearLayout() ->
// QWidget::setParent(nullptr) -> MainWindow::applySelectionToUi).
//
// TWO defects made that stall, and this suite pins both:
//
//   THE STORM. clearLayout() detached every blade with setParent(nullptr) and
//   setSceneNode() re-attached it a few lines later. Qlementine installs one
//   WidgetWithFocusFrameEventFilter per focusable descendant and each of them
//   watches its ANCESTORS, so a single selection change delivered two
//   QEvent::ParentChange to each blade and every filter under it answered with
//   a QFocusFrame re-derivation: a QWidget::setParent of the frame plus
//   install/remove of the frame's event filters along the whole chain. Dozens
//   of times, twice, per click.
//
//   THE GROWTH. The material blade was rebuilt for every mesh selection and the
//   previous one was handed to clearLayout(), which orphaned it instead of
//   deleting it — a live widget tree, with its focus frames, its filters and
//   its property listeners, leaked per click for the whole session.
//
// So the assertions are: per-switch wall time stays FLAT (the last 50 switches
// cost no more than 1.5x the first 50), the population of live widgets and
// focus frames does not grow across 200 switches, and a switch sends ZERO
// QEvent::ParentChange to the widgets it reuses — which is also, exactly, why
// the focus frames stop being re-derived.
//
// The real SceneNodePropertiesWidget, the real property blades, the real
// Qlementine style (without it there are no focus frames and no amplifier at
// all) and a real 15-node scene of mixed types. Offscreen QPA; the widgets are
// painted with grab() every turn, because a focus frame only attaches after its
// widget's FIRST PAINT.

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFocusFrame>
#include <QScrollArea>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/materials/pbrmaterial.h"

#include "services/services.h"
#include "services/undoservice.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/style/thememanager.h"
#include "data/settingsmanager.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("ok:   %s\n", name); } \
    else { std::printf("FAIL: %s\n", name); ++failures; } \
} while (0)

namespace {

/// Counts QEvent::ParentChange, split three ways: events to widgets that
/// existed BEFORE the measured stretch began (the reused blades and their
/// subtrees — the storm), events to QFocusFrame objects (the amplifier's own
/// re-parenting, which is what the owner's backtraces were standing in), and
/// everything else (rows a panel legitimately builds for the node it is now
/// showing).
class ParentChangeCounter : public QObject {
public:
    QSet<const QObject *> blades;       // the panel's own permanent children
    int onBlades = 0;                   // must be exactly zero: the storm's source
    int frameReDerivations = 0;         // a frame re-parented AGAIN: the amplifier
    int total = 0;
    // Keyed by ADDRESS but VALIDATED by QPointer: frames are destroyed with
    // their widgets now, and malloc recycles the address — a raw pointer set
    // would report a brand-new frame's first attach as a re-derivation.
    QHash<const QObject *, QPointer<QFocusFrame>> framesSeen;
    QMap<QString, int> byClass;         // WHO, when a count is not zero

    void reset() { onBlades = frameReDerivations = total = 0; framesSeen.clear(); byClass.clear(); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ParentChange) {
            ++total;
            if (blades.contains(watched)) {
                ++onBlades;
                ++byClass[QString::fromLatin1(watched->metaObject()->className())];
            }
            if (qobject_cast<QFocusFrame *>(watched)) {
                // A frame's FIRST re-parent is its attachment (setWidget hands
                // it to the scroll viewport). Any later one is a
                // RE-DERIVATION — the work the owner's backtraces were standing
                // in, and the thing a selection change must never cause.
                auto it = framesSeen.constFind(watched);
                if (it != framesSeen.cend() && !it.value().isNull())
                    ++frameReDerivations;
                else
                    framesSeen.insert(watched, static_cast<QFocusFrame *>(watched));
            }
        }
        return QObject::eventFilter(watched, event);
    }
};

double average(const QVector<double> &v, int from, int count)
{
    double sum = 0;
    for (int i = from; i < from + count; ++i) sum += v[i];
    return count > 0 ? sum / count : 0.0;
}

double median(QVector<double> v, int from, int count)
{
    QVector<double> slice(v.begin() + from, v.begin() + from + count);
    std::sort(slice.begin(), slice.end());
    return slice[slice.size() / 2];
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-selection-cost-ogre.log");
    if (!graph.require()) return 1;

    // THE STYLE IS THE POINT: focus frames (and the event filters that maintain
    // them) exist only under Qlementine.
    auto *settings = SettingsManager::getDefaultManager();
    settings->settings->remove(ThemeManager::settingsKey());
    ThemeManager::applyAtStartup(app);

    QUndoStack stack;
    UndoService undo(&stack);
    StudioServices services;
    services.undo = &undo;

    // ---- a 15-node scene of mixed types -------------------------------------
    auto scene = iris::Scene::create();
    QVector<iris::SceneNodePtr> nodes;
    for (int i = 0; i < 5; ++i) {
        auto mesh = iris::MeshNode::create();
        mesh->setName(QStringLiteral("mesh%1").arg(i));
        mesh->setMaterial(iris::PbrMaterial::create());
        nodes.append(mesh);
    }
    for (int i = 0; i < 3; ++i) {
        auto light = iris::LightNode::create();
        light->setName(QStringLiteral("light%1").arg(i));
        nodes.append(light);
    }
    for (int i = 0; i < 2; ++i) {
        auto cam = iris::CameraNode::create();
        cam->setName(QStringLiteral("camera%1").arg(i));
        nodes.append(cam);
    }
    for (int i = 0; i < 3; ++i) {
        auto empty = iris::SceneNode::create();
        empty->setName(QStringLiteral("empty%1").arg(i));
        nodes.append(empty);
    }
    for (int i = 0; i < 2; ++i) {
        auto ps = iris::ParticleSystemNode::create();
        ps->setName(QStringLiteral("particles%1").arg(i));
        nodes.append(ps);
    }
    for (const auto &n : nodes) scene->getRootNode()->addChild(n);
    CHECK(nodes.size() == 15, "scene: 15 nodes of five types");

    // ---- the panel, inside a scroll area, the way the dock hosts it ----------
    QWidget host;
    host.setLayout(new QVBoxLayout);
    auto *scroll = new QScrollArea(&host);
    host.layout()->addWidget(scroll);
    auto *panel = new SceneNodePropertiesWidget;
    panel->setServices(&services);
    panel->setDatabase(nullptr);
    panel->setProject(nullptr);
    panel->setScene(scene);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    host.resize(400, 800);
    host.show();

    // A focus frame attaches one event-loop turn after its widget's first
    // PAINT, and the offscreen platform never paints on its own — so a turn is
    // a grab plus a drained queue, which is also what a real click costs.
    auto turn = [&]() {
        host.grab();
        QApplication::processEvents();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    for (int i = 0; i < 4; ++i) turn();

    // PIXELS, TAKE ONE: the panel showing the FIRST mesh selection of the
    // session, before anything has been switched. Hiding blades instead of
    // orphaning them is a lifetime change and must not be a LOOK change, and
    // this is the grab where the two builds are otherwise in identical states.
    panel->setSceneNode(nodes[0]);
    for (int i = 0; i < 4; ++i) turn();
    host.grab().toImage().save(QStringLiteral("selection-panel-first-mesh.png"));
    std::printf("  first mesh selection: panel %dx%d, minimumSizeHint %dx%d\n",
                panel->width(), panel->height(),
                panel->minimumSizeHint().width(), panel->minimumSizeHint().height());

    ParentChangeCounter counter;
    app.installEventFilter(&counter);

    const int kSwitches = 200;
    QVector<double> switchMs(kSwitches, 0.0);   // setSceneNode() alone
    QVector<double> turnMs(kSwitches, 0.0);     // the whole turn: select, settle, paint

    auto population = [&]() { return panel->findChildren<QObject *>().size(); };
    auto frames = [&]() { return host.findChildren<QFocusFrame *>().size(); };

    int warmObjects = 0, warmFrames = 0, warmWidgets = 0;

    QElapsedTimer timer;
    for (int i = 0; i < kSwitches; ++i) {
        const auto &node = nodes[i % nodes.size()];

        if (i == kSwitches - 50) {
            // The last 50 switches carry the strict assertions. THE BLADES are
            // the panel's permanent direct children — the widgets a selection
            // change mounts and unmounts, and the ones that used to be
            // orphaned and re-adopted twice per click.
            counter.blades.clear();
            for (QObject *o : panel->children())
                if (qobject_cast<QWidget *>(o)) counter.blades.insert(o);
            counter.reset();
        }

        timer.start();
        panel->setSceneNode(node);
        switchMs[i] = timer.nsecsElapsed() / 1e6;
        turn();
        turnMs[i] = timer.nsecsElapsed() / 1e6;

        // The population baseline is taken once every node TYPE has been shown
        // (and its blade built) — the first 15 switches are construction, not
        // steady state.
        if (i == 19) {
            warmObjects = population();
            warmFrames = frames();
            warmWidgets = QApplication::allWidgets().size();
        }
    }
    app.removeEventFilter(&counter);

    const double firstAvg = average(turnMs, 0, 50);
    const double lastAvg = average(turnMs, kSwitches - 50, 50);
    const double firstSwitchAvg = average(switchMs, 0, 50);
    const double lastSwitchAvg = average(switchMs, kSwitches - 50, 50);
    double total = 0;
    for (double v : turnMs) total += v;

    const int endObjects = population();
    const int endFrames = frames();
    const int endWidgets = QApplication::allWidgets().size();

    std::printf("\n--- selection cost over %d switches (15 nodes, 5 types) ---\n", kSwitches);
    std::printf("  turn  (select + settle + paint): first50 avg %.2f ms, last50 avg %.2f ms"
                "  (median %.2f -> %.2f), ratio %.2fx\n",
                firstAvg, lastAvg, median(turnMs, 0, 50), median(turnMs, kSwitches - 50, 50),
                firstAvg > 0 ? lastAvg / firstAvg : 0.0);
    std::printf("  setSceneNode() alone:            first50 avg %.2f ms, last50 avg %.2f ms, ratio %.2fx\n",
                firstSwitchAvg, lastSwitchAvg,
                firstSwitchAvg > 0 ? lastSwitchAvg / firstSwitchAvg : 0.0);
    std::printf("  total %.0f ms\n", total);
    std::printf("  population: panel objects %d -> %d, focus frames %d -> %d, "
                "app widgets %d -> %d (baseline taken at switch 20)\n",
                warmObjects, endObjects, warmFrames, endFrames, warmWidgets, endWidgets);
    std::printf("  ParentChange over the last 50 switches: %d total, %d on the %d reused blades, "
                "%d focus-frame RE-derivations\n\n",
                counter.total, counter.onBlades, counter.blades.size(), counter.frameReDerivations);

    // ---- the assertions -----------------------------------------------------
    CHECK(lastAvg <= 1.5 * firstAvg,
          "cost: the last 50 switches cost no more than 1.5x the first 50");
    // A generous ceiling, ~10x the measured cost of a healthy run on the
    // development box: this catches a switch that became slow OUTRIGHT (rather
    // than progressively) without turning into a machine-speed assertion.
    CHECK(total < 60000.0, "cost: 200 switches finish well inside a minute");

    CHECK(endObjects <= warmObjects,
          "growth: the panel's object population does not grow across 200 switches");
    CHECK(endFrames <= warmFrames,
          "growth: the number of live focus frames does not grow");
    CHECK(endWidgets <= warmWidgets,
          "growth: the process's widget population does not grow");

    if (counter.onBlades != 0) {
        for (auto it = counter.byClass.cbegin(); it != counter.byClass.cend(); ++it)
            std::printf("      blade ParentChange: %s x%d\n", qPrintable(it.key()), it.value());
    }
    CHECK(counter.onBlades == 0,
          "storm: a selection change sends NO ParentChange to the blades it reuses");
    CHECK(counter.frameReDerivations == 0,
          "storm: no focus frame is re-derived by a selection change");

    // ...and the panel still WORKS: the right blades are on screen for the node
    // that is selected. (Cheap end-to-end sanity, not a UI spec — that is
    // ui.material_panel's and ui.rayon_panel's job.)
    panel->setSceneNode(nodes[0]);           // a mesh
    turn();
    int visible = 0;
    for (QObject *o : panel->children()) {
        auto *w = qobject_cast<QWidget *>(o);
        if (w && w->isVisible()) ++visible;
    }
    CHECK(visible >= 3, "panel: a selected mesh shows its transform, mesh and material blades");
    panel->setSceneNode(nodes[5]);           // a light
    turn();
    int lightVisible = 0;
    for (QObject *o : panel->children()) {
        auto *w = qobject_cast<QWidget *>(o);
        if (w && w->isVisible()) ++lightVisible;
    }
    CHECK(lightVisible >= 2 && lightVisible < visible,
          "panel: selecting a light swaps the blades instead of stacking them");

    // PIXELS. Hiding blades instead of orphaning them is a LIFETIME change, and
    // a lifetime change must not be a LOOK change: these two grabs are the
    // panel as a user sees it for a mesh and for a light, written next to the
    // binary so a before/after run can be compared byte for byte (they are
    // identical across the fix — verified by sha256 on 2026-09-08).
    panel->setSceneNode(nodes[0]);
    turn();
    const bool meshShot = host.grab().toImage().save(QStringLiteral("selection-panel-mesh.png"));
    std::printf("  after 200 switches:   panel %dx%d, minimumSizeHint %dx%d\n",
                panel->width(), panel->height(),
                panel->minimumSizeHint().width(), panel->minimumSizeHint().height());
    panel->setSceneNode(nodes[5]);
    turn();
    const bool lightShot = host.grab().toImage().save(QStringLiteral("selection-panel-light.png"));
    CHECK(meshShot && lightShot, "pixels: the panel grabs of a mesh and a light are written out");

    host.hide();
    std::printf(failures == 0 ? "\nui.selection_cost: PASS\n" : "\nui.selection_cost: %d FAILURE(S)\n",
                failures);
    return failures == 0 ? 0 : 1;
}
