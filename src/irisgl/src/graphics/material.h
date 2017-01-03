/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIAL_H
#define MATERIAL_H

#include "../irisglfwd.h"
#include <QOpenGLShaderProgram>

class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLFunctions_3_2_Core;

namespace iris
{

struct MaterialTexture
{
    Texture2DPtr texture;
    QString name;
};

class Material
{
public:
    QOpenGLShaderProgram* program;
    QMap<QString, Texture2DPtr> textures;

    bool acceptsLighting;
    int numTextures;

    Material()
    {
        acceptsLighting = true;
        numTextures = 0;
    }

    virtual ~Material()
    {

    }

    /**
     * Called at the beginning of rendering a primitive
     * This function is used by subclasses to bind the shader pass parameters,
     * bind textures and set states.
     * @param gl
     */
    virtual void begin(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene);

    /**
     * Called after endering a pritimitive.
     * This is used to cleanup after rendering
     */
    virtual void end(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene);

    /**
     * Adds texture to the material by name
     * If the material already contains the texture, it wil be replaced
     * @param name name of the texture uniform in the shader
     * @param textures texture pointer
     */
    void addTexture(QString name,Texture2DPtr textures);

    /**
     * Removes texture from material
     * @param name
     */
    void removeTexture(QString name);

    /**
     * binds all material's textures
     * @param gl
     */
    void bindTextures(QOpenGLFunctions_3_2_Core* gl);

    /**
     * unbinds all material textures
     * @param gl
     */
    void unbindTextures(QOpenGLFunctions_3_2_Core* gl);

    void createProgramFromShaderSource(QString vsFile,QString fsFile);

    template<typename T>
    void setUniformValue(QString name,T value)
    {
        program->setUniformValue(name.toStdString().c_str(), value);
    }

protected:
    /**
     * Sets the amount of textures your shader uses
     * This is to ensure that no extra textures states are left unset when the material is finishes
     * Also, this ensures that the material wont end up using a texture from a previously set material
     * if a texture the shader uses isnt set
     * @param count
     */
    void setTextureCount(int count);
};

}

#endif // MATERIAL_H
