/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>
#include <QPushButton>
#include <QStackedWidget>
class SettingsManager;
class Database;

class IEditorViewport;
class MainWindow;
class ShortcutRegistry;

class WorldSettingsWidget : public QWidget
{
    Q_OBJECT
    SettingsManager *settings;

public:
    /// Wired by the shell once the viewport exists (Phase 4: was UiManager).
    void wireEditor(IEditorViewport *viewport, MainWindow *mainWindow) {
        this->editorViewport = viewport; this->mainWindow = mainWindow;
    }
    /// Wired by the shell once the registry exists — generates the Shortcuts
    /// page from it (EDITOR_SHORTCUTS_SPEC §1; the old page was 11 stale labels).
    void setShortcutRegistry(ShortcutRegistry *registry);
    explicit WorldSettingsWidget(Database *db, SettingsManager* settings);
    ~WorldSettingsWidget();

    int outlineWidth;
    QColor outlineColor;
    QString defaultProjectDirectory;
    QString defaultEditorPath;
    bool showFps;
	bool autoSave;
	bool openInPlayer;
	bool autoUpdate;

	Database *db;


private:
    IEditorViewport *editorViewport = nullptr;
    MainWindow *mainWindow = nullptr;
    ShortcutRegistry *shortcutRegistry = nullptr;
    QWidget *shortcutsTable = nullptr;   // rebuilt whenever bindings change
	QPushButton * viewport;
	QPushButton * editor;
	QPushButton * content;
	QPushButton * mining;
	QPushButton * help;
	QPushButton * about;
	QPushButton * shortcuts;
	QPushButton * database;
	QPushButton * desktopBtn;   // Desktop section (DESKTOP_SLIDER_SPEC.md)

	QWidget *viewportWidget;
	QWidget *editorWidget;
	QWidget *contentWidget;
	QWidget *miningWidget;
	QWidget *helpWidget;
	QWidget *aboutWidget;
	QWidget *shortcutsWidget;
	QWidget *databaseWidget;
	QWidget *desktopWidget;

	QStackedWidget* stack;

	void configureViewport();
	void configureEditor();
	void configureContent();
	void configureAbout();
	void configureShortcuts();
	void configureDatabaseWidget();
	void configureDesktop();

	void setSizePolicyForWidgets(QWidget *);
private slots:
    void outlineWidthChanged(double width);
    void outlineColorChanged(QColor color);
    void showFpsChanged(bool show);
	void setShowPerspectiveLabel(bool show);
	void enableAutoSave(bool state);
	void enableOpenInPlayer(bool state);
    void changeDefaultDirectory();
    void projectDirectoryChanged(QString path);
    void changeEditorPath();
    void editorPathChanged(QString path);
	void enableAutoUpdate(bool state);
	void mouseControlChanged(const QString& value);
	void sliderRowsChanged(int rows);
	void shadowMeshOptimizationChanged(bool on);

public slots:
	void saveSettings();
	void rebuildShortcutsTable();

};

