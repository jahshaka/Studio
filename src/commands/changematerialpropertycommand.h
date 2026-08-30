/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CHANGEMATERIALPROPERTYCOMMAND_H
#define CHANGEMATERIALPROPERTYCOMMAND_H

#include <QUndoCommand>
#include <QMatrix4x4>
#include <QVariant>
#include "irisgl/irisglfwd.h"

// Widened from CustomMaterialPtr to MaterialPtr (SCRIPTING_SPEC §1.4): PbrMaterial
// edits — the engine viewport's authoring model — are undoable through the same
// command. CustomMaterial keeps its property-list/texture-uniform path; every
// other material goes through the virtual Material::setValue bridge.
class ChangeMaterialPropertyCommand : public QUndoCommand
{
    iris::MaterialPtr material;
    QString propName;
    QVariant oldValue;
    QVariant newValue;


public:
    ChangeMaterialPropertyCommand(iris::MaterialPtr material, QString name, QVariant oldValue, QVariant newValue);

    void undo() override;
    void redo() override;

private:
    void setMaterialProperty(QString name, QVariant value);
};

#endif // CHANGEMATERIALPROPERTYCOMMAND_H
