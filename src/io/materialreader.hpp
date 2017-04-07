/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALREADER_HPP
#define MATERIALREADER_HPP

#include <QSharedPointer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonDocument>

#include "../irisgl/src/irisglfwd.h"
#include "assetiobase.h"
#include "../irisgl/src/materials/propertytype.h"

class MaterialReader : public AssetIOBase
{
public:
    MaterialReader();

    bool readJahShader(const QString &filePath);
    QJsonObject getParsedShader();

    QList<Property*> getPropertyList();

private:
    QJsonObject parsedShader;
    static int uuid;
};

#endif // MATERIALREADER_HPP
