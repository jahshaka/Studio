/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef GIZMOBASE_H
#define GIZMOBASE_H

#include <QString>

class GizmoHandleBase
{
public:

};

class GizmoBase
{
public:
    bool isGizmoHandle(QString name);

    GizmoHandleBase* getGizmoHandle(QString name);

    void addGizmoHandle(QString name,GizmoHandleBase* handle);
};

#endif // GIZMOBASE_H
