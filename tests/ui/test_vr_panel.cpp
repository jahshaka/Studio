/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.vr_panel — THE WORLD PANEL'S VR SECTION (lane VR-WORLD-1; the owner's
// request 2026-09-18: "add a VR World settings section where we can set that").
//
// The product ask is a UI one — a section where a project's VR settings can be
// SET — and a UI claim needs a UI test: assertions on `world.vr` alone would
// pass just as well with no section on screen at all. So this builds the REAL
// widget over a real document and counts what it put in front of the user, at
// the values the PROJECT holds; then it drives those controls the way a user
// does and asserts on the DOCUMENT and on the undo stack.
//
// It also pins the two properties that make the section honest:
//   * every row is GENERATED from services/vrworld.h — the same table
//     `world.vr` and `vr.locomotion` are generated from, so the panel cannot
//     offer a value the verbs refuse; and
//   * a row that means nothing in the current mode is GREYED, not hidden (the
//     snap step while the turn is smooth, and the smooth rate while it snaps).
//
// Offscreen QPA, no display, no rendering — the section is widgets and a
// document. ui.photon_panel is the precedent for the shape.

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QUndoStack>

#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"

#include "services/services.h"
#include "services/undoservice.h"
#include "services/vrworld.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/panels/propertywidgets/worldvrpropertywidget.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

static void pump()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/// A row by its label: the control a user touches is inside a wrapper whose
/// QLabel carries the name — ELIDED to the column's width (ui/controls/rowfit.h
/// puts "Smooth Turn Rate" on screen as "Smooth…"), so the match is on what is
/// left of the ellipsis. A test that compared the whole string would fail on
/// every long row for a reason that has nothing to do with its subject.
static bool labelIs(const QString &shown, const QString &label)
{
    const QString head = shown.section(QChar(0x2026), 0, 0);
    return !head.isEmpty() && label.startsWith(head);
}

static ComboBoxWidget *comboWith(QWidget *w, const QString &label)
{
    for (ComboBoxWidget *c : w->findChildren<ComboBoxWidget *>())
        for (QLabel *l : c->findChildren<QLabel *>())
            if (labelIs(l->text(), label)) return c;
    return nullptr;
}

