/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QDialog>

namespace Ui {
    class ProgressDialog;
}

class ProgressDialog : public QDialog
{
    Q_OBJECT

public:
    ProgressDialog(QDialog *parent = nullptr);
    ~ProgressDialog();

public slots:
    void setLabelText(const QString&);
    void setRange(int, int);
    void setValue(int);
	void setValueAndText(int value, QString text);
    void reset();

private:
    Ui::ProgressDialog *ui;
};

#endif // PROGRESSDIALOG_H
