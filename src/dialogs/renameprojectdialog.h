/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef RENAMEPROJECTDIALOG_H
#define RENAMEPROJECTDIALOG_H

#include <QDialog>

namespace Ui {
    class RenameProjectDialog;
}

class RenameProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RenameProjectDialog(QDialog *parent = Q_NULLPTR);
    ~RenameProjectDialog();

public slots:
    void newText();

signals:
    void newTextEmit(const QString &);

private:
    Ui::RenameProjectDialog *ui;
};

#endif // RENAMEPROJECTDIALOG_H
