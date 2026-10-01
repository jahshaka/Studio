/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHELLSCRIPTING_H
#define SHELLSCRIPTING_H

// ShellScripting — WHERE THE SCRIPTING SURFACE MEETS THE WINDOW
// (SCRIPTING_SPEC §2; D10-SHELL-MODULES): the ScriptHost the verbs see the
// live app through (the shell's view, the services, the run's database scope,
// its render-loop state and its one undo entry), the edit gate's notice, the
// ScriptEngine with every domain's and every module's verbs, the console that
// is the bottom area's third tab, and the Claude assistant that talks to the
// engine through the MCP endpoint.

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

class ClaudeAssistant;
class Database;
class EnginePlayerView;
class IEditorViewport;
class IShellView;
class ModuleHub;
class PreferencesDialog;
class Project;
class QUndoStack;
class QWidget;
class ScriptConsole;
class ScriptEngine;
class SettingsManager;
struct ScriptHost;
struct StudioServices;

class ShellScripting : public QObject
{
    Q_OBJECT
public:
    struct Deps {
        IShellView *shell = nullptr;
        Database *db = nullptr;
        Project *project = nullptr;
        IEditorViewport *viewport = nullptr;
        QUndoStack *undoStack = nullptr;
        StudioServices *services = nullptr;
        EnginePlayerView *playerBackend = nullptr;
        SettingsManager *settings = nullptr;
        PreferencesDialog *prefs = nullptr;
        ModuleHub *modules = nullptr;
        QWidget *window = nullptr;
        /// The window-bottom toast (the edit gate's "Script running").
        std::function<void(const QString &, const QString &)> windowToast;
        /// The properties column re-read from the document.
        std::function<void()> refreshProperties;
    };
    ShellScripting(const Deps &deps, QObject *parent = nullptr);

    ~ShellScripting() override;

    ScriptHost *host() const { return mHost.get(); }
    ScriptEngine *engine() const { return mEngine; }
    ScriptConsole *console() const { return mConsole; }
    ClaudeAssistant *assistant() const { return mAssistant; }

private:
    /// Owned: every ApiModule and the engine hold a ScriptHost&, so the
    /// destructor deletes this object's children before the host goes.
    std::unique_ptr<ScriptHost> mHost;
    ScriptEngine *mEngine = nullptr;
    ScriptConsole *mConsole = nullptr;
    ClaudeAssistant *mAssistant = nullptr;
};

#endif // SHELLSCRIPTING_H
