// Stand-ins for src/modules/materials/core/texturemanager.cpp, which drags the
// Studio database and main window into the link. The nodes compiled into this
// test only touch createTexture/removeTexture/getSingleton and
// GraphTexture::setImage; none of the database-backed methods are reachable.
#include "modules/materials/core/texturemanager.h"

TextureManager* TextureManager::instance = nullptr;

TextureManager* TextureManager::getSingleton()
{
    if (instance == nullptr) instance = new TextureManager();
    return instance;
}

GraphTexture* TextureManager::createTexture()
{
    auto tex = new GraphTexture();
    tex->dirty = true;
    textures.append(tex);
    return tex;
}

void TextureManager::removeTexture(GraphTexture* tex)
{
    textures.removeAt(textures.indexOf(tex));
}

void GraphTexture::setImage(QString path)
{
    this->path = path;
    this->dirty = true;
}
