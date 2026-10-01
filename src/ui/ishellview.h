/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ISHELLVIEW_H
#define ISHELLVIEW_H

// IShellView — WHAT THE SHELL SHOWS, AS AN INTERFACE (D10-SHELL-MODULES; audit S3).
//
// The verbs and the modules need the window: which space is up, a toast over
// the viewport, the editor's panels and tray, the dialogs by name. They used
// to get it by including shell/mainwindow.h — the API layer compiled against
// the whole shell (13 ApiModule TUs, 69 reaches from editorapi alone). This is
// the surface they actually use, in the shape IEditorViewport gives the
// viewport: the shell implements it (shell/shellview.cpp), headless hosts
// leave it null, and nothing under scripting/ or modules/ names the shell.
//
// Spaces are named as app.space() names them: "desktop", "player", "editor",
// "materials", "assets", "publish", "avatar".

#include <QString>

class QWidget;

class IShellView
{
public:
    virtual ~IShellView() = default;

    /// The top-level window (dialog parent, findChild root).
    virtual QWidget *window() const = 0;

    // ---- spaces --------------------------------------------------------
    /// The space on screen.
    virtual QString space() const = 0;
    /// Switches to `space` through the product's own switch (the buttons'
    /// path). False for an unknown name; a refused switch leaves the space
    /// where it was and spaceRefusal() says why.
    virtual bool setSpace(const QString &space) = 0;

    // ---- feedback ------------------------------------------------------
    /// The one transient toast over the editor viewport.
    virtual void showViewportToast(const QString &title, const QString &text) = 0;
};

#endif // ISHELLVIEW_H
