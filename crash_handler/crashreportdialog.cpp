#include "crashreportdialog.h"
#include "ui_crashreportdialog.h"
#include "client\windows\handler\exception_handler.h"
#include "client\windows\sender\crash_report_sender.h"

#include <QProgressDialog>
#include <QTextEdit>
#include <QThread>
#include <QDebug>
#include <QMessageBox>

#include <map>
#include <string>

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
	//qDebug() << "sending report";
	google_breakpad::CrashReportSender sender(L"crash.checkpoint");

	std::map<std::wstring, std::wstring> params;
	params[L"text"] = ui->textEdit->toPlainText().toStdWString();
	params[L"prod"] = L"Jahshaka";
	params[L"ver"] = version.toStdWString();

	// Finally, send a report
	std::map<std::wstring, std::wstring> files;
	if (ui->crashCheck->checked())
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
}