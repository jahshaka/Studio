#include "shadergraphmainwindow.h"
#include <QApplication>


// https://adared.ch/qnodeseditor-qt-nodesports-based-data-processing-flow-editor/
int main(int argc, char *argv[])
{
  
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
	QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
	QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
	QApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
	QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
	QApplication a(argc, argv);


	shadergraph::MainWindow w;
	w.show();

    return a.exec();
}
