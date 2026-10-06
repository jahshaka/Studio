/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/


#include "services/physicsconstraints.h"

#include <QVariantMap>
#include <QVector>

#include "commands/nodeeditcommand.h"
#include "irisgl/document/physics/physicsproperties.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/undoservice.h"

namespace physicsconstraints {

namespace {

struct TypeRow { const char *name = nullptr; iris::PhysicsConstraintType type = iris::PhysicsConstraintType::None; };

// None is the enum's "unset" value: a constraint of no type builds nothing in
// PhysicsHelper, so it is not a name a caller can pass.
const TypeRow kTypes[] = {
    { "ball", iris::PhysicsConstraintType::Ball },
    { "dof6", iris::PhysicsConstraintType::Dof6 },
};

QString nameOf(iris::PhysicsConstraintType type)
{
    for (const TypeRow &row : kTypes)
        if (row.type == type) return QString::fromLatin1(row.name);
    return QStringLiteral("none");
}

bool refuse(QString *error, const QString &why)
{
    if (error) *error = why;
    return false;
}

}   // namespace

QStringList typeNames()
{
    QStringList out;
    for (const TypeRow &row : kTypes) out << QString::fromLatin1(row.name);
    return out;
}

bool add(const iris::SceneNodePtr &from, const iris::SceneNodePtr &to, const QString &type,
         UndoService *undo, QString *error)
{
    if (!from || !to) return refuse(error, QStringLiteral("both ends of a constraint must be nodes"));

    const QString key = type.trimmed().toLower();
    const TypeRow *row = nullptr;
    for (const TypeRow &r : kTypes)
        if (key == QLatin1String(r.name)) row = &r;
    if (!row)
        return refuse(error, QStringLiteral("unknown type '%1' (%2)")
                                 .arg(type, typeNames().join(QStringLiteral(", "))));
    if (from == to)
        return refuse(error, QStringLiteral("'%1' cannot be joined to itself").arg(from->getName()));
    for (const iris::SceneNodePtr &n : { from, to }) {
        if (!n->isPhysicsBody)
            return refuse(error, QStringLiteral("'%1' is not a physics body — make it one first "
                                                "(node.physics(id, {type: \"rigidbody\"}))")
                                     .arg(n->getName()));
    }
    const QString toGuid = to->getGUID();
    for (const iris::ConstraintProperty &c : from->physicsProperty.constraints) {
        if (c.constraintTo == toGuid && c.constraintType == row->type)
            return refuse(error, QStringLiteral("'%1' already has a %2 constraint to '%3'")
                                     .arg(from->getName(), key, to->getName()));
    }

    const QVector<iris::ConstraintProperty> was = from->physicsProperty.constraints;
    QVector<iris::ConstraintProperty> next = was;
    iris::ConstraintProperty c;
    c.constraintFrom = from->getGUID();
    c.constraintTo = toGuid;
    c.constraintType = row->type;
    next.append(c);

    // Whole-list assignment both ways, so the push's immediate redo() is
    // idempotent (NodeEditCommand's contract).
    from->physicsProperty.constraints = next;
    if (undo)
        undo->push(new NodeEditCommand(QStringLiteral("Add Constraint"),
                                       [from, next]() { from->physicsProperty.constraints = next; },
                                       [from, was]() { from->physicsProperty.constraints = was; }));
    return true;
}

QVariantList describe(const iris::SceneNodePtr &node)
{
    QVariantList out;
    if (!node) return out;
    for (const iris::ConstraintProperty &c : node->physicsProperty.constraints)
        out.append(QVariantMap{ { QStringLiteral("to"), c.constraintTo },
                                { QStringLiteral("type"), nameOf(c.constraintType) } });
    return out;
}

}   // namespace physicsconstraints
