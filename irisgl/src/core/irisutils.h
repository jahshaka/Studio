/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef IRISUTILS_H
#define IRISUTILS_H

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QJsonObject>
#include <QVector3D>

#ifdef Q_OS_WIN32
    #include <Windows.h>
#endif

class IrisUtils
{
public:
    static QString getAbsoluteAssetPath(QString relToApp) {
        QDir basePath = QDir(QCoreApplication::applicationDirPath());
        auto path = QDir::cleanPath(basePath.absolutePath() + QDir::separator() + relToApp);
        return path;
    }

	static QString buildFileName(const QString &fileName, const QString &suffix) {
		if (!suffix.isEmpty()) return fileName + "." + suffix;
		return fileName;
	}

    template<typename... Args>
    static QString join(Args const&... args) {
        QString result;
        int unpack[]{ 0, (result += [=](QString const& s) { return s + "/"; }(args), 0) ... };
        static_cast<void>(unpack);
        return QDir::cleanPath(result);
    }

    static QVector3D readVector3(const QJsonObject &vecObj) {
        if (vecObj.isEmpty()) return QVector3D();

        QVector3D vec;
        vec.setX(vecObj["x"].toDouble(0));
        vec.setY(vecObj["y"].toDouble(0));
        vec.setZ(vecObj["z"].toDouble(0));

        return vec;
    }

    static QColor readColor(const QJsonObject &colorObj) {
        if (colorObj.isEmpty()) return QColor();

        QColor col;
        col.setRed(colorObj["r"].toInt(0));
        col.setGreen(colorObj["g"].toInt(0));
        col.setBlue(colorObj["b"].toInt(0));
        col.setAlpha(colorObj["a"].toInt(255));

        return col;
    }

    static bool removeDir(const QString &path) {
        QDir dirToRemove(path);
        return dirToRemove.removeRecursively();
    }
};

#endif // IRISUTILS_H
