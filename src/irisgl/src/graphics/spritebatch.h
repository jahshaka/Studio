#ifndef SPRITEBATCH_H
#define SPRITEBATCH_H

#include "../irisglfwd.h"
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QColor>

namespace iris
{

struct SpriteBatchVertex
{
    QVector3D pos;
    QVector2D texCoord;
    QColor color;
};

struct SpriteBatchItem
{
    Texture2DPtr tex;
    float sortKey;

    SpriteBatchVertex vertTL;
    SpriteBatchVertex vertTR;
    SpriteBatchVertex vertBL;
    SpriteBatchVertex vertBR;

    void setData(float x, float y, float dx, float dy,
                 float w, float h, float sin, float cos,
                 QColor color, QVector2D texCoordTl, QVector2D texCoordBR, float depth);

    void setData(float x, float y, float w, float h,
                 QColor color, QVector2D texCoordTl, QVector2D texCoordBR, float depth);
};

class SpriteBatch
{
public:
    static SpriteBatchPtr create(GraphicsDevicePtr graphicsDevice);

    void begin();
    void draw(Texture2DPtr texture, QRect rect, QColor color);
    void draw(Texture2DPtr texture, QVector2D rect, QColor color);
    void end();

private:
    SpriteBatch(GraphicsDevicePtr graphicsDevice);
    SpriteBatchItem* addItem();
    void addVertexToData(SpriteBatchVertex& vertex);

    QVector<SpriteBatchItem*> items;
    QVector<SpriteBatchItem*> _pool;

    VertexBufferPtr vertexBuffer;
    QVector<float> data;

    ShaderPtr spriteShader;
    GraphicsDevicePtr graphics;
};

}

#endif // SPRITEBATCH_H
