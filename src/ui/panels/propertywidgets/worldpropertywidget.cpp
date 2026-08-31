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

WorldPropertyWidget::WorldPropertyWidget()
{
    this->setPanelTitle("World Settings");

	worldGravity = this->addFloatValueSlider("Gravity", 0.f, 48.f);
    ambientColor = this->addColorPicker("Ambient Color");
    showGridToggle = this->addCheckBox("Show Grid", true);

	ambientMusicSelector = this->addComboBox("Background Ambience");
	ambientMusicVolume = this->addFloatValueSlider("Volume", 1, 100, 50);

	connect(ambientMusicSelector,		SIGNAL(currentIndexChanged(int)),
			this,						SLOT(onBackgroundAmbienceChanged(int)));

	connect(ambientMusicVolume,			SIGNAL(valueChanged(float)),
			this,						SLOT(onAmbientMusicVolumeChanged(float)));

	connect(worldGravity,				SIGNAL(valueChanged(float)),
			this,						SLOT(onGravityChanged(float)));

    connect(ambientColor->getPicker(),  SIGNAL(onColorChanged(QColor)),
            this,                       SLOT(onAmbientColorChanged(QColor)));
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

        ambientColor->setColorValue(scene->ambientColor);
		worldGravity->setValue(scene->gravity);

		auto musicFilesAvailableFromDatabase = db->fetchAssetsByType(static_cast<int>(ModelTypes::Music), project->getProjectGuid());

		if (musicFilesAvailableFromDatabase.isEmpty()) ambientMusicSelector->hide();
		else ambientMusicSelector->show();

		ambientMusicSelector->getWidget()->blockSignals(true);	// don't register initial signals
		ambientMusicSelector->clear();
		ambientMusicSelector->addItem("None", "");
		for (const auto music : musicFilesAvailableFromDatabase) ambientMusicSelector->addItem(music.name, music.guid);
		// If we have a current audio clip set that as the current item
		ambientMusicSelector->setCurrentItemData(scene->ambientMusicGuid);
		ambientMusicSelector->getWidget()->blockSignals(false);
		// If we only have one audio clip, we need to trigger the update function
		// Maybe this is not needed depending on how we want to trigger audio playback
		//if (musicFilesAvailableFromDatabase.count() == 1) onBackgroundAmbienceChanged(0);
    }
	else {
        this->scene.clear();
    }
}

void WorldPropertyWidget::onGravityChanged(float value)
{
	scene->setWorldGravity(value);
}

void WorldPropertyWidget::onAmbientColorChanged(QColor color)
{
    scene->setAmbientColor(color);
}

void WorldPropertyWidget::onBackgroundAmbienceChanged(int index)
{
	if (index == 0) {
		scene->ambientMusicGuid = "";
		scene->stopPlayingAmbientMusic();
		return;
	}

	auto currentGuid = ambientMusicSelector->getCurrentItemData();
	// Pin-world resolution: project pin -> library source (phase 4).
	QString fullPathToAudio = AssetCas::resolvePinned(
		QSqlDatabase::database(), AssetStorePaths::root(),
		project->getProjectGuid(), currentGuid);

	// Start playing here
	scene->ambientMusicGuid = currentGuid;
	scene->setAmbientMusic(fullPathToAudio);
	scene->startPlayingAmbientMusic();
}

void WorldPropertyWidget::onAmbientMusicVolumeChanged(float volume)
{
	scene->setAmbientMusicVolume(volume);
}

