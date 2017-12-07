#ifndef ASSETVIEW_H
#define ASSETVIEW_H

#include <QWidget>

#include "irisgl/src/core/irisutils.h"

class QSplitter;
class QListWidget;

class QLineEdit;
class QComboBox;
class QTreeWidgetItem;
class QFocusEvent;
#include <QPushButton>
#include <QJsonObject>
#include <QJsonArray>
#include <QApplication>
#include <QResizeEvent>
#include <QGridLayout>
#include <QScrollArea>
#include <qdebug.h>
#include <QLabel>
#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <QOpenGLFunctions>
#include <QOpenGLFunctions_3_2_Core>
#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector3D>
#include <QButtonGroup>

#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/graphics/rendertarget.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisglfwd.h"

class AssetViewer : public QOpenGLWidget, protected QOpenGLFunctions_3_2_Core
{
	QElapsedTimer _elapsedTimer;
	QOpenGLFunctions_3_2_Core *gl;
	QOpenGLShaderProgram *program;
public:
	iris::MeshPtr mesh;
	iris::RenderTargetPtr previewRT;
	iris::Texture2DPtr screenshotTex;
	bool render_;

	AssetViewer::AssetViewer(QWidget *parent) : QOpenGLWidget(parent) {
		QSurfaceFormat format = QSurfaceFormat::defaultFormat();
		format.setVersion(3, 2);
		format.setSamples(4);
		format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
		format.setRenderableType(QSurfaceFormat::OpenGL);
		format.setProfile(QSurfaceFormat::CoreProfile);
		format.setSwapInterval(0);
		setFormat(format);

		QTimer *timer = new QTimer(this);
		connect(timer, SIGNAL(timeout()), this, SLOT(update()));
		timer->start(1000.f / 60.f);

		render_ = false;

		connect(this, SIGNAL(frameSwapped()), this, SLOT(update()));
	}

	QImage takeScreenshot(int width, int height)
	{
		makeCurrent();
		//previewRT->resize(width, height, true);
		auto img = previewRT->toImage();
		doneCurrent();
		return img;
	}

	void initializeGL() {
		initializeOpenGLFunctions();
		gl = QOpenGLContext::currentContext()->versionFunctions<QOpenGLFunctions_3_2_Core>();

		previewRT = iris::RenderTarget::create(640, 480);
		screenshotTex = iris::Texture2D::create(640, 480);
		previewRT->addTexture(screenshotTex);

		mesh = iris::Mesh::loadMesh(IrisUtils::getAbsoluteAssetPath("app/content/primitives/torus.obj"));

		static const char *vertexShaderSource =
			"#version 330\n"
			"uniform mat4 u_model;\n"
			"uniform mat4 u_view;\n"
			"uniform mat4 u_proj;\n"
			"// fuck it we'll do it live\n"
			"layout (location = 0) in vec3 a_pos;\n"
			"layout (location = 6) in vec3 a_normal;\n"
			"out vec3 onormal;\n"
			"out vec3 FragPos;\n"
			"void main() {\n"
			"	onormal = mat3(transpose(inverse(u_model))) * a_normal;\n"
			"	FragPos = vec3(u_model * vec4(a_pos, 1.0));\n"
			"	gl_Position = u_proj * u_view * u_model * vec4(a_pos, 1);\n"
			"}";

		static const char *fragmentShaderSource =
			"#version 330\n"
			"in vec3 onormal;\n"
			"in vec3 FragPos;\n"
			"out vec4 FragColor;\n"
			"void main() {\n"
			"	vec3 N = normalize(onormal);\n"
			"	vec3 lightDir = normalize(vec3(1, 1, 2) - FragPos);\n"
			"	float diff = max(dot(N, lightDir), 0.0);\n"
			"   vec3 diffuse = diff * vec3(0.78);\n"
			"	vec3 result = ((vec3(0.78) * 0.2) + diffuse) * vec3(0.89, 0.95, 0.88);\n"
			"	FragColor = vec4(result, 1.0);\n"
			"}";

		program = new QOpenGLShaderProgram;
		program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
		program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);

