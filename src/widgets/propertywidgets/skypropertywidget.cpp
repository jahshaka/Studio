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
#include "cubemappropertywidget.h"
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
        scene->skyType = iris::SkyType::SINGLE_COLOR;

        singleColor = this->addColorPicker("Sky Color");
        //singleColor->setColor(scene->skyColor);

        connect(singleColor->getPicker(), SIGNAL(onColorChanged(QColor)),
            this, SLOT(onSingleSkyColorChanged(QColor)));
    }
    else if (index == static_cast<int>(iris::SkyType::CUBEMAP))
    {
        scene->skyType = iris::SkyType::CUBEMAP;
        cubeSelector = this->addComboBox("CubeMap");

        for (const auto cubemap : AssetManager::getAssets()) {
            if (cubemap->type == ModelTypes::CubeMap)  {
                cubeSelector->addItem(cubemap->fileName, cubemap->assetGuid);
                break;
            }
        }

        setSkyMap(cubeSelector->getCurrentItemData());
        connect(cubeSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(onSkyCubeMapChanged(int)));
    }
    else if (index == static_cast<int>(iris::SkyType::EQUIRECTANGULAR)) {
        scene->skyType = iris::SkyType::EQUIRECTANGULAR;
        // one string
        equiTexture = this->addTexturePicker("Equi Map");

        connect(equiTexture, &TexturePickerWidget::valueChanged, this, [this](QString value) {
            Q_UNUSED(value);
            onEquiTextureChanged(equiTexture->textureGuid);
        });
    }
    else if (index == static_cast<int>(iris::SkyType::GRADIENT)) {
        scene->skyType = iris::SkyType::GRADIENT;
        // two strings
        // one float
        gradientType = this->addComboBox("Type");
        gradientType->addItem("Linear");
        gradientType->addItem("Radial");
        color1 = this->addColorPicker("Top Color");
        color2 = this->addColorPicker("Bottom Color");
        scale = this->addFloatValueSlider("Scale", 0, 1.f);
        angle = this->addFloatValueSlider("Angle", -360.f, 360.f);
    }
    else if (index == static_cast<int>(iris::SkyType::MATERIAL)) {
        scene->skyType = iris::SkyType::MATERIAL;
        // material asset
        shaderSelector = this->addComboBox("Material");

        for (const auto material : AssetManager::getAssets()) {
            if (material->type == ModelTypes::Material) {
                shaderSelector->addItem(material->fileName, material->assetGuid);
            }
        }
    }
    else if (index == static_cast<int>(iris::SkyType::REALISTIC)) {
        scene->skyType = iris::SkyType::REALISTIC;
        // floats
        // height
        luminance = this->addFloatValueSlider("Luminance", 0.01, 1.f);
        reileigh = this->addFloatValueSlider("Reileigh", 0, 10.f);
        mieCoefficient = this->addFloatValueSlider("Mie Coeff", 0, 100.f);
        mieDirectionalG = this->addFloatValueSlider("Mie Dir.", 0, 100.f);
        turbidity = this->addFloatValueSlider("Turbidity", 0, 1.f);
        sunPosX = this->addFloatValueSlider("X", 0, 10.f);
        sunPosY = this->addFloatValueSlider("Y", 0, 10.f);
        sunPosZ = this->addFloatValueSlider("Z", 0, 10.f);

        connect(luminance, SIGNAL(valueChanged(float)), SLOT(onLuminanceChanged(float)));
        connect(reileigh, SIGNAL(valueChanged(float)), SLOT(onReileighChanged(float)));
        connect(mieCoefficient, SIGNAL(valueChanged(float)), SLOT(onMieCoeffGChanged(float)));
        connect(mieDirectionalG, SIGNAL(valueChanged(float)), SLOT(onMieDireChanged(float)));
        connect(turbidity, SIGNAL(valueChanged(float)), SLOT(onTurbidityChanged(float)));
        connect(sunPosX, SIGNAL(valueChanged(float)), SLOT(onSunPosXChanged(float)));
        connect(sunPosY, SIGNAL(valueChanged(float)), SLOT(onSunPosYChanged(float)));
        connect(sunPosZ, SIGNAL(valueChanged(float)), SLOT(onSunPosZChanged(float)));
    }

    currentSky = static_cast<iris::SkyType>(index);

    scene->skyType = currentSky;
    scene->switchSkyTexture(currentSky);
}

void SkyPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;

        skyTypeChanged(static_cast<int>(scene->skyType));

        /*switch (scene->skyType)  {
            case iris::SkyType::REALISTIC: {
                reileigh->setValue(scene->skyRealistic.reileigh);
                luminance->setValue(scene->skyRealistic.luminance);
                mieCoefficient->setValue(scene->skyRealistic.mieCoefficient);
                mieDirectionalG->setValue(scene->skyRealistic.mieDirectionalG);
                turbidity->setValue(scene->skyRealistic.turbidity);
                sunPosX->setValue(scene->skyRealistic.sunPosX);
                sunPosY->setValue(scene->skyRealistic.sunPosY);
                sunPosZ->setValue(scene->skyRealistic.sunPosZ);
                break;
            }

            case iris::SkyType::SINGLE_COLOR: {
                singleColor->setColorValue(scene->skyColor);
                singleColor->setColorValue(scene->skyColor);
                break;
            }

            case iris::SkyType::CUBEMAP: {
                if (!scene->cubeMapGuid.isEmpty()) {
                    cubeSelector->setCurrentItemData(scene->cubeMapGuid);
                    setSkyMap(scene->cubeMapGuid);
                }
                break;
            }

            case iris::SkyType::MATERIAL: {
                shaderSelector->setCurrentItemData(scene->materialGuid);
                break;
            }

            case iris::SkyType::EQUIRECTANGULAR: {
                setEquiMap(scene->equiTextureGuid);
                break;
            }

            default: break;
        }*/

    } else {
        this->scene.clear();
    }
}

void SkyPropertyWidget::setDatabase(Database *db)
{
    this->db = db;
}

void SkyPropertyWidget::setSky(const QString &guid, iris::SkyType skyType)
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
			sunPosX->setValue(skyDefinition.value("sunPosX").toInt());
			sunPosY->setValue(skyDefinition.value("sunPosY").toInt());
			sunPosZ->setValue(skyDefinition.value("sunPosZ").toInt());
			break;
		}

		case iris::SkyType::SINGLE_COLOR: {
			qDebug() << "reading " << skyDefinition.value("skyColor").toObject();
			singleColor->setColorValue(SceneReader::readColor(skyDefinition.value("skyColor").toObject()));
			break;
		}

		case iris::SkyType::CUBEMAP: {
			if (!scene->cubeMapGuid.isEmpty()) {
				cubeSelector->setCurrentItemData(scene->cubeMapGuid);
				setSkyMap(scene->cubeMapGuid);
			}
			break;
		}

		case iris::SkyType::MATERIAL: {
			shaderSelector->setCurrentItemData(scene->materialGuid);
			break;
		}

		case iris::SkyType::GRADIENT: {
			shaderSelector->setCurrentItemData(scene->materialGuid);
			break;
		}

		case iris::SkyType::EQUIRECTANGULAR: {
			setEquiMap(scene->equiTextureGuid);
			break;
		}

		default: break;
	}
}

// Let's hijack this event and use it to update the asset in the db
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

		case static_cast<int>(iris::SkyType::CUBEMAP) : {
			for (const QString& key : cubeMapDefinition.keys()) {
				skyProperties.insert(key, cubeMapDefinition.value(key));
			}
			break;
		}

		case static_cast<int>(iris::SkyType::EQUIRECTANGULAR) : {
			for (const QString& key : equiSkyDefinition.keys()) {
				skyProperties.insert(key, equiSkyDefinition.value(key));
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

		case static_cast<int>(iris::SkyType::REALISTIC) : {
			for (const QString& key : realisticDefinition.keys()) {
				skyProperties.insert(key, realisticDefinition.value(key));
			}
			break;
		}
	}

	qDebug() << skyProperties;

	db->updateAssetAsset(skyGuid, QJsonDocument(skyProperties).toBinaryData());
	db->updateAssetProperties(skyGuid, QJsonDocument(properties).toBinaryData());

	emit Globals::eventSubscriber->updateAssetSkyItemFromSkyPropertyWidget(skyGuid, currentSky);

	return;
}

