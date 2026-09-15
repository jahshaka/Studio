/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetcas.h"
#include "ui/panels/propertyrows.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>
#include "ui/panels/propertywidgets/worldpropertywidget.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/core/irisutils.h"

#include "data/project.h"
#include "data/database/database.h"

#include "ui/controls/texturepickerwidget.h"
#include "ui/controls/colorvaluewidget.h"
#include "ui/controls/colorpickerwidget.h"
#include "ui/controls/hfloatsliderwidget.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/dragvaluewidgets.h"
#include "ui/controls/comboboxwidget.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "io/scenereader.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "ui/panels/propertywidgets/rowundo.h"

// Every row on this blade writes ONE world property and is undoable through
// ScenePropertyCommand (debt L6 / N5): ambient and gravity as one step per
// gesture, the play mode and the background clip as one step per choice. The
// keys are the sceneprops table's — the same fields world.ambient,
// world.gravity and scene.playMode write, so no verb was needed for any of them.
// The Ground Grid row is deliberately NOT here: it is a face of the View
// Options action and the per-scene EditorData flag, not a document property.
WorldPropertyWidget::WorldPropertyWidget()
    : rows([this]() { return scene; }, [this]() { return services; },
           [this]() { refreshRows(); }, [this]() { return !loading; })
{
    this->setPanelTitle("World Settings");

	worldGravity = this->addFloatValueSlider("Gravity", 0.f, 48.f);
    // (The "Ambient Color" picker that stood here is GONE — SKY_LIGHT_SPEC.md
    // §5, owner decision D14. Ambient is a LIGHT now: a Sky Light node, with
    // its own Intensity and Tint rows in the light panel. A flat colour that
    // lit everything from nowhere was never a thing in the world.)

    // THE SUN DISC (SKY_LIGHT_SPEC.md §3, owner picks 2 and 4): a world
    // setting, because the disc is part of the world's picture like the sky it
    // is drawn on — its SIZE included, since the drawn disc is a picture of the
    // sun and not the sun's geometry (lane SUN-DISC-1 retired the light
    // panel's Sun Angle row).
    sunDiscVisible = this->addCheckBox("Sun Disc", true);
    sunDiscVisible->setToolTip(QStringLiteral(
        "Draw the sun as a bright disc in the sky, where the scene's sun light points. NOTE for "
        "image skies: an equirectangular or cubemap sky usually has a sun painted into it — aim "
        "the sun light at that painted sun, or turn this off, or the scene shows two suns."));
    sunDiscInProbes = this->addCheckBox("Sun Disc in Reflections", false);
    sunDiscInProbes->setToolTip(QStringLiteral(
        "Include the sun disc in the reflection probes' captures. Off by default: the sun's "
        "light already reaches shiny surfaces through the sun light's own highlight, so a "
        "captured disc paints a SECOND sun on everything the probes light. Mirrors (planar "
        "reflections) always show the disc."));
    sunDiscSize = this->addDragFloat("Sun Disc Size", double(iris::kDefaultSunDiscSize),
                                     double(iris::kMinSunDiscSize),
                                     double(iris::kMaxSunDiscSize), 0.01, 2);
    sunDiscSize->setToolTip(QStringLiteral(
        "How wide the sun disc is drawn, in degrees. The real sun is 0.53 degrees across, but a "
        "photograph's sun looks larger than that because glare spreads its saturated core — so "
        "the default is 2.12, four times the physical angle. Making it wider costs no light: the "
        "disc's brightness falls with its area, so the same amount of sun is spread over more of "
        "the sky and the scene's lighting does not change."));
    // Constructed OFF to agree with EditorData::showGrid (the three-way default
    // the ui.grid_default gate pins). setGridAction immediately re-reads the
    // real state from the View menu's action, so this only ever shows for the
    // instant before a scene is bound — but a row that starts by disagreeing
    // with the world is exactly how the old default drifted unnoticed.
    showGridToggle = this->addCheckBox("Show Grid", false);

    // WHAT PLAY DOES (AVATAR_LOCOMOTION_SPEC §8.5). It belongs here and not in
    // the World Mode section: world.modeTable is the SCALABILITY registry
    // (Low/Medium/High/Epic), and a gameplay decision that followed the quality
    // tier would be nonsense.
    playModeSelector = this->addComboBox("Play Mode");
    playModeSelector->addItem("Explorer", QString::fromLatin1(
        iris::playModeName(iris::ScenePlayMode::Explorer)));
    playModeSelector->addItem("Third Person", QString::fromLatin1(
        iris::playModeName(iris::ScenePlayMode::ThirdPerson)));
    playModeSelector->addItem("Scene Camera", QString::fromLatin1(
        iris::playModeName(iris::ScenePlayMode::Camera)));
    playModeSelector->setToolTip(QStringLiteral(
        "What pressing Play does with this scene. Explorer is the free camera with nobody "
        "driving a character — what Play always did. Third Person takes over the first avatar "
        "in the scene and puts the camera over its shoulder; a scene with no character falls "
        "back to Explorer and says so in the log. Scene Camera renders through the scene's "
        "active camera. Saved with the scene."));
    // Combo rows carry the mode NAME as item data; the row index means nothing
    // to the document (the enum's ints must stay free to be reordered).
    rowundo::bind(playModeSelector, rows(QStringLiteral("playMode"), tr("Play Mode"),
                                         [this](const QVariant &row) {
        iris::ScenePlayMode mode = iris::ScenePlayMode::Explorer;
        iris::playModeFromName(playModeSelector->getItemData(row.toInt()).toString(), mode);
        return QVariant(int(mode));
    }));

    // HARDWARE RAY TRACING (owner, 2026-09-15; ledger §425). A PROJECT fact,
    // saved with the scene and travelling with it — not an application
    // preference (it was one for two days, and a machine-wide switch meant the
    // same project rendered differently depending on something that was not in
    // it), and not a World Mode row either: the tier table is the SCALABILITY
    // registry, and "what this project was authored for" is not a quality
    // trade a tier switch may overwrite.
    rayTracingSelector = this->addComboBox("Hardware Ray Tracing");
    rayTracingSelector->addItem("Off", QStringLiteral("off"));
    rayTracingSelector->addItem("Auto", QStringLiteral("auto"));
    rayTracingSelector->addItem("On", QStringLiteral("on"));
    rayTracingSelector->setToolTip(QStringLiteral(
        "What this project was authored for. Nothing here can give a machine ray-tracing "
        "hardware it does not have. Auto (the default) uses the hardware wherever it exists and "
        "falls back silently everywhere else, so the same file looks right on a ray-capable "
        "desktop and on a Mac. Off never traces, even where the GPU can — for a scene that must "
        "look and cost the same on every machine. On renders exactly like Auto and additionally "
        "tells you, in the scene errors above the viewport, when the machine you are on has no "
        "ray tracing and is therefore not showing you what you built. Saved with the scene."));
    PropertyRows::describe(rayTracingSelector,
                           { QStringLiteral("ray tracing"), QStringLiteral("raytracing"),
                             QStringLiteral("rt"), QStringLiteral("rays"),
                             QStringLiteral("hardware"), QStringLiteral("reflections"),
                             QStringLiteral("rendering") });

	ambientMusicSelector = this->addComboBox("Background Ambience");
	ambientMusicVolume = this->addFloatValueSlider("Volume", 1, 100, 50);

	connect(ambientMusicSelector,		SIGNAL(currentIndexChanged(int)),
			this,						SLOT(onBackgroundAmbienceChanged(int)));

	rowundo::bind(ambientMusicVolume, rows(QStringLiteral("ambientMusicVolume"), tr("Ambience Volume")));
	rowundo::bind(worldGravity, rows(QStringLiteral("gravity"), tr("Gravity")));
	rowundo::bind(sunDiscVisible, rows(QStringLiteral("sunDiscVisible"), tr("Sun Disc")));
	rowundo::bind(sunDiscInProbes, rows(QStringLiteral("sunDiscInProbes"), tr("Sun Disc in Reflections")));
	rowundo::bind(sunDiscSize, rows(QStringLiteral("sunDiscSize"), tr("Sun Disc Size")));
    // The combo carries the STATE NAME as item data, exactly like Play Mode
    // above: the row index means nothing to the document, and the enum's ints
    // stay free to be reordered.
    rowundo::bind(rayTracingSelector, rows(QStringLiteral("rayTracing"),
                                           tr("Hardware Ray Tracing"),
                                           [this](const QVariant &row) {
        iris::RayTracingMode mode = iris::RayTracingMode::Auto;
        iris::rayTracingModeFromName(
            rayTracingSelector->getItemData(row.toInt()).toString(), mode);
        return QVariant(int(mode));
    }));
}

