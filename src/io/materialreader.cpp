/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materialreader.hpp"

MaterialReader::MaterialReader()
{

}

void MaterialReader::readJahShader(const QString &filePath)
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
