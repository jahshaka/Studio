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

void WorldPropertyWidget::setupViewSelector()
{
    viewSelector = this->addComboBox("Skybox View");
    viewSelector->addItem("Front");
    viewSelector->addItem("Back");
    viewSelector->addItem("Top");
    viewSelector->addItem("Bottom");
    viewSelector->addItem("Left");
    viewSelector->addItem("Right");

    connect(viewSelector,               SIGNAL(currentIndexChanged(QString)),
            this,                       SLOT(viewTextureSlotChanged(QString)));
}

WorldPropertyWidget::WorldPropertyWidget()
{
    this->setPanelTitle("World Settings");

	worldGravity = this->addFloatValueSlider("Gravity", 0.f, 48.f);
    skyColor = this->addColorPicker("Sky Color");
    ambientColor = this->addColorPicker("Ambient Color");

    setupViewSelector();

	skySelector = this->addComboBox("Sky");

	connect(skySelector,				SIGNAL(currentIndexChanged(int)),
			this,						SLOT(onSkyChanged(int)));

    skyTexture = this->addTexturePicker("Skybox Texture");

	connect(worldGravity,				SIGNAL(valueChanged(float)),
			this,						SLOT(onGravityChanged(float)));

    connect(skyTexture,                 SIGNAL(valueChanged(QString)),
            this,                       SLOT(onSkyTextureChanged(QString)));

    connect(skyColor->getPicker(),      SIGNAL(onColorChanged(QColor)),
            this,                       SLOT(onSkyColorChanged(QColor)));

    connect(ambientColor->getPicker(),  SIGNAL(onColorChanged(QColor)),
            this,                       SLOT(onAmbientColorChanged(QColor)));
}

void WorldPropertyWidget::viewTextureSlotChanged(const QString &text)
{
    if (text == "Front") {
        skyTexture->setTexture(scene->skyBoxTextures[0].isEmpty()
                ? QString()
                : scene->skyBoxTextures[0]);
    } else if (text == "Back") {
        skyTexture->setTexture(scene->skyBoxTextures[1].isEmpty()
                ? QString()
                : scene->skyBoxTextures[1]);
    } else if (text == "Top") {
        skyTexture->setTexture(scene->skyBoxTextures[2].isEmpty()
                ? QString()
                : scene->skyBoxTextures[2]);
    } else if (text == "Bottom") {
        skyTexture->setTexture(scene->skyBoxTextures[3].isEmpty()
                ? QString()
                : scene->skyBoxTextures[3]);
    } else if (text == "Left") {
        skyTexture->setTexture(scene->skyBoxTextures[4].isEmpty()
                ? QString()
                : scene->skyBoxTextures[4]);
    } else if (text == "Right") {
        skyTexture->setTexture(scene->skyBoxTextures[5].isEmpty()
                ? QString()
                : scene->skyBoxTextures[5]);
    }
}

void WorldPropertyWidget::setDatabase(Database *db)
{
	this->db = db;
}

void WorldPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;

        for (int i = 0; i < 6; i++) {
            skyBoxTextures[i] = scene->skyBoxTextures[i];
        }

        // always set the first one - hack
        skyTexture->setTexture(scene->skyBoxTextures[0].isEmpty()
                ? QString()
                : scene->skyBoxTextures[0]);

        skyColor->setColorValue(scene->skyColor);
        ambientColor->setColorValue(scene->ambientColor);
		worldGravity->setValue(scene->gravity);

		auto skiesAvailableFromDatabase = db->fetchAssetsByType(static_cast<int>(ModelTypes::Sky));
		skySelector->getWidget()->blockSignals(true);	// don't register initial signals
		skySelector->clear();
		for (const auto sky : skiesAvailableFromDatabase) skySelector->addItem(sky.name, sky.guid);
		skySelector->getWidget()->blockSignals(false);
		// If we have a current sky set that as the current item
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
	scene->skyGuid = skySelector->getCurrentItemData();
}

void WorldPropertyWidget::onSkyTextureChanged(QString texPath)
{
    if (viewSelector->getCurrentItem() == "Front") {
        scene->skyBoxTextures[0] = texPath;
    } else if (viewSelector->getCurrentItem() == "Back") {
        scene->skyBoxTextures[1] = texPath;
    } else if (viewSelector->getCurrentItem() == "Top") {
        scene->skyBoxTextures[2] = texPath;
    } else if (viewSelector->getCurrentItem() == "Bottom") {
        scene->skyBoxTextures[3] = texPath;
    } else if (viewSelector->getCurrentItem() == "Left") {
        scene->skyBoxTextures[4] = texPath;
    } else if (viewSelector->getCurrentItem() == "Right") {
        scene->skyBoxTextures[5] = texPath;
    }

    QImage *info;
    for (int i = 0; i < 6; i++) {
        if (!scene->skyBoxTextures[i].isEmpty()) {
            info = new QImage(scene->skyBoxTextures[i]);
            break;
        }
    }

    scene->setSkyTexture(iris::Texture2D::createCubeMap(scene->skyBoxTextures[0],
                                                        scene->skyBoxTextures[1],
                                                        scene->skyBoxTextures[2],
                                                        scene->skyBoxTextures[3],
                                                        scene->skyBoxTextures[4],
                                                        scene->skyBoxTextures[5],
                                                        info));
}

void WorldPropertyWidget::onSkyColorChanged(QColor color)
{
    scene->setSkyColor(color);
}

void WorldPropertyWidget::onAmbientColorChanged(QColor color)
{
    scene->setAmbientColor(color);
}