		program->link();
		program->bind();

		gl->glEnable(GL_DEPTH_TEST);
	}

	void update() {
		QOpenGLWidget::update();
	}

	void renderObject() {
		render_ = true;
	}

	void loadModel(QString str) {
		renderObject();
		makeCurrent();
		mesh = iris::Mesh::loadMesh(str);
		doneCurrent();
	}

	void paintGL() {
		if (render_) {
			glViewport(0, 0, this->width() * devicePixelRatio(), this->height() * devicePixelRatio());
			glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			float deltaTime = _elapsedTimer.nsecsElapsed() / (1000.f * 1000.f * 1000.f);
			_elapsedTimer.restart();

			program->bind();

			QMatrix4x4 model, view, projection;
			model.setToIdentity();
			view.setToIdentity();
			view.lookAt(QVector3D(0, 0, 5),
				QVector3D(0, 0, 0),
				QVector3D(0, 1, 0));
			projection.setToIdentity();
			projection.perspective(45.f, (GLfloat)this->width() / this->height(), .1f, 64.f);

			program->setUniformValue("u_model", model);
			program->setUniformValue("u_view", view);
			program->setUniformValue("u_proj", projection);

			mesh->draw(gl, program);

			gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

			//previewRT->resize(previewRT->getWidth(), previewRT->getHeight(), true);
			previewRT->bind();
			glViewport(0, 0, previewRT->getWidth(), previewRT->getHeight());
			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			program->bind();
			view.setToIdentity();
			view.lookAt(QVector3D(0, 0, 4),
				QVector3D(0, 0, 0),
				QVector3D(0, 1, 0));
			projection.setToIdentity();
			projection.perspective(45.f, (GLfloat)previewRT->getWidth() / previewRT->getHeight(), .1f, 64.f);
			program->setUniformValue("u_model", model);
			program->setUniformValue("u_view", view);
			program->setUniformValue("u_proj", projection);
			mesh->draw(gl, program);
			previewRT->unbind();
		}
		else {
			glClearColor(0.18, 0.18, 0.18, 1);
		}
	}

	void resizeGL(int w, int h) {
		glViewport(0, 0, w, h);
	}
};

class Widget : public QWidget
{
	Q_OBJECT

public:
	QString url;
	QPixmap pixmap;
	QLabel *gridImageLabel;
	QLabel *textLabel;
	bool selected;
	QJsonObject metadata;

	// local
	Widget(QJsonObject details, QImage image, QWidget *parent = Q_NULLPTR) : QWidget(parent) {
		this->metadata = details;
		url = details["icon_url"].toString();
		selected = false;
		auto layout = new QGridLayout;
		layout->setMargin(0);
		layout->setSpacing(0);
		pixmap = QPixmap::fromImage(image);
		gridImageLabel = new QLabel;
		gridImageLabel->setPixmap(pixmap.scaledToHeight(164, Qt::SmoothTransformation));
		gridImageLabel->setAlignment(Qt::AlignCenter);

		layout->addWidget(gridImageLabel, 0, 0);
		textLabel = new QLabel(details["name"].toString());

		textLabel->setWordWrap(true);
		textLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
		layout->addWidget(textLabel, 1, 0);

		setMinimumWidth(180);
		setMaximumWidth(180);
		setMinimumHeight(196);
		setMaximumHeight(196);

		textLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 12px; background: #1e1e1e;"
			"border-left: 3px solid rgba(0, 0, 0, 3%); border-bottom: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
		gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 3%); border-top: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");

		setStyleSheet("background: #272727");
		setLayout(layout);
		setCursor(Qt::PointingHandCursor);

