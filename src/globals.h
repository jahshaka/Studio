/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GLOBALS_H
#define GLOBALS_H

class Project;
class SceneViewWidget;

#include <QMap>
#include <QPointer>
#include <QString>
#include "src/core/subscriber.h"

class Database;
class Globals
{
public:
    static float animFrameTime;//maybe this needs to go
    static QString appWorkingDir;
    static Project* project;
	static Database* db;
    static SceneViewWidget* sceneViewWidget;
    static QMap<QString, QString> assetNames;

	static QPointer<Subscriber> eventSubscriber;
};

#endif // GLOBALS_H
