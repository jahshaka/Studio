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

#include <algorithm>

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
        for (const auto &file : dir.entryInfoList(QStringList(QStringLiteral("*.material")),
                                                  QDir::Files, QDir::Name)) {
            auto preset = reader.readMaterialPreset(file.absoluteFilePath());
            if (preset.type.compare(QStringLiteral("PBR"), Qt::CaseInsensitive) != 0) continue;
            // ONE LIST, AND EVERY ENTRY IN IT IS A GRAPH (PRESET-UNIFY-1).
            // A preset whose graph file this build does not ship would be a
            // tile that cannot answer the one gesture the drawer offers —
            // select and see it — so it is not listed at all, loudly.
            if (preset.graph.isEmpty()) {
                qWarning("MaterialPresets: '%s' has no graph and is NOT listed",
                         qUtf8Printable(preset.name));
                continue;
            }
            out.append(preset);
        }
        // SORTED BY NAME, CASE-INSENSITIVELY (PRESET-UNIFY-1 fix round). It
        // was the directory's own order — a CASE-SENSITIVE filename sort, so
        // every uppercase-named file came first and `brick_ground`…`wood`
        // trailed behind, which is neither alphabetical nor anything a user
        // could predict. This is THE list both windows draw, so the order is
        // part of it; and nothing may choose an entry by POSITION (see
        // CreateNewDialog::presetTile).
        std::sort(out.begin(), out.end(),
                  [](const MaterialPreset &a, const MaterialPreset &b) {
                      return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
                  });
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
