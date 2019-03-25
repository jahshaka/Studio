/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "skypropertywidget.h"
#include "../texturepickerwidget.h"
#include "irisgl/src/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../hfloatsliderwidget.h"
#include "../comboboxwidget.h"

#include "../checkboxwidget.h"
#include "core/database/database.h"
#include "core/subscriber.h"
#include "globals.h"
#include "io/scenewriter.h"
#include "io/scenereader.h"
#include "io/assetmanager.h"

SkyPropertyWidget::SkyPropertyWidget()
{
}

void SkyPropertyWidget::skyTypeChanged(int index)
{
    // Decide whether of not to update the panel
    // if (index == static_cast<int>(currentSky)) {
    //     return;
    // } else {
	clearPanel(this->layout());

	setMouseTracking(true);

	skySelector = this->addComboBox("Sky Type");
	skySelector->addItem("Single Color");
	skySelector->addItem("Cubemap");
	skySelector->addItem("Equirectangular");
	skySelector->addItem("Gradient");
	skySelector->addItem("Material");
	skySelector->addItem("Realistic");
	skySelector->setCurrentIndex(index);

	connect(skySelector, SIGNAL(currentIndexChanged(int)), this, SLOT(skyTypeChanged(int)));
    // }

    // Draw the widgets depending on the sky type
    if (index == static_cast<int>(iris::SkyType::SINGLE_COLOR)) {
        singleColor = this->addColorPicker("Sky Color");
		// This should be revisited, some differences between scene color and our default
		singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(QColor(255, 118, 117)));
        
		connect(singleColor->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onSingleSkyColorChanged(QColor)));
    }
	else if (index == static_cast<int>(iris::SkyType::REALISTIC)) {
		luminance			= addFloatValueSlider("Luminance",	.01f,	1.f,	1.f);
		reileigh			= addFloatValueSlider("Reileigh",	0,		10.f,	2.5f);
		mieCoefficient		= addFloatValueSlider("Mie Coeff",	0,		100.f,	.053f);
		mieDirectionalG		= addFloatValueSlider("Mie Dir.",	0,		100.f,	.75f);
		turbidity			= addFloatValueSlider("Turbidity",	0,		1.f,	.32f);
		sunPosX				= addFloatValueSlider("X",			0,		10.f,	10.f);
		sunPosY				= addFloatValueSlider("Y",			0,		10.f,	7.f);
		sunPosZ				= addFloatValueSlider("Z",			0,		10.f,	10.f);

		realisticDefinition.insert("luminance", luminance->getValue());
		realisticDefinition.insert("reileigh", reileigh->getValue());
		realisticDefinition.insert("mieCoefficient", mieCoefficient->getValue());
		realisticDefinition.insert("mieDirectionalG", mieDirectionalG->getValue());
		realisticDefinition.insert("turbidity", turbidity->getValue());
		realisticDefinition.insert("sunPosX", sunPosX->getValue());
		realisticDefinition.insert("sunPosY", sunPosY->getValue());
		realisticDefinition.insert("sunPosZ", sunPosZ->getValue());

		connect(luminance,			&HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onLuminanceChanged);
		connect(reileigh,			&HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onReileighChanged);
		connect(mieCoefficient,		SIGNAL(valueChanged(float)), SLOT(onMieCoeffGChanged(float)));
		connect(mieDirectionalG,	SIGNAL(valueChanged(float)), SLOT(onMieDireChanged(float)));
		connect(turbidity,			SIGNAL(valueChanged(float)), SLOT(onTurbidityChanged(float)));
		connect(sunPosX,			SIGNAL(valueChanged(float)), SLOT(onSunPosXChanged(float)));
		connect(sunPosY,			SIGNAL(valueChanged(float)), SLOT(onSunPosYChanged(float)));
		connect(sunPosZ,			SIGNAL(valueChanged(float)), SLOT(onSunPosZChanged(float)));
	}
    else if (index == static_cast<int>(iris::SkyType::EQUIRECTANGULAR)) {
        equiTexture = this->addTexturePicker("Equi Map");

		// There are no default values, this definition gets set whenever we change the texture

		connect(equiTexture, &TexturePickerWidget::valueChanged, this, [this](QString value) {
			// Remember that asset names are unique (auto incremented) so this is fine
			QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(value).fileName());
			db->removeDependenciesByType(skyGuid, ModelTypes::Texture);
			if (!assetGuid.isEmpty()) {
				db->createDependency(
					static_cast<int>(ModelTypes::Sky),
					static_cast<int>(ModelTypes::Texture),
					skyGuid, assetGuid,
					Globals::project->getProjectGuid()
				);

				onEquiTextureChanged(assetGuid);
			}
        });
    }
    else if (index == static_cast<int>(iris::SkyType::CUBEMAP))
    {
		cubemapFront = this->addTexturePicker("Front");
		cubemapBack = this->addTexturePicker("Back");
		cubemapLeft = this->addTexturePicker("Left");
		cubemapRight = this->addTexturePicker("Right");
		cubemapTop = this->addTexturePicker("Top");
		cubemapBottom = this->addTexturePicker("Bottom");

		// remember the image on the tiles... TODO
		connect(cubemapFront, &TexturePickerWidget::valuesChanged, this, [this](QString value, QString guid) {
			onSlotChanged(value, guid, 0);
		});

		connect(cubemapBack, &TexturePickerWidget::valuesChanged, this, [this](QString value, QString guid) {
			onSlotChanged(value, guid, 1);
		});

		connect(cubemapLeft, &TexturePickerWidget::valuesChanged, this, [this](QString value, QString guid) {
			onSlotChanged(value, guid, 2);
		});

		connect(cubemapRight, &TexturePickerWidget::valuesChanged, this, [this](QString value, QString guid) {
			onSlotChanged(value, guid, 3);
		});

		connect(cubemapTop, &TexturePickerWidget::valuesChanged, this, [this](QString value, QString guid) {
			onSlotChanged(value, guid, 4);
		});

		connect(cubemapBottom, &TexturePickerWidget::valuesChanged, this, [this](QString value, QString guid) {
			onSlotChanged(value, guid, 5);
		});

    }
	else if (index == static_cast<int>(iris::SkyType::MATERIAL)) {
		shaderSelector = this->addComboBox("Material");

		auto materialsAvailableFromDatabase = db->fetchAssetsByType(static_cast<int>(ModelTypes::Shader));
		shaderSelector->getWidget()->blockSignals(true);	// don't register initial signals
		shaderSelector->clear();
		for (const auto shader : materialsAvailableFromDatabase) {
			shaderSelector->addItem(QFileInfo(shader.name).baseName(), shader.guid);
		}
		shaderSelector->getWidget()->blockSignals(false);
		//If we only have one shader, we need to trigger the update function
		if (materialsAvailableFromDatabase.count() == 1) onMaterialChanged(0);
		
		connect(shaderSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(onMaterialChanged(int)));
	}
    else if (index == static_cast<int>(iris::SkyType::GRADIENT)) {
        colorTop = this->addColorPicker("Top Color");
		colorMid = this->addColorPicker("Middle Color");
		colorBot = this->addColorPicker("Bottom Color");
        offset = this->addFloatValueSlider("Offset", 0.01, .9f, .73f);

		gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(QColor(255, 146, 138)));
		gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(QColor("white")));
		gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(QColor(64, 128, 255)));
		gradientDefinition.insert("gradientOffset", .73f);

		connect(colorTop->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onGradientTopColorChanged(QColor)));
		connect(colorMid->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onGradientMidColorChanged(QColor)));
		connect(colorBot->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onGradientBotColorChanged(QColor)));
		connect(offset, SIGNAL(valueChanged(float)), SLOT(onGradientOffsetChanged(float)));
    }

	// This is a bug, the world property widget should get and set the current sky type, not this widget
	currentSky = static_cast<iris::SkyType>(index);
}

