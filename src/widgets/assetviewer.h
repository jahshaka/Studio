#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector3D>

#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/graphics/rendertarget.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisglfwd.h"

class AssetViewer : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core
{
public:
	AssetViewer(QWidget *parent);
	~AssetViewer();

	void initializeGL();
	void update();
	void paintGL();
	void resizeGL(int w, int h);

	void renderObject();
	void loadModel(QString str);
	QImage takeScreenshot(int width, int height);

private:
	QElapsedTimer _elapsedTimer;
	QOpenGLFunctions_3_2_Core *gl;
	QOpenGLShaderProgram *program;
	iris::MeshPtr mesh;
	iris::RenderTargetPtr previewRT;
	iris::Texture2DPtr screenshotTex;
	bool render_;
};