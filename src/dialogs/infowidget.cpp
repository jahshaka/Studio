#include "infowidget.h"
#include <QKeyEvent>
#include "src/widgets/sceneviewwidget.h"
#include "src/uimanager.h"
InfoWidget::InfoWidget(MainWindowMenus menu) : CustomDialog()
{
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setWindowFlag(Qt::SubWindow);

	menus = menu;
	if (menus == MainWindowMenus::Editormenu) configureEditorMenu();
	if (menus == MainWindowMenus::Datamenu) configureDataMenu();
	if (menus == MainWindowMenus::WorkspaceMenu) configureWorkspaceMenu();
}


InfoWidget::~InfoWidget()
{
}

void InfoWidget::presetWidget()
{
	show(); // shows the widget to set the geometry
	auto offset = 30;
	setGeometry(QCursor::pos().x()-offset, QCursor::pos().y()-offset, width(), height());
	hide();
	exec();
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
	auto lay = new QGridLayout;
	wid->setLayout(lay);
	lay->addWidget(new QLabel("Access Key.....q"),0,1,1,2);
	lay->addWidget(new QLabel("Any other key to dismiss"), 20, 1, 1, 2);

	lay->addWidget(new QLabel("T"), 1, 1, 1, 1);
	lay->addWidget(new QLabel(".....Translate"), 1, 2, 1, 1);
	lay->addWidget(new QLabel("R"), 2, 1, 1, 1);
	lay->addWidget(new QLabel(".....Rotate"), 2, 2, 1, 1);
	lay->addWidget(new QLabel("S"), 3, 1, 1, 1);
	lay->addWidget(new QLabel(".....Scale"), 3, 2, 1, 1);
	lay->addWidget(new QLabel("G"), 4, 1, 1, 1);
	lay->addWidget(new QLabel(".....Global Space"), 4, 2, 1, 1);
	lay->addWidget(new QLabel("L"), 5, 1, 1, 1);
	lay->addWidget(new QLabel(".....Local Space"), 5, 2, 1, 1);
	lay->addWidget(new QLabel("F"), 6, 1, 1, 1);
	lay->addWidget(new QLabel(".....Freeball Camera"), 6, 2, 1, 1);
	lay->addWidget(new QLabel("A"), 7, 1, 1, 1);
	lay->addWidget(new QLabel(".....Arcball Camera"), 7, 2, 1, 1);
	lay->addWidget(new QLabel("O"), 8, 1, 1, 1);
	lay->addWidget(new QLabel(".....Othragonal View"), 8, 2, 1, 1);
	lay->addWidget(new QLabel("P"), 9, 1, 1, 1);
	lay->addWidget(new QLabel(".....Perspective View"), 9, 2, 1, 1);

	



	/*lay->addWidget(new QLabel("T ..... Translate"));
	lay->addWidget(new QLabel("R ..... Rotate"));
	lay->addWidget(new QLabel("S ..... Scale"));
	lay->addWidget(new QLabel("G ..... Global Space"));
	lay->addWidget(new QLabel("L ..... Local Space"));
	lay->addWidget(new QLabel("F ..... Freeball Camera"));
	lay->addWidget(new QLabel("A ..... Arcball camera"));
	lay->addWidget(new QLabel("O ..... Othragonal View"));
	lay->addWidget(new QLabel("P ..... Perspective View"));*/

	insertWidget(wid);
	setStyleSheet(StyleSheet::QLabelWhite());
}

void InfoWidget::dataMenu(QKeyEvent*event)
{
	if (event->key() == Qt::Key::Key_F) emit toggleFps();
	if (event->key() == Qt::Key::Key_V) emit togglePerspective();
}

void InfoWidget::configureDataMenu()
{
	auto wid = new QWidget;
	auto lay = new QGridLayout;
	wid->setLayout(lay);
	lay->addWidget(new QLabel("Any other key to dismiss"), 20, 0, 1, 2);
	lay->addWidget(new QLabel("Access Key.....I"),0,0,1,2);
	lay->addWidget(new QLabel("F"), 1, 0, 1, 1);
	lay->addWidget(new QLabel(".....Toggle FPS Info"), 1, 1, 1, 1); 
	lay->addWidget(new QLabel("V"), 2, 0, 1, 1);
	lay->addWidget(new QLabel(".....Toggle View Info"), 2, 1, 1, 1);


	/*lay->addWidget(new QLabel("F ..... Toggle FPS Info"));
	lay->addWidget(new QLabel("V ..... Toggle View Info"));	*/

	insertWidget(wid);
	setStyleSheet(StyleSheet::QLabelWhite());
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
	auto lay = new QGridLayout;
	wid->setLayout(lay);
	lay->addWidget(new QLabel("Access Key.....v"), 0, 1,1,2);
	lay->addWidget(new QLabel("Any other key to dismiss"), 20, 1, 1, 2);

	lay->addWidget(new QLabel("S"), 1, 1, 1, 1);
	lay->addWidget(new QLabel(".....Scene Window"), 1, 2, 1, 1);
	if (UiManager::isSceneOpen) {
		lay->addWidget(new QLabel("P"), 2, 1, 1, 1);
		lay->addWidget(new QLabel(".....Player Window"), 2, 2, 1, 1);
		lay->addWidget(new QLabel("E"), 3, 1, 1, 1);
		lay->addWidget(new QLabel(".....Editor Window"), 3, 2, 1, 1);
	}
	lay->addWidget(new QLabel("M"), 4, 1, 1, 1);
	lay->addWidget(new QLabel(".....Materials Window"), 4, 2, 1, 1);
	lay->addWidget(new QLabel("A"), 5, 1, 1, 1);
	lay->addWidget(new QLabel(".....Asset Window"), 5, 2, 1, 1);


	//lay->addWidget(new QLabel("S ..... Scene Window"));
	//if (UiManager::isSceneOpen) lay->addWidget(new QLabel("P ..... Player Window"));
	//if (UiManager::isSceneOpen) lay->addWidget(new QLabel("E ..... Editor Window"));
	//lay->addWidget(new QLabel("M ..... Materials Window"));
	//lay->addWidget(new QLabel("A ..... Asset Window"));


	insertWidget(wid);
	setStyleSheet(StyleSheet::QLabelWhite());
}

void InfoWidget::keyPressEvent(QKeyEvent* event)
{
	if (menus == MainWindowMenus::Editormenu) editorMenu(event);
	if (menus == MainWindowMenus::Datamenu) dataMenu(event);
	if (menus == MainWindowMenus::WorkspaceMenu) workspaceMenu(event);

	close();
}

void InfoWidget::leaveEvent(QEvent* event)
{
	close();
}
