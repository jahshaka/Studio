/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SKYPROPERTYWIDGET_H
#define SKYPROPERTYWIDGET_H

// THE SKY PANEL — one implementation, two bindings (debt L6 / N3).
//
// There were TWO of these for years: WorldSkyPropertyWidget (the World panel's
// Sky section, editing the open scene) and SkyPropertyWidget (the library's sky
// ASSET, editing a database blob). 1,577 lines that were the same panel twice —
// the same five sky types, the same combo table, the same clamp of legacy
// values, the same sun-coupling row, the same six-face cubemap wiring — with
// the differences scattered through both copies. Every sky change had to be
// made twice and, predictably, was not: the asset copy never got the sky-detail
// row, the ambient-from-sky row, the analytic clamp on load or the deferred
// rebuild that stops a combo being destroyed inside its own signal.
//
// This is that panel, once. What differs is a BINDING, named and explicit:
//
//   Binding::Scene — the World panel's section. Rows write the scene's live
//     fields AND its per-type `skyData` blob, every edit is undoable
//     (ScenePropertyCommand over the whole sky block — a sky edit writes the
//     blob and the live field, and undoing half of that leaves the panel
//     showing a sky the renderer is not drawing). It carries no world-mode
//     rows any anymore: "Ambient From Sky" went with D14 and "Sky Detail" with
//     the CPU sky bake (SKY-GPU).
//
//   Binding::Asset — a sky in the library. Rows write the asset's JSON blob,
//     which is flushed to the database when the panel is hidden, and are
//     mirrored onto the scene only while that asset IS the open scene's sky.
//     NOT undoable: the app has no undo history for library ASSET content (no
//     command family reaches the asset tables), and inventing one for this
//     panel alone would have been a second model of the same idea — the very
//     thing this file exists to stop. Recorded as such, not forgotten.

#include <QWidget>
#include <QSharedPointer>
#include "ui/controls/accordionbladewidget.h"
#include "ui/controls/bladerow.h"
#include "ui/panels/propertywidgets/panelundo.h"

#include "irisgl/document/scenegraph/scene.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <functional>
#include <QHideEvent>

class Database;

class ColorValueWidget;
class ColorPickerWidget;
class TexturePicker;

namespace iris {
    class Scene;
    class SceneNode;
    class LightNode;
}

class IEditorViewport;
class Subscriber;
struct StudioServices;

class SkyPropertyWidget: public AccordianBladeWidget
{
    Q_OBJECT

public:
    SkyPropertyWidget();

    /// Injected by the properties panel (Phase 4: was Globals::eventSubscriber).
    Subscriber *eventBus = nullptr;

    /// BINDS TO THE OPEN SCENE (the World panel's Sky section).
    void setScene(QSharedPointer<iris::Scene> scene);
    /// BINDS TO A LIBRARY SKY ASSET (the Assets page's sky item).
    void setSkyAlongWithProperties(const QString &guid, iris::SkyType skyType);

    void setDatabase(Database *);
    void wireViewportEvents(IEditorViewport *viewport);
    /// Selection + undo. Nullable: without it the rows still write, they just
    /// record no step (headless hosts, the panel suites).
    void setServices(StudioServices *s) { this->services = s; }

    void hideEvent(QHideEvent *event) override;

signals:
    /// The Scene binding has (re)built its rows for a sky type — the Clouds
    /// blade re-reads whether the layer may be drawn over it.
    void skyTypeApplied();

public slots:
    void skyTypeChanged(int index);
    void onSlotChanged(QString value, QString guid, int index);

protected slots:
    void setEquiMap(const QString &guid);
    void setSkyMap(const QJsonObject& definition);
    void onSingleSkyColorChanged(QColor color);
    void onEquiTextureChanged(QString guid);

    void onSkyDensityChanged(float val);
    void onSkyDiffusionChanged(float val);
    void onSkyHorizonChanged(float val);
    void onSkyPowerChanged(float val);
    void onSunHazeChanged(float val);

