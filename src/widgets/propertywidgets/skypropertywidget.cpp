/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "skypropertywidget.h"

#include "irisgl/src/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../texturepickerwidget.h"
#include "../hfloatsliderwidget.h"
#include "../comboboxwidget.h"
#include "../checkboxwidget.h"

#include "widgets/propertywidgets/worldpropertywidget.h"

#include "globals.h"
#include "core/database/database.h"
#include "core/subscriber.h"
#include "io/scenewriter.h"
#include "io/scenereader.h"
#include "io/assetmanager.h"

SkyPropertyWidget::SkyPropertyWidget()
{
}

void SkyPropertyWidget::skyTypeChanged(int index)
{
	if (skyGuid.isEmpty()) return;

	//QJsonDocument skyProperties = QJsonDocument::fromBinaryData(db->fetchAsset(scene->skyGuid).properties);
	//QJsonObject skyPropertiesDefinition = skyProperties.object().value("sky").toObject();
	//auto switchedIndex = static_cast<iris::SkyType>(skyPropertiesDefinition.value("type").toInt());

	const QJsonObject skyDefinition = static_cast<int>(scene->skyType) == index
										? QJsonDocument::fromBinaryData(db->fetchAssetData(skyGuid)).object()
										: QJsonObject();

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

	switch (static_cast<iris::SkyType>(index)) {
		case iris::SkyType::SINGLE_COLOR: {
			singleColor = this->addColorPicker("Sky Color");
			connect(singleColor->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onSingleSkyColorChanged(QColor)));

			// This should be revisited, some differences between scene color and our default
			if (skyDefinition.isEmpty()) {
				singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(QColor(72, 72, 72)));
				singleColor->setColorValue(QColor(72, 72, 72));
			}
			else {
				singleColor->setColorValue(SceneReader::readColor(skyDefinition.value("skyColor").toObject()));
				onSingleSkyColorChanged(SceneReader::readColor(skyDefinition.value("skyColor").toObject()));
			}

			break;
		}

		case iris::SkyType::REALISTIC: {
			luminance = addFloatValueSlider("Intensity", .01f, 1.f, 1.f);
			reileigh = addFloatValueSlider("Light Scattering", 0, 10.f, 2.5f);
			mieCoefficient = addFloatValueSlider("Sun Glow", 0, 1.f, .053f);
			mieDirectionalG = addFloatValueSlider("Sun Glare", 0, 1.f, .75f);
			turbidity = addFloatValueSlider("Turbidity", 0, 1.f, .32f);
			sunPosY = addFloatValueSlider("Sun Height", -0.99f, 10.f, 7.f);
			sunPosX = addFloatValueSlider("Strafe X", 0, 1.f, 10.f);
			sunPosZ = addFloatValueSlider("Strafe Z", 0, 1.f, 10.f);

			connect(luminance, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onLuminanceChanged);
			connect(reileigh, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onReileighChanged);
			connect(mieCoefficient, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onMieCoeffGChanged);
			connect(mieDirectionalG, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onMieDireChanged);
			connect(turbidity, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onTurbidityChanged);
			connect(sunPosX, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onSunPosXChanged);
			connect(sunPosY, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onSunPosYChanged);
			connect(sunPosZ, &HFloatSliderWidget::valueChanged, this, &SkyPropertyWidget::onSunPosZChanged);

			if (skyDefinition.isEmpty()) {
				realisticDefinition.insert("luminance", luminance->getValue());
				realisticDefinition.insert("reileigh", reileigh->getValue());
				realisticDefinition.insert("mieCoefficient", mieCoefficient->getValue());
				realisticDefinition.insert("mieDirectionalG", mieDirectionalG->getValue());
				realisticDefinition.insert("turbidity", turbidity->getValue());
				realisticDefinition.insert("sunPosX", sunPosX->getValue());
				realisticDefinition.insert("sunPosY", sunPosY->getValue());
				realisticDefinition.insert("sunPosZ", sunPosZ->getValue());
			}
			else {
				reileigh->setValue(skyDefinition.value("reileigh").toDouble());
				luminance->setValue(skyDefinition.value("luminance").toDouble());
				mieCoefficient->setValue(skyDefinition.value("mieCoefficient").toDouble());
				mieDirectionalG->setValue(skyDefinition.value("mieDirectionalG").toDouble());
				turbidity->setValue(skyDefinition.value("turbidity").toDouble());
				sunPosX->setValue(skyDefinition.value("sunPosX").toDouble());
				sunPosY->setValue(skyDefinition.value("sunPosY").toDouble());
				sunPosZ->setValue(skyDefinition.value("sunPosZ").toDouble());
			}

			break;
		}

		case iris::SkyType::EQUIRECTANGULAR: {
			equiTexture = this->addTexturePicker("Equi Map");

			// There are no default values, this definition gets set whenever we change the texture
			setEquiMap(skyDefinition.value("equiSkyGuid").toString());

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

			break;
		}

		case iris::SkyType::CUBEMAP: {
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

			setSkyMap(skyDefinition);

			break;
		}

		case iris::SkyType::MATERIAL: {
			shaderSelector = this->addComboBox("Material");

			//auto materialsAvailableFromDatabase = db->fetchAssetsByType(static_cast<int>(ModelTypes::Shader));
			//shaderSelector->getWidget()->blockSignals(true);	// don't register initial signals
			//shaderSelector->clear();
			//for (const auto shader : materialsAvailableFromDatabase) {
			//	shaderSelector->addItem(QFileInfo(shader.name).baseName(), shader.guid);
			//}
			//shaderSelector->getWidget()->blockSignals(false);
			////If we only have one shader, we need to trigger the update function
			//if (materialsAvailableFromDatabase.count() == 1) onMaterialChanged(0);

			// BROKEN!
			//shaderSelector->setCurrentItemData(skyDefinition.value("materialGuid").toString());
			//setSkyFromCustomMaterial(skyDefinition);

			//connect(shaderSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(onMaterialChanged(int)));

			break;
		}

		case iris::SkyType::GRADIENT: {
			colorTop = this->addColorPicker("Top Color");
			colorMid = this->addColorPicker("Middle Color");
			colorBot = this->addColorPicker("Bottom Color");
			offset = this->addFloatValueSlider("Offset", 0.01, .9f, .73f);

			connect(colorTop->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onGradientTopColorChanged(QColor)));
			connect(colorMid->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onGradientMidColorChanged(QColor)));
			connect(colorBot->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onGradientBotColorChanged(QColor)));
			connect(offset, SIGNAL(valueChanged(float)), SLOT(onGradientOffsetChanged(float)));

			if (skyDefinition.isEmpty()) {
				gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(QColor(255, 146, 138)));
				gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(QColor("white")));
				gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(QColor(64, 128, 255)));
				gradientDefinition.insert("gradientOffset", .73f);

				colorTop->setColorValue(QColor(255, 146, 138));
				colorMid->setColorValue(QColor("white"));
				colorBot->setColorValue(QColor(64, 128, 255));
			}
			else {
				colorTop->setColorValue(SceneReader::readColor(skyDefinition.value("gradientTop").toObject()));
				colorMid->setColorValue(SceneReader::readColor(skyDefinition.value("gradientMid").toObject()));
				colorBot->setColorValue(SceneReader::readColor(skyDefinition.value("gradientBot").toObject()));
				offset->setValue(skyDefinition.value("gradientOffset").toDouble());
			}

			break;
		}
	}

	// This is a bug, the world property widget should get and set the current sky type, not this widget
	currentSky = static_cast<iris::SkyType>(index);

	if (skyDefinition.isEmpty()) {
		updateAssetAndKeys();
		worldPropWidget->setScene(scene);
		worldPropWidget->onSkyChanged(index);
	}
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
        //skyTypeChanged(static_cast<int>(scene->skyType));
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
}

