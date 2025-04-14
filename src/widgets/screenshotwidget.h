/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCREENSHOTWIDGET_H
#define SCREENSHOTWIDGET_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class ScreenshotWidget;
}

class ScreenshotWidget : public QDialog
{
    Q_OBJECT

public:
    explicit ScreenshotWidget(QWidget *parent = 0);
    ~ScreenshotWidget();

    void setImage(const QImage &value);
public slots:
    void saveImage();
    //void closeWindow();

private:
    Ui::ScreenshotWidget *ui;
    QImage image;
};

#endif // SCREENSHOTWIDGET_H
