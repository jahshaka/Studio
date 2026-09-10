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
//     showing a sky the renderer is not drawing), and the section carries the
//     two world rows a sky has: Sky Detail and Ambient From Sky.
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
#include "ui/panels/propertywidgets/panelundo.h"

#include "irisgl/document/scenegraph/scene.h"

#include <QJsonDocument>
#include <QJsonObject>
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

public slots:
    void skyTypeChanged(int index);
    void onSlotChanged(QString value, QString guid, int index);

protected slots:
    void setEquiMap(const QString &guid);
    void setSkyMap(const QJsonObject& definition);
    void onSingleSkyColorChanged(QColor color);
    void onEquiTextureChanged(QString guid);

    void onReileighChanged(float val);
    void onLuminanceChanged(float val);
    void onTurbidityChanged(float val);
    void onMieCoeffGChanged(float val);
    void onMieDireChanged(float val);
    void onSunAzimuthChanged(float val);
    void onSunElevationChanged(float val);
    void onSkyDetailChanged(int row);
    void onAmbientFromSkyChanged(bool on);
    void onSunDrivesLightChanged(bool on);

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
    void addSunLinkRow();
    /// Adds the "Ambient From Sky" row for sky types that have something to
    /// integrate; single-colour skies always use the flat Ambient Color.
    void addAmbientFromSkyRow();
	/// Pushes the two angle sliders into the sun vector and the serialized blob
	/// (they are one and the same three floats).
	void writeSunAngles();
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

    ComboBoxWidget *skySelector = nullptr;

    ColorValueWidget *singleColor = nullptr;

    TexturePickerWidget *equiTexture = nullptr;

    ColorValueWidget *colorTop = nullptr;
    ColorValueWidget *colorMid = nullptr;
    ColorValueWidget *colorBot = nullptr;
    HFloatSliderWidget *offset = nullptr;

    HFloatSliderWidget *luminance = nullptr;
    HFloatSliderWidget *reileigh = nullptr;
    HFloatSliderWidget *mieCoefficient = nullptr;
    HFloatSliderWidget *mieDirectionalG = nullptr;
    HFloatSliderWidget *turbidity = nullptr;
    // The sun is a polar control (VISUAL_PARITY_SPEC item 1): the document
    // still stores sunPosX/Y/Z, these two are the readable view of them.
    HFloatSliderWidget *sunAzimuth = nullptr;
    HFloatSliderWidget *sunElevation = nullptr;
    ComboBoxWidget *skyDetail = nullptr;          // realistic-sky bake width
    CheckBoxWidget *sunDrivesLight = nullptr;     // sun coupling (re-audit F5)
    CheckBoxWidget *ambientFromSky = nullptr;     // sky-driven ambient (item 3b)

	QJsonObject singleColorDefinition;
	QJsonObject cubeMapDefinition;
	QJsonObject equiSkyDefinition;
	QJsonObject gradientDefinition;
	QJsonObject realisticDefinition;

	class CubeMapWidget *cubeMapWidget = nullptr;
};

#endif // SKYPROPERTYWIDGET_H
