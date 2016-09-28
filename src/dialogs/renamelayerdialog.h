/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef RENAMELAYERDIALOG_H
#define RENAMELAYERDIALOG_H

#include <QDialog>

namespace Ui {
class RenameLayerDialog;
}

class RenameLayerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RenameLayerDialog(QWidget *parent = 0);
    void setName(QString name);
    QString getName();
    ~RenameLayerDialog();

private:
    Ui::RenameLayerDialog *ui;
};

#endif // RENAMELAYERDIALOG_H
