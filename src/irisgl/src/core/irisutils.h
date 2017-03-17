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

class IrisUtils
{
public:
    static QString getAbsoluteAssetPath(QString relToApp)
    {
        QDir basePath = QDir(QCoreApplication::applicationDirPath());
    #if defined(WIN32) && defined(QT_DEBUG)
        basePath.cdUp();
    #elif defined(Q_OS_MAC)
        basePath.cdUp();
        basePath.cdUp();
        basePath.cdUp();
    #endif

        auto path = QDir::cleanPath(basePath.absolutePath() + QDir::separator() + relToApp);
        return path;
    }
};

#endif // IRISUTILS_H
