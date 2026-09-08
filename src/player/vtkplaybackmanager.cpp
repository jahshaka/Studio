#include "VtkPlaybackManager.h"

#include <algorithm>

VtkPlaybackManager::VtkPlaybackManager(QObject *parent)
    : QObject(parent)
    , update_timer_(new QTimer(this))
{
    update_timer_->setInterval(16);
    connect(update_timer_, &QTimer::timeout, this, &VtkPlaybackManager::onUpdateTimer);
}

VtkPlaybackManager::~VtkPlaybackManager()
{
    update_timer_->stop();
}

void VtkPlaybackManager::setRenderer(vtkRenderer* renderer)
{
    renderer_ = renderer;
}

void VtkPlaybackManager::startPlayback()
{
    if (renderer_ == nullptr) {
        return;
    }

    is_playing_ = true;

    elapsed_timer_.start();
    update_timer_->start();
}

void VtkPlaybackManager::stopPlayback()
{
    if (!is_playing_) {
        return;
    }

    is_playing_ = false;
    update_timer_->stop();

}

void VtkPlaybackManager::onUpdateTimer()
{
    if (!is_playing_ || renderer_ == nullptr) {
        return;
    }

    const float delta_seconds = static_cast<float>(elapsed_timer_.restart()) / 1000.0f;
    emit frameAdvanced(delta_seconds);
}

void VtkPlaybackManager::addSceneActor(vtkSmartPointer<vtkActor> actor)
{
    if (!actor) {
        return;
    }

    auto it = std::find(actors_.begin(), actors_.end(), actor);
    if (it == actors_.end()) {
        actors_.push_back(actor);
    }
}

void VtkPlaybackManager::removeSceneActor(vtkSmartPointer<vtkActor> actor)
{
    actors_.erase(std::remove(actors_.begin(), actors_.end(), actor), actors_.end());
}
