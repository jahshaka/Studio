/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.rayon_panel — the World panel's Rayon section IS one switch, one dial and
// one budget (SPECS/GI_UNIFIED_SPEC.md §2, owner decisions D3/D5).
//
// The product ask this phase answers is a UI claim ("hide the settings it
// consumes"), and a UI claim needs a UI test: an assertion on the verbs would
// pass just as well with the old eight-row panel still on screen. So this
// builds the REAL widget with a real scene and counts what it put in front of
// the user — three rows and a disclosure — then drives those controls the way a
// user does and asserts on the DOCUMENT.
//
// It also pins the two properties that make the disclosure safe:
//   * an Advanced edit PINS its row (the tier stops overwriting it), and
//   * the panel never invents state of its own — every gesture goes through
//     services/worldmodes, which is what makes the panel, world.rayon,
//     world.gi and world.override the same model.
//
// Offscreen QPA, no display, no rendering (the section is widgets and a
// document). ui.material_panel is the precedent for the shape.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>

#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"

#include "services/worldmodes.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/panels/propertywidgets/worldgipropertywidget.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// THE PANEL REBUILDS ITSELF on every edit (the sky panel's pattern), and
/// AccordianBladeWidget::clearPanel retires the old rows with deleteLater() —
/// which needs an event loop. This suite has none, so it drains the deferred
/// deletions by hand after every gesture; without this, findChildren() would
/// keep returning the widgets of every previous build and every count below
/// would be meaningless.
static void pump()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

/// The widget the panel builds for a labelled row is a wrapper; the control a
/// user actually clicks is the plain Qt widget inside it.
static QCheckBox *box(CheckBoxWidget *w) { return w ? w->findChild<QCheckBox *>() : nullptr; }

