/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SUBSCRIBER_H
#define SUBSCRIBER_H

#include "irisgl/document/scenegraph/scene.h"
#include "data/project.h"

#include <QObject>

class Subscriber : public QObject
{
	Q_OBJECT

public:
	explicit Subscriber(QObject *parent = nullptr) : QObject(parent) {}

signals:
	void updateAssetSkyItemFromSkyPropertyWidget(const QString &guid, iris::SkyType skyType);
};

#endif // SUBSCRIBER_H