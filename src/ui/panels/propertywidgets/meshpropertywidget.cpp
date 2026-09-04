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
#include "services/planarreflectors.h"

#include <QMessageBox>

MeshPropertyWidget::MeshPropertyWidget()
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
	connect(faceCullMode, SIGNAL(currentIndexChanged(const QString&)), this, SLOT(onCullModeChanged(const QString&)));

    // A TOP-LEVEL row, not a buried "Reflections" section: marking a flat
    // surface is the ONLY way a user gets a mirror, and an author cannot be
    // expected to hunt for it (PLANAR_REFLECTIONS_SPEC.md §7). How many of the
    // marked planes actually render is the World panel's budget, not this.
    planarReflector = this->addCheckBox("Planar Reflector", false);
    connect(planarReflector, SIGNAL(valueChanged(bool)), this, SLOT(onPlanarReflectorChanged(bool)));

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
	if (cullMode == "Front")
		meshNode->setFaceCullingMode(iris::FaceCullingMode::Front);
	else if (cullMode == "Back")
		meshNode->setFaceCullingMode(iris::FaceCullingMode::Back);
	else if (cullMode == "None")
		meshNode->setFaceCullingMode(iris::FaceCullingMode::None);
	else
		meshNode->setFaceCullingMode(iris::FaceCullingMode::DefinedInMaterial);
}

void MeshPropertyWidget::onPlanarReflectorChanged(bool enabled)
{
    if (meshNode.isNull()) return;
    QString error;
    if (!planarreflectors::set(meshNode, enabled, sceneView, &error)) {
        // The service already put the document flag back; the checkbox has to
        // follow, without re-entering this slot.
        planarReflector->blockSignals(true);
        planarReflector->setValue(false);
        planarReflector->blockSignals(false);
        QMessageBox::warning(this, tr("Not a reflection plane"), error);
    }
}

void MeshPropertyWidget::setSceneNode(iris::SceneNodePtr sceneNode)
{
    if (!!sceneNode && sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        this->meshNode = sceneNode.staticCast<iris::MeshNode>();
        planarReflector->blockSignals(true);
        planarReflector->setValue(meshNode->getPlanarReflector());
        planarReflector->blockSignals(false);
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
    } else {
        this->meshNode.clear();
    }
}
