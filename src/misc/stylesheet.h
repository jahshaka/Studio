/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QWidget>

class StyleSheet : public QObject
{
public:
	static const QString QPushButtonBlue();
    static const QString QPushButtonInvisible();
    static const QString QPushButtonGreyscale();
    static const QString QPushButtonRounded(int size);
    static const QString QSpinBox();
    static const QString QSlider();
    static const QString QLineEdit();
    static const QString QWidgetDark();
    static const QString QLabelWhite();
    static const QString QComboBox();
    static const QString QPushButtonGrouped();
};


