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
    // THE MESH PRESET (PREVIEWS-1 stage 2, the lead's ruling): a Mesh row's BAKE, every mesh of it in
    // ONE neutral material (iris::PbrMaterial's documented default), framed to its bounds in the studio.
    static const Preset kMesh{ "mesh", 1, true };
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
    case ModelTypes::Mesh: return &kMesh;
    case ModelTypes::Texture: return &kTexture;
    case ModelTypes::Video: return &kVideo;
    case ModelTypes::Music: return &kMusic;
    case ModelTypes::File: return &kFile;
    case ModelTypes::Animation: return &kAnimation;
    case ModelTypes::LightProfile: return &kLightProfile;
    default: return nullptr;
    }
}

QStringList bakeOf(const QStringList &files)
{
    QStringList out;
    for (const QString &file : files)
        if (file.startsWith(QLatin1String("bake:"))) out.append(file);
    return out;
}

QString key(const KeySource &source, int previewenvVersion)
{
    const Preset *preset = presetFor(source.type);
    if (!preset) return QString();
    QStringList files = source.files;
    QStringList members = source.members;
    QString pin = source.pin;
    QString blob = source.blob;
    if (static_cast<ModelTypes>(source.type) == ModelTypes::Mesh) {
        // A MESH DEPICTS ITS BAKE ALONE: the bake's oid (the source and its import settings are in
        // it), never its members, a project's pin of its source, or a blob it does not have.
        files = bakeOf(source.files);
        members.clear();
        pin.clear();
        blob.clear();
    }
    files.sort();
    members.sort();
    // One field a line, each named, so no two different inputs can serialise alike.
    QByteArray text = QByteArrayLiteral("previews-key 1\npreset ") + preset->name
                      + "\npreset-version " + QByteArray::number(preset->version)
                      + "\nstudio " + QByteArray::number(preset->studioLit ? previewenvVersion : 0);
    if (files.isEmpty() && pin.isEmpty())
        text += "\nguid " + source.guid.toUtf8();
    text += "\nfiles " + files.join(QLatin1Char(',')).toUtf8();
    text += "\npin " + pin.toUtf8();
    text += "\nmembers " + members.join(QLatin1Char(',')).toUtf8();
    text += "\nblob " + blob.toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(text, QCryptographicHash::Sha256).left(8).toHex());
}

}   // namespace previews
