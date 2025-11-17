#include "vtkplayerinteractorstyle.h"

#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkCamera.h>
#include <vtkActor.h>
#include <vtkMath.h>
#include <vtkObjectFactory.h>

#include <QQuaternion>
#include <QVector3D>

#include "vtkplaybackmanager.h"

vtkStandardNewMacro(VtkPlayerInteractorStyle);

namespace  External {
bool isKeyDown(int key) {
    return false;
}
void createPickingConstraint(vtkActor* actor, double hitPos[3]) { /* ... */ }
void updatePickingConstraint(const double rayEnd[3], const double camPos[3]) { /* ... */ }
void cleanupPickingConstraint() { /* ... */ }
}

VtkPlayerInteractorStyle::VtkPlayerInteractorStyle()
{
    return;

    picker_ = vtkSmartPointer<vtkPropPicker>::New();
    this->SetMotionFactor(10.0);

    // 假设初始化 yaw/pitch
    yaw_ = 0.0f;
    pitch_ = 0.0f;
}

void VtkPlayerInteractorStyle::setPlaybackManager(VtkPlaybackManager *manager)
{
    playback_magager_ = manager;
}

void VtkPlayerInteractorStyle::OnLeftButtonDown()
{
    // if (current_mode_ == SceneMode::EditMode && playback_magager_) {
    //     int* pos = this->Interactor->GetEventPosition();

    //     // --- 修改后的 DoObjectPicking 逻辑 ---
    //     if (picker_->Pick(pos[0], pos[1], 0, this->CurrentRenderer)) {
    //         vtkProp* pickedProp = picker_->GetViewProp(); // 使用通用的 vtkProp
    //         double hitPos[3];
    //         picker_->GetPickPosition(hitPos);

    //         if (pickedProp && playback_magager_) {
    //             // 启动物理拖拽约束
    //             playback_magager_->startPhysicsPicking(pickedProp, hitPos);
    //             is_object_picked_ = true; // 标记正在拖拽
    //         }
    //     }
    //     // ---------------------------------------
    // }

    // 调用父类方法处理 VTK 默认的左键交互
    vtkInteractorStyleTrackballCamera::OnLeftButtonDown();
}

void VtkPlayerInteractorStyle::OnRightButtonDown()
{
    // // 记录右键按下状态
    // right_mouse_down_ = true;
    // // 获取当前的 Yaw 和 Pitch
    // getYawPitchFromVtkCamera(yaw_, pitch_);

    // // 阻止父类默认行为，我们自定义旋转
    vtkInteractorStyleTrackballCamera::OnRightButtonDown();
}

void VtkPlayerInteractorStyle::OnRightButtonUp()
{
//    right_mouse_down_ = false;
    vtkInteractorStyleTrackballCamera::OnRightButtonUp();
}

void VtkPlayerInteractorStyle::OnMouseWheelForward()
{
    // 替换 PlayerMouseController::onMouseWheel
    // VTK 默认执行 Dolly (缩放)，这里我们保持默认即可
    vtkInteractorStyleTrackballCamera::OnMouseWheelForward();
}

void VtkPlayerInteractorStyle::OnMouseWheelBackward()
{
    // 替换 PlayerMouseController::onMouseWheel
    vtkInteractorStyleTrackballCamera::OnMouseWheelBackward();
}

