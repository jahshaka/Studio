/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef GETNAMEDIALOG_H
#define GETNAMEDIALOG_H

#include <QDialog>

namespace Ui {
class GetNameDialog;
}

class GetNameDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GetNameDialog(QWidget *parent = 0);
    ~GetNameDialog();

    void setName(QString name);
    QString getName();

private:
    Ui::GetNameDialog *ui;
};

#endif // GETNAMEDIALOG_H
