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
#include "irisgl/irisglfwd.h"
#include "irisgl/core/value.h"

#include <string>

// Widened from CustomMaterialPtr to MaterialPtr (SCRIPTING_SPEC §1.4): PbrMaterial
// edits — the engine viewport's authoring model — are undoable through the same
// command. CustomMaterial keeps its property-list/texture-uniform path; every
// other material goes through the virtual Material::setValue bridge.
class ChangeMaterialPropertyCommand : public QUndoCommand
{
    iris::MaterialPtr material;
    std::string propName;
    iris::Value oldValue;
    iris::Value newValue;


public:
    /// The two Values are of the row's kind: the verb and the panel convert at their edge
    /// (qtstd::toRowValue) BEFORE they push, so a replay is never refused.
    ChangeMaterialPropertyCommand(iris::MaterialPtr material, std::string name, iris::Value oldValue, iris::Value newValue);

    void undo() override;
    void redo() override;

private:
    void setMaterialProperty(const iris::Value &value);
};

#endif // CHANGEMATERIALPROPERTYCOMMAND_H