static DragFloatWidget *dragWith(QWidget *w, const QString &label)
{
    for (DragFloatWidget *d : w->findChildren<DragFloatWidget *>())
        for (QLabel *l : d->findChildren<QLabel *>())
            if (labelIs(l->text(), label)) return d;
    return nullptr;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-vr-panel-ogre.log");
    if (!graph.require()) return 1;

    QUndoStack stack;
    UndoService undo(&stack);
    StudioServices services;
    services.undo = &undo;

    auto scene = iris::Scene::create();

    // ---- 0. THE DOCUMENT'S DEFAULTS ARE THE ONES SHIPPED -------------------
    CHECK(qFuzzyCompare(scene->vrFlySpeed, iris::kDefaultVrFlySpeed) &&
              qFuzzyCompare(scene->vrFlySpeed, 15.0f),
          "a new scene flies at 15 m/s (iris::kDefaultVrFlySpeed, the one definition)");
    CHECK(scene->vrFlyMode == iris::VrFlyMode::Aim && scene->vrTurnMode == iris::VrTurnMode::Snap
              && scene->vrDominantRight,
          "...along the aim ray, snap turning, right hand dominant");

    // THE SECTION SHOWS THE PROJECT'S VALUES, not a set of its own: the scene
    // is given values BEFORE the panel is bound to it.
    scene->vrFlySpeed = 7.5f;
    scene->vrFlyMode = iris::VrFlyMode::Gaze;
    scene->vrTurnMode = iris::VrTurnMode::Smooth;
    scene->vrSnapTurnDegrees = 45.0f;
    scene->vrSmoothTurnDegreesPerSecond = 120.0f;
    scene->vrDominantRight = false;

    WorldVrPropertyWidget panel;
    panel.setServices(&services);
    const int bound = stack.index();
    panel.setScene(scene);
    pump();
    CHECK(stack.index() == bound, "binding the panel to a scene is not an edit");

    // ---- 1. ONE ROW PER TABLE ENTRY, AT THE PROJECT'S VALUES ---------------
    {
        const auto combos = panel.findChildren<ComboBoxWidget *>();
        const auto fields = panel.findChildren<DragFloatWidget *>();
        int enums = 0, numbers = 0;
        for (const vrworld::Row &r : vrworld::rows())
            (r.kind == vrworld::RowKind::Enum ? enums : numbers) += 1;
        CHECK(combos.size() == enums && fields.size() == numbers,
              "the section is GENERATED: one row per table entry, and nothing else");

        DragFloatWidget *speed = dragWith(&panel, QStringLiteral("Fly Speed"));
        CHECK(speed && qFuzzyCompare(speed->value(), 7.5), "the fly speed row shows 7.5 m/s");
        ComboBoxWidget *fly = comboWith(&panel, QStringLiteral("Fly Direction"));
        CHECK(fly && fly->getWidget() && fly->getWidget()->count() == 3,
              "the fly direction offers three modes");
        CHECK(fly && fly->getItemData(fly->getWidget()->currentIndex()).toInt()
                         == int(iris::VrFlyMode::Gaze),
              "...and shows the project's: gaze");
        ComboBoxWidget *hand = comboWith(&panel, QStringLiteral("Dominant Hand"));
        CHECK(hand && hand->getItemData(hand->getWidget()->currentIndex()).toInt() == 0,
              "the dominant hand row shows the project's: left");
    }

    // ---- 2. A ROW THAT MEANS NOTHING IN THIS MODE IS GREYED, NOT HIDDEN ----
    {
        DragFloatWidget *snap = dragWith(&panel, QStringLiteral("Snap Turn"));
        DragFloatWidget *smooth = dragWith(&panel, QStringLiteral("Smooth Turn Rate"));
        CHECK(snap && smooth, "both turn rows exist whichever mode the project is in");
        CHECK(snap && !snap->isEnabled(), "the snap step is greyed while the turn is smooth");
        CHECK(smooth && smooth->isEnabled(), "...and the smooth rate is live");
    }

    // ---- 3. A COMBO WRITES THE DOCUMENT, AS ONE UNDO STEP ------------------
    {
        ComboBoxWidget *turn = comboWith(&panel, QStringLiteral("Turning"));
        CHECK(turn != nullptr, "the turning row is on the blade");
        const int before = stack.index();
        if (turn && turn->getWidget()) {
            const int snapIndex = turn->findData(int(iris::VrTurnMode::Snap));
            turn->getWidget()->setCurrentIndex(snapIndex);
        }
        pump();
        CHECK(scene->vrTurnMode == iris::VrTurnMode::Snap, "picking Snap wrote the document");
        CHECK(stack.index() == before + 1, "...as exactly ONE undo step");
        stack.undo();
        pump();
        CHECK(scene->vrTurnMode == iris::VrTurnMode::Smooth, "and undo put Smooth back");
        stack.redo();
        pump();
        CHECK(scene->vrTurnMode == iris::VrTurnMode::Snap, "...and redo re-applied it");
        // The rows that depend on the mode followed it.
        DragFloatWidget *snap = dragWith(&panel, QStringLiteral("Snap Turn"));
        DragFloatWidget *smooth = dragWith(&panel, QStringLiteral("Smooth Turn Rate"));
        CHECK(snap && snap->isEnabled(), "the snap step is live now the turn snaps");
        CHECK(smooth && !smooth->isEnabled(), "...and the smooth rate is greyed");
    }

    // ---- 4. A SCRUB IS LIVE, AND LANDS AS ONE STEP -------------------------
    {
        DragFloatWidget *speed = dragWith(&panel, QStringLiteral("Fly Speed"));
        CHECK(speed != nullptr, "the fly speed row is on the blade");
        if (speed) {
            const float was = scene->vrFlySpeed;
            const int steps = stack.index();
            emit speed->valueChanged(double(was) + 1.0);
            emit speed->valueChanged(double(was) + 4.0);
            CHECK(qFuzzyCompare(scene->vrFlySpeed, was + 4.0f),
                  "every tick of the scrub wrote through live");
            CHECK(stack.index() == steps, "...and nothing landed mid-scrub");
            emit speed->editingDone();
            CHECK(stack.index() == steps + 1, "the finished scrub is ONE undo step");
            CHECK(stack.text(stack.index() - 1) == QStringLiteral("Fly Speed"),
                  "named for the row");
            stack.undo();
            pump();
            CHECK(qFuzzyCompare(scene->vrFlySpeed, was), "undo restored the speed");
            CHECK(qFuzzyCompare(float(speed->value()), was),
                  "...and the ROW followed the document");
        }
    }

    // ---- 5. THE PANEL CANNOT OFFER WHAT THE VERBS REFUSE -------------------
    // Both are the same table: the row's range IS the verb's range.
    {
        const vrworld::Row *r = vrworld::row(QStringLiteral("flySpeed"));
        CHECK(r && r->minValue > 0.0, "a fly speed row cannot reach zero");
        double out = 0.0;
        QString why;
        CHECK(r && !vrworld::validate(*r, QVariant(0.0), out, why),
              "...and the verb refuses zero for the same reason");
    }

    std::printf(failures ? "FAILURES: %d\n" : "all cases passed (%d failures)\n", failures);
    return failures ? 1 : 0;
}
