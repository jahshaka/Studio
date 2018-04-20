#ifndef SOFTWAREUPDATEDIALOG_H
#define SOFTWAREUPDATEDIALOG_H

#include <QDialog>

namespace Ui {
	class SoftwareUpdateDialog;
}

class SoftwareUpdateDialog : public QDialog
{
	Q_OBJECT

public:
	explicit SoftwareUpdateDialog(QDialog *parent = Q_NULLPTR);
	void setVersionNotes(QString nodes);
	void setDownloadUrl(QString url);

	~SoftwareUpdateDialog();

private:
	Ui::SoftwareUpdateDialog *ui;
	QString downloadUrl;
};

#endif //SOFTWAREUPDATEDIALOG_H
