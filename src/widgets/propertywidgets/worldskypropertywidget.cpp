/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "worldskypropertywidget.h"

#include "irisgl/src/materials/defaultskymaterial.h"

#include "../colorvaluewidget.h"
#include "../colorpickerwidget.h"
#include "../texturepickerwidget.h"
#include "../hfloatsliderwidget.h"
#include "../comboboxwidget.h"
#include "../checkboxwidget.h"

#include "src/widgets/propertywidgets/cubemapwidget.h"

#include "globals.h"
#include "core/database/database.h"
#include "core/subscriber.h"
#include "io/scenewriter.h"
#include "io/scenereader.h"
#include "io/assetmanager.h"

#include "widgets/sceneviewwidget.h"

WorldSkyPropertyWidget::WorldSkyPropertyWidget()
{
	connect(Globals::sceneViewWidget, &SceneViewWidget::changeSkyFromAssetWidget, this, [this](int index) {
		skyTypeChanged(index);
	});
}

void WorldSkyPropertyWidget::skyTypeChanged(int index)
{
	scene->skyType = static_cast<iris::SkyType>(index);

	clearPanel(this->layout());
	setMouseTracking(true);

	QJsonObject skyDefinition;
	switch (static_cast<iris::SkyType>(index)) {
	case iris::SkyType::SINGLE_COLOR:
		skyDefinition = scene->skyData.value("SingleColor");
		break;
	case iris::SkyType::REALISTIC:
		skyDefinition = scene->skyData.value("Realistic");
		break;
	case iris::SkyType::GRADIENT:
		skyDefinition = scene->skyData.value("Gradient");
		break;
	case iris::SkyType::EQUIRECTANGULAR:
		skyDefinition = scene->skyData.value("Equirectangular");
		break;
	case iris::SkyType::CUBEMAP:
		skyDefinition = scene->skyData.value("Cubemap");
		break;
	}

	skySelector = this->addComboBox("Sky Type");
	skySelector->addItem("Single Color");
	skySelector->addItem("Cubemap");
	skySelector->addItem("Equirectangular");
	skySelector->addItem("Gradient");
	skySelector->addItem("Material");
	skySelector->addItem("Realistic");
	skySelector->setCurrentIndex(index);

	connect(skySelector, SIGNAL(currentIndexChanged(int)), this, SLOT(skyTypeChanged(int)));

	switch (scene->skyType) {
		case iris::SkyType::SINGLE_COLOR: {
			singleColor = this->addColorPicker("Sky Color");
			connect(singleColor->getPicker(), SIGNAL(onColorChanged(QColor)), this, SLOT(onSingleSkyColorChanged(QColor)));

			QColor skyColor = SceneReader::readColor(skyDefinition.value("skyColor").toObject());
			singleColor->setColorValue(skyColor);
			singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(skyColor));
			onSingleSkyColorChanged(skyColor);

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

			connect(luminance, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onLuminanceChanged);
			connect(reileigh, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onReileighChanged);
			connect(mieCoefficient, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onMieCoeffGChanged);
			connect(mieDirectionalG, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onMieDireChanged);
			connect(turbidity, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onTurbidityChanged);
			connect(sunPosX, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onSunPosXChanged);
			connect(sunPosY, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onSunPosYChanged);
			connect(sunPosZ, &HFloatSliderWidget::valueChanged, this, &WorldSkyPropertyWidget::onSunPosZChanged);
	
			reileigh->setValue(skyDefinition.value("reileigh").toDouble());
			luminance->setValue(skyDefinition.value("luminance").toDouble());
			mieCoefficient->setValue(skyDefinition.value("mieCoefficient").toDouble());
			mieDirectionalG->setValue(skyDefinition.value("mieDirectionalG").toDouble());
			turbidity->setValue(skyDefinition.value("turbidity").toDouble());
			sunPosX->setValue(skyDefinition.value("sunPosX").toDouble());
			sunPosY->setValue(skyDefinition.value("sunPosY").toDouble());
			sunPosZ->setValue(skyDefinition.value("sunPosZ").toDouble());

			realisticDefinition.insert("luminance", luminance->getValue());
			realisticDefinition.insert("reileigh", reileigh->getValue());
			realisticDefinition.insert("mieCoefficient", mieCoefficient->getValue());
			realisticDefinition.insert("mieDirectionalG", mieDirectionalG->getValue());
			realisticDefinition.insert("turbidity", turbidity->getValue());
			realisticDefinition.insert("sunPosX", sunPosX->getValue());
			realisticDefinition.insert("sunPosY", sunPosY->getValue());
			realisticDefinition.insert("sunPosZ", sunPosZ->getValue());

			scene->skyRealistic.luminance = skyDefinition["luminance"].toDouble();
			scene->skyRealistic.reileigh = skyDefinition["reileigh"].toDouble();
			scene->skyRealistic.mieCoefficient = skyDefinition["mieCoefficient"].toDouble();
			scene->skyRealistic.mieDirectionalG = skyDefinition["mieDirectionalG"].toDouble();
			scene->skyRealistic.turbidity = skyDefinition["turbidity"].toDouble();
			scene->skyRealistic.sunPosX = skyDefinition["sunPosX"].toDouble();
			scene->skyRealistic.sunPosY = skyDefinition["sunPosY"].toDouble();
			scene->skyRealistic.sunPosZ = skyDefinition["sunPosZ"].toDouble();

			break;
		}

		case iris::SkyType::EQUIRECTANGULAR: {
			equiTexture = this->addTexturePicker("Equi Map");

			// There are no default values, this definition gets set whenever we change the texture
			setEquiMap(skyDefinition.value("equiSkyGuid").toString());

			connect(equiTexture, &TexturePickerWidget::valueChanged, this, [this](QString value) {
				// Remember that asset names are unique (auto incremented) so this is fine
				QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(value).fileName());
				db->removeDependenciesByType(scene->skyGuid, ModelTypes::Texture);
				if (!assetGuid.isEmpty()) {
					db->createDependency(
						static_cast<int>(ModelTypes::Sky),		// Not really but who cares
						static_cast<int>(ModelTypes::Texture),
						scene->skyGuid, assetGuid,
						Globals::project->getProjectGuid()
					);

					onEquiTextureChanged(assetGuid);
				}
			});

			break;
		}

		case iris::SkyType::CUBEMAP: {
			cubeMapWidget = this->addCubeMapWidget();

			connect(cubeMapWidget, &CubeMapWidget::valuesChanged, [=](QString value, QString guid, CubeMapPosition pos) {
				onSlotChanged(value, guid, static_cast<int>(pos));
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

			colorTop->setColorValue(SceneReader::readColor(skyDefinition.value("gradientTop").toObject()));
			colorMid->setColorValue(SceneReader::readColor(skyDefinition.value("gradientMid").toObject()));
			colorBot->setColorValue(SceneReader::readColor(skyDefinition.value("gradientBot").toObject()));
			offset->setValue(skyDefinition.value("gradientOffset").toDouble());

			gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(colorTop->getPicker()->getColor()));
			gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(colorMid->getPicker()->getColor()));
			gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(colorBot->getPicker()->getColor()));
			gradientDefinition.insert("gradientOffset", .73f);

			scene->gradientTop = SceneReader::readColor(skyDefinition.value("gradientTop").toObject());
			scene->gradientMid = SceneReader::readColor(skyDefinition.value("gradientMid").toObject());
			scene->gradientBot = SceneReader::readColor(skyDefinition.value("gradientBot").toObject());
			scene->gradientOffset = skyDefinition.value("gradientOffset").toDouble();

			break;
		}
	}

	scene->switchSkyTexture(scene->skyType);
	scene->queueSkyCapture();
}

