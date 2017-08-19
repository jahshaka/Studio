#ifndef RENDERVIEW_H
#define RENDERVIEW_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_2_Core>

#include "../irisglfwd.h"
#include "../graphics/graphicsdevice.h"

class QElapsedTimer;
class QTimer;

namespace iris
{

class RenderWidget : public QOpenGLWidget,
                     protected QOpenGLFunctions_3_2_Core
{
    QElapsedTimer* elapsedTimer;
    QTimer* updateTimer;
protected:
    GraphicsDevicePtr device;
    //ContentManagerPtr content;
    SpriteBatchPtr spriteBatch;
public:
    explicit RenderWidget(QWidget *parent);
    void initializeGL();
    void paintGL();

    virtual void start();
    virtual void update(float dt);
    virtual void render();
};

}

#endif // RENDERVIEW_H
