/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SUBSCRIBER_H
#define SUBSCRIBER_H

#include "irisgl/src/scenegraph/scene.h"
#include "core/project.h"

#include <QObject>

class Subscriber : public QObject
{
	Q_OBJECT

public:
	Subscriber() = default;

signals:
	void updateAssetSkyItemFromSkyPropertyWidget(const QString &guid, iris::SkyType skyType);
};

#endif // SUBSCRIBER_H