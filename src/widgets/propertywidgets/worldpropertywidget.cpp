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
    skyColor = this->addColorPicker("Sky Color");
    ambientColor = this->addColorPicker("Ambient Color");

	skySelector = this->addComboBox("Sky");

	connect(skySelector,				SIGNAL(currentIndexChanged(int)),
			this,						SLOT(onSkyChanged(int)));

	connect(worldGravity,				SIGNAL(valueChanged(float)),
			this,						SLOT(onGravityChanged(float)));

    connect(skyColor->getPicker(),      SIGNAL(onColorChanged(QColor)),
            this,                       SLOT(onSkyColorChanged(QColor)));

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

        skyColor->setColorValue(scene->skyColor);
        ambientColor->setColorValue(scene->ambientColor);
		worldGravity->setValue(scene->gravity);

		auto skiesAvailableFromDatabase = db->fetchAssetsByType(static_cast<int>(ModelTypes::Sky));
		skySelector->getWidget()->blockSignals(true);	// don't register initial signals
		skySelector->clear();
		for (const auto sky : skiesAvailableFromDatabase) skySelector->addItem(sky.name, sky.guid);
		skySelector->getWidget()->blockSignals(false);
		// If we have a current sky set that as the current item
		skySelector->setCurrentItemData(scene->skyGuid);
		// If we only have one sky, we need to trigger the update function
		if (skiesAvailableFromDatabase.count() == 1) onSkyChanged(0);
    }
	else {
        this->scene.clear();
    }
}

void WorldPropertyWidget::onGravityChanged(float value)
{
	scene->setWorldGravity(value);
}

void WorldPropertyWidget::onSkyChanged(int index)
{
	Q_UNUSED(index)

	scene->skyGuid = skySelector->getCurrentItemData();
	QJsonDocument skyProperties = QJsonDocument::fromBinaryData(db->fetchAsset(scene->skyGuid).properties);
	QJsonDocument skyData = QJsonDocument::fromBinaryData(db->fetchAssetData(scene->skyGuid));
	QJsonObject skyPropertiesDefinition = skyProperties.object().value("sky").toObject();
	QJsonObject skyDataDefinition = skyData.object();
	scene->skyType = static_cast<iris::SkyType>(skyPropertiesDefinition.value("type").toInt());

	if (scene->skyType == iris::SkyType::SINGLE_COLOR) {
		scene->skyColor = SceneReader::readColor(skyDataDefinition.value("skyColor").toObject());
	}
	else if (scene->skyType == iris::SkyType::REALISTIC) {
		scene->skyRealistic.luminance = skyDataDefinition["luminance"].toDouble();
		scene->skyRealistic.reileigh = skyDataDefinition["reileigh"].toDouble();
		scene->skyRealistic.mieCoefficient = skyDataDefinition["mieCoefficient"].toDouble();
		scene->skyRealistic.mieDirectionalG = skyDataDefinition["mieDirectionalG"].toDouble();
		scene->skyRealistic.turbidity = skyDataDefinition["turbidity"].toDouble();
		scene->skyRealistic.sunPosX = skyDataDefinition["sunPosX"].toDouble();
		scene->skyRealistic.sunPosY = skyDataDefinition["sunPosY"].toDouble();
		scene->skyRealistic.sunPosZ = skyDataDefinition["sunPosZ"].toDouble();
	}
	else if (scene->skyType == iris::SkyType::EQUIRECTANGULAR) {
		QStringList dependency = db->fetchAssetDependeesByType(scene->skyGuid, ModelTypes::Texture);
		if (!dependency.isEmpty()) {
			auto image = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(dependency.first()).name);
			if (QFileInfo(image).isFile()) scene->setSkyTexture(iris::Texture2D::load(image, false));
		}
	}
	else if (scene->skyType == iris::SkyType::CUBEMAP) {
		auto front = db->fetchAsset(skyDataDefinition["front"].toString()).name;
		auto back = db->fetchAsset(skyDataDefinition["back"].toString()).name;
		auto left = db->fetchAsset(skyDataDefinition["left"].toString()).name;
		auto right = db->fetchAsset(skyDataDefinition["right"].toString()).name;
		auto top = db->fetchAsset(skyDataDefinition["top"].toString()).name;
		auto bottom = db->fetchAsset(skyDataDefinition["bottom"].toString()).name;

		QVector<QString> sides = { front, back, top, bottom, left, right };
		for (int i = 0; i < sides.count(); ++i) {
			QString path = IrisUtils::join(Globals::project->getProjectFolder(), sides[i]);
			if (QFileInfo(path).isFile()) {
				sides[i] = path;
			}
			else {
				sides[i] = QString();
			}
		}

		// We need at least one valid image to get some metadata from
		QImage* info;
		bool useTex = false;
		for (const auto image : sides) {
			if (!image.isEmpty()) {
				info = new QImage(image);
				useTex = true;
				break;
			}
		}

		if (useTex) {
			scene->setSkyTexture(iris::Texture2D::createCubeMap(sides[0], sides[1], sides[2], sides[3], sides[4], sides[5], info));
		}
	}
	else if (scene->skyType == iris::SkyType::MATERIAL) {
		auto vert = skyDataDefinition.value("vertexShader").toString();
		auto frag = skyDataDefinition.value("fragmentShader").toString();

		auto vPath = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(vert).name);
		auto fPath = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(frag).name);

		scene->skyMaterial->createProgramFromShaderSource(vPath, fPath);
	}
	else if (scene->skyType == iris::SkyType::GRADIENT) {
		scene->gradientTop = SceneReader::readColor(skyDataDefinition.value("gradientTop").toObject());
		scene->gradientMid = SceneReader::readColor(skyDataDefinition.value("gradientMid").toObject());
		scene->gradientBot = SceneReader::readColor(skyDataDefinition.value("gradientBot").toObject());
		scene->gradientOffset = skyDataDefinition.value("gradientOffset").toDouble();
	}

	scene->switchSkyTexture(scene->skyType);
}

void WorldPropertyWidget::onSkyColorChanged(QColor color)
{
    scene->setSkyColor(color);
}

void WorldPropertyWidget::onAmbientColorChanged(QColor color)
{
    scene->setAmbientColor(color);
}