		connect(this, SIGNAL(hovered()), SLOT(dimHighlight()));
		connect(this, SIGNAL(left()), SLOT(noHighlight()));
		connect(this, SIGNAL(singleClicked(Widget*)), SLOT(selectedHighlight(Widget*)));
	}

	// online
	Widget(QJsonObject details, QWidget *parent = Q_NULLPTR) : QWidget(parent) {
		this->metadata = details;
		url = details["icon_url"].toString();
		selected = false;
		auto layout = new QGridLayout;
		layout->setMargin(0);
		layout->setSpacing(0);
		pixmap = QPixmap(IrisUtils::getAbsoluteAssetPath("app/modelpresets/cube.png"));
		gridImageLabel = new QLabel;
		gridImageLabel->setPixmap(pixmap.scaledToHeight(164, Qt::SmoothTransformation));
		gridImageLabel->setAlignment(Qt::AlignCenter);

		layout->addWidget(gridImageLabel, 0, 0);
		textLabel = new QLabel(details["name"].toString());

		textLabel->setWordWrap(true);
		textLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
		layout->addWidget(textLabel, 1, 0);

		setMinimumWidth(180);
		setMaximumWidth(180);
		setMinimumHeight(196);
		setMaximumHeight(196);
		
		textLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 12px; background: #1e1e1e;"
			"border-left: 3px solid rgba(0, 0, 0, 3%); border-bottom: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
		gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 3%); border-top: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");

		setStyleSheet("background: #272727");
		setLayout(layout);
		setCursor(Qt::PointingHandCursor);

		connect(this, SIGNAL(hovered()), SLOT(dimHighlight()));
		connect(this, SIGNAL(left()), SLOT(noHighlight()));
		connect(this, SIGNAL(singleClicked(Widget*)), SLOT(selectedHighlight(Widget*)));
	}

	void setTile(QPixmap pix) {
		pixmap = pix;
		gridImageLabel->setPixmap(pixmap.scaledToHeight(164, Qt::SmoothTransformation));
		gridImageLabel->setAlignment(Qt::AlignCenter);
	}

	void enterEvent(QEvent *event) {
		QWidget::enterEvent(event);
		emit hovered();
	}

	void leaveEvent(QEvent *event) {
		QWidget::leaveEvent(event);
		emit left();
	}

	void mousePressEvent(QMouseEvent *event) {
		if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
			emit singleClicked(this);
		}
	}

public slots:
	void dimHighlight() {
		textLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 12px; background: #1e1e1e;"
			"border-left: 3px solid rgba(0, 0, 0, 10%); border-bottom: 3px solid rgba(0, 0, 0, 10%); border-right: 3px solid rgba(0, 0, 0, 10%)");
		gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 10%); border-top: 3px solid rgba(0, 0, 0, 10%); border-right: 3px solid rgba(0, 0, 0, 10%)");
	}

	void noHighlight() {
		textLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 12px; background: #1e1e1e;"
			"border-left: 3px solid rgba(0, 0, 0, 3%); border-bottom: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
		gridImageLabel->setStyleSheet("border-left: 3px solid rgba(0, 0, 0, 3%); border-top: 3px solid rgba(0, 0, 0, 3%); border-right: 3px solid rgba(0, 0, 0, 3%)");
	}

	void selectedHighlight(Widget*) {
		selected = true;
		textLabel->setStyleSheet("font-weight: bold; color: #ddd; font-size: 12px; background: #1e1e1e;"
			"border-left: 3px solid #3498db; border-bottom: 3px solid #3498db; border-right: 3px solid #3498db");
		gridImageLabel->setStyleSheet("border-left: 3px solid #3498db; border-top: 3px solid #3498db; border-right: 3px solid #3498db");
	}

signals:
	void hovered();
	void left();
	void singleClicked(Widget*);
};

enum AssetSource
{
	LOCAL,
	ONLINE
};

class FastGrid;

class AssetView : public QWidget
{
	Q_OBJECT

public slots:
	void fetchMetadata(Widget*);

public:
	int gridCount;
	QButtonGroup *sourceGroup;
	QAbstractButton *localAssetsButton;
	QAbstractButton *onlineAssetsButton;
	AssetSource assetSource;
	explicit AssetView(QWidget *parent = Q_NULLPTR);
	~AssetView();
	void focusInEvent(QFocusEvent *event);
	bool eventFilter(QObject *watched, QEvent *event);

private:
	QSplitter *_splitter;
	QWidget *_filterBar;
	QWidget *_navPane;
	QOpenGLWidget *_assetPreview;
	QWidget *_previewPane;
	QWidget *_viewPane;
	QWidget *_metadataPane;

