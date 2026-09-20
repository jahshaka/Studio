/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPRESETS_H
#define MATERIALPRESETS_H

#include <QVector>

#include "data/materialpreset.h"

/// MaterialPresets — THE shipped preset list (MATERIAL-PREVIEW-1 item c).
///
/// `app/content/materials/*.material` is the set of materials the app ships,
/// each behind a reserved guid (Constants::Reserved::DefaultMaterials). THREE
/// places walked that directory with their own MaterialPresetReader and their
/// own filter — the asset panel's session registration, the presets tray's
/// tiles and `materials.presets` — and they had already drifted: the panel's
/// copy built a Default-shader material for PBR presets, and only two of the
/// three hid the legacy flavour.
namespace MaterialPresets
{

/// Every shipped PBR preset, read once per process. (The legacy, non-PBR
/// flavour is not returned: the engine viewport authors PBR only, so a legacy
/// preset has no tile, no verb and no apply.)
const QVector<MaterialPreset> &all();

/// The preset a reserved GUID or a preset NAME (case-insensitive) stands for.
/// `found` — optional, because MaterialPreset has no null — says whether one
/// was.
MaterialPreset find(const QString &presetOrGuid, bool *found = nullptr);

} // namespace MaterialPresets

#endif // MATERIALPRESETS_H
