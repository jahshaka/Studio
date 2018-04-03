/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHADERPROPERTY_H
#define SHADERPROPERTY_H

#include <QWidget>
#include <QSharedPointer>

#include "irisgl/src/irisglfwd.h"
#include "../accordianbladewidget.h"

class ShaderPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
	ShaderPropertyWidget();
    ~ShaderPropertyWidget();

    void setShaderGuid(const QString&);

    void leaveEvent(QEvent*) override;

protected slots:
    void onVertexShaderFileChanged(int);
    void onFragmentShaderFileChanged(int);
    void onAllowBuiltinShaders(const bool);

private:
    QStringList vertexShaders;
    QStringList fragmentShaders;
    iris::ShaderPtr shader;

    CheckBoxWidget *allowBuiltinShaders;
    CheckBoxWidget *autoReloadOnChange;
	ComboBoxWidget *vertexShaderCombo;
    ComboBoxWidget *fragmentShaderCombo;

    QStringList builtinShaders;
};

#endif // NODEPROPERTY_H
