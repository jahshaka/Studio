/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PLAYERMODULE_H
#define PLAYERMODULE_H

// PlayerModule — the Player space as a StudioModule (verb-coverage audit F1).
//
// It contributes VERBS ONLY. The page itself (PlayerWidget) stays where the
// shell builds it, because the stacked-widget index order is load-bearing
// (WindowSpaces: PLAYER = 4) and moving it would be a second, unrelated change
// in a lane that is about the verb surface.
//
// So this is the smallest honest module: id(), registerApi(), shutdown(). It
// exists rather than a line in registerStudioModules() because the player is a
// feature domain with its own service and its own page, and the module loop is
// where a domain's verbs belong (studiomodule.h: "verbs registered through
// registerApi are its real interface").

#include "modules/studiomodule.h"

class PlayerApi;

class PlayerModule : public StudioModule
{
public:
    QString id() const override { return QStringLiteral("player"); }

    void initialize(ModuleHost &host) override { this->host = host; }
    void registerApi(ScriptEngine &engine) override;
    void shutdown() override {}

private:
    ModuleHost host;
    PlayerApi *mApi = nullptr;   // owned by the ScriptEngine
};

#endif // PLAYERMODULE_H
