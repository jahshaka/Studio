#include "crashreportdialog.h"
#include "ui_crashreportdialog.h"
#if defined(Q_OS_WIN32)
#include "client\windows\sender\crash_report_sender.h"
#elif defined(Q_OS_LINUX)
#include "common/linux/google_crashdump_uploader.h"
#endif

#include <QProgressDialog>
#include <QTextEdit>
#include <QThread>
#include <QDebug>
#include <QMessageBox>

#include <map>
#include <string>

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QFile>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QUrlQuery>
#include <QFile>
#include <QStandardPaths>

#define SERVER_URL L"http://breakpad.jahshaka.com/crashreports"

CrashReportDialog::CrashReportDialog(QWidget* parent):
	QDialog(parent),
	ui(new Ui::CrashReportDialog)
{
	ui->setupUi(this);

	//ui->crashCheck->hide();
	ui->crashCheck->setChecked(false);
	ui->crashCheck->setEnabled(false);

	connect(ui->sendButton, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(ui->cancelButton, SIGNAL(clicked()), this, SLOT(onCancel()));
}

void CrashReportDialog::setCrashLogPath(QString path)
{
	logPath = path;
	ui->crashCheck->setChecked(true);
	ui->crashCheck->setEnabled(true);
	ui->crashCheck->show();
}

void CrashReportDialog::setVersion(QString ver)
{
	version = ver;
}

void CrashReportDialog::onCancel()
{
	this->close();
}

void CrashReportDialog::onSend()
{
#if defined(Q_OS_WIN32)
	QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("crash.checkpoint");
    google_breakpad::CrashReportSender sender(path.toStdWString());

	std::map<std::wstring, std::wstring> params;
	params[L"text"] = ui->textEdit->toPlainText().toStdWString();
	params[L"prod"] = L"Jahshaka";
	params[L"ver"] = version.toStdWString();

	// Finally, send a report
	std::map<std::wstring, std::wstring> files;
	if (ui->crashCheck->isChecked())
		files[std::wstring(L"upload_file_minidump")] = logPath.toStdWString();

	QProgressDialog progress("Uploading Crash Report", "Cancel", 0, 3, this);
	progress.setWindowModality(Qt::WindowModal);
	progress.setValue(2);

	google_breakpad::ReportResult r = sender.SendCrashReport(SERVER_URL, params, files, 0);

	progress.setValue(3);
	QMessageBox msg;
	if (r == google_breakpad::RESULT_SUCCEEDED)
		msg.setText("Report sent successfully!");
	else
		msg.setText("Failed to send report!");
	msg.exec();
    this->close();
#elif defined(Q_OS_LINUX)

    QNetworkAccessManager* manager = new QNetworkAccessManager();
    QUrl url(QString::fromStdWString(SERVER_URL));
    QUrlQuery query;


    query.addQueryItem("text", ui->textEdit->toPlainText());
    query.addQueryItem("prod", "Jahshaka");
    query.addQueryItem("ver", version);

    url.setQuery(query.query());
    QNetworkRequest request(url);

    QHttpPart fileDataPart;
    fileDataPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"upload_file_minidump\"; filename=\"data.dump\""));
    fileDataPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("application/octet-stream"));

    QHttpMultiPart *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QFile *file = new QFile(logPath);
    if (file->exists()) {
        file->open(QIODevice::ReadOnly);
        fileDataPart.setBodyDevice(file);
        multiPart->append(fileDataPart);
    }

    // params
    QHttpPart textPart;
    textPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"text\""));
    textPart.setBody(ui->textEdit->toPlainText().toUtf8());
    multiPart->append(textPart);

    QHttpPart prodPart;
    prodPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"prod\""));
    prodPart.setBody("Jahshaka");
    multiPart->append(prodPart);

    QHttpPart verPart;
    verPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"ver\""));
    verPart.setBody(version.toUtf8());
    multiPart->append(verPart);

    connect(manager, &QNetworkAccessManager::finished, this, [this, file](QNetworkReply* reply) {
        QMessageBox msg;
        if (reply->error()) {
            msg.setText("Failed to send report!");
        } else {
            msg.setText("Report sent successfully!");
        }
        if(file->exists()) {
            if(file->isOpen())
                file->close();
            file->remove();
        }

        delete file;

        msg.exec();
        this->close();
    } );

    manager->post(request,multiPart);
#endif

}
