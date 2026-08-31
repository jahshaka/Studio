/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/ziphelper.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include "zip.h"

namespace ZipHelper
{

bool zipDirectory(const QString &dirPath, const QString &zipPath, QString *errorOut)
{
    const QDir baseDir(dirPath);
    if (!baseDir.exists()) {
        if (errorOut) *errorOut = QStringLiteral("no such directory %1").arg(dirPath);
        return false;
    }
    QFile::remove(zipPath);

    struct zip_t *zip = zip_open(zipPath.toStdString().c_str(),
                                 ZIP_DEFAULT_COMPRESSION_LEVEL, 'w');
    if (!zip) {
        if (errorOut) *errorOut = QStringLiteral("cannot create %1").arg(zipPath);
        return false;
    }

    QDirIterator it(dirPath, QDir::NoDotAndDotDot | QDir::Files | QDir::Dirs,
                    QDirIterator::Subdirectories);
    bool ok = true;
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        const QString relative = baseDir.relativeFilePath(path);
        if (info.isDir()) {
            // A trailing '/' makes zip create the (possibly empty) directory.
            zip_entry_open(zip, QString(relative + "/").toStdString().c_str());
            zip_entry_close(zip);
        } else {
            zip_entry_open(zip, relative.toStdString().c_str());
            if (zip_entry_fwrite(zip, path.toStdString().c_str()) != 0) {
                if (errorOut) *errorOut = QStringLiteral("failed writing %1").arg(relative);
                ok = false;
            }
            zip_entry_close(zip);
            if (!ok) break;
        }
    }

    zip_close(zip);
    if (!ok) QFile::remove(zipPath);
    return ok;
}

bool extract(const QString &zipPath, const QString &destDir, QString *errorOut)
{
    if (!QFileInfo::exists(zipPath)) {
        if (errorOut) *errorOut = QStringLiteral("no such archive %1").arg(zipPath);
        return false;
    }
    QDir().mkpath(destDir);
    if (zip_extract(zipPath.toStdString().c_str(), destDir.toStdString().c_str(),
                    nullptr, nullptr) != 0) {
        if (errorOut) *errorOut = QStringLiteral("could not extract %1").arg(zipPath);
        return false;
    }
    return true;
}

} // namespace ZipHelper
