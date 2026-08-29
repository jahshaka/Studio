#ifndef SOCKETS_H
#define SOCKETS_H

#include <QString>

#include "../models/socketmodel.h"
#include "../core/sockethelper.h"

class FloatSocketModel : public SocketModel
{
public:
	FloatSocketModel(QString name, QString defaultValue = "0.0f") :
		SocketModel(name, "float")
	{
		this->setValue(defaultValue);
		socketColor = QColor(105, 150, 150);
	}

	QString convertVarTo(SocketModel* toModel)
	{
		return SocketHelper::convertVectorValue(varName, this, toModel);
	}

	virtual SocketModel* duplicate()
	{
		return new FloatSocketModel(name);
	}

	virtual bool canConvertTo(SocketModel* other)
	{
		if (other->typeName == "float" ||
			other->typeName == "vec2" ||
			other->typeName == "vec3" ||
			other->typeName == "vec4")
			return true;
		return false;
	}
};

class Vector2SocketModel : public SocketModel
{
public:
	Vector2SocketModel(QString name, QString defaultValue = "vec2(0.0f, 0.0f)") :
		SocketModel(name, "vec2")
	{
		this->setValue(defaultValue);
		socketColor = QColor(105, 150, 150);
	}

	QString convertVarTo(SocketModel* toModel)
	{
		return SocketHelper::convertVectorValue(varName, this, toModel);
	}

	virtual SocketModel* duplicate()
	{
		return new Vector2SocketModel(name);
	}

	virtual bool canConvertTo(SocketModel* other)
	{
		if (other->typeName == "float" ||
			other->typeName == "vec2" ||
			other->typeName == "vec3" ||
			other->typeName == "vec4")
			return true;
		return false;
	}
};

class Vector3SocketModel : public SocketModel
{
public:
	Vector3SocketModel(QString name, QString defaultValue = "vec3(0.0f, 0.0f, 0.0f)") :
		SocketModel(name, "vec3")
	{
		this->setValue(defaultValue);
		socketColor = QColor(105, 150, 150);
	}

	QString convertVarTo(SocketModel* toModel)
	{
		return SocketHelper::convertVectorValue(varName, this, toModel);
	}

	virtual bool canConvertTo(SocketModel* other)
	{
		if (other->typeName == "float" ||
			other->typeName == "vec2" ||
			other->typeName == "vec3" ||
			other->typeName == "vec4")
			return true;
		return false;
	}

	virtual SocketModel* duplicate()
	{
		return new Vector3SocketModel(name);
	}
};

class Vector4SocketModel : public SocketModel
{
public:
	Vector4SocketModel(QString name, QString defaultValue = "vec4(0.0f, 0.0f, 0.0f, 0.0f)") :
		SocketModel(name, "vec4")
	{
		this->setValue(defaultValue);
		socketColor = QColor(105, 150, 150);
	}

	QString convertVarTo(SocketModel* toModel)
	{
		return SocketHelper::convertVectorValue(varName, this, toModel);
	}

	virtual bool canConvertTo(SocketModel* other)
	{
		if (other->typeName == "float" ||
			other->typeName == "vec2" ||
			other->typeName == "vec3" ||
			other->typeName == "vec4")
			return true;
		return false;
	}

	virtual SocketModel* duplicate()
	{
		return new Vector4SocketModel(name);
	}
};

class TextureSocketModel : public SocketModel
{
public:
	TextureSocketModel(QString name) :
		SocketModel(name, "texture")
	{
		socketColor = QColor(60, 155, 60);
	}

	virtual bool canConvertTo(SocketModel* other)
	{
		if (other->typeName == "texture")
			return true;
		return false;
	}

	virtual SocketModel* duplicate()
	{
		return new TextureSocketModel(name);
	}
};

#endif// SOCKETS_H