/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetcas.h"
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
    ambientColor = this->addColorPicker("Ambient Color");
    // With Sky > Ambient From Sky on (the default, VISUAL_PARITY_SPEC item 3b)
    // this colour stops being the ambient and becomes a per-channel GAIN on the
    // sky's own hemisphere integrals. Say so, or the row looks broken.
    ambientColor->setToolTip(QStringLiteral(
        "Flat ambient light. When Sky > Ambient From Sky is on, the ambient colour comes from "
        "the sky instead and this becomes its strength and tint: white = the sky at full "
        "strength, black = no ambient."));
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

	ambientMusicSelector = this->addComboBox("Background Ambience");
	ambientMusicVolume = this->addFloatValueSlider("Volume", 1, 100, 50);

	connect(ambientMusicSelector,		SIGNAL(currentIndexChanged(int)),
			this,						SLOT(onBackgroundAmbienceChanged(int)));

	rowundo::bind(ambientMusicVolume, rows(QStringLiteral("ambientMusicVolume"), tr("Ambience Volume")));
	rowundo::bind(worldGravity, rows(QStringLiteral("gravity"), tr("Gravity")));
	rowundo::bind(ambientColor->getPicker(), rows(QStringLiteral("ambientColor"), tr("Ambient Colour")));
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
    ambientColor->setColorValue(scene->ambientColor);
    worldGravity->setValue(scene->gravity);
    ambientMusicVolume->setValue(scene->ambientMusicVolume);
    playModeSelector->setCurrentItemData(
        QString::fromLatin1(iris::playModeName(scene->getPlayMode())));

    QVector<AssetRecord> musicFilesAvailableFromDatabase;
    if (db && project)
        musicFilesAvailableFromDatabase =
            db->fetchAssetsByType(static_cast<int>(ModelTypes::Music), project->getProjectGuid());

    if (musicFilesAvailableFromDatabase.isEmpty()) ambientMusicSelector->hide();
    else ambientMusicSelector->show();

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
