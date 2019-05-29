/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTMATERIALPANEL_H
#define PROJECTMATERIALPANEL_H

#include <QWidget>
#include <QListWidget>
#include <QListWidgetItem>
#include "globals.h"
#include "assetpanel.h"
#include "core/project.h"
#include "core/database/database.h"
#include "core/materialpreset.h"

#include "assetwidget.h"

class MainWindow;
class MaterialPreset;

class ProjectMaterialPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ProjectMaterialPanel(Database* handle, QWidget *parent = 0);
    ~ProjectMaterialPanel();



	void setMainWindow(MainWindow* mainWindow) {
		this->mainWindow = mainWindow;
	}

	MainWindow* mainWindow;
	Database* database;
	void addItem(const AssetRecord& assetData);
	void refresh();

	ListWidget* listWidget;

	QPoint startPos;
	bool draggingItem;

protected:
	bool eventFilter(QObject*, QEvent*) override;

signals:
	void assetItemSelected(QListWidgetItem*);

};

#endif // MATERIALSETS_H
