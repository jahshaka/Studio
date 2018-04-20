#include "contentmanager.h"
#include "../graphics/graphicsdevice.h"
#include "../graphics/mesh.h"
#include "../graphics/texture2d.h"
#include "../graphics/font.h"
#include "../graphics/shader.h"

namespace iris
{

ContentManager::ContentManager(GraphicsDevicePtr graphics)
{
    this->graphics = graphics;
}

MeshPtr ContentManager::loadMesh(QString meshPath)
{
    return Mesh::loadMesh(meshPath);
}

Texture2DPtr ContentManager::loadTexture(QString texturePath, bool flipY)
{
    return Texture2D::load(texturePath, flipY);
}

FontPtr ContentManager::loadDefaultFont(int size)
{
    return Font::create(graphics, size);
}

FontPtr ContentManager::loadFont(QString fontName, int size)
{
    return Font::create(graphics, fontName, size);
}

ShaderPtr ContentManager::loadShader(QString vertexShaderPath, QString fragmentShaderPath)
{
    return Shader::load(vertexShaderPath, fragmentShaderPath);
}

ContentManagerPtr ContentManager::create(GraphicsDevicePtr graphics)
{
    return ContentManagerPtr(new ContentManager(graphics));
}



}
