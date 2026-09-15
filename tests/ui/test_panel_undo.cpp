/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// ui.panel_undo — EVERY PROPERTIES ROW IS UNDOABLE (debt L6 / N5).
//
// The claim this suite defends is one sentence: a gesture on a properties row
// writes the document live AND lands exactly one step on the undo stack, which
// puts the document back. It was false for every panel but two until this lane
// — an API-first INVERSION, since the verbs the same rows call have promised
// "one run, one undo step" since the scripting engine shipped.
//
// It is a UI test because the claim is about GESTURES, not about the document:
// the same assertion against the verbs passes just as well with a panel that
// writes `scene->fogDensity = v` from its slot and records nothing. So this
// builds the REAL blades over a real document with a real UndoService and
// drives their controls the way a user does — press, move, move, release —
// then asserts on the DOCUMENT and on the STACK.
//
// The four properties every row must have, and which every section below
// checks for at least one of its rows:
//   1. a drag writes through LIVE (the viewport is the feedback) ...
//   2. ... and lands ONE step, on release, not one per tick;
//   3. undo restores the document (and, for a quality-registry row, the PIN
//      the edit created — the World Mode section must not be left lying);
//   4. SELECTING a node or a scene records NOTHING (the rows emit from their
//      setters, so this is a real hazard and not a theoretical one).
//
// Offscreen QPA, no display, no rendering: panels and a document.

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QLineEdit>
#include <QPushButton>
#include <QMouseEvent>
#include <QVector3D>
#include <QUndoStack>
#include <limits>

#include <cstdio>

#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "commands/nodeeditcommand.h"
#include "data/database/database.h"
#include "data/project.h"
#include "services/services.h"
#include "services/editgate.h"
#include "services/undoservice.h"
#include "services/worldmodes.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/dragspinbox.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/panels/propertywidgets/emitterpropertywidget.h"
#include "ui/panels/propertywidgets/fogpropertywidget.h"
#include "ui/panels/propertywidgets/lightpropertywidget.h"
#include "ui/panels/propertywidgets/meshpropertywidget.h"
#include "ui/panels/propertywidgets/mobilitypropertywidget.h"
#include "ui/panels/propertywidgets/physicspropertywidget.h"
#include "ui/panels/propertywidgets/skypropertywidget.h"
#include "ui/panels/propertywidgets/worldaapropertywidget.h"
#include "ui/panels/propertywidgets/worldpostfxpropertywidget.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"
#include "ui/panels/propertywidgets/worldshadowpropertywidget.h"
#include "ui/panels/scenenodepropertieswidget.h"
#include "ui/panels/transformeditor.h"
#include "ui/controls/texturepickerwidget.h"
#include "ui_hfloatsliderwidget.h"

#include "../support/documentgraph.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

/// The sky panel rebuilds itself with deleteLater()'d rows and a QUEUED type
/// switch; this suite has no event loop, so it drains both by hand.
static void pump()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

static QCheckBox *box(CheckBoxWidget *w) { return w ? w->findChild<QCheckBox *>() : nullptr; }

/// A slider row by its label (the row's QLabel is the slider's own child).
static HFloatSliderWidget *sliderWith(QWidget *w, const QString &label)
{
    for (HFloatSliderWidget *s : w->findChildren<HFloatSliderWidget *>())
        if (s->ui->label->text().startsWith(label)) return s;
    return nullptr;
}

static CheckBoxWidget *checkWith(QWidget *w, const QString &label)
{
    for (CheckBoxWidget *c : w->findChildren<CheckBoxWidget *>())
        for (QLabel *l : c->findChildren<QLabel *>())
            if (l->text().startsWith(label)) return c;
    return nullptr;
}

static ComboBoxWidget *comboWith(QWidget *w, const QString &label)
{
    for (ComboBoxWidget *c : w->findChildren<ComboBoxWidget *>())
        for (QLabel *l : c->findChildren<QLabel *>())
            if (l->text().startsWith(label)) return c;
    return nullptr;
}

/// Every RETIRED row is hidden.
///
/// A blade that rebuilds itself hands its rows to clearPanel(), which
/// deleteLater()s them — and a widget that has left the layout but is still
/// visible keeps painting at its old geometry until the next event-loop turn.
/// One repaint in that window (a page switch, a dock resize) and the old rows
/// are drawn ON TOP of the new ones: the "garbled Properties rows" shape.
/// So: nothing that is a child of the content pane and NOT in its layout may
/// still be able to paint. `sample` is any row the blade currently holds — its
/// parent IS the content pane.
static bool noStrandedRows(QWidget *sample)
{
    QWidget *pane = sample ? sample->parentWidget() : nullptr;
    if (!pane || !pane->layout()) return false;   // no rows at all: say so loudly
    for (QWidget *child : pane->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
        if (pane->layout()->indexOf(child) >= 0) continue;
        // isVisibleTo(), not isVisible(): nothing in this suite is ever SHOWN,
        // so the question is "would this paint when the pane does" — which is
        // exactly the condition a stranded row satisfies.
        if (child->isVisibleTo(pane)) return false;
    }
    return true;
}

static TexturePickerWidget *pickerWith(QWidget *w, const QString &label)
{
    for (TexturePickerWidget *p : w->findChildren<TexturePickerWidget *>())
        for (QLabel *l : p->findChildren<QLabel *>())
            if (l->text().startsWith(label)) return p;
    return nullptr;
}

static DragFloatWidget *dragWith(QWidget *w, const QString &label)
{
    for (DragFloatWidget *d : w->findChildren<DragFloatWidget *>())
        for (QLabel *l : d->findChildren<QLabel *>())
            if (l->text().startsWith(label)) return d;
    return nullptr;
}

