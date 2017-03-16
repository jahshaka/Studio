/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef IVIEWSOURCE_H
#define IVIEWSOURCE_H


namespace iris{

class IViewSource
{
public:
    virtual QVector3D getPosition() = 0;
    virtual QMatrix4x4 getViewMatrix() = 0;
    virtual QMatrix4x4 getProjMatrix() = 0;
    virtual bool supportsVr();
    virtual float getNearClip();
    virtual float getFarClip();
};

}

#endif // IVIEWSOURCE_H