static QPushButton *buttonWith(QWidget *w, const QString &text)
{
    for (QPushButton *b : w->findChildren<QPushButton *>())
        if (b->text().contains(text, Qt::CaseInsensitive)) return b;
    return nullptr;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-rayon-panel-ogre.log");
    if (!graph.require()) return 1;

    auto scene = iris::Scene::create();
    // Exactly what MainWindow::createDefaultScene does: a new scene is Epic.
    worldmodes::setMode(scene, worldmodes::Mode::Epic);

    CHECK(scene->giNumBounces == 3 && scene->giDynamicProbes == 2,
          "a new Epic scene carries Epic's column (3 bounces, 2 dynamic probes)");

    WorldGiPropertyWidget panel;
    panel.setScene(scene);

    // ---- 1. THREE ROWS AND A DISCLOSURE ------------------------------------
    {
        const auto checks = panel.findChildren<CheckBoxWidget *>();
        const auto combos = panel.findChildren<ComboBoxWidget *>();
        const auto sliders = panel.findChildren<HFloatSliderWidget *>();
        CHECK(checks.size() == 1, "one switch row (Rayon) and nothing else checkable");
        CHECK(combos.size() == 1, "one combo row (the quality tier) — the technique picker "
                                  "and the voxel/probe quality are NOT on the visible surface");
        CHECK(sliders.size() == 1, "one slider row (the GI update budget)");
        CHECK(buttonWith(&panel, QStringLiteral("Advanced")) != nullptr,
              "and an Advanced disclosure");
        // The old panel's rows must be reachable but not visible: the Fit
        // Bounds button is the cheapest witness that the volume rows are away.
        CHECK(buttonWith(&panel, QStringLiteral("Fit Bounds")) == nullptr,
              "the bounds rows are behind the disclosure, not on the surface");
    }

    // ---- 2. THE TIER ROW DRIVES THE DOCUMENT -------------------------------
    {
        auto *combo = panel.findChildren<ComboBoxWidget *>().value(0);
        QComboBox *tier = combo ? combo->getWidget() : nullptr;
        CHECK(tier && tier->count() == 4, "the tier row offers exactly four tiers");
        CHECK(tier && tier->currentIndex() == 3, "and a new scene shows Epic");
        if (tier) tier->setCurrentIndex(1);   // Medium
        pump();
        CHECK(scene->giMode == iris::GiMode::VCT && int(scene->giQuality) == 1 &&
                  scene->giDdgi == 1 && scene->giNumBounces == 1 && scene->giDynamicProbes == 0,
              "picking Medium wrote the technique, the quality (DDGI-fed), the bounces and "
              "the dynamic probes through");
        CHECK(scene->giTier == int(worldmodes::RayonTier::Medium),
              "and recorded the tier on the document");
    }

    // ---- 3. THE SWITCH ------------------------------------------------------
    {
        auto *sw = box(panel.findChildren<CheckBoxWidget *>().value(0));
        CHECK(sw && sw->isChecked(), "the switch shows Rayon on");
        if (sw) sw->setChecked(false);
        pump();
        CHECK(scene->giMode == iris::GiMode::OFF, "unchecking it turns GI off");
        CHECK(scene->giTier == int(worldmodes::RayonTier::Medium),
              "and the tier is remembered while off");
        // The panel rebuilt itself, so the control is a NEW widget.
        auto *sw2 = box(panel.findChildren<CheckBoxWidget *>().value(0));
        CHECK(sw2 && !sw2->isChecked(), "the rebuilt switch shows off");
        if (sw2) sw2->setChecked(true);
        pump();
        CHECK(scene->giMode == iris::GiMode::VCT, "and switching back on restores Medium");
    }

    // ---- 4. THE DISCLOSURE + THE PIN ---------------------------------------
    {
        auto *advanced = buttonWith(&panel, QStringLiteral("Advanced"));
        CHECK(advanced && advanced->isCheckable(), "the disclosure is a toggle");
        if (advanced) advanced->setChecked(true);
        pump();

        const auto combos = panel.findChildren<ComboBoxWidget *>();
        CHECK(combos.size() >= 3,
              "opening it reveals the technique and quality pickers alongside the tier");
        CHECK(buttonWith(&panel, QStringLiteral("Fit Bounds")) != nullptr,
              "and the bounds rows with their Fit button");
        CHECK(panel.findChildren<CheckBoxWidget *>().size() == 2,
              "and the irradiance-field toggle");

        // An Advanced edit PINS: the technique combo is the second one (the
        // tier is first, by construction of rebuild()).
        QComboBox *technique = combos.value(1)->getWidget();
        CHECK(technique && technique->count() == 4, "the technique picker offers all four modes");
        if (technique) technique->setCurrentIndex(3);   // VCT + Probes
        pump();
        CHECK(scene->giMode == iris::GiMode::VCT_PCC_HYBRID, "picking one writes it through");
        CHECK(scene->worldOverrides.contains(QStringLiteral("giMode")),
              "and PINS it, which is what stops the tier overwriting it");
        CHECK(worldmodes::rayonCustom(scene), "so the tier row now reads Custom");

        // ... and the pin survives a tier switch made from the visible row.
        auto *tierCombo = panel.findChildren<ComboBoxWidget *>().value(0)->getWidget();
        CHECK(tierCombo && tierCombo->count() == 5,
              "the tier row grows a (non-pickable) Custom entry while a pin deviates");
        if (tierCombo) tierCombo->setCurrentIndex(0);   // Low
        pump();
        CHECK(scene->giTier == int(worldmodes::RayonTier::Low), "the tier moved to Low");
        CHECK(scene->giMode == iris::GiMode::VCT_PCC_HYBRID,
              "and the pinned technique SURVIVED it");

        // The way back is one button, and it is only offered when there is
        // something to hand back.
        auto *reset = buttonWith(&panel, QStringLiteral("Reset Advanced"));
        CHECK(reset != nullptr, "a reset is offered while a row is pinned");
        if (reset) reset->click();
        pump();
        CHECK(!scene->worldOverrides.contains(QStringLiteral("giMode")) &&
                  scene->giMode == iris::GiMode::INSTANT_RADIOSITY,
              "and it hands the technique back to the tier (Low = Instant Radiosity)");
        CHECK(buttonWith(&panel, QStringLiteral("Reset Advanced")) == nullptr,
              "after which the reset is not offered any more");
    }

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
