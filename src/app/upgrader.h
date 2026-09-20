/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef UPGRADER_H
#define UPGRADER_H

#include <QObject>

// THE SCHEMA CHECK, AND ONLY THAT (RESET-LIBRARY-1's fix round). The second
// member — `checkIfDeprecatedVersion`, a modal "confirm wipe" dialog that
// removed the whole projects root and the default AssetStore directory by
// hand — was DEAD: its one call site in main() had been commented out, and
// the reset it half-implemented is `app.resetLibrary` now
// (services/libraryreset.h), which knows what in those directories is the
// app's to delete. 114 lines removed with it, and nothing else changed.
class Upgrader : protected QObject
{
	Q_OBJECT

public:
	Upgrader() = default;
	void checkIfSchemaNeedsUpdating();
};

#endif // UPGRADER_H