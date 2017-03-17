/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef TEXTINPUTWIDGET_H
#define TEXTINPUTWIDGET_H

#include <QWidget>

namespace Ui {
    class TextInputWidget;
}

class TextInputWidget : public QWidget
{
    Q_OBJECT

public:
    TextInputWidget(QWidget* parent = 0);
    ~TextInputWidget();

    void setText(const QString&);
    void setLabel(const QString&);
    void clearText();
    QString getText();

private slots:
    void onTextInputChanged(const QString&);

private:
    Ui::TextInputWidget* ui;
};

#endif // TEXTINPUTWIDGET_H
