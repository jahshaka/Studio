#include "mainwindow.h"
#include <QCoreApplication>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <qstandardpaths.h>
#include <QThread>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent){

	progressBar = new ProgressBar;
	progressBar->adjustSize();
	progressBar->setTitle("Downloading update...");
	progressBar->setMode(ProgressBar::Mode::Indefinite);
	hasError = false;

	appName = +"/"+ QUrl(QCoreApplication::arguments()[1]).fileName();
	doDownload(QCoreApplication::arguments()[1]);
    
        
//#if QT_CONFIG(ssl)
//    qDebug()<<"using ssl";
//#else
//    qDebug()<<"not using ssl";
//#endif
}

MainWindow::~MainWindow()
{
   
}

void MainWindow::doDownload(QString url)
{
    QNetworkRequest request(url);
    QNetworkReply *reply = manager.get(request);
    QString filePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + appName;
	file = new QFile(filePath);
	file->open(QFile::ReadWrite);
    progressBar->setTitle("Downloading update...");

	QProcess *process = new QProcess(this);

    connect(reply, &QNetworkReply::readyRead,[&, reply]()
    {
        file->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::downloadProgress,[&](qint64 bytesReceived, qint64 bytesTotal)
    {
		if (bytesTotal <= 0) return;
		progressBar->setMaximum(bytesTotal);
		progressBar->setValue(bytesReceived);
    });

    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::error),[&](QNetworkReply::NetworkError error)
    {
		progressBar->setTitle("Error downloading update");
		progressBar->setConfirmationText("there was an error downloading this update");
		progressBar->setConfirmationButtons("ok", "", true, false);
		progressBar->showConfirmationDialog();
		progressBar->clearButtonConnection();
		connect(progressBar->confirmButton(), &QPushButton::clicked, [=]() {
			progressBar->close();
		});
		hasError = true;
    });

	connect(reply, &QNetworkReply::finished, [&, filePath, reply]()
	{
		file->close();

		if (isHttpRedirect(reply)) {
			// follow download
            progressBar->setTitle(tr("Redirectting..."));
			for (auto pair : reply->rawHeaderPairs()) {
				if (pair.first == "Location") {
					doDownload(pair.second);
				}
			}
		}
		else {
			if (!hasError) {
				progressBar->setTitle("Download complete");
				progressBar->setConfirmationText("Would you like to run the installer?");
				progressBar->setConfirmationButtons("yes", "no", true, true);
				progressBar->showConfirmationDialog();
				progressBar->clearButtonConnection();

				connect(progressBar->confirmButton(), &QPushButton::clicked, [=]() {
#ifdef Q_OS_MACOS
                     process->startDetached("hdiutil", QStringList({"attach",file->fileName()}));
#elif defined Q_OS_WIN
					QProcess::startDetached(filePath);
#endif
				});

				connect(progressBar->cancelButton(), &QPushButton::clicked, [=]() {
					progressBar->hideConfirmationDialog();
					progressBar->close();
				});
			}

			//file not beind executed
		}
	});
}

bool MainWindow::isHttpRedirect(QNetworkReply *reply)
{
    int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    return statusCode == 301 || statusCode == 302 || statusCode == 303
           || statusCode == 305 || statusCode == 307 || statusCode == 308;
}