void VtkPlayerInteractorStyle::OnMouseMove()
{
    // int* currentPos = this->Interactor->GetEventPosition();
    // int* lastPos = this->Interactor->GetLastEventPosition();

    // // 替换 PlayerMouseController::onMouseMove - rightMouseDown 逻辑
    // if (right_mouse_down_) {
    //     int dx = currentPos[0] - lastPos[0];
    //     int dy = currentPos[1] - lastPos[1];

    //     // 累加 Yaw/Pitch (替换 PlayerMouseController 中的逻辑)
    //     this->yaw_ += dx / 10.0f;
    //     this->pitch_ += dy / 10.0f;

    //     // 更新 VTK 相机
    //     setVtkCameraRotation(this->yaw_, this->pitch_);

    //     this->Interactor->GetRenderWindow()->Render();
    // }

    // // 物理拖拽更新 (替换 PlayerMouseController::onMouseMove 中的物理更新)
    // // if (currentMode == SceneMode::PlayMode && leftMouseDown && isPicked) {
    // //     // ... 复杂的射线计算和物理更新调用
    // //     double rayEnd[3] = {0.0};
    // //     double camPos[3] = {0.0};
    // //     this->CurrentRenderer->GetActiveCamera()->GetPosition(camPos);
    // //     // calculateRayEndFromScreen(currentPos[0], currentPos[1], rayEnd); // 假设函数
    // //     External::updatePickingConstraint(rayEnd, camPos);
    // //     this->Interactor->GetRenderWindow()->Render();
    // // }

    // // 2. 物理拖拽更新 (左键按下且正在拖拽)
    // if (is_object_picked_ && playback_magager_ /*&& this->Interactor->GetLeftButton()*/) {

    //     // 计算新的射线方向和终点
    //     double camPos[3];
    //     this->CurrentRenderer->GetActiveCamera()->GetPosition(camPos);

    //     double rayEnd[3];
    //     // VTK 拾取器可以将屏幕坐标转换为世界坐标的射线
    //     // 这里需要使用一个辅助函数来计算屏幕上鼠标位置对应的世界坐标点 rayEnd

    //     // 🚨 占位符：你需要实现一个函数来计算屏幕点对应的 3D 射线终点
    //     // calculateRayEndFromScreen(currentPos[0], currentPos[1], rayEnd);

    //     // 临时简化实现 (假设 rayEnd 位于一个远处的平面上)
    //     // 实际应用中，这需要一个投影计算
    //     rayEnd[0] = 0.0; rayEnd[1] = 0.0; rayEnd[2] = 0.0; // 这是一个占位符，需要真实的计算

    //     // 调用 PlaybackManager 更新物理拖拽目标
    //     playback_magager_->updatePhysicsPicking(rayEnd, camPos);

    //     this->Interactor->GetRenderWindow()->Render();
    // }

    vtkInteractorStyleTrackballCamera::OnMouseMove();
}

void VtkPlayerInteractorStyle::OnLeftButtonUp()
{
    // // 释放物理约束 (如果处于拖拽状态)
    // if (is_object_picked_ && playback_magager_) {
    //     playback_magager_->cleanupPhysicsPicking();
    //     is_object_picked_ = false; // 重置状态
    // }

    // 调用父类方法处理 VTK 默认的左键抬起事件
    vtkInteractorStyleTrackballCamera::OnLeftButtonUp();
}

// void VtkPlayerInteractorStyle::OnKeyPress()
// {

// }

void VtkPlayerInteractorStyle::updateCameraRotation()
{
    return;

    setVtkCameraRotation(this->yaw_, this->pitch_);
    this->Interactor->GetRenderWindow()->Render();
}

void VtkPlayerInteractorStyle::doGodModeMovement(float dt)
{
    return;

    vtkCamera* cam = this->CurrentRenderer->GetActiveCamera();
    if (!cam) return;

    double motionVector[3] = {0.0, 0.0, 0.0};
    double normal[3], up[3], forward[3], right[3];

    // 1. 获取 View Up Vector (上向量)
    cam->GetViewUp(up);

    // 2. 获取 View Plane Normal (视平面法线，即指向“后”方的向量)
    cam->GetViewPlaneNormal(normal);

    // 3. 计算 Forward Vector (前向量，即 normal 的反方向)
    forward[0] = -normal[0];
    forward[1] = -normal[1];
    forward[2] = -normal[2];

    vtkMath::Cross(up, normal, right);

    float linearSpeed = movement_speed_ * dt;

    // W/Up (向前)
    if (External::isKeyDown(Qt::Key_W) || External::isKeyDown(Qt::Key_Up)) {
        motionVector[0] += forward[0]; motionVector[1] += forward[1]; motionVector[2] += forward[2];
    }
    // S/Down (向后)
    if (External::isKeyDown(Qt::Key_S) || External::isKeyDown(Qt::Key_Down)) {
        motionVector[0] -= forward[0]; motionVector[1] -= forward[1]; motionVector[2] -= forward[2];
    }
    // A/Left (向左)
    if (External::isKeyDown(Qt::Key_A) || External::isKeyDown(Qt::Key_Left)) {
        motionVector[0] -= right[0]; motionVector[1] -= right[1]; motionVector[2] -= right[2];
    }
    // D/Right (向右)
    if (External::isKeyDown(Qt::Key_D) || External::isKeyDown(Qt::Key_Right)) {
        motionVector[0] += right[0]; motionVector[1] += right[1]; motionVector[2] += right[2];
    }

    // 归一化并移动
    double norm = std::sqrt(motionVector[0]*motionVector[0] + motionVector[1]*motionVector[1] + motionVector[2]*motionVector[2]);
    if (norm > 0.0) {
        double currentPos[3];
        cam->GetPosition(currentPos);

        currentPos[0] += motionVector[0] / norm * linearSpeed;
        currentPos[1] += motionVector[1] / norm * linearSpeed;
        currentPos[2] += motionVector[2] / norm * linearSpeed;

        cam->SetPosition(currentPos);

        // VTK 相机移动时，需要更新 FocalPoint 以保持看向的方向不变
        double focalPoint[3];
        cam->GetFocalPoint(focalPoint);

        focalPoint[0] += motionVector[0] / norm * linearSpeed;
        focalPoint[1] += motionVector[1] / norm * linearSpeed;
        focalPoint[2] += motionVector[2] / norm * linearSpeed;

        cam->SetFocalPoint(focalPoint);
    }

    // 强制 VTK 渲染更新
    this->Interactor->GetRenderWindow()->Render();
}

