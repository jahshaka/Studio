#ifndef VTKPLAYBACKMANAGER_H
#define VTKPLAYBACKMANAGER_H

#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <vector>
#include <vtkSmartPointer.h>
#include <vtkActor.h>
#include <vtkRenderer.h>
#include <vtkProp.h>

namespace vtkmeta {
class Node;
}

class VtkPlaybackManager : public QObject
{
    Q_OBJECT
public:
    explicit VtkPlaybackManager(QObject *parent = nullptr);
    ~VtkPlaybackManager();

    void setRenderer(vtkRenderer* renderer);
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

signals:
    void frameAdvanced(float delta_seconds);

private slots:
    void onUpdateTimer();

private:
    bool is_playing_ = false;
    QElapsedTimer elapsed_timer_;
    QTimer* update_timer_ = nullptr;

    vtkRenderer* renderer_ = nullptr;
    std::vector<vtkSmartPointer<vtkActor>> actors_;
};

#endif // VTKPLAYBACKMANAGER_H
