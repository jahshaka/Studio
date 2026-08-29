/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "appapi.h"

#include "../../mainwindow.h"
#include "../../uimanager.h"
#include "../../widgets/projectmanager.h"

QVector<VerbInfo> AppApi::verbs() const
{
    return {
        { "desktop", "app.desktop(n=0) -> current",
          "Switches to desktop 1-4; app.desktop() just returns the current one.",
          Needs::Window },
        { "space", "app.space(name) -> bool",
          "Switches the main window space: desktop, player, editor, materials, assets, publish. player and editor need an open project.",
          Needs::Window },
    };
}

int AppApi::desktop(int n)
{
    if (!host.projectManager) { fail("app: not available in this session"); return 0; }
    if (n >= 1) host.projectManager->switchDesktop(n);
    return host.projectManager->getCurrentDesktop();
}

bool AppApi::space(const QString &name)
{
    if (!host.mainWindow) return fail("app: not available in this session");
    const QString s = name.trimmed().toLower();
    WindowSpaces space;
    if (s == "desktop")                          space = WindowSpaces::DESKTOP;
    else if (s == "player")                      space = WindowSpaces::PLAYER;
    else if (s == "editor")                      space = WindowSpaces::EDITOR;
    else if (s == "materials" || s == "effects") space = WindowSpaces::EFFECT;
    else if (s == "assets")                      space = WindowSpaces::ASSETS;
    else if (s == "publish")                     space = WindowSpaces::PUBLISH;
    else return fail(QStringLiteral("app.space: unknown space '%1' (desktop, player, editor, materials, assets, publish)").arg(name));

    if ((space == WindowSpaces::PLAYER || space == WindowSpaces::EDITOR) && !UiManager::isSceneOpen)
        return fail(QStringLiteral("app.space: '%1' needs an open project").arg(s));

    host.mainWindow->switchSpace(space);
    return true;
}
