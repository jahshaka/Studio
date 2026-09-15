/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHADERPROPERTY_H
#define SHADERPROPERTY_H

#include <QWidget>
#include <QSharedPointer>

#include "irisgl/irisglfwd.h"
#include "ui/controls/accordionbladewidget.h"

class Database;

class ShaderPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
	ShaderPropertyWidget();
    ~ShaderPropertyWidget();

    void setShaderGuid(const QString&);
    void setDatabase(Database*);

    void leaveEvent(QEvent*) override;

protected slots:
    /// Both combos land here (the per-combo slots beside it were never
    /// connected to anything and are gone — lane DBPTR-1, CRUD).
    void onShaderFileChanged(int);

private:
    QStringList vertexShaders;
    QStringList fragmentShaders;

    CheckBoxWidget *allowBuiltinShaders;
	ComboBoxWidget *vertexShaderCombo;
    ComboBoxWidget *fragmentShaderCombo;

    QString shaderGuid;
    QStringList builtinShaders;
    Database *db = nullptr;
};

#endif // NODEPROPERTY_H
