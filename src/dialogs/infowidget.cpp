#include "infowidget.h"
#include <QKeyEvent>


InfoWidget::InfoWidget(MainWindowMenus menu) : CustomDialog()
{
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_TranslucentBackground, true);

	menus = menu;
	if (menus == MainWindowMenus::Editormenu) configureEditorMenu();
	if (menus == MainWindowMenus::Datamenu) configureDataMenu();
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

void InfoWidget::keyPressEvent(QKeyEvent* event)
{
	if (menus == MainWindowMenus::Editormenu) editorMenu(event);
	if (menus == MainWindowMenus::Datamenu) dataMenu(event);

	close();
}
