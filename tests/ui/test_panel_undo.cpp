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
#include <QUndoStack>

#include <cstdio>

#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "data/database/database.h"
#include "data/project.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "services/worldmodes.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/panels/propertywidgets/emitterpropertywidget.h"
#include "ui/panels/propertywidgets/fogpropertywidget.h"
#include "ui/panels/propertywidgets/lightpropertywidget.h"
#include "ui/panels/propertywidgets/physicspropertywidget.h"
#include "ui/panels/propertywidgets/skypropertywidget.h"
#include "ui/panels/propertywidgets/worldaapropertywidget.h"
#include "ui/panels/propertywidgets/worldpostfxpropertywidget.h"
#include "ui/panels/propertywidgets/worldpropertywidget.h"
#include "ui/panels/propertywidgets/worldshadowpropertywidget.h"
#include "ui/panels/scenenodepropertieswidget.h"
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
        if (combo && combo->getWidget()) combo->getWidget()->setCurrentIndex(1);   // 2x
        CHECK(scene->antiAliasing == 2, "msaa: the pick wrote the document");
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

        HFloatSliderWidget *azimuth = sliderWith(&panel, QStringLiteral("Sun Azimuth"));
        CHECK(azimuth != nullptr, "sky: the realistic sky's sun rows are on the blade");
        const float was = scene->skyRealistic.sunAzimuth();
        CHECK(drag(azimuth, 120.0f, 200.0f), "sky: the azimuth row can be dragged");
        CHECK(qAbs(scene->skyRealistic.sunAzimuth() - 200.0f) < 0.5f,
              "sky: the drag moved the sun live");
        CHECK(stack.index() == before + 1, "sky: as ONE step");
        stack.undo();
        pump();
        CHECK(qAbs(scene->skyRealistic.sunAzimuth() - was) < 0.5f,
              "sky: undo restored the sun (the whole sky block travels together)");
        // AN UNBRACKETED TICK IS ITS OWN STEP. A keyboard arrow or a typed
        // value arrives as a bare valueChanged, and the row's write must run
        // INSIDE the binding — when the panel also connected the writing slot
        // directly, the document was already written by the time the gesture
        // snapshotted it, so after == before and the edit vanished from the
        // history (code review).
        // The undo above REPAINTED the section (the rows are the sky), so the
        // slider from before it is gone — ask for the row again, which is also
        // the cheapest proof that the repaint happened.
        HFloatSliderWidget *azimuth2 = sliderWith(&panel, QStringLiteral("Sun Azimuth"));
        CHECK(azimuth2 != nullptr && azimuth2 != azimuth,
              "sky: an undo rebuilt the section's rows");
        if (azimuth2) {
            const int steps = stack.index();
            azimuth2->setValue(60.0f);
            pump();
            CHECK(qAbs(scene->skyRealistic.sunAzimuth() - 60.0f) < 0.5f,
                  "sky: an unbracketed tick writes through");
            CHECK(stack.index() == steps + 1, "sky: and is its own one-step edit");
            stack.undo();
            pump();
        }

        // The blob the scene SAVES is the same three floats: an undo that put
        // back the live field and not the blob would reappear on the next open.
        const QJsonObject stored = scene->skyData.value(QStringLiteral("Realistic"));
        CHECK(qAbs(stored.value(QStringLiteral("sunPosX")).toDouble()
                       - double(scene->skyRealistic.sunPosX)) < 1.0,
              "sky: and the serialized blob agrees with the live field");
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
            // What the picker emits when a user drops or chooses an image.
            emit equi->valueChanged(QStringLiteral("stub-asset.png"));
            pump();
            const QJsonObject stored = scene->skyData.value(QStringLiteral("Equirectangular"));
            CHECK(stored.value(QStringLiteral("equiSkyGuid")).toString()
                      == QStringLiteral("stub-guid"),
                  "host: the equirect pick RESOLVED through the library and reached the "
                  "document (it returned on a null db before)");
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

    std::printf(failures ? "\nFAILED: %d check(s)\n" : "\nALL CHECKS PASSED\n", failures);
    return failures ? 1 : 0;
}
