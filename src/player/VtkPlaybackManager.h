#ifndef VTKPLAYBACKMANAGER_H
#define VTKPLAYBACKMANAGER_H

#include <QObject>
#include <QVector2D>
#include <QTimer>
#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkProp.h>
#include <QDateTime> // For time tracking

namespace vtkmeta {
class Node;
class Scence;
}

class VtkPlaybackManager : public QObject
{
    Q_OBJECT
public:
    explicit VtkPlaybackManager(QObject *parent = nullptr);
    ~VtkPlaybackManager();

    void setRenderer(vtkSmartPointer<vtkRenderer> renderer);
    void addSceneActor(vtkSmartPointer<vtkActor> actor);
    void removeSceneActor(vtkSmartPointer<vtkActor> actor);
    const std::vector<vtkSmartPointer<vtkActor>>& getActors() const { return actors_; }
//    void addNode(std::shared_ptr<vtkmeta::Node> node);

    void startPlayback();
    void stopPlayback();
    bool isScenePlaying() const { return is_playing_; }

//    void handleMouseMove(int x, int y, bool left, bool right, bool middle);
//    void handleMouseWheel(int delta);
 //   void updateCamera(float dt);

private slots:
    void update(float dt);

private:
    bool is_playing_ = false;
    float last_time_ = 0.0f;
    QTimer* update_timer_;

    vtkRenderer* renderer_;
    std::vector<vtkSmartPointer<vtkActor>> actors_;
    std::vector<std::shared_ptr<vtkmeta::Node>> nodes_;
};

#endif // VTKPLAYBACKMANAGER_H
