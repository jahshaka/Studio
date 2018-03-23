/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef CUSTOMMATERIAL_H
#define CUSTOMMATERIAL_H

#include "../graphics/material.h"
#include "../irisglfwd.h"
#include "../core/property.h"

class QOpenGLFunctions_3_2_Core;

namespace iris
{

class CustomMaterial : public Material
{
public:
    QList<Property*> properties;

    void begin(GraphicsDevicePtr device, ScenePtr scene) override;
    void end(GraphicsDevicePtr device, ScenePtr scene) override;

    void generate(const QString&, bool project = false);
    void setTextureWithUniform(const QString&, const QString&);
    void setValue(const QString&, const QVariant&);
    void setBaseMaterialProperties(const QJsonObject&);
    void setName(const QString&);
    void setProperties(QList<Property*> props);
    QList<Property*> getProperties();
    void setUniformValues(GraphicsDevicePtr device, Property*);
    void purge();

    QString getName() const;
    QString firstTextureSlot() const;
    int getCalculatedPropHeight() const;

    static CustomMaterialPtr create();
	// called when the material's value is changed
	// returning true means the change is accepted and false means the change is rejected
	virtual bool onValueChange(const QString &name, const QVariant &value)
	{
		return true;
	}

	MaterialPtr duplicate() override;

	CustomMaterialPtr createFromShader(iris::ShaderPtr shader);

protected:
    CustomMaterial() = default;
    QString materialName;
	QString materialPath;

    QJsonObject loadShaderFromDisk(const QString &);
};

Q_DECLARE_METATYPE(CustomMaterial)
Q_DECLARE_METATYPE(CustomMaterialPtr)

}

#endif // CUSTOMMATERIAL_H
