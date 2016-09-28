/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef COLORVALUEWIDGET_H
#define COLORVALUEWIDGET_H

#include <QWidget>

namespace Ui {
class ColorValueWidget;
}

class ColorValueWidget : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(QString title READ getTitle WRITE setTitle)

    QString _title;
public:
    explicit ColorValueWidget(QWidget *parent = 0);
    ~ColorValueWidget();

    QString getTitle();
    void setTitle(QString title);

private slots:
    void onColorChanged(QColor color);
    void onColorSet(QColor color);

signals:
    void colorChanged(QColor color);
    void setColor(QColor color);

private:
    Ui::ColorValueWidget *ui;
};

#endif // COLORVALUEWIDGET_H
