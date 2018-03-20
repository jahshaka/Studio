#include "spritebatch.h"
#include "graphicsdevice.h"
#include "shader.h"
#include "texture2d.h"
#include "font.h"
#include "blendstate.h"
#include <QVector>
#include <QMatrix4x4>
#include <QOpenGLShaderProgram>

namespace iris
{

SpriteBatch::SpriteBatch(GraphicsDevicePtr graphicsDevice)
{
    graphics = graphicsDevice;
    spriteShader = Shader::load(graphics,
                          ":/assets/shaders/sprite.vert",
                          ":/assets/shaders/sprite.frag");

    // preallocate batches
    for(auto i=0;i<100;i++)
        _pool.append(new SpriteBatchItem());

    VertexLayout layout;
    layout.addAttrib(VertexAttribUsage::Position, GL_FLOAT, 3, sizeof(float)*3);
    layout.addAttrib(VertexAttribUsage::Color, GL_FLOAT, 4, sizeof(float)*4);
    layout.addAttrib(VertexAttribUsage::TexCoord0, GL_FLOAT, 2, sizeof(float)*2);
    vertexBuffer = VertexBuffer::create(layout);

    data.reserve(_pool.size() * layout.getStride());
}

SpriteBatchPtr SpriteBatch::create(GraphicsDevicePtr graphicsDevice)
{
    return SpriteBatchPtr(new SpriteBatch(graphicsDevice));
}

void SpriteBatch::begin()
{
    graphics->setShader(spriteShader);
    QMatrix4x4 view;
    view.setToIdentity();

    QMatrix4x4 proj;
    auto rect = graphics->getViewport();
    proj.setToIdentity();
    proj.ortho(0, rect.width(),rect.height(), 0, -10, 10);
    //proj.ortho(0, 500, 0, 300, -10, 10);

    //spriteShader->program->setUniformValue("u_viewMatrix", view);
    //spriteShader->program->setUniformValue("u_projMatrix", proj);

    graphics->setShaderUniform("u_viewMatrix", view);
    graphics->setShaderUniform("u_projMatrix", proj);
}

void SpriteBatch::draw(Texture2DPtr texture, QRect rect, QColor color)
{
    auto item = addItem();
    item->tex = texture;

    item->setData(rect.x(), rect.y(),
                  rect.width(), rect.height(),
                  color,
                  QVector2D(0,0), QVector2D(1,1),
                  0);
}

void SpriteBatch::draw(Texture2DPtr texture, QVector2D pos, QColor color)
{
    auto item = addItem();
    item->tex = texture;

    item->setData(pos.x(), pos.y(),
                  texture->getWidth(), texture->getHeight(),
                  color,
                  QVector2D(0,0), QVector2D(1,1),
                  0);
}

void SpriteBatch::drawString(FontPtr font, QString text, QVector2D pos, QColor color)
{
    int advance = 0;
    int spacing = 0;
    for(auto chr : text) {
        auto& glyph = font->glyphs[chr];
        draw(glyph.tex, pos, color);

        //advance += glyph.width + spacing;
        pos.setX(pos.x() + glyph.width + spacing);
    }
}

void SpriteBatch::end()
{
    int vertexCount = 0;
    int startIndex = 0;
    Texture2DPtr lastTex;
    data.clear();

    // testing
    auto gl = graphics->getGL();
    // remmeber to unforce these
    graphics->setRasterizerState(RasterizerState::CullNone, true);
    graphics->setDepthState(DepthState::None, true);
    graphics->setBlendState(BlendState::AlphaBlend, true);

    graphics->setShader(spriteShader);

    // build vbo and submit
    while(items.size()>0) {
        auto item = items.takeFirst();

        if(lastTex != item->tex) {
            // batch has ended, draw sprites
            graphics->setTexture(0, lastTex);

            vertexBuffer->setData(data.data(), data.size()*sizeof(float));//slow to do for each sprite, fix!
            graphics->setVertexBuffer(vertexBuffer);
            graphics->drawPrimitives(GL_TRIANGLES, startIndex, vertexCount);

            startIndex += vertexCount;
            vertexCount = 0;
            lastTex = item->tex;
        }

        // triangle 1
        addVertexToData(item->vertTL);
        addVertexToData(item->vertTR);
        addVertexToData(item->vertBL);
        vertexCount+=3;

        // triangle 2
        addVertexToData(item->vertTR);
        addVertexToData(item->vertBR);
        addVertexToData(item->vertBL);
        vertexCount+=3;

        // return to pool
        _pool.append(item);
    }

    // draw remaining
    graphics->setTexture(0, lastTex);
    vertexBuffer->setData(data.data(), data.size()*sizeof(float));//slow to do for each sprite, fix!
    graphics->setVertexBuffer(vertexBuffer);
    graphics->drawPrimitives(GL_TRIANGLES, startIndex, vertexCount);

}

SpriteBatchItem *SpriteBatch::addItem()
{
    SpriteBatchItem* item;
    if(_pool.size() > 0) {
        item = _pool.takeLast();
    } else {
        item = new SpriteBatchItem();
    }

    items.append(item);

    return item;
}

void SpriteBatch::addVertexToData(SpriteBatchVertex &vertex)
{
    data.append(vertex.pos.x()); data.append(vertex.pos.y()); data.append(vertex.pos.z());
    data.append(vertex.color.redF()); data.append(vertex.color.greenF()); data.append(vertex.color.blueF()); data.append(vertex.color.alphaF());
    data.append(vertex.texCoord.x()); data.append(vertex.texCoord.y());
}

void SpriteBatchItem::setData(float x, float y, float dx, float dy, float w, float h, float sin, float cos, QColor color, QVector2D texCoordTl, QVector2D texCoordBR, float depth)
{
    Q_ASSERT_X(false,"SpriteBatch","Not implemented");
}

void SpriteBatchItem::setData(float x, float y, float w, float h, QColor color, QVector2D texCoordTL, QVector2D texCoordBR, float depth)
{
    vertTL.pos = QVector3D(x, y, depth);
    vertTL.color = color;
    vertTL.texCoord = QVector2D(texCoordTL.x(), texCoordTL.y());

    vertTR.pos = QVector3D(x + w, y, depth);
    vertTR.color = color;
    vertTR.texCoord = QVector2D(texCoordBR.x(), texCoordTL.y());

    vertBL.pos = QVector3D(x, y + h, depth);
    vertBL.color = color;
    vertBL.texCoord = QVector2D(texCoordTL.x(), texCoordBR.y());

    vertBR.pos = QVector3D(x + w, y + h, depth);
    vertBR.color = color;
    vertBR.texCoord = QVector2D(texCoordBR.x(), texCoordBR.y());
}



}
