#ifndef DEFAULTMATERIAL_H
#define DEFAULTMATERIAL_H

#include "../graphics/material.h"
#include <QOpenGLShaderProgram>
#include <QColor>

class QOpenGLFunctions_3_2_Core;

namespace iris
{

class Texture;
class Texture2D;

class DefaultMaterial:public Material
{
    float textureScale;
    QColor ambientColor;

    bool useNormalTex;
    float normalIntensity;
    //QOpenGLTexture* normalTexture;
    QSharedPointer<Texture2D> normalTexture;

    QColor diffuseColor;
    bool useDiffuseTex;
    QSharedPointer<Texture2D> diffuseTexture;

    float shininess;
    bool useSpecularTex;
    QColor specularColor;
    //QOpenGLTexture* specularTexture;
    QSharedPointer<Texture2D> specularTexture;

    float reflectionInfluence;
    bool useReflectionTex;
    //QOpenGLTexture* reflectionTexture;
    QSharedPointer<Texture2D> reflectionTexture;

public:

    void setDiffuseTexture(QSharedPointer<Texture2D> tex);
    QString getDiffuseTextureSource();

    void setAmbientColor(QColor col);
    QColor getAmbientColor()
    {
        return ambientColor;
    }

    void setDiffuseColor(QColor col);
    QColor getDiffuseColor();

    void setNormalTexture(QSharedPointer<Texture2D> tex);
    QString getNormalTextureSource();

    void setNormalIntensity(float intensity);
    float getNormalIntensity();

    void setSpecularTexture(QSharedPointer<Texture2D> tex);
    QString getSpecularTextureSource();

    void setSpecularColor(QColor col);
    QColor getSpecularColor();

    void setShininess(float intensity);
    float getShininess();

    void setReflectionTexture(QSharedPointer<Texture2D> tex);
    QString getReflectionTextureSource();

    void setTextureScale(float scale);
    float getTextureScale();

    void setReflectionInfluence(float intensity);
    float getReflectionInfluence();


    void begin(QOpenGLFunctions_3_2_Core* gl) override;
    void end(QOpenGLFunctions_3_2_Core* gl) override;

    static QSharedPointer<DefaultMaterial> create()
    {
        return QSharedPointer<DefaultMaterial>(new DefaultMaterial());
    }

private:
    DefaultMaterial();
};

}

#endif // DEFAULTMATERIAL_H
