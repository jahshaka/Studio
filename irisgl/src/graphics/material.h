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
//#include "renderitem.h"
#include <QOpenGLShaderProgram>
#include "renderstates.h"

class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLFunctions_3_2_Core;

namespace iris
{
struct RenderLayer
{
	enum Value {
		Background = 1000,
		Opaque = 2000,
		AlphaTested = 3000,
		Transparent = 4000,
		Overlay = 5000,
		Gizmo = 6000
	};
};

struct MaterialTexture {
    Texture2DPtr texture;
    QString name;
};

class Material
{
	friend class ForwardRenderer;
public:
    int renderLayer;
    //QOpenGLShaderProgram* program;
	ShaderPtr shader;

	// if not null, this will be used to render the shadow instead
	// of the renderer's default shadow shader
	ShaderPtr shadowShader;

    QMap<QString, Texture2DPtr> textures;

    bool acceptsLighting;
    int numTextures;
    RenderStates renderStates;

    Material() {
        acceptsLighting = true;
        numTextures = 0;
    }

    virtual ~Material() {}

    void setRenderLayer(int layer) {
        this->renderLayer = layer;
    }

	void setShader(ShaderPtr shader);
	void setShadowShader(ShaderPtr shader);

	bool isFlagEnabled(QString flag);
	void enableFlag(QString flag);
	void disableFlag(QString flag);

    /**
     * Called at the beginning of rendering a primitive
     * This function is used by subclasses to bind the shader pass parameters,
     * bind textures and set states.
     * @param gl
     */
    virtual void begin(GraphicsDevicePtr device, ScenePtr scene);
    virtual void beginCube(GraphicsDevicePtr device, ScenePtr scene);

    /**
     * Called after endering a pritimitive.
     * This is used to cleanup after rendering
     */
    virtual void end(GraphicsDevicePtr device, ScenePtr scene);
    virtual void endCube(GraphicsDevicePtr device, ScenePtr scene);

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
    void bindTextures(GraphicsDevicePtr device);
    void bindCubeTextures(GraphicsDevicePtr device);

    /**
     * unbinds all material textures
     * @param gl
     */
    void unbindTextures(GraphicsDevicePtr device);

    void createProgramFromShaderSource(QString vsFile, QString fsFile);

    template<typename T>
    void setUniformValue(QString name,T value) {
        getProgram()->setUniformValue(name.toStdString().c_str(), value);
    }

	virtual MaterialPtr duplicate() {
		return MaterialPtr(new Material());
	}

	static MaterialPtr fromShader(ShaderPtr shader);

protected:
    /**
     * Sets the amount of textures your shader uses
     * This is to ensure that no extra textures states are left unset when the material is finishes
     * Also, this ensures that the material wont end up using a texture from a previously set material
     * if a texture the shader uses isnt set
     * @param count
     */
    void setTextureCount(int count);

	QSet<QString> flags;

	QOpenGLShaderProgram* getProgram();
};

}

#endif // MATERIAL_H
