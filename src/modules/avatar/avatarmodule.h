/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef AVATARMODULE_H
#define AVATARMODULE_H

// AvatarModule — the Avatar domain as a StudioModule (AVATAR_MODULE_SPEC
// PART 0). The permanent viewing/testing surface the later avatar lanes look
// through: load a rigged file, see its mesh, see its skeleton, play its clips.
//
// Part 0 is a VIEWER. It owns one AvatarPreviewModel (a document, engine-free),
// contributes one page and the `avatar` ApiModule, and mutates nothing else:
// no library row, no project pin, no database write, no undo command, and no
// touch of the editor scene or its clock. The avatar DOCUMENT concept
// (isAvatar/AvatarComponent, spawn, CRUD) is Part 1's.

#include <memory>

#include "modules/studiomodule.h"

class AvatarApi;
class AvatarPreview;

namespace avatar { class AvatarPage; class AvatarPreviewModel; }

class AvatarModule : public StudioModule
{
public:
    AvatarModule();
    ~AvatarModule() override;

    QString id() const override { return QStringLiteral("avatar"); }

    void initialize(ModuleHost &host) override;
    QWidget *createPage() override;
    void registerApi(ScriptEngine &engine) override;
    void shutdown() override;

    /// The live page, for the shell's direct calls (page-switch refresh).
    avatar::AvatarPage *page() const { return mPage; }

private:
    ModuleHost host;
    std::unique_ptr<avatar::AvatarPreviewModel> mModel;
    avatar::AvatarPage *mPage = nullptr;
    AvatarPreview      *mPreview = nullptr;   // owned by the page once injected
    AvatarApi          *mApi = nullptr;       // owned by the ScriptEngine
};

#endif // AVATARMODULE_H
