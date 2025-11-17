#ifndef VTKPLAYERINTERACTORSTYLE_H
#define VTKPLAYERINTERACTORSTYLE_H

#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkPropPicker.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkCamera.h>

class VtkPlaybackManager;

class VtkPlayerInteractorStyle : public vtkInteractorStyleTrackballCamera
{
public:
    static VtkPlayerInteractorStyle* New();
    vtkTypeMacro(VtkPlayerInteractorStyle, vtkInteractorStyleTrackballCamera);

    VtkPlayerInteractorStyle();

    void setPlaybackManager(VtkPlaybackManager* manager);

    enum class SceneMode { EditMode, PlayMode };
    void setSceneMode(SceneMode mode) { current_mode_= mode; }

    void updateMovement(float dt);

protected:
    void OnMouseMove() override;
    void OnLeftButtonDown() override;
    void OnLeftButtonUp() override;
//    void OnMiddleButtonDown() override;
//    void OnMiddleButtonUp() override;
    void OnRightButtonDown() override;
    void OnRightButtonUp() override;
    void OnMouseWheelForward() override;
    void OnMouseWheelBackward() override;
//    void OnKeyPress() override;

private:
    void updateCameraRotation();
    void doGodModeMovement(float dt);
    void doObjectPicking(int x, int y);
    void getYawPitchFromVtkCamera(double &yaw, double &pitch);
    void setVtkCameraRotation(double yaw, double pitch);


    VtkPlaybackManager* playback_magager_ = nullptr;
    SceneMode current_mode_ = SceneMode::EditMode;

    double yaw_ = 0.0f;
    double pitch_ = 0.0f;
    double movement_speed_ = 25.0f;
    bool right_mouse_down_ = false;
    bool is_object_picked_ = false;

    vtkSmartPointer<vtkPropPicker> picker_;

    bool is_playing_ = false;
};

#endif // VTKPLAYERINTERACTORSTYLE_H
