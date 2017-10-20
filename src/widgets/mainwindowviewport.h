#ifndef MAINWINDOWVIEWPORT_H
#define MAINWINDOWVIEWPORT_H

#include <QMainWindow>

// This is nothing more than a "proxy" or passthru widget so we can promote a regular QWidget
// TODO - remove and construct MainWindow interface programmatically (iKlsR)
class MainWindowViewPort : public QMainWindow
{
public:
    MainWindowViewPort();
};

#endif // MAINWINDOWVIEWPORT_H
