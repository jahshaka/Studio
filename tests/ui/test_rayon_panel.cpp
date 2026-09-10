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
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QUndoStack>

#include <cstdio>

#include "irisgl/document/scenegraph/scene.h"

#include "services/services.h"
#include "services/undoservice.h"
#include "services/worldmodes.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/panels/propertywidgets/worldgipropertywidget.h"
#include "ui_hfloatsliderwidget.h"

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

/// A slider row by its label (the row's QLabel is the slider's own child).
static HFloatSliderWidget *sliderWith(QWidget *w, const QString &label)
{
    for (HFloatSliderWidget *s : w->findChildren<HFloatSliderWidget *>())
        for (QLabel *l : s->findChildren<QLabel *>())
            if (l->text().startsWith(label)) return s;
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

    // ---- 5. THE EPIC-COLUMN SLIDERS: live, in place, and ONE undo step -----
    // (rayontiers review follow-up.) A drag is bracketed by the slider's
    // valueChangeStart/End; every tick writes through, the pin mark / reset
    // button / "Custom" entry follow IN PLACE (the slider survives the drag),
    // and release pushes exactly one WorldModeCommand.
    {
        QUndoStack stack;
        UndoService undo(&stack);
        StudioServices services;
        services.undo = &undo;
        panel.setServices(&services);

        worldmodes::setRayon(scene, true, worldmodes::RayonTier::Epic);
        panel.setScene(scene);   // rebuild at Epic (Advanced is still open)
        pump();
        CHECK(scene->giNumBounces == 3 && !scene->worldOverrides.contains(QStringLiteral("giBounces")),
              "Epic again: three bounces, nothing pinned");
        HFloatSliderWidget *bounces = sliderWith(&panel, QStringLiteral("Light Bounces"));
        CHECK(bounces != nullptr, "the Light Bounces slider is on the Advanced surface");
        if (bounces) {
            QSlider *bar = bounces->ui->slider;
            const int before = stack.count();
            emit bar->sliderPressed();          // valueChangeStart
            bounces->setValue(2.0f);            // two ticks of the drag
            bounces->setValue(1.0f);
            pump();
            CHECK(scene->giNumBounces == 1, "each tick wrote through live (the viewport follows the drag)");
            CHECK(scene->worldOverrides.contains(QStringLiteral("giBounces")), "and pinned the row");
            CHECK(stack.count() == before, "but NO undo step landed mid-drag");
            CHECK(sliderWith(&panel, QStringLiteral("Light Bounces")) == bounces,
                  "the slider being dragged was NOT rebuilt away");
            CHECK(bounces->ui->label->text().endsWith(QStringLiteral(" *")),
                  "its pin mark appeared in place");
            CHECK(buttonWith(&panel, QStringLiteral("Reset Advanced")) != nullptr,
                  "the reset button was offered in place");
            auto *tierCombo = panel.findChildren<ComboBoxWidget *>().value(0)->getWidget();
            CHECK(tierCombo && tierCombo->count() == 5 && tierCombo->currentIndex() == 4,
                  "and the tier row reads Custom in place");
            emit bar->sliderReleased();         // valueChangeEnd
            CHECK(stack.count() == before + 1, "release pushed exactly ONE undo step");
            CHECK(stack.count() > 0 && stack.text(stack.count() - 1) == QStringLiteral("Rayon Light Bounces"),
                  "named for the row");

            // A press-and-release that moved nothing is not an edit.
            emit bar->sliderPressed();
            emit bar->sliderReleased();
            CHECK(stack.count() == before + 1, "a click that moved nothing pushes nothing");

            stack.undo();
            pump();
            CHECK(scene->giNumBounces == 3 && !scene->worldOverrides.contains(QStringLiteral("giBounces")),
                  "undo restores the tier's three bounces AND drops the pin");
            CHECK(buttonWith(&panel, QStringLiteral("Reset Advanced")) == nullptr,
                  "the panel repainted: no reset offered");
            auto *tierCombo2 = panel.findChildren<ComboBoxWidget *>().value(0)->getWidget();
            CHECK(tierCombo2 && tierCombo2->count() == 4 && tierCombo2->currentIndex() == 3,
                  "and the tier row reads Epic again");
            stack.redo();
            pump();
            CHECK(scene->giNumBounces == 1 && scene->worldOverrides.contains(QStringLiteral("giBounces")),
                  "redo puts the pinned edit back");
            stack.undo();
            pump();
        }

        // Dynamic Probes is wired the same way (Epic = hybrid, so its row exists).
        HFloatSliderWidget *dyn = sliderWith(&panel, QStringLiteral("Dynamic Probes"));
        CHECK(dyn != nullptr, "the Dynamic Probes slider is on the Advanced surface at Epic");
        if (dyn) {
            // index(), not count(): the undone bounces step above is still ON
            // the stack until this push discards it.
            const int before = stack.index();
            emit dyn->ui->slider->sliderPressed();
            dyn->setValue(5.0f);
            emit dyn->ui->slider->sliderReleased();
            pump();
            CHECK(scene->giDynamicProbes == 5 && scene->worldOverrides.contains(QStringLiteral("giDynamicProbes")),
                  "a dynamic-probes drag writes through and pins");
            CHECK(stack.index() == before + 1 &&
                      stack.text(stack.index() - 1) == QStringLiteral("Rayon Dynamic Probes"),
                  "as one named undo step");
            stack.undo();
            pump();
            CHECK(scene->giDynamicProbes == 2, "undone to Epic's two");
        }

        // A tick with no bracket (the slider's contract allows one) is its own
        // one-step edit — nothing can write through without an undo step.
        HFloatSliderWidget *bounces2 = sliderWith(&panel, QStringLiteral("Light Bounces"));
        if (bounces2) {
            const int before = stack.index();
            bounces2->setValue(4.0f);
            pump();
            CHECK(scene->giNumBounces == 4 && stack.index() == before + 1,
                  "an un-bracketed tick is an atomic undo step");
            stack.undo();
            pump();
            CHECK(scene->giNumBounces == 3, "and undoes");
        }
        panel.setServices(nullptr);
    }

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