// Let's hijack this event and use it to update the asset in the db (iKlsR)
void SkyPropertyWidget::hideEvent(QHideEvent *event)
{
	Q_UNUSED(event)

	updateAssetAndKeys();

	return;
}

void SkyPropertyWidget::updateAssetAndKeys()
{
	QJsonObject properties;
	QJsonObject skyProps;
	skyProps.insert("type", static_cast<int>(currentSky));
	properties.insert("sky", skyProps);

	skyProperties = QJsonObject();
	skyProperties.insert("guid", skyGuid);

	switch (currentSky) {
	case iris::SkyType::SINGLE_COLOR: {
		for (const QString& key : singleColorDefinition.keys()) {
			skyProperties.insert(key, singleColorDefinition.value(key));
		}
		break;
	}

	case iris::SkyType::REALISTIC: {
		for (const QString& key : realisticDefinition.keys()) {
			skyProperties.insert(key, realisticDefinition.value(key));
		}
		break;
	}

	case iris::SkyType::EQUIRECTANGULAR: {
		for (const QString& key : equiSkyDefinition.keys()) {
			skyProperties.insert(key, equiSkyDefinition.value(key));
		}
		break;
	}

	case iris::SkyType::CUBEMAP: {
		for (const QString& key : cubeMapDefinition.keys()) {
			skyProperties.insert(key, cubeMapDefinition.value(key));
		}
		break;
	}

	case iris::SkyType::MATERIAL: {
		for (const QString& key : materialDefinition.keys()) {
			skyProperties.insert(key, materialDefinition.value(key));
		}
		break;
	}

	case iris::SkyType::GRADIENT: {
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
}

void SkyPropertyWidget::setEquiMap(const QString &guid)
{
    if (!guid.isEmpty()) {
		equiSkyDefinition.insert("equiSkyGuid", guid);
        auto image = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(guid).name);
        equiTexture->setTexture(QFileInfo(image).isFile() ? image : QString());
        scene->setSkyTexture(iris::Texture2D::load(image, false));
    }
}

void SkyPropertyWidget::setSkyMap(const QJsonObject &skyDataDefinition)
{
	auto front = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(skyDataDefinition["front"].toString()).name);
	auto back = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(skyDataDefinition["back"].toString()).name);
	auto left = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(skyDataDefinition["left"].toString()).name);
	auto right = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(skyDataDefinition["right"].toString()).name);
	auto top = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(skyDataDefinition["top"].toString()).name);
	auto bottom = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(skyDataDefinition["bottom"].toString()).name);

	cubeMapDefinition.insert("front", skyDataDefinition["front"].toString());
	cubeMapDefinition.insert("back", skyDataDefinition["back"].toString());
	cubeMapDefinition.insert("left", skyDataDefinition["left"].toString());
	cubeMapDefinition.insert("right", skyDataDefinition["right"].toString());
	cubeMapDefinition.insert("top", skyDataDefinition["top"].toString());
	cubeMapDefinition.insert("bottom", skyDataDefinition["bottom"].toString());

	cubemapFront->setTexture(front);
	cubemapBack->setTexture(back);
	cubemapTop->setTexture(top);
	cubemapBottom->setTexture(bottom);
	cubemapLeft->setTexture(left);
	cubemapRight->setTexture(right);

	// We need at least one valid image to get some metadata from
	QImage *info;
	bool useTex = false;
	QVector<QString> sides = { front, back, left, right, top, bottom };
	for (const auto image : sides) {
		if (!image.isEmpty() && QFileInfo(image).isFile()) {
			info = new QImage(image);
			useTex = true;
			break;
		}
	}

	if (useTex) {
		scene->setSkyTexture(iris::Texture2D::createCubeMap(front, back, top, bottom, left, right, info));
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

