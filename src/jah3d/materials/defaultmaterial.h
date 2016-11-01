#ifndef DEFAULTMATERIAL_H
#define DEFAULTMATERIAL_H

#include "../graphics/material.h"
#include <QOpenGLShaderProgram>
#include <QColor>

class QOpenGLFunctions;

namespace jah3d
{

class DefaultMaterial:public Material
{
    float textureScale;
    QColor ambientColor;

    bool useNormalTex;
    float normalIntensity;
    QOpenGLTexture* normalTexture;

    QColor diffuseColor;
    bool useDiffuseTex;
    QOpenGLTexture* diffuseTexture;

    float shininess;
    bool useSpecularTex;
    QColor specularColor;
    QOpenGLTexture* specularTexture;

    float reflectionInfluence;
    bool useReflectonTex;
    QOpenGLTexture* reflectionTexture;

    DefaultMaterial();

    void setDiffuseTexture(QOpenGLTexture* tex);

    void setAmbientColor(QColor col);
    QColor getAmbientColor();

    void setDiffuseColor(QColor col);
    QColor getDiffuseColor();

    void setNormalTexture(QOpenGLTexture* image);

    void setNormalIntensity(float intensity);
    float getNormalIntensity();

    void setSpecularTexture(QOpenGLTexture* image);

    void setSpecularColor(QColor col);
    QColor getSpecularColor();

    void setShininess(float intensity);
    float getShininess();

    void setReflectionTexture(QOpenGLTexture* image);

    void setTextureScale(float scale);
    float getTextureScale();

    void setReflectionInfluence(float intensity);
    float getReflectionInfluence();

    template<typename T>
    void setUniformValue(QString name,T value)
    {
        program->setUniformValue(name.toStdString().c_str(), value);
    }

    void begin(QOpenGLFunctions* gl);
    void end();
};

}

#endif // DEFAULTMATERIAL_H
