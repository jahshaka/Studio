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
	//progressBar->show();
	//progressBar->adjustSize();
	progressBar->setTitle("Downloading update...");
	progressBar->setMode(ProgressBar::Mode::Indefinite);
	hasError = false;

	appName = +"/"+ QUrl(QCoreApplication::arguments()[1]).fileName();
        qDebug() << appName;
	doDownload(QCoreApplication::arguments()[1]);
        
	/*appName = "/cop.exe";
	doDownload(url);*/

#if QT_CONFIG(ssl)
    qDebug()<<"using ssl";
#else
    qDebug()<<"not using ssl";
#endif
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

	QProcess *process = new QProcess(this);
	QObject::connect(process, &QProcess::readyReadStandardOutput, [this, process]()
	{
		qDebug().noquote() << QString(process->readAllStandardOutput());
	});
	QObject::connect(process, &QProcess::readyReadStandardError, [this, process]()
	{
		qDebug().noquote() << QString(process->readAllStandardError());
	});
	QObject::connect(process, &QProcess::errorOccurred, [this, process](QProcess::ProcessError error)
	{
		qDebug() << "Process Error: " << error;
		qDebug() << process->readAllStandardOutput();

	});

    connect(reply, &QNetworkReply::readyRead,[&, reply]()
    {
        file->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::downloadProgress,[&](qint64 bytesReceived, qint64 bytesTotal)
    {
        //qDebug()<<bytesReceived << "/" << bytesTotal;
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
			for (auto pair : reply->rawHeaderPairs()) {
				if (pair.first == "Location") {
					doDownload(pair.second);
				}
			}
		}
		else {
			if (!hasError) {
				progressBar->setTitle("Download complete");
				progressBar->setConfirmationText("Would you like to execute the file?");
				progressBar->setConfirmationButtons("yes", "later", true, true);
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