void WorldSkyPropertyWidget::onSlotChanged(QString value, QString guid, int index)
{
	// Normally there'd be a check for if value is empty here but in that case we can clear the guid
	QString assetGuid = db->fetchAssetGUIDByName(QFileInfo(value).fileName());
	db->deleteDependency(scene->skyGuid, guid);
	// Remember that asset names are unique (auto incremented) so this is fine
	if (!assetGuid.isEmpty()) {
		db->createDependency(
			static_cast<int>(ModelTypes::Sky),
			static_cast<int>(ModelTypes::Texture),
			scene->skyGuid,
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
		setSkyMap(cubeMapDefinition); 
		scene->queueSkyCapture(); 
	}
}

void WorldSkyPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
		skyTypeChanged(static_cast<int>(scene->skyType));
    } else {
        this->scene.clear();
    }
}

void WorldSkyPropertyWidget::setDatabase(Database *db)
{
    this->db = db;
}

void WorldSkyPropertyWidget::updateAssetAndKeys()
{
	QJsonObject skyProperties = QJsonObject();

	switch (scene->skyType) {
	case iris::SkyType::SINGLE_COLOR: {
		for (const QString& key : singleColorDefinition.keys()) {
			skyProperties.insert(key, singleColorDefinition.value(key));
		}
		scene->skyData.insert("SingleColor", skyProperties);
		break;
	}

	case iris::SkyType::REALISTIC: {
		for (const QString& key : realisticDefinition.keys()) {
			skyProperties.insert(key, realisticDefinition.value(key));
		}
		scene->skyData.insert("Realistic", skyProperties);
		break;
	}

	case iris::SkyType::EQUIRECTANGULAR: {
		for (const QString& key : equiSkyDefinition.keys()) {
			skyProperties.insert(key, equiSkyDefinition.value(key));
		}
		scene->skyData.insert("Equirectangular", skyProperties);
		break;
	}

	case iris::SkyType::CUBEMAP: {
		for (const QString& key : cubeMapDefinition.keys()) {
			skyProperties.insert(key, cubeMapDefinition.value(key));
		}
		scene->skyData.insert("Cubemap", skyProperties);
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
		scene->skyData.insert("Gradient", skyProperties);
		break;
	}
	}

	scene->queueSkyCapture();
}

