#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QFile>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    file = new QFile("file.download");
    file->open(QFile::ReadWrite);

    //auto url = "https://github.com/jahshaka/VR/releases/download/v0.6.1-alpha/jahshaka-win-0.6.1-alpha.exe";
    auto url = "http://localhost/download/genius.mkv";
    //auto url = "https://www.jahfx.com/download/skull-island/?wpdmdl=730&refresh=5b21810ea6a311528922382";
    doDownload(url);

#if QT_CONFIG(ssl)
    qDebug()<<"using ssl";
#else
    qDebug()<<"not using ssl";
#endif
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::doDownload(QString url)
{
    QNetworkRequest request(url);
    QNetworkReply *reply = manager.get(request);

    connect(reply, &QNetworkReply::readyRead,[&, reply]()
    {
        file->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::downloadProgress,[&](qint64 bytesReceived, qint64 bytesTotal)
    {
        qDebug()<<bytesReceived << "/" << bytesTotal;
        ui->progressBar->setMaximum(bytesTotal);
        ui->progressBar->setValue(bytesReceived);
    });

    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),[&](QNetworkReply::NetworkError error)
    {
        qDebug()<<"error: "<<error;
        QMessageBox::warning(this, "Error","Error downloading update");
    });

    connect(reply, &QNetworkReply::finished,[&, reply]()
    {
        qDebug()<<"Download complete";
        qDebug()<<reply->readAll();
        if (isHttpRedirect(reply)) {
            // follow download
            qDebug()<<"redirect";
            ///Ddebug()<<reply->header(QNetworkRequest::RedirectPolicyAttribute)
            ///
            for (auto pair : reply->rawHeaderPairs()) {
                qDebug()<<pair;
            }
        }

        file->close();
    });
}

void MainWindow::startDownload()
{
    QStringList args = QCoreApplication::instance()->arguments();
    args.takeFirst();           // skip the first argument, which is the program's name
    if (args.isEmpty()) {
        printf("Qt Download example - downloads all URLs in parallel\n"
               "Usage: download url1 [url2... urlN]\n"
               "\n"
               "Downloads the URLs passed in the command-line to the local directory\n"
               "If the target file already exists, a .0, .1, .2, etc. is appended to\n"
               "differentiate.\n");
        QCoreApplication::instance()->quit();
        return;
    }

    //auto url = args[0];
    auto url = "http://github.com/jahshaka/VR/releases/download/v0.6.1-alpha/jahshaka-win-0.6.1-alpha.exe";
    doDownload(url);
}

bool MainWindow::isHttpRedirect(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qDebug()<<statusCode;
    return statusCode == 301 || statusCode == 302 || statusCode == 303
           || statusCode == 305 || statusCode == 307 || statusCode == 308;
}
