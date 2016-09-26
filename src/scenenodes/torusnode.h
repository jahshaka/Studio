/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TORUSNODE_H
#define TORUSNODE_H

class SceneNode;
namespace Qt3DCore
{
    class QEntity;
}

namespace Qt3DRender
{
    class QLight;
    class QTorusMesh;
}

class QXmlStreamWriter;
class QXmlStreamReader;

class TorusNode:public SceneNode
{
public:
    Qt3DRender::QTorusMesh* torus;

    TorusNode(Qt3DCore::QEntity* entity);

    void setRadius(float radius);
    void setMinorRadius(float minorRadius);
    void setRings(int rings);
    void setSlices(int slices);

    virtual void writeData(QXmlStreamWriter& xml);
    virtual void readData(QXmlStreamReader& xml);

    static TorusNode* createTorus(QString name);
};

#endif // TORUSNODE_H
