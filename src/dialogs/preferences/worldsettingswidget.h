/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>
#include <QPushButton>
#include <QStackedWidget>
class SettingsManager;
class Database;

class WorldSettingsWidget : public QWidget
{
    Q_OBJECT
    SettingsManager *settings;

public:
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
	QPushButton * viewport;
	QPushButton * editor;
	QPushButton * content;
	QPushButton * mining;
	QPushButton * help;
	QPushButton * about;
	QPushButton * shortcuts;
	QPushButton * database;

	QWidget *viewportWidget;
	QWidget *editorWidget;
	QWidget *contentWidget;
	QWidget *miningWidget;
	QWidget *helpWidget;
	QWidget *aboutWidget;
	QWidget *shortcutsWidget;
	QWidget *databaseWidget;

	QStackedWidget* stack;

	void configureViewport();
	void configureEditor();
	void configureContent();
	void configureAbout();
	void configureShortcuts();
	void configureDatabaseWidget();

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

public slots:
	void saveSettings();

};