/// A user's SCRUB on a transform field: press on the field, a few horizontal
/// ticks, release — the gesture DragSpinBox watches for on its line edit
/// (ui/controls/dragspinbox.cpp). `dx` is the travel in pixels; the value moves
/// by dx * the row's per-pixel step. Returns the value the field held just
/// before the release (the live half of the gesture), or NaN if the box is
/// missing, so a renamed field FAILS rather than passing silently.
static double scrub(DragSpinBox *box, int dx)
{
    QWidget *edit = box ? box->findChild<QLineEdit *>() : nullptr;
    if (!edit) return std::numeric_limits<double>::quiet_NaN();
    const QPointF local(6, 6);
    const QPointF origin = edit->mapToGlobal(local.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, local, origin, Qt::LeftButton, Qt::LeftButton,
                      Qt::NoModifier);
    QCoreApplication::sendEvent(edit, &press);
    const int step = dx > 0 ? 4 : -4;
    for (int moved = step; qAbs(moved) <= qAbs(dx); moved += step) {
        const QPointF at = origin + QPointF(moved, 0);
        QMouseEvent move(QEvent::MouseMove, local + QPointF(moved, 0), at, Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(edit, &move);
    }
    const double live = box->value();
    const QPointF end = origin + QPointF(dx, 0);
    QMouseEvent release(QEvent::MouseButtonRelease, local + QPointF(dx, 0), end, Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(edit, &release);
    return live;
}

/// A user's drag: press, a couple of ticks, release. Returns false when the row
/// was not found, so a renamed row FAILS rather than passing silently.
static bool drag(HFloatSliderWidget *slider, float first, float last)
{
    if (!slider) return false;
    emit slider->ui->slider->sliderPressed();
    slider->setValue(first);
    slider->setValue(last);
    emit slider->ui->slider->sliderReleased();
    return true;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    enginetest::DocumentGraph graph("ui-panel-undo-ogre.log");
    if (!graph.require()) return 1;

    QUndoStack stack;
    UndoService undo(&stack);
    StudioServices services;
    services.undo = &undo;

    auto scene = iris::Scene::create();
    worldmodes::setMode(scene, worldmodes::Mode::Epic);

    // ---- 1. THE FOG SECTION -------------------------------------------------
    {
        FogPropertyWidget panel;
        panel.setServices(&services);

        const int before = stack.index();
        panel.setScene(scene);          // SELECTING the world is not an edit
        pump();
        CHECK(stack.index() == before, "fog: binding the panel to a scene records nothing");

        HFloatSliderWidget *density = sliderWith(&panel, QStringLiteral("Fog Density"));
        CHECK(density != nullptr, "fog: the density row is on the blade");
        const float was = scene->fogDensity;
        CHECK(drag(density, 0.02f, 0.05f), "fog: the density row can be dragged");
        CHECK(qFuzzyCompare(scene->fogDensity, 0.05f), "fog: every tick wrote through live");
        CHECK(stack.index() == before + 1, "fog: the drag landed exactly ONE step");
        CHECK(stack.text(stack.index() - 1) == QStringLiteral("Fog Density"),
              "fog: named for the row");
        stack.undo();
        pump();
        CHECK(qFuzzyCompare(scene->fogDensity, was), "fog: undo restored the density");
        CHECK(qFuzzyCompare(density->getValue(), was),
              "fog: and the ROW followed the document (the panel repainted in place)");

        // A press-and-release that moved nothing is not an edit.
        const int quiet = stack.index();
        emit density->ui->slider->sliderPressed();
        emit density->ui->slider->sliderReleased();
        CHECK(stack.index() == quiet, "fog: a click that moved nothing pushes nothing");

        CheckBoxWidget *enabled = checkWith(&panel, QStringLiteral("Fog Enabled"));
        CHECK(enabled != nullptr, "fog: the enable row is on the blade");
        if (auto *b = box(enabled)) b->setChecked(!scene->fogEnabled);
        CHECK(stack.index() == quiet + 1, "fog: a checkbox is one step");
        const bool nowEnabled = scene->fogEnabled;
        stack.undo();
        CHECK(scene->fogEnabled != nowEnabled, "fog: undone");
    }

    // ---- 2. THE WORLD SECTION ----------------------------------------------
    {
        WorldPropertyWidget panel;
        panel.setServices(&services);
        const int before = stack.index();
        panel.setScene(scene);
        pump();
        CHECK(stack.index() == before, "world: binding records nothing");

        HFloatSliderWidget *gravity = sliderWith(&panel, QStringLiteral("Gravity"));
        CHECK(gravity != nullptr, "world: the gravity row is on the blade");
        const float was = scene->gravity;
        CHECK(drag(gravity, 5.0f, 12.0f), "world: the gravity row can be dragged");
        CHECK(qFuzzyCompare(scene->gravity, 12.0f), "world: gravity wrote through live");
        CHECK(stack.index() == before + 1, "world: ONE step for the drag");
        stack.undo();
        CHECK(qFuzzyCompare(scene->gravity, was), "world: undo restored gravity");

        ComboBoxWidget *play = comboWith(&panel, QStringLiteral("Play Mode"));
        CHECK(play != nullptr, "world: the play-mode row is on the blade");
        const int steps = stack.index();
        if (play && play->getWidget()) play->getWidget()->setCurrentIndex(1);
        CHECK(scene->getPlayMode() == iris::ScenePlayMode::ThirdPerson,
              "world: picking a play mode wrote it through");
        CHECK(stack.index() == steps + 1, "world: as ONE step");
        stack.undo();
        CHECK(scene->getPlayMode() == iris::ScenePlayMode::Explorer, "world: undone");

        // HARDWARE RAY TRACING (ledger §425) — a PLAIN world row and not a
        // quality-registry one, so it writes the document and pins NOTHING:
        // what a project was authored for is not a scalability trade, and a
        // World Mode switch must never move it.
        // Found by a PREFIX: a row label is elided to the label column's width
        // The row is "Ray Tracing" (renamed from "Hardware Ray Tracing" at the
        // RAYROW-1 merge so it fits a narrow label column without eliding).
        ComboBoxWidget *rays = comboWith(&panel, QStringLiteral("Ray Tracing"));
        CHECK(rays != nullptr, "rays: the row is on the World blade");
        CHECK(scene->rayTracing == iris::RayTracingMode::Auto,
              "rays: a scene starts at Auto");
        const int raySteps = stack.index();
        if (rays && rays->getWidget()) rays->getWidget()->setCurrentIndex(2);   // On
        CHECK(scene->rayTracing == iris::RayTracingMode::On,
              "rays: picking On wrote it through");
        CHECK(stack.index() == raySteps + 1, "rays: as ONE step");
        CHECK(!scene->worldOverrides.contains(QStringLiteral("rayTracing")),
              "rays: and it pinned NOTHING — it is not a tier row");
        stack.undo();
        pump();
        CHECK(scene->rayTracing == iris::RayTracingMode::Auto, "rays: undone");
        CHECK(rays && rays->getWidget() && rays->getWidget()->currentIndex() == 1,
              "rays: and the ROW followed the document back to Auto");
        // Off is the third state, and the row reaches it the same way.
        if (rays && rays->getWidget()) rays->getWidget()->setCurrentIndex(0);   // Off
        CHECK(scene->rayTracing == iris::RayTracingMode::Off, "rays: Off writes through too");
        stack.undo();
        pump();
        CHECK(scene->rayTracing == iris::RayTracingMode::Auto, "rays: and undoes");
    }

    // ---- 3. A QUALITY-REGISTRY ROW: THE VALUE **AND** THE PIN ---------------
    {
        WorldAaPropertyWidget panel;
        panel.setServices(&services);
        const int before = stack.index();
        panel.setScene(scene);
        pump();
        CHECK(stack.index() == before, "msaa: binding records nothing");
        CHECK(!scene->worldOverrides.contains(QStringLiteral("msaa")),
              "msaa: nothing is pinned to start with");

        ComboBoxWidget *combo = comboWith(&panel, QStringLiteral("MSAA"));
        CHECK(combo != nullptr, "msaa: the row is on the blade");
        const int was = scene->antiAliasing;
        // 4x — a level the tier does NOT already give (the tier is 2x since
        // 2026-09-15; picking the tier's own value is a no-op that pins nothing).
        if (combo && combo->getWidget()) combo->getWidget()->setCurrentIndex(2);   // 4x
        CHECK(scene->antiAliasing == 4, "msaa: the pick wrote the document");
        CHECK(scene->worldOverrides.contains(QStringLiteral("msaa")),
              "msaa: and PINNED the row against the tier");
        CHECK(stack.index() == before + 1, "msaa: as ONE step");
        stack.undo();
        pump();
        CHECK(scene->antiAliasing == was, "msaa: undo restored the count");
        CHECK(!scene->worldOverrides.contains(QStringLiteral("msaa")),
              "msaa: AND dropped the pin — the World Mode section is not left lying");
    }

    {
        WorldShadowPropertyWidget panel;
        panel.setServices(&services);
        panel.setScene(scene);
        pump();
        ComboBoxWidget *combo = comboWith(&panel, QStringLiteral("Shadow Quality"));
        CHECK(combo != nullptr, "shadows: the quality row is on the blade");
        const int was = scene->shadowResolution;
        const int before = stack.index();
        // A row already showing the tier's value is not an edit — pick the OTHER
        // one (Epic resolves this row to 2048, so 1024 is always a change).
        if (combo && combo->getWidget()) combo->getWidget()->setCurrentIndex(1);   // 1024
        CHECK(scene->shadowResolution == 1024, "shadows: the pick wrote the document");
        CHECK(stack.index() == before + 1, "shadows: as ONE step");
        stack.undo();
        pump();
        CHECK(scene->shadowResolution == was, "shadows: undone, pin and all");
        CHECK(!scene->worldOverrides.contains(QStringLiteral("shadowResolution")),
              "shadows: the pin went with it");
    }

    // ---- 4. THE POST-PROCESS SECTION: A TOGGLE AND A SCRUB ------------------
    {
        WorldPostFxPropertyWidget panel;
        panel.setServices(&services);
        const int before = stack.index();
        panel.setScene(scene);
        pump();
        CHECK(stack.index() == before, "postfx: binding records nothing");

        CheckBoxWidget *bloom = checkWith(&panel, QStringLiteral("Bloom"));
        CHECK(bloom != nullptr, "postfx: the bloom row is on the blade");
        const bool wasOn = worldmodes::resolved(scene, *worldmodes::row(QStringLiteral("bloom"))) != 0;
        if (auto *b = box(bloom)) b->setChecked(!wasOn);
        pump();
        CHECK(stack.index() == before + 1, "postfx: a toggle is ONE step");
        stack.undo();
        pump();
        CHECK(worldmodes::resolved(scene, *worldmodes::row(QStringLiteral("bloom"))) != 0 == wasOn,
              "postfx: undone");

        // A parameter SCRUB: values stream, one step on the mouse-up.
        DragFloatWidget *exposure = dragWith(&panel, QStringLiteral("Exposure"));
        CHECK(exposure != nullptr, "postfx: the exposure parameter is on the blade");
        if (exposure) {
            const float was = scene->exposure;
            const int steps = stack.index();
            emit exposure->valueChanged(was + 0.5);
            emit exposure->valueChanged(was + 1.0);
            CHECK(!qFuzzyCompare(scene->exposure, was), "postfx: the scrub wrote through live");
            CHECK(stack.index() == steps, "postfx: and nothing landed mid-scrub");
            emit exposure->editingDone();
            CHECK(stack.index() == steps + 1, "postfx: the finished scrub is ONE step");
            stack.undo();
            pump();
            CHECK(qFuzzyCompare(scene->exposure, was), "postfx: undone");
        }
    }

    // ---- 5. THE SKY SECTION: ONE STEP OVER THE WHOLE SKY BLOCK -------------
    {
        SkyPropertyWidget panel;
        panel.setServices(&services);
        scene->skyType = iris::SkyType::REALISTIC;
        const int before = stack.index();
        panel.setScene(scene);
        pump();
        CHECK(stack.index() == before, "sky: binding records nothing");

        // A REBUILT blade leaves nothing stranded (debt L6 / A5b): the sky
        // section is one of the two that genuinely rebuilds (its rows change
        // with the sky type), so it is where the rule is checked.
        CHECK(noStrandedRows(comboWith(&panel, QStringLiteral("Sky Type"))),
              "sky: a freshly built blade has no stranded rows");
        scene->skyType = iris::SkyType::GRADIENT;
        panel.setScene(scene);          // rebuild: the previous rows are retired
        CHECK(noStrandedRows(comboWith(&panel, QStringLiteral("Sky Type"))),
              "sky: and after a rebuild every retired row is HIDDEN, not left painting");
        scene->skyType = iris::SkyType::REALISTIC;
        panel.setScene(scene);
        pump();

        // THE SUN DIALS ARE GONE (SKY_LIGHT_SPEC.md §3): the realistic sky's
        // sun is the scene's sun LIGHT, so Azimuth and Elevation are not rows
        // any more. DENSITY takes the role this case was written for (the
        // Preetham dials went with the CPU bake, SKY-GPU) — it is the same
        // binding through the same `sky` sceneprops row, which is what is
        // actually being gated (one drag, one undo step, the live field AND
        // the serialized blob travelling together).
        CHECK(sliderWith(&panel, QStringLiteral("Sun Azimuth")) == nullptr,
              "sky: the sun dials are gone from the blade (the sky follows the sun light)");
        HFloatSliderWidget *turb = sliderWith(&panel, QStringLiteral("Density"));
        CHECK(turb != nullptr, "sky: the realistic sky's own rows are on the blade");
        const float was = scene->skyRealistic.density;
        CHECK(drag(turb, 0.4f, 0.9f), "sky: the density row can be dragged");
        CHECK(qAbs(scene->skyRealistic.density - 0.9f) < 0.05f,
              "sky: the drag moved the density live");
        CHECK(stack.index() == before + 1, "sky: as ONE step");
        stack.undo();
        pump();
        CHECK(qAbs(scene->skyRealistic.density - was) < 0.05f,
              "sky: undo restored it (the whole sky block travels together)");
        // AN UNBRACKETED TICK IS ITS OWN STEP. A keyboard arrow or a typed
        // value arrives as a bare valueChanged, and the row's write must run
        // INSIDE the binding — when the panel also connected the writing slot
        // directly, the document was already written by the time the gesture
        // snapshotted it, so after == before and the edit vanished from the
        // history (code review).
        // The undo above REPAINTED the section (the rows are the sky), so the
        // slider from before it is gone — ask for the row again, which is also
        // the cheapest proof that the repaint happened.
        HFloatSliderWidget *turb2 = sliderWith(&panel, QStringLiteral("Density"));
        CHECK(turb2 != nullptr && turb2 != turb, "sky: an undo rebuilt the section's rows");
        if (turb2) {
            const int steps = stack.index();
            turb2->setValue(0.6f);
            pump();
            CHECK(qAbs(scene->skyRealistic.density - 0.6f) < 0.05f,
                  "sky: an unbracketed tick writes through");
            CHECK(stack.index() == steps + 1, "sky: and is its own one-step edit");
            stack.undo();
            pump();
        }

        // THE SUN'S OWN AIR IS A ROW OF ITS OWN (lane SKY-DENSITY-1) and it is
        // NOT a sky-look row: it is the atmosphere's turbidity, the one input
        // to the sunlight's transmittance (sky.sun_transmittance gates the
        // model). Here it only has to be a row like every other one — one
        // drag, one undo step, the live field and the blob together.
        HFloatSliderWidget *haze = sliderWith(&panel, QStringLiteral("Sun Haze"));
        CHECK(haze != nullptr, "sky: the Sun Haze row is on the blade");
        if (haze) {
            const int steps = stack.index();
            const float hazeWas = scene->skyRealistic.sunHaze;
            CHECK(drag(haze, 2.5f, 5.0f), "sky: the Sun Haze row can be dragged");
            CHECK(qAbs(scene->skyRealistic.sunHaze - 5.0f) < 0.1f,
                  "sky: the drag moved the haze live");
            CHECK(stack.index() == steps + 1, "sky: as ONE step");
            stack.undo();
            pump();
            CHECK(qAbs(scene->skyRealistic.sunHaze - hazeWas) < 0.1f,
                  "sky: undo restored the haze");
        }

        // The blob the scene SAVES carries the same value: an undo that put back
        // the live field and not the blob would reappear on the next open.
        const QJsonObject stored = scene->skyData.value(QStringLiteral("Realistic"));
        CHECK(qAbs(stored.value(QStringLiteral("density")).toDouble()
                       - double(scene->skyRealistic.density)) < 0.01,
              "sky: and the serialized blob agrees with the live field");
        CHECK(qAbs(stored.value(QStringLiteral("sunHaze")).toDouble()
                       - double(scene->skyRealistic.sunHaze)) < 0.01,
              "sky: ...and so does the sun's haze");
    }

    // ---- 6. A NODE ROW: THE LIGHT PANEL ------------------------------------
    {
        auto light = iris::LightNode::create();
        light->intensity = 1.0f;
        LightPropertyWidget panel;
        panel.setServices(&services);
        const int before = stack.index();
        panel.setSceneNode(light);      // SELECTING a light is not an edit
        CHECK(stack.index() == before, "light: selecting a light records nothing");
        CHECK(qFuzzyCompare(light->intensity, 1.0f),
              "light: and did not write the row's defaults back into it");

        HFloatSliderWidget *intensity = sliderWith(&panel, QStringLiteral("Intensity"));
        CHECK(intensity != nullptr, "light: the intensity row is on the blade");
        CHECK(drag(intensity, 3.0f, 6.0f), "light: the intensity row can be dragged");
        CHECK(qFuzzyCompare(light->intensity, 6.0f), "light: wrote through live");
        CHECK(stack.index() == before + 1, "light: ONE step for the drag");
        stack.undo();
        CHECK(qFuzzyCompare(light->intensity, 1.0f), "light: undo restored the intensity");

        ComboBoxWidget *shadow = comboWith(&panel, QStringLiteral("Shadow Type"));
        CHECK(shadow != nullptr, "light: the shadow-type row is on the blade");
        const int steps = stack.index();
        // Whatever a fresh light's filter is (Soft, today), Hard is a change.
        const iris::ShadowMapType bornWith = light->shadowMap->shadowType;
        if (shadow && shadow->getWidget()) shadow->getWidget()->setCurrentIndex(1);   // Hard
        CHECK(light->shadowMap->shadowType == iris::ShadowMapType::Hard,
              "light: picking a shadow type wrote it through");
        CHECK(stack.index() == steps + 1, "light: as ONE step");
        stack.undo();
        CHECK(light->shadowMap->shadowType == bornWith, "light: undone");
    }

    // ---- 6b. THE MOVEMENT ROW (REALTIME_REFLECTIONS_SPEC §3.3) -------------
    // "Does this object move?" — the classification the renderer bakes the
    // room's lighting on. The row must REFLECT what the document says (the
    // panel is where an author discovers what Auto decided) and WRITE what the
    // author picks, as one undo step, through the same reflected key
    // node.setProperty(id, 'mobility', ...) writes.
    {
        auto node = iris::SceneNode::create();
        scene->getRootNode()->addChild(node, false);
        MobilityPropertyWidget panel;
        panel.setServices(&services);
        const int before = stack.index();
        panel.setSceneNode(node);        // SELECTING a node is not an edit
        pump();
        CHECK(stack.index() == before, "movement: selecting a node records nothing");
        CHECK(node->mobility() == iris::Mobility::Auto,
              "movement: ...and did not write the row's default back into it");

        ComboBoxWidget *row = comboWith(&panel, QStringLiteral("Movement"));
        CHECK(row != nullptr, "movement: the row is on the blade");
        CHECK(row && row->getWidget() && row->getWidget()->count() == 3,
              "movement: three choices");
        // PLAIN ENGLISH, in the same words as the result line below it (the
        // owner is not a programmer): the document's "auto"/"static"/"movable"
        // is a file format, not a sentence.
        CHECK(row && row->getWidget()
                  && row->getWidget()->itemText(0) == QStringLiteral("Auto")
                  && row->getWidget()->itemText(1) == QStringLiteral("Static (never moves)")
                  && row->getWidget()->itemText(2) == QStringLiteral("Movable (moves)"),
              "movement: the choices read plainly, in the row's own vocabulary");
        CHECK(row && row->getWidget() && row->getWidget()->currentIndex() == 0,
              "movement: the row REFLECTS the document (a fresh node is auto)");

        // What AUTO resolved to is shown, in plain words, with the reason.
        CHECK(MobilityPropertyWidget::resolvedText(node) == QStringLiteral("Never moves - nothing moves it"),
              "movement: the result line says what auto worked out to");

        if (row && row->getWidget()) row->getWidget()->setCurrentIndex(2);   // movable
        CHECK(node->mobility() == iris::Mobility::Movable,
              "movement: picking Movable wrote it through to the document");
        CHECK(stack.index() == before + 1, "movement: as ONE undo step");
        CHECK(MobilityPropertyWidget::resolvedText(node) == QStringLiteral("Moves - you set it"),
              "movement: ...and the result line followed it");
        stack.undo();
        pump();
        CHECK(node->mobility() == iris::Mobility::Auto, "movement: undone");

        // A DRIVEN node: the row still shows the SETTING (auto), and the result
        // line is where the author learns why it moves anyway.
        node->isPhysicsBody = true;
        node->physicsProperty.type = iris::PhysicsType::RigidBody;
        panel.setSceneNode(node);
        pump();
        CHECK(row && row->getWidget() && row->getWidget()->currentIndex() == 0,
              "movement: a driven node still shows its SETTING (auto), not the answer");
        CHECK(MobilityPropertyWidget::resolvedText(node)
                  == QStringLiteral("Moves - it is a physics object"),
              "movement: ...and the result line explains what drives it");
        node->isPhysicsBody = false;
        node->removeFromParent();
    }

    // ---- 6c. THE CAST SHADOW ROW (CLEANUP-1 item 4) ------------------------
    // A control the UI promised and did not have. The light panel's Lighting
    // Channels row has told authors since it shipped to "turn off Cast Shadow
    // on the object itself" — and there was no Cast Shadow row anywhere in
    // src/ui: the only door was node.setCastShadow from a script, which is the
    // API-first rule inverted. Same reflected key (`castShadow`), so the row,
    // node.setProperty and node.setCastShadow are one code path and one step.
    {
        auto mesh = iris::MeshNode::create();
        scene->getRootNode()->addChild(mesh, false);
        MeshPropertyWidget panel;
        panel.setServices(&services);
        const int before = stack.index();
        panel.setSceneNode(mesh);        // SELECTING a node is not an edit
        pump();
        CHECK(stack.index() == before, "cast shadow: selecting a mesh records nothing");
        CHECK(mesh->getShadowCastingEnabled(),
              "cast shadow: ...and did not write the row's value back into it");

        CheckBoxWidget *row = checkWith(&panel, QStringLiteral("Cast Shadow"));
        CHECK(row != nullptr, "cast shadow: THE ROW EXISTS on the mesh blade");
        CHECK(row && row->getValue(),
              "cast shadow: the row REFLECTS the document (a new mesh casts shadows)");

        if (auto *b = box(row)) b->setChecked(false);
        CHECK(!mesh->getShadowCastingEnabled(),
              "cast shadow: unticking it wrote through to the document");
        CHECK(stack.index() == before + 1, "cast shadow: as ONE undo step");
        stack.undo();
        pump();
        CHECK(mesh->getShadowCastingEnabled(), "cast shadow: undone");
        stack.redo();
        pump();
        CHECK(!mesh->getShadowCastingEnabled(), "cast shadow: redone");
        stack.undo();
        pump();

        // AND THE ROW FOLLOWS A NODE THAT ALREADY HAS IT OFF: the panel is
        // where an author discovers the state, not only where they change it.
        mesh->setShadowCastingEnabled(false);
        panel.setSceneNode(mesh);
        pump();
        CHECK(row && !row->getValue(), "cast shadow: the row shows a node that is already off");
        CHECK(stack.index() == before, "cast shadow: ...and re-selecting still records nothing");
        mesh->setShadowCastingEnabled(true);
        mesh->removeFromParent();
    }

    // ---- 7. THE PHYSICS SECTION (a struct, not a reflected property) -------
    {
        auto node = iris::SceneNode::create();
        PhysicsPropertyWidget panel;
        panel.setServices(&services);
        const int before = stack.index();
        panel.setSceneNode(node);
        CHECK(stack.index() == before, "physics: selecting a node records nothing");

        HFloatSliderWidget *mass = sliderWith(&panel, QStringLiteral("Object Mass"));
        CHECK(mass != nullptr, "physics: the mass row is on the blade");
        const float was = node->physicsProperty.objectMass;
        CHECK(drag(mass, 4.0f, 9.0f), "physics: the mass row can be dragged");
        CHECK(qFuzzyCompare(node->physicsProperty.objectMass, 9.0f),
              "physics: the drag wrote the body settings live");
        CHECK(stack.index() == before + 1, "physics: ONE step for the drag");
        stack.undo();
        CHECK(qFuzzyCompare(node->physicsProperty.objectMass, was), "physics: undone");

        // THE TYPE ROW WRITES WHAT IT MEANS. It used to build a default
        // PhysicsProperty from four widget values and assign it whole, wiping
        // the collision shape (and friction, damping, centre of mass, pivot) —
        // fields this row does not edit and the undo step cannot restore.
        node->physicsProperty.shape = iris::PhysicsCollisionShape::Sphere;
        node->physicsProperty.objectFriction = 0.75f;
        ComboBoxWidget *type = comboWith(&panel, QStringLiteral("Physics Type"));
        CHECK(type != nullptr, "physics: the type row is on the blade");
        const int steps = stack.index();
        if (type && type->getWidget()) type->getWidget()->setCurrentIndex(2);   // Rigid Body
        CHECK(node->physicsProperty.type == iris::PhysicsType::RigidBody,
              "physics: picking a type wrote it through");
        CHECK(node->physicsProperty.shape == iris::PhysicsCollisionShape::Sphere &&
                  qFuzzyCompare(node->physicsProperty.objectFriction, 0.75f),
              "physics: and left the shape and the friction it does not edit ALONE");
        CHECK(stack.index() == steps + 1, "physics: as one step");
        stack.undo();
        CHECK(node->physicsProperty.type != iris::PhysicsType::RigidBody &&
                  node->physicsProperty.shape == iris::PhysicsCollisionShape::Sphere,
              "physics: undone, shape still intact");
    }

    // ---- 8. THE EMITTER SECTION --------------------------------------------
    {
        auto emitter = iris::ParticleSystemNode::create();
        EmitterPropertyWidget panel;
        panel.setServices(&services);
        const int before = stack.index();
        panel.setSceneNode(emitter);
        CHECK(stack.index() == before, "emitter: selecting an emitter records nothing");

        HFloatSliderWidget *rate = sliderWith(&panel, QStringLiteral("Emission Rate"));
        CHECK(rate != nullptr, "emitter: the rate row is on the blade");
        const float was = emitter->particlesPerSecond;
        CHECK(drag(rate, 40.0f, 90.0f), "emitter: the rate row can be dragged");
        CHECK(qFuzzyCompare(emitter->particlesPerSecond, 90.0f), "emitter: wrote through live");
        CHECK(stack.index() == before + 1, "emitter: ONE step for the drag");
        stack.undo();
        CHECK(qFuzzyCompare(emitter->particlesPerSecond, was), "emitter: undone");

        // A PRESET stamps a whole recipe: one step, and the undo puts back what
        // the user had — not the previous preset's numbers.
        const int steps = stack.index();
        emitter->particlesPerSecond = 123.0f;
        panel.setSceneNode(emitter);          // re-read the hand-tuned value
        ComboBoxWidget *preset = comboWith(&panel, QStringLiteral("Preset"));
        CHECK(preset != nullptr, "emitter: the preset row is on the blade");
        if (preset && preset->getWidget()) preset->getWidget()->setCurrentIndex(1);   // fire
        CHECK(!qFuzzyCompare(emitter->particlesPerSecond, 123.0f),
              "emitter: the preset rewrote the recipe");
        CHECK(stack.index() == steps + 1, "emitter: as ONE step");
        stack.undo();
        CHECK(qFuzzyCompare(emitter->particlesPerSecond, 123.0f),
              "emitter: and the undo restored the HAND-TUNED value, not a preset's");
    }

    // ---- 9. THE PANEL HOST FORWARDS THE LIBRARY ----------------------------
    //
    // Every child panel is built in the host's CONSTRUCTOR, before anything has
    // handed it a Database — so a panel that only got the pointer there has
    // none, forever. That was not theoretical: the sky section's equirect pick
    // and cubemap slots returned early on `!db`, a sky ASSET could not be
    // edited at all, and the emitter's image row dereferenced the null. Every
    // panel suite passed `setDatabase(nullptr)`, which is why the gate never
    // saw it. This drives the row a user drives, through the HOST, with a
    // library present.
    {
        Database db;
        Project project;
        SceneNodePropertiesWidget host;
        host.setDatabase(&db);          // exactly the order MainWindow uses
        host.setProject(&project);
        host.setServices(&services);

        SkyPropertyWidget *sky = host.findChild<SkyPropertyWidget *>();
        CHECK(sky != nullptr, "host: the sky section is one of the host's blades");
        scene->skyType = iris::SkyType::EQUIRECTANGULAR;
        scene->skyData.remove(QStringLiteral("Equirectangular"));
        host.setScene(scene);
        pump();

        TexturePickerWidget *equi = sky ? pickerWith(sky, QStringLiteral("Equi Map")) : nullptr;
        CHECK(equi != nullptr, "host: and it built the equirect row");
        if (equi) {
            const int before = stack.index();
            // What the picker emits when a user drops or chooses an image: the
            // resolved file AND the guid of the row that was picked. The file is
            // a store object, so its NAME is a sha256 that no catalog row is
            // called — which is the point: the pick must reach the document
            // through the CARRIED guid (plan item 15c). It used to look the row
            // up by the file's name, found nothing for any imported image, and
            // silently did nothing; this suite only passed because its stub
            // answered one hard-coded name.
            emit equi->valuesChanged(QStringLiteral("/store/objects/ab/abababab.png"),
                                     QStringLiteral("stub-guid"));
            pump();
            const QJsonObject stored = scene->skyData.value(QStringLiteral("Equirectangular"));
            CHECK(stored.value(QStringLiteral("equiSkyGuid")).toString()
                      == QStringLiteral("stub-guid"),
                  "host: the equirect pick reached the document by the picked row's GUID "
                  "(not by the file's name; and not blocked by a null db, as before)");
            CHECK(stack.index() == before + 1, "host: as one undo step");
            stack.undo();
            pump();
        }

        // The emitter's image row: it used to LOAD A PATH and write the library
        // rows by hand — dereferencing the null db as it went (the SEGV the
        // review found). It binds through SceneEditService::setParticleTexture
        // now, the same door node.setParticleTexture uses, so the row cannot
        // touch the library at all. This suite has no service (the class is not
        // in this slice), so what it pins is the half it can see: the row runs,
        // touches no database, and — with nobody to bind through — records
        // nothing rather than half-binding. The service half rides on the verb's
        // own coverage, because it is now literally the same call.
        auto emitter = iris::ParticleSystemNode::create();
        EmitterPropertyWidget *emitterPanel = host.findChild<EmitterPropertyWidget *>();
        CHECK(emitterPanel != nullptr, "host: the emitter section is one of the host's blades");
        if (emitterPanel) {
            emitterPanel->setSceneNode(emitter);
            TexturePickerWidget *image =
                pickerWith(emitterPanel, QStringLiteral("Particle Image"));
            CHECK(image != nullptr, "host: and it built the particle image row");
            if (image) {
                const int before = stack.index();
                emit image->valuesChanged(QStringLiteral("stub-asset.png"),
                                          QStringLiteral("stub-guid"));
                CHECK(stack.index() == before,
                      "host: the image row records nothing without a scene-edit service "
                      "(and reaches no database on the way — it used to SEGV here)");
            }
        }
    }

    // ---- 10. A WIDE EDIT BELONGS TO ITS NODE, not to the panel -------------
    //
    // The emitter's preset and its two ramps rewrite everything at once, so
    // their undo step carries the panel's whole editable state — and it must
    // carry it for the emitter it was RECORDED for. Selecting another emitter
    // and pressing Ctrl+Z used to stamp the first one's recipe onto the second.
    {
        auto a = iris::ParticleSystemNode::create();
        auto b = iris::ParticleSystemNode::create();
        a->particlesPerSecond = 11.0f;
        b->particlesPerSecond = 77.0f;

        EmitterPropertyWidget panel;
        panel.setServices(&services);
        panel.setSceneNode(a);
        ComboBoxWidget *preset = comboWith(&panel, QStringLiteral("Preset"));
        CHECK(preset != nullptr, "identity: the preset row is on the blade");
        const int before = stack.index();
        if (preset && preset->getWidget()) preset->getWidget()->setCurrentIndex(1);   // fire
        CHECK(stack.index() == before + 1, "identity: the preset stamped one step on A");
        CHECK(!qFuzzyCompare(a->particlesPerSecond, 11.0f), "identity: and rewrote A");

        // The user moves on to another emitter, then undoes.
        panel.setSceneNode(b);
        stack.undo();
        CHECK(qFuzzyCompare(a->particlesPerSecond, 11.0f),
              "identity: undo restored the emitter the step was recorded for");
        CHECK(qFuzzyCompare(b->particlesPerSecond, 77.0f),
              "identity: and left the SELECTED emitter alone");
    }

    // ---- THE TRANSFORM ROWS ALL SCRUB (owner report, 2026-09-14) -----------
    // "I can click and drag the Position and Scale boxes (X/Y/Z) to change
    // them, but I can't click and drag the Rotation box."
    //
    // All nine fields are the same widget with the same gesture (DragSpinBox),
    // so the claim worth pinning is per FIELD and on the DOCUMENT: a drag of a
    // known length moves that component of the transform by a known amount,
    // and the value SURVIVES the release (the release rewinds the node and
    // pushes one undo step — a row with no services silently reverts).
    {
        auto node = iris::SceneNode::create();
        node->setName("scrubbed");
        TransformEditor editor;
        editor.setServices(&services);
        editor.setSceneNode(node);
        editor.resize(420, 200);
        editor.show();
        pump();

        struct Field { const char *name; const char *label; };
        static const Field fields[] = {
            { "xpos", "Position X" },   { "ypos", "Position Y" },   { "zpos", "Position Z" },
            { "xrot", "Rotation X" },   { "yrot", "Rotation Y" },   { "zrot", "Rotation Z" },
            { "xscale", "Scale X" },    { "yscale", "Scale Y" },    { "zscale", "Scale Z" },
        };
        const int dx = 20;              // 0.4 units of position/scale, 10 degrees of rotation
        for (const Field &field : fields) {
            auto *box = editor.findChild<DragSpinBox *>(QLatin1String(field.name));
            CHECK(box != nullptr,
                  qPrintable(QStringLiteral("transform: the %1 field is on the panel")
                                 .arg(QLatin1String(field.label))));
            if (!box) continue;
            const iris::Vec3 pos = node->getLocalPos();
            const iris::Vec3 scale = node->getLocalScale();
            const iris::Vec3 rot = node->getLocalRot().toEulerAngles();
            const double before = box->value();
            const int steps = stack.index();

            const double live = scrub(box, dx);
            pump();
            const iris::Vec3 nowRot = node->getLocalRot().toEulerAngles();
            const iris::Vec3 nowPos = node->getLocalPos();
            const iris::Vec3 nowScale = node->getLocalScale();
            std::printf("    %s: %.4f -> %.4f (live), node pos %.4f/%.4f/%.4f rot %.3f/%.3f/%.3f "
                        "scale %.4f/%.4f/%.4f\n",
                        field.label, before, live, nowPos.x(), nowPos.y(), nowPos.z(),
                        nowRot.x(), nowRot.y(), nowRot.z(),
                        nowScale.x(), nowScale.y(), nowScale.z());
            CHECK(qAbs(live - before) > 1e-3,
                  qPrintable(QStringLiteral("transform: a drag on %1 moves the FIELD")
                                 .arg(QLatin1String(field.label))));

            // …and the DOCUMENT: the component that field owns, and only it.
            const double movedPos = QVector3D(nowPos.x() - pos.x(), nowPos.y() - pos.y(),
                                              nowPos.z() - pos.z()).length();
            const double movedScale = QVector3D(nowScale.x() - scale.x(), nowScale.y() - scale.y(),
                                                nowScale.z() - scale.z()).length();
            const double movedRot = QVector3D(nowRot.x() - rot.x(), nowRot.y() - rot.y(),
                                              nowRot.z() - rot.z()).length();
            const QString row = QString::fromLatin1(field.label).section(' ', 0, 0);
            const double moved = row == QLatin1String("Position") ? movedPos
                               : row == QLatin1String("Scale")    ? movedScale
                                                                  : movedRot;
            CHECK(moved > 1e-3,
                  qPrintable(QStringLiteral("transform: …and the node's %1 with it (moved %2)")
                                 .arg(QLatin1String(field.label)).arg(moved)));
            CHECK(stack.index() == steps + 1,
                  qPrintable(QStringLiteral("transform: …in ONE undo step (%1)")
                                 .arg(QLatin1String(field.label))));
            CHECK(qAbs(box->value() - live) < 1e-3,
                  qPrintable(QStringLiteral("transform: …and the value SURVIVES the release (%1)")
                                 .arg(QLatin1String(field.label))));
        }
    }

    // ---- THE ROTATION FIELDS ARE THE ROTATION, AT GIMBAL LOCK TOO ----------
    // The owner's "I can't drag the Rotation box", found on the rig: the three
    // callbacks re-derived the euler triple from the node's QUATERNION on every
    // tick, and a quaternion does not remember which triple built it. At a
    // pitch of +/-90 degrees — every flat plane, image plane and decal, and the
    // imported models that arrive rotated -90 on X — the decomposition that
    // came back was a DIFFERENT triple, so the edit landed elsewhere: measured
    // before the fix, a 20-degree drag of Z at (-90, 0, 0) moved Y by 90 and
    // left Z at 0 (the field snapped back and the panel read as dead), and a
    // drag of X from 89 went DOWN to 71.
    //
    // What the panel shows is what the document gets. Asserted on the
    // QUATERNION, since that is the thing that must match — comparing euler
    // triples would be comparing two decompositions.
    {
        auto node = iris::SceneNode::create();
        TransformEditor editor;
        editor.setServices(&services);
        editor.setSceneNode(node);
        editor.resize(420, 200);
        editor.show();
        pump();
        auto *xrot = editor.findChild<DragSpinBox *>("xrot");
        auto *yrot = editor.findChild<DragSpinBox *>("yrot");
        auto *zrot = editor.findChild<DragSpinBox *>("zrot");
        CHECK(xrot && yrot && zrot, "gimbal: the three rotation fields are on the panel");

        auto sameQuat = [](const iris::Quat &a, const iris::Quat &b) {
            // q and -q are the same rotation
            const float dot = iris::Quat::dotProduct(a, b);
            return qAbs(qAbs(dot) - 1.0f) < 1e-3f;
        };

        if (xrot && yrot && zrot) {
            // A node lying flat: the case a plane, an image plane, a decal and
            // most imported models are in the moment they reach the scene.
            node->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(-90, 0, 0)));
            editor.refreshUi();
            pump();
            const double zBefore = zrot->value();
            const double zLive = scrub(zrot, 40);       // +20 degrees
            pump();
            std::printf("    gimbal: Z %.3f -> %.3f, node euler %.3f/%.3f/%.3f\n", zBefore, zLive,
                        node->getLocalRot().toEulerAngles().x(),
                        node->getLocalRot().toEulerAngles().y(),
                        node->getLocalRot().toEulerAngles().z());
            CHECK(sameQuat(node->getLocalRot(),
                           iris::Quat::fromEulerAngles(iris::Vec3(-90, 0, float(zLive)))),
                  "gimbal: at pitch -90, dragging Z rotates the node by what the FIELDS say");
            CHECK(qAbs(zrot->value() - zLive) < 1e-3,
                  "gimbal: …and the Z field keeps the value the user dragged to");
            // …INCLUDING THROUGH A REFRESH. Anything that refreshes the panel
            // (the gizmo's transformRefreshRequested, a re-bind) used to
            // rewrite the row with the quaternion's canonical triple, which at
            // gimbal lock is a different one: on the rig the Z field snapped
            // back to 0 and the 20 degrees appeared under Y.
            editor.refreshUi();
            pump();
            std::printf("    gimbal: after a refresh the row reads %.3f/%.3f/%.3f\n",
                        xrot->value(), yrot->value(), zrot->value());
            CHECK(qAbs(zrot->value() - zLive) < 1e-3 && qAbs(yrot->value()) < 1e-3,
                  "gimbal: …and a refresh of the panel does not move it to another triple");

            // Past +/-90: the drag must keep going the way the user is pulling.
            node->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(89, 0, 0)));
            editor.refreshUi();
            pump();
            const double xLive = scrub(xrot, 40);       // 89 -> 109
            pump();
            std::printf("    gimbal: X 89 -> %.3f\n", xLive);
            CHECK(xLive > 100.0,
                  "gimbal: a drag from 89 degrees carries ON past 90 (it used to reverse)");
            CHECK(sameQuat(node->getLocalRot(),
                           iris::Quat::fromEulerAngles(iris::Vec3(float(xLive), 0, 0))),
                  "gimbal: …and the node is rotated to the angle the field shows");
        }

        // NO DEAD BAND AROUND THE ROW (round 2). The rule is "the row keeps its
        // triple while the document holds exactly what the row built", and the
        // first cut wrote it as a TOLERANCE on a quaternion dot product — 1e-4
        // of |dot| is 1.62 degrees, so a small rotation from anywhere else (a
        // script, an MCP client, the gizmo, an animation) left the panel
        // showing the old numbers.
        {
            node->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(0, 0, 0)));
            editor.refreshUi();
            pump();
            // …somebody else rotates it by ONE degree: node.transform's job.
            node->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(0, 1, 0)));
            editor.refreshUi();
            pump();
            std::printf("    dead band: after a 1 degree write the row reads %.3f/%.3f/%.3f\n",
                        xrot->value(), yrot->value(), zrot->value());
            CHECK(qAbs(yrot->value() - 1.0) < 1e-2,
                  "no dead band: a 1-degree rotation from anywhere else moves the row");

            // A NEW SELECTION ALWAYS SHOWS ITS OWN TRIPLE, even one 1.5 degrees
            // from the node that was selected before it.
            auto other = iris::SceneNode::create();
            other->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(0, 2.5, 0)));
            editor.setSceneNode(other);
            pump();
            std::printf("    dead band: selecting a node 1.5 degrees away reads %.3f/%.3f/%.3f\n",
                        xrot->value(), yrot->value(), zrot->value());
            CHECK(qAbs(yrot->value() - 2.5) < 1e-2,
                  "no dead band: selecting a node 1.5 degrees from the last one shows ITS triple");
            editor.setSceneNode(node);
            pump();
        }

        // RESET still resets — it used to drive the same row callbacks, which
        // read the fields now (they still hold the old angles at that moment).
        node->setLocalRot(iris::Quat::fromEulerAngles(iris::Vec3(20, 30, 40)));
        node->setLocalPos(iris::Vec3(1, 2, 3));
        editor.refreshUi();
        pump();
        if (auto *reset = editor.findChild<QPushButton *>("resetBtn")) reset->click();
        pump();
        const iris::Vec3 afterReset = node->getLocalRot().toEulerAngles();
        std::printf("    reset: euler %.3f/%.3f/%.3f pos %.3f/%.3f/%.3f\n", afterReset.x(),
                    afterReset.y(), afterReset.z(), node->getLocalPos().x(),
                    node->getLocalPos().y(), node->getLocalPos().z());
        CHECK(qAbs(afterReset.x()) < 1e-2 && qAbs(afterReset.y()) < 1e-2
                  && qAbs(afterReset.z()) < 1e-2,
              "reset: the Reset button puts the rotation back to zero");
        CHECK(node->getLocalPos().x() == 0 && node->getLocalPos().y() == 0
                  && node->getLocalPos().z() == 0,
              "reset: …and the position with it");
    }

    // ---- THE EDIT GATE: NON-EDITABLE WHILE A SCRIPT RUNS --------------------
    //
    // Owner, ledger §423. The script engine runs off the UI thread, so the
    // event loop turns between two verbs and a person can reach these very rows
    // in the middle of somebody else's edit — and the run is ONE open undo
    // entry, so a hand edit would silently join it. While a run is in flight
    // every document write arriving from the UI is refused (services/editgate.h)
    // and NAVIGATION is untouched.
    //
    // What is asserted here is the part a state flag cannot show: after a full
    // gesture on a real row, and after a full scrub on a real transform field,
    // the DOCUMENT IS BYTE-IDENTICAL and the stack has not moved — because
    // these rows write live and only push at the end, so a refusal that only
    // stopped the push would leave the dragged value behind.
    {
        int notices = 0;
        editgate::setNoticeHook([&notices]() { ++notices; });
        editgate::runStarted();          // what ScriptEngine::evaluate does

        // 1. A SLIDER ROW (rowundo, i.e. every panel's generic rows).
        FogPropertyWidget panel;
        panel.setServices(&services);
        panel.setScene(scene);
        pump();
        HFloatSliderWidget *density = sliderWith(&panel, QStringLiteral("Fog Density"));
        CHECK(density != nullptr, "gate: the density row is on the blade");
        const float fogWas = scene->fogDensity;
        const int stackWas = stack.index();
        CHECK(drag(density, 0.02f, 0.09f), "gate: the row can still be dragged (nothing is disabled)");
        CHECK(qFuzzyCompare(scene->fogDensity, fogWas),
              "gate: ...and the document did not move by a single tick");
        CHECK(stack.index() == stackWas, "gate: ...and nothing reached the undo stack");

        // 2. A CHECKBOX — a one-shot row, the other half of rowundo.
        const bool fogEnabledWas = scene->fogEnabled;
        if (auto *b = box(checkWith(&panel, QStringLiteral("Fog Enabled"))))
            b->setChecked(!fogEnabledWas);
        CHECK(scene->fogEnabled == fogEnabledWas, "gate: a checkbox row writes nothing either");
        CHECK(stack.index() == stackWas, "gate: ...and records nothing");

        // 3. A TRANSFORM FIELD — its own gesture, not a rowundo binding: it
        //    writes the node live through the scrub and records on release.
        auto node = iris::SceneNode::create();
        node->setLocalPos(iris::Vec3(1, 2, 3));
        TransformEditor editor;
        editor.setServices(&services);
        editor.setSceneNode(node);
        pump();
        auto *xpos = editor.findChild<DragSpinBox *>(QStringLiteral("xpos"));
        CHECK(xpos != nullptr, "gate: the transform panel's X field is there");
        scrub(xpos, 40);
        CHECK(qFuzzyCompare(node->getLocalPos().x(), 1.0f),
              "gate: a full scrub of a transform field left the node where it was");
        CHECK(stack.index() == stackWas, "gate: ...and pushed no step");

        // 4. A PLAIN COMMAND — the Delete key, a paste, a menu action that
        //    edits: everything whose work happens in the command's redo().
        bool applied = false;
        undo.push(new NodeEditCommand(QStringLiteral("a hand edit"),
                                      [&applied]() { applied = true; },
                                      [&applied]() { applied = false; }));
        CHECK(!applied, "gate: a command pushed by hand never ran");
        CHECK(stack.index() == stackWas, "gate: ...and never reached the stack");

        // 5. THE NOTICE: once per run, however many edits were refused.
        CHECK(notices == 1, "gate: the run's notice was raised ONCE, not once per refused event");
        CHECK(editgate::refusals() >= 4, "gate: every refusal was counted");

        // 6. AND AFTERWARDS THE SAME EDITS WORK.
        editgate::runFinished();
        CHECK(!editgate::runActive(), "gate: the run gave the document back");
        CHECK(drag(density, 0.02f, 0.09f), "gate: the same row drags again");
        CHECK(qFuzzyCompare(scene->fogDensity, 0.09f), "gate: ...and writes the document");
        CHECK(stack.index() == stackWas + 1, "gate: ...and records its one step");
        undo.push(new NodeEditCommand(QStringLiteral("a hand edit"),
                                      [&applied]() { applied = true; },
                                      [&applied]() { applied = false; }));
        CHECK(applied && stack.index() == stackWas + 2, "gate: ...and a command pushes and runs");
        editgate::setNoticeHook({});
        editgate::reset();
    }

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
