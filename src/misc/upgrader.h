/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef UPGRADER_H
#define UPGRADER_H

#include <QObject>

class Upgrader : protected QObject
{
	Q_OBJECT

public:
	Upgrader() = default;
	void checkIfDeprecatedVersion();
	void checkIfSchemaNeedsUpdating();
};

#endif // UPGRADER_H