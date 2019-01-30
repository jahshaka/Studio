#pragma once

#include <QOpenGLBuffer>
#include <QHash>
#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLWidget>
#include <QSharedPointer>

#include "irisgl/src/bullet3/src/btBulletDynamicsCommon.h" 

#include "irisgl/src/irisglfwd.h"
#include "irisgl/src/math/intersectionhelper.h"
#include "irisgl/src/physics/physicsproperties.h"

#include "mainwindow.h"
#include "uimanager.h"

#include "core/project.h"

namespace iris
{
	class CameraNode;
	class DefaultMaterial;
	class ForwardRenderer;
	class FullScreenQuad;
	class Mesh;
	class MeshNode;
	class Scene;
	class SceneNode;
	class Viewport;
	class ContentManager;
}

class CameraControllerBase;
class EditorCameraController;
class EditorData;
class EditorVrController;
class Gizmo;
class OrbitalCameraController;
class OutlinerRenderer;
class QElapsedTimer;
class QOpenGLDebugLogger;
class QOpenGLShaderProgram;
class QTimer;
class RotationGizmo;
class ScaleGizmo;
class ThumbnailGenerator;
class OutlinerRenderer;
class AnimationPath;
class TranslationGizmo;
class ViewerCameraController;
class ViewportGizmo;
class Globals;
class btRigidBody;

class PlayerWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core
{
	Q_OBJECT

	iris::CameraNodePtr editorCam;
public:
	PlayerWidget(QWidget* parent = nullptr);
	~PlayerWidget();
};