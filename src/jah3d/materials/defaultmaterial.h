#ifndef DEFAULTMATERIAL_H
#define DEFAULTMATERIAL_H

#include "../graphics/material.h"
#include <QOpenGLShaderProgram>
#include <QColor>

class QOpenGLFunctions;

namespace jah3d
{

class Texture;

class DefaultMaterial:public Material
{
    float textureScale;
    QColor ambientColor;

    bool useNormalTex;
    float normalIntensity;
    QOpenGLTexture* normalTexture;

    QColor diffuseColor;
    bool useDiffuseTex;
    QSharedPointer<Texture> diffuseTexture;

    float shininess;
    bool useSpecularTex;
    QColor specularColor;
    QOpenGLTexture* specularTexture;

    float reflectionInfluence;
    bool useReflectonTex;
    QOpenGLTexture* reflectionTexture;

public:

    void setDiffuseTexture(QSharedPointer<Texture> tex);

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


    void begin(QOpenGLFunctions* gl);
    void end();

    static QSharedPointer<DefaultMaterial> create()
    {
        return QSharedPointer<DefaultMaterial>(new DefaultMaterial());
    }

private:
    DefaultMaterial();
};

}

#endif // DEFAULTMATERIAL_H
