/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef JAHMATERIAL_H
#define JAHMATERIAL_H

#include <QColor>
#include "materialpresets.h"

namespace Qt3DRender
{
    class QAbstractTextureImage;
    class QTextureImage;
    class QMaterial;
    //class QEffect;
    class QTechnique;
    class QParameter;
    class QTexture2D;
}

class AdvanceEffect:public Qt3DRender::QEffect
{
public:
    AdvanceEffect();

    Qt3DRender::QTechnique *tech;
};

/**
 * @brief Generalized material class
 */
class AdvanceMaterial:public Qt3DRender::QMaterial
{
public:
    explicit AdvanceMaterial();

    void setAmbientColor(QColor col);
    QColor getAmbientColor();

    void setDiffuseColor(QColor col);
    QColor getDiffuseColor();

    void setDiffuseTexture(Qt3DRender::QTextureImage* image);
    Qt3DRender::QTextureImage* getDiffuseTexture();
    QString getDiffuseTextureSource();
    void removeDiffuseTexture();

    void setNormalTexture(Qt3DRender::QTextureImage* image);
    Qt3DRender::QTextureImage* getNormalTexture();
    QString getNormalTextureSource();
    void removeNormalTexture();

    void setNormalIntensity(float intensity);
    float getNormalIntensity();

    void setSpecularTexture(Qt3DRender::QTextureImage* image);
    Qt3DRender::QTextureImage* getSpecularTexture();
    QString getSpecularTextureSource();
    void removeSpecularTexture();

    void setSpecularColor(QColor col);
    QColor getSpecularColor();

    void setShininess(float intensity);
    float getShininess();

    void setReflectionTexture(Qt3DRender::QTextureImage* image);
    Qt3DRender::QTextureImage* getReflectionTexture();
    QString getReflectionTextureSource();
    void removeReflectionTexture();

    void setTextureScale(float scale);
    float getTextureScale();

    void setReflectionInfluence(float intensity);
    float getReflectionInfluence();

    void setFogEnabled(bool enabled);
    void setFogColor(QColor color);
    void setFogStart(float fogStart);
    void setFogEnd(float fogEnd);
    void updateFogParams(bool enabled,QColor color,float start,float end);

    AdvanceMaterial* duplicate();

    void applyPreset(MaterialPreset preset);

private:
    Qt3DRender::QTexture2D* diffuseTexture;
    Qt3DRender::QTexture2D* normalTexture;
    Qt3DRender::QTexture2D* specularTexture;
    Qt3DRender::QTexture2D* reflectionTexture;

    Qt3DRender::QParameter* ambientColorParam;

    Qt3DRender::QParameter* useDiffuseTexParam;
    Qt3DRender::QParameter* diffuseTexParam;
    Qt3DRender::QParameter* diffuseColorParam;

    Qt3DRender::QParameter* useNormalTexParam;
    Qt3DRender::QParameter* normalTexParam;
    Qt3DRender::QParameter* normalIntensityParam;

    Qt3DRender::QParameter* useSpecularTexParam;
    Qt3DRender::QParameter* specularTexParam;
    Qt3DRender::QParameter* specularColorParam;
    Qt3DRender::QParameter* shininessParam;

    Qt3DRender::QParameter* useReflectionTexParam;
    Qt3DRender::QParameter* reflectionTexParam;
    Qt3DRender::QParameter* reflectionInfluenceParam;

    Qt3DRender::QParameter* textureScaleParam;

    Qt3DRender::QParameter* fogEnabledParam;
    Qt3DRender::QParameter* fogStartParam;
    Qt3DRender::QParameter* fogEndParam;
    Qt3DRender::QParameter* fogColorParam;
};

#endif // JAHMATERIAL_H
