#include "assetviewer.h"

#include "../constants.h"
#include "../core/keyboardstate.h"

AssetViewer::AssetViewer(QWidget *parent) : QOpenGLWidget(parent)
{
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setVersion(3, 2);
    format.setSamples(4);
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapInterval(0);
    setFormat(format);
    
    //QTimer *timer = new QTimer(this);
    //connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    //timer->start(1000.f / 60.f);
    //
    //connect(this, SIGNAL(frameSwapped()), this, SLOT(update()));

    viewport = new iris::Viewport();
    render = false;

    // needed in order to get mouse events
    setMouseTracking(true);
    // needed in order to get key events http://stackoverflow.com/a/7879484/991834
    setFocusPolicy(Qt::ClickFocus);
}

void AssetViewer::paintGL()
{
    makeCurrent();

    if (render) {
        // glViewport(0, 0, this->width() * devicePixelRatio(), this->height() * devicePixelRatio());
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        float dt = elapsedTimer.nsecsElapsed() / (1000.0f * 1000.0f * 1000.0f);
        elapsedTimer.restart();

        if (!!renderer && !!scene) {
            camController->update(dt);
            scene->update(dt);
            renderer->renderScene(dt, viewport);
        }
    }

    doneCurrent();
}

void AssetViewer::updateScene()
{

}

void AssetViewer::initializeGL()
{
    QOpenGLWidget::initializeGL();

    initializeOpenGLFunctions();
    gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();

    gl->glEnable(GL_DEPTH_TEST);
    gl->glEnable(GL_CULL_FACE);

    renderer = iris::ForwardRenderer::create(false);
    scene = iris::Scene::create();
    renderer->setScene(scene);

    auto node = iris::MeshNode::create();
    node->setMesh(":/models/ground.obj");
    node->setLocalPos(QVector3D(0, 0, 0));
    node->setName("Ground");
    node->setPickable(false);
    node->setShadowEnabled(false);

    auto m = iris::CustomMaterial::create();
    m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
    m->setValue("diffuseTexture", IrisUtils::getAbsoluteAssetPath("app/content/textures/tile.png"));
    //m->setValue("diffuseTexture", ":/content/skies/alternative/cove/back.jpg");
    m->setValue("textureScale", 4.f);
    //m->setValue("emission",QColor(255, 255, 255));
    m->setValue("diffuseColor", QColor(255, 255, 255, 255));
    m->setValue("useAlpha", false);
    node->setMaterial(m);

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    scene->rootNode->addChild(dlight);
    dlight->setName("Key Light");
    dlight->setLocalRot(QQuaternion::fromEulerAngles(45, -45, 0));
    dlight->intensity = 1;

    scene->rootNode->addChild(node);

    defaultCam = new EditorCameraController();
    orbitalCam = new OrbitalCameraController();

    camera = iris::CameraNode::create();
    camera->setLocalPos(QVector3D(1, 1, 5));
    camera->lookAt(QVector3D(0, 0.5f, 0));

    scene->setCamera(camera);

    scene->setSkyColor(QColor(100, 100, 100));
    scene->setAmbientColor(QColor(255, 255, 255));

    scene->fogColor = QColor(72, 72, 72);
    scene->fogEnabled = false;
    scene->shadowEnabled = false;

    camera->update(0);
    scene->update(0);

    //camController->end();
    camController = orbitalCam;
    camController->setCamera(camera);
    camController->resetMouseStates();
    camController->start();

    auto timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(update()));
    timer->start(Constants::FPS_60);
}

void AssetViewer::wheelEvent(QWheelEvent *event)
{
    if (camController != nullptr) {
        camController->onMouseWheel(event->delta());
    }
}

void AssetViewer::renderObject() {
	render = true;
}

void AssetViewer::mouseMoveEvent(QMouseEvent *e)
{
    // @ISSUE - only fired when mouse is dragged
    QPointF localPos = e->localPos();
    QPointF dir = localPos - prevMousePos;

    if (camController != nullptr) {
        camController->onMouseMove(-dir.x(), -dir.y());
    }

    prevMousePos = localPos;
}

void AssetViewer::mousePressEvent(QMouseEvent *e)
{
    prevMousePos = e->localPos();

    if (camController != nullptr) {
        camController->onMouseDown(e->button());
    }
}

void AssetViewer::mouseReleaseEvent(QMouseEvent *e)
{
    if (camController != nullptr) {
        camController->onMouseUp(e->button());
    }
}

void AssetViewer::loadModel(QString str) {
	renderObject();
	makeCurrent();
	//mesh = iris::Mesh::loadMesh(str);
	doneCurrent();
}

void AssetViewer::update() {
	QOpenGLWidget::update();
}

void AssetViewer::resizeGL(int width, int height)
{
    // we do an explicit call to glViewport(...) in forwardrenderer
    // with the "good DPI" values so it is not needed here initially (iKlsR)
    viewport->pixelRatioScale = devicePixelRatio();
    viewport->width = width;
    viewport->height = height;
}