void SkyPropertyWidget::setEquiMap(const QString &guid)
{
    if (!guid.isEmpty()) {
        bool found = false;

        for (auto asset : AssetManager::getAssets()) {
            if (found) break;

            if (asset->assetGuid == guid && asset->type == ModelTypes::Texture) {
                found = true;
                auto image = IrisUtils::join(Globals::project->getProjectFolder(), asset->fileName);
                equiTexture->setTexture(QFileInfo(image).isFile() ? image : "");
                scene->setSkyTexture(iris::Texture2D::load(image, false));
                scene->equiTextureGuid = guid;
            }
        }
    }
}

void SkyPropertyWidget::setSkyMap(const QString &guid)
{
    if (!guid.isEmpty()) {
        bool found = false;

        for (auto asset : AssetManager::getAssets()) {
            if (found) break;

            if (asset->assetGuid == guid && asset->type == ModelTypes::CubeMap) {
                found = true;

                auto mapObject = asset->getValue().toJsonObject();

                auto front = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["front"].toString()).name);
                auto back = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["back"].toString()).name);
                auto left = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["left"].toString()).name);
                auto right = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["right"].toString()).name);
                auto top = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["top"].toString()).name);
                auto bottom = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["bottom"].toString()).name);

                QString sides[6] = {front, back, top, bottom, left, right};

                QImage *info;
                bool useTex = false;
                for (int i = 0; i < 6; i++) {
                    if (!sides[i].isEmpty()) {
                        info = new QImage(sides[i]);
                        useTex = true;
                        break;
                    }
                }

                if (useTex)
                    scene->setSkyTexture(iris::Texture2D::createCubeMap(front, back, top, bottom, left, right, info));
            }
        }
    }
}

void SkyPropertyWidget::onSingleSkyColorChanged(QColor color)
{
	singleColorDefinition.insert("skyColor", SceneWriter::jsonColor(color));

	if (!!scene) {
		if (scene->skyGuid == skyGuid) scene->skyColor = color;
	}
}

void SkyPropertyWidget::onSkyCubeMapChanged(int index)
{
    Q_UNUSED(index);
	if (!!scene) {
		setSkyMap(cubeSelector->getCurrentItemData());
	}
}

void SkyPropertyWidget::onEquiTextureChanged(QString tex)
{
	equiSkyDefinition.insert("equiSkyGuid", tex);

	if (!!scene) {
		if (scene->skyGuid == skyGuid) setEquiMap(tex);
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
    if (!!scene) scene->skyRealistic.turbidity = val;
}

void SkyPropertyWidget::onMieCoeffGChanged(float val)
{
	realisticDefinition.insert("mieCoefficient", val);
    if (!!scene) scene->skyRealistic.mieCoefficient = val;
}

void SkyPropertyWidget::onMieDireChanged(float val)
{
	realisticDefinition.insert("mieDirectionalG", val);
    if (!!scene) scene->skyRealistic.mieDirectionalG = val;
}

void SkyPropertyWidget::onSunPosXChanged(float val)
{
	realisticDefinition.insert("sunPosX", val);
    if (!!scene)  scene->skyRealistic.sunPosX = val;
}

void SkyPropertyWidget::onSunPosYChanged(float val)
{
	realisticDefinition.insert("sunPosY", val);
    if (!!scene) scene->skyRealistic.sunPosY = val;
}

void SkyPropertyWidget::onSunPosZChanged(float val)
{
	realisticDefinition.insert("sunPosZ", val);
    if (!!scene) scene->skyRealistic.sunPosZ = val;
}

