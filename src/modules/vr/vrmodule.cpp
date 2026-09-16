/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/vr/vrmodule.h"

#include "bridge/enginehost.h"
#include "modules/vr/vrapi.h"
#include "scripting/scriptengine.h"
#include "viewport/enginerenderdriver.h"

void VrModule::registerApi(ScriptEngine &engine)
{
    engine.addModule(new VrApi(engine.scriptHost(), host));
}

void VrModule::shutdown()
{
    // A SESSION MUST NOT OUTLIVE THE SHELL. It holds a View on the editor's
    // scene, a second workspace on the viewport's target and a set of XR
    // swapchains; the engine's own destructor ends one too, but by then the
    // shell has already begun taking the scene apart.
    if (!host.engine) return;
    const auto e = host.engine->engine();
    if (!e) return;
    if (e->vrStatus().active) {
        if (host.engine->driver()) host.engine->driver()->setVrSessionActive(false);
        e->setVrMirrorView(nullptr);
        e->endVrSession();
    }
}