//AssetViewer::AssetViewer(QWidget *parent) : QOpenGLWidget(parent) {
//	QSurfaceFormat format = QSurfaceFormat::defaultFormat();
//	format.setVersion(3, 2);
//	format.setSamples(4);
//	format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
//	format.setRenderableType(QSurfaceFormat::OpenGL);
//	format.setProfile(QSurfaceFormat::CoreProfile);
//	format.setSwapInterval(0);
//	setFormat(format);
//
//	QTimer *timer = new QTimer(this);
//	connect(timer, SIGNAL(timeout()), this, SLOT(update()));
//	timer->start(1000.f / 60.f);
//
//	render_ = false;
//
//	connect(this, SIGNAL(frameSwapped()), this, SLOT(update()));
//}
//
//AssetViewer::~AssetViewer()
//{
//
//}
//
//QImage AssetViewer::takeScreenshot(int width, int height)
//{
//	makeCurrent();
//	//previewRT->resize(width, height, true);
//	auto img = previewRT->toImage();
//	doneCurrent();
//	return img;
//}
//
//void AssetViewer::initializeGL() {
//	initializeOpenGLFunctions();
//	gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();
//
//	previewRT = iris::RenderTarget::create(640, 480);
//	screenshotTex = iris::Texture2D::create(640, 480);
//	previewRT->addTexture(screenshotTex);
//
//	mesh = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/torus.obj"));
//
//	static const char *vertexShaderSource =
//		"#version 330\n"
//		"uniform mat4 u_model;\n"
//		"uniform mat4 u_view;\n"
//		"uniform mat4 u_proj;\n"
//		"// fuck it we'll do it live\n"
//		"layout (location = 0) in vec3 a_pos;\n"
//		"layout (location = 6) in vec3 a_normal;\n"
//		"out vec3 onormal;\n"
//		"out vec3 FragPos;\n"
//		"void main() {\n"
//		"	onormal = mat3(transpose(inverse(u_model))) * a_normal;\n"
//		"	FragPos = vec3(u_model * vec4(a_pos, 1.0));\n"
//		"	gl_Position = u_proj * u_view * u_model * vec4(a_pos, 1);\n"
//		"}";
//
//	static const char *fragmentShaderSource =
//		"#version 330\n"
//		"in vec3 onormal;\n"
//		"in vec3 FragPos;\n"
//		"out vec4 FragColor;\n"
//		"void main() {\n"
//		"	vec3 N = normalize(onormal);\n"
//		"	vec3 lightDir = normalize(vec3(1, 1, 2) - FragPos);\n"
//		"	float diff = max(dot(N, lightDir), 0.0);\n"
//		"   vec3 diffuse = diff * vec3(0.78);\n"
//		"	vec3 result = ((vec3(0.78) * 0.2) + diffuse) * vec3(0.89, 0.95, 0.88);\n"
//		"	FragColor = vec4(result, 1.0);\n"
//		"}";
//
//	program = new QOpenGLShaderProgram;
//	program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
//	program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
//
//	program->link();
//	program->bind();
//
//	gl->glEnable(GL_DEPTH_TEST);
//}
//
//void AssetViewer::update() {
//	QOpenGLWidget::update();
//}
//
//void AssetViewer::renderObject() {
//	render_ = true;
//}
//
//void AssetViewer::loadModel(QString str) {
//	renderObject();
//	makeCurrent();
//	mesh = iris::Mesh::loadMesh(str);
//	doneCurrent();
//}
//
//void AssetViewer::paintGL() {
//	if (render_) {
//		glViewport(0, 0, this->width() * devicePixelRatio(), this->height() * devicePixelRatio());
//		glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
//		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//
//		float deltaTime = _elapsedTimer.nsecsElapsed() / (1000.f * 1000.f * 1000.f);
//		_elapsedTimer.restart();
//
//		program->bind();
//
//		QMatrix4x4 model, view, projection;
//		model.setToIdentity();
//		view.setToIdentity();
//		view.lookAt(QVector3D(0, 0, 5),
//			QVector3D(0, 0, 0),
//			QVector3D(0, 1, 0));
//		projection.setToIdentity();
//		projection.perspective(45.f, (GLfloat)this->width() / this->height(), .1f, 64.f);
//
//		program->setUniformValue("u_model", model);
//		program->setUniformValue("u_view", view);
//		program->setUniformValue("u_proj", projection);
//
//		mesh->draw(gl, program);
//
//		gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
//
//		//previewRT->resize(previewRT->getWidth(), previewRT->getHeight(), true);
//		previewRT->bind();
//		glViewport(0, 0, previewRT->getWidth(), previewRT->getHeight());
//		glClearColor(0, 0, 0, 0);
//		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
//		program->bind();
//		view.setToIdentity();
//		view.lookAt(QVector3D(0, 0, 4),
//			QVector3D(0, 0, 0),
//			QVector3D(0, 1, 0));
//		projection.setToIdentity();
//		projection.perspective(45.f, (GLfloat)previewRT->getWidth() / previewRT->getHeight(), .1f, 64.f);
//		program->setUniformValue("u_model", model);
//		program->setUniformValue("u_view", view);
//		program->setUniformValue("u_proj", projection);
//		mesh->draw(gl, program);
//		previewRT->unbind();
//	}
//	else {
//		glClearColor(0.18, 0.18, 0.18, 1);
//	}
//}
//
//void AssetViewer::resizeGL(int w, int h) {
//	glViewport(0, 0, w, h);
//}