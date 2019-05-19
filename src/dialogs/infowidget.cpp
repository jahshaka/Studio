#include "infowidget.h"
#include <QKeyEvent>
#include "src/widgets/sceneviewwidget.h"
#include "src/uimanager.h"
InfoWidget::InfoWidget(MainWindowMenus menu) : CustomDialog()
{
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_TranslucentBackground, true);

	menus = menu;
	if (menus == MainWindowMenus::Editormenu) configureEditorMenu();
	if (menus == MainWindowMenus::Datamenu) configureDataMenu();
	if (menus == MainWindowMenus::WorkspaceMenu) configureWorkspaceMenu();
}


InfoWidget::~InfoWidget()
{
}

void InfoWidget::editorMenu(QKeyEvent *event)
{
	if (event->key() == Qt::Key::Key_T) emit translateTool();
	if (event->key() == Qt::Key::Key_R) emit rotateTool();
	if (event->key() == Qt::Key::Key_S) emit scaleTool();
	if (event->key() == Qt::Key::Key_G) emit useGlobalSpace();
	if (event->key() == Qt::Key::Key_L) emit useLocalSpace();
	if (event->key() == Qt::Key::Key_F) emit useFreeCamera();
	if (event->key() == Qt::Key::Key_A) emit useArcballCamera();
	if (event->key() == Qt::Key::Key_O) emit orthagonal();
	if (event->key() == Qt::Key::Key_P) emit perspective();
}

void InfoWidget::configureEditorMenu()
{
	auto wid = new QWidget;
	auto lay = new QVBoxLayout;
	wid->setLayout(lay);
	lay->addWidget(new QLabel("Access Key.....q"));
	lay->addWidget(new QLabel("T ..... Translate"));
	lay->addWidget(new QLabel("R ..... Rotate"));
	lay->addWidget(new QLabel("S ..... Scale"));
	lay->addWidget(new QLabel("G ..... Global Space"));
	lay->addWidget(new QLabel("L ..... Local Space"));
	lay->addWidget(new QLabel("F ..... Freeball Camera"));
	lay->addWidget(new QLabel("A ..... Arcball camera"));
	lay->addWidget(new QLabel("O ..... Othragonal View"));
	lay->addWidget(new QLabel("P ..... Perspective View"));

	insertWidget(wid);
	setStyleSheet(StyleSheet::QLabelWhite());
	show();
}

void InfoWidget::dataMenu(QKeyEvent*event)
{
	if (event->key() == Qt::Key::Key_F) emit toggleFps();
	if (event->key() == Qt::Key::Key_V) emit togglePerspective();
}

void InfoWidget::configureDataMenu()
{
	auto wid = new QWidget;
	auto lay = new QVBoxLayout;
	wid->setLayout(lay);
	lay->addWidget(new QLabel("Access Key.....I"));
	lay->addWidget(new QLabel("F ..... Toggle FPS Info"));
	lay->addWidget(new QLabel("V ..... Toggle View Info"));	

	insertWidget(wid);
	setStyleSheet(StyleSheet::QLabelWhite());
	show();
}

void InfoWidget::workspaceMenu(QKeyEvent* event)
{
	if (event->key() == Qt::Key::Key_S) UiManager::sceneViewWidget->mainWindow->switchSpace(WindowSpaces::DESKTOP);
	if (event->key() == Qt::Key::Key_P && UiManager::isSceneOpen) UiManager::sceneViewWidget->mainWindow->switchSpace(WindowSpaces::PLAYER);
	if (event->key() == Qt::Key::Key_E && UiManager::isSceneOpen) UiManager::sceneViewWidget->mainWindow->switchSpace(WindowSpaces::EDITOR);
	if (event->key() == Qt::Key::Key_M) UiManager::sceneViewWidget->mainWindow->switchSpace(WindowSpaces::EFFECT);
	if (event->key() == Qt::Key::Key_A) UiManager::sceneViewWidget->mainWindow->switchSpace(WindowSpaces::ASSETS);
}

void InfoWidget::configureWorkspaceMenu()
{
	auto wid = new QWidget;
	auto lay = new QVBoxLayout;
	wid->setLayout(lay);
	lay->addWidget(new QLabel("Access Key.....v"));
	lay->addWidget(new QLabel("S ..... Scene Window"));
	if (UiManager::isSceneOpen) lay->addWidget(new QLabel("P ..... Player Window"));
	if (UiManager::isSceneOpen) lay->addWidget(new QLabel("E ..... Editor Window"));
	lay->addWidget(new QLabel("M ..... Materials Window"));
	lay->addWidget(new QLabel("A ..... Asset Window"));


	insertWidget(wid);
	setStyleSheet(StyleSheet::QLabelWhite());
	show();
}

void InfoWidget::keyPressEvent(QKeyEvent* event)
{
	if (menus == MainWindowMenus::Editormenu) editorMenu(event);
	if (menus == MainWindowMenus::Datamenu) dataMenu(event);
	if (menus == MainWindowMenus::WorkspaceMenu) workspaceMenu(event);

	close();
}
