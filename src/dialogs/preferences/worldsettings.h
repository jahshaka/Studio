/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef WORLDSETTINGS_H
#define WORLDSETTINGS_H

#include <QWidget>

namespace Ui {
    class WorldSettings;
}

class SettingsManager;
class Database;

class WorldSettings : public QWidget
{
    Q_OBJECT
    SettingsManager *settings;

public:
    explicit WorldSettings(Database *db, SettingsManager* settings);
    ~WorldSettings();

    int outlineWidth;
    QColor outlineColor;
    QString defaultProjectDirectory;
    QString defaultEditorPath;
    bool showFps;
	bool autoSave;
	bool openInPlayer;
	bool autoUpdate;

	Database *db;

    void setupDirectoryDefaults();
    void setupOutline();
	void setupMouseControls();

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

public:
    Ui::WorldSettings *ui;
};

#endif // WORLDSETTINGS_H
