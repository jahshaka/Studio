/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/vec.h"
#include <Qt>

#include <QColor>
#include <QDir>
#include <QFile>

#include <QJsonObject>

#include "io/assetiobase.h"

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
    if (filename.trimmed().startsWith(":") || filename.trimmed().startsWith("qrc:")) return filename;

    // CLEANED, so one picture has one spelling (PATHCLEAN-1). `QDir::
    // absoluteFilePath` only JOINS — it leaves every '..' and '.' in the
    // result — so a preset whose map slot says '../../shadergraph/wood.jpg'
    // came back as '…/materials/../../shadergraph/wood.jpg' while the same
    // file named by the graph's texture node came back cleaned. Two strings,
    // one file: everything that keys by PATH (the preset seed's hash list,
    // the readers' texture caches) saw two of it, and the seed hashed 34
    // files to import 31 — the three extra rows it used to mint that way were
    // named by no definition and stood in the tray for ever (SEED-STAMP-1's
    // content dedup hid the symptom; this removes the cause).
    //
    // Lexical, not `canonicalFilePath`: the answer must not depend on the
    // filesystem beyond the existence test below, and a canonical path would
    // also resolve symlinks — which would make a scene saved under one link
    // and read under another disagree with what it wrote.
    const QString absPath = QDir::cleanPath(dir.absoluteFilePath(filename));

    //file should exist, else return null string
    if (!QFile(absPath).exists()) return QString();

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

iris::Vec2 AssetIOBase::readVector2(const QJsonObject& vecObj)
{
    if(vecObj.isEmpty())
    {
        return iris::Vec2();
    }

    iris::Vec2 vec;
    vec.setX(vecObj["x"].toDouble(0));
    vec.setY(vecObj["y"].toDouble(0));

    return vec;
}

iris::Vec3 AssetIOBase::readVector3(const QJsonObject& vecObj)
{
	if (vecObj.isEmpty())
	{
		return iris::Vec3();
	}

	iris::Vec3 vec;
	vec.setX(vecObj["x"].toDouble(0));
	vec.setY(vecObj["y"].toDouble(0));
	vec.setZ(vecObj["z"].toDouble(0));

	return vec;
}

iris::Vec4 AssetIOBase::readVector4(const QJsonObject& vecObj)
{
	if (vecObj.isEmpty())
	{
		return iris::Vec4();
	}

	iris::Vec4 vec;
	vec.setX(vecObj["x"].toDouble(0));
	vec.setY(vecObj["y"].toDouble(0));
	vec.setZ(vecObj["z"].toDouble(0));
	vec.setW(vecObj["w"].toDouble(0));

	return vec;
}