	QListWidget *_assetView;

	QByteArray downloadedImage;
	QVector<QByteArray> iconList;
	QString filename;

	QLineEdit *nameField;
	QComboBox *typeField;
	QPushButton *addToLibrary;
	QPushButton *uploadBtn;
	QLabel *renameModel;
	QLineEdit *renameModelField;
	QWidget *renameWidget;

	QTreeWidgetItem *rootItem;
	QVector<int> collections;

	FastGrid *fastGrid;
	QWidget *emptyGrid;
	QWidget *filterPane;

	QLabel *metadataName;
	QLabel *metadataType;
	QLabel *metadataVisibility;
	QLabel *metadataCollection;

	QJsonArray fetchedOnlineAssets;

	AssetViewer *viewer;
};

class FastGrid : public QScrollArea
{
	Q_OBJECT
	QWidget *gridWidget;

public:
	QGridLayout *fastGrid;
	int lastWidth;
	QWidget *parent;

	FastGrid(QWidget *parent) : QScrollArea(parent) {
		this->parent = parent;
		gridWidget = new QWidget(this);
		fastGrid = new QGridLayout();
		fastGrid->setMargin(0);
		fastGrid->setSpacing(12);
		gridWidget->setLayout(fastGrid);
		setAlignment(Qt::AlignHCenter);
		//setWidgetResizable(true);
		setWidget(gridWidget);
		setStyleSheet("background: transparent");
	}

	void updateImage() {

	}

	// local
	void addTo(QJsonObject details, QImage image, int count) {
		auto sampleWidget = new Widget(details, image);

		int columnCount = viewport()->width() / (180 + 10);
		if (columnCount == 0) columnCount = 1;

		connect(sampleWidget, &Widget::singleClicked, parent, [this](Widget *item) {
			qobject_cast<AssetView*>(parent)->fetchMetadata(item);
		});

		fastGrid->addWidget(sampleWidget, count / columnCount + 1, count % columnCount + 1);
		gridWidget->adjustSize();

		emit gridCount(fastGrid->count());
	}

	// online
	void addTo(QJsonObject details, int count) {
		auto sampleWidget = new Widget(details);

		int columnCount = viewport()->width() / (180 + 10);
		if (columnCount == 0) columnCount = 1;

		connect(sampleWidget, &Widget::singleClicked, parent, [this](Widget *item) {
			qobject_cast<AssetView*>(parent)->fetchMetadata(item);
		});

		fastGrid->addWidget(sampleWidget, count / columnCount + 1, count % columnCount + 1);
		gridWidget->adjustSize();

		emit gridCount(fastGrid->count());
	}

	void resizeEvent(QResizeEvent *event)
	{
		lastWidth = event->size().width();
		int check = event->size().width() / (180 + 10);
		//gridWidget->setMinimumWidth(viewport()->width());

		if (check != 0) {
			updateGridColumns(event->size().width());
		}
		else
			QScrollArea::resizeEvent(event);
	}

	void updateGridColumns(int width)
	{
		int columnCount = width / (180 + 10);
		if (columnCount == 0) columnCount = 1;

		int gridCount = fastGrid->count();
		QList<Widget*> gridItems;
		for (int count = 0; count < gridCount; count++)
			gridItems << static_cast<Widget*>(fastGrid->takeAt(0)->widget());

		int count = 0;
		foreach(Widget *gridItem, gridItems) {
			fastGrid->addWidget(gridItem, count / columnCount + 1, count % columnCount + 1);
			count++;
		}

		// gridWidget->setMinimumWidth(gridCount * (180 + 10));
		gridWidget->adjustSize();
	}

signals:
	void gridCount(int);
};

#endif // ASSETVIEW_H