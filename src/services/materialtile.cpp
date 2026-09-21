/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/materialtile.h"

#include "bridge/enginehost.h"
#include "jahshaka/engine/Engine.h"
#include "irisgl/core/logger.h"
#include "services/thumbnailrebuild.h"

namespace materialtile {

bool mint(Database *db, Project *project, const QString &materialGuid, const char *who)
{
    if (!db || materialGuid.isEmpty()) return false;
    const auto engine = EngineHost::instance().engine();
    const thumbrebuild::Outcome outcome =
        thumbrebuild::rebuildOne(db, project, materialGuid, engine);
    if (outcome.ok) return true;
    // THE ANSWER IS NOT DISCARDED ANY MORE — but the line is for the case
    // worth reading, not for the one that cannot draw at all. With NO engine,
    // or a headless one (--headless, --script, every document-only suite),
    // "no picture" is the correct and expected answer and the fallback tile is
    // the right tile: saying so once per minted material would be noise in
    // every script run. What this names is a render that COULD have happened
    // and did not — a REFUSED BORROW above all (the one thumbnail renderer was
    // busy with another render, so this material kept whatever tile it
    // inherited), which used to be invisible: every door threw the Outcome
    // away.
    if (engine && !engine->isHeadless())
        irisLog(QStringLiteral("%1: no tile rendered for %2 — %3")
                    .arg(QString::fromUtf8(who ? who : "a material"), materialGuid,
                         outcome.reason.isEmpty() ? QStringLiteral("no reason given")
                                                  : outcome.reason));
    return false;
}

}   // namespace materialtile