void WorldPropertyWidget::setDatabase(Database *db)
{
	this->db = db;
}

void WorldPropertyWidget::setGridAction(QAction *action)
{
    if (gridAction == action) return;
    gridAction = action;
    if (!gridAction || !showGridToggle) return;

    // action -> row (CheckBoxWidget::setValue does not re-emit valueChanged,
    // so there is no feedback loop); row -> action (QAction::setChecked is a
    // no-op when unchanged, which also runs toggleGrid via its toggled signal)
    showGridToggle->setValue(gridAction->isChecked());
    connect(gridAction, &QAction::toggled, this, [this](bool on) {
        if (showGridToggle) showGridToggle->setValue(on);
    });
    connect(showGridToggle, &CheckBoxWidget::valueChanged, this, [this](bool on) {
        if (gridAction) gridAction->setChecked(on);
    });
}

void WorldPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        refreshRows();
    }
	else {
        this->scene.clear();
    }
}

void WorldPropertyWidget::refreshRows()
{
    if (!scene) return;
    // In place, never a rebuild: these rows outlive every selection, so an undo
    // repaints numbers instead of destroying and re-wiring the blade.
    loading = true;
    sunDiscVisible->setValue(scene->sunDiscVisible);
    sunDiscInProbes->setValue(scene->sunDiscInProbes);
    sunDiscSize->setValue(scene->sunDiscSize);
    worldGravity->setValue(scene->gravity);
    ambientMusicVolume->setValue(scene->ambientMusicVolume);
    playModeSelector->setCurrentItemData(
        QString::fromLatin1(iris::playModeName(scene->getPlayMode())));
    rayTracingSelector->setCurrentItemData(
        QString::fromLatin1(iris::rayTracingModeName(scene->rayTracing)));

    QVector<AssetRecord> musicFilesAvailableFromDatabase;
    if (db && project)
        musicFilesAvailableFromDatabase =
            db->fetchAssetsByType(static_cast<int>(ModelTypes::Music), project->getProjectGuid());

    // THE PANEL'S INTENT, not a raw hide (PROPERTY_FILTER_SPEC §3.3): the row
    // registry ANDs it with the filter's verdict, so a refresh under a live
    // filter can no longer put a filtered-out row back on screen.
    PropertyRows::setPanelVisible(ambientMusicSelector,
                                  !musicFilesAvailableFromDatabase.isEmpty());

    ambientMusicSelector->clear();
    ambientMusicSelector->addItem("None", "");
    for (const auto &music : musicFilesAvailableFromDatabase)
        ambientMusicSelector->addItem(music.name, music.guid);
    ambientMusicSelector->setCurrentItemData(scene->ambientMusicGuid);
    loading = false;
}

