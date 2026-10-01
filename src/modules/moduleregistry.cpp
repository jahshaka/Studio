/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/moduleregistry.h"

#include "modules/avatar/avatarmodule.h"
#include "modules/materials/materialsmodule.h"
#include "modules/publish/publishmodule.h"
#include "modules/vr/vrmodule.h"
#include "player/playermodule.h"

namespace moduleregistry {

QVector<StudioModule *> createAll()
{
    // VR (SPECS/VR_SPEC.md §4.6) has no page: the session's UI is the Player's
    // VR mode and the editor preview, both of which call the `vr.*` verbs it
    // registers. The Player space contributes VERBS only (verb-coverage audit
    // F1); its page is the shell's PlayerWidget. A module with no page still
    // gets a place in the loop.
    return { new MaterialsModule, new PublishModule, new AvatarModule, new PlayerModule,
             new VrModule };
}

}   // namespace moduleregistry
