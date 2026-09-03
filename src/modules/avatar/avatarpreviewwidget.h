/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

// IAvatarPreviewWidget — the Avatar page's centre 3D view, abstracted.
//
// The exact arrangement materials uses (IMaterialPreviewWidget <- the bridge's
// EngineMaterialPreview): the module declares what it needs, Studio builds the
// engine-backed widget in src/bridge/ and injects it. The module therefore
// links no engine code and includes no engine header — pure Qt + iris here.
//
// The subject is the module's own AvatarPreviewModel, which is engine-free:
// the widget mirrors that document, draws the bone overlay from it, and never
// owns any of that state itself.

#include <QImage>

class QWidget;

namespace avatar
{

class AvatarPreviewModel;

class IAvatarPreviewWidget
{
public:
    virtual ~IAvatarPreviewWidget() {}

    /// The QWidget to place in the page (the implementation itself).
    virtual QWidget *previewWidget() = 0;
    /// Binds the subject. The model outlives the widget.
    virtual void setPreviewModel(AvatarPreviewModel *model) = 0;
    /// Re-frames the camera on the loaded fragment (after a load).
    virtual void framePreview() = 0;
    /// Offscreen render + readback, for avatar.snapshot. Null image on failure.
    virtual QImage renderPreview(int width, int height) = 0;
    /// Makes the engine evaluate the current clip time, so a pose read right
    /// after a setTime is THIS time's pose. Clip evaluation is the engine's
    /// since the document evaluator was retired, and the engine evaluates
    /// during a render.
    virtual void resolvePose() = 0;
};

}
