/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef CHANGEMATERIALPROPERTYCOMMAND_H
#define CHANGEMATERIALPROPERTYCOMMAND_H

#include <QUndoCommand>
#include <QMatrix4x4>
#include <QVariant>
#include "../irisgl/src/irisglfwd.h"

class ChangeMaterialPropertyCommand : public QUndoCommand
{
    iris::CustomMaterialPtr material;
    QString propName;
    QVariant oldValue;
    QVariant newValue;


public:
    ChangeMaterialPropertyCommand(iris::CustomMaterialPtr material, QString name, QVariant oldValue, QVariant newValue);

    void undo() override;
    void redo() override;

private:
    void setMaterialProperty(QString name, QVariant value);
};

#endif // CHANGEMATERIALPROPERTYCOMMAND_H
