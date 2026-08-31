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
class McpServer;
class IEditorViewport;
class ShortcutRegistry;
class MainWindow;
class Database;

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
    ~PreferencesDialog();

	WorldSettingsWidget* worldSettings;
	McpSettingsWidget* mcpSettings = nullptr;
	AssetsSettingsWidget* assetsSettings = nullptr;
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
