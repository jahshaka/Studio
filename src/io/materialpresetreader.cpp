/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../irisgl/src/materials/defaultmaterial.h"
#include "materialpresetreader.h"
#include "../core/materialpreset.h"

#include <QJsonDocument>
#include <QJsonObject>

MaterialPresetReader::MaterialPresetReader()
{

}

QJsonObject MaterialPresetReader::getMatPreset(const QString &filename)
{
    this->setAssetPath(filename);

    QFile file(filename);
    file.open(QIODevice::ReadOnly);

    auto data = file.readAll();
    auto doc = QJsonDocument::fromJson(data);

    return doc.object();
}

MaterialPreset* MaterialPresetReader::readMaterialPreset(QString filename)
{
    this->setAssetPath(filename);

    QFile file(filename);
    file.open(QIODevice::ReadOnly);

    auto data = file.readAll();
    auto doc = QJsonDocument::fromJson(data);

    auto matObj = doc.object();

    auto material = new MaterialPreset();

    material->name = matObj["name"].toString("");

    auto icon = matObj["icon"].toString("");
    if(!icon.isEmpty())
        material->icon = getAbsolutePath(icon);

    material->type = matObj["material_type"].toString();

    auto colObj = matObj["ambientColor"].toString();
    QColor col;
    col.setNamedColor(colObj);
    material->ambientColor = col;

    colObj = matObj["diffuseColor"].toString();
    col.setNamedColor(colObj);
    material->diffuseColor = col;

    auto tex = matObj["diffuseTexture"].toString("");
    if(!tex.isEmpty())
        material->diffuseTexture = getAbsolutePath(tex);

    colObj = matObj["specularColor"].toString();
    col.setNamedColor(colObj);
    material->specularColor = col;
    material->shininess = (float)matObj["shininess"].toDouble(0.0f);

    tex = matObj["specularTexture"].toString("");
    if(!tex.isEmpty())
        material->specularTexture = getAbsolutePath(tex);

    tex = matObj["normalTexture"].toString("");
    if(!tex.isEmpty())
        material->normalTexture = getAbsolutePath(tex);
    material->normalIntensity = (float)matObj["normalIntensity"].toDouble(0.0f);

    tex = matObj["reflectionTexture"].toString("");
    if(!tex.isEmpty())
        material->reflectionTexture = getAbsolutePath(tex);
    material->reflectionInfluence = (float)matObj["reflectionInfluence"].toDouble(0.0f);

    material->textureScale = (float)matObj["textureScale"].toDouble(1.0f);

    return material;
}
