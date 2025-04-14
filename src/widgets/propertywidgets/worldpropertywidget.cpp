/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "worldpropertywidget.h"

#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/materials/defaultskymaterial.h"

#include "core/project.h"
#include "core/database/database.h"

#include "../texturepickerwidget.h"
#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../hfloatsliderwidget.h"
#include "../checkboxwidget.h"
#include "../comboboxwidget.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "globals.h"
#include "io/scenereader.h"

WorldPropertyWidget::WorldPropertyWidget()
{
    this->setPanelTitle("World Settings");

	worldGravity = this->addFloatValueSlider("Gravity", 0.f, 48.f);
    ambientColor = this->addColorPicker("Ambient Color");

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

void WorldPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;

        ambientColor->setColorValue(scene->ambientColor);
		worldGravity->setValue(scene->gravity);

		auto musicFilesAvailableFromDatabase = db->fetchAssetsByType(static_cast<int>(ModelTypes::Music));

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
	auto asset = db->fetchAsset(currentGuid);
	auto name = asset.name;
	QString fullPathToAudio = IrisUtils::join(Globals::project->getProjectFolder(), name);

	// Start playing here
	scene->ambientMusicGuid = currentGuid;
	scene->setAmbientMusic(fullPathToAudio);
	scene->startPlayingAmbientMusic();
}

void WorldPropertyWidget::onAmbientMusicVolumeChanged(float volume)
{
	scene->setAmbientMusicVolume(volume);
}

