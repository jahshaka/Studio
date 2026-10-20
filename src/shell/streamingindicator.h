/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef JAH_STREAMINGINDICATOR_H
#define JAH_STREAMINGINDICATOR_H

#include <QObject>
#include <QString>

#include <functional>

class QAction;
class QLabel;
class QTimer;
class QToolBar;

/// THE TOOLBAR'S STREAM-IN INDICATOR (ASYNC-SHADERS-1's "Compiling shaders (N)", BUGS-7 stage 3). What the world still
/// owes the picture while it streams in, from the engine's last published frame: the shaders the background compiler
/// has not landed, the materials still drawing a fallback for a texture on its way, the stages of the first lighting
/// arm left. Shown while ANY is owed, gone when nothing is. Painted in the theme's window text — a label a QToolBar
/// holds otherwise takes the toolbar's ButtonText, a dark text for a light button plate (~1.1:1 on the band: the owner
/// never saw it). Its action is `shaderCompiles` in editor.toolbar (visible + the text as its tooltip).
class StreamingIndicator : public QObject
{
public:
    struct Work {
        unsigned shaders = 0;    ///< permutations the background compiler has not landed
        unsigned textures = 0;   ///< materials drawing a fallback until their texture lands
        unsigned gi = 0;         ///< stages of the first lighting arm left
        bool any() const { return shaders || textures || gi; }
    };
    using Source = std::function<Work()>;

    /// Adds the label to `bar` (an action of its own, hidden until something is owed) and polls `source` four times a
    /// second (two atomics and a snapshot read).
    StreamingIndicator(QToolBar *bar, Source source);

    /// One poll, now (the timer's body).
    void refresh();
    /// The line for `w`: "Compiling shaders (N)", "Loading textures (N)", "Lighting the world", joined; empty when
    /// nothing is owed.
    static QString compose(const Work &w);

    QAction *action() const { return mAction; }
    QLabel *label() const { return mLabel; }

private:
    Source mSource;
    QLabel *mLabel = nullptr;
    QAction *mAction = nullptr;
    QTimer *mPoll = nullptr;
};

#endif // JAH_STREAMINGINDICATOR_H
