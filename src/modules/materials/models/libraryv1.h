#pragma once

#include "library.h"

class LibraryV1 : public NodeLibrary
{
public:
	LibraryV1()
	{
		init();
	}

	void init();

	void initMath();

	void initTest();

	void initObject();

	void initVector();

	void initTexture();

	void initConstants();

	void initUtility();
};