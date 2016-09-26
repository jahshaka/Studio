/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef GIZMOTRANSFORM_H
#define GIZMOTRANSFORM_H

#include <QString>

class GizmoTransform
{
public:
    /**
     * @brief called when a handle is selected form picker
     * this is where the transformation begins
     * @param name
     * @return
     */
    virtual bool onHandleSelected(QString name,int x,int y)
    {
        Q_UNUSED(name);
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }

    virtual bool onMousePress(int x,int y)
    {
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }

    virtual bool onMouseMove(int x,int y)
    {
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }

    virtual bool onMouseRelease(int x,int y)
    {
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }

    virtual bool isGizmoHit(int x,int y)
    {
        Q_UNUSED(x);
        Q_UNUSED(y);
        return false;
    }
};

#endif // GIZMOTRANSFORM_H
