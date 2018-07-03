#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QNetworkAccessManager>

namespace Ui {
class MainWindow;
}

// http://doc.qt.io/qt-5/qtnetwork-download-main-cpp.html

class QNetworkReply;
class QNetworkAccessManager;
class QFile;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    QNetworkAccessManager manager;
    QVector<QNetworkReply *> currentDownloads;

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void doDownload(QString url);
    void startDownload();
    bool isHttpRedirect(QNetworkReply *reply);

    QFile* file;

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
