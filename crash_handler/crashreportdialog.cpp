#include "crashreportdialog.h"
#include "ui_crashreportdialog.h"
#if defined(Q_OS_WIN32)
#include "client\windows\handler\exception_handler.h"
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

	//qDebug() << "sending report";
    google_breakpad::CrashReportSender sender(L"crash.checkpoint");

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

	google_breakpad::ReportResult r = sender.SendCrashReport(L"http://breakpad.jahshaka.com/crashreports", params, files, 0);

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
    QUrl url("http://breakpad.jahshaka.com/crashreports");
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
        qDebug() << "test file size:" << file->size();
        fileDataPart.setBodyDevice(file);
        multiPart->append(fileDataPart);
    }

    connect(manager, &QNetworkAccessManager::finished, this, [this](QNetworkReply* reply) {
        if (reply->error()) {
            qDebug()<<"error";
        } else {
            qDebug()<<"success";
        }
        if(file->exists() && file->isOpen())
            file->close();

        file->deleteLater();
        this->close();
    } );

    manager->post(request,multiPart);
#endif

}
