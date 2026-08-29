/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "properties.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>
#include "../core/guidhelper.h"

Property::Property()
{
	id = GuidHelper::createGuid();
}

QJsonObject Property::serialize()
{
    QJsonObject obj;
    obj["id"] = id;
	obj["name"] = name;
	obj["uniform"] = getUniformName();
    obj["displayName"] = displayName;

    switch(type) {
    case PropertyType::Bool:
        obj["type"] = "bool";
        break;
    case PropertyType::Color:
        obj["type"] = "color";
        break;
    case PropertyType::Float:
        obj["type"] = "float";
        break;
    case PropertyType::Int:
        obj["type"] = "int";
        break;
    case PropertyType::Texture:
        obj["type"] = "texture";
        break;
    case PropertyType::Vec2:
        obj["type"] = "vec2";
        break;
    case PropertyType::Vec3:
        obj["type"] = "vec3";
        break;
    case PropertyType::Vec4:
        obj["type"] = "vec4";
        break;
    }

    return obj;
}

void Property::deserialize(const QJsonObject& obj)
{

}

Property* Property::parse(const QJsonObject& obj)
{
    Property* prop = nullptr;

    if (!obj.contains("type"))
        return nullptr;

    auto type = obj["type"].toString();
    if (type=="bool")
        prop = new BoolProperty();
    else if (type=="color")
        prop = new ColorProperty();
    else if (type=="float")
        prop = new FloatProperty();
    else if (type=="int")
        prop = new IntProperty();
    else if (type=="texture")
        prop = new TextureProperty();
    else if (type=="vec2")
        prop = new Vec2Property();
    else if (type=="vec3")
        prop = new Vec3Property();
    else if (type=="vec4")
        prop = new Vec4Property();

    if (prop != nullptr) {
        prop->id            = obj["id"].toString();
		prop->name          = obj["name"].toString();
        prop->displayName   = obj["displayName"].toString();

        // deserialize custom properties
        prop->deserialize(obj);
    }


    return prop;
}