void WorldSkyPropertyWidget::setEquiMap(const QString &guid)
{
    if (!guid.isEmpty()) {
		equiSkyDefinition.insert("equiSkyGuid", guid);
        auto image = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(guid).name);
        equiTexture->setTexture(QFileInfo(image).isFile() ? image : QString());
        scene->setSkyTexture(iris::Texture2D::load(image, false));
		updateAssetAndKeys();

		scene->queueSkyCapture();
    }
}

void WorldSkyPropertyWidget::setSkyMap(const QJsonObject &skyDataDefinition)
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

	cubeMapWidget->addCubeMapImages(top, bottom, left, front, right, back);

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
		updateAssetAndKeys();
	}

	scene->queueSkyCapture();
}

void WorldSkyPropertyWidget::setSkyFromCustomMaterial(const QJsonObject& definition)
{
	//auto vert = materialDefinition.value("vertexShader").toString();
	//auto frag = materialDefinition.value("fragmentShader").toString();

	//auto vPath = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(vert).name);
	//auto fPath = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(frag).name);

	//scene->skyMaterial->createProgramFromShaderSource(vPath, fPath);
}

void WorldSkyPropertyWidget::onSingleSkyColorChanged(QColor color)
{
	singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(color));

	if (!!scene) {
		scene->skyColor = color;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onMaterialChanged(int index)
{
	Q_UNUSED(index)

	//QJsonDocument shaderData = QJsonDocument::fromBinaryData(db->fetchAssetData(shaderSelector->getCurrentItemData()));
	//QJsonObject shaderDataDefinition = shaderData.object();

	//auto vert = shaderDataDefinition.value("vertex_shader").toString();
	//auto frag = shaderDataDefinition.value("fragment_shader").toString();

	//materialDefinition.insert("materialGuid", shaderSelector->getCurrentItemData());
	//materialDefinition.insert("vertexShader", vert);
	//materialDefinition.insert("fragmentShader", frag);

	//if (!!scene) {
	//	if (scene->skyGuid == skyGuid) {
	//		setSkyFromCustomMaterial(materialDefinition);
	//		updateAssetAndKeys();
	//	}
	//}
}

void WorldSkyPropertyWidget::onEquiTextureChanged(QString guid)
{
	equiSkyDefinition.insert("equiSkyGuid", guid);

	if (!!scene) {
		setEquiMap(guid);
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onReileighChanged(float val)
{
	realisticDefinition.insert("reileigh", val);
	if (!!scene) {
		scene->skyRealistic.reileigh = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onLuminanceChanged(float val)
{
	realisticDefinition.insert("luminance", val);
	if (!!scene) {
		scene->skyRealistic.luminance = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onTurbidityChanged(float val)
{
	realisticDefinition.insert("turbidity", val);
	if (!!scene) {
		scene->skyRealistic.turbidity = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onMieCoeffGChanged(float val)
{
	realisticDefinition.insert("mieCoefficient", val);
	if (!!scene) {
		scene->skyRealistic.mieCoefficient = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onMieDireChanged(float val)
{
	realisticDefinition.insert("mieDirectionalG", val);
	if (!!scene) {
		scene->skyRealistic.mieDirectionalG = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onSunPosXChanged(float val)
{
	realisticDefinition.insert("sunPosX", val);
	if (!!scene) {
		scene->skyRealistic.sunPosX = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onSunPosYChanged(float val)
{
	realisticDefinition.insert("sunPosY", val);
	if (!!scene) {
		scene->skyRealistic.sunPosY = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onSunPosZChanged(float val)
{
	realisticDefinition.insert("sunPosZ", val);
	if (!!scene) {
		scene->skyRealistic.sunPosZ = val;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onGradientTopColorChanged(QColor color)
{
	gradientDefinition.insert("gradientTop", SceneWriter::jsonColor(color));

	if (!!scene) {
		scene->gradientTop = color;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onGradientMidColorChanged(QColor color)
{
	gradientDefinition.insert("gradientMid", SceneWriter::jsonColor(color));

	if (!!scene) {
		scene->gradientMid = color;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onGradientBotColorChanged(QColor color)
{
	gradientDefinition.insert("gradientBot", SceneWriter::jsonColor(color));

	if (!!scene) {
		scene->gradientBot = color;
		updateAssetAndKeys();
	}
}

void WorldSkyPropertyWidget::onGradientOffsetChanged(float offset)
{
	gradientDefinition.insert("gradientOffset", offset);

	if (!!scene) {
		scene->gradientOffset = offset;
		updateAssetAndKeys();
	}
}