/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PREVIEWROUTER_H
#define PREVIEWROUTER_H

#include "data/project.h"

// The Assets page's type→viewer routing (ASSET_MEDIA_SPEC §2): one map from
// ModelTypes to the page of the viewers QStackedLayout, replacing the old
// ad-hoc if-chain in the selectedTile flow. Page values ARE the stack
// indices — pages must be added to the stack in this order.
enum class PreviewPage
{
    Viewer3D = 0,      // objects, meshes, particles, materials, shaders, skies
    Image = 1,
    Audio = 2,
    Video = 3,
    Placeholder = 4,   // File and anything unroutable: icon + name, no stale 3D
};

namespace PreviewRouter
{

inline PreviewPage pageFor(ModelTypes type)
{
    switch (type) {
    case ModelTypes::Object:
    case ModelTypes::Mesh:
    case ModelTypes::ParticleSystem:
    case ModelTypes::Material:
    case ModelTypes::Shader:
    case ModelTypes::Sky:
        return PreviewPage::Viewer3D;
    case ModelTypes::Texture:
        return PreviewPage::Image;
    case ModelTypes::Music:
    case ModelTypes::SoundEffect:
        return PreviewPage::Audio;
    case ModelTypes::Video:
        return PreviewPage::Video;
    default:
        return PreviewPage::Placeholder;
    }
}

} // namespace PreviewRouter

#endif // PREVIEWROUTER_H
