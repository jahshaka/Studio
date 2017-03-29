/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <Qt>

#include <QQuaternion>
#include <QVector3D>
#include <QColor>
#include <QDir>
#include <QFile>

#include <QJsonObject>

#include "assetiobase.h"

QDir AssetIOBase::getDirFromFileName(QString filename)
{
    QFileInfo info(filename);
    return info.absoluteDir();
}

void AssetIOBase::setAssetPath(QString assetPath)
{
    dir = QDir(AssetIOBase::getDirFromFileName(assetPath));
}

QString AssetIOBase::getRelativePath(QString filename)
{
    //if it's a resource then return it
    if(filename.trimmed().startsWith(":") || filename.trimmed().startsWith("qrc:"))
        return filename;

    return dir.relativeFilePath(filename);
}

QString AssetIOBase::getAbsolutePath(QString filename)
{
    //if it's a resource then return it
    if(filename.trimmed().startsWith(":") || filename.trimmed().startsWith("qrc:"))
        return filename;

    auto absPath = dir.absoluteFilePath(filename);

    //file should exist, else return null string
    if(!QFile(absPath).exists())
        return QString::null;

    //everything went ok :)
    return absPath;
}

QColor AssetIOBase::readColor(const QJsonObject& colorObj)
{
    if (colorObj.isEmpty()) {
        return QColor();
    }

    QColor col;
    col.setRed(colorObj["r"].toInt(0));
    col.setGreen(colorObj["g"].toInt(0));
    col.setBlue(colorObj["b"].toInt(0));
    col.setAlpha(colorObj["a"].toInt(255));

    return col;
}

QVector3D AssetIOBase::readVector3(const QJsonObject& vecObj)
{
    if(vecObj.isEmpty())
    {
        return QVector3D();
    }

    QVector3D vec;
    vec.setX(vecObj["x"].toDouble(0));
    vec.setY(vecObj["y"].toDouble(0));
    vec.setZ(vecObj["z"].toDouble(0));

    return vec;
}
