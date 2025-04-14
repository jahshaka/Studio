/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/src/irisglfwd.h"
#include <QColor>

class QOpenGLFunctions_3_2_Core;
class QOpenGLShaderProgram;

class OutlinerRenderer
{
public:
	//QOpenGLFunctions_3_2_Core * gl;
	iris::SceneNodePtr selectedNode;
	iris::ShaderPtr meshShader;
	iris::ShaderPtr skinnedShader;
	iris::ShaderPtr particleShader;

	iris::ShaderPtr outlineShader;

	// rtt used to render the selected objects texture
	iris::Texture2DPtr objectTexture;

	// rtt used to produce outline
	iris::Texture2DPtr outlineTexture;

	iris::FullScreenQuad* fsQuad;
	iris::RenderData* renderData;

	OutlinerRenderer();

	void renderOutline(iris::GraphicsDevicePtr device,
		iris::SceneNodePtr selectedNode,
		iris::CameraNodePtr cam,
		float lineWidth = 1.0f,
		QColor color = QColor(255, 255, 255)); //sceneTexture with outline if selected node

	void loadAssets();
	void renderNode(iris::GraphicsDevicePtr device, 
		iris::SceneNodePtr node,
		iris::CameraNodePtr cam);
	~OutlinerRenderer();
};