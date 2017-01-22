/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QDir>

#include "materialhelper.h"
#include "../irisglfwd.h"
#include "assimp/scene.h"
#include "assimp/mesh.h"
#include "assimp/material.h"
#include "assimp/matrix4x4.h"
#include "assimp/vector3.h"
#include "assimp/quaternion.h"
#include "defaultmaterial.h"
#include "../graphics/texture2d.h"

namespace iris
{

QColor getAiMaterialColor(aiMaterial* aiMat, const char* pKey, unsigned int type,
                          unsigned int idx)
{
    aiColor3D col;
    aiMat->Get(pKey, type, idx, col);
    auto color = QColor(col.r * 255, col.g * 255, col.b * 255, 255);

    return color;
}

QString getAiMaterialTexture(aiMaterial* aiMat, aiTextureType texType)
{
    if (aiMat->GetTextureCount(texType) > 0) {
        aiString tex;
        aiMat->GetTexture(texType, 0, &tex);

        return QString(tex.C_Str());
    }

    return QString();
}

float getAiMaterialFloat(aiMaterial* aiMat, const char* pKey, unsigned int type,
                          unsigned int idx)
{
    float floatVal;
    aiMat->Get(pKey, type, idx, floatVal);
    return floatVal;
}

// http://www.assimp.org/lib_html/materials.html
DefaultMaterialPtr MaterialHelper::createMaterial(aiMaterial* aiMat,QString assetPath)
{
    auto mat =  DefaultMaterial::create();
    mat->setDiffuseColor(getAiMaterialColor(aiMat, AI_MATKEY_COLOR_DIFFUSE));
    mat->setSpecularColor(getAiMaterialColor(aiMat, AI_MATKEY_COLOR_SPECULAR));
    mat->setAmbientColor(getAiMaterialColor(aiMat, AI_MATKEY_COLOR_AMBIENT));
    //mat->setEmissiveColor(getAiMaterialColor(aiMat,AI_MATKEY_COLOR_EMISSIVE));

    mat->setShininess(getAiMaterialFloat(aiMat, AI_MATKEY_SHININESS));

    if(!assetPath.isEmpty())
    {
        auto diffuseTex = getAiMaterialTexture(aiMat, aiTextureType_DIFFUSE);
        mat->setDiffuseTexture(Texture2D::load(
                                   QDir::cleanPath(assetPath + QDir::separator() + diffuseTex)));
    }

    return mat;
}

}
