/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/previewkey.h"

#include <QCryptographicHash>

#include "data/project.h"

namespace previews {

const Preset *presetFor(int assetType)
{
    // THE STUDIO-LIT PRESETS draw through the one thumbnail renderer under previewenv; the CPU
    // ones (an image's own pixels, a pose strip, a candela lobe, an icon) need no engine.
    static const Preset kMaterial{ "material", 1, true };
    static const Preset kAsset{ "asset", 1, true };   // the subject framed to its bounds
    static const Preset kTexture{ "texture", 1, false };
    static const Preset kVideo{ "video", 1, false };
    static const Preset kMusic{ "music", 1, false };
    static const Preset kFile{ "file", 1, false };
    static const Preset kAnimation{ "animation", 1, false };
    static const Preset kLightProfile{ "lightprofile", 1, false };
    switch (static_cast<ModelTypes>(assetType)) {
    case ModelTypes::Material: return &kMaterial;
    case ModelTypes::Object:
    case ModelTypes::ParticleSystem:
    case ModelTypes::Avatar: return &kAsset;
    case ModelTypes::Texture: return &kTexture;
    case ModelTypes::Video: return &kVideo;
    case ModelTypes::Music: return &kMusic;
    case ModelTypes::File: return &kFile;
    case ModelTypes::Animation: return &kAnimation;
    case ModelTypes::LightProfile: return &kLightProfile;
    default: return nullptr;
    }
}

QString key(const KeySource &source, int previewenvVersion)
{
    const Preset *preset = presetFor(source.type);
    if (!preset) return QString();
    QStringList files = source.files;
    QStringList members = source.members;
    files.sort();
    members.sort();
    // One field a line, each named, so no two different inputs can serialise alike.
    QByteArray text = QByteArrayLiteral("previews-key 1\npreset ") + preset->name
                      + "\npreset-version " + QByteArray::number(preset->version)
                      + "\nstudio " + QByteArray::number(preset->studioLit ? previewenvVersion : 0);
    if (files.isEmpty() && source.pin.isEmpty())
        text += "\nguid " + source.guid.toUtf8();
    text += "\nfiles " + files.join(QLatin1Char(',')).toUtf8();
    text += "\npin " + source.pin.toUtf8();
    text += "\nmembers " + members.join(QLatin1Char(',')).toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(text, QCryptographicHash::Sha256).left(8).toHex());
}

}   // namespace previews
