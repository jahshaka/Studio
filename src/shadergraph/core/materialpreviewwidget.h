/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

// IMaterialPreviewWidget — the Display dock's 3D preview, abstracted.
//
// In engine viewport mode the GL SceneWidget must never be realized (a live
// QOpenGLWidget freezes the whole window on xcb), so Studio builds an
// engine-rendered preview widget instead and hands it to this module through
// this interface (MainWindow::setEnginePreview). The module never sees the
// engine: it pushes the graph's evaluated iris::PbrMaterial, the primitive
// choice and the background colour, nothing else. Pure Qt + iris types here —
// no engine, no GL, no Ogre.
#include <QColor>
#include "irisgl/src/irisglfwd.h"

class QWidget;

namespace shadergraph
{

class IMaterialPreviewWidget
{
public:
	// Mirrors PreviewModel (widgets/scenewidget.h) — same primitives, same order.
	enum class Model
	{
		Sphere,
		Cube,
		Plane,
		Cylinder,
		Capsule,
		Torus
	};

	virtual ~IMaterialPreviewWidget() {}

	/// The QWidget to dock (the implementation itself).
	virtual QWidget *previewWidget() = 0;
	/// Applies `material` to the preview primitive. Null is ignored.
	virtual void setPreviewMaterial(iris::MaterialPtr material) = 0;
	/// Switches the preview primitive (the legacy Model menu).
	virtual void setPreviewModel(Model model) = 0;
	/// The background colour behind the primitive (the legacy Background menu).
	virtual void setPreviewBackground(const QColor &colour) = 0;
};

}
