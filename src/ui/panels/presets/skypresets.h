/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SKYPRESETS_H
#define SKYPRESETS_H

#include <QWidget>

namespace Ui {
class SkyPresets;
}

class QListWidgetItem;
class MainWindow;
class Database;
class Project;

class SkyPresets : public QWidget
{
    Q_OBJECT

public:
    explicit SkyPresets(QWidget *parent = 0);
    ~SkyPresets();

    void addSky(QString path, QString name);
    void addCubeSky(QString path, QString name);

    void setMainWindow(MainWindow* mainWindow)
    {
        this->mainWindow = mainWindow;
    }

	Database *db;
	void setDatabase(Database *db)
	{
		this->db = db;
	}

	/// The one live Project (Phase 4: was the Globals::project static).
	Project *project = nullptr;
	void setProject(Project *p)
	{
		this->project = p;
	}

protected slots:
    void applySky(QListWidgetItem* item);
    void applyCubeSky(QListWidgetItem* item);

signals:
	void changeSceneCubemap(QStringList guids);

private:
    Ui::SkyPresets *ui;
    QList<QString> skies;
    QList<QString> alternativeSkies;
    MainWindow* mainWindow;
};

#endif // SKYPRESETS_H
