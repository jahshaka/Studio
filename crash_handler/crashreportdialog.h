#ifndef CRASHREPORTDIALOG_H
#define CRASHREPORTDIALOG_H

#include <QDialog>
#include <QString>

class QWidget;

namespace Ui {
	class CrashReportDialog;
}

class CrashReportDialog : public QDialog
{
	Q_OBJECT

	Ui::CrashReportDialog* ui;
	QString logPath;
	QString version;
public:
	CrashReportDialog(QWidget* parent);
	void setCrashLogPath(QString path);
	void setVersion(QString ver);

private slots:
	void onCancel();
	void onSend();
};
#endif // CRASHREPORTDIALOG_H
