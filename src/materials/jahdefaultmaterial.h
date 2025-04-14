/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef JAHDEFAULTMATERIAL_H
#define JAHDEFAULTMATERIAL_H

#include <QFileInfo>
#include <QVariant>
#include "../constants.h"
#include "../../irisgl/src/core/irisutils.h"
#include "../../irisgl/src/graphics/material.h"
#include "../../irisgl/src/materials/custommaterial.h"

class JahDefaultMaterial : public iris::CustomMaterial
{
public:
	JahDefaultMaterial()
	{
		this->setName("DefaultMaterial");
		auto file = QFileInfo(IrisUtils::getAbsoluteAssetPath(
			Constants::SHADER_DEFS + "DefaultMaterial.shader"));
		//this->generate(file.absoluteFilePath());
	}

	virtual bool onValueChange(const QString &name, const QVariant &value)
	{
		if (name == "alpha") {
			float floatVal = (float)value.toDouble();
			if (floatVal >= 0.99999999f)
			{
				this->setRenderLayer((int)iris::RenderLayer::Opaque);
				this->renderStates.blendState = iris::BlendState::Opaque;
			}
			else
			{
				this->setRenderLayer((int)iris::RenderLayer::Transparent);
				this->renderStates.blendState = iris::BlendState::AlphaBlend;
			}
		}
		return true;
	}
};

#endif