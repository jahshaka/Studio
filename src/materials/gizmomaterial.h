/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GIZMOMATERIAL_H
#define GIZMOMATERIAL_H

class GizmoEffect:public Qt3DRender::QEffect
{
public:
    GizmoEffect();
    Qt3DRender::QTechnique *tech;
};

class GizmoMaterial:public Qt3DRender::QMaterial
{
    Qt3DRender::QParameter* colorParam;
    Qt3DRender::QParameter* showParam;
public:
    GizmoMaterial();
    void setColor(QColor color);
    void showHalf(bool shouldShowHalf);
};

#endif // GIZMOMATERIAL_H
