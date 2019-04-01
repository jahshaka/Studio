/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef CUBEMAPPROPERTY_H
#define CUBEMAPPROPERTY_H

#include <QWidget>
#include <QJsonDocument>
#include <QJsonObject>

#include <QSharedPointer>

#include "irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

#include "irisgl/src/scenegraph/scene.h"

class Database;

class CubeMapPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
	CubeMapPropertyWidget();
    ~CubeMapPropertyWidget();

    void setCubeMapGuid(const QString&);
    void setDatabase(Database*);
    void setScene(QSharedPointer<iris::Scene> scene);

    void removeDependency(QString);

    void leaveEvent(QEvent*) override;

	QJsonObject valuesAsJson() const;

public slots:
    void onSlotChanged(QString value, int index);

private:
    QSharedPointer<iris::Scene> scene;

    QString guid;

    TexturePickerWidget *cubemapFront;
    TexturePickerWidget *cubemapBack;
    TexturePickerWidget *cubemapLeft;
    TexturePickerWidget *cubemapRight;
    TexturePickerWidget *cubemapTop;
    TexturePickerWidget *cubemapBottom;

    Database *db;
};

#endif // CUBEMAPPROPERTY
