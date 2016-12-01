#ifndef MATERIAL_H
#define MATERIAL_H

//#include "shaderprogram.h"
//#include "texture.h"
#include <QSharedPointer>
#include <QOpenGLShaderProgram>

class QOpenGLShaderProgram;
class QOpenGLTexture;
class QOpenGLFunctions_3_2_Core;

namespace jah3d
{

class Material;
typedef QSharedPointer<Material> MaterialPtr;

class Texture2D;
typedef QSharedPointer<Texture2D> Texture2DPtr;

class Texture;
typedef QSharedPointer<Texture> TexturePtr;

struct MaterialTexture
{
    Texture2DPtr texture;
    QString name;
};

class Material
{
public:
    //ShaderProgramPtr program;
    QOpenGLShaderProgram* program;
    QMap<QString, Texture2DPtr> textures;
    //QMap<QString, MaterialTexture*> textures;
    //QList<MaterialTexture*> textures;

    bool acceptsLighting;

    Material()
    {
        acceptsLighting = true;
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
    virtual void begin(QOpenGLFunctions_3_2_Core* gl);

    /**
     * Called after endering a pritimitive.
     * This is used to cleanup after rendering
     */
    virtual void end(QOpenGLFunctions_3_2_Core* gl);

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
};

}

#endif // MATERIAL_H
