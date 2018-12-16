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

#include <QJsonObject>

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
    void generate(const QJsonObject&);
    void setTextureWithUniform(const QString&, const QString&);
    void setValue(const QString&, const QVariant&);
    void setBaseMaterialProperties(const QJsonObject&);
    void setName(const QString&);
    void setGuid(const QString&);
    void setProperties(QList<Property*> props);
    QList<Property*> getProperties();
    void setUniformValues(GraphicsDevicePtr device, Property*);
    void purge();

	void setMaterialDefinition(const QJsonObject &def) {
		materialDefinitions = def;
	}

	QJsonObject getMaterialDefinition() {
		return materialDefinitions;
	}

    QString getName();
    QString getGuid();
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

	static CustomMaterialPtr createFromShader(iris::ShaderPtr shader);

    CustomMaterial() = default;
    QString materialName;
    QString materialGuid;
	QString materialPath;

    QJsonObject loadShaderFromDisk(const QString &);
    void createWidgets(const QJsonArray&);

	QJsonObject materialDefinitions;
};

}
Q_DECLARE_METATYPE(iris::CustomMaterial)
Q_DECLARE_METATYPE(iris::CustomMaterialPtr)

#endif // CUSTOMMATERIAL_H
