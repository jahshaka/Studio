#ifndef CHANGEMATERIALPROPERTYCOMMAND_H
#define CHANGEMATERIALPROPERTYCOMMAND_H

#include <QUndoCommand>
#include <QMatrix4x4>
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
