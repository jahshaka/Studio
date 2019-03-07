/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "cubemappropertywidget.h"

#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>

#include "irisgl/src/graphics/shader.h"
#include "irisgl/src/scenegraph/meshnode.h"

#include "constants.h"
#include "globals.h"

#include "../checkboxwidget.h"
#include "../comboboxwidget.h"
#include "../sceneviewwidget.h"
#include "../texturepickerwidget.h"
#include "core/database/database.h"
#include "io/assetmanager.h"

CubeMapPropertyWidget::CubeMapPropertyWidget()
{
    cubemapFront = this->addTexturePicker("Front");
    cubemapBack = this->addTexturePicker("Back");
    cubemapLeft = this->addTexturePicker("Left");
    cubemapRight = this->addTexturePicker("Right");
    cubemapTop = this->addTexturePicker("Top");
    cubemapBottom = this->addTexturePicker("Bottom");

    // remember the image on the tiles... TODO

    connect(cubemapFront, &TexturePickerWidget::valueChanged, this, [this](QString value) {
        onSlotChanged(value, 0);
    });

    connect(cubemapBack, &TexturePickerWidget::valueChanged, this, [this](QString value) {
        onSlotChanged(value, 1);
    });

    connect(cubemapLeft, &TexturePickerWidget::valueChanged, this, [this](QString value) {
        onSlotChanged(value, 2);
    });

    connect(cubemapRight, &TexturePickerWidget::valueChanged, this, [this](QString value) {
        onSlotChanged(value, 3);
    });

    connect(cubemapTop, &TexturePickerWidget::valueChanged, this, [this](QString value) {
        onSlotChanged(value, 4);
    });

    connect(cubemapBottom, &TexturePickerWidget::valueChanged, this, [this](QString value) {
        onSlotChanged(value, 5);
    });
}

CubeMapPropertyWidget::~CubeMapPropertyWidget()
{

}

void CubeMapPropertyWidget::setCubeMapGuid(const QString &guid)
{
    this->guid = guid;

    bool found = false;

    for (auto asset : AssetManager::getAssets()) {
        if (found) break;

        if (asset->assetGuid == guid && asset->type == ModelTypes::CubeMap) {
            auto mapObject = asset->getValue().toJsonObject();

            auto front = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["front"].toString()).name);
            auto back = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["back"].toString()).name);
            auto left = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["left"].toString()).name);
            auto right = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["right"].toString()).name);
            auto top = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["top"].toString()).name);
            auto bottom = IrisUtils::join(Globals::project->getProjectFolder(), db->fetchAsset(mapObject["bottom"].toString()).name);

            cubemapFront->setTexture(QFileInfo(front).isFile() ? front : "");
            cubemapBack->setTexture(QFileInfo(back).isFile() ? back : "");
            cubemapLeft->setTexture(QFileInfo(left).isFile() ? left : "");
            cubemapRight->setTexture(QFileInfo(right).isFile() ? right : "");
            cubemapTop->setTexture(QFileInfo(top).isFile() ? top : "");
            cubemapBottom->setTexture(QFileInfo(bottom).isFile() ? bottom : "");

            found = true;
        }
    }
}

void CubeMapPropertyWidget::setDatabase(Database *db)
{
    this->db = db;
}

void CubeMapPropertyWidget::onSlotChanged(QString value, int index)
{
    // check if dependency exists,
    switch (index) {
        case 0: {
            db->deleteDependency(guid, cubemapFront->textureGuid);
            if (!value.isEmpty()) {
                db->createDependency(
                    static_cast<int>(ModelTypes::CubeMap),
                    static_cast<int>(ModelTypes::Texture),
                    guid,
                    cubemapFront->textureGuid,
                    Globals::project->getProjectGuid()
                );
            }
            break;
        }

        case 1: {
            db->deleteDependency(guid, cubemapBack->textureGuid);
            if (!value.isEmpty()) {
                db->createDependency(
                    static_cast<int>(ModelTypes::CubeMap),
                    static_cast<int>(ModelTypes::Texture),
                    guid,
                    cubemapBack->textureGuid,
                    Globals::project->getProjectGuid()
                );
            }
            break;
        }

        case 2: {
            db->deleteDependency(guid, cubemapLeft->textureGuid);
            if (!value.isEmpty()) {
                db->createDependency(
                    static_cast<int>(ModelTypes::CubeMap),
                    static_cast<int>(ModelTypes::Texture),
                    guid,
                    cubemapLeft->textureGuid,
                    Globals::project->getProjectGuid()
                );
            }
            break;
        }

        case 3: {
            db->deleteDependency(guid, cubemapRight->textureGuid);
            if (!value.isEmpty()) {
                db->createDependency(
                    static_cast<int>(ModelTypes::CubeMap),
                    static_cast<int>(ModelTypes::Texture),
                    guid,
                    cubemapRight->textureGuid,
                    Globals::project->getProjectGuid()
                );
            }
            break;
        }

        case 4: {
            db->deleteDependency(guid, cubemapTop->textureGuid);
            if (!value.isEmpty()) {
                db->createDependency(
                    static_cast<int>(ModelTypes::CubeMap),
                    static_cast<int>(ModelTypes::Texture),
                    guid,
                    cubemapTop->textureGuid,
                    Globals::project->getProjectGuid()
                );
            }
            break;
        }

        case 5: {
            db->deleteDependency(guid, cubemapBottom->textureGuid);
            if (!value.isEmpty()) {
                db->createDependency(
                    static_cast<int>(ModelTypes::CubeMap),
                    static_cast<int>(ModelTypes::Texture),
                    guid,
                    cubemapBottom->textureGuid,
                    Globals::project->getProjectGuid()
                );
            }
            break;
        }

        default:
            break;
    }

    QJsonObject mapDefinition;
    mapDefinition["front"]  = cubemapFront->textureGuid;
    mapDefinition["back"]   = cubemapBack->textureGuid;
    mapDefinition["left"]   = cubemapLeft->textureGuid;
    mapDefinition["right"]  = cubemapRight->textureGuid;
    mapDefinition["top"]    = cubemapTop->textureGuid;
    mapDefinition["bottom"] = cubemapBottom->textureGuid;

    bool found = false;
    for (auto asset : AssetManager::getAssets()) {
        if (found) break;
        if (asset->assetGuid == guid && asset->type == ModelTypes::CubeMap) {
            asset->setValue(QVariant::fromValue(mapDefinition));
            found = true;
        }
    }

    db->updateAssetAsset(guid, QJsonDocument(mapDefinition).toBinaryData());

    // check if cubemap is the selected one
    if (!!scene) {
        if (guid == scene->cubeMapGuid) {
            // bypasses the assetmanager but shouldn't matter...
            //scene->setSkyTexture(iris::Texture2D::createCubeMap(cubemapFront->filePath,
            //                                                    cubemapBack->filePath,
            //                                                    cubemapTop->filePath,
            //                                                    cubemapBottom->filePath,
            //                                                    cubemapLeft->filePath,
            //                                                    cubemapRight->filePath,
            //                                                    new QImage(value)));
        }
    }
}

void CubeMapPropertyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    if (!!scene) {
        this->scene = scene;
    }
    else {
        this->scene.clear();
    }
}

void CubeMapPropertyWidget::removeDependency(QString)
{
}

void CubeMapPropertyWidget::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
}
