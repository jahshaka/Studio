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
#include "assetiobase.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonValueRef>
#include <QJsonDocument>

#include "../irisgl/src/irisglfwd.h"

class MaterialReader : public AssetIOBase
{
public:
    MaterialReader();

    QJsonObject parsedShader;
    bool readJahShader(const QString &filePath);
    void parseJahShader(const QJsonObject&);
    QJsonObject getParsedShader() {
        return parsedShader;
    }

    // return texture
};

#endif // MATERIALREADER_HPP
