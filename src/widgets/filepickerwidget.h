/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef FILEPICKERWIDGET_H
#define FILEPICKERWIDGET_H

#include <QWidget>
#include <QLabel>

namespace Ui {
    class FilePickerWidget;
}

class FilePickerWidget : public QWidget
{
    Q_OBJECT

private slots:

void filePicker();

public:

    QString filename;
    QString filepath;
    QString suffix;

    QString getFilePath() {
        return filepath;
    }

    explicit FilePickerWidget(QWidget *parent = 0);
    ~FilePickerWidget();
    Ui::FilePickerWidget *ui;

    QString getFilepath() const;
    void setFilepath(const QString &value);

signals:
    void onPathChanged(const QString&);

private:
    QString openFile();

};

#endif // FILEPICKERWIDGET_H
