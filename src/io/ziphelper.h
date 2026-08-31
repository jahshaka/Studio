/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ZIPHELPER_H
#define ZIPHELPER_H

// The ONE zip loop (ASSET_PIPELINE preflight amendment 7): every archive
// producer/consumer calls these instead of hand-rolling zip_entry loops.

#include <QString>

namespace ZipHelper
{
/// Zip a directory's contents (recursively, relative entry names, empty
/// directories included) into zipPath. Overwrites zipPath.
bool zipDirectory(const QString &dirPath, const QString &zipPath, QString *errorOut = nullptr);

/// Extract an archive into an existing directory.
bool extract(const QString &zipPath, const QString &destDir, QString *errorOut = nullptr);
} // namespace ZipHelper

#endif // ZIPHELPER_H
