/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materialreader.hpp"
#include <QDebug>

int MaterialReader::uuid = 0;

MaterialReader::MaterialReader()
{

}

bool MaterialReader::readJahShader(const QString &filePath)
{
    dir = AssetIOBase::getDirFromFileName(filePath);
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);

    auto data = file.readAll();
    file.close();
    auto doc = QJsonDocument::fromJson(data);

    parsedShader = doc.object();
}

QJsonObject MaterialReader::getParsedShader()
{
    return parsedShader;
}

QList<Property*> MaterialReader::getPropertyList()
{
    QList<Property*> properties;

    auto widgetProps = parsedShader["uniforms"].toArray();

    for (auto property : widgetProps) {
        if (property.toObject()["type"].toString() == "float") {
            auto prop = new FloatProperty();

            prop->id            = ++uuid;
            prop->name          = property.toObject()["name"].toString();
            prop->displayName   = property.toObject()["displayName"].toString();
            prop->minValue      = property.toObject()["start"].toDouble();
            prop->maxValue      = property.toObject()["end"].toDouble();

            properties.append(prop);
        }

        else if (property.toObject()["type"].toString() == "color") {
            auto prop = new ColorProperty;

            prop->id            = ++uuid;
            prop->name          = property.toObject()["name"].toString();
            prop->displayName   = property.toObject()["displayName"].toString();

            properties.append(prop);
        }

        else if (property.toObject()["type"].toString() == "bool") {
            auto prop = new BoolProperty;

            prop->id            = ++uuid;
            prop->name          = property.toObject()["name"].toString();
            prop->displayName   = property.toObject()["displayName"].toString();

            properties.append(prop);
        }

        else if (property.toObject()["type"].toString() == "texture") {
            auto prop = new TextureProperty;

            prop->id            = ++uuid;
            prop->name          = property.toObject()["name"].toString();
            prop->displayName   = property.toObject()["displayName"].toString();

            properties.append(prop);
        }

        else if (property.toObject()["type"].toString() == "file") {
            auto prop = new FileProperty;

            prop->id            = ++uuid;
            prop->name          = property.toObject()["name"].toString();
            prop->displayName   = property.toObject()["displayName"].toString();

            properties.append(prop);
        }

        else if (property.toObject()["type"].toString() == "vec3") {
            auto prop = new Vec3Property;
            prop->id            = ++uuid;
            prop->type          = PropertyType::Vec3;

            properties.append(prop);
        }
    }

    return properties;
}
