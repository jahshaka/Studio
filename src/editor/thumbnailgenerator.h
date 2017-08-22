#ifndef THUMBNAILGENERATOR_H
#define THUMBNAILGENERATOR_H

#include "../irisgl/src/irisglfwd.h"
#include <QThread>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_2_Core>
#include <QMutex>

enum class ThumbnailRequestType
{
    Material,
    Mesh
};

struct ThumbnailRequest
{
public:
    ThumbnailRequestType type;
    QString path;
    QString id;
};

struct ThumbnailResult
{
    ThumbnailRequestType type;
    QString path;
    QString id;
    QImage thumbnail;
};

class RenderThread : public QThread
{
public:
    QOffscreenSurface *surface;
    QOpenGLContext *context;

    iris::Texture2DPtr tex;
    iris::RenderTargetPtr renderTarget;

    iris::ForwardRendererPtr renderer;
    iris::ScenePtr scene;
    iris::MeshNodePtr meshNode;

    iris::CameraNodePtr cam;
    iris::CustomMaterialPtr material;

    QMutex requestMutex;
    QList<ThumbnailRequest> requests;

    void requestThumbnail(const ThumbnailRequest& request);

    void run() override;

    void initScene();
    void prepareScene(const ThumbnailRequest& request);
    void cleanupScene();

private:
    //ThumbnailRequest& getRequest();

signals:
    void thumbnailComplete(const ThumbnailResult& result);
};

// http://doc.qt.io/qt-5/qtquick-scenegraph-textureinthread-threadrenderer-cpp.html
class ThumbnialGenerator
{
    RenderThread* renderThread;
public:
    ThumbnialGenerator();
    void requestThumbnail(ThumbnailRequestType type, QString path, QString id = "");
};

#endif // THUMBNAILGENERATOR_H