void WorldPropertyWidget::applyAmbientMusic(const QString &guid)
{
	if (!scene) return;
	if (guid.isEmpty() || !project) {
		scene->ambientMusicGuid.clear();
		scene->stopPlayingAmbientMusic();
		return;
	}
	// Pin-world resolution: project pin -> library source (phase 4).
	QString fullPathToAudio = AssetCas::resolvePinned(
		QSqlDatabase::database(), AssetStorePaths::root(),
		project->getProjectGuid(), guid);

	scene->ambientMusicGuid = guid;
	scene->setAmbientMusic(fullPathToAudio);
	scene->startPlayingAmbientMusic();
}

void WorldPropertyWidget::onBackgroundAmbienceChanged(int index)
{
	Q_UNUSED(index)
	if (loading || !scene) return;
	const QString before = scene->ambientMusicGuid;
	const QString after = ambientMusicSelector->getCurrentItemData();
	if (before == after) return;
	// The clip is a document field with a SIDE EFFECT (playback), so it does
	// not go through the generic sceneprops write — the undo step replays the
	// same call the row just made, which is what stops the two drifting apart.
	//
	// THE PUSH DOES THE APPLY. NodeEditCommand has no first-redo skip (unlike
	// the value commands), so QUndoStack::push replays redo() immediately — and
	// applying twice here RESTARTS the clip from the top, audibly (code review).
	// With no undo stack (headless hosts, the panel suites) nothing would run
	// at all, so that case applies by hand.
	auto redo = [this, after]() { applyAmbientMusic(after); };
	if (!services || !services->undo) { redo(); return; }
	panelundo::pushEdit(services, tr("Background Ambience"), redo,
	                    [this, before]() { applyAmbientMusic(before); refreshRows(); });
}
