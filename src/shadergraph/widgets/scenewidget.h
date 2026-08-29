#pragma once

#include <QQuaternion>
#include <QtGlobal>
#include <irisgl/IrisGL.h>
#include "materialsettingswidget.h"

enum class PreviewModel
{
	Sphere,
	Cube,
	Plane,
	Cylinder,
	Capsule,
	Torus
};

class QMouseEvent;
class QWheelEvent;

class NodeGraph;
class GraphNodeScene;
class MaterialSettings;
class SceneWidget : public iris::RenderWidget
{
	iris::MeshPtr sphereMesh;
	iris::MeshPtr cubeMesh;
	iris::MeshPtr planeMesh;
	iris::MeshPtr cylinderMesh;
	iris::MeshPtr capsuleMesh;
	iris::MeshPtr torusMesh;

	iris::MeshPtr mesh;
    iris::ShaderPtr shader;
    iris::MaterialPtr mat;

    iris::FontPtr font;
    float fps;
	bool initialized = false;

    iris::VertexBufferPtr vertexBuffer;
    iris::Texture2DPtr tex;

    iris::CameraNodePtr cam;
	int viewportWidth, viewportHeight;
	QColor clearColor;

	// template files
    QString vertString;
    QString fragString;

	// generated source
	QString generatedVertString;
	QString generatedFragString;

    static float renderTime;
    QList<iris::LightNodePtr> lights;

    NodeGraph* graph;
	

	bool dragging;
	QPoint lastMousePos;
	QQuaternion rot;
	float scale;

	MaterialSettings materialSettings;
	iris::BlendState blendState;
	iris::DepthState depthState;
	iris::RasterizerState rasterState;

	PreviewModel previewModel;
public:
	GraphNodeScene * graphScene;
    SceneWidget();

    void start();

    void update(float dt);

    void render();

	void setClearColor(QColor color) { this->clearColor = color; }

	static float getRenderTime() { return renderTime; }
	void setVertexShader(QString vertShader);
	void setFragmentShader(QString fragShader);
	//void updateShader(QString shaderCode);
	void updateShader();

    void resetRenderTime();

	QString getTextureUniformName(QString texGuid);
    void passNodeGraphUniforms();
    void setNodeGraph(NodeGraph* graph);


	// mouse events
	void mousePressEvent(QMouseEvent* evt);
	void mouseMoveEvent(QMouseEvent* evt);	
	void mouseReleaseEvent(QMouseEvent* evt);
	void wheelEvent(QWheelEvent* evt);

	iris::Texture2DPtr screenshotTex;
	iris::RenderTargetPtr screenshotRT;
	QImage takeScreenshot(int width, int height);

	void resizeEvent(QResizeEvent* evt);

	void setPreviewModel(PreviewModel model);
public slots:
	void setMaterialSettings(MaterialSettings settings);
};