    /// One realistic dial, written through iris::Scene::setSkyRealistic — the
    /// only supported writer of that block (SKY-WRITE-1).
    void writeRealisticDial(const std::function<void(iris::SkyRealistic &)> &edit);
    void onSkyColourChanged(QColor colour);

	void onGradientTopColorChanged(QColor color);
	void onGradientMidColorChanged(QColor color);
	void onGradientBotColorChanged(QColor color);
	void onGradientOffsetChanged(float offset);

private:
    /// Which document this panel is editing.
    enum class Binding { Scene, Asset };

    /// True while the rows are being filled (they emit from their setters).
    bool populating() const { return loading; }
    /// The scene the live fields belong to, or null: the open scene in Scene
    /// binding, and in Asset binding ONLY while the asset is the scene's sky.
    iris::ScenePtr liveScene() const;
    /// The stored definition for a sky type — the scene's skyData block, or the
    /// asset's stored JSON.
    QJsonObject storedDefinition(iris::SkyType type) const;
    /// Flushes the per-type definition back to wherever it came from.
    void updateAssetAndKeys();
    /// Adds the "Drive Selected Directional Light" row (realistic sky only —
    /// no other sky has a sun).
    /// A read-only row naming the light the sky's sun comes from (§3).
    void addSunReadoutRow();
    /// Adds the "Ambient From Sky" row for sky types that have something to
    /// integrate; single-colour skies always use the flat Ambient Color.
	/// Pushes the two angle sliders into the sun vector and the serialized blob
	/// (they are one and the same three floats).
    /// Wires one sky row: `write` is the ONLY path from the control to the
    /// document (a second, direct connect would write before the gesture could
    /// snapshot), and a gesture becomes ONE undo step over the whole sky block
    /// — in a SCENE binding; an asset's rows write and record nothing (see the
    /// file header for why).
    void wireSkyRow(QWidget *row, const QString &text,
                    const std::function<void(const QVariant &)> &write);
    /// Records the sky edit `write` just made, as one step.
    void commitSky(const QVariant &before, const QString &text);

    Database *db = nullptr;
    StudioServices *services = nullptr;
    QSharedPointer<iris::Scene> scene;
    Binding binding = Binding::Scene;
    iris::SkyType currentSky = iris::SkyType::SINGLE_COLOR;
    QString skyGuid;              ///< Asset binding: the library row being edited
    bool loading = false;

    RowPtr<ComboBoxWidget> skySelector;

    RowPtr<ColorValueWidget> singleColor;

    RowPtr<TexturePickerWidget> equiTexture;

    RowPtr<ColorValueWidget> colorTop;
    RowPtr<ColorValueWidget> colorMid;
    RowPtr<ColorValueWidget> colorBot;
    RowPtr<HFloatSliderWidget> offset;

    // The analytic sky's own dials (SKY-GPU): the ENGINE's parameters, not the
    // retired CPU bake's. "Sky Detail" went with the bake — there is nothing
    // to be detailed about a shader.
    RowPtr<HFloatSliderWidget> skyDensity;
    RowPtr<HFloatSliderWidget> skyDiffusion;
    RowPtr<HFloatSliderWidget> skyHorizon;
    RowPtr<HFloatSliderWidget> skyPower;
    /// The SUN's transmittance dial (the atmosphere's turbidity) — a sky-block
    /// row that changes no sky pixel: it colours the direct sunlight.
    RowPtr<HFloatSliderWidget> sunHaze;
    RowPtr<ColorValueWidget> skyColour;
    RowPtr<LabelWidget> sunReadout;            // which light the sky's sun is (§3)

	QJsonObject singleColorDefinition;
	QJsonObject cubeMapDefinition;
	QJsonObject equiSkyDefinition;
	QJsonObject gradientDefinition;
	QJsonObject realisticDefinition;

	RowPtr<class CubeMapWidget> cubeMapWidget;
};

#endif // SKYPROPERTYWIDGET_H
