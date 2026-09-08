/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.properties_width — THE PROPERTIES PANEL FITS ITS DOCK, at every width the
// dock can have (owner report 2026-09-08: "the settings run off the right side
// of the panel and I can't see some of them").
//
// The dock hosts the panel in a QScrollArea with widgetResizable(true) and NO
// horizontal scrollbar (shell/mainwindow.cpp setupDockWidgets). With that
// combination a widget whose minimumSizeHint is wider than the viewport is not
// scrolled and not shrunk — it is CLIPPED, silently, on the right. Every row
// that cannot shrink below the dock's width therefore loses its right-hand end:
// the value the row exists to show.
//
// So the assertions are about MINIMUM WIDTH, not about looks:
//   * the panel's minimumSizeHint fits the viewport at the narrowest width the
//     dock allows (PanelMetrics::rightColumnMinWidth), and at the default and a
//     wide one,
//   * with the panel laid out at that width, EVERY visible descendant's right
//     edge is inside the viewport — the pixel-level form of the same claim, and
//   * the right column's two panels share ONE width
//     (PanelMetrics::rightColumnWidth is what the presets panel below is sized
//     from too, owner refinement: "make the presets right column the same width
//     as the presets panel on the main screen").
//
// Every selection the panel has a shape for is measured: the world root (the
// blades the owner was looking at — Rayon, Post Process, Shadows), a mesh with
// a material, a light, a camera, a particle emitter and an empty.
//
// The real widget, the real blades, the real Qlementine style (its metrics are
// part of every row's minimum) — the ui.selection_cost shape. Offscreen QPA.

#include <QApplication>
#include <QComboBox>
#include <QFocusFrame>
#include <QEvent>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
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

#include "data/project.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/style/panelmetrics.h"
#include "ui/style/thememanager.h"
#include "data/settingsmanager.h"

#include <QUndoStack>

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, name) do { \
    if (cond) { std::printf("ok:   %s\n", name); } \
    else { std::printf("FAIL: %s\n", name); ++failures; } \
} while (0)

