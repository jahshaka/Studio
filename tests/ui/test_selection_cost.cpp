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
// (`clearLayout()` itself no longer exists: SELECT-COST-1 retired it with the
// take-everything-off shape it belonged to — a mount moves only the blades
// that differ now, applyMountDiff. The two defects above are still what these
// assertions pin, because the property they protect is the same one.)
//
// So the assertions are: per-switch wall time stays FLAT (the last 50 switches
// cost no more than 1.5x the first 50), the population of live widgets and
// focus frames does not grow across 200 switches, a switch sends ZERO
// QEvent::ParentChange to the widgets it reuses — which is also, exactly, why
// the focus frames stop being re-derived — and, since SELECT-COST-1, a pick
// fits inside a 90 Hz FRAME in absolute milliseconds at 16, 1,000 and 10,000
// nodes, attaching no blades at all when the set has not changed.
//
// The real SceneNodePropertiesWidget, the real property blades, the real
// Qlementine style (without it there are no focus frames and no amplifier at
// all) and a real 15-node scene of mixed types. Offscreen QPA; the widgets are
// painted with grab() every turn, because a focus frame only attaches after its
// widget's FIRST PAINT.

#include <QApplication>
#include "ui/controls/accordionbladewidget.h"
#include <QElapsedTimer>
#include <QEvent>
#include <QFocusFrame>
#include <QScrollArea>
#include <QDockWidget>
#include <QFrame>
#include <QTabBar>
#include <QLabel>
#include <QMainWindow>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <array>
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
        //
        // AT 50, NOT AT 20 (2026-09-12, lane R1): construction is not finished
        // when the last blade has been shown. A qlementine focus frame — and
        // the per-widget WidgetWithFocusFrameEventFilter behind it — attaches
        // ONE EVENT-LOOP TURN AFTER ITS WIDGET'S FIRST PAINT (the comment on
        // `turn` above), so the tail of that construction is paced by paints
        // rather than by switches, and on a loaded box a handful of rows were
        // still to arrive at switch 20. That sampled a baseline five objects
        // short of the steady state and read as growth (a full 317-suite gate,
        // -j4: 3682 -> 3687, where the same binary run alone reports
        // 3687 -> 3687 and every other run of the day agreed).
        //
        // This is not a weaker assertion: it is the same "no growth in steady
        // state", measured over the last 150 switches instead of the last 180,
        // with the baseline taken after the lazy construction has settled.
        if (i == 49) {
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
                "app widgets %d -> %d (baseline taken at switch 50)\n",
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

    // ---- THE SAME 200 SWITCHES UNDER A LIVE FILTER ------------------------
    // Every mount re-applies the property filter (PROPERTY_FILTER_SPEC §6.1),
    // which walks the registry's entries for the mounted blades — ~250 rows at
    // the widest selection — and hides or shows each one. That is per PICK, so
    // it belongs in this suite's budget rather than in a micro-benchmark: a
    // filtered column must not make clicking around the scene slower in kind.
    {
        panel->setPropertiesFilter(SceneNodePropertiesWidget::Tab::Selection,
                                   QStringLiteral("sha"));   // matches a few rows everywhere
        turn();
        QVector<double> filteredMs(kSwitches, 0.0);
        QElapsedTimer filteredTimer;
        for (int i = 0; i < kSwitches; ++i) {
            filteredTimer.start();
            panel->setSceneNode(nodes[i % nodes.size()]);
            filteredMs[i] = filteredTimer.nsecsElapsed() / 1e6;
            turn();
        }
        const double filteredFirst = average(filteredMs, 0, 50);
        const double filteredLast = average(filteredMs, kSwitches - 50, 50);
        double filteredTotal = 0;
        for (double v : filteredMs) filteredTotal += v;
        std::printf("  UNDER A LIVE FILTER:             first50 avg %.2f ms, last50 avg %.2f ms, "
                    "total %.0f ms (unfiltered setSceneNode last50 %.2f ms)\n",
                    filteredFirst, filteredLast, filteredTotal, lastSwitchAvg);

        CHECK(filteredLast <= 1.5 * filteredFirst,
              "cost: under a live filter the last 50 switches cost no more than 1.5x the first 50");
        CHECK(filteredTotal < 60000.0,
              "cost: 200 switches under a live filter finish well inside a minute");
        // The filter is a walk and a hide, not a rebuild: a pick with one live
        // must stay the same ORDER of work as a pick without one. (Generous, so
        // this is a shape assertion and not a machine-speed one.)
        CHECK(filteredLast <= qMax(3.0, 4.0 * lastSwitchAvg),
              QStringLiteral("cost: a filtered pick stays in the same order as an unfiltered "
                             "one (%1 ms vs %2 ms)").arg(filteredLast, 0, 'f', 2)
                  .arg(lastSwitchAvg, 0, 'f', 2).toUtf8().constData());
        panel->setPropertiesFilter(SceneNodePropertiesWidget::Tab::Selection, QString());
        turn();
    }

    // ...and the panel still WORKS: the right blades are on screen for the node
    // that is selected. (Cheap end-to-end sanity, not a UI spec — that is
    // ui.material_panel's and ui.photon_panel's job.)
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

    // ---- THE SCRIPT SHAPE: rebuilds with NO event-loop turn ----------------
    //
    // Everything above turns the event loop between switches, and that is what
    // an interactive session does. A SCRIPT does not: a `--script` or MCP run is
    // one call that never yields, so every deleteLater() it produces stays in
    // Qt's global posted-event list for the whole run.
    //
    // AccordianBladeWidget::clearPanel retires its rows with deleteLater()
    // alone, and that made a scripted scene build QUADRATIC (MIRROR_SCALE lane,
    // 2026-09-13): each rebuild left ~100 rows alive as children of the blade
    // AND ~100 DeferredDelete events in the posted list, and Qt scans that list
    // on every widget construction (compressEvent) and every widget destruction
    // (removePostedEvents). Measured through the app: scene.addPrimitive in a
    // loop cost 92 ms per cube at 55 nodes and 2,076 ms at 405, and the process
    // then spent MINUTES in ~MainWindow. Fixed, the same run is 57 -> 92 ms.
    //
    // The assertion is a SHAPE, not a millisecond budget: the retired-row
    // population is bounded however many rebuilds happen without a turn.
    {
        // A REBUILD NEEDS A SHAPE CHANGE (ADD-1). Showing another mesh's
        // material is a REFILL now — the same rows, pointed at another property
        // list — and a refill retires nothing, which is most of why a pick got
        // cheap. The thing this case is about is the REBUILD, so it drives the
        // one a user really produces: a mesh whose material is gone (a
        // material that failed to load, a node built without one) has no rows
        // at all, so every switch between it and a normal mesh rebuilds.
        auto bareMesh = iris::MeshNode::create();
        bareMesh->setName(QStringLiteral("mesh-without-material"));
        scene->getRootNode()->addChild(bareMesh);
        const QVector<iris::SceneNodePtr> rebuilders = {
            nodes[0], bareMesh.staticCast<iris::SceneNode>()
        };

        panel->setSceneNode(nodes[0]);       // a mesh: the material blade is up
        turn();
        auto retiredRows = [&]() {
            int n = 0;
            for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
                n += b->retiredRowCount();
            return n;
        };
        const int afterOne = retiredRows();
        QElapsedTimer noTurn; noTurn.start();
        // NO turn() in this loop, on purpose. 60 rebuilds is what a script
        // adding 30 primitives produces (each add rebuilds the blade twice).
        //
        // flushPendingMount() after each: a selection only RAISES a mount debt
        // now (ADD-1), and the debt is one per turn — so without this the loop
        // would produce ONE rebuild and prove nothing about the ring. The
        // subject here is the rebuild, so the rebuilds are driven explicitly;
        // the coalescing has its own case below.
        for (int i = 0; i < 60; ++i) { panel->setSceneNode(rebuilders[i % 2]); panel->flushPendingMount(); }
        const double noTurnMs = double(noTurn.elapsed());
        const int after60 = retiredRows();
        // ...and THREE TIMES AS MANY, which is what makes this a shape and not
        // a magic number: the retired population is bounded by the generation
        // ring (AccordianBladeWidget::kRetiredGenerations), so it must not
        // depend on how many rebuilds happened. Before the fix it was one
        // generation PER REBUILD — 180 rows at 60 rebuilds, 540 at 180.
        for (int i = 0; i < 120; ++i) { panel->setSceneNode(rebuilders[i % 2]); panel->flushPendingMount(); }
        const int after180 = retiredRows();
        std::printf("  no-event-loop rebuilds: retired rows %d after 1, %d after 60, %d after 180"
                    "  (%.0f ms for the first 60)\n", afterOne, after60, after180, noTurnMs);
        CHECK(after180 <= after60,
              "script shape: the retired population does not grow with the NUMBER of rebuilds");
        // A ceiling too, so "bounded" cannot be satisfied by a constant leak of
        // thousands: at most one generation ring of one blade's rows.
        CHECK(after180 <= AccordianBladeWidget::kRetiredGenerations * 8,
              "script shape: ...and it is one generation ring of rows, not a pile");
        const int afterMany = after180;
        turn();
        CHECK(retiredRows() <= afterMany,
              "script shape: ...and a turn of the loop does not leave more behind");
        panel->setSceneNode(nodes[0]);
        turn();

        // ---- THE HEADROOM, DRIVEN AT ITS BOUNDARY (lead review F3) --------
        //
        // The retired generations are freed by CALL COUNT, not by anything Qt
        // enforces, so the safety property is a number: a row may drive
        // AccordianBladeWidget::kRetiredGenerations rebuilds from inside its
        // OWN slot before its memory is reclaimed under it. Today's measured
        // maximum from a row's own signal is ONE, so three is two of slack.
        // This drives the promise exactly, at its boundary.
        //
        // The vehicle is a real Qt signal of a real live ROW —
        // objectNameChanged, which every QObject has — so the rebuilds happen
        // INSIDE the row's own emission, with the row on the stack, and
        // setObjectName goes on touching the row after the lambda returns.
        // Under ASan (the gate's configuration) a generation too few is a
        // use-after-free here instead of a mystery crash in a session.
        {
            // FINDING A REAL ROW, and not a piece of the blade's own
            // scaffolding — which is what any "pick a visible child" heuristic
            // lands on, and which is never retired, so the whole case would
            // pass while testing nothing. Rows are identified by what happens
            // TO them: a rebuild HIDES the generation it retires and leaves the
            // scaffolding visible, so the widgets that were visible before a
            // rebuild and are hidden after it are exactly the retired rows.
            //
            // Driving from an already-retired row is not a contrivance either —
            // it is the real shape. A row that asks the panel to rebuild is
            // retired by the rebuild it asked for, and everything it does after
            // that call returns, it does while retired.
            auto deepRows = [&]() {
                QList<QWidget *> out;
                for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
                    for (QWidget *w : b->findChildren<QWidget *>())
                        if (!qobject_cast<AccordianBladeWidget *>(w)) out.append(w);
                return out;
            };
            panel->setSceneNode(rebuilders[0]);
            turn();
            QList<QPointer<QWidget>> wereVisible;
            for (QWidget *w : deepRows()) if (w->isVisible()) wereVisible.append(w);
            panel->setSceneNode(rebuilders[1]);     // shift 1: they are retired now
            panel->flushPendingMount();
            QPointer<QWidget> victim;
            for (const QPointer<QWidget> &w : wereVisible)
                if (w && w->isHidden()) { victim = w; break; }
            CHECK(!victim.isNull(), "headroom: a row that a rebuild really RETIRED was found");

            if (victim) {
                int drove = 0;
                auto conn = QObject::connect(victim.data(), &QObject::objectNameChanged,
                                             victim.data(), [&](const QString &) {
                    // One shift is already spent (the rebuild that retired it),
                    // so the promise leaves kRetiredGenerations - 1 here.
                    for (int i = 0; i < AccordianBladeWidget::kRetiredGenerations - 1; ++i) {
                        panel->setSceneNode(rebuilders[i % 2]);
                        panel->flushPendingMount();   // drive a real rebuild (ADD-1)
                        ++drove;
                    }
                });
                victim->setObjectName(QStringLiteral("selection-cost-headroom-probe"));
                QObject::disconnect(conn);
                // The row is on the stack here: reading it is what a real slot
                // does after asking the panel to rebuild.
                const bool stillThere = !victim.isNull();
                const QString readBack = stillThere ? victim->objectName() : QString();
                CHECK(drove == AccordianBladeWidget::kRetiredGenerations - 1,
                      "headroom: the retired row's own slot drove the promised rebuilds");
                CHECK(readBack == QStringLiteral("selection-cost-headroom-probe"),
                      "headroom: ...and it is still readable afterwards (no use-after-free)");
            }
            panel->setSceneNode(nodes[0]);
            turn();
        }
    }

    // ---- THE COALESCED MOUNT (ADD-1) --------------------------------------
    //
    // WHAT IT COSTS TO ADD AN OBJECT. `scene.addPrimitive` selects the node it
    // just made (AddSceneNodeCommand::redo -> SelectionService::select), so a
    // script adding 64 spheres rebuilt this column 64 times — 44 ms of each
    // add's 50 — and only the last of the 64 was ever seen by anybody. A
    // selection raises a mount DEBT now, settled once at the end of the turn.
    //
    // The assertion is the mechanism, not a millisecond: 64 selections inside
    // ONE turn of the event loop mount the column ONCE, and what it mounts is
    // the LAST selection, not the first.
    {
        panel->setSceneNode(nodes[0]);
        turn();
        const int before = panel->mountCount();
        for (int i = 0; i < 64; ++i) panel->setSceneNode(nodes[i % nodes.size()]);
        const int during = panel->mountCount() - before;
        CHECK(during == 0,
              QStringLiteral("coalesce: 64 selections in one turn mount NOTHING while the turn "
                             "lasts (%1)").arg(during).toUtf8().constData());
        CHECK(panel->mountIsPending(), "coalesce: ...the column knows a mount is owed");
        turn();
        const int after = panel->mountCount() - before;
        CHECK(after == 1,
              QStringLiteral("coalesce: ...and the turn ends with exactly ONE mount (%1)")
                  .arg(after).toUtf8().constData());
        CHECK(!panel->mountIsPending(), "coalesce: ...with no debt left over");

        // THE LAST SELECTION IS THE ONE MOUNTED. nodes[5] is a light and
        // nodes[0] a mesh, so the blade set says which one won.
        panel->setSceneNode(nodes[0]);            // a mesh
        panel->setSceneNode(nodes[5]);            // ...then a light, same turn
        turn();
        bool lightSection = false, materialSection = false;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>()) {
            if (!b->isVisibleTo(panel)) continue;
            if (b->panelTitle() == QStringLiteral("Light")) lightSection = true;
            if (b->panelTitle() == QStringLiteral("Material")) materialSection = true;
        }
        CHECK(lightSection && !materialSection,
              "coalesce: the LAST selection of the turn is the one mounted");

        // A QUESTION PAYS THE DEBT. Nothing may ever read a column that has not
        // been built for the selection it claims to describe.
        panel->setSceneNode(nodes[0]);            // a mesh, debt owed
        const auto listing = panel->propertyRows(SceneNodePropertiesWidget::Tab::Selection);
        CHECK(!panel->mountIsPending() && !listing.isEmpty(),
              "coalesce: asking what the column holds settles the owed mount first");
        turn();
    }

    // ---- A COLUMN NOBODY CAN SEE BUILDS NOTHING (ADD-1) -------------------
    //
    // With the properties dock CLOSED a scripted add still cost 32 ms of its 50:
    // the panel built its whole blade column into a widget tree that was not on
    // screen. Visibility is the SECOND input (the two-inputs law, as in
    // PROPERTY_FILTER_SPEC): the panel's own reason is "the selection moved",
    // the dock's is "is anyone looking", and the debt is owed to whichever comes
    // last — the showEvent, or the first question asked.
    {
        panel->setSceneNode(nodes[0]);
        turn();
        scroll->hide();                       // the dock closes
        turn();
        const int before = panel->mountCount();
        for (int i = 0; i < 20; ++i) { panel->setSceneNode(nodes[i % nodes.size()]); turn(); }
        CHECK(panel->mountCount() == before,
              QStringLiteral("hidden: 20 selections with the column hidden mount NOTHING (%1)")
                  .arg(panel->mountCount() - before).toUtf8().constData());
        scroll->show();                       // the dock opens again
        turn();
        CHECK(panel->mountCount() == before + 1,
              QStringLiteral("hidden: ...and showing it mounts exactly once, for the LAST "
                             "selection (%1)").arg(panel->mountCount() - before)
                  .toUtf8().constData());
        // ...and what it mounted is the last selection's blade set, not the
        // first's: nodes[19 % 15] == nodes[4] is a mesh.
        bool material = false;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            if (b->isVisibleTo(panel) && b->panelTitle() == QStringLiteral("Material")) material = true;
        CHECK(material, "hidden: ...and it is the LAST selection that got mounted");

        // A QUESTION STILL GETS A TRUE ANSWER while the column is hidden: the
        // verb that lists the rows must never report an empty column just
        // because the dock is closed.
        scroll->hide();
        turn();
        panel->setSceneNode(nodes[5]);        // a light
        turn();
        const auto hiddenRows = panel->propertyRows(SceneNodePropertiesWidget::Tab::Selection);
        CHECK(!hiddenRows.isEmpty(),
              "hidden: asking what a hidden column holds builds it and answers truthfully");
        scroll->show();
        turn();
    }

    // ---- A TAB BEHIND ANOTHER TAB IS NOBODY LOOKING (TABS-HIDDEN-1) -------
    //
    // The saving above reads "can anybody see this column". `isVisible()` is
    // NOT that question: a QDockWidget that shares a tab bar with another is
    // SHOWN whichever tab is in front — Qt parks the ones behind off-screen
    // (geometry().right() < 0, MainWindow::isFrontTab, lane SPACE-2) — so a
    // Properties dock tabbed behind the Hierarchy answered true and rebuilt its
    // whole column on every selection turn, for nobody. Correct, and the 44 ms
    // -> 3 ms win simply did not apply to that arrangement.
    //
    // Qt sends the PANEL no event when its tab comes to the front (the dock
    // moves; nothing inside it does), so the raise has to be noticed as well:
    // the panel watches its own dock, and MainWindow connects the dock's
    // visibilityChanged(true) so the product fills in the same turn as the
    // click. This is the panel half — a real tabified QMainWindow, no
    // MainWindow in sight.
    {
        QMainWindow win;
        win.resize(1200, 800);
        auto *otherDock = new QDockWidget(QStringLiteral("Hierarchy"), &win);
        otherDock->setObjectName(QStringLiteral("otherDock"));
        otherDock->setWidget(new QLabel(QStringLiteral("hierarchy"), otherDock));
        auto *propsDock = new QDockWidget(QStringLiteral("Properties"), &win);
        propsDock->setObjectName(QStringLiteral("propsDock"));
        // The dock body is the shape MainWindow gives this panel: a fixed-width
        // column holding a scroll area with NO horizontal bar. (Handing the
        // scroll area to the dock directly instead leaves the column's width
        // free on both sides and QScrollArea::eventFilter recurses until the
        // stack runs out — the rig has to pin the width the way the real one
        // does, PanelMetrics::rightColumnWidth.)
        auto *body = new QWidget(propsDock);
        auto *bodyLayout = new QVBoxLayout(body);
        bodyLayout->setContentsMargins(0, 0, 0, 0);
        body->setFixedWidth(400);
        auto *dockScroll = new QScrollArea(body);
        dockScroll->setFrameShape(QFrame::NoFrame);
        dockScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        dockScroll->setWidgetResizable(true);
        // takeWidget(), not a bare reparent: a QScrollArea keeps filtering the
        // events of a widget handed to it, so leaving the panel registered with
        // BOTH areas has the two of them resizing it against each other until
        // the stack runs out (measured — 82,000 frames of
        // QScrollArea::eventFilter).
        scroll->takeWidget();
        dockScroll->setWidget(panel);              // the panel moves into the dock
        bodyLayout->addWidget(dockScroll);
        propsDock->setWidget(body);
        win.addDockWidget(Qt::RightDockWidgetArea, otherDock);
        win.addDockWidget(Qt::RightDockWidgetArea, propsDock);
        win.tabifyDockWidget(otherDock, propsDock);
        win.show();
        auto winTurn = [&]() {
            win.grab();
            QApplication::processEvents();
            QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        };
        for (int i = 0; i < 4; ++i) winTurn();

        // CLICKING A TAB is what raises one: the group's QTabBar is a private
        // child of the dock area, so the rig finds it by the tab TEXT, which is
        // the dock's title. (QWidget::raise() re-orders siblings and the dock
        // area's next layout pass simply parks the dock again.)
        auto raiseTab = [&](QDockWidget *dock) {
            for (QTabBar *bar : win.findChildren<QTabBar *>())
                for (int i = 0; i < bar->count(); ++i)
                    if (bar->tabText(i) == dock->windowTitle()) { bar->setCurrentIndex(i); return true; }
            return false;
        };
        CHECK(raiseTab(otherDock), "tabbed: the group's tab bar is there to click");
        for (int i = 0; i < 3; ++i) winTurn();
        for (int i = 0; i < 3; ++i) winTurn();
        CHECK(propsDock->isVisible() && propsDock->geometry().right() < 0,
              QStringLiteral("tabbed: a dock behind another tab is SHOWN and parked off-screen "
                             "(visible=%1 right=%2)").arg(propsDock->isVisible())
                  .arg(propsDock->geometry().right()).toUtf8().constData());
        const auto behind = panel->propertiesStats();
        CHECK(!behind.visible,
              "tabbed: ...and the column says nobody can see it, though isVisible() is true");

        const int before = panel->mountCount();
        for (int i = 0; i < 20; ++i) { panel->setSceneNode(nodes[i % nodes.size()]); winTurn(); }
        CHECK(panel->mountCount() == before,
              QStringLiteral("tabbed: 20 selections behind another tab mount NOTHING (%1)")
                  .arg(panel->mountCount() - before).toUtf8().constData());
        CHECK(panel->mountIsPending() && panel->propertiesStats().deferredHidden,
              "tabbed: ...the debt is owed to the moment the tab is raised");

        raiseTab(propsDock);                       // ...and the user clicks the tab
        for (int i = 0; i < 3; ++i) winTurn();
        CHECK(propsDock->geometry().right() >= 0, "tabbed: the raise brings the dock on screen");
        CHECK(panel->mountCount() == before + 1,
              QStringLiteral("tabbed: raising the tab mounts exactly ONCE (%1)")
                  .arg(panel->mountCount() - before).toUtf8().constData());
        // ...for the LAST selection: nodes[19 % 15] == nodes[4], a mesh.
        bool material = false;
        for (AccordianBladeWidget *b : panel->findChildren<AccordianBladeWidget *>())
            if (b->isVisibleTo(panel) && b->panelTitle() == QStringLiteral("Material")) material = true;
        CHECK(material, "tabbed: ...and it is the CURRENT node that got mounted");
        CHECK(!panel->mountIsPending(), "tabbed: ...with no debt left over");

        // THE FRONT TAB IS ORDINARY: a selection there mounts at the end of its
        // turn exactly as it does with no tab bar at all.
        const int front = panel->mountCount();
        panel->setSceneNode(nodes[5]);             // a light
        winTurn();
        CHECK(panel->mountCount() == front + 1,
              QStringLiteral("tabbed: a selection on the FRONT tab still mounts once (%1)")
                  .arg(panel->mountCount() - front).toUtf8().constData());
        // HIDING the dock while it is in front is the old rule, unchanged.
        propsDock->hide();
        winTurn();
        const int hidden = panel->mountCount();
        for (int i = 0; i < 5; ++i) { panel->setSceneNode(nodes[i]); winTurn(); }
        CHECK(panel->mountCount() == hidden,
              "tabbed: a hidden dock still builds nothing");
        propsDock->show();
        for (int i = 0; i < 3; ++i) winTurn();
        // A re-shown dock rejoins the group WHERE ITS TAB LANDS, which need not
        // be in front — so "bring it back" is show plus, if it came back behind,
        // the click. Either way the column is built exactly ONCE.
        raiseTab(propsDock);
        for (int i = 0; i < 3; ++i) winTurn();
        CHECK(panel->mountCount() == hidden + 1,
              QStringLiteral("tabbed: ...and bringing it back mounts exactly once (%1)")
                  .arg(panel->mountCount() - hidden).toUtf8().constData());

        // THE PANEL GOES HOME to the plain scroll area the rest of the suite
        // uses, so nothing below depends on the dock rig.
        dockScroll->takeWidget();
        scroll->setWidget(panel);
        win.hide();
        for (int i = 0; i < 3; ++i) turn();
    }

    // ---- WHAT A PICK COSTS, AND THE BOUND IT HAS TO FIT IN ----------------
    //
    // THE TWO PICK SHAPES (ADD-1). A SAME-TYPE pick (mesh -> mesh) can reuse
    // every row it has; a TYPE-CHANGE pick (mesh -> light) has to show a
    // different blade set. Both are measured; both are bounded.
    //
    // THE BOUND IS A FRAME (SELECT-COST-1, 2026-09-18). `vr.select()` measured
    // 16-17 ms per call and a desktop click paid the same, so a controller
    // trigger press cost MORE THAN A FRAME at 90 Hz — which is the number this
    // has to fit inside: 1000/90 = 11.11 ms. The bound below is 11.0 ms, the
    // frame, and it is the product requirement rather than a fit to this box:
    // the measured medians over five runs on the development box (load average
    // 6-8.5, two other lanes live) are 0.32-0.39 ms same-type and 1.56-3.57 ms
    // type-change, worst single switch 5.23 ms — 28x and 3.1x of headroom at
    // the worst reading, 7x at the typical one. A red here means a pick got
    // slow OUTRIGHT, which is what this lane found: before it, the same
    // measurement read 4.72 ms mean / 6.64 worst for a same-type pick and the
    // select+settle+paint turn cost 11.04 ms — one whole 90 Hz frame for a
    // click.
    //
    // AND A STRUCTURAL ONE BESIDE IT, because a millisecond bound on a shared
    // box can only ever be generous: a same-type pick must ATTACH NO BLADES.
    // The 4.2 ms it used to cost was `addWidget` + `show()` on five blades that
    // had just been taken off the layout and hidden — a full layout, style
    // polish and focus-frame pass per blade, for a set that had not changed.
    // The column moves only what differs now (applyMountDiff), so this number
    // is 0 for a pick between two meshes whatever the machine is doing.
    const double kFrameMs90 = 11.0;
    {
        auto pickCost = [&](const iris::SceneNodePtr &a, const iris::SceneNodePtr &b) {
            panel->setSceneNode(a);
            panel->flushPendingMount();
            QElapsedTimer t;
            double worst = 0, sum = 0;
            QVector<double> each;
            const int n = 40;
            for (int i = 0; i < n; ++i) {
                t.start();
                panel->setSceneNode(i % 2 ? b : a);
                panel->flushPendingMount();
                const double ms = t.nsecsElapsed() / 1e6;
                each.append(ms);
                sum += ms;
                worst = qMax(worst, ms);
            }
            std::sort(each.begin(), each.end());
            return std::array<double, 3>{ sum / n, worst, each[each.size() / 2] };
        };
        const auto sameType = pickCost(nodes[0], nodes[1]);      // mesh -> mesh
        const auto typeChange = pickCost(nodes[0], nodes[5]);    // mesh -> light
        std::printf("  PICK COST: same-type (mesh->mesh) %.2f ms mean / %.2f median / %.2f worst; "
                    "type-change (mesh->light) %.2f ms mean / %.2f median / %.2f worst\n",
                    sameType[0], sameType[2], sameType[1],
                    typeChange[0], typeChange[2], typeChange[1]);
        CHECK(sameType[2] <= kFrameMs90,
              QStringLiteral("pick: a same-type pick fits inside a 90 Hz frame (%1 ms median, "
                             "bound %2)").arg(sameType[2], 0, 'f', 2)
                  .arg(kFrameMs90, 0, 'f', 1).toUtf8().constData());
        CHECK(typeChange[2] <= kFrameMs90,
              QStringLiteral("pick: a type-change pick fits inside a 90 Hz frame (%1 ms median, "
                             "bound %2)").arg(typeChange[2], 0, 'f', 2)
                  .arg(kFrameMs90, 0, 'f', 1).toUtf8().constData());

        // THE STRUCTURAL HALF: nothing is re-shown when nothing changed.
        panel->setSceneNode(nodes[0]);
        panel->flushPendingMount();
        panel->setSceneNode(nodes[1]);              // another mesh
        panel->flushPendingMount();
        const int attachedSameType = panel->propertiesStats().attached;
        panel->setSceneNode(nodes[5]);              // a light: the set really changes
        panel->flushPendingMount();
        const int attachedTypeChange = panel->propertiesStats().attached;
        std::printf("  blades attached by a pick: same-type %d, type-change %d\n",
                    attachedSameType, attachedTypeChange);
        CHECK(attachedSameType == 0,
              QStringLiteral("pick: a same-type pick attaches NO blades — it re-points the ones "
                             "already up (%1)").arg(attachedSameType).toUtf8().constData());
        CHECK(attachedTypeChange > 0,
              "pick: ...and a type-change pick does attach the blades that differ");
        turn();
    }

    // ---- THE SAME PICK IN A 1,000- AND A 10,000-NODE SCENE -----------------
    //
    // The 16-17 ms was INDEPENDENT OF SCENE SIZE (VR-INPUT-1S measured it at
    // 1k, 5k and 10k), because this column's work is per SELECTION, not per
    // node — and that independence is a property worth pinning: a pick that
    // starts walking the document would show up here and nowhere else in this
    // suite. The nodes are plain empties added to the root: they cost the
    // panel nothing to have, which is the point.
    //
    // The 10k arm is then repeated with the column OFF SCREEN, because that is
    // the other half of the rule (a column nobody can see builds nothing) and
    // it is the arrangement a headset is in: the wearer sees the scene, not the
    // dock. That half is counted in MOUNTS, not milliseconds — see it below.
    {
        // 200 picks per reading, not 40: the independence check below divides
        // one median by another, and on a loaded box a 40-sample median of a
        // sub-millisecond quantity moves enough for that ratio to be noise
        // measuring noise. 200 picks cost ~70 ms at the measured 0.35 ms.
        auto medianPick = [&](const iris::SceneNodePtr &a, const iris::SceneNodePtr &b) {
            panel->setSceneNode(a);
            panel->flushPendingMount();
            QVector<double> each;
            QElapsedTimer t;
            for (int i = 0; i < 200; ++i) {
                t.start();
                panel->setSceneNode(i % 2 ? b : a);
                panel->flushPendingMount();
                each.append(t.nsecsElapsed() / 1e6);
            }
            std::sort(each.begin(), each.end());
            return each[each.size() / 2];
        };
        auto grow = [&](int to) {
            int have = scene->getRootNode()->children().size();
            while (have < to) {
                auto n = iris::SceneNode::create();
                n->setName(QStringLiteral("filler%1").arg(have));
                scene->getRootNode()->addChild(n);
                ++have;
            }
            return have;
        };

        const double base = medianPick(nodes[0], nodes[1]);
        const int n1k = grow(1000);
        const double at1k = medianPick(nodes[0], nodes[1]);
        const int n10k = grow(10000);
        const double at10k = medianPick(nodes[0], nodes[1]);

        std::printf("  PICK BY SCENE SIZE: %d nodes %.2f ms, %d nodes %.2f ms, %d nodes %.2f ms\n",
                    int(nodes.size()) + 1, base, n1k, at1k, n10k, at10k);
        CHECK(at1k <= kFrameMs90 && at10k <= kFrameMs90,
              QStringLiteral("size: a pick fits inside a 90 Hz frame at 1k (%1 ms) and 10k (%2 ms) "
                             "nodes").arg(at1k, 0, 'f', 2).arg(at10k, 0, 'f', 2).toUtf8().constData());
        // INDEPENDENCE, as a shape: 3x of the small-scene median (or 1 ms,
        // whichever is larger — a sub-millisecond baseline is all timer noise
        // on a loaded box) rather than a second millisecond budget.
        CHECK(at10k <= qMax(1.0, 3.0 * base),
              QStringLiteral("size: ...and it does not grow with the scene (%1 ms at 10k vs %2 ms "
                             "at %3 nodes)").arg(at10k, 0, 'f', 2).arg(base, 0, 'f', 2)
                  .arg(int(nodes.size()) + 1).toUtf8().constData());

        // ...AND A COLUMN NOBODY CAN SEE BUILDS NOTHING, AT THIS SIZE TOO —
        // measured the way the rule actually works, which `flushPendingMount()`
        // cannot show: a flush mounts unconditionally (that is its job — a
        // question always gets a true answer), and the deferral lives in
        // scheduleMount()'s `if (!onScreen()) return`. So this arm drives
        // exactly what a user does — select, let the turn end — and counts
        // MOUNTS rather than milliseconds, which is a fact about the code and
        // not about the box.
        {
            scroll->hide();                   // the dock closes: nobody is looking
            turn();
            const int before = panel->mountCount();
            QElapsedTimer t; t.start();
            for (int i = 0; i < 40; ++i) { panel->setSceneNode(nodes[i % 2]); turn(); }
            const double hiddenMs = t.elapsed() / 40.0;
            const int mountedHidden = panel->mountCount() - before;
            scroll->show();                   // ...and it opens again
            turn();
            const int mountedOnShow = panel->mountCount() - before;
            std::printf("  OFF SCREEN at %d nodes: 40 picks cost %.2f ms each (turn included), "
                        "%d mounts; showing the dock -> %d\n",
                        n10k, hiddenMs, mountedHidden, mountedOnShow);
            CHECK(mountedHidden == 0,
                  QStringLiteral("size: 40 picks at 10k nodes with the column off screen build "
                                 "NOTHING (%1 mounts)").arg(mountedHidden).toUtf8().constData());
            CHECK(mountedOnShow == 1,
                  QStringLiteral("size: ...and opening the dock builds it once, for the last "
                                 "selection (%1)").arg(mountedOnShow).toUtf8().constData());
        }
    }

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
