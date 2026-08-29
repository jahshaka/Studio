/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "sockethelper.h"
#include "../models/socketmodel.h"

class SocketModel;

QString SocketHelper::convertVectorValue(QString fromValue, SocketModel* from, SocketModel* to)
{
	int numFrom = getNumComponents(from->typeName);
	int numTo = getNumComponents(to->typeName);

	Q_ASSERT(numFrom != 0 && numTo != 0);

	QString suffix = ".";

	// float being casted to a vector
	//
	if (numFrom == 1 && numTo>1) {
		return QString("vec%1(%2)").arg(numTo).arg(fromValue);
	}

	if (numFrom > numTo) {
		// eg vec4 > vec2 = var.xy
		for (int i = 0; i<numTo; i++) {
			suffix += getVectorComponent(i);
		}

		return fromValue + suffix;
	}
	if (numFrom < numTo)
	{
		// eg vec2 > vec4 = var.xyyy
		for (int i = 0; i<numTo; i++) {
			suffix += getVectorComponent(qMin(i, numFrom - 1));
		}
		return fromValue + suffix;
	}

	return fromValue;

}

QString SocketHelper::getVectorComponent(int index)
{
	switch (index)
	{
	case 0: return "x";
	case 1: return "y";
	case 2: return "z";
	case 3: return "w";
	}

	return "x";
}

int SocketHelper::getNumComponents(QString valueType)
{
	if (valueType == "float")
		return 1;

	if (valueType == "vec2")
		return 2;

	if (valueType == "vec3")
		return 3;

	if (valueType == "vec4")
		return 4;

	return 0;
}
