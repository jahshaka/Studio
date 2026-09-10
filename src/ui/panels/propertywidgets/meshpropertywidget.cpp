/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/meshpropertywidget.h"
#include "ui/controls/filepickerwidget.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "ui/controls/comboboxwidget.h"
#include "ui/controls/checkboxwidget.h"
#include "ui/controls/lightchannelswidget.h"
#include "services/planarreflectors.h"
#include "commands/setnodepropertycommand.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "ui/panels/propertywidgets/rowundo.h"

#include <QMessageBox>

// The three rows here are all node properties (debt L6): the cull mode and the
// lighting channels are REFLECTED keys — `faceCullingMode` and `lightMask`, the
// same keys node.setProperty / node.setLightMask write — and the reflector flag
// goes through the planarreflectors service, which is what
// node.setPlanarReflector calls. Each gesture is one undo step.
MeshPropertyWidget::MeshPropertyWidget()
    : rows([this]() { return iris::SceneNodePtr(meshNode); }, [this]() { return services; },
           [this]() { return !loading; })
{
    // MESH PATH ROW — deliberately still absent (deep audit 2026-09, area 5).
    //
    // The row was commented out because swapping a MeshNode's mesh never
    // reached the engine: SceneMirror wrote Entry::meshPtr and never read it,
    // so the picker changed the document and nothing on screen. That half is
    // FIXED — the mirror re-attaches on a mesh change now (mirror.document_to_engine
    // asserts it, and the material preview dock dropped its node-churn
    // workaround) — but the row cannot come back as it was written: it called
    // MeshNode::setMesh(<absolute path>) directly, which
    //   * bypasses the ONE import pipeline (src/services/import/): no sniff, no
    //     validation, no CAS object, no project pin — and it serialises a raw
    //     absolute path into the scene;
    //   * runs assimp on the UI thread;
    //   * is not undoable;
    //   * has no ApiRegistry verb, which SCRIPTING_SPEC §2.3 makes a
    //     prerequisite, not a nicety.
    // What it needs is a `node.setMesh(<asset guid>)` verb resolving through the
    // asset store, an undo command, and a library picker rather than a file
    // dialog. That is a feature, not this lane's fix.
    //meshPicker = this->addFilePicker("Mesh Path");
	faceCullMode = this->addComboBox("Face Cull Mode");
	faceCullMode->addItem("Front");
	faceCullMode->addItem("Back");
	faceCullMode->addItem("None");
	faceCullMode->addItem("DefinedInMaterial");
	// The combo row carries a NAME; the mapping onto the enum lives here.
	rowundo::bind(faceCullMode, rows(QStringLiteral("faceCullingMode"), [this](const QVariant &row) {
		const QString mode = faceCullMode->getWidget()->itemText(row.toInt());
		if (mode == "Front") return QVariant(int(iris::FaceCullingMode::Front));
		if (mode == "Back")  return QVariant(int(iris::FaceCullingMode::Back));
		if (mode == "None")  return QVariant(int(iris::FaceCullingMode::None));
		return QVariant(int(iris::FaceCullingMode::DefinedInMaterial));
	}));

    // A TOP-LEVEL row, not a buried "Reflections" section: marking a flat
    // surface is the ONLY way a user gets a mirror, and an author cannot be
    // expected to hunt for it (PLANAR_REFLECTIONS_SPEC.md §7). How many of the
    // marked planes actually render is the World panel's budget, not this.
    planarReflector = this->addCheckBox("Planar Reflector", false);
    connect(planarReflector, SIGNAL(valueChanged(bool)), this, SLOT(onPlanarReflectorChanged(bool)));

    // LIGHTING CHANNELS, object side. Every node is on every channel by
    // default, so this row is inert until a user turns something off — which is
    // also why it can sit here without changing any existing scene.
    lightChannels = new LightChannelsWidget(this);
    lightChannels->setDescription(
        tr("Only lights that share a channel with this object light it. Shadows are NOT "
           "filtered: an unlit object still casts a shadow from that light."));
    this->addWidgetToContent(lightChannels);
    connect(lightChannels, &LightChannelsWidget::maskChanged,
            this, &MeshPropertyWidget::onLightChannelsChanged);

    //connect(meshPicker, SIGNAL(onPathChanged(QString)), SLOT(onMeshPathChanged(QString)));
}

MeshPropertyWidget::~MeshPropertyWidget()
{

}

void MeshPropertyWidget::onMeshPathChanged(const QString &path)
{
    // Nothing connects here; the row that would is off (see the ctor).
    Q_UNUSED(path);
    //meshNode->setMesh(path);
}

void MeshPropertyWidget::onCullModeChanged(const QString& cullMode)
{
	Q_UNUSED(cullMode)   // the row is wired through rowundo (faceCullingMode)
}

void MeshPropertyWidget::onPlanarReflectorChanged(bool enabled)
{
    if (loading || meshNode.isNull()) return;
    QString error;
    if (planarreflectors::set(meshNode, enabled, sceneView, &error)) {
        // Applied and accepted: record it. The undo replays the same service
        // call (NodeEditCommand's contract) — the flag alone would skip the
        // renderer-side registration the service performs.
        auto node = meshNode;
        IEditorViewport *view = sceneView;
        panelundo::pushEdit(services, tr("Planar Reflector"),
                            [node, enabled, view]() { planarreflectors::set(node, enabled, view); },
                            [node, enabled, view]() { planarreflectors::set(node, !enabled, view); });
        return;
    }
    {
        // The service already put the document flag back; the checkbox has to
        // follow, without re-entering this slot.
        planarReflector->blockSignals(true);
        planarReflector->setValue(false);
        planarReflector->blockSignals(false);
        QMessageBox::warning(this, tr("Not a reflection plane"), error);
    }
}

void MeshPropertyWidget::onLightChannelsChanged(quint32 mask)
{
    if (loading || meshNode.isNull()) return;
    // Straight to the document: the mirror pushes the mask onto the node's Item
    // on the next sync, change-guarded like every other flag. `lightMask` is a
    // reflected key, so the step is the one node.setLightMask records.
    const QVariant before = meshNode->getPropertyValue(QStringLiteral("lightMask"));
    meshNode->setLightMask(mask);
    const QVariant after = meshNode->getPropertyValue(QStringLiteral("lightMask"));
    if (before == after || !services || !services->undo) return;
    services->undo->push(new SetNodePropertyCommand(meshNode, QStringLiteral("lightMask"),
                                                    before, after));
}

void MeshPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode && sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        this->meshNode = sceneNode.staticCast<iris::MeshNode>();
        loading = true;
        planarReflector->blockSignals(true);
        planarReflector->setValue(meshNode->getPlanarReflector());
        planarReflector->blockSignals(false);
        // setMask does not emit, so this cannot write the value back into the
        // node it was just read from.
        lightChannels->setMask(meshNode->getLightMask());
        //meshPicker->setFilepath(meshNode->meshPath);

		switch (meshNode->getFaceCullingMode())
		{
		case iris::FaceCullingMode::Back:
			faceCullMode->setCurrentItem("Back");
			break;
		case iris::FaceCullingMode::Front:
			faceCullMode->setCurrentItem("Front");
			break;
		case iris::FaceCullingMode::None:
			faceCullMode->setCurrentItem("None");
			break;
		case iris::FaceCullingMode::DefinedInMaterial:
			faceCullMode->setCurrentItem("DefinedInMaterial");
			break;
		}
		loading = false;
    } else {
        this->meshNode.clear();
    }
}