namespace {

/// A widget's identity in the report: the class, the object name, and the text
/// if it carries any (which is what makes an offending row recognisable).
QString describe(const QWidget *w)
{
    QString s = QString::fromLatin1(w->metaObject()->className());
    if (!w->objectName().isEmpty()) s += "#" + w->objectName();
    if (auto *l = qobject_cast<const QLabel *>(w)) {
        if (!l->text().isEmpty()) s += " \"" + l->text().left(40) + "\"";
    } else if (auto *c = qobject_cast<const QComboBox *>(w)) {
        if (!c->currentText().isEmpty()) s += " [" + c->currentText().left(40) + "]";
    }
    return s;
}

/// The blade a widget belongs to — the section title the owner sees.
QString bladeOf(const QWidget *panel, const QWidget *w)
{
    const QWidget *cur = w;
    while (cur && cur->parentWidget() != panel) cur = cur->parentWidget();
    if (!cur) return QStringLiteral("(panel)");
    for (const QLabel *l : cur->findChildren<const QLabel *>(QStringLiteral("content_title")))
        return l->text();
    return QString::fromLatin1(cur->metaObject()->className());
}

struct Offender {
    QString blade, what;
    int minWidth = 0;
};

/// Every widget whose own minimumSizeHint/minimumWidth is what forces the panel
/// wide: walked top-down, and a container is only blamed when none of its
/// children is (the leaf that cannot shrink is the row to fix).
void collectMinimumOffenders(const QWidget *panel, const QWidget *w, int budget,
                             QVector<Offender> &out)
{
    if (!w->isVisibleTo(panel) && w != panel) return;
    for (QObject *o : w->children()) {
        auto *c = qobject_cast<QWidget *>(o);
        if (!c || c->isWindow()) continue;
        collectMinimumOffenders(panel, c, budget, out);
    }
    const int own = std::max(w->minimumWidth(),
                             w->sizePolicy().horizontalPolicy() == QSizePolicy::Ignored
                                 ? 0 : w->minimumSizeHint().width());
    // Reported from HALF the budget up, not only over it: the row that has to
    // shrink next is the useful number, and the widest LEAF is what a fix has
    // to attack (its containers just add it up).
    if (own > budget / 2 && w != panel)
        out.append({ bladeOf(panel, w), describe(w), own });
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-properties-width-ogre.log");
    if (!graph.require()) return 1;

    // The style is part of the measurement: Qlementine's spin box, combo box
    // and check box metrics are inside every row's minimum width.
    auto *settings = SettingsManager::getDefaultManager();
    settings->settings->remove(ThemeManager::settingsKey());
    ThemeManager::applyAtStartup(app);

    QUndoStack stack;
    UndoService undo(&stack);
    StudioServices services;
    services.undo = &undo;

    Project project;
    project.setProjectGuid(QStringLiteral("ui-properties-width"));

    auto scene = iris::Scene::create();
    auto mesh = iris::MeshNode::create();
    mesh->setName(QStringLiteral("mesh"));
    mesh->setMaterial(iris::PbrMaterial::create());
    auto light = iris::LightNode::create();
    light->setName(QStringLiteral("light"));
    auto camera = iris::CameraNode::create();
    camera->setName(QStringLiteral("camera"));
    auto emitter = iris::ParticleSystemNode::create();
    emitter->setName(QStringLiteral("emitter"));
    auto empty = iris::SceneNode::create();
    empty->setName(QStringLiteral("empty"));
    for (const auto &n : { mesh.staticCast<iris::SceneNode>(),
                           light.staticCast<iris::SceneNode>(),
                           camera.staticCast<iris::SceneNode>(),
                           emitter.staticCast<iris::SceneNode>(), empty })
        scene->getRootNode()->addChild(n);

    // The dock, verbatim: shell/mainwindow.cpp setupDockWidgets().
    QWidget host;
    auto *hostLayout = new QVBoxLayout(&host);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(&host);
    scroll->setMinimumWidth(PanelMetrics::rightColumnMinWidth);
    scroll->setStyleSheet("border: 0");
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    auto *panel = new SceneNodePropertiesWidget;
    panel->setServices(&services);
    panel->setDatabase(nullptr);
    // A REAL Project, unlike ui.selection_cost's null one: the WORLD selection
    // is the whole point of this suite, and WorldPropertyWidget::setScene reads
    // project->getProjectGuid() to fill its ambient-music row (it segfaults on
    // a null project — which is why no existing suite ever selected the root).
    panel->setProject(&project);
    panel->setScene(scene);
    panel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    hostLayout->addWidget(scroll);
    host.resize(PanelMetrics::rightColumnWidth, 900);
    host.show();

    auto turn = [&]() {
        host.grab();
        QApplication::processEvents();
        QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    for (int i = 0; i < 3; ++i) turn();

    struct Sel { const char *name; iris::SceneNodePtr node; };
    const QVector<Sel> selections = {
        { "world (root)", scene->getRootNode() },
        { "mesh",         mesh },
        { "light",        light },
        { "camera",       camera },
        { "emitter",      emitter },
        { "empty",        empty },
    };

    // The three widths of the brief: the narrowest the dock allows, the default
    // the shell gives the right column, and a user-widened one.
    const QVector<int> widths = { PanelMetrics::rightColumnMinWidth,
                                  PanelMetrics::rightColumnWidth, 450 };

    bool allFit = true, allInside = true, allTracked = true;
    // NOTHING THAT USED TO BE READABLE MAY BE LOST: a name that had to elide
    // still carries its full text as a tooltip (RowFit's ElideFilter), so the
    // fix costs a hover, never the information.
    bool allElidedNamed = true;
    int elidedSeen = 0;
    for (int w : widths) {
        host.resize(w, 900);
        for (int i = 0; i < 2; ++i) turn();

        for (const Sel &sel : selections) {
            panel->setSceneNode(sel.node);
            for (int i = 0; i < 2; ++i) turn();
            // MEASURED PER SELECTION, not once per width: a taller panel raises
            // the VERTICAL scrollbar, which takes 12 px off the viewport. The
            // budget is whatever the panel actually has at this moment.
            const int viewport = scroll->viewport()->width();

            const int minW = panel->minimumSizeHint().width();
            const bool fits = minW <= viewport;
            const bool tracks = panel->width() == viewport;
            if (!fits) allFit = false;
            if (!tracks) allTracked = false;

            // Pixel-level: nothing the user can see may end past the viewport.
            int worstRight = 0;
            QString worst;
            for (QWidget *c : panel->findChildren<QWidget *>()) {
                if (!c->isVisible() || c->isWindow()) continue;
                // A QFocusFrame is a decoration Qt parks AROUND a widget (it
                // overhangs by a pixel or two by design) and it is not a row —
                // it is drawn by the style, not by a panel.
                if (qobject_cast<QFocusFrame *>(c)) continue;
                const int right = c->mapTo(panel, QPoint(0, 0)).x() + c->width();
                if (right > worstRight) { worstRight = right; worst = describe(c); }
            }
            if (worstRight > viewport) allInside = false;

            for (QLabel *l : panel->findChildren<QLabel *>()) {
                if (!l->isVisible() || !l->text().contains(QChar(0x2026))) continue;
                ++elidedSeen;
                const QString head = l->text().left(l->text().indexOf(QChar(0x2026)));
                if (l->toolTip().isEmpty() || !l->toolTip().startsWith(head)) {
                    allElidedNamed = false;
                    std::printf("        ELIDED WITHOUT ITS FULL TEXT: \"%s\" tooltip \"%s\"\n",
                                qPrintable(l->text()), qPrintable(l->toolTip()));
                }
            }

            std::printf("  %-14s @ dock %3d (viewport %3d): panel %3d, minimumSizeHint %3d%s"
                        "  rightmost edge %3d%s\n",
                        sel.name, w, viewport, panel->width(), minW,
                        fits ? "" : "  << OVERFLOWS",
                        worstRight, worstRight > viewport
                            ? qPrintable("  << CLIPPED: " + worst) : "");

            if (!fits || worstRight > viewport) {
                QVector<Offender> offenders;
                collectMinimumOffenders(panel, panel, viewport, offenders);
                std::sort(offenders.begin(), offenders.end(),
                          [](const Offender &a, const Offender &b) { return a.minWidth > b.minWidth; });
                for (int i = 0; i < offenders.size() && i < 8; ++i)
                    std::printf("        forces %4d px  [%s]  %s\n", offenders[i].minWidth,
                                qPrintable(offenders[i].blade), qPrintable(offenders[i].what));
            }
        }
        std::printf("\n");
    }

    CHECK(allFit, "width: every selection's minimumSizeHint fits the dock viewport at 300/396/450");
    CHECK(allTracked, "width: the panel is laid out at exactly the viewport width (no horizontal scroll)");
    CHECK(allInside, "width: no visible row's right edge falls outside the viewport");
    std::printf("  (%d elided label states seen)\n", elidedSeen);
    CHECK(allElidedNamed,
          "readability: every elided name still carries its full text as a tooltip");

    // The right column is ONE column: the presets panel under the properties
    // dock is sized from the same number (shell/mainwindow.cpp).
    CHECK(PanelMetrics::rightColumnWidth >= PanelMetrics::rightColumnMinWidth,
          "column: the default right-column width is at least its minimum");
    CHECK(PanelMetrics::presetsPanelWidth == PanelMetrics::rightColumnWidth,
          "column: the presets panel and the properties dock share one width");

    std::printf("\n%s\n", failures == 0 ? "ui.properties_width PASSED" : "ui.properties_width FAILED");
    return failures == 0 ? 0 : 1;
}
