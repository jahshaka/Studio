/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
	void setType(QString string);

	~SoftwareUpdateDialog();

private:
	Ui::SoftwareUpdateDialog *ui;
	QString downloadUrl;
	QString type;
};

#endif //SOFTWAREUPDATEDIALOG_H