void SkyPropertyWidget::onSlotChanged(QString value, QString guid, int index)
{
	// Normally there'd be a check for if value is empty here but in that case we can clear the guid
	QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(value).fileName());
	db->deleteDependency(skyGuid, guid);
	// Remember that asset names are unique (auto incremented) so this is fine
	if (!assetGuid.isEmpty()) {
		db->createDependency(
			static_cast<int>(ModelTypes::Sky),
			static_cast<int>(ModelTypes::Texture),
			skyGuid,
			assetGuid,
			Globals::project->getProjectGuid()
		);
	}

	switch (index) {
		case 0: { cubeMapDefinition.insert("front", !assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 1: { cubeMapDefinition.insert("back",	!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 2: { cubeMapDefinition.insert("left",	!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 3: { cubeMapDefinition.insert("right",	!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 4: { cubeMapDefinition.insert("top",	!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		case 5: { cubeMapDefinition.insert("bottom",!assetGuid.isEmpty() ? assetGuid : QString()); break; }
		default: break;
	}

	if (!!scene) {
		if (scene->skyGuid == skyGuid) setSkyMap(cubeMapDefinition);
	}
}

void SkyPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
        skyTypeChanged(static_cast<int>(scene->skyType));
    } else {
        this->scene.clear();
    }
}

void SkyPropertyWidget::setDatabase(Database *db)
{
    this->db = db;
}

void SkyPropertyWidget::setSkyAlongWithProperties(const QString &guid, iris::SkyType skyType)
{
	skyGuid = guid;
	skyTypeChanged(static_cast<int>(skyType));

	QJsonDocument propDoc = QJsonDocument::fromBinaryData(db->fetchAssetData(guid));
	QJsonObject skyDefinition = propDoc.object();

	switch (skyType) {
		case iris::SkyType::REALISTIC: {
			reileigh->setValue(skyDefinition.value("reileigh").toDouble());
			luminance->setValue(skyDefinition.value("luminance").toDouble());
			mieCoefficient->setValue(skyDefinition.value("mieCoefficient").toDouble());
			mieDirectionalG->setValue(skyDefinition.value("mieDirectionalG").toDouble());
			turbidity->setValue(skyDefinition.value("turbidity").toDouble());
			sunPosX->setValue(skyDefinition.value("sunPosX").toDouble());
			sunPosY->setValue(skyDefinition.value("sunPosY").toDouble());
			sunPosZ->setValue(skyDefinition.value("sunPosZ").toDouble());
			break;
		}

		case iris::SkyType::SINGLE_COLOR: {
			singleColor->setColorValue(SceneReader::readColor(skyDefinition.value("skyColor").toObject()));
			break;
		}

		case iris::SkyType::EQUIRECTANGULAR: {
			setEquiMap(skyDefinition.value("equiSkyGuid").toString());
			break;
		}

		case iris::SkyType::CUBEMAP: {
			setSkyMap(skyDefinition);
			break;
		}

		case iris::SkyType::MATERIAL: {
			// If we have a current sky set that as the current item
			shaderSelector->setCurrentItemData(skyDefinition.value("materialGuid").toString());
			setSkyFromCustomMaterial(skyDefinition);
			break;
		}

		case iris::SkyType::GRADIENT: {
			colorTop->setColorValue(SceneReader::readColor(skyDefinition.value("gradientTop").toObject()));
			colorMid->setColorValue(SceneReader::readColor(skyDefinition.value("gradientMid").toObject()));
			colorBot->setColorValue(SceneReader::readColor(skyDefinition.value("gradientBot").toObject()));
			offset->setValue(skyDefinition.value("gradientOffset").toDouble());
			break;
		}

		default: break;
	}
}

// Let's hijack this event and use it to update the asset in the db (iKlsR)
void SkyPropertyWidget::hideEvent(QHideEvent *event)
{
	QJsonObject properties;
	QJsonObject skyProps;
	skyProps.insert("type", static_cast<int>(currentSky));
	properties.insert("sky", skyProps);

	skyProperties = QJsonObject();
	skyProperties.insert("guid", skyGuid);

	switch (static_cast<int>(currentSky)) {
		case static_cast<int>(iris::SkyType::SINGLE_COLOR) : {
			for (const QString& key : singleColorDefinition.keys()) {
				skyProperties.insert(key, singleColorDefinition.value(key));
			}
			break;
		}
		
		case static_cast<int>(iris::SkyType::REALISTIC) : {
			for (const QString& key : realisticDefinition.keys()) {
				skyProperties.insert(key, realisticDefinition.value(key));
			}
			break;
		}

		case static_cast<int>(iris::SkyType::EQUIRECTANGULAR): {
			for (const QString& key : equiSkyDefinition.keys()) {
				skyProperties.insert(key, equiSkyDefinition.value(key));
			}
			break;
		}

		case static_cast<int>(iris::SkyType::CUBEMAP) : {
			for (const QString& key : cubeMapDefinition.keys()) {
				skyProperties.insert(key, cubeMapDefinition.value(key));
			}
			break;
		}

		case static_cast<int>(iris::SkyType::MATERIAL) : {
			for (const QString& key : materialDefinition.keys()) {
				skyProperties.insert(key, materialDefinition.value(key));
			}
			break;
		}

		case static_cast<int>(iris::SkyType::GRADIENT) : {
			for (const QString& key : gradientDefinition.keys()) {
				skyProperties.insert(key, gradientDefinition.value(key));
			}
			break;
		}
	}

	db->updateAssetAsset(skyGuid, QJsonDocument(skyProperties).toBinaryData());
	db->updateAssetProperties(skyGuid, QJsonDocument(properties).toBinaryData());
	// Not really used but keep around for now, the intent is clear (iKlsR)
	emit Globals::eventSubscriber->updateAssetSkyItemFromSkyPropertyWidget(skyGuid, currentSky);

	return;
}

void SkyPropertyWidget::setEquiMap(const QString &guid)
{
    if (!guid.isEmpty()) {
        auto image = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(guid).name);
        equiTexture->setTexture(QFileInfo(image).isFile() ? image : QString());
        scene->setSkyTexture(iris::Texture2D::load(image, false));
    }
}

void SkyPropertyWidget::setSkyMap(const QJsonObject &skyDataDefinition)
{
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

	cubemapFront->setTexture(sides[0]);
	cubemapBack->setTexture(sides[1]);
	cubemapTop->setTexture(sides[2]);
	cubemapBottom->setTexture(sides[3]);
	cubemapLeft->setTexture(sides[4]);
	cubemapRight->setTexture(sides[5]);

	// We need at least one valid image to get some metadata from
	QImage *info;
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

void SkyPropertyWidget::setSkyFromCustomMaterial(const QJsonObject& definition)
{
	auto vert = materialDefinition.value("vertexShader").toString();
	auto frag = materialDefinition.value("fragmentShader").toString();

	auto vPath = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(vert).name);
	auto fPath = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(frag).name);

	scene->skyMaterial->createProgramFromShaderSource(vPath, fPath);
}

void SkyPropertyWidget::onSingleSkyColorChanged(QColor color)
{
	singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(color));

	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyColor = color;
	}
}

void SkyPropertyWidget::onMaterialChanged(int index)
{
	Q_UNUSED(index)

	QJsonDocument shaderData = QJsonDocument::fromBinaryData(db->fetchAssetData(shaderSelector->getCurrentItemData()));
	QJsonObject shaderDataDefinition = shaderData.object();

	auto vert = shaderDataDefinition.value("vertex_shader").toString();
	auto frag = shaderDataDefinition.value("fragment_shader").toString();

	materialDefinition.insert("materialGuid", shaderSelector->getCurrentItemData());
	materialDefinition.insert("vertexShader", vert);
	materialDefinition.insert("fragmentShader", frag);

	if (!!scene) {
		if (scene->skyGuid == skyGuid) setSkyFromCustomMaterial(materialDefinition);
	}
}

void SkyPropertyWidget::onEquiTextureChanged(QString guid)
{
	equiSkyDefinition.insert("equiSkyGuid", guid);

	if (!!scene) {
		if (scene->skyGuid == skyGuid) setEquiMap(guid);
	}
}

void SkyPropertyWidget::onReileighChanged(float val)
{
	realisticDefinition.insert("reileigh", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyRealistic.reileigh = val;
	}
}

void SkyPropertyWidget::onLuminanceChanged(float val)
{
	realisticDefinition.insert("luminance", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyRealistic.luminance = val;
	}
}

void SkyPropertyWidget::onTurbidityChanged(float val)
{
	realisticDefinition.insert("turbidity", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyRealistic.turbidity = val;
	}
}

void SkyPropertyWidget::onMieCoeffGChanged(float val)
{
	realisticDefinition.insert("mieCoefficient", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyRealistic.mieCoefficient = val;
	}
}

void SkyPropertyWidget::onMieDireChanged(float val)
{
	realisticDefinition.insert("mieDirectionalG", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyRealistic.mieDirectionalG = val;
	}
}

void SkyPropertyWidget::onSunPosXChanged(float val)
{
	realisticDefinition.insert("sunPosX", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyRealistic.sunPosX = val;
	}
}

void SkyPropertyWidget::onSunPosYChanged(float val)
{
	realisticDefinition.insert("sunPosY", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyRealistic.sunPosY = val;
	}
}

void SkyPropertyWidget::onSunPosZChanged(float val)
{
	realisticDefinition.insert("sunPosZ", val);
	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyRealistic.sunPosZ = val;
	}
}

void SkyPropertyWidget::onGradientTopColorChanged(QColor color)
{
	gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(color));

	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->gradientTop = color;
	}
}

void SkyPropertyWidget::onGradientMidColorChanged(QColor color)
{
	gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(color));

	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->gradientMid = color;
	}
}

void SkyPropertyWidget::onGradientBotColorChanged(QColor color)
{
	gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(color));

	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->gradientBot = color;
	}
}

void SkyPropertyWidget::onGradientOffsetChanged(float offset)
{
	gradientDefinition.insert("gradientOffset", offset);

	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->gradientOffset = offset;
	}
}

