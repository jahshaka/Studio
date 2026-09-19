/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/materialpresets.h"

#include <QDir>

#include "data/constants.h"
#include "io/materialpresetreader.h"
#include "irisgl/core/irisutils.h"

namespace MaterialPresets {

const QVector<MaterialPreset> &all()
{
    static const QVector<MaterialPreset> loaded = [] {
        QVector<MaterialPreset> out;
        MaterialPresetReader reader;
        const QDir dir(IrisUtils::getAbsoluteAssetPath("app/content/materials"));
        for (const auto &file : dir.entryInfoList(QStringList(), QDir::Files)) {
            auto preset = reader.readMaterialPreset(file.absoluteFilePath());
            if (preset.type.compare(QStringLiteral("PBR"), Qt::CaseInsensitive) != 0) continue;
            out.append(preset);
        }
        return out;
    }();
    return loaded;
}

MaterialPreset find(const QString &presetOrGuid, bool *found)
{
    if (found) *found = false;
    if (presetOrGuid.isEmpty()) return MaterialPreset();
    // A reserved GUID names a preset; so does the preset's own NAME, which is
    // what a tile's Qt::UserRole and every script call carry.
    QString name = Constants::Reserved::DefaultMaterials.value(presetOrGuid);
    if (name.isEmpty()) name = presetOrGuid;
    for (const auto &preset : all()) {
        if (preset.name.compare(name, Qt::CaseInsensitive) != 0) continue;
        if (found) *found = true;
        return preset;
    }
    return MaterialPreset();
}

} // namespace MaterialPresets
