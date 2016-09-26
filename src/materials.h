/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALS_H
#define MATERIALS_H

#include <QMaterial>
#include <QEffect>
#include <QTechnique>
#include <QBlendEquation>
#include <QBlendEquationArguments>
#include <QParameter>
#include <QAbstractTextureImage>
#include <QTextureWrapMode>
#include <QTextureImage>
#include <QTextureWrapMode>
#include <QGraphicsApiFilter>
#include <Qt3DRender/qtexture.h>
#include <Qt3DRender/qfilterkey.h>
#include <Qt3DRender/qcullface.h>
#include <Qt3DRender/qdepthtest.h>

namespace Qt3DRender
{
    class QAbstractTextureProvider;
    class QParameter;
    class QTextureImage;
    class QTechnique;
}

#include "materials/gizmomaterial.h"
#include "materials/flatshadedmaterial.h"
#include "materials/colormaterial.h"
#include "materials/endlessplanematerial.h"
#include "materials/billboardmaterial.h"
#include "materials/reflectivematerial.h"
#include "materials/refractivematerial.h"
#include "materials/advancematerial.h"

//note to self:
//techniques are per opengl version
//passes are per scene render calls


#endif // MATERIALS_H