void VtkPlayerInteractorStyle::doObjectPicking(int x, int y)
{
    return;

    if (picker_->Pick(x, y, 0, this->CurrentRenderer)) {
        vtkActor* pickedActor = picker_->GetActor();
        double hitPos[3];
        picker_->GetPickPosition(hitPos);

        if (pickedActor /* && pickedActor->IsPhysicsBody() */) {
            // 启动物理拖拽约束 (替换 scene->getPhysicsEnvironment()->createPickingConstraint)
            External::createPickingConstraint(pickedActor, hitPos);
        }
    }
}

void VtkPlayerInteractorStyle::updateMovement(float dt)
{
    return;

    if (this->CurrentRenderer == nullptr) return;

    // 替换 PlayerMouseController::update(dt) 中的 movement 逻辑
    if (current_mode_ == SceneMode::EditMode || !playback_magager_->isScenePlaying()) {
        doGodModeMovement(dt);
    } else {
        // PlayMode: 驱动物理角色控制器 (替换 viewer/physics 模式)
        // playbackManager->SetPhysicsMovementDirection(...);
    }
}

void VtkPlayerInteractorStyle::getYawPitchFromVtkCamera(double &yaw, double &pitch)
{
    // 这是一个复杂的转换，因为 VTK 使用的是方位角(Azimuth)/仰角(Elevation)
    // 而不是欧拉角 Yaw/Pitch。
    // 在此处，我们假设通过之前的 SetVtkCameraRotation 逻辑，我们只需要维护 yaw 和 pitch 状态
    // 如果需要从 VTK 相机精确反推，则需要复杂的向量几何。

    // 为了简化，我们只在 OnRightButtonDown 时读取一次当前的 Rotation，
    // 并在 OnMouseMove 中通过累加 dx/dy 维持。

    // 如果需要更健壮的实现，可以参考 vtkCamera::Elevation/Azimuth 的内部实现。
}

void VtkPlayerInteractorStyle::setVtkCameraRotation(double yaw, double pitch)
{
    return;

    vtkCamera* cam = this->CurrentRenderer->GetActiveCamera();
    if (!cam) return;

    // 简化实现：通过旋转操作来实现 (VTK 的 Azimuth/Elevation 是基于 FocalPoint 的操作)

    // 1. 设置一个新的 ViewUp (通常是 Y 轴)
    cam->SetViewUp(0, 1, 0);

    // 2. 重置相机，然后执行旋转
    cam->SetFocalPoint(0, 0, 0);
    cam->SetPosition(0, 0, 1);

    // 设置 Azimuth (Yaw)
    cam->Azimuth(yaw);
    // 设置 Elevation (Pitch)
    cam->Elevation(pitch);

    // 确保相机位置回到原点 (如果是在 GodMode)
    // ... 这一步需要根据实际的相机/FocalPoint 策略来处理

    // 这里是为了演示 Yaw/Pitch 的概念，实际生产代码中，应使用 VTK 的 Dolly/Zoom/Track
    // 来完成相机的定位和方向，或手动计算 ViewPlaneNormal 和 ViewUp。
}
