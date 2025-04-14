/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GUIDMANAGER_H
#define GUIDMANAGER_H

#include <QUuid>

class GUIDManager
{
public:
    GUIDManager();

    static QString generateGUID() {
        auto id = QUuid::createUuid();
        auto guid = id.toString().remove(0, 1);
        guid.chop(1);
        return guid;
    }
};

#endif // GUIDMANAGER_H
