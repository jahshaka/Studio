#pragma once

#include <QUuid>

class GuidHelper
{
public:

	// creates GUID without opening and closing braces
	static inline QString createGuid()
	{
		auto id = QUuid::createUuid();
		auto guid = id.toString().remove(0, 1);
		guid.chop(1);
		return guid;
	}
};