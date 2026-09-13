/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PREFERENCESDIALOG_H
#define PREFERENCESDIALOG_H

#include <QDialog>
#include <QCloseEvent>
#include <QDebug>

namespace Ui {
    class PreferencesDialog;
}

class QListWidgetItem;
class SettingsManager;
class WorldSettingsWidget;
class McpSettingsWidget;
class AssetsSettingsWidget;
class CacheSettingsWidget;
class McpServer;
class IEditorViewport;
class ShortcutRegistry;
class MainWindow;
class Database;
class ProjectManager;

class PreferencesDialog : public QDialog
{
    Q_OBJECT
    SettingsManager* settings;

signals:
    void PreferencesDialogClosed();

protected:
    void closeEvent(QCloseEvent *event) {
        emit PreferencesDialogClosed();
        event->accept();
    }

public:
    explicit PreferencesDialog(QWidget* parent, Database *db, SettingsManager* settings);
    /// Forwards the editor wiring to the world-settings page (Phase 4).
    void wireEditor(IEditorViewport *viewport, MainWindow *mainWindow);
    /// Forwards the MCP server to its settings page (created after the dialog).
    void wireMcp(McpServer *server, MainWindow *mainWindow);
    /// Forwards the shortcut registry to the Shortcuts page (created after the dialog).
    void wireShortcuts(ShortcutRegistry *registry);
    /// Connects the desktop page to the live desktop so Preferences ->
    /// Desktop -> Slider Rows re-lays it out as the spin box moves
    /// (VISUAL_PARITY re-audit F8). Called by the shell once the project
    /// manager exists.
    void wireDesktop(ProjectManager *projectManager);
    ~PreferencesDialog();

	WorldSettingsWidget* worldSettings;
	McpSettingsWidget* mcpSettings = nullptr;
	AssetsSettingsWidget* assetsSettings = nullptr;
	/// The Cache page (SHADER_CACHE_SPEC §4.5): shader-cache size, location and
	/// the confirm-guarded rebuild button. Reads the verbs, never the directory.
	CacheSettingsWidget* cacheSettings = nullptr;
	/// The Performance page (CLEANUP-1 item 11): the render monitor's capture
	/// length, where its recordings are written, and how long they are kept.
	class PerfSettingsWidget* perfSettings = nullptr;
	Database *db;

protected:
    void mousePressEvent(QMouseEvent *evt) {
        oldPos = evt->globalPos();
    }

    void mouseMoveEvent(QMouseEvent *evt) {
        const QPoint delta = evt->globalPos() - oldPos;
        move(x() + delta.x(), y() + delta.y());
        oldPos = evt->globalPos();
    }

    QPoint oldPos;

private:
    void setupPages();

private slots:
    void saveSettings();

private:
    Ui::PreferencesDialog *ui;
};

#endif // PREFERENCESDIALOG_H
