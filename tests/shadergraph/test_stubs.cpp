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
    int index = textures.indexOf(tex);
    if (index >= 0) textures.removeAt(index);
}

// §3b migration: TextureNode::setTextureGuid resolves through this. The test
// slice has no database, so mirror the real no-database branch: keep the guid,
// leave the path unresolved.
GraphTexture* TextureManager::loadTextureFromGuid(QString guid)
{
    auto tex = createTexture();
    tex->guid = guid;
    return tex;
}

void GraphTexture::setImage(QString path)
{
    this->path = path;
    this->dirty = true;
}
