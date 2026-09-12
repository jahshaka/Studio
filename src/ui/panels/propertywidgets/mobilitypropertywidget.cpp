/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/mobilitypropertywidget.h"

#include <QComboBox>

#include "irisgl/document/scenegraph/scenenode.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/labelwidget.h"
#include "ui/panels/propertywidgets/rowundo.h"

// MOVEMENT (SPECS/REALTIME_REFLECTIONS_SPEC.md §3.3), the panel half.
//
// THE ROW IS THE VERB. The combo writes the reflected `mobility` key through
// panelundo::NodeRows — the very key node.setProperty(id, 'mobility', name)
// writes — so the panel and a script produce the same document edit and the
// same undo step. Nothing here knows how the resolution works; it asks the
// document (SceneNode::resolvedMobility) and prints the answer.
//
// PLAIN WORDS, because the person reading it is not a programmer: the combo
// says Auto / Static / Movable, the tooltip says what that means for what they
// will see, and the line under it says what Auto worked out to and why.

QStringList MobilityPropertyWidget::settingNames()
{
    // Index IS iris::Mobility (Auto, Static, Movable) — the mapping below is a
    // cast, and the names are the document's own spelling, so the panel, the
    // file and the verbs cannot drift apart.
    return { QString::fromLatin1(iris::mobilityName(iris::Mobility::Auto)),
             QString::fromLatin1(iris::mobilityName(iris::Mobility::Static)),
             QString::fromLatin1(iris::mobilityName(iris::Mobility::Movable)) };
}

QStringList MobilityPropertyWidget::settingLabels()
{
    // WHAT THE AUTHOR READS, in the same words as everything else on this row.
    // The document's spellings ("auto"/"static"/"movable") are a file format,
    // not a sentence: "static" is jargon, and a lower-case word in a combo box
    // beside "Moves" and "Never moves" is a third vocabulary for one idea.
    // Same order, so the index is still the enum.
    return { tr("Auto"), tr("Static (never moves)"), tr("Movable (moves)") };
}

QString MobilityPropertyWidget::resolvedText(const iris::SceneNodePtr &node)
{
    if (!node) return QString();
    iris::MobilityReason why = iris::MobilityReason::Default;
    const iris::Mobility resolved = node->resolvedMobility(&why);
    const QString word = resolved == iris::Mobility::Movable
                             ? QObject::tr("Moves") : QObject::tr("Never moves");
    QString because;
    switch (why) {
    case iris::MobilityReason::Physics:   because = QObject::tr("it is a physics object"); break;
    case iris::MobilityReason::Avatar:    because = QObject::tr("it is a character"); break;
    case iris::MobilityReason::Socket:    because = QObject::tr("it is attached to a bone"); break;
    case iris::MobilityReason::Animation: because = QObject::tr("it is animated"); break;
    case iris::MobilityReason::Skeleton:  because = QObject::tr("it has an animation clip"); break;
    case iris::MobilityReason::Particles: because = QObject::tr("it is a particle emitter"); break;
    case iris::MobilityReason::Parent:    because = QObject::tr("what it is attached to moves"); break;
    case iris::MobilityReason::Play:      because = QObject::tr("it moved while playing"); break;
    case iris::MobilityReason::User:      because = QObject::tr("you set it"); break;
    case iris::MobilityReason::Default:   because = QObject::tr("nothing moves it"); break;
    }
    return QObject::tr("%1 - %2").arg(word, because);
}

MobilityPropertyWidget::MobilityPropertyWidget()
    : rows([this]() { return sceneNode; }, [this]() { return services; },
           [this]() { return !loading; })
{
    setting = this->addComboBox("Movement");
    for (const QString &label : settingLabels()) setting->addItem(label);
    if (QComboBox *box = setting->getWidget()) {
        box->setToolTip(tr(
            "Does this object move?\n\n"
            "Auto works it out: anything driven by physics, an animation, a character, a bone "
            "or particles moves, and so does anything attached to something that moves. "
            "Everything else never moves.\n\n"
            "An object that never moves lights the room and appears in its reflections. One "
            "that moves is lit by the room and casts shadows every frame, but never makes the "
            "room's lighting redo itself - which is what keeps the frame rate steady while "
            "things move.\n\n"
            "Set it by hand when Auto is wrong: Movable for something a script will push, "
            "Static for something you only ever move yourself while building the scene."));
    }
    // The row's own mapping: the combo index IS the enum (settingNames()).
    rowundo::bind(setting, rows(QStringLiteral("mobility"),
                                [](const QVariant &row) { return QVariant(row.toInt()); }));
    // The combo changing is also the moment the resolved line can change.
    connect(setting, SIGNAL(currentIndexChanged(int)), this, SLOT(refreshResolved()));

    resolved = this->addLabel("Right now", "");
}

void MobilityPropertyWidget::refreshResolved()
{
    if (resolved) resolved->setText(resolvedText(sceneNode));
}

void MobilityPropertyWidget::setSceneNode(iris::SceneNodePtr node)
{
    this->sceneNode = node;
    if (!node) return;
    // `loading` is rowundo's guard: setCurrentIndex emits, and a populate that
    // looked like an edit would write the node's own value back and record an
    // undo step for a selection change.
    loading = true;
    setting->setCurrentIndex(static_cast<int>(node->mobility()));
    loading = false;
    refreshResolved();
}
