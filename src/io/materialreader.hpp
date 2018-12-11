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

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonObject>
#include <QJsonValueRef>
#include <QJsonDocument>
#include <QSharedPointer>

#include "../irisgl/src/irisglfwd.h"
#include "assetiobase.h"
#include "irisglfwd.h"

class Database;
class MaterialReader : public AssetIOBase
{
public:
    MaterialReader();

    void readJahShader(const QString &filePath);
    QJsonObject getParsedShader();

	iris::CustomMaterialPtr parseMaterial(QJsonObject materialObj, Database* handle);

private:
    QJsonObject parsedShader;
};

#endif // MATERIALREADER_HPP
