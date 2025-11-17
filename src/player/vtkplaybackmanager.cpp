#include "VtkPlaybackManager.h"
#include <vtkRenderWindow.h>
#include <vtkCamera.h>
#include <QDebug>

#include "vtkmeta/node.h"

VtkPlaybackManager::VtkPlaybackManager(QObject *parent)
    : QObject(parent)
    , is_playing_(false)
    , update_timer_(new QTimer(this))
    , renderer_(nullptr)
{
    // 设置更新频率，例如 60 FPS
    //connect(update_timer_, &QTimer::timeout, this, &VtkPlaybackManager::update);
    update_timer_->setInterval(16); // ~ 60 FPS (1000ms / 60 ≈ 16.6ms)
}

VtkPlaybackManager::~VtkPlaybackManager()
{
    update_timer_->stop();
}

void VtkPlaybackManager::setRenderer(vtkSmartPointer<vtkRenderer> renderer)
{
    renderer_ = renderer;
}

void VtkPlaybackManager::startPlayback()
{
    if (!renderer_) return;

    is_playing_ = true;

    last_time_ = static_cast<float>(QDateTime::currentMSecsSinceEpoch());
    update_timer_->start();

    qDebug() << "Playback started.";
}

void VtkPlaybackManager::stopPlayback()
{
    if (!is_playing_) return;

    is_playing_ = false;
    update_timer_->stop();

    // 清理物理环境中的角色控制器等

    qDebug() << "Playback stopped.";
}

void VtkPlaybackManager::update(float dt)
{
    if (!is_playing_ || !renderer_) return;

    // for (auto& n : nodes_) {
    //     auto t = n->getTransform();
    //     //t->RotateY(30.0f * dt); // 每秒 30 度
    // }
}

void VtkPlaybackManager::addSceneActor(vtkSmartPointer<vtkActor> actor)
{
    if (!actor) return;

    auto it = std::find(actors_.begin(), actors_.end(), actor);
    if (it == actors_.end()) {
        actors_.push_back(actor);
    }
}

void VtkPlaybackManager::removeSceneActor(vtkSmartPointer<vtkActor> actor)
{
    actors_.erase(std::remove(actors_.begin(), actors_.end(), actor), actors_.end());
}

// void VtkPlaybackManager::addNode(std::shared_ptr<vtkmeta::Node> node)
// {
//     if (node) {
//         nodes_.push_back(node);
//         if (renderer_ && node->getVTKActor()) {
//             renderer_->AddActor(node->getVTKActor());
//         }
//     }
// }
