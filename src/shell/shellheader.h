/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHELLHEADER_H
#define SHELLHEADER_H

// ShellHeader — THE WINDOW'S HEADER BAND (D10-SHELL-MODULES): the logo, the
// space buttons (Desktop, Player, Editor, Materials, Assets, Avatar) and the
// right-hand glyph cluster (Publish, Help, Preferences), and what each button
// looks like for the space that is on screen. The buttons switch spaces
// through the window's one switch; this only draws them.

#include <QFont>
#include <QObject>

#include <functional>

#include "shell/spaces.h"

class QGridLayout;
class QLabel;
class QPushButton;
class QWidget;
class QtAwesome;

class ShellHeader : public QObject
{
    Q_OBJECT
public:
    explicit ShellHeader(QObject *parent = nullptr);

    /// Builds the band into the window's header layout. `switchTo` is the
    /// window's space switch, `current` the space on screen, `showPreferences`
    /// the cog's action.
    void build(QGridLayout *layout, QtAwesome *icons, std::function<void(WindowSpaces)> switchTo,
               std::function<WindowSpaces()> current, std::function<void()> showPreferences);

    /// One state per space button: the active space, the rest, and — while no
    /// scene is open — Editor and Player disabled.
    void updateStates(WindowSpaces activeSpace, bool sceneOpen);
    /// Editor and Player greyed out (a close made from the desktop).
    void disableSceneSpaces();

private:
    /// THE size of the header's glyph icons (Publish / Help / Preferences) —
    /// one font for all three, so they cannot drift apart again.
    QFont glyphFont() const;

    QtAwesome *mIcons = nullptr;
    QPushButton *worlds_menu = nullptr;
    QPushButton *player_menu = nullptr;
    QPushButton *editor_menu = nullptr;
    QPushButton *effect_menu = nullptr;
    QPushButton *assets_menu = nullptr;
    QPushButton *publish_menu = nullptr;
    QPushButton *avatar_menu = nullptr;
    QWidget *assets_panel = nullptr;
    QLabel *jlogo = nullptr;
    QPushButton *help = nullptr;
    QPushButton *prefs = nullptr;
};

#endif // SHELLHEADER_H
