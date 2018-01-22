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

#include <QDir>
#include <QCoreApplication>

#ifdef Q_OS_WIN32
    #include <Windows.h>
#endif

class IrisUtils
{
public:
    static QString getAbsoluteAssetPath(QString relToApp) {
        QDir basePath = QDir(QCoreApplication::applicationDirPath());
    // #if defined(WIN32) && defined(QT_DEBUG)
    //     basePath.cdUp();
		#if defined(Q_OS_MAC)
			basePath.cdUp();
			basePath.cdUp();
			basePath.cdUp();
		#endif
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

    static bool removeDir(const QString &path) {
        QDir dirToRemove(path);
        return dirToRemove.removeRecursively();
    }
};

#endif // IRISUTILS_H
